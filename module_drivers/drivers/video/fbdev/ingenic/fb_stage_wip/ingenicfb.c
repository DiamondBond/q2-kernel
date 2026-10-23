/*
 * drivers/video/fbdev/ingenic/x2000_v12/ingenicfb.c
 *
 * Copyright (c) 2020 Ingenic Semiconductor Co., Ltd.
 *              http://www.ingenic.com/
 *
 * Core file for Ingenic Display Controller driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/irq.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/ctype.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/memory.h>
#include <linux/suspend.h>
#include <linux/string.h>
#include <linux/kthread.h>
#include <linux/gpio.h>
#include <asm/cacheflush.h>
#include <linux/of_address.h>
#include <linux/dma-buf.h>
#include "dpu_reg.h"
#include "ingenicfb.h"
#include "hw_composer_fb.h"
#include "hw_composer_v4l2.h"
#include <libdmmu.h>
#include <linux/of_reserved_mem.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/overflow.h>
#include "x2600_rotate.h"

#ifdef CONFIG_SOC_X2600
#include "x2600_rotate.h"
#define X2600_DDR_CCHC0 0xb3012024
#define X2600_DDR_CCHC1 0xb3012028
#define X2600_DDR_CCHC0_VALUE 0x88404010
#define X2600_DDR_CCHC1_VALUE 0x88404030
#endif

#define dpu_debug 1
#define print_dbg(f, arg...) if(dpu_debug) printk(KERN_INFO "dpu: %s, %d: " f "\n", __func__, __LINE__, ## arg)

#define COMPOSER_DIRECT_OUT_EN

#include "./jz_mipi_dsi/jz_mipi_dsih_hal.h"
#include "./jz_mipi_dsi/jz_mipi_dsi_regs.h"
#include "./jz_mipi_dsi/jz_mipi_dsi_lowlevel.h"
extern struct dsi_device * jzdsi_init(struct jzdsi_data *pdata);
extern int dsi_change_video_config(struct dsi_device *dsi,struct lcd_panel *panel);
extern void jzdsi_remove(struct dsi_device *dsi);
extern void dump_dsi_reg(struct dsi_device *dsi);
int jz_mipi_dsi_set_client(struct dsi_device *dsi, int power);
static const struct of_device_id ingenicfb_of_match[];
#ifdef CONFIG_TRUE_COLOR_LOGO
#include <video/ingenic_logo.h>
extern unsigned char logo_buf_initdata[] __initdata; /* obj/drivers/video/logo-ingenic/logo.c */
extern void show_logo(struct fb_info *info);
static unsigned char * copyed_logo_buf = NULL;
static unsigned char *logo_early_buf = NULL;
#endif

struct lcd_panel *fbdev_panel = NULL;
struct platform_device *fbdev_pdev = NULL;

static int showFPS = 0;

static bool panel_mipi_init_ok = false;
static struct ingenicfb_device *fbdev;

#ifdef CONFIG_FB_VSYNC_SKIP_DISABLE
__maybe_unused static unsigned int old_desc_addr = 0;
#endif
/* #define TEST_IRQ */

const struct fb_fix_screeninfo ingenicfb_fix  = {
	.id = "ingenicfb",
	.type = FB_TYPE_PACKED_PIXELS,
	.visual = FB_VISUAL_TRUECOLOR,
	.xpanstep = 0,
	.ypanstep = 1,
	.ywrapstep = 0,
	.accel = FB_ACCEL_NONE,
};

struct ingenicfb_colormode {
	uint32_t mode;
	const char *name;
	uint32_t color;
	uint32_t bits_per_pixel;
	uint32_t nonstd;
	struct fb_bitfield red;
	struct fb_bitfield green;
	struct fb_bitfield blue;
	struct fb_bitfield transp;
};

struct ingenic_priv {
	unsigned int dsi_iobase;
	unsigned int dsi_phy_iobase;
	bool support_comp;
};

/* 这个实际是comp支持的, 应该放在compfb中。这里应该只记录rdma支持的格式.*/
static struct ingenicfb_colormode ingenicfb_colormodes[] = {
	{
		.mode = RDMA_CHAIN_CFG_FORMAT_888,
		.name = "rgb888",
		.bits_per_pixel = 32,
		.nonstd = 0,
#ifdef CONFIG_FB_FORMAT_X8B8G8R8
		.color = RDMA_CHAIN_CFG_COLOR_BGR,
#else
		.color = RDMA_CHAIN_CFG_COLOR_RGB,
#endif
		.red	= { .length = 8, .offset = 16, .msb_right = 0 },
		.green	= { .length = 8, .offset = 8, .msb_right = 0 },
		.blue	= { .length = 8, .offset = 0, .msb_right = 0 },
		.transp	= { .length = 0, .offset = 0, .msb_right = 0 },
	}, {
		.mode = RDMA_CHAIN_CFG_FORMAT_555,
		.name = "rgb555",
		.color = RDMA_CHAIN_CFG_COLOR_RGB,
		.bits_per_pixel = 16,
		.nonstd = 0,
		.red	= { .length = 5, .offset = 10, .msb_right = 0 },
		.green	= { .length = 5, .offset = 5, .msb_right = 0 },
		.blue	= { .length = 5, .offset = 0, .msb_right = 0 },
		.transp	= { .length = 0, .offset = 0, .msb_right = 0 },
	}, {
		.mode = RDMA_CHAIN_CFG_FORMAT_ARGB1555,
		.name = "argb1555",
		.color = RDMA_CHAIN_CFG_COLOR_RGB,
		.bits_per_pixel = 16,
		.nonstd = 0,
		.red	= { .length = 5, .offset = 10, .msb_right = 0 },
		.green	= { .length = 5, .offset = 5, .msb_right = 0 },
		.blue	= { .length = 5, .offset = 0, .msb_right = 0 },
		.transp	= { .length = 1, .offset = 15, .msb_right = 0 },
	}, {
		.mode = RDMA_CHAIN_CFG_FORMAT_565,
		.name = "rgb565",
		.color = RDMA_CHAIN_CFG_COLOR_RGB,
		.bits_per_pixel = 16,
		.nonstd = 0,
		.red	= { .length = 5, .offset = 11, .msb_right = 0 },
		.green	= { .length = 6, .offset = 5, .msb_right = 0 },
		.blue	= { .length = 5, .offset = 0, .msb_right = 0 },
		.transp	= { .length = 0, .offset = 0, .msb_right = 0 },
	},
};

static int ingenicfb_dmmu_mm_release(void *data)
{
	struct ingenicfb_device *fbdev = (struct ingenicfb_device *)data;

	hw_composer_lock(fbdev->comp_ctx);
	hw_composer_stop(fbdev->comp_ctx);
	hw_composer_unlock(fbdev->comp_ctx);

	return 0;
}


static struct dmmu_mm_ops ingenicfb_dmmu_mm_ops = {
	.mm_release = ingenicfb_dmmu_mm_release,
};

static void
ingenicfb_videomode_to_var(struct fb_var_screeninfo *var,
		const struct fb_videomode *mode)
{
	var->xres = mode->xres;
	var->yres = mode->yres;
	var->xres_virtual = mode->xres;
	var->yres_virtual = mode->yres * CONFIG_FB_INGENIC_NR_FRAMES;
	var->xoffset = 0;
	var->yoffset = 0;
	var->left_margin = mode->left_margin;
	var->right_margin = mode->right_margin;
	var->upper_margin = mode->upper_margin;
	var->lower_margin = mode->lower_margin;
	var->hsync_len = mode->hsync_len;
	var->vsync_len = mode->vsync_len;
	var->sync = mode->sync;
	var->vmode = mode->vmode & FB_VMODE_MASK;
	var->pixclock = mode->pixclock;
}

static struct fb_videomode *ingenicfb_get_mode(struct fb_var_screeninfo *var,
		struct fb_info *info)
{
	size_t i;
	struct ingenicfb_device *fbdev = info->par;
	struct lcd_panel *panel = fbdev->panel;
	struct fb_videomode *mode = NULL;
	for (i = 0; i < fbdev->video_modes_num; i++) {
		mode = fbdev->video_mode[i];
		if (mode->xres == var->xres && mode->yres == var->yres
				&& mode->vmode == var->vmode
				&& mode->right_margin == var->right_margin) {
			if (fbdev->panel->lcd_type != LCD_TYPE_SLCD) {
				if (mode->pixclock == var->pixclock) {
					panel->cur_active_videomode = i;
					return mode;
				}
			} else {
				panel->cur_active_videomode = i;
				return mode;
			}
		}
	}

	return NULL;
}

static void ingenicfb_colormode_to_var(struct fb_var_screeninfo *var,
		struct ingenicfb_colormode *color)
{
	var->bits_per_pixel = color->bits_per_pixel;
	var->nonstd = color->nonstd;
	var->red = color->red;
	var->green = color->green;
	var->blue = color->blue;
	var->transp = color->transp;
}

