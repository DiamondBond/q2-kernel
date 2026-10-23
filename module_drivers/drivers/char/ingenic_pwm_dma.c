/* linux/drivers/char/ingenic_pwm_dma.c
 *
 * Ingenic pwm_dma driver, use kernel char device framework
 *
 * Copyright (c) 2023 Ingenic
 * Author:renlichao <lichao.ren@ingenic.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * 驱动说明：
 * - 本驱动提供 /dev/ingenic_pwm_dma 字符设备，用于通过 PWM DMA 连续
 *   输出用户态上传的通用 PWM 波形数组。
 * - 驱动不绑定具体协议；协议编码由用户态完成，驱动只接收
 *   struct pwm_dma_waveform { low, high } 数组并按顺序写入 PWM_DR(channel)。
 * - 用户态 ABI 为：
 *     CONFIG      配置 ABI 版本、PWM 通道、分频、初始电平和空闲电平；
 *     write       上传 struct pwm_dma_waveform 数组；
 *     START       构建 DMA 描述符并启动 PWM DMA 输出；
 *     STOP        停止 PWM DMA，保留当前波形，状态回到 READY；
 *     GET_STATUS  查询状态、通道、波形数量、当前位置和最近错误；
 *     WAIT_DONE   阻塞等待传输结束，返回最终状态，用户态无需轮询；
 *     RESET       停止传输并释放 PWM、DMA 描述符和波形缓冲。
 * - 内部状态机为 IDLE -> CONFIGURED -> READY -> RUNNING，输出完成后进入
 *   DONE，STOP 后回到 READY，RESET/close 后回到 IDLE，资源错误进入 ERROR；
 *   DONE 状态下继续 START 会返回 -EINVAL，调用 STOP 或重新 write 后可再次 START。
 * - DMA 完成中断通过 dmaengine callback 调度 workqueue 收尾；workqueue
 *   基于 PWM_ON(channel) 的输出波形数量确认 cur_pos >= wave_count 后进入 DONE，
 *   避免 DMA 已写入 FIFO 但 PWM 尚未输出完时提前关闭 PWM。
 *   如果 PWM DMA FIFO underflow 或长时间无输出进展，则进入 ERROR 并唤醒
 *   WAIT_DONE，避免用户态永久阻塞，同时不限制正常大波形传输时长。
 * - PWM/PWM DMA 基础配置仍通过 pwm-ingenic-v3 的 pwm_config()、
 *   pwm_enable() 和 pwm_disable() 完成。现有 pwm-ingenic-v3 未提供
 *   SMC 启动阶段单独控制 DMA request 的公开接口，本驱动仅在 START/STOP
 *   和错误收尾路径中最小化读写 PWM_DRE，并在状态路径中读取 PWM_ON、清除
 *   PWM_DFS 和触发 PWM_DCFF；这些寄存器访问均复用 pwm-ingenic-v3 的
 *   chip iomem 和 chip mutex，避免硬编码基地址和无锁 RMW。
 * - prescale 使用 pwm-ingenic-v3 的 sys_prescale 语义，合法范围为 1..65536。
 * - DMA 描述符和波形缓冲使用 dma_alloc_coherent() 按设备实例分配；
 *   优先使用 DMA 控制器 device，缺少 DMA mask 时回退到本 platform device，
 *   避免全局描述符在多次启动或多实例场景下被覆盖。
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/pwm.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/scatterlist.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include <linux/bitops.h>
#include <linux/limits.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/wait.h>
#include <linux/ktime.h>
#include <linux/iopoll.h>

#include <pwm-ingenic-v3.h>
#include <ingenic_dma.h>

#define PWM_DMA_NAME "ingenic_pwm_dma"
#define PWM_DMA_IOC_MAGIC 'P'
#define PWM_DMA_ABI_VERSION 1
#define PWM_DMA_MAX_CHANNEL 15
#define PWM_DMA_REQ_BASE 0x2c
#define PWM_DMA_DEFAULT_DUTY_NS 5000
#define PWM_DMA_DEFAULT_PERIOD_NS 10000
#define PWM_DMA_MIN_PRESCALE 1
#define PWM_DMA_MAX_PRESCALE 65536
#define PWM_DMA_FINISH_CHECK_MS 1
#define PWM_DMA_PROGRESS_TIMEOUT_MS 1000
#define PWM_DMA_FIFO_FLUSH_TIMEOUT_US 1000
#define PWM_DMA_FLAG_LINK BIT(0)
#define PWM_DMA_ALLOWED_FLAGS PWM_DMA_FLAG_LINK

struct pwm_dma_waveform {
	__u16 low;
	__u16 high;
};

enum pwm_dma_state {
	PWM_DMA_STATE_IDLE = 0,
	PWM_DMA_STATE_CONFIGURED = 1,
	PWM_DMA_STATE_READY = 2,
	PWM_DMA_STATE_RUNNING = 3,
	PWM_DMA_STATE_ERROR = 4,
	PWM_DMA_STATE_DONE = 5,
};

struct pwm_dma_config {
	__u32 version;
	__u32 channel;
	__u32 prescale;
	__u32 init_level;
	__u32 idle_level;
	__u32 flags;
};

struct pwm_dma_status {
	__u32 state;
	__u32 channel;
	__u32 wave_count;
	__u32 cur_pos;
	__s32 last_error;
};

#define PWM_DMA_IOC_CONFIG     _IOW(PWM_DMA_IOC_MAGIC, 0x01, struct pwm_dma_config)
#define PWM_DMA_IOC_GET_CONFIG _IOR(PWM_DMA_IOC_MAGIC, 0x02, struct pwm_dma_config)
#define PWM_DMA_IOC_START      _IO(PWM_DMA_IOC_MAGIC,  0x03)
#define PWM_DMA_IOC_STOP       _IO(PWM_DMA_IOC_MAGIC,  0x04)
#define PWM_DMA_IOC_GET_STATUS _IOR(PWM_DMA_IOC_MAGIC, 0x05, struct pwm_dma_status)
#define PWM_DMA_IOC_RESET      _IO(PWM_DMA_IOC_MAGIC,  0x06)
#define PWM_DMA_IOC_WAIT_DONE  _IOR(PWM_DMA_IOC_MAGIC, 0x07, struct pwm_dma_status)

struct ingenic_pwm_dma {
	int major;
	int minor;
	dev_t dev_num;
	int nr_devs;
	struct class *class;
	struct cdev cdev;
	struct device *dev;
	struct device *dma_dev;
	struct platform_device *pdev;

	struct mutex lock;
	wait_queue_head_t wait;
	bool opened;
	enum pwm_dma_state state;
	int last_error;
	__u32 cur_pos;

	struct pwm_dma_config config;
	struct pwm_device *pwm;
	struct ingenic_pwm_chan *chan;
	struct dma_chan *dma_chan;

	struct scatterlist sg;
	struct pwm_dma_waveform *wave_buf;
	dma_addr_t wave_dma;
	size_t wave_count;
	size_t wave_size;
	ktime_t progress_deadline;
	__u32 last_progress_pos;
	struct work_struct dma_done_work;
	struct delayed_work finish_work;
};

static void pwm_dma_dma_done(void *data);

static int pwm_dma_req_type(unsigned int channel)
{
	if (channel > PWM_DMA_MAX_CHANNEL)
		return -EINVAL;

	return PWM_DMA_REQ_BASE + channel;
}

static void pwm_dma_set_error(struct ingenic_pwm_dma *pwm_dma, int err)
{
	pwm_dma->last_error = err;
	if (err) {
		pwm_dma->state = PWM_DMA_STATE_ERROR;
		wake_up_interruptible_all(&pwm_dma->wait);
	}
}

static void pwm_dma_prepare_dma_dev(struct device *dev)
{
	if (!dev->coherent_dma_mask)
		dev->coherent_dma_mask = DMA_BIT_MASK(32);
	if (!dev->dma_mask)
		dev->dma_mask = &dev->coherent_dma_mask;
}

static struct device *pwm_dma_get_dma_dev(struct ingenic_pwm_dma *pwm_dma)
{
	if (pwm_dma->dma_chan &&
	    pwm_dma->dma_chan->device &&
	    pwm_dma->dma_chan->device->dev &&
	    pwm_dma->dma_chan->device->dev->dma_mask &&
	    pwm_dma->dma_chan->device->dev->coherent_dma_mask)
		return pwm_dma->dma_chan->device->dev;

	return &pwm_dma->pdev->dev;
}

static bool pwm_dma_level_valid(__u32 level)
{
	return level == 0 || level == 1;
}

static struct ingenic_pwm_chip *pwm_dma_chip(struct ingenic_pwm_dma *pwm_dma)
{
	if (!pwm_dma->chan)
		return NULL;

	return pwm_dma->chan->chip;
}

static inline unsigned int pwm_dma_read(struct ingenic_pwm_chip *chip,
					unsigned int offset)
{
	return readl(chip->iomem + offset);
}

static inline void pwm_dma_write(struct ingenic_pwm_chip *chip,
				 unsigned int offset, unsigned int val)
{
	writel(val, chip->iomem + offset);
}

static __u32 pwm_dma_get_cur_pos(struct ingenic_pwm_dma *pwm_dma)
{
	struct ingenic_pwm_chip *chip = pwm_dma_chip(pwm_dma);

	if (!pwm_dma->pwm)
		return pwm_dma->cur_pos;
	if (!chip)
		return pwm_dma->cur_pos;

	/* PWM_ON(channel)：The Output waveform number，表示已输出的波形数量。 */
	mutex_lock(&chip->mutex);
	pwm_dma->cur_pos = pwm_dma_read(chip, PWM_ON(pwm_dma->config.channel));
	mutex_unlock(&chip->mutex);

	return pwm_dma->cur_pos;
}

