/*
 * Ingenic JPEG V4L2 Driver Core
 *
 * Copyright (C) 2025 Ingenic Semiconductor Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "ingenic_jpeg_v4l2.h"


/* Codec mode constants */
#define INGENIC_JPEG_DECODER	JPEG_DECODE
#define INGENIC_JPEG_ENCODER	JPEG_ENCODE

/* Debug level definitions for VB2 buffer management */
#define VB2_DEBUG_LEVEL 4

/* Debug macros for VB2 buffer management */
#define vb2_dbg(level, dev, fmt, ...) do {		\
	if (VB2_DEBUG_LEVEL >= level)				\
		dev_dbg(dev, fmt, ##__VA_ARGS__);		\
} while (0)

#define vb2_info(dev, fmt, ...) dev_info(dev, fmt, ##__VA_ARGS__)
#define vb2_warn(dev, fmt, ...) dev_warn(dev, fmt, ##__VA_ARGS__)
#define vb2_err(dev, fmt, ...) dev_err(dev, fmt, ##__VA_ARGS__)

#define vb2_dbg_buffer(level, dev, vb, fmt, ...) do { 	\
	if (VB2_DEBUG_LEVEL >= level)						\
		dev_dbg(dev, "[buf %d] " fmt, vb->index, ##__VA_ARGS__);	\
} while (0)

/* Debug macros for JPEG operations */
#define jpeg_dbg(level, dev, fmt, ...) do {		\
	if (VB2_DEBUG_LEVEL >= level)				\
		dev_dbg(dev, fmt, ##__VA_ARGS__);		\
} while (0)

#define jpeg_info(dev, fmt, ...) jpeg_dbg(3, dev, fmt, ##__VA_ARGS__)
#define jpeg_warn(dev, fmt, ...) jpeg_dbg(2, dev, fmt, ##__VA_ARGS__)
#define jpeg_err(dev, fmt, ...) jpeg_dbg(1, dev, fmt, ##__VA_ARGS__)
#define jpeg_dbg_verbose(dev, fmt, ...) jpeg_dbg(4, dev, fmt, ##__VA_ARGS__)

/* Format descriptions for JPEG capture and output planes */
static struct ingenic_jpeg_format {
	u32 fourcc;
	int depth;
} ingenic_jpeg_formats[] = {
	{
		.fourcc = V4L2_PIX_FMT_JPEG,
		.depth = 8,
	},
	{
		.fourcc = V4L2_PIX_FMT_YUYV,
		.depth = 16,
	},
	{
		.fourcc = V4L2_PIX_FMT_RGB565,
		.depth = 16,
	},
	{
		.fourcc = V4L2_PIX_FMT_RGB24,
		.depth = 24,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV12,
		.depth = 12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_NV21,
		.depth		= 12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_YUV420,
		.depth		= 12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_YVU420,
		.depth		= 12,
	},
	{
		.fourcc		= V4L2_PIX_FMT_RGBA32,
		.depth		= 32,
	},
	{
		.fourcc		= V4L2_PIX_FMT_BGR24,
		.depth		= 24,
	},
	{
		.fourcc		= V4L2_PIX_FMT_GREY,
		.depth		= 8,
	},
};

/* Decoder-specific output formats */
static const struct ingenic_jpeg_format ingenic_jpeg_decoder_formats[] = {
	{
		.fourcc = V4L2_PIX_FMT_NV12,
		.depth = 12,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV21,
		.depth = 12,
	},
	{
		.fourcc = V4L2_PIX_FMT_YUV420,
		.depth = 12,
	},
	{
		.fourcc = V4L2_PIX_FMT_YVU420,
		.depth = 12,
	},
	{
		.fourcc = V4L2_PIX_FMT_RGB24,
		.depth = 24,
	},
	{
		.fourcc = V4L2_PIX_FMT_RGB565,
		.depth = 16,
	},
	{
		.fourcc = V4L2_PIX_FMT_ARGB32,
		.depth = 32,
	},
	{
		.fourcc = V4L2_PIX_FMT_RGBA32,
		.depth = 32,
	},
	{
		.fourcc = V4L2_PIX_FMT_BGR24,
		.depth = 24,
	},
	{
		.fourcc = V4L2_PIX_FMT_YUV422P,
		.depth = 16,
	},
};

#define INGENIC_JPEG_NUM_FORMATS ARRAY_SIZE(ingenic_jpeg_formats)
#define INGENIC_JPEG_DECODER_NUM_FORMATS ARRAY_SIZE(ingenic_jpeg_decoder_formats)

/* Helper functions */
static inline struct ingenic_jpeg_ctx *fh_to_ctx(struct v4l2_fh *fh)
{
	return container_of(fh, struct ingenic_jpeg_ctx, fh);
}

static inline struct ingenic_jpeg_ctx *ctrl_to_ctx(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct ingenic_jpeg_ctx, hdl);
}

static inline struct ingenic_jpeg_dev *ctx_to_dev(struct ingenic_jpeg_ctx *ctx)
{
	return ctx->dev;
}

/* Forward declarations */
static int ingenic_jpeg_s_ctrl(struct v4l2_ctrl *ctrl);

