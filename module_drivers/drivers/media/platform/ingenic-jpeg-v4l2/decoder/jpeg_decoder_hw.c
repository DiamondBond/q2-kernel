/*
 * Ingenic JPEG V4L2 Decoder Hardware Operations
 *
 * Copyright (C) 2025 Ingenic Semiconductor Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "jpeg_decoder_hw.h"

// 硬件操作调试级别控制
#define JPEG_HW_DEBUG_LEVEL 0  // 0=关闭, 1=错误, 2=警告, 3=信息, 4=详细

#define jpeg_hw_dbg(level, dev, fmt, ...) 			\
	do { 											\
		if (level <= JPEG_HW_DEBUG_LEVEL && dev) 	\
			dev_dbg(dev, "[JPEG_HW] " fmt, ##__VA_ARGS__); \
	} while (0)

#define jpeg_hw_info(dev, fmt, ...) jpeg_hw_dbg(3, dev, fmt, ##__VA_ARGS__)
#define jpeg_hw_warn(dev, fmt, ...) jpeg_hw_dbg(2, dev, fmt, ##__VA_ARGS__)
#define jpeg_hw_err(dev, fmt, ...) jpeg_hw_dbg(1, dev, fmt, ##__VA_ARGS__)
#define jpeg_hw_dbg_verbose(dev, fmt, ...) jpeg_hw_dbg(4, dev, fmt, ##__VA_ARGS__)

/* Register operation functions for JPEG decoder */
int jpeg_dec_reg_bit_set(struct ingenic_jpeg_dev *jpeg_dev, unsigned int offset, unsigned int val)
{
	unsigned int tmp = readl((void __iomem *)(jpeg_dev->base + offset));
	unsigned int old_val = tmp;
	
	tmp |= val;
	writel(tmp, (void __iomem *)(jpeg_dev->base + offset));
	
	jpeg_hw_dbg_verbose(jpeg_dev->dev, "REG_SET: offset=0x%x, old_val=0x%x, mask=0x%x, new_val=0x%x", 
	                   offset, old_val, val, tmp);
	
	return 0;
}

int jpeg_dec_reg_bit_clr(struct ingenic_jpeg_dev *jpeg_dev, unsigned int offset, unsigned int val)
{
	unsigned int tmp = readl((void __iomem *)(jpeg_dev->base + offset));
	unsigned int old_val = tmp;
	
	tmp &= ~(val);
	writel(tmp, (void __iomem *)(jpeg_dev->base + offset));
	
	jpeg_hw_dbg_verbose(jpeg_dev->dev, "REG_CLR: offset=0x%x, old_val=0x%x, mask=0x%x, new_val=0x%x", 
	                   offset, old_val, val, tmp);
	
	return 0;
}

/* Decoder helper functions */
void jpeg_dec_finish_irq_disable(struct ingenic_jpeg_dev *jpeg_dev)
{
	jpeg_hw_dbg_verbose(jpeg_dev->dev, "Disabling finish interrupt");
	jpeg_dec_reg_bit_set(jpeg_dev, JPEGD_CTL_STA, FINISH_IRQ_MASK);
}

void jpeg_dec_finish_irq_enable(struct ingenic_jpeg_dev *jpeg_dev)
{
	jpeg_hw_dbg_verbose(jpeg_dev->dev, "Enabling finish interrupt");
	jpeg_dec_reg_bit_clr(jpeg_dev, JPEGD_CTL_STA, FINISH_IRQ_MASK);
}

void jpeg_dec_err_irq_disable(struct ingenic_jpeg_dev *jpeg_dev)
{
	jpeg_hw_dbg_verbose(jpeg_dev->dev, "Disabling error interrupt");
	jpeg_dec_reg_bit_set(jpeg_dev, JPEGD_CTL_STA, ERR_IRQ_MASK);
}

void jpeg_dec_err_irq_enable(struct ingenic_jpeg_dev *jpeg_dev)
{
	jpeg_hw_dbg_verbose(jpeg_dev->dev, "Enabling error interrupt");
	jpeg_dec_reg_bit_clr(jpeg_dev, JPEGD_CTL_STA, ERR_IRQ_MASK);
}