static bool cmp_var_to_colormode(struct fb_var_screeninfo *var,
		struct ingenicfb_colormode *color)
{
	bool cmp_component(struct fb_bitfield *f1, struct fb_bitfield *f2)
	{
		return f1->length == f2->length &&
			f1->offset == f2->offset &&
			f1->msb_right == f2->msb_right;
	}

	if (var->bits_per_pixel == 0 ||
			var->red.length == 0 ||
			var->blue.length == 0 ||
			var->green.length == 0)
		return 0;

	return var->bits_per_pixel == color->bits_per_pixel &&
		cmp_component(&var->red, &color->red) &&
		cmp_component(&var->green, &color->green) &&
		cmp_component(&var->blue, &color->blue) &&
		cmp_component(&var->transp, &color->transp);
}

static struct ingenicfb_colormode *ingenicfb_check_colormode(struct fb_var_screeninfo *var)
{
	int i;

	if (var->nonstd) {
		for (i = 0; i < ARRAY_SIZE(ingenicfb_colormodes); ++i) {
			struct ingenicfb_colormode *m = &ingenicfb_colormodes[i];
			if (var->nonstd == m->nonstd) {
				ingenicfb_colormode_to_var(var, m);
				return m;
			}
		}

		return NULL;
	}

	for (i = 0; i < ARRAY_SIZE(ingenicfb_colormodes); ++i) {
		struct ingenicfb_colormode *m = &ingenicfb_colormodes[i];
		if (cmp_var_to_colormode(var, m)) {
			ingenicfb_colormode_to_var(var, m);
			return m;
		}
	}
	/* To support user libraries that only support RGB format */
	for (i = 0; i < ARRAY_SIZE(ingenicfb_colormodes); ++i) {
		struct ingenicfb_colormode *m = &ingenicfb_colormodes[i];
		if (var->bits_per_pixel == m->bits_per_pixel) {
			ingenicfb_colormode_to_var(var, m);
			return m;
		}
	}

	return NULL;
}

static int ingenicfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct ingenicfb_device *fbdev = info->par;
	struct fb_videomode *mode;
	struct ingenicfb_colormode *color_mode;
	mode = ingenicfb_get_mode(var, info);
	if (mode == NULL) {
		dev_err(info->dev, "%s get video mode failed\n", __func__);
		return -EINVAL;
	}

	ingenicfb_videomode_to_var(var, mode);

	/*TODO:不应该修改var.*/
	color_mode = ingenicfb_check_colormode(var);
	if(color_mode == NULL) {
		dev_err(info->dev,"%s Check colormode failed!\n",__func__);
		return  -EINVAL;
	}

	fbdev->color_mode = color_mode;

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2
static int jzfb_dmabuf_get_phy(struct device *dev,int fd,unsigned int *phyaddr)
{
	struct dma_buf_attachment *attach;
	struct dma_buf *dbuf;
	struct sg_table *sgt;
	int err = 0;
	dbuf = dma_buf_get(fd);
	if (IS_ERR(dbuf))
		return -EINVAL;
	attach = dma_buf_attach(dbuf, dev);
	if (IS_ERR(attach)) {
		err = -EINVAL;
		goto fail_attach;
	}
	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		err = -EINVAL;
		goto fail_map;
	}

	*phyaddr = sg_dma_address(sgt->sgl);

	dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
fail_map:
	dma_buf_detach(dbuf, attach);
fail_attach:
	dma_buf_put(dbuf);
	return err;
}
#endif


#define DPU_WAIT_IRQ_TIME 8000
static int ingenicfb_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct comp_setup_info comp_info;
	struct ingenicfb_device *fbdev = info->par;
	int ret = 0;
	csc_mode_t csc_mode;
	int value;
	switch (cmd) {
        case JZFB_SET_ROT_ANGLE:
            fbdev->dctrl.rot_cfg.rot_angle_set = arg;
            break;
        case JZFB_GET_ROT_ANGLE:
            if (unlikely(copy_to_user(argp,&fbdev->dctrl.rot_cfg.rot_angle_set, sizeof(int))))
				return -EFAULT;
			break;

		case JZFB_SET_VSYNCINT:
			if (unlikely(copy_from_user(&value, argp, sizeof(int))))
				return -EFAULT;
			break;
		case FBIO_WAITFORVSYNC:
			if(fbdev->dctrl.chan == DATA_CH_COMP)
				break;
#ifndef CONFIG_FB_VSYNC_SKIP_DISABLE
			unlock_fb_info(info);
			if(fbdev->timestamp.wp == fbdev->timestamp.rp) {
				ret = wait_event_interruptible_timeout(fbdev->vsync_wq,
				fbdev->timestamp.wp != fbdev->timestamp.rp,
					msecs_to_jiffies(DPU_WAIT_IRQ_TIME));
			}else
				ret = 1;
			lock_fb_info(info);
			if(ret == 0) {
				dev_err(info->dev, "DPU wait vsync timeout!\n");
				return -EFAULT;
			}

			ret = copy_to_user(argp, fbdev->timestamp.value + fbdev->timestamp.rp,
					sizeof(u64));

			if (unlikely(ret))
				return -EFAULT;
#else
			{
				unlock_fb_info(info);
				fbdev->timestamp.rp = fbdev->timestamp.wp;
				ret = wait_event_interruptible_timeout(fbdev->vsync_wq,
						fbdev->timestamp.wp != fbdev->timestamp.rp,
						msecs_to_jiffies(DPU_WAIT_IRQ_TIME));
				lock_fb_info(info);
				if(ret == 0) {
					dev_err(info->dev, "DPU wait vsync timeout!\n");
					return -EFAULT;
				}
			}
#endif
			dpu_ctrl_rdma_wait_for_vsync(&fbdev->dctrl);
			break;
		case JZFB_PUT_FRM_CFG:
			copy_from_user(&comp_info.frm_cfg, (void *)argp, sizeof(struct ingenicfb_frm_cfg));


			comp_info.nframes = 1;
			//comp_info.out_mode = COMP_WRITE_BACK;

			/*设置composer帧信息，并且启动一次comp设备进行合成.*/
			hw_composer_lock(fbdev->comp_ctx);

			if(fbdev->dctrl.blank == 0){
				hw_composer_force_update(fbdev->comp_ctx);
				hw_composer_setup(fbdev->comp_ctx, &comp_info);
				ret = hw_composer_start(fbdev->comp_ctx);
				if (!ret)
					hw_composer_wait_done(fbdev->comp_ctx); //TODO 阻塞等待当前帧合成完成.
			}

			hw_composer_unlock(fbdev->comp_ctx);
			if (ret)
				return ret;
			break;
		case JZFB_GET_FRM_CFG:
			copy_to_user((void *)argp,
				      &fbdev->comp_ctx->comp_info.frm_cfg,
				      sizeof(struct ingenicfb_frm_cfg));
			break;
		case JZFB_GET_LAYERS_NUM:
			{
				unsigned int layers_num = CONFIG_FB_INGENIC_NR_LAYERS;
				copy_to_user((void *)argp,
					      &layers_num,
					      sizeof(unsigned int));
			}
			break;
		case JZFB_SET_CSC_MODE:
			if (unlikely(copy_from_user(&csc_mode, argp, sizeof(csc_mode_t))))
				return -EFAULT;

			//TODO:
			//csc_mode_set(fbdev, csc_mode);
			break;
		case JZFB_USE_TLB:
			if((unsigned int)arg != 0){
				fbdev->dctrl.tlba = (unsigned int)arg;

				dmmu_register_mm_notifier(&fbdev->dmn);
			} else
				printk("tlb err!!!\n");
			break;
		case JZFB_DMMU_MEM_SMASH:
			{
				struct smash_mode sm;
				if (copy_from_user(&sm, (void *)arg, sizeof(sm))) {
					ret = -EFAULT;
					break;
				}
				return dmmu_memory_smash(sm.vaddr, sm.mode);
				break;
			}
		case JZFB_DMMU_DUMP_MAP:
			{
				unsigned long vaddr;
				if (copy_from_user(&vaddr, (void *)arg, sizeof(unsigned long))) {
					ret = -EFAULT;
					break;
				}
				return dmmu_dump_map(vaddr);
				break;
			}
		case JZFB_DMMU_MAP:
			{
				struct dpu_dmmu_map_info di;
				if (copy_from_user(&di, (void *)arg, sizeof(di))) {
					ret = -EFAULT;
					break;
				}
				fbdev->user_addr = di.addr;
				return dmmu_map(fbdev->dev,di.addr,di.len);
				break;
			}
		case JZFB_DMMU_UNMAP:
			{
				struct dpu_dmmu_map_info di;
				if (copy_from_user(&di, (void *)arg, sizeof(di))) {
					ret = -EFAULT;
					break;
				}
				  if (dpu_ctrl_comp_stop(&fbdev->dctrl,  GEN_STOP) )
					        printk("*** comp stop fail\n");
				return dmmu_unmap(fbdev->dev,di.addr,di.len);
				break;
			}
		case JZFB_DMMU_UNMAP_ALL:
			dmmu_unmap_all(fbdev->dev);
			break;
		case JZFB_DMMU_FLUSH_CACHE:
			{
				struct dpu_dmmu_map_info di;
				if (copy_from_user(&di, (void *)arg, sizeof(di))) {
					ret = -EFAULT;
					break;
				}
				return dmmu_flush_cache(di.addr,di.len);
				break;
			}
#ifdef CONFIG_VIDEO_V4L2
		case JZFB_GET_DMA_PHY:
			{
				unsigned int phyaddr = 0;
				int fd = 0;
				if (copy_from_user(&fd, (void *)arg, sizeof(fd))) {
					dev_err(info->dev, "copy_from_user error!!!\n");
					ret = -EFAULT;
					break;
				}
				ret = jzfb_dmabuf_get_phy(info->dev,fd,&phyaddr);
				if (copy_to_user((void *)arg, &phyaddr, sizeof(phyaddr)))
					return -EFAULT;
			}
			break;
#endif
		default:
			printk("Command:%x Error!\n",cmd);
			break;
	}
	return 0;
}

