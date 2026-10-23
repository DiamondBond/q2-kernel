/*
 * Ingenic JPEG V4L2 Encoder Hardware Operations
 *
 * Copyright (C) 2025 Ingenic Semiconductor Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "jpeg_encoder_hw.h"

/* Register operation functions for JPEG encoder */
int jpeg_enc_reg_bit_set(struct ingenic_jpeg_dev *jpeg_dev, unsigned int offset, unsigned int val)
{
	unsigned int tmp = readl((void __iomem *)(jpeg_dev->base + offset));
	tmp |= val;
	writel(tmp, (void __iomem *)(jpeg_dev->base + offset));
	return 0;
}

int jpeg_enc_reg_bit_clr(struct ingenic_jpeg_dev *jpeg_dev, unsigned int offset, unsigned int val)
{
	unsigned int tmp = readl((void __iomem *)(jpeg_dev->base + offset));
	tmp &= ~(val);
	writel(tmp, (void __iomem *)(jpeg_dev->base + offset));
	return 0;
}

/* Encoder helper functions */
void jpeg_enc_start(struct ingenic_jpeg_dev *jpeg_dev)
{
	jpeg_enc_reg_bit_set(jpeg_dev, JPEGE_CTL_STA, START_MASK);
}

void jpeg_enc_set_quality(struct ingenic_jpeg_dev *jpeg_dev, char quality)
{
	jpeg_enc_reg_bit_clr(jpeg_dev, JPEGE_CTL_STA, QUALITY_MASK);
	jpeg_enc_reg_bit_set(jpeg_dev, JPEGE_CTL_STA, (quality & 0x7f) << 1);
}

void jpeg_enc_clear_all_status(struct ingenic_jpeg_dev *jpeg_dev)
{
	jpeg_enc_reg_bit_set(jpeg_dev, JPEGE_CTL_STA, CLR_FLAGS_MASK);
}

void jpeg_enc_input_is_yuv(struct ingenic_jpeg_dev *jpeg_dev)
{
	jpeg_enc_reg_bit_clr(jpeg_dev, JPEGE_CTL_STA, GRAYSCALE_MASK);
	jpeg_enc_reg_bit_set(jpeg_dev, JPEGE_CTL_STA, YCBCR_MASK);
}

void jpeg_enc_input_is_grayscale(struct ingenic_jpeg_dev *jpeg_dev)
{
	jpeg_enc_reg_bit_set(jpeg_dev, JPEGE_CTL_STA, YCBCR_MASK);
	jpeg_enc_reg_bit_set(jpeg_dev, JPEGE_CTL_STA, GRAYSCALE_MASK);
}

void jpeg_enc_input_is_rgb(struct ingenic_jpeg_dev *jpeg_dev)
{
	jpeg_enc_reg_bit_clr(jpeg_dev, JPEGE_CTL_STA, GRAYSCALE_MASK);
	jpeg_enc_reg_bit_clr(jpeg_dev, JPEGE_CTL_STA, YCBCR_MASK);
}

void jpeg_enc_set_input_subsamp(struct ingenic_jpeg_dev *jpeg_dev, int x)
{
	/* 首先清除现有的子采样配置 */
	jpeg_enc_reg_bit_clr(jpeg_dev, JPEGE_CTL_STA, SUBSAMP_MASK);
	
	/* 设置新的子采样模式 */
	jpeg_enc_reg_bit_set(jpeg_dev, JPEGE_CTL_STA, (x & 3) << 10);
}

/* 新增：根据输入格式自动选择最佳子采样模式 */
void jpeg_enc_auto_subsampling(struct ingenic_jpeg_dev *jpeg_dev, u32 pixelformat)
{
	u32 subsamp_mode;
	
	switch (pixelformat) {
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		/* YUV420格式使用4:2:0子采样 */
		subsamp_mode = SUBSAMP_420;
		break;
		
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_UYVY:
		/* YUV422格式使用4:2:2子采样 */
		subsamp_mode = SUBSAMP_422;
		break;
		
	case V4L2_PIX_FMT_RGB24:
	case V4L2_PIX_FMT_BGR24:
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_RGBA32:
	case V4L2_PIX_FMT_BGRA32:
		/* RGB格式使用4:4:4子采样以保持最佳质量 */
		subsamp_mode = SUBSAMP_444;
		break;
		
	case V4L2_PIX_FMT_GREY:
		/* 灰度图像不需要色度子采样 */
		subsamp_mode = SUBSAMP_444;
		break;
		
	default:
		/* 默认使用4:2:2子采样 */
		subsamp_mode = SUBSAMP_422;
		break;
	}
	
	jpeg_enc_set_input_subsamp(jpeg_dev, subsamp_mode);
}

void jpeg_enc_clr_int(struct ingenic_jpeg_dev *jpeg_dev)
{
	jpeg_enc_reg_bit_set(jpeg_dev, JPEGE_CTL_STA, CLR_FLAGS_MASK);
}

void jpeg_enc_soft_reset(struct ingenic_jpeg_dev *jpeg_dev)
{
	/* Note: 0xb00000C4 is a system-level register address for controlling the JPEG encoder reset */
	/* In a production environment, this should be defined in a system header file */
	unsigned int tmp = readl((void __iomem *)0xb00000C4);
	tmp |= (1 << 26);
	writel(tmp, (void __iomem *)0xb00000C4);
	tmp &= ~(1 << 26);
	writel(tmp, (void __iomem *)0xb00000C4);
}