/* Control operations */
static const struct v4l2_ctrl_ops ingenic_jpeg_ctrl_ops = {
	.s_ctrl = ingenic_jpeg_s_ctrl,
};

static inline unsigned int ingenic_jpeg_find_depth(u32 pixelformat)
{
	int i;

	/* 首先在通用格式数组中查找 */
	for (i = 0; i < INGENIC_JPEG_NUM_FORMATS; i++) {
		if (ingenic_jpeg_formats[i].fourcc == pixelformat)
			return ingenic_jpeg_formats[i].depth;
	}
	
	/* 如果在通用数组中没找到，在解码器格式数组中查找 */
	for (i = 0; i < INGENIC_JPEG_DECODER_NUM_FORMATS; i++) {
		if (ingenic_jpeg_decoder_formats[i].fourcc == pixelformat)
			return ingenic_jpeg_decoder_formats[i].depth;
	}

	return 0;
}

/* Queue operations */
static int ingenic_jpeg_queue_setup(struct vb2_queue *vq,
		unsigned int *nbuffers,
		unsigned int *nplanes,
		unsigned int sizes[],
		struct device *alloc_devs[])
{
	struct ingenic_jpeg_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_pix_format *pix_fmt;
	const char *queue_type = V4L2_TYPE_IS_OUTPUT(vq->type) ? "OUTPUT" : "CAPTURE";

	vb2_dbg(1, ctx->dev->dev, "Queue setup: type=%s, req_buffers=%u, req_planes=%u\n",
		queue_type, *nbuffers, *nplanes);

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		pix_fmt = &ctx->src_fmt;
	else
		pix_fmt = &ctx->dst_fmt;

	vb2_dbg(2, ctx->dev->dev, "Queue %s format: width=%u, height=%u, sizeimage=%u, fourcc=%.4s\n",
		queue_type, pix_fmt->width, pix_fmt->height, pix_fmt->sizeimage,
		(char *)&pix_fmt->pixelformat);

	if (*nplanes) {
		vb2_dbg(2, ctx->dev->dev, "Queue %s: user provided planes=%u, size=%u\n",
			queue_type, *nplanes, sizes[0]);
		if (sizes[0] < pix_fmt->sizeimage) {
			vb2_err(ctx->dev->dev, "Queue %s: buffer too small (%u < %u)\n",
				queue_type, sizes[0], pix_fmt->sizeimage);
			return -EINVAL;
		}
	} else {
		sizes[0] = pix_fmt->sizeimage;
		*nplanes = 1;
		vb2_dbg(1, ctx->dev->dev, "Queue %s: set planes=%u, size=%u\n",
			queue_type, *nplanes, sizes[0]);
	}

	if (*nbuffers < 2) {
		vb2_dbg(1, ctx->dev->dev, "Queue %s: increasing buffers from %u to 2\n",
			queue_type, *nbuffers);
		*nbuffers = 2;
	}

	vb2_dbg(1, ctx->dev->dev, "Queue %s setup completed: buffers=%u, planes=%u, size=%u\n",
		queue_type, *nbuffers, *nplanes, sizes[0]);

	return 0;
}

static int ingenic_jpeg_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct ingenic_jpeg_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_pix_format *pix_fmt;
	const char *queue_type = V4L2_TYPE_IS_OUTPUT(vq->type) ? "OUTPUT" : "CAPTURE";
	unsigned int plane_size = vb2_plane_size(vb, 0);

	vb2_dbg_buffer(1, ctx->dev->dev, vb, "Buffer prepare: type=%s, plane_size=%u\n",
		queue_type, plane_size);

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		pix_fmt = &ctx->src_fmt;
	else
		pix_fmt = &ctx->dst_fmt;

	vb2_dbg_buffer(2, ctx->dev->dev, vb, "Required size: %u\n", pix_fmt->sizeimage);

	if (plane_size < pix_fmt->sizeimage) {
		vb2_err(ctx->dev->dev, "Buffer %d too small (%u < %u)\n",
			vb->index, plane_size, pix_fmt->sizeimage);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, pix_fmt->sizeimage);
	vb2_dbg_buffer(1, ctx->dev->dev, vb, "Buffer prepared successfully, payload=%u\n",
		pix_fmt->sizeimage);

	return 0;
}

static void ingenic_jpeg_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct ingenic_jpeg_ctx *ctx = vb2_get_drv_priv(vq);
	const char *queue_type = V4L2_TYPE_IS_OUTPUT(vq->type) ? "OUTPUT" : "CAPTURE";
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	// 使用vb2_info确保日志可见
	vb2_dbg(1, ctx->dev->dev, "Buffer queue: index=%d, type=%s, flags=0x%x\n",
		vb->index, queue_type, vbuf->flags);
	vb2_dbg(1, ctx->dev->dev, "Buffer address: 0x%p, length: %lu\n",
		vb2_plane_vaddr(vb, 0), (unsigned long)vb2_plane_size(vb, 0));

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);

	vb2_dbg(1, ctx->dev->dev, "Buffer %d queued successfully\n", vb->index);
}