static void pwm_dma_set_request(struct ingenic_pwm_dma *pwm_dma, bool enable)
{
	struct ingenic_pwm_chip *chip = pwm_dma_chip(pwm_dma);
	unsigned int mask = BIT(pwm_dma->config.channel);
	unsigned int val;

	if (!chip)
		return;

	mutex_lock(&chip->mutex);
	val = pwm_dma_read(chip, PWM_DRE);
	if (enable)
		val |= mask;
	else
		val &= ~mask;
	pwm_dma_write(chip, PWM_DRE, val);
	mutex_unlock(&chip->mutex);
}

static bool pwm_dma_get_underflow(struct ingenic_pwm_dma *pwm_dma, bool clear)
{
	struct ingenic_pwm_chip *chip = pwm_dma_chip(pwm_dma);
	unsigned int mask = BIT(pwm_dma->config.channel);
	unsigned int status;

	if (!chip)
		return false;

	mutex_lock(&chip->mutex);
	status = pwm_dma_read(chip, PWM_DFS) & mask;
	if (status && clear)
		pwm_dma_write(chip, PWM_DFS, status);
	mutex_unlock(&chip->mutex);

	return !!status;
}

static int pwm_dma_flush_fifo(struct ingenic_pwm_dma *pwm_dma)
{
	struct ingenic_pwm_chip *chip = pwm_dma_chip(pwm_dma);
	unsigned int mask = BIT(pwm_dma->config.channel);
	unsigned int val;
	int ret;

	if (!chip)
		return -ENODEV;

	mutex_lock(&chip->mutex);
	pwm_dma_write(chip, PWM_DCFF, mask);

	ret = readl_poll_timeout(chip->iomem + PWM_DCFF, val,
				 !(val & mask), 1,
				 PWM_DMA_FIFO_FLUSH_TIMEOUT_US);
	mutex_unlock(&chip->mutex);

	return ret;
}

