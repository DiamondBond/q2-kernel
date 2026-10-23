/*
 * A V4L2 driver for OmniVision gc5603 cameras.
 *
 * Copyright 2006 One Laptop Per Child Association, Inc.  Written
 * by Jonathan Corbet with substantial inspiration from Mark
 * McClelland's ovcamchip code.
 *
 * Copyright 2006-7 Jonathan Corbet <corbet@lwn.net>
 *
 * This file may be distributed under the terms of the GNU General
 * Public License, version 2.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-clk.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-image-sizes.h>
#include <linux/of_gpio.h>
#include <linux/clk.h>

#include <isp-sensor.h>

#define GC5603_CHIP_ID_H	(0x56)
#define GC5603_CHIP_ID_L	(0x03)
#define GC5603_REG_CHIP_ID_HIGH         0x03f0
#define GC5603_REG_CHIP_ID_LOW          0x03f1

#define GC5603_REG_END		0xffff
#define GC5603_REG_DELAY	0x00
#define GC2093_PAGE_REG	    0xfd

static bool debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");


struct gc5603_win_size {
	unsigned int width;
	unsigned int height;
	unsigned int mbus_code;
	struct sensor_info sensor_info;
	enum v4l2_colorspace colorspace;
	void *regs;
};

struct gc5603_gpio {
	int pin;
	int active_level;
};

struct gc5603_info {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *gain;
	struct v4l2_ctrl *again;

	struct v4l2_clk *clk;
	/* struct clk *sclka; */

	struct v4l2_ctrl *exposure;

	struct media_pad pad;

	struct v4l2_subdev_format *format;	/*current fmt.*/
	struct gc5603_win_size *win;

	struct gc5603_gpio xshutdn;
	struct gc5603_gpio efsync;
	struct gc5603_gpio led;
};

/*
 * the part of driver maybe modify about different sensor and different board.
 */
struct again_lut {
	int index;
	unsigned int regb4;
	unsigned int regb3;
	unsigned int dpc;
	unsigned int blc;
	unsigned int gain;
};
 
static struct again_lut gc5603_again_lut[] = {
    {0, 0x00, 0x00,0x01,0x00, 0},             //  1.000000
    {1, 0x00, 0x10,0x01,0x0c, 13726},         //  1.156250
    {2, 0x00, 0x20,0x01,0x1b, 31176},         //   1.390625
    {3, 0x00, 0x30,0x01,0x2c, 44067},         //   1.593750
    {4, 0x00, 0x40,0x01,0x3f, 64793},         //   1.984375
    {5, 0x00, 0x50,0x02,0x16, 78620},         //   2.296875
    {6, 0x00, 0x60,0x02,0x35, 96179},         //   2.765625
    {7, 0x00, 0x70,0x03,0x16, 109137},        //    3.171875
    {8, 0x00, 0x80,0x04,0x02, 132535},        //   4.062500
    {9, 0x00, 0x90,0x04,0x31, 146065},        //    4.687500
    {10, 0x00, 0xa0,0x05,0x32, 163565},       //    5.640625
    {11, 0x00, 0xb0,0x06,0x35, 176745},       //    6.484375
    {12, 0x00, 0xc0,0x08,0x04, 195116},       //   7.875000
    {13, 0x00, 0x5a,0x09,0x19, 208558},       //    9.078125
    {14, 0x00, 0x83,0x0b,0x0f, 229100},       //   11.281250
    {15, 0x00, 0x93,0x0d,0x12, 242508},       //    13.000000
    {16, 0x00, 0x84,0x10,0x00, 262416},       //     16.046875
    {17, 0x00, 0x94,0x12,0x3a, 275706},       //      18.468750
    {18, 0x01, 0x2c,0x1a,0x02, 292248},       //     22.000000
    {19, 0x01, 0x3c,0x1b,0x20, 305567},       //      25.328125
    {20, 0x00, 0x8c,0x20,0x0f, 324958},       //     31.093750
    {21, 0x00, 0x9c,0x26,0x07, 338276},       //     35.796875
    {22, 0x02, 0x64,0x36,0x21, 358918},       //      44.531250
    {23, 0x02, 0x74,0x37,0x3a, 372262},       //      51.281250
    {24, 0x00, 0xc6,0x3d,0x02, 392095},       //     63.250000
    {25, 0x00, 0xdc,0x3f,0x3f, 415409},       //      80.937500
    {26, 0x02, 0x85,0x3f,0x3f, 421076},       //      85.937500
    {27, 0x02, 0x95,0x3f,0x3f, 440355},       //      105.375000
    {28, 0x00, 0xce,0x3f,0x3f, 444858},       //      110.515625
};


