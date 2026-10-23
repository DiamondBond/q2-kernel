#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>

#include "spi-ingenic.h"

#define SPI_INGENIC_FIFO_SIZE 128u
#define SPI_INGENIC_DMA_MEM_BUSWIDTH DMA_SLAVE_BUSWIDTH_4_BYTES
#define SPI_INGENIC_DMA_MAX_BURST_BYTES 128u
#define SPI_INGENIC_XFER_TIMEOUT (HZ)
#define SPI_INGENIC_POLL_TIMEOUT_US 1000000

struct jz_soc_info {
	u32 bits_per_word_mask;
	u8 flen_shift;
	bool has_trendian;

	unsigned int max_speed_hz;
	unsigned int max_native_cs;
};

struct ingenic_spi;

struct dma_state {
	struct sg_table *sg;
	enum dma_transfer_direction dir;
	unsigned int bits;
	unsigned int maxburst;
	unsigned int fifo_threshold;
	struct dma_chan *chan;
	struct spi_controller *ctlr;
	struct ingenic_spi *priv;
	bool rx_first;
};

struct ingenic_spi {
	const struct jz_soc_info *soc_info;
	struct clk *clk_gate;
	struct clk *clk_div;
	struct resource *mem_res;

	void __iomem *base;
	struct dma_state rx;
	struct dma_state tx;
	struct sg_table rx_sg[2];
	struct completion pio_done;
	struct completion rx_done;
	bool pio_irq_armed;
	int dma_error;
	int irq;
	bool hw_prepared;
};

static int
spi_ingenic_wait(struct ingenic_spi *priv, unsigned long mask, bool condition)
{
	return ingenic_ssi_wait_status(priv->base, mask, condition,
					      SPI_INGENIC_POLL_TIMEOUT_US);
}

static void spi_ingenic_set_cs(struct spi_device *spi, bool disable)
{
	struct ingenic_spi *priv = spi_controller_get_devdata(spi->controller);

	if (disable) {
		ingenic_ssi_clear_bits(priv->base, SSI_CR1, SSI_CR1_UNFIN);
		ingenic_ssi_clear_bits(priv->base, SSI_SR,
				       SSI_SR_UNDR | SSI_SR_OVER);

		spi_ingenic_wait(priv, SSI_SR_END, true);
	} else {
		ingenic_ssi_set_bits(priv->base, SSI_CR1, SSI_CR1_UNFIN);
	}

	ingenic_ssi_set_bits(priv->base, SSI_CR0,
			    SSI_CR0_RFLUSH | SSI_CR0_TFLUSH);
}

static int spi_ingenic_prepare_transfer(struct ingenic_spi *priv,
					struct spi_device *spi,
					struct spi_transfer *xfer)
{
	unsigned long clk_hz = clk_get_rate(priv->clk_gate);
	u32 cdiv, speed_hz = xfer->speed_hz ?: spi->max_speed_hz,
		  bits_per_word = xfer->bits_per_word ?: spi->bits_per_word;

	if (bits_per_word < 2 || bits_per_word > 32 ||
	    !(priv->soc_info->bits_per_word_mask &
	      SPI_BPW_MASK(bits_per_word)))
		return -EINVAL;

	/* 每次传输均从干净的 SSI 状态开始。 */
	ingenic_ssi_clear_bits(priv->base, SSI_CR0, SSI_CR0_SSIE);
	ingenic_ssi_clear_bits(priv->base, SSI_SR, SSI_SR_UNDR | SSI_SR_OVER);
	ingenic_ssi_set_bits(priv->base, SSI_CR0, SSI_CR0_RFLUSH | SSI_CR0_TFLUSH);
	ingenic_ssi_writel(priv->base, SSI_SR, 0);

	cdiv = clk_hz / (speed_hz * 2);
	cdiv = clamp(cdiv, 1u, 0x100u) - 1;

	ingenic_ssi_writel(priv->base, SSI_GR, cdiv);
	ingenic_ssi_set_flen(priv->base, priv->soc_info->flen_shift,
			     bits_per_word);
	ingenic_ssi_set_bits(priv->base, SSI_CR0, SSI_CR0_SSIE);

	return 0;
}

static enum dma_slave_buswidth
spi_ingenic_dma_ssi_buswidth(unsigned int bits)
{
	if (bits <= 8)
		return DMA_SLAVE_BUSWIDTH_1_BYTE;
	if (bits <= 16)
		return DMA_SLAVE_BUSWIDTH_2_BYTES;
	return DMA_SLAVE_BUSWIDTH_4_BYTES;
}