static int ingenic_jpeg_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct ingenic_jpeg_ctx *ctx = vb2_get_drv_priv(vq);
	struct ingenic_jpeg_dev *dev = ctx->dev;
	const char *queue_type = V4L2_TYPE_IS_OUTPUT(vq->type) ? "OUTPUT" : "CAPTURE";
	int ret;

	vb2_dbg(1, dev->dev, "Start streaming: queue=%s, buffers=%u\n",
		queue_type, count);

	vb2_dbg(1, dev->dev, "Queue %s status: num_buffers=%u\n",
		queue_type, vq->num_buffers);

	ret = clk_prepare_enable(dev->clk);
	if (ret) {
		vb2_err(dev->dev, "Failed to enable clock: %d\n", ret);
		return ret;
	}

	vb2_dbg(1, dev->dev, "Clock enabled successfully\n");

	return 0;
}

static void ingenic_jpeg_stop_streaming(struct vb2_queue *vq)
{
	struct ingenic_jpeg_ctx *ctx = vb2_get_drv_priv(vq);
	struct ingenic_jpeg_dev *dev = ctx->dev;
	struct vb2_buffer *vb;
	unsigned int i;
	unsigned int cancelled = 0;
	unsigned int active = 0, queued = 0, done = 0, dequeued = 0;
	const char *queue_type = V4L2_TYPE_IS_OUTPUT(vq->type) ? "OUTPUT" : "CAPTURE";

	vb2_dbg(1, dev->dev, "Stop streaming: queue=%s\n", queue_type);

	/* 获取缓冲区状态统计 */
	for (i = 0; i < vq->num_buffers; i++) {
		vb = vq->bufs[i];
		if (!vb)
			continue;

		switch (vb->state) {
		case VB2_BUF_STATE_ACTIVE:
			active++;
			break;
		case VB2_BUF_STATE_QUEUED:
			queued++;
			break;
		case VB2_BUF_STATE_DONE:
			done++;
			break;
		case VB2_BUF_STATE_DEQUEUED:
			dequeued++;
			break;
		case VB2_BUF_STATE_IN_REQUEST:
		case VB2_BUF_STATE_PREPARING:
		case VB2_BUF_STATE_ERROR:
			/* 这些状态在停止流时会被正确处理，无需额外统计 */
			break;
		default:
			break;
		}
	}

	vb2_dbg(1, dev->dev, "Buffer states before cleanup - Active: %u, Queued: %u, Done: %u, Dequeued: %u\n",
		active, queued, done, dequeued);

	/* 遍历所有缓冲区，确保它们都被取消队列并处于非活动状态 */
	for (i = 0; i < vq->num_buffers; i++) {
		vb = vq->bufs[i];
		if (vb && (vb->state == VB2_BUF_STATE_ACTIVE || vb->state == VB2_BUF_STATE_QUEUED)) {
			const char *state_str = vb->state == VB2_BUF_STATE_ACTIVE ? "ACTIVE" : "QUEUED";
			vb2_dbg_buffer(1, dev->dev, vb, "Cancelling buffer in state %s\n", state_str);
			/* 将缓冲区标记为完成，这样vb2核心可以正确清理 */
			vb->state = VB2_BUF_STATE_DONE;
			cancelled++;
		}
	}

	if (cancelled > 0) {
		vb2_info(dev->dev, "Cancelled %u buffers during stop_streaming\n", cancelled);
	}

	/* 禁用时钟 */
	vb2_dbg(1, dev->dev, "Disabling clock\n");
	clk_disable_unprepare(dev->clk);

	vb2_dbg(1, dev->dev, "Streaming stopped for queue %s\n", queue_type);
}

static const struct vb2_ops ingenic_jpeg_qops = {
	.queue_setup	 = ingenic_jpeg_queue_setup,
	.buf_prepare	 = ingenic_jpeg_buf_prepare,
	.buf_queue	 = ingenic_jpeg_buf_queue,
	.start_streaming = ingenic_jpeg_start_streaming,
	.stop_streaming	 = ingenic_jpeg_stop_streaming,
	.wait_prepare	 = vb2_ops_wait_prepare,
	.wait_finish	 = vb2_ops_wait_finish,
};

/**
 * ingenic_jpeg_queue_init() - Initialize vb2 queue for a context
 *
 * This function is called by v4l2_m2m_ctx_init to initialize the source and
 * destination video queues for a specific instance.
 */
static int ingenic_jpeg_queue_init(void *priv, struct vb2_queue *src_vq, struct vb2_queue *dst_vq)
{
	struct ingenic_jpeg_ctx *ctx = priv;
	int ret;

	vb2_dbg(1, ctx->dev->dev, "Initializing VB2 queues\n");

	/* Initialize source queue */
	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &ingenic_jpeg_qops;
	src_vq->mem_ops = &ingenic_vb2_dma_contig_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->dev->dev_mutex;
	src_vq->dev = ctx->dev->dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	/* Initialize destination queue */
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &ingenic_jpeg_qops;
	dst_vq->mem_ops = &ingenic_vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->dev->dev_mutex;
	dst_vq->dev = ctx->dev->dev;

	ret = vb2_queue_init(dst_vq);
	if (ret)
		return ret;

	return 0;
}