static void pwm_dma_free_wave(struct ingenic_pwm_dma *pwm_dma)
{
	if (!pwm_dma->wave_buf)
		return;

	dma_free_coherent(pwm_dma->dma_dev, pwm_dma->wave_size,
			  pwm_dma->wave_buf, pwm_dma->wave_dma);
	pwm_dma->wave_buf = NULL;
	pwm_dma->wave_dma = 0;
	pwm_dma->wave_size = 0;
	pwm_dma->wave_count = 0;
	pwm_dma->cur_pos = 0;
	pwm_dma->progress_deadline = 0;
	pwm_dma->last_progress_pos = 0;
}

static void pwm_dma_cancel_work(struct ingenic_pwm_dma *pwm_dma)
{
	cancel_work_sync(&pwm_dma->dma_done_work);
	cancel_delayed_work_sync(&pwm_dma->finish_work);
}

static int pwm_dma_alloc_wave(struct ingenic_pwm_dma *pwm_dma,
			      const char __user *buf, size_t count)
{
	struct pwm_dma_waveform *wave_buf;
	dma_addr_t wave_dma;
	size_t wave_count;

	if (!buf || count == 0 || count % sizeof(struct pwm_dma_waveform))
		return -EINVAL;

	wave_count = count / sizeof(struct pwm_dma_waveform);
	if (wave_count > DTC_TC_MSK)
		return -EINVAL;

	wave_buf = dma_alloc_coherent(pwm_dma->dma_dev, count, &wave_dma,
				      GFP_KERNEL);
	if (!wave_buf)
		return -ENOMEM;

	if (copy_from_user(wave_buf, buf, count)) {
		dma_free_coherent(pwm_dma->dma_dev, count, wave_buf, wave_dma);
		return -EFAULT;
	}

	pwm_dma_free_wave(pwm_dma);

	pwm_dma->wave_buf = wave_buf;
	pwm_dma->wave_dma = wave_dma;
	pwm_dma->wave_size = count;
	pwm_dma->wave_count = wave_count;
	pwm_dma->cur_pos = 0;
	pwm_dma->state = PWM_DMA_STATE_READY;
	pwm_dma->last_error = 0;

	return 0;
}

