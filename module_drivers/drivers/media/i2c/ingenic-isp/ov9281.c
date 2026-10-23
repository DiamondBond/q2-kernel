/*
 * A V4L2 driver for OmniVision ov9281 cameras.
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
#include <linux/regulator/consumer.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-clk.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-image-sizes.h>
#include <linux/of_gpio.h>
#include <isp-sensor.h>

/*** x2000-isp-mipi dtsi
&i2c1 {
	status = "okay";
	clock-frequency = <100000>;
	timeout = <1000>;
	pinctrl-names = "default";
	pinctrl-0 = <&i2c1_pc>;

	ov9281:ov9281@60 {
		status = "okay";
		compatible = "ovti,ov9281";
		reg = <0x60>;
		pinctrl-names = "default";
		pinctrl-0 = <&cim_vic_mclk_pe>;

		ingenic,pwdn-gpio =  <&gpb 7 GPIO_ACTIVE_HIGH INGENIC_GPIO_NOBIAS>;
		ingenic,rst-gpio = <&gpb 1 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>;

		port {
			ov9281_ep0:endpoint {
				remote-endpoint = <&isp0_ep>;
			};
		};
	};
};

&isp0_ep {
	remote-endpoint = <&ov9281_ep0>;
	data-lanes = <0 1>;
	clk-lanes = <2>;
};
***/


#define OV9281_CHIP_ID_H	(0x92)
#define OV9281_CHIP_ID_L	(0x81)
#define OV9281_CHIP_ID          (0x300A)
#define OV9281_CHIP_ID_REG      (0x300B)

#define OV9281_REG_END		0xffff

static bool debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

struct ov9281_win_size {
	unsigned int width;
	unsigned int height;
	unsigned int mbus_code;
	struct sensor_info sensor_info;
	enum v4l2_colorspace colorspace;
	void *regs;
};

struct ov9281_gpio {
	int pin;
	int active_level;
};

struct ov9281_info {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *gain;
	struct v4l2_ctrl *again;

	struct v4l2_clk *clk;
	struct v4l2_clk *mclk;

	struct v4l2_ctrl *exposure;

	struct media_pad pad;

	struct v4l2_subdev_format *format;	/*current fmt.*/
	struct ov9281_win_size *win;

	struct ov9281_gpio reset;
	struct ov9281_gpio pwdn;
};


/*
 * the part of driver maybe modify about different sensor and different board.
 */
struct again_lut {
	unsigned int value;	/*sensor regs value*/
	unsigned int gain;	/*isp gain*/
};

#if 0
struct again_lut ov9281_again_lut[] = {
	{0x10, 0},
	{0x28,86643},
}; //根据手册0x3509 增益倍数关系描述填写；
#else
struct again_lut ov9281_again_lut[] = {
	{0x10,	0},
	{0x11,	5731},
	{0x12,	11136},
	{0x13,	16248},
	{0x14,	21097},
	{0x15,	25710},
	{0x16,	30109},
	{0x17,	34312},
	{0x18,	38336},
	{0x19,	42195},
	{0x1a,	45904},
	{0x1b,	49472},
	{0x1c,	52910},
	{0x1d,	56228},
	{0x1e,	59433},
	{0x1f,	62534},
	{0x20,	65536},
	{0x22,	71267},
	{0x24,	76672},
	{0x26,	81784},
	{0x28,	86633},
	{0x2a,	91246},
	{0x2c,	95645},
	{0x2e,	99848},
	{0x30,	103872},
	{0x32,	107731},
	{0x34,	111440},
	{0x36,	115008},
	{0x38,	118446},
	{0x3a,	121764},
	{0x3c,	124969},
	{0x3e,	128070},
	{0x40,	131072},
	{0x44,	136803},
	{0x48,	142208},
	{0x4c,	147320},
	{0x50,	152169},
	{0x54,	156782},
	{0x58,	161181},
	{0x5c,	165384},
	{0x60,	169408},
	{0x64,	173267},
	{0x68,	176976},
	{0x6c,	180544},
	{0x70,	183982},
	{0x74,	187300},
	{0x78,	190505},
	{0x7c,	193606},
	{0x80,	196608},
	{0x88,	202339},
	{0x90,	207744},
	{0x98,	212856},
	{0xa0,	217705},
	{0xa8,	222318},
	{0xb0,	226717},
	{0xb8,	230920},
	{0xc0,	234944},
	{0xc8,	238803},
	{0xd0,	242512},
	{0xd8,	246080},
	{0xe0,	249518},
	{0xe8,	252836},
	{0xf0,	256041},
	{0xf8,	259142},
}; //参考ov2735b.c
#endif