/* V4L2 file operations */
static int ingenic_jpeg_open(struct file *file)
{
	struct ingenic_jpeg_dev *dev = video_drvdata(file);
	struct ingenic_jpeg_ctx *ctx = NULL;
	struct v4l2_ctrl_handler *hdl;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = dev;
	ctx->mode = dev->mode;
	
	v4l2_fh_init(&ctx->fh, dev->vfd);
	hdl = &ctx->hdl;
	
	/* 根据设备模式初始化不同的控制 */
	if (ctx->mode == INGENIC_JPEG_DECODER) {
		/* 解码器控制 - 解码器不需要设置quality，只提供查询功能 */
		v4l2_ctrl_handler_init(hdl, 2);
		/* 添加JPEG质量查询控制 - 只读，用于查询当前解码的JPEG质量 */
		v4l2_ctrl_new_std(hdl, &ingenic_jpeg_ctrl_ops, 
				  V4L2_CID_JPEG_COMPRESSION_QUALITY,
				  0, 100, 1, 0);
		/* 设置为只读模式 - 解码器不能设置质量，只能查询 */
		struct v4l2_ctrl *ctrl = v4l2_ctrl_find(hdl, V4L2_CID_JPEG_COMPRESSION_QUALITY);
		if (ctrl)
			ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;
		v4l2_ctrl_new_std(hdl, &ingenic_jpeg_ctrl_ops,
				  V4L2_CID_MIN_BUFFERS_FOR_CAPTURE,
				  1, 8, 1, 2);
	} else {
		/* 编码器控制 */
		v4l2_ctrl_handler_init(hdl, 3);
		v4l2_ctrl_new_std(hdl, &ingenic_jpeg_ctrl_ops, 
				  V4L2_CID_JPEG_COMPRESSION_QUALITY,
				  1, 100, 1, 50);
		v4l2_ctrl_new_std(hdl, &ingenic_jpeg_ctrl_ops,
				  V4L2_CID_JPEG_RESTART_INTERVAL,
				  0, 65535, 1, 0);

		v4l2_ctrl_new_std(hdl, &ingenic_jpeg_ctrl_ops,
				  V4L2_CID_MIN_BUFFERS_FOR_OUTPUT,
				  1, 8, 1, 2);
	}
	ctx->fh.ctrl_handler = hdl;
	if (hdl->error) {
		ret = hdl->error;
		v4l2_ctrl_handler_free(hdl);
		kfree(ctx);
		return ret;
	}

	/* 初始化控制参数的默认值 */
	if (ctx->mode == INGENIC_JPEG_DECODER) {
		/* 解码器默认值 */
		ctx->detected_quality = 0;  /* 初始为0，表示未检测到质量 */
		ctx->min_capture_buffers = 2;
	} else {
		/* 编码器默认值 */
		ctx->quality = 50;                    /* 默认JPEG质量 */
		ctx->restart_interval = 0;            /* 默认无重启间隔 */
		ctx->active_markers = V4L2_JPEG_ACTIVE_MARKER_DQT | V4L2_JPEG_ACTIVE_MARKER_DHT;
		ctx->min_output_buffers = 2;
	}

	/* Set default formats */
	ctx->src_fmt.width = 640;
	ctx->src_fmt.height = 480;
	ctx->src_fmt.pixelformat = V4L2_PIX_FMT_NV12;
	ctx->src_fmt.field = V4L2_FIELD_NONE;
	ctx->src_fmt.colorspace = V4L2_COLORSPACE_SRGB;
	
	ctx->dst_fmt.width = 640;
	ctx->dst_fmt.height = 480;
	ctx->dst_fmt.pixelformat = V4L2_PIX_FMT_JPEG;
	ctx->dst_fmt.field = V4L2_FIELD_NONE;
	ctx->dst_fmt.colorspace = V4L2_COLORSPACE_JPEG;

	/* Initialize the media context */
	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(dev->m2m_dev, ctx, &ingenic_jpeg_queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		v4l2_ctrl_handler_free(hdl);
		kfree(ctx);
		return ret;
	}

	v4l2_fh_add(&ctx->fh);
	file->private_data = &ctx->fh;

	return 0;
}

static int ingenic_jpeg_release(struct file *file)
{
	struct ingenic_jpeg_ctx *ctx = fh_to_ctx(file->private_data);

	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	v4l2_ctrl_handler_free(&ctx->hdl);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);

	return 0;
}

/* V4L2 ioctl operations */
static int vidioc_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	strncpy(cap->driver, INGENIC_JPEG_DRIVER_NAME, sizeof(cap->driver) - 1);
	strncpy(cap->card, INGENIC_JPEG_DRIVER_NAME, sizeof(cap->card) - 1);
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:ingenic-jpeg");

	cap->device_caps = V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int vidioc_enum_fmt(struct file *file, void *priv,
			   struct v4l2_fmtdesc *f)
{
	struct ingenic_jpeg_ctx *ctx = fh_to_ctx(priv);
	