static int pwm_dma_config_dmaengine(struct ingenic_pwm_dma *pwm_dma)
{
	struct ingenic_pwm_chip *chip = pwm_dma_chip(pwm_dma);
	struct dma_slave_config tx_config;

	if (!pwm_dma->wave_buf || !pwm_dma->wave_count)
		return -EINVAL;
	if (!chip)
		return -ENODEV;

	memset(&tx_config, 0, sizeof(tx_config));
	tx_config.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	tx_config.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	tx_config.dst_maxburst = 1;
	tx_config.src_maxburst = 1;
	tx_config.dst_addr = (dma_addr_t)(chip->phys +
					  PWM_DR(pwm_dma->config.channel));
	tx_config.slave_id = pwm_dma_req_type(pwm_dma->config.channel);
	tx_config.direction = DMA_MEM_TO_DEV;

	return dmaengine_slave_config(pwm_dma->dma_chan, &tx_config);
}

static int pwm_dma_start_dma(struct ingenic_pwm_dma *pwm_dma)
{
	struct dma_async_tx_descriptor *txdesc;
	dma_cookie_t cookie;

	if (!pwm_dma->dma_chan)
		return -EINVAL;

	sg_init_table(&pwm_dma->sg, 1);
	sg_dma_address(&pwm_dma->sg) = pwm_dma->wave_dma;
	sg_dma_len(&pwm_dma->sg) = pwm_dma->wave_size;

	txdesc = dmaengine_prep_slave_sg(pwm_dma->dma_chan, &pwm_dma->sg, 1,
					 DMA_MEM_TO_DEV,
					 DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!txdesc)
		return -EIO;

	txdesc->callback = pwm_dma_dma_done;
	txdesc->callback_param = pwm_dma;

	cookie = dmaengine_submit(txdesc);
	if (dma_submit_error(cookie))
		return dma_submit_error(cookie);

	dma_async_issue_pending(pwm_dma->dma_chan);

	return 0;
}

static bool pwm_dma_check_output_error_locked(struct ingenic_pwm_dma *pwm_dma,
					      __u32 cur_pos)
{
	if (pwm_dma_get_underflow(pwm_dma, true)) {
		pwm_dma_set_request(pwm_dma, false);
		dmaengine_terminate_sync(pwm_dma->dma_chan);
		if (pwm_dma->pwm)
			pwm_disable(pwm_dma->pwm);
		pwm_dma_set_error(pwm_dma, -EIO);
		return true;
	}

	if (cur_pos != pwm_dma->last_progress_pos) {
		pwm_dma->last_progress_pos = cur_pos;
		pwm_dma->progress_deadline = ktime_add_ms(ktime_get(),
			PWM_DMA_PROGRESS_TIMEOUT_MS);
		return false;
	}

	if (pwm_dma->progress_deadline &&
	    ktime_after(ktime_get(), pwm_dma->progress_deadline)) {
		pwm_dma_set_request(pwm_dma, false);
		dmaengine_terminate_sync(pwm_dma->dma_chan);
		if (pwm_dma->pwm)
			pwm_disable(pwm_dma->pwm);
		pwm_dma_set_error(pwm_dma, -ETIMEDOUT);
		return true;
	}

	return false;
}

static bool pwm_dma_finish_if_output_done_locked(struct ingenic_pwm_dma *pwm_dma)
{
	__u32 cur_pos;

	if (pwm_dma->state != PWM_DMA_STATE_RUNNING ||
	    pwm_dma->wave_count == 0)
		return false;

	cur_pos = pwm_dma_get_cur_pos(pwm_dma);
	if (pwm_dma_check_output_error_locked(pwm_dma, cur_pos))
		return true;

	if (cur_pos < pwm_dma->wave_count)
		return false;

	pwm_dma_set_request(pwm_dma, false);
	if (pwm_dma->pwm)
		pwm_disable(pwm_dma->pwm);
	pwm_dma->last_error = 0;
	pwm_dma->state = PWM_DMA_STATE_DONE;
	pwm_dma->progress_deadline = 0;
	wake_up_interruptible_all(&pwm_dma->wait);

	return true;
}

static void pwm_dma_finish_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct ingenic_pwm_dma *pwm_dma =
		container_of(dwork, struct ingenic_pwm_dma, finish_work);

	mutex_lock(&pwm_dma->lock);
	pwm_dma_finish_if_output_done_locked(pwm_dma);
	if (pwm_dma->state == PWM_DMA_STATE_RUNNING)
		schedule_delayed_work(&pwm_dma->finish_work,
			msecs_to_jiffies(PWM_DMA_FINISH_CHECK_MS));
	mutex_unlock(&pwm_dma->lock);
}