int ingenicfb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	unsigned long start;
	unsigned long off;
	u32 len;

	off = vma->vm_pgoff << PAGE_SHIFT;

	start = info->fix.smem_start;
	len = PAGE_ALIGN((start & ~PAGE_MASK) + info->fix.smem_len);
	start &= PAGE_MASK;

	if ((vma->vm_end - vma->vm_start + off) > len)
		return -EINVAL;
	off += start;

	vma->vm_pgoff = off >> PAGE_SHIFT;
	vma->vm_flags |= VM_IO;

	pgprot_val(vma->vm_page_prot) &= ~_CACHE_MASK;
	/* Write-Acceleration */
#ifdef CONFIG_FB_USING_CACHABLE
	pgprot_val(vma->vm_page_prot) |= _CACHE_CACHABLE_NONCOHERENT;
#else
	pgprot_val(vma->vm_page_prot) |= _CACHE_CACHABLE_WA;
#endif
	if (io_remap_pfn_range(vma, vma->vm_start, off >> PAGE_SHIFT,
				vma->vm_end - vma->vm_start, vma->vm_page_prot))
	{
		return -EAGAIN;
	}

	return 0;
}

static void ingenicfb_set_vsync_value(void *data)
{
	struct ingenicfb_device *fbdev = (struct ingenicfb_device *)data;

	fbdev->vsync_skip_map = (fbdev->vsync_skip_map >> 1 |
				fbdev->vsync_skip_map << 9) & 0x3ff;
	if(likely(fbdev->vsync_skip_map & 0x1)) {
		fbdev->timestamp.value[fbdev->timestamp.wp] =
			ktime_to_ns(ktime_get());
		fbdev->timestamp.wp = (fbdev->timestamp.wp + 1) % TIMESTAMP_CAP;
		wake_up_interruptible(&fbdev->vsync_wq);
#ifndef CONFIG_FB_VSYNC_SKIP_DISABLE
	} else {
		fbdev->timestamp.wp = fbdev->timestamp.rp + 1;
		wake_up_interruptible(&fbdev->vsync_wq);
#endif
	}
}

static inline uint32_t convert_color_to_hw(unsigned val, struct fb_bitfield *bf)
{
	return (((val << bf->length) + 0x7FFF - val) >> 16) << bf->offset;
}

static int ingenicfb_setcolreg(unsigned regno, unsigned red, unsigned green,
		unsigned blue, unsigned transp, struct fb_info *fb)
{
	if (regno >= 16)
		return -EINVAL;

	((uint32_t *)(fb->pseudo_palette))[regno] =
		convert_color_to_hw(red, &fb->var.red) |
		convert_color_to_hw(green, &fb->var.green) |
		convert_color_to_hw(blue, &fb->var.blue) |
		convert_color_to_hw(transp, &fb->var.transp);

	return 0;
}

static int calc_refresh_ratio(struct lcd_panel *panel)
{
	struct smart_config *smart_config = panel->smart_config;

	switch(smart_config->smart_type){
	case SMART_LCD_TYPE_8080:
	case SMART_LCD_TYPE_6800:
		break;
	case SMART_LCD_TYPE_SPI_3:
		return 9;
	case SMART_LCD_TYPE_SPI_4:
		return 8;
	default:
		printk("%s %d err!\n",__func__,__LINE__);
		break;
	}

	switch(smart_config->pix_fmt) {
	case SMART_LCD_FORMAT_888:
		if(smart_config->dwidth == SMART_LCD_DWIDTH_8_BIT)
			return 4;
		if(smart_config->dwidth == SMART_LCD_DWIDTH_24_BIT)
			return 2;
        break;
	case SMART_LCD_FORMAT_565:
		if(smart_config->dwidth == SMART_LCD_DWIDTH_8_BIT)
			return 3;
		if(smart_config->dwidth == SMART_LCD_DWIDTH_16_BIT)
			return 2;
        break;
	default:
		printk("%s %d err!\n",__func__,__LINE__);
		break;
	}

	return 1;

}

static int refresh_pixclock_auto_adapt(struct fb_info *info, struct fb_videomode *mode)
{
	struct ingenicfb_device *fbdev = info->par;
	struct lcd_panel *panel = fbdev->panel;
    //	struct fb_var_screeninfo *var = &info->var;
	uint16_t hds, vds;
	uint16_t hde, vde;
	uint16_t ht, vt;
	unsigned long rate;
	unsigned int refresh_ratio = 1;

	if (mode == NULL) {
		dev_err(fbdev->dev, "%s get video mode failed\n", __func__);
		return -EINVAL;
	}

	if (fbdev->panel->lcd_type == LCD_TYPE_SLCD) {
		refresh_ratio = calc_refresh_ratio(panel);
	}

	hds = mode->hsync_len + mode->left_margin;
	hde = hds + mode->xres;
	ht = hde + mode->right_margin;

	vds = mode->vsync_len + mode->upper_margin;
	vde = vds + mode->yres;
	vt = vde + mode->lower_margin;

	if(mode->refresh){
		rate = mode->refresh * vt * ht * refresh_ratio;

		mode->pixclock = KHZ2PICOS(rate / 1000 + 1);	//KHz
	}else if(mode->pixclock){
		rate = PICOS2KHZ(mode->pixclock) * 1000;	//Hz
		mode->refresh = rate / vt / ht / refresh_ratio;
	}else{
		dev_err(fbdev->dev,"%s error:lcd important config info is absenced\n",__func__);
		return -EINVAL;
	}

	dev_info(fbdev->dev, "mode->refresh: %d, mode->pixclock: %d, rate: %ld, modex->pixclock: %d\n", mode->refresh, mode->pixclock, rate, mode->pixclock);

	return 0;
}

static int ingenicfb_update_vidmem(struct ingenicfb_device *fbdev, unsigned int x_res, unsigned int y_res, unsigned int bits_per_pixel)
{
	unsigned int alloc_size = 0;
    	unsigned int linesize;
	int i = 0;

	fbdev->vidmem_size = 0;

    	linesize = (x_res * bits_per_pixel) >> 3;
    	linesize = rotator_aligned_size(linesize);

    	alloc_size = linesize * y_res;
    	fbdev->frm_size = alloc_size;

    	alloc_size = alloc_size * CONFIG_FB_INGENIC_NR_FRAMES;

	if(IS_ERR_OR_NULL(fbdev->vidmem[0])) {
		return -ENOMEM;
	}
	fbdev->vidmem_size = alloc_size;

	for(i = 1; i < CONFIG_FB_INGENIC_NR_FRAMES; i++) {
		fbdev->vidmem[i] = fbdev->vidmem[0] + i * fbdev->frm_size;
		fbdev->vidmem_phys[i] = fbdev->vidmem_phys[0] + i * fbdev->frm_size;
	}


	return 0;
}


