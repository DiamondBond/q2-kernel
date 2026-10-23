/*
 * Ingenic JPEG V4L2 Decoder Driver
 *
 * Copyright (C) 2025 Ingenic Semiconductor Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "../ingenic_jpeg_v4l2.h"
#include "jpeg_decoder_hw.h"

// 解码调试级别控制 - 临时提高到4以便调试
#define JPEG_DEC_DEBUG_LEVEL 0  // 0=关闭, 1=错误, 2=警告, 3=信息, 4=详细

#define jpeg_dbg(level, dev, fmt, ...) 		\
	do { 									\
		if (level <= JPEG_DEC_DEBUG_LEVEL) 	\
			dev_info(dev, "[JPEG_DEC] " fmt, ##__VA_ARGS__); \
	} while (0)

#define jpeg_info(dev, fmt, ...) jpeg_dbg(3, dev, fmt, ##__VA_ARGS__)
#define jpeg_warn(dev, fmt, ...) jpeg_dbg(2, dev, fmt, ##__VA_ARGS__)
#define jpeg_err(dev, fmt, ...) jpeg_dbg(1, dev, fmt, ##__VA_ARGS__)
#define jpeg_dbg_verbose(dev, fmt, ...) jpeg_dbg(4, dev, fmt, ##__VA_ARGS__)

static const struct of_device_id ingenic_jpegd_dt_match[] = {
	{ .compatible = "ingenic,x2600-jpegd", .data = NULL },
	{ .compatible = "ingenic,ad100-jpegd", .data = NULL },
	{},
};

MODULE_DEVICE_TABLE(of, ingenic_jpegd_dt_match);

static int ingenic_jpeg_dec_start(struct ingenic_jpeg_dev *jpeg_dev,
			  struct ingenic_jpeg_ctx *ctx)
{
	unsigned long flags;
	dma_addr_t src_dma_addr, dst_dma_addr;
	unsigned int src_size;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	
	jpeg_info(jpeg_dev->dev, "Starting JPEG decoding process");
	
	/* 在device_run中，框架已经准备好了源和目标缓冲区 */
	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	
	jpeg_dbg_verbose(jpeg_dev->dev, "Buffers: src_buf=%p, dst_buf=%p", src_buf, dst_buf);
	
	if (!src_buf || !dst_buf) {
		jpeg_err(jpeg_dev->dev, "No buffers available: src_buf=%p, dst_buf=%p", src_buf, dst_buf);
		return -EINVAL;
	}
	
	/* 将缓冲区信息保存到上下文，供中断处理使用 */
	ctx->curr_src_buf = src_buf;
	ctx->curr_dst_buf = dst_buf;
	
	src_dma_addr = vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 0);
	dst_dma_addr = vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0);
	src_size = vb2_get_plane_payload(&src_buf->vb2_buf, 0);
	
	jpeg_dbg_verbose(jpeg_dev->dev, "DMA addresses: src=0x%pad, dst=0x%pad, src_size=%u", 
	               &src_dma_addr, &dst_dma_addr, src_size);
	
	if (!src_dma_addr || !dst_dma_addr) {
		jpeg_err(jpeg_dev->dev, "Failed to get DMA addresses: src=0x%pad, dst=0x%pad", 
	             &src_dma_addr, &dst_dma_addr);
		return -EINVAL;
	}
	
	spin_lock_irqsave(&jpeg_dev->lock, flags);
	
	/* Reset the decoder */
	jpeg_dbg_verbose(jpeg_dev->dev, "Soft resetting decoder");
	jpeg_dec_soft_reset(jpeg_dev);

    	jpeg_dec_out_normal_order(jpeg_dev);
	/* Setup decoder parameters using proper register access */
	jpeg_dbg_verbose(jpeg_dev->dev, "Writing to registers: JPEGD_FILE_SIZE=0x%x", src_size);
	writel(src_size, (void __iomem *)(jpeg_dev->base + JPEGD_FILE_SIZE));
	jpeg_dbg_verbose(jpeg_dev->dev, "Writing to registers: JPEGD_RD_ADDR=0x%pad", &src_dma_addr);
	writel(src_dma_addr, (void __iomem *)(jpeg_dev->base + JPEGD_RD_ADDR));
	jpeg_dbg_verbose(jpeg_dev->dev, "Writing to registers: JPEGD_WR_ADDR=0x%pad", &dst_dma_addr);
	writel(dst_dma_addr, (void __iomem *)(jpeg_dev->base + JPEGD_WR_ADDR));
	
	/* Set output format based on destination format */
	jpeg_dbg_verbose(jpeg_dev->dev, "Setting output format for pixelformat=0x%x", ctx->dst_fmt.pixelformat);
	switch (ctx->dst_fmt.pixelformat) {
	case V4L2_PIX_FMT_YUYV:
		jpeg_dec_set_output_fmt(jpeg_dev, JPEGD_OUT_YUV422P);
        	jpeg_dec_out_yuv(jpeg_dev);
		jpeg_info(jpeg_dev->dev, "Output format set to YUV422P");
		break;
	case V4L2_PIX_FMT_NV12:
		jpeg_dec_set_output_fmt(jpeg_dev, JPEGD_OUT_NV12);
        	jpeg_dec_out_yuv(jpeg_dev);
		jpeg_info(jpeg_dev->dev, "Output format set to NV12");
		break;
	case V4L2_PIX_FMT_NV21:
		jpeg_dec_set_output_fmt(jpeg_dev, JPEGD_OUT_NV21);
        	jpeg_dec_out_yuv(jpeg_dev);
		jpeg_info(jpeg_dev->dev, "Output format set to NV21");
		break;
	case V4L2_PIX_FMT_YUV420:
		jpeg_dec_set_output_fmt(jpeg_dev, JPEGD_OUT_YUV420P);
        	jpeg_dec_out_yuv(jpeg_dev);
		jpeg_info(jpeg_dev->dev, "Output format set to YUV420P");
		break;
	case V4L2_PIX_FMT_RGB24:
		jpeg_dec_set_output_fmt(jpeg_dev, JPEGD_OUT_RGB24);
		jpeg_dec_out_rgb(jpeg_dev);
		jpeg_info(jpeg_dev->dev, "Output format set to RGB24");
		break;
	case V4L2_PIX_FMT_RGB565:
		jpeg_dec_set_output_fmt(jpeg_dev, JPEGD_OUT_RGB565);
		jpeg_dec_out_rgb(jpeg_dev);
		jpeg_info(jpeg_dev->dev, "Output format set to RGB565");
		break;
	case V4L2_PIX_FMT_ARGB32:
		jpeg_dec_set_output_fmt(jpeg_dev, JPEGD_OUT_ARGB32);
		jpeg_dec_out_rgb(jpeg_dev);
		jpeg_info(jpeg_dev->dev, "Output format set to ARGB32");
		break;
	case V4L2_PIX_FMT_BGR24:
		jpeg_dec_set_output_fmt(jpeg_dev, JPEGD_OUT_BGR24);
		jpeg_dec_out_rgb(jpeg_dev);
		jpeg_info(jpeg_dev->dev, "Output format set to BGR24");
		break;
	default:
		/* 智能格式转换：如果请求的格式不被硬件直接支持，尝试使用最接近的格式 */
		jpeg_warn(jpeg_dev->dev, "Requested format 0x%x not directly supported, using smart conversion", ctx->dst_fmt.pixelformat);
		
		/* 根据格式类型选择最接近的支持格式 */
		switch (ctx->dst_fmt.pixelformat) {
		case V4L2_PIX_FMT_YVU420:
			/* YVU420转换为YUV420 */
			jpeg_dec_set_output_fmt(jpeg_dev, JPEGD_OUT_YUV420P);
			jpeg_info(jpeg_dev->dev, "YVU420 converted to YUV420P");
			break;
		case V4L2_PIX_FMT_RGBA32:
			/* RGBA32转换为ARGB32 */
			jpeg_dec_set_output_fmt(jpeg_dev, JPEGD_OUT_ARGB32);
			jpeg_dec_out_rgb(jpeg_dev);
			jpeg_info(jpeg_dev->dev, "RGBA32 converted to ARGB32");
			break;
		default:
			/* 默认使用NV12 */
			jpeg_dec_set_output_fmt(jpeg_dev, JPEGD_OUT_NV12);
			jpeg_info(jpeg_dev->dev, "Using NV12 as default format");
			break;
		}
		break;
	}
	
	/* Enable interrupts */
	jpeg_dbg_verbose(jpeg_dev->dev, "Enabling interrupts");
	jpeg_dec_finish_irq_enable(jpeg_dev);
	jpeg_dec_err_irq_enable(jpeg_dev);
	
	/* Start decoding */
	jpeg_info(jpeg_dev->dev, "Starting JPEG decoder hardware");
	jpeg_dec_start(jpeg_dev);
	
	spin_unlock_irqrestore(&jpeg_dev->lock, flags);
	
	return 0;
}