static void pwm_dma_dma_done_work(struct work_struct *work)
{
	struct ingenic_pwm_dma *pwm_dma =
		container_of(work, struct ingenic_pwm_dma, dma_done_work);

	mutex_lock(&pwm_dma->lock);
	pwm_dma_finish_if_output_done_locked(pwm_dma);
	if (pwm_dma->state == PWM_DMA_STATE_RUNNING)
		schedule_delayed_work(&pwm_dma->finish_work,
			msecs_to_jiffies(PWM_DMA_FINISH_CHECK_MS));
	mutex_unlock(&pwm_dma->lock);
}

static void pwm_dma_dma_done(void *data)
{
	struct ingenic_pwm_dma *pwm_dma = data;

	schedule_work(&pwm_dma->dma_done_work);
}

static void pwm_dma_release_pwm(struct ingenic_pwm_dma *pwm_dma)
{
	if (!pwm_dma->pwm)
		return;

	pwm_disable(pwm_dma->pwm);
	pwm_put(pwm_dma->pwm);
	pwm_dma->pwm = NULL;
	pwm_dma->chan = NULL;
	pwm_dma->dma_chan = NULL;
}

static int pwm_dma_stop_locked(struct ingenic_pwm_dma *pwm_dma)
{
	if (pwm_dma->pwm)
		pwm_dma_get_cur_pos(pwm_dma);

	if (pwm_dma->state == PWM_DMA_STATE_DONE) {
		if (pwm_dma->wave_buf)
			pwm_dma->state = PWM_DMA_STATE_READY;
		else
			pwm_dma->state = PWM_DMA_STATE_CONFIGURED;
		pwm_dma->progress_deadline = 0;
		wake_up_interruptible_all(&pwm_dma->wait);
		return 0;
	}

	if (pwm_dma->state != PWM_DMA_STATE_RUNNING)
		return 0;

	pwm_dma_set_request(pwm_dma, false);
	dmaengine_terminate_sync(pwm_dma->dma_chan);
	pwm_disable(pwm_dma->pwm);

	if (pwm_dma->wave_buf)
		pwm_dma->state = PWM_DMA_STATE_READY;
	else
		pwm_dma->state = PWM_DMA_STATE_CONFIGURED;
	pwm_dma->progress_deadline = 0;
	wake_up_interruptible_all(&pwm_dma->wait);

	return 0;
}

static void pwm_dma_reset_locked(struct ingenic_pwm_dma *pwm_dma)
{
	pwm_dma_stop_locked(pwm_dma);
	pwm_dma_free_wave(pwm_dma);
	pwm_dma_release_pwm(pwm_dma);
	memset(&pwm_dma->config, 0, sizeof(pwm_dma->config));
	pwm_dma->dma_dev = &pwm_dma->pdev->dev;
	pwm_dma->cur_pos = 0;
	pwm_dma->last_error = 0;
	pwm_dma->progress_deadline = 0;
	pwm_dma->last_progress_pos = 0;
	pwm_dma->state = PWM_DMA_STATE_IDLE;
}

static int pwm_dma_validate_config(const struct pwm_dma_config *config)
{
	if (config->version != PWM_DMA_ABI_VERSION)
		return -EINVAL;
	if (config->channel > PWM_DMA_MAX_CHANNEL)
		return -EINVAL;
	if (config->prescale < PWM_DMA_MIN_PRESCALE ||
	    config->prescale > PWM_DMA_MAX_PRESCALE)
		return -EINVAL;
	if (!pwm_dma_level_valid(config->init_level) ||
	    !pwm_dma_level_valid(config->idle_level))
		return -EINVAL;
	if (config->flags & ~PWM_DMA_ALLOWED_FLAGS)
		return -EINVAL;

	return 0;
}

static int pwm_dma_config_locked(struct ingenic_pwm_dma *pwm_dma,
				 const struct pwm_dma_config *config)
{
	struct pwm_device *pwm;
	struct ingenic_pwm_chan *chan;
	int ret;

	if (pwm_dma->state == PWM_DMA_STATE_RUNNING)
		return -EBUSY;

	ret = pwm_dma_validate_config(config);
	if (ret)
		return ret;

	pwm_dma_reset_locked(pwm_dma);

	pwm = pwm_request(config->channel, "pwm_dma_channel");
	if (IS_ERR(pwm)) {
		ret = PTR_ERR(pwm);
		pwm_dma_set_error(pwm_dma, ret);
		return ret;
	}

	chan = (struct ingenic_pwm_chan *)pwm_get_chip_data(pwm);
	if (!chan || !chan->dma_chan) {
		pwm_put(pwm);
		ret = -ENODEV;
		pwm_dma_set_error(pwm_dma, ret);
		return ret;
	}

	chan->mode = DMA_MODE_SMC;
	chan->init_level = config->init_level;
	chan->finish_level = config->idle_level;
	chan->sys_prescale = config->prescale;

	pwm_dma->pwm = pwm;
	pwm_dma->chan = chan;
	pwm_dma->dma_chan = chan->dma_chan;
	pwm_dma->dma_dev = pwm_dma_get_dma_dev(pwm_dma);
	pwm_dma->config = *config;

	ret = pwm_config(pwm_dma->pwm, PWM_DMA_DEFAULT_DUTY_NS,
			 PWM_DMA_DEFAULT_PERIOD_NS);
	if (ret < 0) {
		pwm_dma_release_pwm(pwm_dma);
		pwm_dma_set_error(pwm_dma, ret);
		return ret;
	}

	pwm_dma->state = PWM_DMA_STATE_CONFIGURED;
	pwm_dma->last_error = 0;
	pwm_dma->cur_pos = 0;

	return 0;
}

