/*
 * Ingenic JPEG V4L2 Encoder Driver
 *
 * Copyright (C) 2025 Ingenic Semiconductor Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "../ingenic_jpeg_v4l2.h"
#include "jpeg_encoder_hw.h"

#define JPEG_ENC_DEBUG_LEVEL 0  // 0=关闭, 1=错误, 2=警告, 3=信息, 4=详细

#define jpeg_dbg(level, dev, fmt, ...) 		\
	do { 									\
		if (level <= JPEG_ENC_DEBUG_LEVEL) 	\
			dev_info(dev, "[JPEG_ENC] " fmt, ##__VA_ARGS__); \
	} while (0)

#define jpeg_info(dev, fmt, ...) jpeg_dbg(3, dev, fmt, ##__VA_ARGS__)
#define jpeg_warn(dev, fmt, ...) jpeg_dbg(2, dev, fmt, ##__VA_ARGS__)
#define jpeg_err(dev, fmt, ...) jpeg_dbg(1, dev, fmt, ##__VA_ARGS__)
#define jpeg_dbg_verbose(dev, fmt, ...) jpeg_dbg(4, dev, fmt, ##__VA_ARGS__)

static const struct of_device_id ingenic_jpege_dt_match[] = {
	{ .compatible = "ingenic,x2600-jpege", .data = NULL },
	{ .compatible = "ingenic,ad100-jpege", .data = NULL },
	{},
};

MODULE_DEVICE_TABLE(of, ingenic_jpege_dt_match);

static int ingenic_jpeg_enc_start(struct ingenic_jpeg_dev *jpeg_dev,
				  struct ingenic_jpeg_ctx *ctx)
{
	unsigned long flags;
	dma_addr_t src_dma_addr, dst_dma_addr;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	unsigned int tmp = 0;
	
	dev_dbg(jpeg_dev->dev, "Starting JPEG encoding: %ux%u -> %s\n",
		ctx->src_fmt.width, ctx->src_fmt.height,
		ctx->dst_fmt.pixelformat == V4L2_PIX_FMT_JPEG ? "JPEG" : "Unknown");
	
	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	
	if (!src_buf || !dst_buf) {
		dev_err(jpeg_dev->dev, "No buffers available\n");
		return -EINVAL;
	}
	
	src_dma_addr = vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 0);
	dst_dma_addr = vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0);
	
	if (!src_dma_addr || !dst_dma_addr) {
		dev_err(jpeg_dev->dev, "Failed to get DMA addresses\n");
		return -EINVAL;
	}
	
	dev_dbg(jpeg_dev->dev, "DMA addresses: src=0x%pad, dst=0x%pad\n",
		&src_dma_addr, &dst_dma_addr);
	
	spin_lock_irqsave(&jpeg_dev->lock, flags);
	
	/* Reset the encoder */
	jpeg_enc_soft_reset(jpeg_dev);
	
	/* Setup encoder parameters */
	{
		unsigned int tmp = ((ctx->src_fmt.height - 1) << 16) | (ctx->src_fmt.width - 1);
		writel(tmp, (void __iomem *)(jpeg_dev->base + JPEGE_FRM_SIZE));
	}
	
	writel(src_dma_addr >> 12 | (1 << 31), (void __iomem *)(jpeg_dev->base + JPEGE_PIX_IN_ADDR));
	writel(dst_dma_addr >> 12 | (1 << 31), (void __iomem *)(jpeg_dev->base + JPEGE_OUT_ADDR));
	writel(ctx->dst_fmt.sizeimage, (void __iomem *)(jpeg_dev->base + JPEGE_OUT_BUFFER_SIZE));
	
	/* Set input format based on source format */
	switch (ctx->src_fmt.pixelformat) {
	case V4L2_PIX_FMT_YUYV:
		{
			unsigned int tmp = (0x01 << 8);
			writel(tmp, (void __iomem *)(jpeg_dev->base + JPEGE_CAP_CTL_STA));
		}
		jpeg_enc_input_is_yuv(jpeg_dev);
		break;
	case V4L2_PIX_FMT_NV12:
		{
			unsigned int tmp = (0x02 << 8) | (0x04 << 4);
			writel(tmp, (void __iomem *)(jpeg_dev->base + JPEGE_CAP_CTL_STA));
			tmp = ctx->src_fmt.width * ctx->src_fmt.height;
			writel(tmp, (void __iomem *)(jpeg_dev->base + JPEGE_UV_OFFSET));
		}
		jpeg_enc_input_is_yuv(jpeg_dev);
		break;
	case V4L2_PIX_FMT_NV21:
		{
			unsigned int tmp = (0x02 << 8) | (0x00 << 4);
			writel(tmp, (void __iomem *)(jpeg_dev->base + JPEGE_CAP_CTL_STA));
			tmp = ctx->src_fmt.width * ctx->src_fmt.height;
			writel(tmp, (void __iomem *)(jpeg_dev->base + JPEGE_UV_OFFSET));
		}
		jpeg_enc_input_is_yuv(jpeg_dev);
		break;
	case V4L2_PIX_FMT_RGB24:
		writel(0, (void __iomem *)(jpeg_dev->base + JPEGE_CAP_CTL_STA));
		jpeg_enc_input_is_rgb(jpeg_dev);
		break;
	case V4L2_PIX_FMT_YUV420:
		{
			unsigned int tmp = (0x03 << 8) | (0x00 << 4);
			writel(tmp, (void __iomem *)(jpeg_dev->base + JPEGE_CAP_CTL_STA));
			tmp = ctx->src_fmt.width * ctx->src_fmt.height;
			writel(tmp, (void __iomem *)(jpeg_dev->base + JPEGE_UV_OFFSET));
		}
		jpeg_enc_input_is_yuv(jpeg_dev);
		break;
	case V4L2_PIX_FMT_YVU420:
		{
			unsigned int tmp = (0x03 << 8) | (0x01 << 4);
			writel(tmp, (void __iomem *)(jpeg_dev->base + JPEGE_CAP_CTL_STA));
			tmp = ctx->src_fmt.width * ctx->src_fmt.height;
			writel(tmp, (void __iomem *)(jpeg_dev->base + JPEGE_UV_OFFSET));
		}
		jpeg_enc_input_is_yuv(jpeg_dev);
		break;
	case V4L2_PIX_FMT_RGB565:
		{
			unsigned int tmp = (0x04 << 8);
			writel(tmp, (void __iomem *)(jpeg_dev->base + JPEGE_CAP_CTL_STA));
		}
		jpeg_enc_input_is_rgb(jpeg_dev);
		break;
	case V4L2_PIX_FMT_RGBA32:
		{
			unsigned int tmp = (0x05 << 8);
			writel(tmp, (void __iomem *)(jpeg_dev->base + JPEGE_CAP_CTL_STA));
		}
		jpeg_enc_input_is_rgb(jpeg_dev);
		break;
	/* TODO: 添加更多输入格式支持
	 * - V4L2_PIX_FMT_YUV420: YUV420平面格式
	 * - V4L2_PIX_FMT_YVU420: YVU420平面格式  
	 * - V4L2_PIX_FMT_RGB565: 16位RGB格式
	 * - V4L2_PIX_FMT_RGBA32: 32位RGBA格式
	 * 需要实现相应的JPEGE_CAP_CTL_STA寄存器配置和UV偏移计算
	 */
	default:
		/* Default to YUYV */
		{
			unsigned int tmp = (0x01 << 8);
			writel(tmp, (void __iomem *)(jpeg_dev->base + JPEGE_CAP_CTL_STA));
		}
		jpeg_enc_input_is_yuv(jpeg_dev);
		break;
	}
	
	/* Set quality */
	jpeg_enc_set_quality(jpeg_dev, ctx->quality);
	
	/* 应用重启间隔控制 */
	tmp = readl((void __iomem *)(jpeg_dev->base + JPEGE_CTL_STA));
	tmp &= ~(1 << 0);
	
	if (ctx->restart_interval > 0) {
		tmp &= ~(0xffff << 16);	
		tmp |= (ctx->restart_interval << 16);
		jpeg_dbg(1, jpeg_dev->dev, "Set restart interval to %d\n", ctx->restart_interval);
	}
	writel(tmp, (void __iomem *)(jpeg_dev->base + JPEGE_CTL_STA));
	
	
