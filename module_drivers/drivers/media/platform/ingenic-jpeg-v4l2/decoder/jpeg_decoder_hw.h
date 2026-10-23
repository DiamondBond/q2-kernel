/*
 * Ingenic JPEG V4L2 Decoder Hardware Operations
 *
 * Copyright (C) 2025 Ingenic Semiconductor Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _JPEG_DECODER_HW_H
#define _JPEG_DECODER_HW_H

#include "../ingenic_jpeg_v4l2.h"

/* JPEG Decoder Register Definitions */
// #define JPEGDEC_BASE	0xb3200000

#define JPEGD_CTL_STA	0x00
#define JPEGD_ROW_COL	0x04
#define JPEGD_RD_ADDR	0x08
#define JPEGD_WR_ADDR	0x0C
#define JPEGD_FILE_SIZE	0x10
#define JPEGD_FRM_CNT	0x14
#define JPEGD_WORD_CNT	0x18
#define JPEGD_ERR_CNT	0x1C
#define JPEGD_PIX_DEC_CNT	0x20
#define JPEGD_PIX_OUT_CNT	0x24
#define JPEGD_DEC_SIZE  0X28
#define JPEGD_RESVER_STA	0x30
#define JPEGD_FB_ROW_COL	0x34
#define JPEGD_WDT_PERIOD	0x78
#define JPEGD_ERR_HW_FLG	0x7C

/* CTR_STA bits */
#define PIX_OUT_ORDER	(1 << 21)
#define DEFAULT_OUT_PIX	(1 << 20)
#define IMG_PIX_TYPE	(7 << 16)
#define IMG_PRASED_FLAG	(1 << 15)
#define ERR_IRQ_MASK	(1 << 9)
#define FINISH_IRQ_MASK	(1 << 8)
#define DEC_FINISH		(1 << 7)
#define INT_STA			(1 << 4)
#define SOFT_RST		(1 << 2)
#define ERR_DETECT		(1 << 1)
#define DEC_START		(1 << 0)

/* Output format definitions */
#define JPEGD_OUT_DEFAULT	0
#define JPEGD_OUT_YUV422P	1
#define JPEGD_OUT_NV12		2
#define JPEGD_OUT_ARGB32	3
#define JPEGD_OUT_NV21		4
#define JPEGD_OUT_YUV420P	5
#define JPEGD_OUT_RGB24		6
#define JPEGD_OUT_RGB565	7
#define JPEGD_OUT_BGR24		8

/* Decoder register operations */
int jpeg_dec_reg_bit_set(struct ingenic_jpeg_dev *jpeg_dev, unsigned int offset, unsigned int val);
int jpeg_dec_reg_bit_clr(struct ingenic_jpeg_dev *jpeg_dev, unsigned int offset, unsigned int val);

/* Decoder helper functions */
void jpeg_dec_finish_irq_disable(struct ingenic_jpeg_dev *jpeg_dev);
void jpeg_dec_finish_irq_enable(struct ingenic_jpeg_dev *jpeg_dev);
void jpeg_dec_err_irq_disable(struct ingenic_jpeg_dev *jpeg_dev);
void jpeg_dec_err_irq_enable(struct ingenic_jpeg_dev *jpeg_dev);
void jpeg_dec_start(struct ingenic_jpeg_dev *jpeg_dev);
void jpeg_dec_out_normal_order(struct ingenic_jpeg_dev *jpeg_dev);
void jpeg_dec_out_yuv(struct ingenic_jpeg_dev *jpeg_dev);
void jpeg_dec_out_rgb(struct ingenic_jpeg_dev *jpeg_dev);
void jpeg_dec_clear_err_hw_flag(struct ingenic_jpeg_dev *jpeg_dev);
void jpeg_dec_soft_reset(struct ingenic_jpeg_dev *jpeg_dev);
void jpeg_dec_clear_int(struct ingenic_jpeg_dev *jpeg_dev);
void jpeg_dec_clear_err(struct ingenic_jpeg_dev *jpeg_dev);
void jpeg_dec_set_output_fmt(struct ingenic_jpeg_dev *jpeg_dev, int fmt);

#endif /* _JPEG_DECODER_HW_H */