static int pwm_dma_start_locked(struct ingenic_pwm_dma *pwm_dma)
{
	int ret;

	if (pwm_dma->state != PWM_DMA_STATE_READY)
		return -EINVAL;
	if (!pwm_dma->pwm || !pwm_dma->chan || !pwm_dma->dma_chan ||
	    !pwm_dma->wave_buf || !pwm_dma->wave_count)
		return -EINVAL;

	ret = pwm_dma_config_dmaengine(pwm_dma);
	if (ret) {
		pwm_dma_set_error(pwm_dma, ret);
		return ret;
	}

	ret = pwm_dma_flush_fifo(pwm_dma);
	if (ret) {
		pwm_dma_set_error(pwm_dma, ret);
		return ret;
	}

	ret = pwm_enable(pwm_dma->pwm);
	if (ret < 0) {
		pwm_dma_set_error(pwm_dma, ret);
		return ret;
	}

	pwm_dma_set_request(pwm_dma, false);
	ret = pwm_dma_start_dma(pwm_dma);
	if (ret) {
		pwm_disable(pwm_dma->pwm);
		pwm_dma_set_error(pwm_dma, ret);
		return ret;
	}

	pwm_dma->cur_pos = 0;
	pwm_dma->last_error = 0;
	pwm_dma->state = PWM_DMA_STATE_RUNNING;
	pwm_dma->last_progress_pos = 0;
	pwm_dma->progress_deadline = ktime_add_ms(ktime_get(),
		PWM_DMA_PROGRESS_TIMEOUT_MS);
	pwm_dma_set_request(pwm_dma, true);
	schedule_delayed_work(&pwm_dma->finish_work,
			      msecs_to_jiffies(PWM_DMA_FINISH_CHECK_MS));

	return 0;
}

static void pwm_dma_fill_status(struct ingenic_pwm_dma *pwm_dma,
				struct pwm_dma_status *status)
{
	pwm_dma_finish_if_output_done_locked(pwm_dma);

	memset(status, 0, sizeof(*status));
	status->state = pwm_dma->state;
	status->channel = pwm_dma->config.channel;
	status->wave_count = pwm_dma->wave_count;
	status->cur_pos = pwm_dma_get_cur_pos(pwm_dma);
	status->last_error = pwm_dma->last_error;
}

static bool pwm_dma_wait_done_ready(struct ingenic_pwm_dma *pwm_dma)
{
	return READ_ONCE(pwm_dma->state) != PWM_DMA_STATE_RUNNING;
}

static long pwm_dma_wait_done_ioctl(struct ingenic_pwm_dma *pwm_dma,
				    unsigned long args)
{
	struct pwm_dma_status status;
	int ret;

	mutex_lock(&pwm_dma->lock);
	pwm_dma_finish_if_output_done_locked(pwm_dma);
	mutex_unlock(&pwm_dma->lock);

	ret = wait_event_interruptible(pwm_dma->wait,
				       pwm_dma_wait_done_ready(pwm_dma));
	if (ret)
		return ret;

	mutex_lock(&pwm_dma->lock);
	pwm_dma_fill_status(pwm_dma, &status);
	mutex_unlock(&pwm_dma->lock);

	if (copy_to_user((void __user *)args, &status, sizeof(status)))
		return -EFAULT;

	return 0;
}

static void set_private_data(struct file *filp, struct ingenic_pwm_dma *pwm_dma)
{
	filp->private_data = pwm_dma;
}

static struct ingenic_pwm_dma *get_private_data(struct file *filp)
{
	return filp->private_data;
}

static int ingenic_pwm_dma_open(struct inode *inode, struct file *filp)
{
	struct cdev *cdev = inode->i_cdev;
	struct ingenic_pwm_dma *pwm_dma =
		container_of(cdev, struct ingenic_pwm_dma, cdev);

	mutex_lock(&pwm_dma->lock);
	if (pwm_dma->opened) {
		mutex_unlock(&pwm_dma->lock);
		return -EBUSY;
	}

	pwm_dma_reset_locked(pwm_dma);
	pwm_dma->opened = true;
	set_private_data(filp, pwm_dma);
	mutex_unlock(&pwm_dma->lock);

	return 0;
}