	/* 根据设备模式选择相应的格式数组 */
	if (ctx->mode == INGENIC_JPEG_DECODER) {
		/* 解码器模式：使用解码器格式数组 */
		if (f->index >= INGENIC_JPEG_DECODER_NUM_FORMATS)
			return -EINVAL;
		f->pixelformat = ingenic_jpeg_decoder_formats[f->index].fourcc;
	} else {
		/* 编码器模式：使用通用格式数组 */
		if (f->index >= INGENIC_JPEG_NUM_FORMATS)
			return -EINVAL;
		f->pixelformat = ingenic_jpeg_formats[f->index].fourcc;
	}

	return 0;
}

static int vidioc_g_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct ingenic_jpeg_ctx *ctx = fh_to_ctx(priv);

	if (V4L2_TYPE_IS_OUTPUT(f->type))
		f->fmt.pix = ctx->src_fmt;
	else
		f->fmt.pix = ctx->dst_fmt;

	return 0;
}

static int ingenic_jpeg_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ingenic_jpeg_ctx *ctx = ctrl_to_ctx(ctrl);

	switch (ctrl->id) {
	case V4L2_CID_JPEG_COMPRESSION_QUALITY:
		/* 解码器模式下，quality是只读的，不允许设置 */
		if (ctx->mode == INGENIC_JPEG_DECODER) {
			jpeg_warn(ctx->dev->dev, "Cannot set quality on decoder (read-only)\n");
			return -EACCES;
		}
		ctx->quality = ctrl->val;
		jpeg_dbg(1, ctx->dev->dev, "Set JPEG quality to %d\n", ctrl->val);
		break;
	case V4L2_CID_JPEG_RESTART_INTERVAL:
		/* 重启间隔只对编码器有效 */
		if (ctx->mode == INGENIC_JPEG_DECODER) {
			jpeg_warn(ctx->dev->dev, "Restart interval not supported on decoder\n");
			return -EINVAL;
		}
		ctx->restart_interval = ctrl->val;
		jpeg_dbg(1, ctx->dev->dev, "Set restart interval to %d\n", ctrl->val);
		break;
	case V4L2_CID_JPEG_ACTIVE_MARKER:
		ctx->active_markers = ctrl->val;
		jpeg_dbg(1, ctx->dev->dev, "Set active markers to 0x%x\n", ctrl->val);
		break;
	case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:
		ctx->min_capture_buffers = ctrl->val;
		jpeg_dbg(1, ctx->dev->dev, "Set min capture buffers to %d\n", ctrl->val);
		break;
	case V4L2_CID_MIN_BUFFERS_FOR_OUTPUT:
		ctx->min_output_buffers = ctrl->val;
		jpeg_dbg(1, ctx->dev->dev, "Set min output buffers to %d\n", ctrl->val);
		break;
	default:
		jpeg_warn(ctx->dev->dev, "Unsupported control ID: 0x%x\n", ctrl->id);
		return -EINVAL;
	}

	return 0;
}

static int vidioc_try_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct ingenic_jpeg_ctx *ctx = fh_to_ctx(priv);
	int i;
	bool format_found = false;
	u64 estimated_size;
	unsigned int bpp;
	u64 line_size;
	u64 total_size;

	/* 根据设备模式选择相应的格式数组进行验证 */
	if (ctx->mode == INGENIC_JPEG_DECODER) {
		/* 解码器模式：检查解码器格式数组 */
		for (i = 0; i < INGENIC_JPEG_DECODER_NUM_FORMATS; i++) {
			if (ingenic_jpeg_decoder_formats[i].fourcc == pix->pixelformat) {
				format_found = true;
				break;
			}
		}
	} else {
		/* 编码器模式：检查通用格式数组 */
		for (i = 0; i < INGENIC_JPEG_NUM_FORMATS; i++) {
			if (ingenic_jpeg_formats[i].fourcc == pix->pixelformat) {
				format_found = true;
				break;
			}
		}
	}

	/* 如果格式未找到，设置默认格式 */
	if (!format_found) {
		if (ctx->mode == INGENIC_JPEG_DECODER) {
			/* 解码器默认输出NV12 */
			pix->pixelformat = V4L2_PIX_FMT_NV12;
		} else {
			/* 编码器默认输出JPEG */
			pix->pixelformat = V4L2_PIX_FMT_JPEG;
		}
	}

	/* 设置色彩空间 */
	if (pix->pixelformat == V4L2_PIX_FMT_JPEG) {
		pix->colorspace = V4L2_COLORSPACE_JPEG;
	} else {
		pix->colorspace = V4L2_COLORSPACE_SRGB;
	}

	/* 验证尺寸范围 */
	if (pix->width == 0 || pix->height == 0) {
		dev_err(ctx->dev->dev, "Invalid dimensions: %dx%d\n", pix->width, pix->height);
		return -EINVAL;
	}
	
	if (pix->width > JPEG_MAX_WIDTH || pix->height > JPEG_MAX_HEIGHT) {
		dev_err(ctx->dev->dev, "Dimensions %dx%d exceed maximum %dx%d\n", 
			pix->width, pix->height, JPEG_MAX_WIDTH, JPEG_MAX_HEIGHT);
		return -EINVAL;
	}
	
	/* 验证尺寸是否为16的倍数 */
	if ((pix->width % 16) != 0 || (pix->height % 16) != 0) {
		dev_err(ctx->dev->dev, "Dimensions %dx%d must be multiples of 16\n", 
			pix->width, pix->height);
		return -EINVAL;
	}

	/* 计算sizeimage字段 */
	if (pix->pixelformat == V4L2_PIX_FMT_JPEG) {
		/* 对于JPEG输出，估算大小 - 添加溢出检查 */
		estimated_size = (u64)pix->width * pix->height * 3 / 2;
		if (estimated_size > UINT_MAX) {
			/* 如果计算结果溢出，限制为最大安全值 */
			pix->sizeimage = JPEG_MAX_WIDTH * JPEG_MAX_HEIGHT * 3 / 2;
		} else {
			pix->sizeimage = max_t(u32, pix->sizeimage, (u32)estimated_size);
		}
		pix->bytesperline = 0;
	} else {
		/* 对于原始格式，基于格式计算 - 添加溢出检查 */
		bpp = ingenic_jpeg_find_depth(pix->pixelformat);
		line_size = (u64)pix->width * bpp;
		if (line_size > (u64)UINT_MAX * 8) {
			/* 如果每行字节数溢出，限制为最大安全值 */
			pix->bytesperline = (JPEG_MAX_WIDTH * bpp) >> 3;
		} else {
			pix->bytesperline = (pix->width * bpp) >> 3;
		}
		
		total_size = (u64)pix->height * pix->bytesperline;
		if (total_size > UINT_MAX) {
			/* 如果总大小溢出，限制为最大安全值 */
			pix->sizeimage = JPEG_MAX_HEIGHT * pix->bytesperline;
		} else {
			pix->sizeimage = pix->height * pix->bytesperline;
		}
	}

	return 0;
}