static inline struct gc5603_info *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct gc5603_info, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct gc5603_info, hdl)->sd;
}
struct regval_list {
	uint16_t reg_num;
	unsigned char value;
};

static struct regval_list gc5603_init_regs_1920_1080_30fps_mipi[] = {
	/* mclk=27mhz,mipi data rate=423mbps/lane, row_time=19us ,30fps*/
	{0x03fe,0xf0},
	{0x03fe,0x00},
	{0x03fe,0x10},
	{0x03fe,0x00},
	{0x0a38,0x02},
	{0x0a38,0x03},
	{0x0a20,0x07},
	{0x061b,0x09},
	{0x061c,0x50},
	{0x061d,0x05},
	{0x061e,0x70},
	{0x061f,0x03},
	{0x0a21,0x08},
	{0x0a28,0x01},
	{0x0a34,0x40},
	{0x0a35,0x11},
	{0x0a36,0x60},
	{0x0a37,0x03},
	{0x0314,0x50},
	{0x0315,0x32},
	{0x031c,0xce},
	{0x0219,0x47},
	{0x0342,0x04},
	{0x0343,0xb0},
	{0x0259,0x06},
	{0x025a,0x8a},
	{0x0340,0x06},
	{0x0341,0xd6},
	{0x0345,0x02},
	{0x0347,0x02},
	{0x0348,0x0b},
	{0x0349,0x98},
	{0x034a,0x06},
	{0x034b,0x8a},
	{0x0094,0x0b},
	{0x0095,0x90},
	{0x0096,0x06},
	{0x0097,0x82},
	{0x0099,0x04},
	{0x009b,0x04},
	{0x060c,0x01},
	{0x060e,0xd2},
	{0x060f,0x05},
	{0x070c,0x01},
	{0x070e,0xd2},
	{0x070f,0x05},
	{0x0709,0x40},
	{0x0719,0x40},
	{0x0909,0x07},
	{0x0902,0x04},
	{0x0904,0x0b},
	{0x0907,0x54},
	{0x0908,0x06},
	{0x0903,0x9d},
	{0x072a,0x1c},
	{0x072b,0x1c},
	{0x0724,0x2b},
	{0x0727,0x2b},
	{0x1466,0x18},
	{0x1467,0x15},
	{0x1468,0x15},
	{0x1469,0x70},
	{0x146a,0xe8},
	{0x0707,0x07},
	{0x0737,0x0f},
	{0x0704,0x01},
	{0x0706,0x02},
	{0x0716,0x02},
	{0x0708,0xc8},
	{0x0718,0xc8},
	{0x061a,0x02},
	{0x1430,0x80},
	{0x1407,0x10},
	{0x1408,0x16},
	{0x1409,0x03},
	{0x1438,0x01},
	{0x02ce,0x03},
	{0x0245,0xc9},
	{0x023a,0x08},
	{0x02cd,0x88},
	{0x0612,0x02},
	{0x0613,0xc7},
	{0x0243,0x03},
	{0x0089,0x03},
	{0x0002,0xab},
	{0x0040,0xa3},
	{0x0075,0x64},
	{0x0004,0x0f},
	{0x0053,0x0a},
	{0x0205,0x0c},
	{0x0052,0x02},
	{0x0076,0x01},
	{0x021a,0x10},
	{0x0049,0x0f},
	{0x004a,0x3c},
	{0x004b,0x00},
	{0x0430,0x25},
	{0x0431,0x25},
	{0x0432,0x25},
	{0x0433,0x25},
	{0x0434,0x59},
	{0x0435,0x59},
	{0x0436,0x59},
	{0x0437,0x59},
	{0x0181,0xf0},
	{0x0182,0x05},
	{0x0185,0x01},
	{0x0180,0x46},
	{0x0100,0x08},
	{0x010d,0x74},
	{0x010e,0x0e},
	{0x0113,0x02},
	{0x0114,0x03},
	{0x0115,0x10},
	{0x0125,0x15},
	{0x0124,0x02},
	{0x0122,0x05},
	{0x0123,0x20},
	{0x0126,0x08},
	{0x0121,0x16},
	{0x0129,0x05},
	{0x012a,0x10},
	{0x012b,0x07},
	{0x031f,0x01},
	{0x031f,0x00},
	{0x0317,0x1c},
	{0x0a67,0x80},
	{0x0a50,0x80},
	{0x0a53,0x00},
	{0x0a51,0x80},
	{0x0a54,0xa0},
	{0x0a63,0x20},
	{0x0a65,0x20},
	{0x0a68,0x21},
	{0x05be,0x00},
	{0x05a9,0x01},
	{0x0024,0x00},
	{0x0025,0x00},
	{0x0028,0x0b},
	{0x0029,0x98},
	{0x002a,0x06},
	{0x002b,0x8a},
	{0x0a82,0x00},
	{0x0a83,0xe0},
	{0x0a72,0x02},
	{0x0a73,0x60},
	{0x0a80,0x01},
	{0x0a70,0x03},
	{0x0a75,0x41},
	{0x0a5a,0x80},
	/*{GC5603_REG_DELAY, 0x14},*/
	{0x05be,0x01},
	{0x0a70,0x00},
	{0x0080,0x02},
	{0x0021,0x40},
	{0x0a67,0x00},
	{0x0100,0x09},
	{GC5603_REG_END, 0x00},	/* END MARKER */
};

