/*
 * Ingenic JPEG V4L2 Driver
 *
 * Copyright (C) 2025 Ingenic Semiconductor Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _INGENIC_JPEG_V4L2_H
#define _INGENIC_JPEG_V4L2_H

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/irq.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/ctype.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/memory.h>
#include <linux/suspend.h>
#include <linux/miscdevice.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/time.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-mem2mem.h>
//#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-dma-contig-ingenic.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>
#include <media/ingenic_video_nr.h>
#include <linux/of_reserved_mem.h>

#define INGENIC_JPEG_DRIVER_NAME "ingenic-jpeg-v4l2"

/* JPEG hardware maximum image size */
#define JPEG_MAX_WIDTH	4096
#define JPEG_MAX_HEIGHT	4096

/* JPEG formats */
enum jpeg_codec_mode {
	JPEG_UNDEFINED,
	JPEG_ENCODE,
	JPEG_DECODE,
};

/* DMA buffer structure */
struct jpeg_dma_buffer {
	void *virt;
	dma_addr_t dma_addr;
	unsigned int size;
};

struct ingenic_jpeg_dev {
	struct platform_device *pdev;
	enum jpeg_codec_mode mode;

	void __iomem *base;
	struct clk *clk;
	int irq;

	struct device *dev;
	struct v4l2_device v4l2_dev;
	struct video_device *vfd;
	struct v4l2_m2m_dev *m2m_dev;

	struct mutex dev_mutex;
	spinlock_t lock;
	struct completion completion;

	/* Current context */
	struct vb2_queue src_vq;
	struct vb2_queue dst_vq;
};

struct ingenic_jpeg_ctx {
	struct v4l2_fh fh;
	struct ingenic_jpeg_dev *dev;
	enum jpeg_codec_mode mode;

	/* Source and destination formats */
	struct v4l2_pix_format src_fmt;
	struct v4l2_pix_format dst_fmt;

	/* JPEG quality (for encoding) */
	int quality;

	/* JPEG编码器控制参数 */
	int restart_interval;        /* 重启间隔 */
	int active_markers;          /* 活动标记 */
	int min_output_buffers;      /* 最小输出缓冲区数 */
	int chroma_subsampling;      /* 色度子采样格式 */
	
	/* JPEG解码器控制参数 */
	int min_capture_buffers;     /* 最小捕获缓冲区数 */
	int detected_quality;        /* 检测到的JPEG质量（只读） */

	/* TODO: 添加子采样配置支持
	 * int subsampling;    // 子采样模式 (4:4:4, 4:2:2, 4:2:0, 4:1:1)
	 * int chroma_qp_offset; // 色度量化偏移
	 * bool luma_quantizer;  // 亮度量化器选择
	 * 需要添加相应的V4L2控制ID和硬件配置函数
	 */

	/* TODO: 添加高级编码参数
	 * int restart_interval;  // 重启间隔
	 * int smoothing_factor;  // 平滑因子
	 * bool optimize_huffman; // 霍夫曼表优化
	 * int compression_ratio; // 压缩比目标
	 */

	/* Controls */
	struct v4l2_ctrl_handler hdl;

	/* 当前处理的缓冲区 */
	struct vb2_v4l2_buffer *curr_src_buf;
	struct vb2_v4l2_buffer *curr_dst_buf;
	
	/* DMA buffers */
	struct jpeg_dma_buffer *src_buf;
	struct jpeg_dma_buffer *dst_buf;
};

/* External functions */
int ingenic_jpeg_probe(struct platform_device *pdev, enum jpeg_codec_mode mode, const struct v4l2_m2m_ops *m2m_ops);
int ingenic_jpeg_remove(struct platform_device *pdev);

/* V4L2 file operations */
extern const struct v4l2_file_operations ingenic_jpeg_fops;
extern const struct v4l2_ioctl_ops ingenic_jpeg_ioctl_ops;

#endif /* _INGENIC_JPEG_V4L2_H */