static unsigned int spi_ingenic_dma_burst_bytes(unsigned int bits)
{
	return bits <= 8 ? 64 : SPI_INGENIC_DMA_MAX_BURST_BYTES;
}

static unsigned int spi_ingenic_dma_threshold(unsigned int bits,
					      unsigned int burst_bytes)
{
	return burst_bytes / DIV_ROUND_UP(bits, 8);
}

static void spi_ingenic_set_rx_threshold(struct ingenic_spi *priv,
					 unsigned int entries)
{
	ingenic_ssi_set_rx_threshold(priv->base, entries);
}

static struct dma_async_tx_descriptor *
spi_ingenic_prepare_dma(struct dma_state *dma,
			struct sg_table *sg)
{
	struct ingenic_spi *priv = spi_controller_get_devdata(dma->ctlr);
	enum dma_slave_buswidth ssi_width;
	struct dma_slave_config cfg = {
		.direction = dma->dir,
		.src_addr = priv->mem_res->start + SSI_DR,
		.dst_addr = priv->mem_res->start + SSI_DR,
	};
	struct dma_async_tx_descriptor *desc;
	int ret;

	ssi_width = spi_ingenic_dma_ssi_buswidth(dma->bits);
	if (dma->dir == DMA_DEV_TO_MEM) {
		cfg.src_addr_width = ssi_width;
		cfg.dst_addr_width = SPI_INGENIC_DMA_MEM_BUSWIDTH;
		cfg.src_maxburst = dma->maxburst;
		cfg.dst_maxburst = 1;

		spi_ingenic_set_rx_threshold(priv, dma->fifo_threshold);

	} else if (dma->dir == DMA_MEM_TO_DEV) {
		cfg.src_addr_width = SPI_INGENIC_DMA_MEM_BUSWIDTH;
		cfg.dst_addr_width = ssi_width;
		cfg.src_maxburst = 1;
		cfg.dst_maxburst = dma->maxburst;
		ingenic_ssi_set_tx_threshold(priv->base,
					    SPI_INGENIC_FIFO_SIZE -
					    dma->fifo_threshold);

	} else {
		return NULL;
	}

	cfg.slave_id = 0xffffffff;
	ret = dmaengine_slave_config(dma->chan, &cfg);
	if (ret)
		return ERR_PTR(ret);

	desc = dmaengine_prep_slave_sg(dma->chan,
				       sg->sgl,
				       sg->nents,
				       dma->dir,
				       DMA_PREP_INTERRUPT);
	if (!desc)
		return ERR_PTR(-ENOMEM);

	return desc;
}

static int spi_ingenic_dma_state_start(struct dma_state *dma);

static void spi_ingenic_rx_dma_cb(void *data)
{
	struct dma_state *dma = data;
	struct ingenic_spi *priv = dma->priv;
	int ret;

	if (dma->rx_first) {
		dma->rx_first = false;
		dma->sg = &priv->rx_sg[1];
		if (dma->sg->nents) {
			/* 尾部数据按一个 SSI 条目一次搬运。 */
			dma->maxburst = 1;
			dma->fifo_threshold = 0;
			ret = spi_ingenic_dma_state_start(dma);
			if (ret) {
				priv->dma_error = ret;
				dma->sg = NULL;
				complete(&priv->rx_done);
			}
		} else {
			dma->sg = NULL;
			complete(&priv->rx_done);
		}
		return;
	}

	dma->sg = NULL;
	complete(&priv->rx_done);
}

static void spi_ingenic_tx_dma_cb(void *data)
{
	/* 保持 callback 非空，使 PDMA 完成中断回收 TX 描述符状态。 */
	(void)data;
}

static int spi_ingenic_dma_state_start(struct dma_state *dma)
{
	struct dma_async_tx_descriptor *desc;
	dma_cookie_t cookie;

	if (!dma->sg || !dma->sg->nents)
		return -EINVAL;

	desc = spi_ingenic_prepare_dma(dma, dma->sg);
	if (IS_ERR(desc))
		return PTR_ERR(desc);

	if (dma->dir == DMA_DEV_TO_MEM) {
		desc->callback = spi_ingenic_rx_dma_cb;
		desc->callback_param = dma;
	} else {
		desc->callback = spi_ingenic_tx_dma_cb;
		desc->callback_param = dma;
	}
	cookie = dmaengine_submit(desc);
	if (dma_submit_error(cookie)) {
		dmaengine_desc_free(desc);
		return -EIO;
	}

	dma_async_issue_pending(dma->chan);
	return 0;
}

