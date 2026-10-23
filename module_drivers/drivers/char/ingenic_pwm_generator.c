/* linux/drivers/char/ingenic_pwm_gen.c
 *
 * Ingenic pwm_gen driver, use kernel char device framework
 *
 * Copyright (c) 2024 Ingenic
 * Author:pengfuyu <fuyu.peng@ingenic.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <asm/uaccess.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/spinlock.h>
#include <linux/pwm.h>
#include <linux/wait.h>
#include <linux/semaphore.h>
#include <linux/dma-mapping.h>
#include <linux/circ_buf.h>
#include <linux/dev_printk.h>

#include <asm/div64.h>

#define BUFFER_NUM	2
#define TRANS_LEN	(10000)

static int g_prescale;

struct ingenic_pwm_chip;

enum pwm_gen_ioctl_cmd
{
	PWM_CHANNEL_CONFIG_AND_REQUEST,
	PWM_UPDATE_WAVEFORM,
	PWM_CONFIG_WAVEFORM,
	PWM_GENERATOR_START,
	PWM_GENERATOR_STOP,
};

enum pwm_status
{
	DISABLE,
	ENABLE,
};

struct pwn_chan_config
{
	int channel;
	int period_ns;
	int duty_ns;
	int number_of_waveforms;
	int offset_ratio;
	int init_level;
	int idle_level;
};

struct ingenic_pwm_config
{
	int num;
	struct pwn_chan_config info[0];
};

struct pwm_channel_info
{
	struct pwm_device *pwm;
	enum pwm_status status;
	dma_addr_t dma_addr;
	void *vir_addr;
	int mem_size;
	int offset;
	int buffer_num;
	int buffer_size;
	void *last_vir_addr;
	struct pwn_chan_config config;
};

struct ingenic_pwm_gen
{
	int major;
	int minor;
	dev_t dev_num;
	int nr_devs;
	struct class *class;
	struct cdev cdev;
	struct device *dev;
	struct platform_device *pdev;
	struct clk *clk_pwm;
	struct mutex mutex;

	struct ingenic_pwm_chip *ingenic_pwm_chip;
	struct pwm_channel_info channel_info[16];
};

extern void pwm_clk_config(struct ingenic_pwm_chip *chip, unsigned int channel, unsigned int prescale);
extern void pwm_enable_hw_multi_channel(struct ingenic_pwm_chip *chip, unsigned int channel_maps);
extern void pwm_disable_hw(struct ingenic_pwm_chip *chip, unsigned int channel);
extern void pwm_disable_hw_multi_channel(struct ingenic_pwm_chip *chip, unsigned int channel_maps);
extern void pwm_set_init_level(struct ingenic_pwm_chip *ingenic_pwm, unsigned int channel, unsigned int level);
extern void pwm_set_finish_level(struct ingenic_pwm_chip *ingenic_pwm, unsigned int channel, unsigned int level);
extern void pwm_waveform_high(struct ingenic_pwm_chip *ingenic_pwm, unsigned int channel, unsigned int high_num);
extern void pwm_waveform_low(struct ingenic_pwm_chip *ingenic_pwm, unsigned int channel, unsigned int low_num);
extern int pwm_read_waveform(struct ingenic_pwm_chip *ingenic_pwm, unsigned int channel);
extern void pwm_dma_enable_hw_multi_channel(struct ingenic_pwm_chip *ingenic_pwm, unsigned int channel_maps);
extern void pwm_dma_disable_hw(struct ingenic_pwm_chip *ingenic_pwm, unsigned int channel);
extern void pwm_dma_disable_hw_multi_channel(struct ingenic_pwm_chip *ingenic_pwm, unsigned int channel_maps);
extern void pwm_set_update_mode(struct ingenic_pwm_chip *ingenic_pwm, unsigned int channel, int mode_sel);
extern void pwm_dma_set_loop(struct ingenic_pwm_chip *ingenic_pwm, unsigned int channel);
extern void pwm_dma_trans_start_multi_channel(struct ingenic_pwm_chip *ingenic_pwm, unsigned int channel_maps);
extern void pwm_dma_set_mem_addr(struct ingenic_pwm_chip *ingenic_pwm, unsigned int channel, unsigned int mem_addr);
extern void pwm_dma_set_trans_length(struct ingenic_pwm_chip *ingenic_pwm, unsigned int channel, int tran_len);
extern void pwm_set_io_output_enable_multi_channel(struct ingenic_pwm_chip *ingenic_pwm, unsigned int channel_maps);

static void pwm_calculate_waveform(struct ingenic_pwm_gen *pwm_gen, int channel, int duty_ns, int period_ns)
{
	int i = 0;
	int tmp = 1;
	int duty = 0;
	int period = 0;
	int pwm_clk = 0;
	int counter_clk = 0;
	unsigned long long clk_ns = 1000000000ULL;
	struct ingenic_pwm_chip *ingenic_pwm = pwm_get_chip_data(pwm_gen->channel_info[channel].pwm);

	pwm_clk_config(ingenic_pwm, channel, g_prescale);
	pwm_clk = clk_get_rate(pwm_gen->clk_pwm);

	for (i = 0; i < g_prescale; i++)
		tmp = 2 * tmp;

	counter_clk = pwm_clk / tmp;
	do_div(clk_ns, counter_clk);

	period = (period_ns / (unsigned int)clk_ns);
	duty = duty_ns / (unsigned int)clk_ns;

	pwm_waveform_high(ingenic_pwm, channel, duty);
	pwm_waveform_low(ingenic_pwm, channel, (period - duty));
}

static int fill_waveform_buffer(int *buffer, int size, int waveform)
{
	int i = 0;
	if (buffer == NULL)
		return -1;

	for (i = 0; i < size; i++)
	{
		buffer[i] = waveform;
	}

	return 0;
}

static int general_channel_configuration(struct ingenic_pwm_gen *pwm_gen, int channel, struct pwn_chan_config config)
{
	int low = 0;
	int high = 0;
	int length = 0;
	int *data = NULL;
	int waveform = 0;
	void *dma_coherent;
	int buffer_size = 0;
	int offset_time = 0;
	dma_addr_t dma_coherent_handle;
	struct ingenic_pwm_chip *ingenic_pwm = pwm_get_chip_data(pwm_gen->channel_info[channel].pwm);

	pwm_calculate_waveform(pwm_gen, channel, config.duty_ns, config.period_ns);
	pwm_set_period(pwm_gen->channel_info[channel].pwm, config.period_ns);
	pwm_set_duty_cycle(pwm_gen->channel_info[channel].pwm, config.duty_ns);

	waveform = pwm_read_waveform(ingenic_pwm, channel);
	high = ((waveform & 0xffff0000) >> 16);
	low = (waveform & 0xffff);

	if ((void *)pwm_gen->channel_info[channel].dma_addr == NULL)
	{

		pwm_gen->channel_info[channel].buffer_num = BUFFER_NUM;
		pwm_gen->channel_info[channel].offset = 0;
		pwm_gen->channel_info[channel].buffer_size = (config.number_of_waveforms > TRANS_LEN ? config.number_of_waveforms : TRANS_LEN);
		buffer_size = pwm_gen->channel_info[channel].buffer_size;
		pwm_gen->channel_info[channel].mem_size = (buffer_size * sizeof(int) * BUFFER_NUM);
		dma_coherent = dma_alloc_coherent(pwm_gen->dev, pwm_gen->channel_info[channel].mem_size, &dma_coherent_handle, GFP_KERNEL);

		pwm_gen->channel_info[channel].dma_addr = dma_coherent_handle;
		pwm_gen->channel_info[channel].vir_addr = dma_coherent;
	}
	data = pwm_gen->channel_info[channel].vir_addr + pwm_gen->channel_info[channel].offset;

	buffer_size = pwm_gen->channel_info[channel].buffer_size;
	if (config.number_of_waveforms < TRANS_LEN)
	{
		if (config.number_of_waveforms == -1)
		{
			length = TRANS_LEN;
			pwm_dma_set_loop(ingenic_pwm, config.channel);
		}
		else
		{
			length = config.number_of_waveforms;
		}
	}
	else if (TRANS_LEN <= buffer_size && (config.number_of_waveforms < buffer_size || config.number_of_waveforms == buffer_size))
	{
		length = config.number_of_waveforms;
	}
	else
	{
		return -1;
	}
	fill_waveform_buffer(data, length, waveform);

	pwm_set_finish_level(ingenic_pwm, channel, config.idle_level);
	pwm_set_init_level(ingenic_pwm, channel, config.init_level);
	offset_time = 0;
	if (config.init_level == 1)
	{
		if (config.offset_ratio != 0)
		{
			offset_time = (high * config.offset_ratio) / 100;
		}

		data[0] = ((high - offset_time) << 16) | low;
	}
	else
	{
		if (config.offset_ratio != 0)
		{
			offset_time = (low * config.offset_ratio) / 100;
		}

		data[0] = (high << 16) | (low - offset_time);
	}
	dma_cache_wback_inv((unsigned int)data, length * sizeof(int));
	pwm_gen->channel_info[channel].last_vir_addr = data;

	pwm_dma_set_mem_addr(ingenic_pwm, channel, pwm_gen->channel_info[channel].dma_addr + pwm_gen->channel_info[channel].offset);
	pwm_dma_set_trans_length(ingenic_pwm, channel, length);

	pwm_gen->channel_info[channel].offset += (pwm_gen->channel_info[channel].mem_size / BUFFER_NUM);
	pwm_gen->channel_info[channel].offset %= pwm_gen->channel_info[channel].mem_size;

	// dma mode 1, cpu mode 0
	pwm_set_update_mode(ingenic_pwm, channel, 1);

	return 0;
}

static void set_private_data(struct file *filp, struct ingenic_pwm_gen *pwm_gen)
{
	filp->private_data = pwm_gen;
}

static struct ingenic_pwm_gen* get_private_data(struct file *filp)
{
	return filp->private_data;
}

static int ingenic_pwm_gen_open(struct inode *inode, struct file *filp)
{
	struct cdev *cdev = inode->i_cdev;
	struct ingenic_pwm_gen *pwm_gen = container_of(cdev, struct ingenic_pwm_gen, cdev);

	set_private_data(filp, pwm_gen);

	return 0;
}

static int ingenic_pwm_gen_read(struct file *filp, char *user_buf, size_t count, loff_t *f_pos)
{
	struct ingenic_pwm_gen *pwm_gen __attribute__((unused)) = get_private_data(filp);

	return count;
}

static int ingenic_pwm_gen_write(struct file *filp, const char *user_buf, size_t count, loff_t *f_pos)
{
	struct ingenic_pwm_gen *pwm_gen __attribute__((unused)) = get_private_data(filp);

	return 0;
}

static long ingenic_pwm_gen_ioctl(struct file *filp, unsigned int cmd, unsigned long args)
{
	int ret = 0;
	size_t i = 0;
	int index = 0;
	int channels = 0;
	int *data = NULL;
	int channel_bit = 0;
	struct pwm_device *pwm = NULL;
	struct pwn_chan_config config[16];
	struct ingenic_pwm_gen *pwm_gen = get_private_data(filp);
	struct ingenic_pwm_chip *ingenic_pwm = pwm_gen->ingenic_pwm_chip;

	switch (cmd)
	{
		case PWM_CHANNEL_CONFIG_AND_REQUEST:
			ret = copy_from_user(&channels, (void *)args, 4);
			if (ret)
			{
				dev_err(pwm_gen->dev, "get channel number from user failed!!!\n");
				return -1;
			}
			memset(config, 0, sizeof(struct pwn_chan_config) * 16);
			ret = copy_from_user(&config, (void *)(args + 4), sizeof(struct pwn_chan_config) * channels);
			if (ret)
			{
				dev_err(pwm_gen->dev, "get channel config from user failed!!!\n");
				return -1;
			}

			for (i = 0; i < channels; i++)
			{
				index = config[i].channel;
				memcpy(&pwm_gen->channel_info[index].config, &config[i], sizeof(struct pwn_chan_config));
				pwm = pwm_request(config[i].channel, "ingenic_pwm_generator");
				if (IS_ERR(pwm))
				{
					dev_err(pwm_gen->dev, "pwm_request failed!!!\n");
					return -1;
				}
				pwm_gen->channel_info[index].pwm = pwm;
				ret = general_channel_configuration(pwm_gen, index, config[i]);
				if (ret != 0)
				{
					dev_err(pwm_gen->dev, "general_channel_configuration failed!!!\n");
					return -1;
				}
				channel_bit |= (1 << index);
				if (pwm_gen->ingenic_pwm_chip == NULL)
				{
					pwm_gen->ingenic_pwm_chip = pwm_get_chip_data(pwm_gen->channel_info[index].pwm);
					ingenic_pwm = pwm_gen->ingenic_pwm_chip;
				}
			}
			pwm_set_io_output_enable_multi_channel(ingenic_pwm, channel_bit);
			break;
		case PWM_UPDATE_WAVEFORM:
			ret = copy_from_user(&channels, (void *)args, 4);
			if (ret)
			{
				dev_err(pwm_gen->dev, "%d: get channel number from user failed!!!\n", __LINE__);
				return -1;
			}
			memset(config, 0, sizeof(struct pwn_chan_config) * 16);
			ret = copy_from_user(&config, (void *)(args + 4), sizeof(struct pwn_chan_config) * channels);
			if (ret)
			{
				dev_err(pwm_gen->dev, "%d: get channel config from user failed!!!\n", __LINE__);
				return -1;
			}

			for (i = 0; i < channels; i++)
			{
				index = config[i].channel;
				ret = general_channel_configuration(pwm_gen, index, config[i]);
				if (ret != 0)
				{
					dev_err(pwm_gen->dev, "general_channel_configuration failed!!!\n");
					return -1;
				}
				channel_bit |= (1 << index);
			}
			pwm_dma_disable_hw_multi_channel(ingenic_pwm, channel_bit);
			pwm_disable_hw_multi_channel(ingenic_pwm, channel_bit);
			pwm_dma_enable_hw_multi_channel(ingenic_pwm, channel_bit);
			pwm_dma_trans_start_multi_channel(ingenic_pwm, channel_bit);
			pwm_enable_hw_multi_channel(ingenic_pwm, channel_bit);
			for (i = 0; i < 16; i++)
			{
				if (channel_bit & (1 << i))
				{
					data = pwm_gen->channel_info[i].last_vir_addr;
					data[0] = data[1];
					dma_cache_wback_inv((unsigned int)&data[0], sizeof(int));
					pwm_gen->channel_info[i].status = ENABLE;
				}
			}

			break;
		case PWM_CONFIG_WAVEFORM:
			ret = copy_from_user(&channels, (void *)args, 4);
			if (ret)
			{
				dev_err(pwm_gen->dev, "%d: get channel number from user failed!!!\n", __LINE__);
				return -1;
			}
			memset(config, 0, sizeof(struct pwn_chan_config) * 16);
			ret = copy_from_user(&config, (void *)(args + 4), sizeof(struct pwn_chan_config) * channels);
			if (ret)
			{
				dev_err(pwm_gen->dev, "%d: get channel config from user failed!!!\n", __LINE__);
				return -1;
			}

			for (i = 0; i < channels; i++)
			{
				index = config[i].channel;
				ret = general_channel_configuration(pwm_gen, index, config[i]);
				if (ret != 0)
				{
					dev_err(pwm_gen->dev, "general_channel_configuration failed!!!\n");
					return -1;
				}
			}
			break;
		case PWM_GENERATOR_START:
			channel_bit = args;
			if (ingenic_pwm == NULL)
			{
				return -1;
			}
			pwm_dma_enable_hw_multi_channel(ingenic_pwm, channel_bit);
			pwm_dma_trans_start_multi_channel(ingenic_pwm, channel_bit);
			mdelay(1);
			pwm_enable_hw_multi_channel(ingenic_pwm, channel_bit);
			for (i = 0; i < 16; i++)
			{
				if (channel_bit & (1 << i))
				{
					data = pwm_gen->channel_info[i].last_vir_addr;
					data[0] = data[1];
					dma_cache_wback_inv((unsigned int)&data[0], sizeof(int));
					pwm_gen->channel_info[i].status = ENABLE;
				}
			}

			break;
		case PWM_GENERATOR_STOP:
			channel_bit = args;
			if (ingenic_pwm == NULL)
			{
				return -1;
			}
			pwm_dma_disable_hw_multi_channel(ingenic_pwm, channel_bit);
			pwm_disable_hw_multi_channel(ingenic_pwm, channel_bit);
			for (i = 0; i < 16; i++)
			{
				if (channel_bit & (1 << i))
				{
					pwm_gen->channel_info[i].status = DISABLE;
				}
			}
			break;
	}

	return 0;
}


static int ingenic_pwm_gen_close(struct inode *inode, struct file *filp)
{
	int i = 0;
	struct ingenic_pwm_gen *pwm_gen = get_private_data(filp);
	struct ingenic_pwm_chip *ingenic_pwm = pwm_gen->ingenic_pwm_chip;

	for (i = 0; i < 16; i++)
	{
		if (pwm_gen->channel_info[i].status == ENABLE)
		{
			pwm_dma_disable_hw(ingenic_pwm, i);
			pwm_disable_hw(ingenic_pwm, i);
			pwm_gen->channel_info[i].status = DISABLE;
		}
		if (pwm_gen->channel_info[i].pwm != NULL)
		{
			pwm_put(pwm_gen->channel_info[i].pwm);
		}
		if ((void *)pwm_gen->channel_info[i].dma_addr != NULL)
		{
			dma_free_coherent(pwm_gen->dev, pwm_gen->channel_info[i].mem_size, pwm_gen->channel_info[i].vir_addr, pwm_gen->channel_info[i].dma_addr);
		}
	}

	memset(&pwm_gen->channel_info, 0, sizeof(struct pwm_channel_info) * 16);

	return 0;
}

static struct file_operations ingenic_pwm_gen_ops = {
	.owner = THIS_MODULE,
	.write = ingenic_pwm_gen_write,
	.read = ingenic_pwm_gen_read,
	.open = ingenic_pwm_gen_open,
	.release = ingenic_pwm_gen_close,
	.unlocked_ioctl = ingenic_pwm_gen_ioctl,
};

static int ingenic_pwm_gen_probe(struct platform_device *pdev)
{
	int ret = 0;
	dev_t devno;
	struct device_node *node;
	struct ingenic_pwm_gen *pwm_gen = NULL;

	pwm_gen = devm_kzalloc(&pdev->dev, sizeof(struct ingenic_pwm_gen), GFP_KERNEL);
	if (!pwm_gen){
		dev_err(&pdev->dev, "alloc pwm_gen failed!\n");
		return -ENOMEM;
	}
	pwm_gen->pdev = pdev;

	platform_set_drvdata(pdev, pwm_gen);

	node = of_find_node_by_name(NULL, "pwm_generator");
	if (node == NULL)
	{
        	dev_err(&pdev->dev, "get device node error!!!\n");
		goto __err0;
	}

	ret = of_property_read_u32_index(node, "pwm_prescale", 0, &g_prescale);
	if (ret != 0)
	{
		g_prescale = 0;
	}

	pwm_gen->clk_pwm = devm_clk_get(&pdev->dev, "div_pwm");
	if(IS_ERR(pwm_gen->clk_pwm)){
        	dev_err(&pdev->dev, "get pwm clk failed %ld\n", PTR_ERR(pwm_gen->clk_pwm));
		goto __err0;
	}

	mutex_init(&pwm_gen->mutex);

	pwm_gen->minor = 0;
	pwm_gen->nr_devs = 1;
	ret = alloc_chrdev_region(&devno, pwm_gen->minor, pwm_gen->nr_devs, "ingenic_pwm_gen");
	if (ret) {
		dev_err(&pdev->dev, "alloc chrdev failed!\n");
		goto __err0;
	}
	pwm_gen->major = MAJOR(devno);
	pwm_gen->dev_num = devno;

	cdev_init(&pwm_gen->cdev, &ingenic_pwm_gen_ops);
	pwm_gen->cdev.owner = THIS_MODULE;
	ret = cdev_add(&pwm_gen->cdev, pwm_gen->dev_num, 1);
	if (ret) {
		dev_err(&pdev->dev, "cdev_add failed!\n");
		goto __err1;
	}

	pwm_gen->class = class_create(THIS_MODULE, "ingenic_pwm_generator");
	if (IS_ERR(pwm_gen->class)) {
		dev_err(&pdev->dev, "class_create failed!\n");
		ret = PTR_ERR(pwm_gen->class);
		goto __err2;
	}

	pwm_gen->dev = device_create(pwm_gen->class, NULL, pwm_gen->dev_num, NULL, "ingenic_pwm_generator");
	if (IS_ERR(pwm_gen->dev)) {
		dev_err(&pdev->dev, "device_create failed!\n");
		ret = PTR_ERR(pwm_gen->dev);
		goto __err3;
	}

	pwm_gen->dev->coherent_dma_mask = pdev->dev.coherent_dma_mask;
	dev_info(&pdev->dev, "Ingenic pwm generator driver init successfully!\n");

	return 0;
__err3:
	class_destroy(pwm_gen->class);
__err2:
	cdev_del(&pwm_gen->cdev);
__err1:
	unregister_chrdev_region(pwm_gen->dev_num, pwm_gen->nr_devs);
__err0:
	kfree(pwm_gen);
	return ret;
}

static int ingenic_pwm_gen_remove(struct platform_device *pdev)
{
	struct ingenic_pwm_gen *pwm_gen = platform_get_drvdata(pdev);

	device_destroy(pwm_gen->class, pwm_gen->dev_num);
	class_destroy(pwm_gen->class);
	cdev_del(&pwm_gen->cdev);
	unregister_chrdev_region(pwm_gen->dev_num, pwm_gen->nr_devs);

	return 0;
}

static const struct of_device_id ingenic_pwm_gen_match[] = {
	{ .compatible = "ingenic,x2000-pwm-generator", },
	{}
};
MODULE_DEVICE_TABLE(of, ingenic_pwm_gen_match);

static struct platform_driver ingenic_pwm_gen_driver = {
	.probe          = ingenic_pwm_gen_probe,
	.remove         = ingenic_pwm_gen_remove,
	.driver		= {
		.name	= "ingenic_pwm_gen",
		.owner	= THIS_MODULE,
		.of_match_table	= ingenic_pwm_gen_match,
	},
};

module_platform_driver(ingenic_pwm_gen_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Ingenic pwm gen driver");
MODULE_AUTHOR("fuyu.peng@ingenic.com");