static inline struct ov9281_info *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov9281_info, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct ov9281_info, hdl)->sd;
}
struct regval_list {
	uint16_t reg_num;
	unsigned short value;
};

static struct regval_list ov9281_init_regs_800p_120fps_mipi[] = {
	{ 0x0103, 0x01},
	{ 0x0302, 0x32},
	{ 0x030d, 0x50},
	{ 0x030e, 0x02},
	{ 0x3001, 0x00},
	{ 0x3004, 0x00},
	{ 0x3005, 0x00},
	{ 0x3006, 0x04},
	{ 0x3011, 0x0a},
	{ 0x3013, 0x18},
	{ 0x3022, 0x01},
	{ 0x3030, 0x10},
	{ 0x3039, 0x32},
	{ 0x303a, 0x00},
	{ 0x3500, 0x00},
	{ 0x3501, 0x2a},
	{ 0x3502, 0x90},
	{ 0x3503, 0x08},
	{ 0x3505, 0x8c},
	{ 0x3507, 0x03},
	{ 0x3508, 0x00},
	{ 0x3509, 0x10},
	{ 0x3610, 0x80},
	{ 0x3611, 0xa0},
	{ 0x3620, 0x6f},
	{ 0x3632, 0x56},
	{ 0x3633, 0x78},
	{ 0x3662, 0x05}, /* 2 lane raw8 */
	{ 0x3666, 0x00},
	{ 0x366f, 0x5a},
	{ 0x3680, 0x84},
	{ 0x3712, 0x80},
	{ 0x372d, 0x22},
	{ 0x3731, 0x80},
	{ 0x3732, 0x30},
	{ 0x3778, 0x00},
	{ 0x377d, 0x22},
	{ 0x3788, 0x02},
	{ 0x3789, 0xa4},
	{ 0x378a, 0x00},
	{ 0x378b, 0x4a},
	{ 0x3799, 0x20},
	{ 0x3800, 0x00},
	{ 0x3801, 0x00},
	{ 0x3802, 0x00},
	{ 0x3803, 0x00},
	{ 0x3804, 0x05},
	{ 0x3805, 0x0f},
	{ 0x3806, 0x03},
	{ 0x3807, 0x2f},
	{ 0x3808, 0x05},
	{ 0x3809, 0x00},
	{ 0x380a, 0x03},
	{ 0x380b, 0x20},

#if 1 /* 120 fps */
	{ 0x380c, 0x02}, /* hsync */
	{ 0x380d, 0xd8},
	{ 0x380e, 0x03}, /* vsync */
	{ 0x380f, 0x8e},
#endif

#if 0 /* 30 fps */
	{ 0x380e, 0x0e},
	{ 0x380f, 0x38},
#endif
#if 0 /* 60 fps */
	{ 0x380e, 0x07}, /* vsync */
	{ 0x380f, 0x1c},
#endif
	{ 0x3810, 0x00},
	{ 0x3811, 0x08},
	{ 0x3812, 0x00},
	{ 0x3813, 0x08},
	{ 0x3814, 0x11},
	{ 0x3815, 0x11},
	{ 0x3820, 0x40},
	{ 0x3821, 0x00},
	{ 0x3881, 0x42},
	{ 0x38b1, 0x00},
	{ 0x3920, 0xff},
	{ 0x4003, 0x40},
	{ 0x4008, 0x04},
	{ 0x4009, 0x0b},
	{ 0x400c, 0x00},
	{ 0x400d, 0x07},
	{ 0x4010, 0x40},
	{ 0x4043, 0x40},
	{ 0x4307, 0x30},
	{ 0x4317, 0x00},
	{ 0x4501, 0x00},
	{ 0x4507, 0x00},
	{ 0x4509, 0x00},
	{ 0x450a, 0x08},
	{ 0x4601, 0x04},
	{ 0x470f, 0x00},
	{ 0x4f07, 0x00},
	{ 0x4800, 0x00},
	{ 0x5000, 0x9f},
//	{ 0x5001, 0x00},
	{ 0x5001, 0x04},
	{ 0x5e00, 0x00},
	{ 0x5d00, 0x07},
	{ 0x5d01, 0x00},
//	{ 0x0100, 0x00},

	{OV9281_REG_END, 0xff},	/* END MARKER */
};