/* 克隆已映射的 SG 区间而不重新映射，DMA 地址保持有效。 */
static int spi_ingenic_sg_slice(struct sg_table *dst, struct sg_table *src,
				unsigned int offset, unsigned int length)
{
	struct scatterlist *in, *out;
	unsigned int in_offset = offset, left = length;
	int i, nents = 0;
	bool found = false;

	for_each_sg(src->sgl, in, src->nents, i) {
		unsigned int len = sg_dma_len(in);

		if (in_offset >= len) {
			in_offset -= len;
			continue;
		}
		nents++;
		if (left <= len - in_offset) {
			found = true;
			break;
		}
		left -= len - in_offset;
		in_offset = 0;
	}
	if (!nents || !found)
		return -EINVAL;

	if (sg_alloc_table(dst, nents, GFP_KERNEL))
		return -ENOMEM;

	in_offset = offset;
	left = length;
	out = dst->sgl;
	for_each_sg(src->sgl, in, src->nents, i) {
		unsigned int len = sg_dma_len(in);
		unsigned int part;

		if (in_offset >= len) {
			in_offset -= len;
			continue;
		}
		part = min(left, len - in_offset);
		sg_set_page(out, sg_page(in), part, in->offset + in_offset);
		sg_dma_address(out) = sg_dma_address(in) + in_offset;
		sg_dma_len(out) = part;
		left -= part;
		if (!left)
			break;
		in_offset = 0;
		out = sg_next(out);
	}

	return 0;
}

static void spi_ingenic_release_rx_sg(struct ingenic_spi *priv)
{
	if (priv->rx_sg[0].sgl)
		sg_free_table(&priv->rx_sg[0]);
	if (priv->rx_sg[1].sgl)
		sg_free_table(&priv->rx_sg[1]);
	memset(priv->rx_sg, 0, sizeof(priv->rx_sg));
}

static int spi_ingenic_dma_tx(struct ingenic_spi *priv,
			      struct spi_controller *ctlr,
			      struct spi_transfer *xfer,
			      unsigned int bits)
{
	unsigned int burst_bytes = spi_ingenic_dma_burst_bytes(bits);
	unsigned int desc0_len = round_down(xfer->len, burst_bytes);
	unsigned int desc1_len = xfer->len - desc0_len;
	unsigned int threshold = spi_ingenic_dma_threshold(bits, burst_bytes);
	int ret;

	if (!xfer->len || !desc0_len)
		return -EINVAL;

	// 冗余清除上次申请的临时sg
	spi_ingenic_release_rx_sg(priv);
	ret = spi_ingenic_sg_slice(&priv->rx_sg[0], &xfer->rx_sg, 0,
				   desc0_len);
	if (ret)
		return ret;
	if (desc1_len) {
		ret = spi_ingenic_sg_slice(&priv->rx_sg[1], &xfer->rx_sg,
					   desc0_len, desc1_len);
		if (ret)
			goto err_free_rx_sg;
	}

	reinit_completion(&priv->rx_done);
	priv->dma_error = 0;

	priv->rx.sg = &priv->rx_sg[0];
	priv->rx.ctlr = ctlr;
	priv->rx.priv = priv;
	priv->rx.chan = ctlr->dma_rx;
	priv->rx.dir = DMA_DEV_TO_MEM;
	priv->rx.bits = bits;
	priv->rx.maxburst = threshold;
	priv->rx.fifo_threshold = threshold;
	priv->rx.rx_first = true;

	ret = spi_ingenic_dma_state_start(&priv->rx);
	if (ret)
		goto err_free_rx_sg;

	priv->tx.sg = &xfer->tx_sg;
	priv->tx.ctlr = ctlr;
	priv->tx.priv = priv;
	priv->tx.chan = ctlr->dma_tx;
	priv->tx.dir = DMA_MEM_TO_DEV;
	priv->tx.bits = bits;
	priv->tx.maxburst = threshold;
	priv->tx.fifo_threshold = threshold;

	ret = spi_ingenic_dma_state_start(&priv->tx);
	if (ret) {
		dmaengine_terminate_sync(ctlr->dma_rx);
		priv->rx.sg = NULL;
		priv->tx.sg = NULL;
		goto err_free_rx_sg;
	}
	/* 最后一个 RX DMA 描述符完成即表示 TX 已完成。 */
	priv->tx.sg = NULL;

	/*
	 * desc0 回调将 desc1 配置为单条目 burst 且 RTRG = 0。desc1 完成时，
	 * 全部 TX 数据已时钟输出，完整 RX 数据也已搬运完毕。
	 */
	if (desc1_len) {
		ret = wait_for_completion_interruptible_timeout(&priv->rx_done,
							SPI_INGENIC_XFER_TIMEOUT);
		if (ret <= 0) {
			if (!ret)
				ret = -ETIMEDOUT;
			goto err_terminate_dma;
		}
	} else {
		ret = wait_for_completion_interruptible_timeout(&priv->rx_done,
							SPI_INGENIC_XFER_TIMEOUT);
		if (ret <= 0) {
			if (!ret)
				ret = -ETIMEDOUT;
		goto err_terminate_dma;
	}
	}
	ret = priv->dma_error;
	priv->rx.sg = NULL;
	spi_ingenic_release_rx_sg(priv);
	return ret;

err_terminate_dma:
	dmaengine_terminate_sync(ctlr->dma_tx);
	dmaengine_terminate_sync(ctlr->dma_rx);
	priv->tx.sg = NULL;
	priv->rx.sg = NULL;
err_free_rx_sg:
	priv->rx.sg = NULL;
	priv->tx.sg = NULL;
	spi_ingenic_release_rx_sg(priv);
	return ret;
}