/*
 * the part of driver was fixed.
 */

static struct regval_list gc5603_stream_on_mipi[] = {
	{GC5603_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list gc5603_stream_off_mipi[] = {
	{GC5603_REG_END, 0x00},	/* END MARKER */
};

static int gc5603_read(struct v4l2_subdev *sd, unsigned short reg,
		unsigned char *value)
{
	struct gc5603_info *info = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	uint8_t buf[2] = {(reg>>8)&0xff, reg&0xff};
	struct i2c_msg msg[2] = {
		[0] = {
			.addr	= client->addr,
			.flags	= 0,
			.len	= 2,
			.buf	= buf,
		},
		[1] = {
			.addr	= client->addr,
			.flags	= I2C_M_RD,
			.len	= 1,
			.buf	= value,
		}
	};
	int ret = 0;

	printk("i2c device addr : 0x%x~~~~~~~~~~~~~\n",client->addr);
	ret = i2c_transfer(client->adapter, msg, 2);
	if(ret > 0)
		ret = 0;

	return ret;
}

static int gc5603_write(struct v4l2_subdev *sd, unsigned short reg,
		unsigned char value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	uint8_t buf[3] = {(reg>>8)&0xff, reg&0xff, value};
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= 3,
		.buf	= buf,
	};
	int ret;
	unsigned int timeout  = 100;
	while(timeout--){
		ret = i2c_transfer(client->adapter, &msg, 1);
		if(ret == -EAGAIN){
			msleep(100);
			continue;
		}
		else
			break;
	}
	if (ret > 0)
		ret = 0;

	return ret;
}

static int gc5603_read_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	int ret;
	unsigned char val;
	while (vals->reg_num != GC5603_REG_END) {
		if (vals->reg_num == GC5603_REG_DELAY) {
			msleep(vals->value);
		} else {
			ret = gc5603_read(sd, vals->reg_num, &val);
			if (ret < 0)
				return ret;
		}
		vals++;
	}
	return 0;
}

/*
 * Write a list of register settings; ff/ff stops the process.
 */
static int gc5603_write_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	int ret;
	while (vals->reg_num != GC5603_REG_END) {
		if (vals->reg_num == GC5603_REG_DELAY) {
			msleep(vals->value);
		} else {
			ret = gc5603_write(sd, vals->reg_num, vals->value);
			if (ret < 0)
				return ret;
		}
		vals++;
	}
	return 0;
}


/*
 * Stuff that knows about the sensor.
 */