/*
 * the part of driver was fixed.
 */

static struct regval_list ov9281_stream_on[] = {
	{0x0100, 0x01},
	{OV9281_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list ov9281_stream_off[] = {
	{0x0100, 0x00},
	{OV9281_REG_END, 0x00},	/* END MARKER */
};

static int ov9281_read(struct v4l2_subdev *sd, unsigned short reg,
		unsigned char *value)
{
	struct ov9281_info *info = to_state(sd);
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

	ret = i2c_transfer(client->adapter, msg, 2);
	if(ret > 0)
		ret = 0;

	return ret;
}

static int ov9281_write(struct v4l2_subdev *sd, unsigned short reg,
		unsigned short value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	uint8_t buf[3]={(reg>>8)&0xff,reg&0xff,value};
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= 3,
		.buf	= buf,
	};
	int ret;
	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret > 0)
		ret = 0;

	return ret;
}

static int ov9281_read_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	int ret;
	unsigned char val;
	uint16_t value;
	while (vals->reg_num != OV9281_REG_END) {
		ret = ov9281_read(sd, vals->reg_num, &val);
		if (ret < 0)
		{
			printk("array: 0x%02x, 0x%02x\n",vals->reg_num, val);
			return ret;
		}
		vals++;
	}
	return 0;
}


/*
 * Write a list of register settings; ff/ff stops the process.
 */
static int ov9281_write_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	int ret;
	while (vals->reg_num != OV9281_REG_END) {
		ret = ov9281_write(sd, vals->reg_num, vals->value);
		if (ret < 0)
			return ret;
		vals++;
	}
	return 0;
}


/*
 * Stuff that knows about the sensor.
 */
static int ov9281_reset(struct v4l2_subdev *sd, u32 val)
{
	struct ov9281_info *info = to_state(sd);

	if(val) {
		gpio_direction_output(info->reset.pin, info->reset.active_level);
		msleep(10);
		gpio_direction_output(info->reset.pin, !info->reset.active_level);
		msleep(10);
	}
	return 0;
}

static int ov9281_power(struct v4l2_subdev *sd, u32 val)
{
	struct ov9281_info *info = to_state(sd);

	if(val) {
		gpio_direction_output(info->pwdn.pin, !info->pwdn.active_level);
		msleep(10);
		gpio_direction_output(info->pwdn.pin, info->pwdn.active_level);
		msleep(10);
	}
	return 0;
}

static int ov9281_init(struct v4l2_subdev *sd, u32 val)
{
	struct ov9281_info *info = to_state(sd);
	int ret = 0;

	ret = ov9281_write_array(sd, info->win->regs);

	return ret;
}



static int ov9281_detect(struct v4l2_subdev *sd, unsigned int *ident)
{
	unsigned char v;
	int ret;
	ret = ov9281_read(sd, OV9281_CHIP_ID, &v);
	printk("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != OV9281_CHIP_ID_H)
		return -ENODEV;
	*ident = v;

	ret = ov9281_read(sd, OV9281_CHIP_ID_REG, &v);
	printk("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != OV9281_CHIP_ID_L)
		return -ENODEV;
	*ident = (*ident << 8) | v;

	return 0;
}


static struct ov9281_win_size ov9281_win_sizes[] = {
	{
		.sensor_info.mipi_cfg.clk		= 200,
		.sensor_info.mipi_cfg.twidth		= 1280,
		.sensor_info.mipi_cfg.theight		= 800,
		.sensor_info.mipi_cfg.mipi_crop_start0x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start0y	= 0,
		.sensor_info.fps			= 45 << 16 | 1,
//		.sensor_info.total_width			= 1296,
//		.sensor_info.total_height			= 816,
		.width		= 1280,
		.height		= 800,
		.mbus_code	= MEDIA_BUS_FMT_SBGGR10_1X10,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.regs 		= ov9281_init_regs_800p_120fps_mipi,
	},
};

static int ov9281_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
#if 0
	if (code->pad || code->index >= N_ov9281_FMTS)
		return -EINVAL;

	code->code = ov9281_formats[code->index].mbus_code;
#endif
	return 0;
}

/*
 * Set a format.
 */
static int ov9281_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct ov9281_format_struct *ovfmt;
	struct ov9281_win_size *wsize;
	struct ov9281_info *info = to_state(sd);
	int ret;

	if (format->pad)
		return -EINVAL;

	return 0;
}

