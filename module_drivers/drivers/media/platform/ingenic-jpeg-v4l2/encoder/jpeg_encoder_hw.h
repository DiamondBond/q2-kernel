/*
 * Ingenic JPEG V4L2 Encoder Hardware Operations
 *
 * Copyright (C) 2025 Ingenic Semiconductor Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _JPEG_ENCODER_HW_H
#define _JPEG_ENCODER_HW_H

#include "../ingenic_jpeg_v4l2.h"

/* JPEG Encoder Register Definitions */
// #define JPEGENC_BASE	0xb3210000

#define JPEGE_VERSION			0x00
#define JPEGE_CTL_STA			0x10
#define JPEGE_FRM_SIZE			0x14
#define JPEGE_CAP_CTL_STA		0x18
#define JPEGE_PIX_IN_STA		0x20
#define JPEGE_PIX_IN_ADDR		0x24
#define JPEGE_UV_OFFSET			0x28
#define JPEGE_OUT_STA			0x30
#define JPEGE_OUT_ADDR			0x34
#define JPEGE_OUT_BUFFER_SIZE	0x38
#define JPEGE_IMGE_SIZE			0x3C
#define JPEGE_NEXT_OUT_ADDR		0x40
#define JPEGE_STA_FLAG1			0x78
#define JPEGE_STA_FLAG2			0x7C

/* control / status bits */
#define ERR_FLAG		(1 << 15)
#define YCBCR_MASK		(1 << 14)
#define DATAENC_IDLE	(1 << 13)
#define IPCORE_IDLE		(1 << 12)
#define SUBSAMP_MASK	(2 << 10)
#define CLR_FLAGS_MASK	(1 << 9)
#define GRAYSCALE_MASK	(1 << 8)
#define QUALITY_MASK	(0x7f << 1)
#define START_MASK		(1 << 0)

/* Subsampling modes */
#define SUBSAMP_444		(0x0 << 10)  /* 4:4:4 subsampling */
#define SUBSAMP_422		(0x1 << 10)  /* 4:2:2 subsampling */
#define SUBSAMP_420		(0x2 << 10)  /* 4:2:0 subsampling */
#define SUBSAMP_411		(0x3 << 10)  /* 4:1:1 subsampling */

/* Encoder register operations */
int jpeg_enc_reg_bit_set(struct ingenic_jpeg_dev *jpeg_dev, unsigned int offset, unsigned int val);
int jpeg_enc_reg_bit_clr(struct ingenic_jpeg_dev *jpeg_dev, unsigned int offset, unsigned int val);

/* Encoder helper functions */
void jpeg_enc_start(struct ingenic_jpeg_dev *jpeg_dev);
void jpeg_enc_set_quality(struct ingenic_jpeg_dev *jpeg_dev, char quality);
void jpeg_enc_clear_all_status(struct ingenic_jpeg_dev *jpeg_dev);
void jpeg_enc_input_is_yuv(struct ingenic_jpeg_dev *jpeg_dev);
void jpeg_enc_input_is_grayscale(struct ingenic_jpeg_dev *jpeg_dev);
void jpeg_enc_input_is_rgb(struct ingenic_jpeg_dev *jpeg_dev);
void jpeg_enc_set_input_subsamp(struct ingenic_jpeg_dev *jpeg_dev, int x);
void jpeg_enc_auto_subsampling(struct ingenic_jpeg_dev *jpeg_dev, u32 pixelformat);
void jpeg_enc_clr_int(struct ingenic_jpeg_dev *jpeg_dev);
void jpeg_enc_soft_reset(struct ingenic_jpeg_dev *jpeg_dev);

#endif /* _JPEG_ENCODER_HW_H */