static ssize_t ingenic_pwm_dma_read(struct file *filp, char __user *user_buf,
				    size_t count, loff_t *f_pos)
{
	return -ENOSYS;
}

static ssize_t ingenic_pwm_dma_write(struct file *filp,
				     const char __user *user_buf,
				     size_t count, loff_t *f_pos)
{
	struct ingenic_pwm_dma *pwm_dma = get_private_data(filp);
	int ret;

	if (!pwm_dma)
		return -ENODEV;

	mutex_lock(&pwm_dma->lock);
	if (pwm_dma->state == PWM_DMA_STATE_RUNNING) {
		ret = -EBUSY;
		goto out_unlock;
	}
	if (pwm_dma->state != PWM_DMA_STATE_CONFIGURED &&
	    pwm_dma->state != PWM_DMA_STATE_READY &&
	    pwm_dma->state != PWM_DMA_STATE_DONE) {
		ret = -EINVAL;
		goto out_unlock;
	}

	ret = pwm_dma_alloc_wave(pwm_dma, user_buf, count);
	if (ret) {
		if (ret == -ENOMEM)
			pwm_dma_set_error(pwm_dma, ret);
		goto out_unlock;
	}

	ret = count;

out_unlock:
	mutex_unlock(&pwm_dma->lock);
	return ret;
}