static int gc5603_lightup(struct v4l2_subdev *sd, u32 val)
{
	struct gc5603_info *info = to_state(sd);

	if(gpio_is_valid(info->led.pin)) {
        if(val) {
            gpio_direction_output(info->led.pin, info->led.active_level);
        } else {
            gpio_direction_output(info->led.pin, !info->led.active_level);
        }
    }
	return 0;
}

static int gc5603_power(struct v4l2_subdev *sd, u32 val)
{
	struct gc5603_info *info = to_state(sd);

	if(val) {
		gpio_direction_output(info->xshutdn.pin, !info->xshutdn.active_level);
		msleep(10);
		gpio_direction_output(info->xshutdn.pin, info->xshutdn.active_level);
		msleep(10);
	}
	return 0;
}

static int gc5603_init(struct v4l2_subdev *sd, u32 val)
{
	struct gc5603_info *info = to_state(sd);
	int ret = 0;

	ret = gc5603_write_array(sd, info->win->regs);

	return ret;
}



static int gc5603_detect(struct v4l2_subdev *sd, unsigned int *ident)
{
	unsigned char v;
	int ret;
	ret = gc5603_read(sd, GC5603_REG_CHIP_ID_HIGH, &v);
	printk("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != GC5603_CHIP_ID_H)
		return -ENODEV;
	*ident = v;

	ret = gc5603_read(sd, GC5603_REG_CHIP_ID_LOW, &v);
	printk("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != GC5603_CHIP_ID_L)
		return -ENODEV;
	*ident = (*ident << 8) | v;

	return 0;
}

static struct gc5603_win_size gc5603_win_sizes[] = {
	{
		.sensor_info.mipi_cfg.clk			= 423,	//data rate = 423Mbps/lane, clk is about 423Mbps.
		.sensor_info.mipi_cfg.twidth		= 2960,
		.sensor_info.mipi_cfg.theight		= 1666,
		.sensor_info.total_height           = 1750,
		.sensor_info.mipi_cfg.mipi_mode		= SENSOR_MIPI_OTHER_MODE,
		.sensor_info.mipi_cfg.mipi_vcomp_en		= 0,
		.sensor_info.mipi_cfg.mipi_hcomp_en		= 0,
		.sensor_info.mipi_cfg.mipi_crop_start0x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start0y	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start1x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start1y	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start2x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start2y	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start3x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start3y	= 0,
		.sensor_info.mipi_cfg.hcrop_diff_en		= 0,
		.sensor_info.mipi_cfg.line_sync_mode	= 0,
		.sensor_info.mipi_cfg.work_start_flag	= 0,
		.sensor_info.mipi_cfg.data_type_en		= 0,
		.sensor_info.mipi_cfg.data_type_value	= 0,
		.sensor_info.mipi_cfg.del_start		= 0,
		.sensor_info.mipi_cfg.sensor_frame_mode	= TX_SENSOR_DEFAULT_FRAME_MODE,
		.sensor_info.mipi_cfg.sensor_fid_mode	= 0,
		.sensor_info.mipi_cfg.sensor_mode		= TX_SENSOR_DEFAULT_MODE,
		.sensor_info.mipi_cfg.sensor_csi_fmt	= TX_SENSOR_RAW10,

		.width				= 2960,
		.height				= 1666,
		.sensor_info.fps				= 30 << 16 | 1,
		.mbus_code	= MEDIA_BUS_FMT_SRGGB10_1X10,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.regs 		= gc5603_init_regs_1920_1080_30fps_mipi,

	},
};

static int gc5603_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
#if 0
	if (code->pad || code->index >= N_5603_FMTS)
		return -EINVAL;

	code->code = gc5603_formats[code->index].mbus_code;
#endif
	return 0;
}

/*
 * Set a format.
 */
static int gc5603_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct gc5603_format_struct *ovfmt;
	struct gc5603_win_size *wsize;
	struct gc5603_info *info = to_state(sd);
	int ret;

	if (format->pad)
		return -EINVAL;

	return 0;
}