#if 0/* 应用活动标记控制 */
	if (ctx->active_markers) {
		unsigned int marker_reg = 0;
		if (ctx->active_markers & V4L2_JPEG_ACTIVE_MARKER_DQT)
			marker_reg |= (1 << 0);  /* DQT标记 */
		if (ctx->active_markers & V4L2_JPEG_ACTIVE_MARKER_DHT)
			marker_reg |= (1 << 1);  /* DHT标记 */
		writel(marker_reg, (void __iomem *)(jpeg_dev->base + JPEGE_ACTIVE_MARKER));
		jpeg_dbg(1, jpeg_dev->dev, "Set active markers to 0x%x\n", ctx->active_markers);
	}
#endif	
	/* 实现智能子采样配置 */
	jpeg_enc_auto_subsampling(jpeg_dev, ctx->src_fmt.pixelformat);
	
	/* Clear interrupt status */
	jpeg_enc_clr_int(jpeg_dev);
	
	/* Start encoding */
	jpeg_enc_start(jpeg_dev);
	
	dev_dbg(jpeg_dev->dev, "JPEG encoding started successfully\n");
	
	spin_unlock_irqrestore(&jpeg_dev->lock, flags);
	
	return 0;
}

static irqreturn_t ingenic_jpeg_enc_irq(int irq, void *data)
{
	struct ingenic_jpeg_dev *dev = data;
	struct ingenic_jpeg_ctx *ctx;
	unsigned int tmp;
	unsigned long flags;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	
	spin_lock_irqsave(&dev->lock, flags);
	
	tmp = readl((void __iomem *)(dev->base + JPEGE_CTL_STA));
	
	/* Clear interrupt */
	jpeg_enc_clr_int(dev);
	
	if (tmp & DATAENC_IDLE) {
		/* Encoding finished successfully */
		ctx = v4l2_m2m_get_curr_priv(dev->m2m_dev);
		if (ctx) {
			/* Get output information */
			tmp = readl((void __iomem *)(dev->base + JPEGE_IMGE_SIZE));
			tmp &= 0x3FFFFFFF;
			
			/* Remove buffers from queue */
			src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
			dst_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
			
			if (!src_buf || !dst_buf) {
				dev_err(dev->dev, "Failed to remove buffers from queue\n");
				spin_unlock_irqrestore(&dev->lock, flags);
				if (ctx) {
					v4l2_m2m_job_finish(dev->m2m_dev, ctx->fh.m2m_ctx);
				}
				return IRQ_HANDLED;
			}
			
			/* Update destination buffer size */
			vb2_set_plane_payload(&dst_buf->vb2_buf, 0, tmp);
			
			/* Copy timestamp from source to destination */
			dst_buf->vb2_buf.timestamp = src_buf->vb2_buf.timestamp;
			
			/* Mark buffers as done */
			v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_DONE);
			v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_DONE);
			
			dev_dbg(dev->dev, "Encoding completed: output size=%u bytes\n", tmp);
		}
		spin_unlock_irqrestore(&dev->lock, flags);
		
		/* Complete the operation */
		if (ctx) {
			v4l2_m2m_job_finish(dev->m2m_dev, ctx->fh.m2m_ctx);
		}
		
		return IRQ_HANDLED;
	} else if (tmp & ERR_FLAG) {
		/* Error occurred */
		dev_err(dev->dev, "JPEG encoding error detected\n");
		ctx = v4l2_m2m_get_curr_priv(dev->m2m_dev);
		if (ctx) {
			/* Remove buffers from queue and mark as error */
			src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
			dst_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
			
			if (src_buf) {
				v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_ERROR);
			}
			if (dst_buf) {
				v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_ERROR);
			}
		}
		spin_unlock_irqrestore(&dev->lock, flags);
		
		if (ctx) {
			v4l2_m2m_job_finish(dev->m2m_dev, ctx->fh.m2m_ctx);
		}
		
		return IRQ_HANDLED;
	}
	
	spin_unlock_irqrestore(&dev->lock, flags);
	
	return IRQ_NONE;
}