static long ingenic_pwm_dma_ioctl(struct file *filp, unsigned int cmd,
				  unsigned long args)
{
	struct ingenic_pwm_dma *pwm_dma = get_private_data(filp);
	struct pwm_dma_config config;
	struct pwm_dma_status status;
	int ret = 0;

	if (!pwm_dma)
		return -ENODEV;

	switch (cmd) {
	case PWM_DMA_IOC_CONFIG:
		if (copy_from_user(&config, (void __user *)args,
				   sizeof(config)))
			return -EFAULT;

		pwm_dma_cancel_work(pwm_dma);
		mutex_lock(&pwm_dma->lock);
		ret = pwm_dma_config_locked(pwm_dma, &config);
		mutex_unlock(&pwm_dma->lock);
		break;

	case PWM_DMA_IOC_GET_CONFIG:
		mutex_lock(&pwm_dma->lock);
		config = pwm_dma->config;
		mutex_unlock(&pwm_dma->lock);

		if (copy_to_user((void __user *)args, &config, sizeof(config)))
			ret = -EFAULT;
		break;

	case PWM_DMA_IOC_START:
		pwm_dma_cancel_work(pwm_dma);
		mutex_lock(&pwm_dma->lock);
		ret = pwm_dma_start_locked(pwm_dma);
		mutex_unlock(&pwm_dma->lock);
		break;

	case PWM_DMA_IOC_STOP:
		pwm_dma_cancel_work(pwm_dma);
		mutex_lock(&pwm_dma->lock);
		ret = pwm_dma_stop_locked(pwm_dma);
		mutex_unlock(&pwm_dma->lock);
		break;

	case PWM_DMA_IOC_GET_STATUS:
		mutex_lock(&pwm_dma->lock);
		pwm_dma_fill_status(pwm_dma, &status);
		mutex_unlock(&pwm_dma->lock);

		if (copy_to_user((void __user *)args, &status, sizeof(status)))
			ret = -EFAULT;
		break;

	case PWM_DMA_IOC_WAIT_DONE:
		ret = pwm_dma_wait_done_ioctl(pwm_dma, args);
		break;

	case PWM_DMA_IOC_RESET:
		pwm_dma_cancel_work(pwm_dma);
		mutex_lock(&pwm_dma->lock);
		pwm_dma_reset_locked(pwm_dma);
		mutex_unlock(&pwm_dma->lock);
		break;

	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

static int ingenic_pwm_dma_close(struct inode *inode, struct file *filp)
{
	struct ingenic_pwm_dma *pwm_dma = get_private_data(filp);

	if (!pwm_dma)
		return 0;

	pwm_dma_cancel_work(pwm_dma);
	mutex_lock(&pwm_dma->lock);
	pwm_dma_reset_locked(pwm_dma);
	pwm_dma->opened = false;
	set_private_data(filp, NULL);
	mutex_unlock(&pwm_dma->lock);

	return 0;
}

static const struct file_operations ingenic_pwm_dma_ops = {
	.owner = THIS_MODULE,
	.write = ingenic_pwm_dma_write,
	.read = ingenic_pwm_dma_read,
	.open = ingenic_pwm_dma_open,
	.release = ingenic_pwm_dma_close,
	.unlocked_ioctl = ingenic_pwm_dma_ioctl,
};

static int ingenic_pwm_dma_probe(struct platform_device *pdev)
{
	struct ingenic_pwm_dma *pwm_dma;
	dev_t devno;
	int ret;

	pwm_dma = devm_kzalloc(&pdev->dev, sizeof(*pwm_dma), GFP_KERNEL);
	if (!pwm_dma) {
		dev_err(&pdev->dev, "alloc pwm_dma failed!\n");
		return -ENOMEM;
	}

	pwm_dma->pdev = pdev;
	pwm_dma->dma_dev = &pdev->dev;
	pwm_dma->minor = 0;
	pwm_dma->nr_devs = 1;
	pwm_dma->state = PWM_DMA_STATE_IDLE;
	mutex_init(&pwm_dma->lock);
	init_waitqueue_head(&pwm_dma->wait);
	INIT_WORK(&pwm_dma->dma_done_work, pwm_dma_dma_done_work);
	INIT_DELAYED_WORK(&pwm_dma->finish_work, pwm_dma_finish_work);
	platform_set_drvdata(pdev, pwm_dma);

	pwm_dma_prepare_dma_dev(pwm_dma->dma_dev);

	ret = alloc_chrdev_region(&devno, pwm_dma->minor, pwm_dma->nr_devs,
				  PWM_DMA_NAME);
	if (ret) {
		dev_err(&pdev->dev, "alloc chrdev failed!\n");
		return ret;
	}

	pwm_dma->major = MAJOR(devno);
	pwm_dma->dev_num = MKDEV(pwm_dma->major, pwm_dma->minor);

	cdev_init(&pwm_dma->cdev, &ingenic_pwm_dma_ops);
	pwm_dma->cdev.owner = THIS_MODULE;
	ret = cdev_add(&pwm_dma->cdev, pwm_dma->dev_num, 1);
	if (ret) {
		dev_err(&pdev->dev, "cdev_add failed!\n");
		goto err_unregister_chrdev;
	}

	pwm_dma->class = class_create(THIS_MODULE, PWM_DMA_NAME);
	if (IS_ERR(pwm_dma->class)) {
		dev_err(&pdev->dev, "class_create failed!\n");
		ret = PTR_ERR(pwm_dma->class);
		goto err_cdev_del;
	}

	pwm_dma->dev = device_create(pwm_dma->class, NULL, pwm_dma->dev_num,
				     NULL, PWM_DMA_NAME);
	if (IS_ERR(pwm_dma->dev)) {
		dev_err(&pdev->dev, "device_create failed!\n");
		ret = PTR_ERR(pwm_dma->dev);
		goto err_class_destroy;
	}

	dev_info(&pdev->dev, "Ingenic pwm dma driver init successfully\n");
	return 0;

err_class_destroy:
	class_destroy(pwm_dma->class);
err_cdev_del:
	cdev_del(&pwm_dma->cdev);
err_unregister_chrdev:
	unregister_chrdev_region(pwm_dma->dev_num, pwm_dma->nr_devs);
	return ret;
}

static int ingenic_pwm_dma_remove(struct platform_device *pdev)
{
	struct ingenic_pwm_dma *pwm_dma = platform_get_drvdata(pdev);

	pwm_dma_cancel_work(pwm_dma);
	mutex_lock(&pwm_dma->lock);
	pwm_dma_reset_locked(pwm_dma);
	mutex_unlock(&pwm_dma->lock);

	device_destroy(pwm_dma->class, pwm_dma->dev_num);
	class_destroy(pwm_dma->class);
	cdev_del(&pwm_dma->cdev);
	unregister_chrdev_region(pwm_dma->dev_num, pwm_dma->nr_devs);

	return 0;
}

static const struct of_device_id ingenic_pwm_dma_match[] = {
	{ .compatible = "ingenic,x2600-pwm-dma", },
	{}
};
MODULE_DEVICE_TABLE(of, ingenic_pwm_dma_match);

static struct platform_driver ingenic_pwm_dma_driver = {
	.probe	= ingenic_pwm_dma_probe,
	.remove	= ingenic_pwm_dma_remove,
	.driver	= {
		.name = PWM_DMA_NAME,
		.owner = THIS_MODULE,
		.of_match_table	= ingenic_pwm_dma_match,
	},
};

static int __init ingenic_pwm_dma_init(void)
{
	return platform_driver_register(&ingenic_pwm_dma_driver);
}

static void __exit ingenic_pwm_dma_exit(void)
{
	platform_driver_unregister(&ingenic_pwm_dma_driver);
}

module_init(ingenic_pwm_dma_init);
module_exit(ingenic_pwm_dma_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Ingenic pwm dma waveform driver");
MODULE_AUTHOR("lichao.ren@ingenic.com");