static int ingenicfb_set_par(struct fb_info *info)
{
	int ret = 0,i;
	struct ingenicfb_device *fbdev = info->par;
	struct fb_var_screeninfo *var = &info->var;
	struct dpu_ctrl *dctrl = &fbdev->dctrl;
	struct lcd_panel *panel = fbdev->panel;
	struct fb_videomode *mode;
	struct ingenicfb_colormode *color_mode;
	struct rdma_setup_info *rdma_info = &fbdev->rdma_info;
    	int linesize = 0;
	/*根据var xres,yres信息，查找panel支持的mode*/
	mode = ingenicfb_get_mode(var, info);
	if (mode == NULL) {
		dev_err(info->dev, "%s get video mode failed\n", __func__);
		return -EINVAL;
	}
	info->mode = mode;
	fbdev->active_video_mode = mode;
	dctrl->active_video_mode = fbdev->active_video_mode;
	/*根据var信息, 查找到colormode.*/
	color_mode = ingenicfb_check_colormode(var);
	if(color_mode == NULL) {
		dev_err(info->dev,"%s Check colormode failed!\n",__func__);
		return  -EINVAL;
	}
	ingenicfb_videomode_to_var(var, fbdev->active_video_mode);
	/*修改当前指向的color_mode? 那check_var需要修改吗?*/
	fbdev->color_mode = color_mode;

	ret = ingenicfb_update_vidmem(fbdev, info->var.xres, info->var.yres, info->var.bits_per_pixel);
	if (ret) {
		dev_err(info->dev, "Failed to update video memory\n");
	}

    	linesize = (info->var.bits_per_pixel * info->var.xres) >> 3;
    	linesize = rotator_aligned_size(linesize);
	info->fix = ingenicfb_fix;
	info->fix.line_length = linesize;
	info->fix.smem_start = fbdev->vidmem_phys[0];
	info->fix.smem_len = fbdev->vidmem_size;
	info->screen_size = info->fix.line_length * info->var.yres;
	info->screen_base = fbdev->vidmem[0];
	info->pseudo_palette = fbdev->pseudo_palette;

	/*构建rdma信息，设置并启动.*/
	rdma_info->nframes = CONFIG_FB_INGENIC_NR_FRAMES;	//TODO:
	rdma_info->format = color_mode->mode;
	rdma_info->color = color_mode->color;
	rdma_info->stride = var->xres;
	rdma_info->continuous = 1; //TODO:
	for(i = 0; i < rdma_info->nframes; i++) {
		rdma_info->vidmem[i] = (unsigned char *)fbdev->vidmem[i]; //(unsigned char**)&fbdev->vidmem;
		rdma_info->vidmem_phys[i] = fbdev->vidmem_phys[i]; //(unsigned int**)&fbdev->vidmem_phys;
	}

//	rdma_info->vidmem = (unsigned char**)&fbdev->vidmem;
//	rdma_info->vidmem_phys = (unsigned int**)&fbdev->vidmem_phys;
	if(panel->lcd_type == LCD_TYPE_LVDS_JEIDA || panel->lcd_type == LCD_TYPE_LVDS_VESA){
		ingenic_set_lvds_display_clk(dctrl);
	} else {
		ingenic_set_pixclk(dctrl, PICOS2KHZ(dctrl->active_video_mode->pixclock));
	}

	if(panel->lcd_type == LCD_TYPE_MIPI_SLCD || panel->lcd_type == LCD_TYPE_MIPI_TFT) {
		if(panel->cur_active_videomode < panel->num_modes)
			dsi_change_video_config(dctrl->dsi, panel);
	}

	if(&dctrl->rdma_info != rdma_info)
		memcpy(&dctrl->rdma_info, rdma_info, sizeof(struct rdma_setup_info));

	return 0;
}

static inline int timeval_sub_to_us(struct timespec64 lhs,
						struct timespec64 rhs)
{
	int sec, nsec;
	sec = lhs.tv_sec - rhs.tv_sec;
	nsec = lhs.tv_nsec - rhs.tv_nsec;

	return (sec*1000000 + nsec/1000);
}

static inline int time_us2ms(int us)
{
	return (us/1000);
}

static void calculate_frame_rate(void)
{
	static struct timespec64 time_now, time_last;
	unsigned int interval_in_us;
	unsigned int interval_in_ms;
	static unsigned int fpsCount = 0;

	switch(showFPS){
	case 1:
		fpsCount++;
		ktime_get_real_ts64(&time_now);
		interval_in_us = timeval_sub_to_us(time_now, time_last);
		if ( interval_in_us > (USEC_PER_SEC) ) { /* 1 second = 1000000 us. */
			printk(" Pan display FPS: %d\n",fpsCount);
			fpsCount = 0;
			time_last = time_now;
		}
		break;
	case 2:
		ktime_get_real_ts64(&time_now);
		interval_in_us = timeval_sub_to_us(time_now, time_last);
		interval_in_ms = time_us2ms(interval_in_us);
		printk(" Pan display interval ms: %d\n",interval_in_ms);
		time_last = time_now;
		break;
	default:
		if (showFPS > 3) {
			int d, f;
			fpsCount++;
			ktime_get_real_ts64(&time_now);
			interval_in_us = timeval_sub_to_us(time_now, time_last);
			if (interval_in_us > USEC_PER_SEC * showFPS ) { /* 1 second = 1000000 us. */
				d = fpsCount / showFPS;
				f = (fpsCount * 10) / showFPS - d * 10;
				printk(" Pan display FPS: %d.%01d\n", d, f);
				fpsCount = 0;
				time_last = time_now;
			}
		}
		break;
	}
}

static int ingenicfb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct ingenicfb_device *fbdev = info->par;
	struct dpu_ctrl *dctrl = &fbdev->dctrl;
	struct lcd_panel *panel = fbdev->panel;
	int next_frm;
 	if (var->xoffset - info->var.xoffset) {
		dev_err(info->dev, "No support for X panning for now\n");
		return -EINVAL;
	}
	fbdev->pan_display_count++;
	if(showFPS){
		calculate_frame_rate();
	}
	next_frm = var->yoffset / var->yres;
#ifdef CONFIG_FB_USING_CACHABLE
	dma_cache_wback_inv((unsigned long)fbdev->vidmem[next_frm], var->yres * var->xres);
#endif

	if(panel->ops && panel->ops->pan_display) {
        char *frm_addr;
        struct rect_frame rframe = {
            .left = 0,
            .right = info->var.xres,
            .top = 0,
            .bottom = info->var.yres,
        };
		frm_addr = CKSEG0ADDR(info->screen_base) + next_frm * info->screen_size;
        panel->ops->pan_display(panel, frm_addr, &rframe);
	}
	else
		dpu_ctrl_rdma_change(dctrl, next_frm);

 	return 0;
}

static int ingenicfb_do_resume(struct ingenicfb_device *fbdev)
{
	struct dpu_ctrl *dctrl = &fbdev->dctrl;
	int ret = 0;
	if(dctrl->blank) {
	ret = dpu_ctrl_resume(dctrl);
	dctrl->blank = 0;
	}
	return ret;
}

static int ingenicfb_do_suspend(struct ingenicfb_device *fbdev)
{
	struct dpu_ctrl *dctrl = &fbdev->dctrl;
	int ret = 0;
	if(!dctrl->blank) {
	ret = dpu_ctrl_suspend(dctrl);
	dctrl->blank = 1;
	}
	return ret;
}

static int ingenicfb_blank(int blank_mode, struct fb_info *info)
{
	struct ingenicfb_device *fbdev = info->par;
	int ret = 0;

	mutex_lock(&fbdev->lock);
	switch(blank_mode) {
		case FB_BLANK_UNBLANK:
		ret = ingenicfb_do_resume(fbdev);
			break;
		case FB_BLANK_NORMAL:
		case FB_BLANK_VSYNC_SUSPEND:
		case FB_BLANK_HSYNC_SUSPEND:
		case FB_BLANK_POWERDOWN:
		ret = ingenicfb_do_suspend(fbdev);
			break;
		default:
			ret = -EINVAL;

	}
	mutex_unlock(&fbdev->lock);


	return ret;
}

static int ingenicfb_open(struct fb_info *info, int user)
{
	struct ingenicfb_device *fbdev = info->par;
#if 0 //TODO:

	if (!fbdev->is_lcd_en && fbdev->vidmem_phys) {
		fbdev->timestamp.rp = 0;
		fbdev->timestamp.wp = 0;
		ingenicfb_set_fix_par(info);
		ret = ingenicfb_set_par(info);
		if(ret) {
			dev_err(info->dev, "Set par failed!\n");
			return ret;
		}
		memset(fbdev->vidmem[fbdev->current_frm_desc][0], 0, fbdev->frm_size);
		ingenicfb_enable(info);
	}
#endif
	dev_dbg(info->dev, "####open count : %d\n", ++fbdev->open_cnt);

	return 0;
}

static int ingenicfb_release(struct fb_info *info, int user)
{
	struct ingenicfb_device *fbdev = info->par;

	dev_dbg(info->dev, "####close count : %d\n", fbdev->open_cnt--);
	if(!fbdev->open_cnt) {
//		fbdev->timestamp.rp = 0;
//		fbdev->timestamp.wp = 0;
	}
	return 0;
}