static void ingenic_jpeg_enc_run(void *priv)
{
	struct ingenic_jpeg_ctx *ctx = priv;
	struct ingenic_jpeg_dev *dev = ctx->dev;
	int ret;
	
	dev_dbg(dev->dev, "Starting JPEG encoding operation\n");
	
	ret = ingenic_jpeg_enc_start(dev, ctx);
	if (ret) {
		dev_err(dev->dev, "Failed to start JPEG encoding: %d\n", ret);
		v4l2_m2m_job_finish(dev->m2m_dev, ctx->fh.m2m_ctx);
	}
}

static const struct v4l2_m2m_ops ingenic_jpeg_enc_m2m_ops = {
	.device_run = ingenic_jpeg_enc_run,
};

static int ingenic_jpeg_enc_probe(struct platform_device *pdev)
{
	int ret;
	
	ret = ingenic_jpeg_probe(pdev, JPEG_ENCODE, &ingenic_jpeg_enc_m2m_ops);

	/* Request IRQ after the base probe is done */
	if (!ret) {
		struct ingenic_jpeg_dev *jpeg_dev = platform_get_drvdata(pdev);
		
		ret = devm_request_irq(&pdev->dev, jpeg_dev->irq,
				       ingenic_jpeg_enc_irq,
				       0, dev_name(&pdev->dev), jpeg_dev);
		if (ret) {
			dev_err(&pdev->dev, "failed to request jpeg encoder irq\n");
			return ret;
		}
	}
	
	return ret;
}

static int ingenic_jpeg_enc_remove(struct platform_device *pdev)
{
	return ingenic_jpeg_remove(pdev);
}

static struct platform_driver ingenic_jpeg_enc_driver = {
	.probe = ingenic_jpeg_enc_probe,
	.remove = ingenic_jpeg_enc_remove,
	.driver = {
		.name = "ingenic-jpeg-encoder-v4l2",
		.of_match_table = ingenic_jpege_dt_match,
	},
};

module_platform_driver(ingenic_jpeg_enc_driver);

MODULE_AUTHOR("Ingenic Semiconductor Co., Ltd.");
MODULE_DESCRIPTION("Ingenic JPEG V4L2 Encoder Driver");
MODULE_LICENSE("GPL v2");