static irqreturn_t ingenic_jpeg_dec_irq(int irq, void *data)
{
	struct ingenic_jpeg_dev *dev = data;
	struct ingenic_jpeg_ctx *ctx = NULL;
	unsigned int tmp;
	unsigned int err_status;
	unsigned long flags;
	struct vb2_v4l2_buffer *src = NULL;
	struct vb2_v4l2_buffer *dst = NULL;
	
	jpeg_dbg_verbose(dev->dev, "JPEG decoder IRQ received: irq=%d", irq);
	
	spin_lock_irqsave(&dev->lock, flags);
	
	tmp = readl((void __iomem *)(dev->base + JPEGD_CTL_STA));
	jpeg_dbg_verbose(dev->dev, "JPEGD_CTL_STA register value: 0x%x", tmp);
	
	/* Clear interrupt */
	jpeg_dbg_verbose(dev->dev, "Clearing interrupts");
	jpeg_dec_finish_irq_disable(dev);
	jpeg_dec_err_irq_disable(dev);
	jpeg_dec_clear_int(dev);

	/* 先获取当前上下文，无论成功失败都需要 */
	ctx = v4l2_m2m_get_curr_priv(dev->m2m_dev);
	if (!ctx) {
		jpeg_warn(dev->dev, "No current context, ignoring IRQ");
		spin_unlock_irqrestore(&dev->lock, flags);
		return IRQ_NONE;
	}

	/* 从队列中移除源缓冲区和目标缓冲区 */
	src = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	if (!src) {
		jpeg_err(dev->dev, "Failed to remove source buffer from queue");
		spin_unlock_irqrestore(&dev->lock, flags);
		return IRQ_HANDLED;
	}
	
	dst = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
	if (!dst) {
		jpeg_err(dev->dev, "Failed to remove destination buffer from queue");
		spin_unlock_irqrestore(&dev->lock, flags);
		return IRQ_HANDLED;
	}
	
	/* 清空上下文指针 */
	ctx->curr_src_buf = NULL;
	ctx->curr_dst_buf = NULL;
	
	if (tmp & DEC_FINISH) {
		/* Decoding finished successfully */
		jpeg_info(dev->dev, "Decoding finished successfully, DEC_FINISH flag set");
		
		/* Get output information */
		tmp = readl((void __iomem *)(dev->base + JPEGD_ROW_COL));
		ctx->dst_fmt.width = tmp & 0xffff;
		ctx->dst_fmt.height = (tmp >> 16) & 0xffff;
		jpeg_info(dev->dev, "Decoded image dimensions: %dx%d", 
		         ctx->dst_fmt.width, ctx->dst_fmt.height);
		
		/* 在中断中更新目标缓冲区的payload大小 */
		if (dst) {
			unsigned int dec_size = readl((void __iomem *)(dev->base + JPEGD_DEC_SIZE));
			jpeg_info(dev->dev, "Decoded size: %u bytes", dec_size);
			vb2_set_plane_payload(&dst->vb2_buf, 0, dec_size);
		}
		
	} else if (tmp & ERR_DETECT) {
		/* Error occurred */
		jpeg_err(dev->dev, "JPEG decoding error detected, ERR_DETECT flag set");
		// 读取更多的错误状态信息
		err_status = readl((void __iomem *)(dev->base + JPEGD_ERR_HW_FLG));
		jpeg_err(dev->dev, "JPEGD_ERR_STA register value: 0x%x", err_status);
		jpeg_dec_clear_err(dev);
	}
	
	spin_unlock_irqrestore(&dev->lock, flags);
	
	/* 使用v4l2_m2m_buf_done正确标记缓冲区状态 */
	if (src && dst) {
		unsigned int state;
		if (tmp & DEC_FINISH) {
			state = VB2_BUF_STATE_DONE;
			jpeg_info(dev->dev, "Marking buffers as done");
		} else {
			state = VB2_BUF_STATE_ERROR;
			jpeg_err(dev->dev, "Marking buffers as error");
		}
		
		// 复制时间戳信息
		dst->vb2_buf.timestamp = src->vb2_buf.timestamp;
		
		// 标记源缓冲区和目标缓冲区完成
		v4l2_m2m_buf_done(src, state);
		v4l2_m2m_buf_done(dst, state);
	}
	
	/* 无论成功失败，都要完成任务 */
	jpeg_dbg_verbose(dev->dev, "Calling v4l2_m2m_job_finish");
	v4l2_m2m_job_finish(dev->m2m_dev, ctx->fh.m2m_ctx);
	
	return IRQ_HANDLED;
}