static int ov9281_get_fmt(struct v4l2_subdev *sd,
		       struct v4l2_subdev_pad_config *cfg,
		       struct v4l2_subdev_format *format)
{
	struct ov9281_info *info = to_state(sd);
	struct v4l2_mbus_framefmt *fmt = &format->format;
	int ret = 0;

	if(!info->win) {
		dev_err(sd->dev, "sensor win_size not set!\n");
		return -EINVAL;
	}

	fmt->width = info->win->width;
	fmt->height = info->win->height;
	fmt->code = info->win->mbus_code;
	fmt->colorspace = info->win->colorspace;
	*(unsigned int *)fmt->reserved = &info->win->sensor_info;


//	printk("----%s, %d, width: %d, height: %d, code: %x\n",
//			__func__, __LINE__, fmt->width, fmt->height, fmt->code);

	return ret;
}

static int ov9281_s_brightness(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int ov9281_s_contrast(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int ov9281_s_hflip(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int ov9281_s_vflip(struct v4l2_subdev *sd, int value)
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
static int ov9281_g_gain(struct v4l2_subdev *sd, __s32 *value)
{
	int ret = 0;
	unsigned char gain = 0;

	*value = gain;
	return ret;
}

static int ov9281_s_gain(struct v4l2_subdev *sd, int value)
{
	int ret = 0;
	return ret;
}

static unsigned int again_to_regval(int gain)
{
	struct again_lut *lut = NULL;
	int i = 0;
	for(i = 0; i < ARRAY_SIZE(ov9281_again_lut); i++) {
		lut = &ov9281_again_lut[i];

		if(gain <= lut->gain) {
			return lut->value;
		}
	}

	/*last value.*/
	return lut->value;
}

static int regval_to_again(unsigned int regval)
{
	struct again_lut *lut = NULL;
	int i = 0;
	for(i = 0; i < ARRAY_SIZE(ov9281_again_lut); i++) {
		lut = &ov9281_again_lut[i];

		if(regval == lut->value) {
			return lut->gain;
		}
	}
//	printk("%s, %d regval not mapped to isp gain\n", __func__, __LINE__);
	return 0;
}

static int ov9281_g_again(struct v4l2_subdev *sd, __s32 *value)
{
	struct ov9281_info *info = to_state(sd);
	unsigned char reg_val = 0;
	int ret = 0;


	ret = ov9281_read(sd, 0x3509, &reg_val);

	*value = regval_to_again(reg_val);

	return ret;
}
/*set analog gain db value, map value to sensor register.*/
static int ov9281_s_again(struct v4l2_subdev *sd, int value)
{
	struct ov9281_info *info = to_state(sd);
	unsigned int reg_value = 0;
	int ret = 0;
	int i;

	if(value < info->again->minimum || value > info->again->maximum) {
		/* use default value. */
		reg_value = again_to_regval(info->again->default_value);
	} else {
		reg_value = again_to_regval(value);
	}

	ret += ov9281_write(sd, 0x3509, reg_value);
	if (ret < 0)
		return ret;

	return ret;
}

/*
 * Tweak autogain.
 */
static int ov9281_s_autogain(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int ov9281_s_exp(struct v4l2_subdev *sd, int value)
{
	struct ov9281_info *info = to_state(sd);
	int ret = 0;

	ret |= ov9281_write(sd, 0x3500, (value >> 16) & 0x0F);  // 取bit[19:16]
	ret |= ov9281_write(sd, 0x3501, (value >> 8) & 0xFF);   // 取bit[15:8]
	ret |= ov9281_write(sd, 0x3502, value & 0xFF);         // 取bit[7:0]
	return ret;
}

static int ov9281_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct ov9281_info *info = to_state(sd);

	switch (ctrl->id) {
	case V4L2_CID_AUTOGAIN:
		return ov9281_g_gain(sd, &info->gain->val);
	case V4L2_CID_ANALOGUE_GAIN:
		return ov9281_g_again(sd, &info->again->val);
	}
	return -EINVAL;
}

static int ov9281_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct ov9281_info *info = to_state(sd);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		return ov9281_s_brightness(sd, ctrl->val);
	case V4L2_CID_CONTRAST:
		return ov9281_s_contrast(sd, ctrl->val);
	case V4L2_CID_VFLIP:
		return ov9281_s_vflip(sd, ctrl->val);
	case V4L2_CID_HFLIP:
		return ov9281_s_hflip(sd, ctrl->val);
	case V4L2_CID_AUTOGAIN:
		/* Only set manual gain if auto gain is not explicitly
		   turned on. */
		if (!ctrl->val) {
			/* ov9281_s_gain turns off auto gain */
			return ov9281_s_gain(sd, info->gain->val);
		}
		return ov9281_s_autogain(sd, ctrl->val);
	case V4L2_CID_GAIN:
		return ov9281_s_gain(sd, ctrl->val);
	case V4L2_CID_ANALOGUE_GAIN:
		return ov9281_s_again(sd, ctrl->val);
	case V4L2_CID_EXPOSURE:
		return ov9281_s_exp(sd, ctrl->val);
	}
	return -EINVAL;
}

static const struct v4l2_ctrl_ops ov9281_ctrl_ops = {
	.s_ctrl = ov9281_s_ctrl,
	.g_volatile_ctrl = ov9281_g_volatile_ctrl,
};

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ov9281_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	unsigned char val = 0;
	int ret;

	ret = ov9281_read(sd, reg->reg & 0xffff, &val);
	reg->val = val;
	reg->size = 1;
	return ret;
}