ssize_t ingenicfb_write(struct fb_info *info, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct ingenicfb_device *fbdev = info->par;
	struct lcd_panel *panel = fbdev->panel;
	u8 *buffer, *src, *update;
	u8 __iomem *dst;
	int c, cnt = 0, err = 0;
	unsigned long total_size;
	unsigned long p = *ppos;
	int screen_base_offset = 0;
	int next_frm = 0;
	struct rect_frame rframe;
	next_frm = info->var.yoffset / info->var.yres;

	screen_base_offset = next_frm * info->screen_size;

	total_size = info->screen_size;

	if (total_size == 0)
		total_size = info->fix.smem_len;

	if (p > total_size)
		return -EFBIG;

	if (count > total_size) {
		err = -EFBIG;
		count = total_size;
	}

	if (count + p > total_size) {
		if (!err)
			err = -ENOSPC;

		count = total_size - p;
	}

	buffer = kmalloc((count > PAGE_SIZE) ? PAGE_SIZE : count,
			 GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	dst = (u8 __iomem *) (info->screen_base + p + screen_base_offset);

	while (count) {
		c = (count > PAGE_SIZE) ? PAGE_SIZE : count;
		src = buffer;

		if (copy_from_user(src, buf, c)) {
			err = -EFAULT;
			break;
		}

		fb_memcpy_tofb(dst, src, c);
		if(panel->ops && panel->ops->pan_display) {
			rframe.left = 0;
			rframe.right = info->var.xres;
			rframe.top = 0;
			rframe.bottom = info->var.yres;
			rframe.frame_size = c;
			update = (u8 *)(CKSEG0ADDR(info->screen_base) + screen_base_offset);
			panel->ops->pan_display(panel, update, &rframe);
		}
		dst += c;
		src += c;
		*ppos += c;
		buf += c;
		cnt += c;
		count -= c;
	}

	kfree(buffer);

	return (cnt) ? cnt : err;
}

static ssize_t
ingenicfb_read(struct fb_info *info, char __user *buf, size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	u8 *buffer, *dst;
	u8 __iomem *src;
	int c, cnt = 0, err = 0;
	unsigned long total_size;

	int screen_base_offset = 0;
	int next_frm = 0;

	next_frm = info->var.yoffset / info->var.yres;

	if (!info || ! info->screen_base)
		return -ENODEV;

	if (info->state != FBINFO_STATE_RUNNING)
		return -EPERM;

	total_size = info->screen_size;

	if (total_size == 0)
		total_size = info->fix.smem_len;

	if (p >= total_size)
		return 0;

	if (count >= total_size)
		count = total_size;

	if (count + p > total_size)
		count = total_size - p;

	buffer = kmalloc((count > PAGE_SIZE) ? PAGE_SIZE : count,
			 GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	src = (u8 __iomem *) (info->screen_base + p + screen_base_offset);

	while (count) {
		c  = (count > PAGE_SIZE) ? PAGE_SIZE : count;
		dst = buffer;
		fb_memcpy_fromfb(dst, src, c);
		dst += c;
		src += c;

		if (copy_to_user(buf, buffer, c)) {
			err = -EFAULT;
			break;
		}
		*ppos += c;
		buf += c;
		cnt += c;
		count -= c;
	}

	kfree(buffer);

	return (err) ? err : cnt;


}

static struct fb_ops ingenicfb_ops = {
	.owner 		= THIS_MODULE,
	.fb_open 	= ingenicfb_open,
	.fb_release 	= ingenicfb_release,
	.fb_write 	= ingenicfb_write,
	.fb_read 	= ingenicfb_read,
	.fb_check_var 	= ingenicfb_check_var,
	.fb_set_par 	= ingenicfb_set_par,
	.fb_setcolreg 	= ingenicfb_setcolreg,
	.fb_blank 	= ingenicfb_blank,
	.fb_pan_display = ingenicfb_pan_display,
	.fb_fillrect 	= cfb_fillrect,
	.fb_copyarea 	= cfb_copyarea,
	.fb_imageblit 	= cfb_imageblit,
	.fb_ioctl 	= ingenicfb_ioctl,
	.fb_mmap 	= ingenicfb_mmap,
};


static int vsync_skip_set(struct ingenicfb_device *fbdev, int vsync_skip)
{
#if 0
	unsigned int map_wide10 = 0;
	int rate, i, p, n;
	int fake_float_1k;

	if (vsync_skip < 0 || vsync_skip > 9)
		return -EINVAL;

	rate = vsync_skip + 1;
	fake_float_1k = 10000 / rate;   /* 10.0 / rate */

	p = 1;
	n = (fake_float_1k * p + 500) / 1000;   /* +0.5 to int */

	for (i = 1; i <= 10; i++) {
		map_wide10 = map_wide10 << 1;
		if (i == n) {
			map_wide10++;
			p++;
			n = (fake_float_1k * p + 500) / 1000;
		}
	}
	mutex_lock(&fbdev->lock);
	fbdev->vsync_skip_map = map_wide10;
	fbdev->vsync_skip_ratio = rate - 1;     /* 0 ~ 9 */
	mutex_unlock(&fbdev->lock);

	printk("vsync_skip_ratio = %d\n", fbdev->vsync_skip_ratio);
	printk("vsync_skip_map = 0x%08x\n", fbdev->vsync_skip_map);
#endif
	return 0;
}

__attribute__((__unused__)) static int ingenicfb_copy_logo(struct fb_info *info)
{

#if 0
	unsigned long src_addr = 0;	/* u-boot logo buffer address */
	unsigned long dst_addr = 0;	/* kernel frame buffer address */
	struct ingenicfb_device *fbdev = info->par;
	unsigned long size;
	unsigned int ctrl;
	unsigned read_times;
	lay_cfg_en_t lay_cfg_en;

	/* Sure the uboot SLCD using the continuous mode, Close irq */
	if (!(reg_read(fbdev, DC_ST) & DC_WORKING)) {
		dev_err(fbdev->dev, "uboot is not display logo!\n");
#ifdef CONFIG_TRUE_COLOR_LOGO
        fbdev->current_frm_desc = 0;
        ingenicfb_set_fix_par(fbdev->fb);
        ingenicfb_set_par(fbdev->fb);
        ingenicfb_enable(fbdev->fb);
        show_logo(fbdev->fb);
        fb_blank(fbdev->fb, FB_BLANK_UNBLANK);
#endif
		return -ENOEXEC;
	}

	/*fbdev->is_lcd_en = 1;*/

	/* Reading Desc from regisger need reset */
	ctrl = reg_read(fbdev, DC_CTRL);
	ctrl |= DC_DES_CNT_RST;
	reg_write(fbdev, DC_CTRL, ctrl);

	/* For geting LayCfgEn need read  DC_FRM_DES 10 times */
	read_times = 10 - 1;
	while(read_times--) {
		reg_read(fbdev, DC_FRM_DES);
	}
	lay_cfg_en.d32 = reg_read(fbdev, DC_FRM_DES);
	if(!lay_cfg_en.b.lay0_en) {
		dev_err(fbdev->dev, "Uboot initialization is not using layer0!\n");
		return -ENOEXEC;
	}

	/* For geting LayerBufferAddr need read  DC_LAY0_DES 3 times */
	read_times = 3 - 1;
	/* get buffer physical address */
	while(read_times--) {
		reg_read(fbdev, DC_LAY0_DES);
	}
	src_addr = (unsigned long)reg_read(fbdev, DC_LAY0_DES);

	if (src_addr) {
		size = info->fix.line_length * info->var.yres;
		src_addr = (unsigned long)phys_to_virt(src_addr);
		dst_addr = (unsigned long)fbdev->vidmem[0][0];
		memcpy((void *)dst_addr, (void *)src_addr, size);
	}
#endif
	return 0;
}

static int ingenicfb_alloc_vidmem(struct ingenicfb_device *fbdev, unsigned int x_res, unsigned int y_res, unsigned int bits_per_pixel)
{
	unsigned int alloc_size = 0;
	unsigned int xaligned_alloc_size = 0, yaligned_alloc_size = 0;
    	unsigned int x_size = 0, y_size = 0;
	int i = 0;

	fbdev->vidmem_size = 0;
    	x_size = (x_res * bits_per_pixel) >> 3;
    	x_size = rotator_aligned_size(x_size);
	xaligned_alloc_size = x_size * y_res;

	y_size = (y_res * bits_per_pixel) >> 3;
	y_size = rotator_aligned_size(y_size);
	yaligned_alloc_size = y_size * x_res;

    	alloc_size = MAX(xaligned_alloc_size, yaligned_alloc_size);
	alloc_size = (alloc_size + y_res - 1) / y_res * y_res;
	fbdev->frm_size = alloc_size;

    	alloc_size = alloc_size * CONFIG_FB_INGENIC_NR_FRAMES;
	fbdev->vidmem[0] = dma_alloc_coherent(fbdev->dev, alloc_size, &fbdev->vidmem_phys[0], GFP_KERNEL);

	if(IS_ERR_OR_NULL(fbdev->vidmem[0])) {
		return -ENOMEM;
	}
	fbdev->vidmem_size = alloc_size;
    	printk("fbdev->frm_size = %d\n",fbdev->frm_size);
    	dev_info(fbdev->dev,"frame%d mem: 0x%08x\n",0,fbdev->vidmem_phys[0]);
	for(i = 1; i < CONFIG_FB_INGENIC_NR_FRAMES; i++) {
		fbdev->vidmem[i] = fbdev->vidmem[0] + i * fbdev->frm_size;
		fbdev->vidmem_phys[i] = fbdev->vidmem_phys[0] + i * fbdev->frm_size;
        	dev_info(fbdev->dev,"frame%d mem: 0x%08x\n",i,fbdev->vidmem_phys[i]);
	}

	dev_info(fbdev->dev, "vidmem @ 0x%08lx size %d\n", (unsigned long)fbdev->vidmem[0], fbdev->vidmem_size);

	return 0;
}

int ingenicfb_release_vidmem(struct ingenicfb_device *fbdev)
{
	if(fbdev->vidmem_size) {
		dma_free_coherent(fbdev->dev, fbdev->vidmem_size, fbdev->vidmem[0], fbdev->vidmem_phys[0]);
		fbdev->vidmem_size = 0;
	}

	return 0;
}

int pan_init_logo(struct fb_info *fb)
{

#ifdef CONFIG_TRUE_COLOR_LOGO
	if(!copyed_logo_buf || !fb) {
		pr_err("logo not support multi layer!\n");
		return 0;
	}

	logo_info.p8 = copyed_logo_buf;
	show_logo(fb);
	if (IS_MODULE(CONFIG_FB_INGENIC)) {
		/* Module: free the vmalloc'd copy after use */
		vfree(copyed_logo_buf);
		copyed_logo_buf = NULL;
	}
#endif

	return 0;
}

#ifdef CONFIG_SOC_X2600
static int dy_rot_angle=-1;
static int __init fbdev_rotate_setup(char *arg)
{
    if (arg[0] == '0' && (arg[1] == 'X' || arg[1] == 'x')) {
        dy_rot_angle = simple_strtol(arg, NULL, 16);
        dy_rot_angle = readl((void*)KSEG1ADDR(dy_rot_angle));
        if ((dy_rot_angle & 0xffff0000) != 0xeeee0000)
            dy_rot_angle = -1;
        else
            dy_rot_angle = dy_rot_angle & 0x0000ffff;
    } else {
        dy_rot_angle = simple_strtol(arg, NULL, 10);
    }
    if (dy_rot_angle != -1 && dy_rot_angle != 0 && dy_rot_angle != 90 &&
            dy_rot_angle != 180 && dy_rot_angle != 270 ) {
        printk("fbdev: 'rotate' parameter out of range[0, 90, 180,270], ignoring.\n");
        dy_rot_angle = -1;
    }
	return 0;
}
early_param("rot_angle", fbdev_rotate_setup); //get nv.rot_angle from kernel cmdargs.
#endif

static int ingenic_rdma_alloc_setup(struct ingenicfb_device *fbdev, unsigned int x_res, unsigned int y_res, unsigned int bits_per_pixel)
{
	int ret =0, i= 0;
	struct dpu_ctrl *dctrl = &fbdev->dctrl;
	struct rdma_setup_info *rdma_info = &fbdev->rdma_info;

	if(fbdev->disable_rdma_fb) {
		ret = ingenicfb_alloc_vidmem(fbdev, x_res, y_res, bits_per_pixel);
		if (ret) {
			dev_err(fbdev->dev, "Failed to allocate video memory\n");
			return ret;
		}
	}
	rdma_info->nframes = CONFIG_FB_INGENIC_NR_FRAMES;	//TODO:
	rdma_info->format = fbdev->color_mode->mode;
	rdma_info->color = fbdev->color_mode->color;
	rdma_info->stride = x_res;
	rdma_info->continuous = 1; //TODO:
	for(i = 0; i < rdma_info->nframes; i++) {
		rdma_info->vidmem[i] = (unsigned char *)fbdev->vidmem[i]; //(unsigned char**)&fbdev->vidmem;
		rdma_info->vidmem_phys[i] = fbdev->vidmem_phys[i]; //(unsigned int**)&fbdev->vidmem_phys;
	}

	dpu_ctrl_rdma_setup(dctrl, rdma_info);
	return ret;
}

static int ingenicfb_do_probe(struct platform_device *pdev, struct lcd_panel *panel)
{
	struct fb_info *fb;
	struct dpu_ctrl *dctrl;
	int ret = 0;
	int i = 0;
	const struct of_device_id *match;
	unsigned int reg;
    //	struct device_node *dsi_node = NULL;
	fb = framebuffer_alloc(sizeof(struct ingenicfb_device), &pdev->dev);
	if (!fb) {
		dev_err(&pdev->dev, "Failed to allocate framebuffer device\n");
		return -ENOMEM;
	}
	of_reserved_mem_device_init(&pdev->dev);

	fbdev = fb->par;
	fbdev->fb = fb;
	fbdev->dev = &pdev->dev;

	fbdev->dmn.dev = fbdev->dev;
	fbdev->dmn.data = fbdev;
	fbdev->dmn.ops = &ingenicfb_dmmu_mm_ops;
	fbdev->swap_dims = 0;

	dctrl = &fbdev->dctrl;
	of_property_read_u32(fbdev->dev->of_node, "ingenic,disable-rdma-fb", &fbdev->disable_rdma_fb);
	if(!fbdev->disable_rdma_fb) {
		dctrl->work_in_rdma = true;
	}
	ret = of_property_read_u32(fbdev->dev->of_node, "ingenic,compfb_wback_out", &dctrl->compfb_wback_out);
	if(ret) {
		dctrl->compfb_wback_out = 0;
	}
#ifdef CONFIG_ROT_INGENIC
    if (fbdev->disable_rdma_fb == 0) {
		dev_err(fbdev->dev,
			"CONFIG_ROT_INGENIC requires ingenic,disable-rdma-fb=<1>, got <%d>\n",
			fbdev->disable_rdma_fb);
	}
#endif
#ifdef CONFIG_SOC_X2600
	if(of_property_read_u32(fbdev->dev->of_node, "ingenic,rot_angle", &fbdev->rot_angle)){
		dev_err(fbdev->dev, "read rot_angle failed! please check dts.");
	}

	dev_info(fbdev->dev,"dts rot_angle = %d, nv.rot_angle = %d\n", fbdev->rot_angle, dy_rot_angle);
	if(dy_rot_angle != -1 && fbdev->rot_angle != dy_rot_angle) {
		int angle_diff = (dy_rot_angle - fbdev->rot_angle + 360) % 360;
		if (angle_diff == 90 || angle_diff == 270) {
			fbdev->swap_dims = 1;
		} else {
			fbdev->swap_dims = 0;
		}
		fbdev->rot_angle = dy_rot_angle;
	}
#endif
	if(panel->dsi_pdata){
		if(!of_property_read_u32(fbdev->dev->of_node, "dsi-host-reg", &reg)){
			panel->dsi_pdata->dsi_iobase = reg;
		}
		if(!of_property_read_u32(fbdev->dev->of_node, "dsi-phy-reg", &reg)){
			panel->dsi_pdata->dsi_phy_iobase = reg;
		}
		if(of_property_read_bool(fbdev->dev->of_node, "lane0_pn_swap")){
			panel->dsi_pdata->video_config.lane0_pn_swap = 1;
		}
		if(of_property_read_bool(fbdev->dev->of_node, "lane1_pn_swap")){
			panel->dsi_pdata->video_config.lane1_pn_swap = 1;
		}
		if(of_property_read_bool(fbdev->dev->of_node, "lane2_pn_swap")){
			panel->dsi_pdata->video_config.lane2_pn_swap = 1;
		}
		if(of_property_read_bool(fbdev->dev->of_node, "lane3_pn_swap")){
			panel->dsi_pdata->video_config.lane3_pn_swap = 1;
		}
		if(of_property_read_bool(fbdev->dev->of_node, "clk_lane_pn_swap")){
			panel->dsi_pdata->video_config.clk_lane_pn_swap = 1;
		}

	}
	if(of_property_read_bool(fbdev->dev->of_node, "composer-support")){
		dctrl->support_comp = true;
	} else {
		dctrl->support_comp = false;
	}

	match = of_match_node(ingenicfb_of_match, pdev->dev.of_node);
	if (!match)
		        return -ENODEV;
	fbdev->panel = panel;

	for(i = 0; i < panel->num_modes; i++) {
		ret = refresh_pixclock_auto_adapt(fb, &panel->modes[i]);
		if(ret) {
		        goto err_calc_pixclk;
		}
		fbdev->video_mode[i] = &panel->modes[i];
		fbdev->video_modes_num ++;
	}
	/*一个屏幕可能由多个modes, 将所有的modes转换成modelist.*/
	fb_videomode_to_modelist(panel->modes, panel->num_modes, &fb->modelist);

	/*同一时刻应该只有一个video_mode, 所以要找到active_video_mode.*/
	fbdev->active_video_mode = &panel->modes[0];

	dctrl->dev = &pdev->dev;
	dctrl->pdev = pdev;
	dctrl->active_video_mode = fbdev->active_video_mode;

	dctrl->set_vsync_value = ingenicfb_set_vsync_value;
	dctrl->vsync_data = fbdev;


	ret = dpu_ctrl_init(dctrl, panel);
	if(ret) {
		goto err_dctrl_init;
	}
	panel_mipi_init_ok = true;

	if(dctrl->support_comp) {
		// composer master
		fbdev->comp_master = hw_composer_init(dctrl);
		if(IS_ERR_OR_NULL(fbdev->comp_master)) {
			goto err_comp_init;
		}

		// one composer instance for fbdev.
		// using by ioctl procedure.
		fbdev->comp_ctx = hw_composer_create(fbdev);
		if(IS_ERR_OR_NULL(fbdev->comp_ctx)) {
			goto err_comp_ctx;
		}
	}

	/*width只是物理尺寸，以mm为单位。可以用于计算PPI*/
	fb->fbops = &ingenicfb_ops;
	fb->flags = FBINFO_DEFAULT;
	fb->var.width = panel->width;
	fb->var.height = panel->height;

	/*默认使用第一个colormode. */
	fbdev->color_mode = &ingenicfb_colormodes[0];
	ingenicfb_videomode_to_var(&fb->var, fbdev->active_video_mode);
	ingenicfb_colormode_to_var(&fb->var, fbdev->color_mode);

	ret = ingenicfb_check_var(&fb->var, fb);
	if (ret) {
		goto err_check_var;
	}


	vsync_skip_set(fbdev, CONFIG_FB_VSYNC_SKIP);
	init_waitqueue_head(&fbdev->vsync_wq);


    fbdev->open_cnt = 0;
	fbdev->is_lcd_en = 0;
	fbdev->timestamp.rp = 0;
	fbdev->timestamp.wp = 0;

    if(IS_LCD_CONTROLLER(panel)) {
#ifdef CONFIG_ROT_INGENIC
	dctrl->rot_init = 0;
	/*由于rotate在prot1 使用旋转时将port1优先级设置为最高*/
	writel(X2600_DDR_CCHC0_VALUE, (void __iomem *)X2600_DDR_CCHC0);
	writel(X2600_DDR_CCHC1_VALUE, (void __iomem *)X2600_DDR_CCHC1);

	if(!fbdev->disable_rdma_fb) {
		fbdev->rot_angle = 0;
	}
	if(fbdev->rot_angle != 0){
		if(!dctrl->compfb_wback_out) {
			ingenic_rdma_alloc_setup(fbdev, fb->var.xres, fb->var.yres, fb->var.bits_per_pixel);
			rot_ctrl_init(dctrl, fbdev->rot_angle, ROT_MODE_WDMA_AUTO);
		}
	}
#endif
    }

	if(!fbdev->disable_rdma_fb) {
		ret = ingenicfb_alloc_vidmem(fbdev, fb->var.xres, fb->var.yres, fb->var.bits_per_pixel);
		if (ret) {
			dev_err(&pdev->dev, "Failed to allocate video memory\n");
			goto err_alloc_vidmem;
		}
		fb->fix = ingenicfb_fix;
		fb->fix.line_length = fbdev->frm_size / fb->var.yres;
		fb->fix.smem_start = fbdev->vidmem_phys[0];
		fb->fix.smem_len = fbdev->vidmem_size;
		fb->screen_size = fb->fix.line_length * fb->var.yres;
		fb->screen_base = fbdev->vidmem[0];
		fb->pseudo_palette = fbdev->pseudo_palette;

		ret = register_framebuffer(fb);
		if (ret) {
			dev_err(&pdev->dev, "Failed to register framebuffer: %d\n",
					ret);
			goto err_register_framebuffer;
		}
	}

	/*1. Common setup. */
	dctrl->chan = DATA_CH_RDMA;
	dpu_ctrl_setup(dctrl);

	if(!fbdev->disable_rdma_fb) {
		ingenic_rdma_alloc_setup(fbdev, fb->var.xres, fb->var.yres, fb->var.bits_per_pixel);
        	if(IS_LCD_CONTROLLER(panel)) {
#ifdef CONFIG_ROT_INGENIC
        		if(fb->var.bits_per_pixel != 32)
                		dev_err(&pdev->dev, "Rotate bits_per_pixel: %d failed,should be 32\n",fb->var.bits_per_pixel);
        		else
                		rot_ctrl_init(dctrl, 0, ROT_MODE_RDMA_MANUAL);
#endif
            		dpu_ctrl_rdma_start(dctrl);
            		pan_init_logo(fbdev->fb);
        	}
		dctrl->work_in_rdma = true;

	} else {

		/* composer writeback fb mode.
		 * support dynamic rotate.
		 * using rdma & rotater
		 * 0 -> rdma.
		 * 90 ~ 180 ~ 270 rotater.
		 * */
		if(dctrl->compfb_wback_out) {
			ingenic_rdma_alloc_setup(fbdev, fb->var.xres, fb->var.yres, fb->var.bits_per_pixel);
#ifdef CONFIG_ROT_INGENIC
        		if(fb->var.bits_per_pixel != 32)
            			dev_err(&pdev->dev, "Rotate bits_per_pixel: %d failed,should be 32\n",fb->var.bits_per_pixel);
        		else
            			rot_ctrl_init(dctrl, fbdev->rot_angle, ROT_MODE_RDMA_MANUAL);
#endif
		}
		dctrl->work_in_rdma = false;
	}

	if(dctrl->support_comp) {
		/*2. compfb init and export.*/
		fbdev->compfb = hw_compfb_init(fbdev);
		if(IS_ERR_OR_NULL(fbdev->compfb)) {
			dev_err(&pdev->dev, "Failed to init compfb!\n");
			goto err_compfb_init;
		}

		/*3. export composer.*/
		fbdev->compfb->active_video_mode = fbdev->active_video_mode;
		fbdev->compfb->dev = &pdev->dev;
		fbdev->compfb->panel = panel;
	}
#ifdef CONFIG_HW_COMPOSER_V4L2_M2M
	{
		fbdev->comp_v4l2 = hw_comp_v4l2_init(&pdev->dev);
	}
#endif

    platform_set_drvdata(pdev, fbdev);
	ret = dpu_sysfs_init(&fbdev->sysfs, dctrl, fbdev->compfb);
	if(ret < 0) {
		dev_err(fbdev->dev, "failed to init sysfs!\n");
	}

	return 0;

err_compfb_init:
err_register_framebuffer:
err_alloc_vidmem:
err_check_var:
err_comp_ctx:
	if(dctrl->support_comp)
		hw_composer_exit(fbdev->comp_master);
err_comp_init:
	dpu_ctrl_exit(dctrl);
err_dctrl_init:
	fb_delete_videomode(panel->modes, &fb->modelist);
	fb_destroy_modelist(&fb->modelist);
err_calc_pixclk:
	/* 候选 panel 初始化失败后会继续尝试，释放本次 reserved-memory 绑定。 */
	of_reserved_mem_device_release(&pdev->dev);
	framebuffer_release(fb);
	return ret;
}

static const char *ingenicfb_panel_name(struct lcd_panel *panel)
{
	if (!panel || !panel->modes)
		return "unknown";

	return panel->modes[0].name ? panel->modes[0].name : "unknown";
}

static int ingenicfb_probe_panel_candidates(struct platform_device *pdev,
					    struct lcd_panel **panels,
					    int panel_num)
{
	int ret = -ENODEV;
	int i;

	/*
	 * panels[] 已经按 DTS endpoint reg 排好序。这里按顺序调用
	 * ingenicfb_do_probe()，由 MIPI DSI 初始化流程触发 panel 的
	 * probe_dev_id() 回调读 ID。ID 匹配的 panel 会完成初始化并返回 0。
	 */
	for (i = 0; i < panel_num; i++) {
		/* 多个 graph endpoint 表示同一输出口的候选屏，按 DTS 顺序读 ID。 */
		dev_info(&pdev->dev, "try graph panel candidate %d: %s\n",
			 i, ingenicfb_panel_name(panels[i]));

		ret = ingenicfb_do_probe(pdev, panels[i]);
		if (!ret)
			return 0;

		/*
		 * 当前候选读 ID 或初始化失败时，panel 驱动需要释放本次申请的
		 * GPIO，fbdev 侧也要清掉本次半初始化状态，避免下一个候选误用
		 * 上一个候选留下的 fbdev 指针和 MIPI 初始化标记。
		 */
		fbdev = NULL;
		panel_mipi_init_ok = false;
		dev_info(&pdev->dev, "graph panel candidate %s failed: %d\n",
			 ingenicfb_panel_name(panels[i]), ret);
	}

	return ret;
}

int ingenicfb_register_panel(struct lcd_panel *panel)
{
	int ret;

	if (panel_mipi_init_ok)
		return -EBUSY;

	fbdev_panel = panel;

	if(fbdev_pdev == NULL)
		return 0;

	if (fbdev_pdev->dev.driver != NULL) {
		ret = ingenicfb_do_probe(fbdev_pdev, fbdev_panel);
		if (ret < 0)
			fbdev_panel = NULL;
		return ret;
	}

	ret = device_attach(&fbdev_pdev->dev);
	if (ret < 0) {
		fbdev_panel = NULL;
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ingenicfb_register_panel);

static int ingenicfb_probe(struct platform_device *pdev)
{
	struct lcd_panel *panels[INGENIC_LCD_PANEL_MAX_CANDIDATES];
	int panel_num;

	if(!fbdev_pdev)
		fbdev_pdev = pdev;
	else
		WARN_ON(fbdev_pdev != pdev);

	panel_num = ingenic_lcd_panel_find_candidates_by_graph(pdev->dev.of_node,
							       0, panels,
							       ARRAY_SIZE(panels));
	if (panel_num > 0)
		return ingenicfb_probe_panel_candidates(pdev, panels, panel_num);

	if (panel_num == -EPROBE_DEFER) {
		if(fbdev_panel != NULL) {
			/*
			 * 兼容旧 panel：DTS 已有 graph，但旧 panel 驱动只调用
			 * ingenicfb_register_panel() 时，不能一直等待 registry。
			 */
			return ingenicfb_do_probe(fbdev_pdev, fbdev_panel);
		}

		dev_dbg(&pdev->dev, "graph panel candidates are not ready, defer probe\n");
		return -EPROBE_DEFER;
	} else if (panel_num < 0 && panel_num != -EINVAL) {
		dev_dbg(&pdev->dev, "graph panel candidate lookup failed: %d\n",
			panel_num);
	}

	/* 没有 graph 候选列表时，只保留旧全局注册方式作为兼容路径。 */
	if(fbdev_panel != NULL) {
		return ingenicfb_do_probe(fbdev_pdev, fbdev_panel);
	}

	return 0;
}

static int ingenicfb_remove(struct platform_device *pdev)
{
	struct ingenicfb_device *fbdev = platform_get_drvdata(pdev);

	// TODO:
	// release comp_ctx, compfb, comp_v4l2.
	hw_compfb_exit(fbdev->compfb);
#ifdef CONFIG_HW_COMPOSER_V4L2_M2M
	hw_comp_v4l2_exit(fbdev->comp_v4l2);
#endif
	hw_composer_destroy(fbdev->comp_ctx);
	hw_composer_exit(fbdev->comp_master);

	dpu_ctrl_exit(&fbdev->dctrl);

	platform_set_drvdata(pdev, NULL);

	dpu_sysfs_exit(&fbdev->sysfs);

	ingenicfb_release_vidmem(fbdev);
	framebuffer_release(fbdev->fb);

	return 0;
}

static void ingenicfb_shutdown(struct platform_device *pdev)
{
	struct ingenicfb_device *fbdev = platform_get_drvdata(pdev);

	if(!IS_ERR_OR_NULL(fbdev))
		fb_blank(fbdev->fb, FB_BLANK_POWERDOWN);
};

#ifdef CONFIG_PM

static int ingenicfb_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ingenicfb_device *fbdev = platform_get_drvdata(pdev);

	if(!IS_ERR_OR_NULL(fbdev))
		fb_blank(fbdev->fb, FB_BLANK_POWERDOWN);

	return 0;
}

static int ingenicfb_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct ingenicfb_device *fbdev = platform_get_drvdata(pdev);

	if(!IS_ERR_OR_NULL(fbdev))
		fb_blank(fbdev->fb, FB_BLANK_UNBLANK);

	return 0;
}

static const struct dev_pm_ops ingenicfb_pm_ops = {
	.suspend = ingenicfb_suspend,
	.resume = ingenicfb_resume,
};
#endif


static const struct of_device_id ingenicfb_of_match[] = {
	{ .compatible = "ingenic,dpu",},
};

static struct platform_driver ingenicfb_driver = {
	.probe = ingenicfb_probe,
	.remove = ingenicfb_remove,
	.shutdown = ingenicfb_shutdown,
	.driver = {
		.name = "ingenic-fb",
		.of_match_table = ingenicfb_of_match,
#ifdef CONFIG_PM
		.pm = &ingenicfb_pm_ops,
#endif

	},
};

#ifdef CONFIG_TRUE_COLOR_LOGO
static struct _logo_info *logo_early = NULL;
static int __init logo_info_early(char *p)
{
	unsigned int pinfo = 0;
	char *endp = NULL;

	if (!p || !*p)
		return 0;

	pinfo = simple_strtoul(p, &endp, 0);
	if (endp == p || *endp != '\0' || pinfo == 0) {
		printk(KERN_ERR "Invalid early logo parameter: %s\n", p);
		return 0;
	}

	if (!IS_ALIGNED(pinfo, sizeof(u32))) {
		printk(KERN_ERR "Early logo address is not word aligned: 0x%x\n", pinfo);
		return 0;
	}

	if (pinfo > (0xffffffffU - sizeof(struct _logo_info))) {
		printk(KERN_ERR "Early logo address overflow: 0x%x\n", pinfo);
		return 0;
	}

	logo_early = (struct _logo_info *)CKSEG0ADDR(pinfo);
	logo_early_buf = (unsigned char *)CKSEG0ADDR(pinfo + sizeof(struct _logo_info));
	return 0;
}
early_param("logo", logo_info_early);
#endif

static int __init ingenicfb_init(void)
{
#ifdef CONFIG_TRUE_COLOR_LOGO
	size_t size;
	unsigned char *logo_source = NULL;
	struct _logo_info logo_early_hdr;

	if (logo_early) {
		size_t pixels;
		unsigned int bytes_per_pixel;

		if (copy_from_kernel_nofault(&logo_early_hdr, logo_early, sizeof(logo_early_hdr))) {
			printk(KERN_ERR "Failed to read early logo header, fallback to built-in logo\n");
			logo_early = NULL;
			logo_early_buf = NULL;
		} else {
			bytes_per_pixel = logo_early_hdr.bpp / 8;
			if ((unsigned long)logo_early_hdr.p8 != INGENIC_LOGO_MAGIC ||
				!logo_early_hdr.width || !logo_early_hdr.height || !bytes_per_pixel ||
				(logo_early_hdr.bpp != 16 && logo_early_hdr.bpp != 32) ||
				check_mul_overflow((size_t)logo_early_hdr.width,
						   (size_t)logo_early_hdr.height, &pixels) ||
				check_mul_overflow(pixels, (size_t)bytes_per_pixel, &size)) {
				printk(KERN_ERR "Invalid early logo header, fallback to built-in logo\n");
				logo_early = NULL;
				logo_early_buf = NULL;
			} else {
				copyed_logo_buf = vmalloc(size);
				if (!copyed_logo_buf) {
					printk(KERN_ERR "Failed to allocate memory for early logo\n");
				} else if (copy_from_kernel_nofault(copyed_logo_buf, logo_early_buf, size)) {
					printk(KERN_ERR "Failed to read early logo payload, fallback to built-in logo\n");
					vfree(copyed_logo_buf);
					copyed_logo_buf = NULL;
					logo_early = NULL;
					logo_early_buf = NULL;
				} else {
					logo_info.width = logo_early_hdr.width;
					logo_info.height = logo_early_hdr.height;
					logo_info.bpp = logo_early_hdr.bpp;
					logo_info.background_color = logo_early_hdr.background_color;
					printk(KERN_INFO "Using early parameter logo, size=%zu\n", size);
				}
			}
		}
	}

	if (!copyed_logo_buf) {
		/* Use built-in logo from .init.data section */
		size = logo_info.width * logo_info.height * (logo_info.bpp / 8);
		logo_source = &logo_buf_initdata[0];

		if (IS_MODULE(CONFIG_FB_INGENIC)) {
			/* Module: __initdata already freed, must copy */
			copyed_logo_buf = vmalloc(size);
			if (copyed_logo_buf) {
				memcpy(copyed_logo_buf, logo_source, size);
				printk(KERN_INFO "Using built-in logo copy, size=%zu\n", size);
			} else {
				printk(KERN_ERR "Failed to allocate memory for logo\n");
			}
		} else {
			/* Built-in: __initdata still valid during initcall, point directly */
			copyed_logo_buf = &logo_buf_initdata[0];
			printk(KERN_INFO "Using built-in logo from .init.data, size=%zu\n", size);
		}
	}

#endif
	platform_driver_register(&ingenicfb_driver);
	return 0;
}

static void __exit ingenicfb_cleanup(void)
{
	platform_driver_unregister(&ingenicfb_driver);
}


#ifdef CONFIG_EARLY_INIT_RUN
rootfs_initcall(ingenicfb_init);
#else
module_init(ingenicfb_init);
#endif

module_exit(ingenicfb_cleanup);

MODULE_DESCRIPTION("JZ LCD Controller driver");
MODULE_AUTHOR("qipenzhen <aric.pzqi@ingenic.com>");
MODULE_LICENSE("GPL");