static irqreturn_t spi_ingenic_irq(int irq, void *data)
{
	struct ingenic_spi *priv = data;
	unsigned int status;

	status = ingenic_ssi_readl(priv->base, SSI_SR);
	if (!priv->pio_irq_armed || !(status & SSI_SR_RFHF))
		return IRQ_NONE;

	priv->pio_irq_armed = false;
	ingenic_ssi_clear_bits(priv->base, SSI_CR0, SSI_CR0_RIE);
	complete(&priv->pio_done);
	return IRQ_HANDLED;
}

static int spi_ingenic_pio_rx(struct ingenic_spi *priv, u8 *rx,
			      unsigned int frame_bytes, unsigned int entries,
			      unsigned long deadline)
{
	unsigned int i, value;
	int ret;

	for (i = 0; i < entries; i++) {
		unsigned long timeout_us;

		// 防止计时回绕
		if (time_after_eq(jiffies, deadline))
			return -ETIMEDOUT;

		timeout_us = jiffies_to_usecs(deadline - jiffies);
		ret = ingenic_ssi_wait_status(priv->base, SSI_SR_RFE, false,
					      max_t(unsigned long, timeout_us, 1));
		if (ret)
			return ret;

		value = ingenic_ssi_readl(priv->base, SSI_DR);
		if (rx)
			memcpy(rx + i * frame_bytes, &value, frame_bytes);
	}

	return 0;
}

static int spi_ingenic_pio_tx(struct ingenic_spi *priv,
				struct spi_transfer *xfer, unsigned int bits)
{
	unsigned int frame_bytes = DIV_ROUND_UP(bits, 8);
	unsigned int entries = xfer->len / frame_bytes;
	unsigned int threshold = round_down(entries, 8);
	const u8 *tx = xfer->tx_buf;
	u8 *rx = xfer->rx_buf;
	unsigned long deadline;
	unsigned int i, value;
	long wait_ret;

	if (!entries || entries >= SPI_INGENIC_FIFO_SIZE)
		return -EINVAL;

	spi_ingenic_set_rx_threshold(priv, threshold);
	if (threshold) {
		reinit_completion(&priv->pio_done);
		priv->pio_irq_armed = true;
		ingenic_ssi_set_bits(priv->base, SSI_CR0, SSI_CR0_RIE);
	}

	/*
	 * 在写入第一条 TX FIFO 数据前使能 RIE。FIFO 填充过程中可能已满足阈值，
	 * SSI 不保证在阈值已跨越后再使能 RIE 时重新产生中断。
	 */
	for (i = 0; i < entries; i++) {
		value = 0;
		if (tx)
			memcpy(&value, tx + i * frame_bytes, frame_bytes);
		ingenic_ssi_writel(priv->base, SSI_DR, value);
	}

	if (threshold) {
		wait_ret = wait_for_completion_interruptible_timeout(&priv->pio_done,
							SPI_INGENIC_XFER_TIMEOUT);
		if (wait_ret <= 0) {
			priv->pio_irq_armed = false;
			ingenic_ssi_clear_bits(priv->base, SSI_CR0, SSI_CR0_RIE);
			return wait_ret ? wait_ret : -ETIMEDOUT;
		}
	}

	deadline = jiffies + SPI_INGENIC_XFER_TIMEOUT;

	/* RIE 表明这些阈值条目已经位于 RX FIFO 中。 */
	if (threshold) {
		for (i = 0; i < threshold; i++) {
			value = ingenic_ssi_readl(priv->base, SSI_DR);
			if (rx)
				memcpy(rx + i * frame_bytes, &value, frame_bytes);
		}
	}

	/* 仅轮询 RX 阈值之后发送的剩余条目。 */
	return spi_ingenic_pio_rx(priv,
				  rx ? rx + threshold * frame_bytes : NULL,
				  frame_bytes, entries - threshold, deadline);
}