static int vidioc_s_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct ingenic_jpeg_ctx *ctx = fh_to_ctx(priv);
	struct vb2_queue *vq;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	int ret;

	/* Get the video queue */
	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	else if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	else
		return -EINVAL;

	/* If the queue is busy, we cannot change the format */
	if (vb2_is_busy(vq))
		return -EBUSY;

	ret = vidioc_try_fmt(file, priv, f);
	if (ret)
		return ret;

	/* Save the format */
	if (V4L2_TYPE_IS_OUTPUT(f->type))
		ctx->src_fmt = *pix;
	else
		ctx->dst_fmt = *pix;

	return 0;
}

static int vidioc_reqbufs(struct file *file, void *priv,
		  struct v4l2_requestbuffers *reqbufs)
{
	struct ingenic_jpeg_ctx *ctx = fh_to_ctx(priv);
	const char *type_str;
	int ret;

	type_str = V4L2_TYPE_IS_OUTPUT(reqbufs->type) ? "OUTPUT" : "CAPTURE";

	vb2_dbg(1, ctx->dev->dev, "Request buffers: type=%s, count=%u, memory=%d\n",
		type_str, reqbufs->count, reqbufs->memory);

	ret = v4l2_m2m_reqbufs(file, ctx->fh.m2m_ctx, reqbufs);

	vb2_dbg(1, ctx->dev->dev, "Request buffers result: ret=%d, count=%u\n",
		ret, reqbufs->count);

	return ret;
}

static int vidioc_querybuf(struct file *file, void *priv,
		   struct v4l2_buffer *buf)
{
	struct ingenic_jpeg_ctx *ctx = fh_to_ctx(priv);
	const char *type_str;
	int ret;

	type_str = V4L2_TYPE_IS_OUTPUT(buf->type) ? "OUTPUT" : "CAPTURE";

	vb2_dbg(1, ctx->dev->dev, "Query buffer: index=%u, type=%s\n",
		buf->index, type_str);

	ret = v4l2_m2m_querybuf(file, ctx->fh.m2m_ctx, buf);

	if (ret == 0) {
		vb2_dbg(2, ctx->dev->dev, "Buffer %u: length=%u, offset=%u, flags=0x%x\n",
			buf->index, buf->length, buf->m.offset, buf->flags);
	}

	return ret;
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct ingenic_jpeg_ctx *ctx = fh_to_ctx(priv);
	const char *type_str;
	int ret;

	type_str = V4L2_TYPE_IS_OUTPUT(buf->type) ? "OUTPUT" : "CAPTURE";

	// 使用vb2_info确保日志可见
	vb2_dbg(1, ctx->dev->dev, "Queue buffer: index=%u, type=%s, bytesused=%u, flags=0x%x\n",
		buf->index, type_str, buf->bytesused, buf->flags);

	ret = v4l2_m2m_qbuf(file, ctx->fh.m2m_ctx, buf);

	if (ret == 0)
		vb2_dbg(1, ctx->dev->dev, "Buffer %u queued successfully\n", buf->index);
	else
		vb2_err(ctx->dev->dev, "Failed to queue buffer %u: %d\n", buf->index, ret);

	return ret;
}

static int vidioc_dqbuf(struct file *file, void *priv,
		   struct v4l2_buffer *buf)
{
	struct ingenic_jpeg_ctx *ctx = fh_to_ctx(priv);
	const char *type_str;
	int ret;

	type_str = V4L2_TYPE_IS_OUTPUT(buf->type) ? "OUTPUT" : "CAPTURE";