static int gc5603_get_fmt(struct v4l2_subdev *sd,
		       struct v4l2_subdev_pad_config *cfg,
		       struct v4l2_subdev_format *format)
{
	struct gc5603_info *info = to_state(sd);
	struct gc5603_win_size *wsize = info->win;
	struct v4l2_mbus_framefmt *fmt = &format->format;
	int ret = 0;

	if(!info->win) {
		dev_err(sd->dev, "sensor win_size not set!\n");
		return -EINVAL;
	}

	fmt->width = wsize->width;
	fmt->height = wsize->height;
	fmt->code = wsize->mbus_code;
	fmt->colorspace = wsize->colorspace;
	*(unsigned int *)fmt->reserved = &wsize->sensor_info;

	printk("----%s, %d, width: %d, height: %d, code: %x\n",
			__func__, __LINE__, fmt->width, fmt->height, fmt->code);

	return ret;
}

static int gc5603_s_brightness(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int gc5603_s_contrast(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int gc5603_s_hflip(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int gc5603_s_vflip(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

/*
 * GAIN is split between REG_GAIN and REG_VREF[7:6].  If one believes
 * the data sheet, the VREF parts should be the most significant, but
 * experience shows otherwise.  There seems to be little value in
 * messing with the VREF bits, so we leave them alone.
 */
static int gc5603_g_gain(struct v4l2_subdev *sd, __s32 *value)
{
	int ret = 0;
	unsigned char gain = 0;

	*value = gain;
	return ret;
}

static int gc5603_s_gain(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static unsigned int again_to_regval(int gain)
{
	struct again_lut *lut = NULL;
	int i = 0;
	for(i = 0; i < ARRAY_SIZE(gc5603_again_lut); i++) {
		lut = &gc5603_again_lut[i];

		if(gain <= lut->gain) {
			return lut->index;
		}
	}

	/*last value.*/
	return lut->index;
}
static int gc5603_g_again(struct v4l2_subdev *sd, __s32 *value)
{
	struct gc5603_info *info = to_state(sd);
	unsigned char v = 0;
	unsigned int reg_val = 0;
	int ret = 0;

	return ret;
}
/*set analog gain db value, map value to sensor register.*/
static int gc5603_s_again(struct v4l2_subdev *sd, int again)
{
	struct gc5603_info *info = to_state(sd);
	struct again_lut *val_lut = gc5603_again_lut;
	unsigned int reg_value = 0;
	int ret = 0;
	if(again < info->again->minimum || again > info->again->maximum) {
		/* use default value. */
		reg_value = again_to_regval(info->again->default_value);
	} else {
		reg_value = again_to_regval(again);
	}

	ret = gc5603_write(sd, 0xfe, 0x00);
	ret += gc5603_write(sd, 0xb4, val_lut[reg_value].regb4);
	ret += gc5603_write(sd, 0xb3, val_lut[reg_value].regb3);
	ret += gc5603_write(sd, 0xb8, val_lut[reg_value].dpc);
	ret += gc5603_write(sd, 0xb9, val_lut[reg_value].blc);
	if(ret < 0){
		printk("%s : gc5603_write error %d~~~~\n",__func__,__LINE__);
		return ret;
	}

	return 0;

}

/*
 * Tweak autogain.
 */
static int gc5603_s_autogain(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int gc5603_s_exp(struct v4l2_subdev *sd, int value)
{
	struct gc5603_info *info = to_state(sd);
	int ret = 0;

	ret = gc5603_write(sd, 0x0203, value&0xff);
	ret += gc5603_write(sd, 0x0202, ((value>>8)&0x3f));
	return ret;
}

static int gc5603_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct gc5603_info *info = to_state(sd);

	switch (ctrl->id) {
	case V4L2_CID_AUTOGAIN:
		return gc5603_g_gain(sd, &info->gain->val);
	case V4L2_CID_ANALOGUE_GAIN:
		return gc5603_g_again(sd, &info->again->val);
	}
	return -EINVAL;
}

static int gc5603_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct gc5603_info *info = to_state(sd);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		return gc5603_s_brightness(sd, ctrl->val);
	case V4L2_CID_CONTRAST:
		return gc5603_s_contrast(sd, ctrl->val);
	case V4L2_CID_VFLIP:
		return gc5603_s_vflip(sd, ctrl->val);
	case V4L2_CID_HFLIP:
		return gc5603_s_hflip(sd, ctrl->val);
	case V4L2_CID_AUTOGAIN:
		/* Only set manual gain if auto gain is not explicitly
		   turned on. */
		if (!ctrl->val) {
			/* gc5603_s_gain turns off auto gain */
			return gc5603_s_gain(sd, info->gain->val);
		}
		return gc5603_s_autogain(sd, ctrl->val);
	case V4L2_CID_GAIN:
		return gc5603_s_gain(sd, ctrl->val);
	case V4L2_CID_ANALOGUE_GAIN:
		return gc5603_s_again(sd, ctrl->val);
	case V4L2_CID_EXPOSURE:
		return gc5603_s_exp(sd, ctrl->val);
	}
	return -EINVAL;
}

static const struct v4l2_ctrl_ops gc5603_ctrl_ops = {
	.s_ctrl = gc5603_s_ctrl,
	.g_volatile_ctrl = gc5603_g_volatile_ctrl,
};

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int gc5603_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	unsigned char val = 0;
	int ret;

	ret = gc5603_read(sd, reg->reg & 0xffff, &val);
	reg->val = val;
	reg->size = 1;
	return ret;
}

static int gc5603_s_register(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg)
{
	gc5603_write(sd, reg->reg & 0xffff, reg->val & 0xff);
	return 0;
}
#endif

int gc5603_s_stream(struct v4l2_subdev *sd, int enable)
{
	int ret = 0;
	if (enable) {
		ret = gc5603_write_array(sd, gc5603_stream_on_mipi);
		printk("gc5603 stream on\n");

	}
	else {
		ret = gc5603_write_array(sd, gc5603_stream_off_mipi);
		printk("gc5603 stream off\n");
	}
	return ret;
}
/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops gc5603_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = gc5603_g_register,
	.s_register = gc5603_s_register,
#endif

};

static const struct v4l2_subdev_video_ops gc5603_video_ops = {
	.s_stream = gc5603_s_stream,
};

static const struct v4l2_subdev_pad_ops gc5603_pad_ops = {
	//.enum_frame_interval = gc5603_enum_frame_interval,
	//.num_frame_size = gc5603_enum_frame_size,
	//.enum_mbus_code = gc5603_enum_mbus_code,
	.set_fmt = gc5603_set_fmt,
	.get_fmt = gc5603_get_fmt,
};

static const struct v4l2_subdev_ops gc5603_ops = {
	.core = &gc5603_core_ops,
	.video = &gc5603_video_ops,
	.pad = &gc5603_pad_ops,
};

/* ----------------------------------------------------------------------- */

static int gc5603_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct v4l2_fract tpf;
	struct v4l2_subdev *sd;
	struct gc5603_info *info;
	int ret;
	unsigned int ident = 0;
	int gpio = -EINVAL;
	unsigned int flags;
	unsigned long rate;
	unsigned char value;
	int mclk_index = -1;
	char id_div[9];
	char id_mux[9];
	info = devm_kzalloc(&client->dev, sizeof(*info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;
	sd = &info->sd;

	gpio = of_get_named_gpio_flags(client->dev.of_node, "ingenic,xshutdn-gpio", 0, &flags);
	if(gpio_is_valid(gpio)) {
		info->xshutdn.pin = gpio;
		info->xshutdn.active_level = (flags == OF_GPIO_ACTIVE_LOW) ? 0 : 1;
	}

	gpio = of_get_named_gpio_flags(client->dev.of_node, "ingenic,efsync-gpio", 0, &flags);
	if(gpio_is_valid(gpio)) {
		info->efsync.pin = gpio;
		info->efsync.active_level = (flags == OF_GPIO_ACTIVE_LOW) ? 0 : 1;
	}

	gpio = of_get_named_gpio_flags(client->dev.of_node, "ingenic,led-gpio", 0, &flags);
	if(gpio_is_valid(gpio)) {
		info->led.pin = gpio;
		info->led.active_level = (flags == OF_GPIO_ACTIVE_LOW) ? 0 : 1;
	}

	gc5603_power(sd, 1);
    gc5603_lightup(sd, 0);

	v4l2_i2c_subdev_init(sd, client, &gc5603_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	/*clk*/
	of_property_read_u32(client->dev.of_node, "ingenic,mclk", &mclk_index);
	if(mclk_index == 0) {
		memcpy(id_div, "div_cim0", sizeof(id_div));
		memcpy(id_mux, "mux_cim0", sizeof(id_mux));
	} else if(mclk_index == 1) {
		memcpy(id_div, "div_cim1", sizeof(id_div));
		memcpy(id_mux, "mux_cim1", sizeof(id_mux));
	} else if(mclk_index == 2) {
		memcpy(id_div, "div_cim2", sizeof(id_div));
		memcpy(id_mux, "mux_cim2", sizeof(id_mux));
	} else
		printk("Unkonwn mclk index\n");

	info->clk = v4l2_clk_get(&client->dev, id_div);
	if (IS_ERR(info->clk)) {
		ret = PTR_ERR(info->clk);
		goto err_clkget;
	}

	ret = v4l2_clk_set_rate(info->clk, 27000000);
	if(ret)
		dev_err(sd->dev, "clk_set_rate err!\n");

	ret = v4l2_clk_enable(info->clk);
	if(ret)
		dev_err(sd->dev, "clk_enable err!\n");

	/* Make sure it's an gc5603 */
	ret = gc5603_detect(sd, &ident);
	if (ret) {
		v4l_err(client,
			"chip found @ 0x%x (%s) is not an gc5603 chip.\n",
			client->addr << 1, client->adapter->name);
		return ret;
	}
	/*IRCUT ctl 0:off 1:on*/
	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	v4l2_ctrl_handler_init(&info->hdl, 8);
	v4l2_ctrl_new_std(&info->hdl, &gc5603_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 255, 1, 128);
	v4l2_ctrl_new_std(&info->hdl, &gc5603_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 127, 1, 64);
	v4l2_ctrl_new_std(&info->hdl, &gc5603_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&info->hdl, &gc5603_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
	info->gain = v4l2_ctrl_new_std(&info->hdl, &gc5603_ctrl_ops,
			V4L2_CID_GAIN, 0, 255, 1, 128);
	info->again = v4l2_ctrl_new_std(&info->hdl, &gc5603_ctrl_ops,
			V4L2_CID_ANALOGUE_GAIN, 0, 444858, 1, 10000);

	/*unit exposure lines: */
	info->exposure = v4l2_ctrl_new_std(&info->hdl, &gc5603_ctrl_ops,
			V4L2_CID_EXPOSURE, 1, 0x2000, 1, 0x1000);

	sd->ctrl_handler = &info->hdl;
	if (info->hdl.error) {
		int err = info->hdl.error;
		v4l2_ctrl_handler_free(&info->hdl);
		return err;
	}
	v4l2_ctrl_handler_setup(&info->hdl);

	info->win = &gc5603_win_sizes[0];

	gc5603_init(sd, 1);
	info->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&info->sd.entity, 1, &info->pad);
	if(ret < 0) {
		goto err_entity_init;
	}
	info->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = v4l2_async_register_subdev(&info->sd);
	if (ret < 0)
		goto err_videoprobe;

	return 0;
err_videoprobe:
err_entity_init:
	v4l2_clk_put(info->clk);
err_clkget:
	return ret;
}


static int gc5603_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc5603_info *info = to_state(sd);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&info->hdl);
	v4l2_clk_put(info->clk);
	return 0;
}

static const struct i2c_device_id gc5603_id[] = {
	{ "gc5603", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, gc5603_id);

static const struct of_device_id gc5603_of_match[] = {
	{.compatible = "galaxy,gc5603", },
	{},
};
MODULE_DEVICE_TABLE(of, gc5603_of_match);


static struct i2c_driver gc5603_driver = {
	.driver = {
		.name	= "gc5603",
		.of_match_table = of_match_ptr(gc5603_of_match),
	},
	.probe		= gc5603_probe,
	.remove		= gc5603_remove,
	.id_table	= gc5603_id,
};

module_i2c_driver(gc5603_driver);
MODULE_DESCRIPTION("A low-level driver for gc5603 sensors");
MODULE_LICENSE("GPL");