static int spi_ingenic_transfer_one(struct spi_controller *ctlr,
				    struct spi_device *spi,
				    struct spi_transfer *xfer)
{
	struct ingenic_spi *priv = spi_controller_get_devdata(ctlr);
	unsigned int bits = xfer->bits_per_word ?: spi->bits_per_word;
	unsigned int frame_bytes = DIV_ROUND_UP(bits, 8);
	unsigned int entries = xfer->len / frame_bytes;
	bool can_dma = ctlr->can_dma && ctlr->can_dma(ctlr, spi, xfer);
	int ret;

	if (xfer->len % frame_bytes)
		return -EINVAL;

	ret = spi_ingenic_prepare_transfer(priv, spi, xfer);
	if (ret)
		return ret;

	if (entries < SPI_INGENIC_FIFO_SIZE)
		return spi_ingenic_pio_tx(priv, xfer, bits);

	if (ctlr->cur_msg_mapped && can_dma) {
		if (priv->rx.sg != NULL) {
			dev_err(&spi->dev,
				"rx dma transfer not complete! nents:%d\n",
				priv->rx.sg->nents);
		}
		if (priv->tx.sg != NULL) {
			dev_err(&spi->dev,
				"tx dma transfer not complete! nents:%d\n",
				priv->tx.sg->nents);
		}

		return spi_ingenic_dma_tx(priv, ctlr, xfer, bits);
	}

	return -ENODEV;
}

static inline void set_spi_frame_interval(struct ingenic_spi *spi, unsigned int interval)
{
	u32 tmp;
	tmp = ingenic_ssi_readl(spi->base, SSI_ITR);
	tmp &= ~GENMASK(14, 0);
	tmp |= interval & GENMASK(14, 0);
	ingenic_ssi_writel(spi->base, SSI_ITR, tmp);
}

static ssize_t frame_interval_show(struct device *dev,
					   struct device_attribute *attr, char *buf)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct ingenic_spi *priv = spi_controller_get_devdata(ctlr);
	int ret;
	u32 interval;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	interval = ingenic_ssi_readl(priv->base, SSI_ITR) & GENMASK(14, 0);
	pm_runtime_put_autosuspend(dev);

	return sysfs_emit(buf, "%u\n", interval);
}

static ssize_t frame_interval_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct ingenic_spi *priv = spi_controller_get_devdata(ctlr);
	unsigned int interval;
	int ret;

	ret = kstrtouint(buf, 0, &interval);
	if (ret)
		return ret;
	if (interval > GENMASK(14, 0))
		return -ERANGE;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	set_spi_frame_interval(priv, interval);
	pm_runtime_put_autosuspend(dev);

	return count;
}

static DEVICE_ATTR_RW(frame_interval);

static struct attribute *spi_ingenic_attrs[] = {
	&dev_attr_frame_interval.attr,
	NULL,
};

static const struct attribute_group spi_ingenic_attr_group = {
	.attrs = spi_ingenic_attrs,
};

static int spi_ingenic_prepare_message(struct spi_controller *ctlr,
				       struct spi_message *message)
{
	struct ingenic_spi *priv = spi_controller_get_devdata(ctlr);
	struct spi_device *spi = message->spi;
	unsigned int cs = SSI_CR1_FRMHL << spi_get_chipselect(spi, 0);
	unsigned int ssicr0_mask = SSI_CR0_LOOP | SSI_CR0_FSEL;
	unsigned int ssicr1_mask = SSI_CR1_PHA | SSI_CR1_POL | cs;
	unsigned int ssicr0 = 0, ssicr1 = 0;

	if (priv->soc_info->has_trendian) {
		ssicr0_mask |= SSI_CR0_RENDIAN_LSB | SSI_CR0_TENDIAN_LSB;

		if (spi->mode & SPI_LSB_FIRST)
			ssicr0 |=
				SSI_CR0_RENDIAN_LSB | SSI_CR0_TENDIAN_LSB;
	} else {
		ssicr1_mask |= SSI_CR1_LFST;

		if (spi->mode & SPI_LSB_FIRST)
			ssicr1 |= SSI_CR1_LFST;
	}

	if (spi->mode & SPI_LOOP) {
		ssicr0 |= SSI_CR0_LOOP;
	}
	if (spi_get_chipselect(spi, 0))
		ssicr0 |= SSI_CR0_FSEL;

	if (spi->mode & SPI_CPHA)
		ssicr1 |= SSI_CR1_PHA;
	if (spi->mode & SPI_CPOL)
		ssicr1 |= SSI_CR1_POL;
	if (spi->mode & SPI_CS_HIGH)
		ssicr1 |= cs;