	vb2_dbg(1, ctx->dev->dev, "Dequeue buffer: type=%s\n", type_str);

	ret = v4l2_m2m_dqbuf(file, ctx->fh.m2m_ctx, buf);

	if (ret == 0) {
		vb2_dbg(1, ctx->dev->dev, "Buffer %u dequeued: bytesused=%u, flags=0x%x, field=%d\n",
			buf->index, buf->bytesused, buf->flags, buf->field);
	}

	return ret;
}

static int vidioc_streamon(struct file *file, void *priv,
		   enum v4l2_buf_type type)
{
	struct ingenic_jpeg_ctx *ctx = fh_to_ctx(priv);
	const char *type_str;
	int ret;

	type_str = V4L2_TYPE_IS_OUTPUT(type) ? "OUTPUT" : "CAPTURE";

	vb2_dbg(1, ctx->dev->dev, "Stream on: type=%s\n", type_str);

	ret = v4l2_m2m_streamon(file, ctx->fh.m2m_ctx, type);

	if (ret == 0)
		vb2_dbg(1, ctx->dev->dev, "Stream %s started successfully\n", type_str);
	else
		vb2_err(ctx->dev->dev, "Failed to start stream %s: %d\n", type_str, ret);

	return ret;
}

static int vidioc_streamoff(struct file *file, void *priv,
		    enum v4l2_buf_type type)
{
	struct ingenic_jpeg_ctx *ctx = fh_to_ctx(priv);
	const char *type_str;
	int ret;

	type_str = V4L2_TYPE_IS_OUTPUT(type) ? "OUTPUT" : "CAPTURE";

	vb2_dbg(1, ctx->dev->dev, "Stream off: type=%s\n", type_str);

	ret = v4l2_m2m_streamoff(file, ctx->fh.m2m_ctx, type);

	if (ret == 0)
		vb2_dbg(1, ctx->dev->dev, "Stream %s stopped successfully\n", type_str);
	else
		vb2_err(ctx->dev->dev, "Failed to stop stream %s: %d\n", type_str, ret);

	return ret;
}

static int vidioc_subscribe_event(struct v4l2_fh *fh,
				  const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subscribe(fh, sub);
	case V4L2_EVENT_EOS:
		return v4l2_event_subscribe(fh, sub, 2, NULL);
	default:
		return v4l2_ctrl_subscribe_event(fh, sub);
	}
}

/* 包装函数：将标准file_operations mmap接口转换为v4l2_m2m_mmap */
static int ingenic_jpeg_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct v4l2_fh *fh = file->private_data;
	struct ingenic_jpeg_ctx *ctx = fh_to_ctx(fh);

	return v4l2_m2m_mmap(file, ctx->fh.m2m_ctx, vma);
}

/* V4L2 file operations */
const struct v4l2_file_operations ingenic_jpeg_fops = {
	.owner = THIS_MODULE,
	.open = ingenic_jpeg_open,
	.release = ingenic_jpeg_release,
	.unlocked_ioctl = video_ioctl2,
	.mmap = ingenic_jpeg_mmap,
};