static int ov9281_s_register(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg)
{
	ov9281_write(sd, reg->reg & 0xffff, reg->val & 0xff);
	return 0;
}
#endif



int ov9281_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov9281_info *info = to_state(sd);
	int ret = 0;
	struct regval_list *vals;

	if (enable) {
		ret = ov9281_write_array(sd, ov9281_stream_on);
		pr_debug("ov9281 stream on\n");
	}
	else {
		ret = ov9281_write_array(sd, ov9281_stream_off);
		pr_debug("ov9281 stream off\n");
	}
	return ret;
}
int ov9281_g_frame_interval(struct v4l2_subdev *sd, struct v4l2_subdev_frame_interval *interval)
{
	struct ov9281_info *info = to_state(sd);
	if(info->win->sensor_info.fps){
		interval->interval.numerator = info->win->sensor_info.fps & 0xffff;
		interval->interval.denominator = info->win->sensor_info.fps >> 16;
		return 0;
	}
	return -EINVAL;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops ov9281_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = ov9281_g_register,
	.s_register = ov9281_s_register,
#endif

};

static const struct v4l2_subdev_video_ops ov9281_video_ops = {
	.s_stream = ov9281_s_stream,
	.g_frame_interval = ov9281_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops ov9281_pad_ops = {
	//.enum_frame_interval = ov9281_enum_frame_interval,
	//.num_frame_size = ov9281_enum_frame_size,
	//.enum_mbus_code = ov9281_enum_mbus_code,
	.set_fmt = ov9281_set_fmt,
	.get_fmt = ov9281_get_fmt,
};

static const struct v4l2_subdev_ops ov9281_ops = {
	.core = &ov9281_core_ops,
	.video = &ov9281_video_ops,
	.pad = &ov9281_pad_ops,
};

/* ----------------------------------------------------------------------- */

static int ov9281_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct v4l2_fract tpf;
	struct v4l2_subdev *sd;
	struct ov9281_info *info;
	int ret;
	unsigned int ident = 0;
	int gpio = -EINVAL;
	unsigned int flags;

	info = devm_kzalloc(&client->dev, sizeof(*info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;
	sd = &info->sd;

	gpio = of_get_named_gpio_flags(client->dev.of_node, "ingenic,rst-gpio", 0, &flags);
	if(gpio_is_valid(gpio)) {
		info->reset.pin = gpio;
		info->reset.active_level = (flags == OF_GPIO_ACTIVE_LOW) ? 0 : 1;
	}
	gpio = of_get_named_gpio_flags(client->dev.of_node, "ingenic,pwdn-gpio", 0, &flags);
	if(gpio_is_valid(gpio)) {
		info->pwdn.pin = gpio;
		info->pwdn.active_level = (flags == OF_GPIO_ACTIVE_LOW) ? 0 : 1;
	}

	v4l2_i2c_subdev_init(sd, client, &ov9281_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	ov9281_power(sd, 1);
	ov9281_reset(sd, 1);
	/*clk*/
	info->mclk = v4l2_clk_get(&client->dev, "gate_cim_mclk");
	if (IS_ERR(info->mclk)) {
		ret = PTR_ERR(info->mclk);
		goto err_mclkget;
	}
	ret = v4l2_clk_enable(info->mclk);
	if(ret)
		dev_err(sd->dev, "mclk_enable err!\n");

	info->clk = v4l2_clk_get(&client->dev, "div_cim");
	if (IS_ERR(info->clk)) {
		ret = PTR_ERR(info->clk);
		goto err_clkget;
	}

	ret = v4l2_clk_set_rate(info->clk, 24000000);
	if(ret)
		dev_err(sd->dev, "clk_set_rate err!\n");

	ret = v4l2_clk_enable(info->clk);
	if(ret)
		dev_err(sd->dev, "clk_enable err!\n");

#if 1
	/* Make sure it's an ov9281 */
	ret = ov9281_detect(sd, &ident);
	if (ret) {
		v4l_err(client,
			"chip found @ 0x%x (%s) is not an ov9281 chip.\n",
			client->addr << 1, client->adapter->name);
		return ret;
	}
#endif

	/*IRCUT ctl 0:off 1:on*/
	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	v4l2_ctrl_handler_init(&info->hdl, 8);
	v4l2_ctrl_new_std(&info->hdl, &ov9281_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 255, 1, 128);
	v4l2_ctrl_new_std(&info->hdl, &ov9281_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 127, 1, 64);
	v4l2_ctrl_new_std(&info->hdl, &ov9281_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&info->hdl, &ov9281_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
	info->gain = v4l2_ctrl_new_std(&info->hdl, &ov9281_ctrl_ops,
			V4L2_CID_GAIN, 0, 255, 1, 128);
	info->again = v4l2_ctrl_new_std(&info->hdl, &ov9281_ctrl_ops,
			V4L2_CID_ANALOGUE_GAIN, 1, 262144, 1, 10000);

	/*unit exposure lines: */
	info->exposure = v4l2_ctrl_new_std(&info->hdl, &ov9281_ctrl_ops,
			V4L2_CID_EXPOSURE, 4, 1520 - 4, 1, 1000);

	sd->ctrl_handler = &info->hdl;
	if (info->hdl.error) {
		int err = info->hdl.error;

		v4l2_ctrl_handler_free(&info->hdl);
		return err;
	}
	v4l2_ctrl_handler_setup(&info->hdl);

	info->win = &ov9281_win_sizes[0];

	ov9281_init(sd, 1);

	info->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&info->sd.entity, 1, &info->pad);
	if(ret < 0) {
		goto err_entity_init;
	}
	info->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = v4l2_async_register_subdev(&info->sd);
	if (ret < 0)
		goto err_videoprobe;

	dev_info(&client->dev, "ov9281 Probed\n");
	return 0;
err_videoprobe:
err_entity_init:
	v4l2_clk_put(info->clk);
err_clkget:
	v4l2_clk_put(info->mclk);
err_mclkget:
	return ret;
}


static int ov9281_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov9281_info *info = to_state(sd);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&info->hdl);
	v4l2_clk_put(info->clk);
	return 0;
}

static const struct i2c_device_id ov9281_id[] = {
	{ "ov9281", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov9281_id);

static const struct of_device_id ov9281_of_match[] = {
	{.compatible = "onsemi,ov9281", },
	{},
};
MODULE_DEVICE_TABLE(of, ov2640_of_match);

int ov9281_suspend(struct device *dev)
{
        struct i2c_client *client = container_of(dev, struct i2c_client, dev);
        struct v4l2_subdev *sd = i2c_get_clientdata(client);
        struct ov9281_info *info = to_state(sd);
	int ret=0;

	ov9281_power(sd, 0);
	v4l2_clk_disable(info->clk);
        return 0;
}

int ov9281_resume(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov9281_info *info = to_state(sd);
	int ret=0;

	ov9281_power(sd, 1);
	v4l2_clk_enable(info->clk);
	ov9281_reset(sd, 1);
	ov9281_init(sd, 1);
	return ret;
}

const struct dev_pm_ops ov9281_pm =
{
        .suspend = ov9281_suspend,
        .resume = ov9281_resume,
};


static struct i2c_driver ov9281_driver = {
	.driver = {
		.name	= "ov9281",
		.of_match_table = of_match_ptr(ov9281_of_match),
		.pm     = &ov9281_pm,
	},
	.probe		= ov9281_probe,
	.remove		= ov9281_remove,
	.id_table	= ov9281_id,
};

module_i2c_driver(ov9281_driver);
MODULE_AUTHOR("qpz <aric.pzqi@ingenic.com>");
MODULE_DESCRIPTION("A low-level driver for onsemi ov9281 sensors");
MODULE_LICENSE("GPL");