	ingenic_ssi_update_bits(priv->base, SSI_CR0, ssicr0_mask, ssicr0);
	ingenic_ssi_update_bits(priv->base, SSI_CR1, ssicr1_mask, ssicr1);

	return 0;
}

static int spi_ingenic_prepare_hardware(struct spi_controller *ctlr)
{
	struct ingenic_spi *priv = spi_controller_get_devdata(ctlr);
	int ret;

	if (!priv->hw_prepared) {
		ret = pm_runtime_resume_and_get(ctlr->dev.parent);
		if (ret)
			return ret;

		priv->hw_prepared = true;

	/* 每条传输特有的状态在 prepare_transfer() 中清除。 */
	}

	return 0;
}

static int spi_ingenic_unprepare_hardware(struct spi_controller *ctlr)
{
	struct ingenic_spi *priv = spi_controller_get_devdata(ctlr);

	/*
	 * 当 message 以 cs_change 结束时，SPI core 会保持 last_cs_enable。
	 * CS 仍有效时不能停止 SSI 引擎。
	 */
	if (ctlr->last_cs_enable)
		return 0;

	if (!priv->hw_prepared)
		return 0;

	pm_runtime_put_autosuspend(ctlr->dev.parent);
	priv->hw_prepared = false;

	return 0;
}

static int spi_ingenic_runtime_resume(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct ingenic_spi *priv = spi_controller_get_devdata(ctlr);
	int ret;

	ret = clk_prepare_enable(priv->clk_gate);
	if (ret)
		return ret;

	ingenic_ssi_writel(priv->base, SSI_CR0, SSI_CR0_EACLRUN);
	ingenic_ssi_writel(priv->base, SSI_CR1, 0);
	ingenic_ssi_writel(priv->base, SSI_SR, 0);
	ingenic_ssi_set_bits(priv->base, SSI_CR0, SSI_CR0_SSIE);
	return 0;
}

static int spi_ingenic_runtime_suspend(struct device *dev)
{
	struct spi_controller *ctlr = dev_get_drvdata(dev);
	struct ingenic_spi *priv = spi_controller_get_devdata(ctlr);

	ingenic_ssi_clear_bits(priv->base, SSI_CR0, SSI_CR0_SSIE);
	clk_disable_unprepare(priv->clk_gate);
	return 0;
}

static const struct dev_pm_ops spi_ingenic_pm_ops = {
	.runtime_suspend = spi_ingenic_runtime_suspend,
	.runtime_resume = spi_ingenic_runtime_resume,
};

static void spi_ingenic_pm_disable(void *data)
{
	struct device *dev = data;

	pm_runtime_disable(dev);
}

static bool spi_ingenic_can_dma(struct spi_controller *ctlr,
				struct spi_device *spi,
				struct spi_transfer *xfer)
{
	unsigned int bits = xfer->bits_per_word ?: spi->bits_per_word;
	unsigned int frame_bytes = DIV_ROUND_UP(bits, 8);

	/*
	 * 仅当 can_dma() 返回 true 时，SPI core 才会将 tx_buf/rx_buf 映射到
	 * xfer->{tx,rx}_sg。PIO 传输不应执行 DMA 映射。
	 */
	if (!ctlr->dma_tx || !ctlr->dma_rx || !xfer->len)
		return false;
	if (xfer->len % frame_bytes)
		return false;

	return xfer->len / frame_bytes >= SPI_INGENIC_FIFO_SIZE;
}

static int spi_ingenic_request_dma(struct spi_controller *ctlr,
				   struct device *dev)
{
	ctlr->dma_tx = dma_request_slave_channel(dev, "tx");
	if (!ctlr->dma_tx)
		return -ENODEV;

	ctlr->dma_rx = dma_request_slave_channel(dev, "rx");

	if (!ctlr->dma_rx) {
		dma_release_channel(ctlr->dma_tx);
		return -ENODEV;
	}

	ctlr->can_dma = spi_ingenic_can_dma;

	return 0;
}

static void spi_ingenic_release_dma(void *data)
{
	struct spi_controller *ctlr = data;

	if (ctlr->dma_tx)
		dma_release_channel(ctlr->dma_tx);
	if (ctlr->dma_rx)
		dma_release_channel(ctlr->dma_rx);
}