/* V4L2 ioctl operations */
const struct v4l2_ioctl_ops ingenic_jpeg_ioctl_ops = {
	.vidioc_querycap = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt,
	.vidioc_enum_fmt_vid_out = vidioc_enum_fmt,
	.vidioc_g_fmt_vid_cap = vidioc_g_fmt,
	.vidioc_g_fmt_vid_out = vidioc_g_fmt,
	.vidioc_try_fmt_vid_cap = vidioc_try_fmt,
	.vidioc_try_fmt_vid_out = vidioc_try_fmt,
	.vidioc_s_fmt_vid_cap = vidioc_s_fmt,
	.vidioc_s_fmt_vid_out = vidioc_s_fmt,
	.vidioc_reqbufs = vidioc_reqbufs,
	.vidioc_querybuf = vidioc_querybuf,
	.vidioc_qbuf = vidioc_qbuf,
	.vidioc_dqbuf = vidioc_dqbuf,
	.vidioc_streamon = vidioc_streamon,
	.vidioc_streamoff = vidioc_streamoff,
	.vidioc_subscribe_event = vidioc_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

/* Platform driver probe function */
int ingenic_jpeg_probe(struct platform_device *pdev, enum jpeg_codec_mode mode, const struct v4l2_m2m_ops *m2m_ops)
{
	struct ingenic_jpeg_dev *jpeg_dev;
	struct resource *res;
	int ret;
	int video_nr = mode == JPEG_DECODE ? INGENIC_JPEGD_VIDEO_NR : INGENIC_JPEGE_VIDEO_NR;

	dev_info(&pdev->dev, "Ingenic JPEG V4L2 driver probe\n");

	jpeg_dev = devm_kzalloc(&pdev->dev, sizeof(*jpeg_dev), GFP_KERNEL);
	if (!jpeg_dev)
		return -ENOMEM;

	jpeg_dev->mode = mode;
	jpeg_dev->pdev = pdev;
	jpeg_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, jpeg_dev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	jpeg_dev->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(jpeg_dev->base))
		return PTR_ERR(jpeg_dev->base);

	if (mode == JPEG_DECODE)
		jpeg_dev->clk = devm_clk_get(&pdev->dev, "gate_jpegd");
	else
		jpeg_dev->clk = devm_clk_get(&pdev->dev, "gate_jpege");
		
	if (IS_ERR(jpeg_dev->clk)) {
		dev_err(&pdev->dev, "failed to get jpeg clock\n");
		return PTR_ERR(jpeg_dev->clk);
	}

	jpeg_dev->irq = platform_get_irq(pdev, 0);
	if (jpeg_dev->irq < 0) {
		dev_err(&pdev->dev, "failed to get jpeg irq\n");
		return jpeg_dev->irq;
	}

	mutex_init(&jpeg_dev->dev_mutex);
	spin_lock_init(&jpeg_dev->lock);
	init_completion(&jpeg_dev->completion);

	/* Initialize V4L2 components */
	ret = v4l2_device_register(&pdev->dev, &jpeg_dev->v4l2_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register v4l2 device\n");
		return ret;
	}


	jpeg_dev->m2m_dev = v4l2_m2m_init(m2m_ops);
	if (IS_ERR(jpeg_dev->m2m_dev)) {
		dev_err(&pdev->dev, "Failed to initialize v4l2 m2m device\n");
		ret = PTR_ERR(jpeg_dev->m2m_dev);
		goto err_v4l2;
	}

	jpeg_dev->vfd = video_device_alloc();
	if (!jpeg_dev->vfd) {
		ret = -ENOMEM;
		goto err_m2m;
	}

	jpeg_dev->vfd->fops = &ingenic_jpeg_fops;
	jpeg_dev->vfd->ioctl_ops = &ingenic_jpeg_ioctl_ops;
	jpeg_dev->vfd->release = video_device_release;
	jpeg_dev->vfd->lock = &jpeg_dev->dev_mutex;
	jpeg_dev->vfd->v4l2_dev = &jpeg_dev->v4l2_dev;
	jpeg_dev->vfd->vfl_dir = VFL_DIR_M2M;
	jpeg_dev->vfd->device_caps = V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING;
	if (mode == JPEG_DECODE)
		strncpy(jpeg_dev->vfd->name, "ingenic-jpeg-decoder", sizeof(jpeg_dev->vfd->name));
	else
		strncpy(jpeg_dev->vfd->name, "ingenic-jpeg-encoder", sizeof(jpeg_dev->vfd->name));

	video_set_drvdata(jpeg_dev->vfd, jpeg_dev);

	/* 验证设备号范围 */
	if (video_nr < 0 || video_nr > 63) {
		dev_err(&pdev->dev, "Invalid video device number %d, must be between 0-63\n", video_nr);
		ret = -EINVAL;
		goto err_m2m;
	}

	ret = video_register_device(jpeg_dev->vfd, VFL_TYPE_VIDEO, video_nr);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register video device with nr %d: %d\n", video_nr, ret);
		if (ret == -ENFILE) {
			dev_err(&pdev->dev, "Too many video devices registered or device number %d already in use\n", video_nr);
		} else if (ret == -EINVAL) {
			dev_err(&pdev->dev, "Invalid device number %d or device already registered\n", video_nr);
		}
		goto err_video;
	}

	ret =  of_reserved_mem_device_init(&pdev->dev);
	if(ret < 0) {
		dev_warn(&pdev->dev, "Failed to init reserved memory\n");
	}

	v4l2_info(&jpeg_dev->v4l2_dev, "Ingenic JPEG %s registered as %s\n",
		  mode == JPEG_DECODE ? "decoder" : "encoder",
		  video_device_node_name(jpeg_dev->vfd));

	return 0;

err_video:
	video_device_release(jpeg_dev->vfd);
err_m2m:
	v4l2_m2m_release(jpeg_dev->m2m_dev);
err_v4l2:
	v4l2_device_unregister(&jpeg_dev->v4l2_dev);
	return ret;
}

/* Platform driver remove function */
int ingenic_jpeg_remove(struct platform_device *pdev)
{
	struct ingenic_jpeg_dev *jpeg_dev = platform_get_drvdata(pdev);

	if (!jpeg_dev)
		return 0;

	v4l2_info(&jpeg_dev->v4l2_dev, "Unregistering Ingenic JPEG %s\n",
		  jpeg_dev->mode == JPEG_DECODE ? "decoder" : "encoder");
	
	video_unregister_device(jpeg_dev->vfd);
	v4l2_m2m_release(jpeg_dev->m2m_dev);
	v4l2_device_unregister(&jpeg_dev->v4l2_dev);

	return 0;
}


/* Module initialization and exit */
static int __init ingenic_jpeg_init(void)
{
	// Module initialization is done in the decoder/encoder drivers
	return 0;
}

static void __exit ingenic_jpeg_exit(void)
{
	// Module exit is done in the decoder/encoder drivers
}

module_init(ingenic_jpeg_init);
module_exit(ingenic_jpeg_exit);

MODULE_AUTHOR("Ingenic Semiconductor Co., Ltd.");
MODULE_DESCRIPTION("Ingenic JPEG V4L2 Driver Core");
MODULE_LICENSE("GPL v2");