static void ingenic_jpeg_dec_device_run(void *priv)
{
	struct ingenic_jpeg_ctx *ctx = priv;
	struct ingenic_jpeg_dev *dev = ctx->dev;
	int ret;
	
	jpeg_info(dev->dev, "Device run called, ctx=%p, dev=%p", ctx, dev);
	
	ret = ingenic_jpeg_dec_start(dev, ctx);
	if (ret) {
		jpeg_err(dev->dev, "Failed to start JPEG decoding: %d", ret);
		v4l2_m2m_job_finish(dev->m2m_dev, ctx->fh.m2m_ctx);
	} else {
		jpeg_dbg_verbose(dev->dev, "JPEG decoding started successfully");
	}
}

static const struct v4l2_m2m_ops ingenic_jpeg_dec_m2m_ops = {
	.device_run = ingenic_jpeg_dec_device_run,
};

static int ingenic_jpeg_dec_probe(struct platform_device *pdev)
{
	int ret;
	
	jpeg_info(&pdev->dev, "Probing JPEG decoder driver");
	
	ret = ingenic_jpeg_probe(pdev, JPEG_DECODE, &ingenic_jpeg_dec_m2m_ops);
	
	/* Request IRQ after the base probe is done */
	if (!ret) {
		struct ingenic_jpeg_dev *jpeg_dev = platform_get_drvdata(pdev);
		jpeg_info(&pdev->dev, "Base probe successful, requesting IRQ: %d", jpeg_dev->irq);
		
		ret = devm_request_irq(&pdev->dev, jpeg_dev->irq,
		       ingenic_jpeg_dec_irq,
		       0, dev_name(&pdev->dev), jpeg_dev);
		if (ret) {
			jpeg_err(&pdev->dev, "Failed to request JPEG decoder IRQ: %d", ret);
			return ret;
		}
		jpeg_info(&pdev->dev, "IRQ requested successfully");
	}
	
	if (ret)
		jpeg_err(&pdev->dev, "Probe failed with error: %d", ret);
	else
		jpeg_info(&pdev->dev, "Probe completed successfully");
	
	return ret;
}

static int ingenic_jpeg_dec_remove(struct platform_device *pdev)
{
	return ingenic_jpeg_remove(pdev);
}

static struct platform_driver ingenic_jpeg_dec_driver = {
	.probe = ingenic_jpeg_dec_probe,
	.remove = ingenic_jpeg_dec_remove,
	.driver = {
		.name = "ingenic-jpeg-decoder-v4l2",
		.of_match_table = ingenic_jpegd_dt_match,
	},
};

module_platform_driver(ingenic_jpeg_dec_driver);

MODULE_AUTHOR("Ingenic Semiconductor Co., Ltd.");
MODULE_DESCRIPTION("Ingenic JPEG V4L2 Decoder Driver");
MODULE_LICENSE("GPL v2");