static int spi_ingenic_probe(struct platform_device *pdev)
{
	const struct jz_soc_info *pdata;
	struct device *dev = &pdev->dev;
	struct spi_controller *ctlr;
	struct ingenic_spi *priv;
	void __iomem *base;
	int num_cs, ret;
	unsigned int dev_clk = 0;

	pdata = of_device_get_match_data(dev);
	if (!pdata) {
		dev_err(dev, "Missing platform data.\n");
		return -EINVAL;
	}

	ctlr = devm_spi_alloc_master(dev, sizeof(*priv));
	if (!ctlr) {
		dev_err(dev, "Unable to allocate SPI controller.\n");
		return -ENOMEM;
	}

	priv = spi_controller_get_devdata(ctlr);
	priv->soc_info = pdata;
	init_completion(&priv->pio_done);
	init_completion(&priv->rx_done);

	priv->clk_gate = of_clk_get(dev->of_node, 0);
	if (IS_ERR(priv->clk_gate)) {
		return dev_err_probe(dev,
				     PTR_ERR(priv->clk_gate),
				     "Unable to get clock gate!\n");
	}
	priv->clk_div = of_clk_get(dev->of_node, 1);
	if (IS_ERR(priv->clk_div)) {
		clk_put(priv->clk_gate);
		return dev_err_probe(dev,
				     PTR_ERR(priv->clk_div),
				     "Unable to get clock divider!\n");
	}

	// SPI控制器输入的时钟,该时钟被多个SPI控制器共用,所以不建议动态修改,应该在开发板级定制的时候确定.
	// 所以: 一个配置无法既兼容高频又兼容低频.
	// 另一种方式通过sys接口,全局动态修改设备时钟源. 对于传输阶段,
	// 时钟是只读的. 对于100 000 000 的时钟输入, SSI控制器内部有时钟分频器,
	// 总线上的可调节范围为: 100 000 000 / (2 * (255 + 1)) ~ 100 000 000 /
	// (2 * (0 + 1))

	if (device_property_read_u32(dev, "spi-max-frequency", &dev_clk)) {
		dev_clk = pdata->max_speed_hz * 2;
	} else {
		if (dev_clk > pdata->max_speed_hz) {
			dev_err(dev,
				"spi spi-max-frequency (%u) rate is invalid. "
				"max_speed_hz is (%u)\n",
				dev_clk,
				pdata->max_speed_hz);
			dev_clk = pdata->max_speed_hz;
		}

		dev_clk = dev_clk * 2; // 转换为 SPI 控制器输入时钟。
	}

	clk_set_rate(priv->clk_div, dev_clk);

	base = devm_platform_get_and_ioremap_resource(pdev, 0, &priv->mem_res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	priv->base = base;

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0)
		return priv->irq;
	ret = devm_request_irq(dev, priv->irq, spi_ingenic_irq, 0,
			       dev_name(dev), priv);
	if (ret)
		return dev_err_probe(dev, ret, "Unable to request SSI IRQ\n");

	if (device_property_read_u32(dev, "num-cs", &num_cs))
		num_cs = pdata->max_native_cs;

	platform_set_drvdata(pdev, ctlr);
	pm_runtime_set_autosuspend_delay(dev, 100);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_enable(dev);
	ret = devm_add_action_or_reset(dev, spi_ingenic_pm_disable, dev);
	if (ret)
		return ret;
	ret = devm_device_add_group(dev, &spi_ingenic_attr_group);
	if (ret)
		return ret;

	ctlr->prepare_transfer_hardware = spi_ingenic_prepare_hardware;
	ctlr->unprepare_transfer_hardware = spi_ingenic_unprepare_hardware;
	ctlr->prepare_message = spi_ingenic_prepare_message;
	ctlr->set_cs = spi_ingenic_set_cs;
	ctlr->transfer_one = spi_ingenic_transfer_one;
	ctlr->mode_bits = SPI_MODE_3 | SPI_LSB_FIRST | SPI_LOOP | SPI_CS_HIGH;
	ctlr->flags = SPI_CONTROLLER_MUST_RX | SPI_CONTROLLER_MUST_TX;
	ctlr->max_dma_len = 0;
	ctlr->bits_per_word_mask = pdata->bits_per_word_mask;
	ctlr->min_speed_hz = dev_clk / 512; // 7200;
	ctlr->max_speed_hz = pdata->max_speed_hz;
	ctlr->use_gpio_descriptors = true;
	ctlr->max_native_cs = pdata->max_native_cs;
	ctlr->num_chipselect = num_cs;
	ctlr->dev.of_node = pdev->dev.of_node;

	if (spi_ingenic_request_dma(ctlr, dev))
		dev_warn(dev, "DMA not available.\n");

	ret = devm_add_action_or_reset(dev, spi_ingenic_release_dma, ctlr);
	if (ret) {
		dev_err(dev, "Unable to add action.\n");
		return ret;
	}

	ret = devm_spi_register_controller(dev, ctlr);
	if (ret) {
		dev_err(dev, "Unable to register SPI controller.\n");
		return ret;
	}

	dev_info(dev,
		 "Register SPI Controller Success, with min_speed_hz: %u, "
		 "max_speed_hz: %u, num_cs: %d\n",
		 ctlr->min_speed_hz,
		 ctlr->max_speed_hz,
		 num_cs);

	return 0;
}