void jpeg_dec_start(struct ingenic_jpeg_dev *jpeg_dev)
{
	jpeg_hw_info(jpeg_dev->dev, "Starting JPEG decoder hardware");
	jpeg_dec_reg_bit_set(jpeg_dev, JPEGD_CTL_STA, DEC_START);
}

void jpeg_dec_out_normal_order(struct ingenic_jpeg_dev *jpeg_dev)
{
	jpeg_hw_dbg_verbose(jpeg_dev->dev, "Setting normal pixel output order");
	jpeg_dec_reg_bit_set(jpeg_dev, JPEGD_CTL_STA, PIX_OUT_ORDER);
}

void jpeg_dec_out_yuv(struct ingenic_jpeg_dev *jpeg_dev)
{
	jpeg_hw_dbg_verbose(jpeg_dev->dev, "Setting YUV output format");
	jpeg_dec_reg_bit_set(jpeg_dev, JPEGD_CTL_STA, DEFAULT_OUT_PIX);
}

void jpeg_dec_out_rgb(struct ingenic_jpeg_dev *jpeg_dev)
{
	jpeg_hw_dbg_verbose(jpeg_dev->dev, "Setting RGB output format");
	jpeg_dec_reg_bit_clr(jpeg_dev, JPEGD_CTL_STA, DEFAULT_OUT_PIX);
}

void jpeg_dec_clear_err_hw_flag(struct ingenic_jpeg_dev *jpeg_dev)
{
	unsigned int err_flags = readl((void __iomem *)(jpeg_dev->base + JPEGD_ERR_HW_FLG));
	jpeg_hw_dbg_verbose(jpeg_dev->dev, "Clearing hardware error flags: 0x%x", err_flags);
	jpeg_dec_reg_bit_set(jpeg_dev, JPEGD_ERR_HW_FLG, 0xffffffff);
}

void jpeg_dec_soft_reset(struct ingenic_jpeg_dev *jpeg_dev)
{
	jpeg_hw_info(jpeg_dev->dev, "Performing soft reset");
	/* 执行软重置 */
	jpeg_dec_reg_bit_set(jpeg_dev, JPEGD_CTL_STA, SOFT_RST);
	/* 等待重置完成 - 给硬件一些时间完成重置 */
	jpeg_hw_dbg_verbose(jpeg_dev->dev, "Waiting 10us for reset to complete");
	udelay(10);
	/* 清除重置位 */
	jpeg_hw_dbg_verbose(jpeg_dev->dev, "Clearing reset bit");
	jpeg_dec_reg_bit_clr(jpeg_dev, JPEGD_CTL_STA, SOFT_RST);
	/* 等待状态稳定 */
	jpeg_hw_dbg_verbose(jpeg_dev->dev, "Waiting 5us for state to stabilize");
	udelay(5);
}

void jpeg_dec_clear_int(struct ingenic_jpeg_dev *jpeg_dev)
{
	jpeg_hw_dbg_verbose(jpeg_dev->dev, "Clearing interrupt status");
	jpeg_dec_reg_bit_set(jpeg_dev, JPEGD_CTL_STA, INT_STA);
}

void jpeg_dec_clear_err(struct ingenic_jpeg_dev *jpeg_dev)
{
	jpeg_hw_info(jpeg_dev->dev, "Clearing error status");
	/* 首先清除硬件错误标志 */
	jpeg_dec_clear_err_hw_flag(jpeg_dev);
	/* 然后清除错误检测位 */
	jpeg_hw_dbg_verbose(jpeg_dev->dev, "Clearing error detect bit");
	jpeg_dec_reg_bit_set(jpeg_dev, JPEGD_CTL_STA, ERR_DETECT);
}

void jpeg_dec_set_output_fmt(struct ingenic_jpeg_dev *jpeg_dev, int fmt)
{
	unsigned int tmp = readl((void __iomem *)(jpeg_dev->base + JPEGD_CTL_STA));
	unsigned int old_val = tmp;
	
	jpeg_hw_info(jpeg_dev->dev, "Setting output format: %d", fmt);
	
	tmp &= ~(7 << 12);
	tmp |= (fmt << 12);
	writel(tmp, (void __iomem *)(jpeg_dev->base + JPEGD_CTL_STA));
	
	jpeg_hw_dbg_verbose(jpeg_dev->dev, "Output format register: old=0x%x, new=0x%x", old_val, tmp);
}