static const struct jz_soc_info jz4750_soc_info = {
	.bits_per_word_mask = SPI_BPW_RANGE_MASK(2, 17),
	.flen_shift = 4,
	.has_trendian = false,

	.max_speed_hz = 54000000,
	.max_native_cs = 2,
};

static const struct jz_soc_info jz4780_soc_info = {
	.bits_per_word_mask = SPI_BPW_RANGE_MASK(2, 32),
	.flen_shift = 3,
	.has_trendian = true,

	.max_speed_hz = 54000000,
	.max_native_cs = 2,
};

static const struct jz_soc_info x1000_soc_info = {
	.bits_per_word_mask = SPI_BPW_RANGE_MASK(2, 32),
	.flen_shift = 3,
	.has_trendian = true,

	.max_speed_hz = 50000000,
	.max_native_cs = 2,
};

static const struct jz_soc_info x1600_soc_info = {
	.bits_per_word_mask = SPI_BPW_RANGE_MASK(2, 32),
	.flen_shift = 3,
	.has_trendian = true,

	.max_speed_hz = 50000000,
	.max_native_cs = 2,
};

static const struct jz_soc_info x2000_soc_info = {
	.bits_per_word_mask = SPI_BPW_RANGE_MASK(2, 32),
	.flen_shift = 3,
	.has_trendian = true,

	.max_speed_hz = 50000000,
	.max_native_cs = 2,
};

static const struct jz_soc_info m300_soc_info = {
	.bits_per_word_mask = SPI_BPW_RANGE_MASK(2, 32),
	.flen_shift = 3,
	.has_trendian = true,

	.max_speed_hz = 50000000,
	.max_native_cs = 2,
};

static const struct jz_soc_info x2500_soc_info = {
	.bits_per_word_mask = SPI_BPW_RANGE_MASK(2, 32),
	.flen_shift = 3,
	.has_trendian = true,

	.max_speed_hz = 50000000,
	.max_native_cs = 2,
};

static const struct jz_soc_info x2600_soc_info = {
	.bits_per_word_mask = SPI_BPW_RANGE_MASK(2, 32),
	.flen_shift = 3,
	.has_trendian = true,

	.max_speed_hz = 50000000,
	.max_native_cs = 2,
};

static const struct of_device_id spi_ingenic_of_match[] = {
	{ .compatible = "ingenic,jz4750-spi", .data = &jz4750_soc_info },
	{ .compatible = "ingenic,jz4775-spi", .data = &jz4780_soc_info },
	{ .compatible = "ingenic,jz4780-spi", .data = &jz4780_soc_info },
	{ .compatible = "ingenic,x1000-spi", .data = &x1000_soc_info },
	{ .compatible = "ingenic,x1600-spi", .data = &x1600_soc_info },
	{ .compatible = "ingenic,x2000-spi", .data = &x2000_soc_info },
	{ .compatible = "ingenic,m300-spi", .data = &m300_soc_info },
	{ .compatible = "ingenic,x2500-spi", .data = &x2500_soc_info },
	{ .compatible = "ingenic,x2600-spi", .data = &x2600_soc_info },
	{}
};
MODULE_DEVICE_TABLE(of, spi_ingenic_of_match);

static int spi_ingenic_remove(struct platform_device *pdev)
{
	struct spi_controller *ctlr = platform_get_drvdata(pdev);
	struct ingenic_spi *priv = spi_controller_get_devdata(ctlr);
	int ret;

	/* 在释放 of_clk_get() 获取的时钟引用前停止硬件。 */
	ret = pm_runtime_force_suspend(&pdev->dev);
	clk_put(priv->clk_div);
	clk_put(priv->clk_gate);

	return ret;
}

static struct platform_driver spi_ingenic_driver = {
	.driver = {
		.name = "spi-ingenic",
		.of_match_table = spi_ingenic_of_match,
		.pm = &spi_ingenic_pm_ops,
	},
	.probe = spi_ingenic_probe,
	.remove = spi_ingenic_remove,
};

module_platform_driver(spi_ingenic_driver);
MODULE_DESCRIPTION("SPI bus driver for the Ingenic SoCs");
MODULE_LICENSE("GPL");
