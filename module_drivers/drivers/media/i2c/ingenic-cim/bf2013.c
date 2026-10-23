/*
 * bf2013 Camera Driver
 *
 * Copyright (C) 2010 Alberto Panizzo <maramaopercheseimorto@gmail.com>
 *
 * Based on ov772x, ov9640 drivers and previous non merged implementations.
 *
 * Copyright 2005-2009 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2006, OmniVision
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/of_gpio.h>
#include <linux/v4l2-mediabus.h>
#include <linux/videodev2.h>

#include <media/v4l2-clk.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-device.h>
#include <ingenic_camera.h>

#include <linux/clk.h>
#define	 PID_BME_REG		0xfc
#define  VER_BME_REG		0xfd
#define  PID_VAL		0x37
#define  VER_VAL		0x03

#define  BF2013_DEFAULT_WIDTH   640
#define  BF2013_DEFAULT_HEIGHT  480


/*
 * Struct
 */
struct regval_list {
	u8 reg_num;
	u8 value;
};

/* Supported resolutions */
enum bf2013_width {
	W_VGA	= BF2013_DEFAULT_WIDTH,
};

enum bf2013_height {
	H_VGA	= BF2013_DEFAULT_HEIGHT,
};

struct bf2013_win_size {
	char *name;
	enum bf2013_width width;
	enum bf2013_height height;
	unsigned int mbus_code;
};

struct bf2013_priv {
	struct v4l2_subdev		subdev;
	u32	cfmt_code;
	struct v4l2_clk			*clk;
	struct v4l2_clk			*mclk;
	const struct bf2013_win_size	*win;
	struct gpio_desc *resetb_gpio;
	struct gpio_desc *pwdn_gpio;
	struct gpio_desc *pwr_gpio;

	int (*power)(struct i2c_client *client, int on);
};

static inline int sensor_i2c_master_send(struct i2c_client *client,
		const char *buf ,int count)
{
	int ret;
	struct i2c_adapter *adap=client->adapter;
	struct i2c_msg msg;

	msg.addr = client->addr;
	msg.flags = client->flags & I2C_M_TEN;
	msg.len = count;
	msg.buf = (char *)buf;

	ret = i2c_transfer(adap, &msg, 1);

	return (ret == 1) ? count : ret;
}

static inline int sensor_i2c_master_recv(struct i2c_client *client,
		char *buf ,int count)
{
	struct i2c_adapter *adap=client->adapter;
	struct i2c_msg msg;
	int ret;

	msg.addr = client->addr;
	msg.flags = client->flags & I2C_M_TEN;
	msg.flags |= I2C_M_RD;
	msg.len = count;
	msg.buf = buf;
	ret = i2c_transfer(adap, &msg, 1);

	return (ret == 1) ? count : ret;
}


unsigned char bf2013_read_reg(struct i2c_client *client, u8 reg)
{
	int ret;
	unsigned char retval;

	ret = sensor_i2c_master_send(client,&reg,1);

	if (ret < 0)
		return ret;
	if (ret != 1)
		return -EIO;

	ret = sensor_i2c_master_recv(client, &retval, 1);
	if (ret < 0)
		return ret;
	if (ret != 1)
		return -EIO;

	return retval;
}

int bf2013_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	unsigned char msg[2];
	int ret;

	msg[0] = reg;
	msg[1] = val;

	ret = sensor_i2c_master_send(client, msg, 2);

	if (ret < 0)
	{
		printk("RET<0\n");
		return ret;
	}
	if (ret < 2)
	{
		printk("RET<2\n");
		return -EIO;
	}

	return 0;
}

/*
 * Registers settings
 */

#define ENDMARKER { 0xff, 0xff }

static const struct regval_list bf2013_init_regs[] = {
	{0x09,0x01},
	{0x15,0x02},
	{0x12,0x00},
	{0x1e,0x00},
	{0x13,0x00},
	{0x01,0x14},
	{0x02,0x21},
	{0x8c,0x02},
	{0x8d,0x64},
	{0x87,0x18},
	{0x13,0x07},
	{0x11,0x80},
	{0x2b,0x00},
	{0x92,0x64},
	{0x9d,0x99},
	{0x06,0xe0},
	{0x29,0x54},
	{0xeb,0x30},
	{0xbb,0x20},
	{0xf5,0x21},
	{0xe1,0x3c},
	{0x16,0x01},
	{0xe0,0x0b},
	{0x2f,0xf6},
	{0x33,0x20},
	{0x34,0x08},
	{0x35,0x50},
	{0x65,0x53},
	{0x66,0x50},
	{0x36,0x05},
	{0x37,0xf6},
	{0x38,0x46},
	{0x9b,0xf6},
	{0x9c,0x46},
	{0xbc,0x01},
	{0xbd,0xf6},
	{0xbe,0x46},
	{0x82,0x14},
	{0x83,0x23},
	{0x9a,0x23},
	{0x70,0x6f},
	{0x72,0x4f},
	{0x73,0x2f},
	{0x74,0x27},
	{0x77,0x90},
	{0x79,0x48},
	{0x7a,0x1e},
	{0x7b,0x30},
	{0x84,0x1a},
	{0x85,0x20},
	{0x89,0x02},
	{0x8a,0x64},
	{0x86,0x3f},
	{0x96,0xa6},
	{0x97,0x0c},
	{0x98,0x18},
	{0x80,0x55},
	{0x24,0x78},
	{0x25,0x88},
	{0x69,0x00},
	{0x94,0x0a},
	{0x1f,0x20},
	{0x22,0x20},
	{0x26,0x20},
	{0x56,0x40},
	{0x61,0xd3},
	{0x79,0x48},
	{0x3b,0x60},
	{0x3c,0x20},
	{0x39,0x80},
	{0x3f,0xb0},
	{0x39,0x80},
	{0x40,0x58},
	{0x41,0x54},
	{0x42,0x4e},
	{0x43,0x44},
	{0x44,0x3e},
	{0x45,0x39},
	{0x46,0x35},
	{0x47,0x31},
	{0x48,0x2e},
	{0x49,0x2b},
	{0x4b,0x29},
	{0x4c,0x27},
	{0x4e,0x23},
	{0x4f,0x20},
	{0x50,0x1e},
	{0x51,0x05},
	{0x52,0x10},
	{0x53,0x0b},
	{0x54,0x15},
	{0x57,0x87},
	{0x58,0x72},
	{0x59,0x5f},
	{0x5a,0x7e},
	{0x5b,0x1f},
	{0x5c,0x0e},
	{0x5d,0x95},
	{0x60,0x28},
	{0xb0,0xe0},
	{0xb1,0xc0},
	{0xb2,0xb0},
	{0xb3,0x88},
	{0x6a,0x01},
	{0x23,0x66},
	{0xa0,0x03},
	{0xa1,0x31},
	{0xa2,0x0b},
	{0xa3,0x26},
	{0xa4,0x05},
	{0xa5,0x25},
	{0xa6,0x06},
	{0xa7,0x80},
	{0xa8,0x80},
	{0xa9,0x20},
	{0xaa,0x20},
	{0xab,0x20},
	{0xac,0x3c},
	{0xad,0xf0},
	{0xc8,0x18},
	{0xc9,0x20},
	{0xca,0x17},
	{0xcb,0x1f},
	{0xaf,0x00},
	{0xc5,0x18},
	{0xc6,0x00},
	{0xc7,0x20},
	{0xae,0x80},
	{0xcc,0x40},
	{0xcd,0x58},
	{0xee,0x4c},
	{0x8e,0x02},
	{0x8f,0x64},
	ENDMARKER,
};

static const struct regval_list bf2013_brightness_tab[][3] = {
	{{0x24, 0x38},  {0x25, 0x48}, ENDMARKER},
	{{0x24, 0x48},  {0x25, 0x58}, ENDMARKER},
	{{0x24, 0x58},  {0x25, 0x68}, ENDMARKER},
	{{0x24, 0x60},  {0x25, 0x78}, ENDMARKER},
	{{0x24, 0x78},  {0x25, 0x88}, ENDMARKER},
	{{0x24, 0x88},  {0x25, 0x98}, ENDMARKER},
	{{0x24, 0x98},  {0x25, 0xa8}, ENDMARKER},
	{{0x24, 0xa0},  {0x25, 0xb0}, ENDMARKER},
	{{0x24, 0xb8},  {0x25, 0xc8}, ENDMARKER},
};

static const struct regval_list bf2013_contrast_tab[][2] = {
	{{0x56, 0x10}, ENDMARKER},
	{{0x56, 0x20}, ENDMARKER},
	{{0x56, 0x30}, ENDMARKER},
	{{0x56, 0x40}, ENDMARKER},
	{{0x56, 0x50}, ENDMARKER},
	{{0x56, 0x60}, ENDMARKER},
	{{0x56, 0x70}, ENDMARKER},
};

static const struct regval_list bf2013_mode_tab[][3] = {
        {{0X8e,0X02}, {0X8f,0X64},ENDMARKER},// normal   min 25fps
	{{0X8e,0X02}, {0X8f,0Xfd},ENDMARKER},// night  // min 20 fps
};

static const struct regval_list bf2013_image_effect_tab[][7] = {
	{{0x56, 0x40},{0x80, 0x45},{0x76, 0x00},{0x69, 0x00},{0x67, 0x80}, {0x68 ,0x80}, ENDMARKER},//effect Normal
	{{0x56, 0x40},{0x80, 0x45},{0x76, 0x00},{0x69, 0x20},{0x67, 0x80}, {0x68 ,0x80}, ENDMARKER},//effect BLACKWHITE
	{{0x56, 0x40},{0x80, 0xc5},{0x76, 0xc0},{0x69, 0x00},{0x67, 0x60}, {0x68 ,0x80}, ENDMARKER},// effect RED
	{{0x56, 0x40},{0x80, 0x45},{0x76, 0x00},{0x69, 0x20},{0x67, 0x40}, {0x68 ,0x40}, ENDMARKER},// effect GREEN
	{{0x56, 0x40},{0x80, 0x45},{0x76, 0x00},{0x69, 0x20},{0x67, 0xa0}, {0x68 ,0x40}, ENDMARKER},// effect  BLUE
	{{0x56, 0x40},{0x80, 0x45},{0x76, 0x00},{0x69, 0x20},{0x67, 0x40}, {0x68 ,0x80}, ENDMARKER},//effect ANTIQUE
	{{0x56, 0x40},{0x80, 0x45},{0x76, 0x00},{0x69, 0x40},{0x67, 0x80}, {0x68 ,0x80}, ENDMARKER},// effect NEGATIVE
	{{0x56, 0x40},{0x80, 0x45},{0x76, 0x00},{0x69, 0x20},{0x67, 0x60}, {0x68 ,0x98}, ENDMARKER},// effect  sepia
};

static const struct regval_list bf2013_awb_tab[][6] = {
	 //AUTO:
	{
	    {0x13, 0x07},
	    ENDMARKER
	},
	//INCANDESCENCE:
	{
	    {0x13, 0x05},
	    {0x01, 0x1f},
	    {0x02, 0x15},
	    ENDMARKER
	},
	 //U30:
	{
	    {0x13, 0x05},
	    {0x01, 0x12},
	    {0x02, 0x19},
	    ENDMARKER
	    },
	//CWF:
	{
	    {0x13, 0x05},
	    {0x01, 0x1b},
	    {0x02, 0x19},
	    ENDMARKER
	},
	//FLUORESCENT:
	{
	    {0x13, 0x05},
	    {0x01, 0x1a},
	    {0x02, 0x1e},
	    ENDMARKER
	},
	//SUN:
	{
	    {0x13, 0x05},
	    {0x01, 0x13},
	    {0x02, 0x26},
	    ENDMARKER
	},
	//CLOUD
	{
	    {0x13, 0x05},
	    {0x01, 0x10},
	    {0x02, 0x28},
	    ENDMARKER
	},
};
static const struct regval_list bf2013_ev_tab[][2] = {
	{{0x55, 0xf8}, ENDMARKER},//-4
	{{0x55, 0xd8}, ENDMARKER},//-3
	{{0x55, 0xb8}, ENDMARKER},//-2
	{{0x55, 0x98}, ENDMARKER},//-1
	{{0x55, 0x00}, ENDMARKER},//0
	{{0x55, 0x18}, ENDMARKER},//+1
	{{0x55, 0x38}, ENDMARKER},//+2
	{{0x55, 0x58}, ENDMARKER},//+3
	{{0x55, 0x78}, ENDMARKER},//+4
};

static const struct regval_list bf2013_anti_flicker_mode_tab[][3] = {
        //50Hz:
	{{0x80,0x55},{0x9d,0x99},ENDMARKER},
	{{0x80,0x54},{0x9e,0x7f},ENDMARKER},
};

static const struct regval_list bf2013_video_mode_nor_tab[][7] = {
        // normal mode 14fps
	{{0x92,0x64},{0x93,0x00},{0x2a,0x00},{0x2b,0x00},{0x8e,0x02},{0x8f,0x64},ENDMARKER},
};

#define BF2013_SIZE(n, w, h, c) \
	{.name = n, .width = w , .height = h, .mbus_code = c }

static struct bf2013_win_size bf2013_supported_win_sizes[] = {
	BF2013_SIZE("VGA", W_VGA, H_VGA, MEDIA_BUS_FMT_YUYV8_2X8),
};

#define N_WIN_SIZES (ARRAY_SIZE(bf2013_supported_win_sizes))

/*
 * General functions
 */
static struct bf2013_priv *to_bf2013(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct bf2013_priv,
			    subdev);
}

static int bf2013_write_array(struct i2c_client *client,
			      const struct regval_list *vals)
{
	int ret;

	while ((vals->reg_num != 0xff) && (vals->value != 0xff)) {
		ret = bf2013_write_reg(client, vals->reg_num, vals->value);
		if (ret < 0){
			printk("err write reg:0x%02x,val:0x%02x\n",vals->reg_num,vals->value);
			return ret;
		}
		vals++;
	}
	return 0;
}

/* OF probe functions */
static int bf2013_hw_power(struct i2c_client *client, int on)
{
	printk("-----------> enter func %s\n",__func__);
	struct bf2013_priv *priv = to_bf2013(client);

	dev_dbg(&client->dev, "%s: %s the camera\n",
			__func__, on ? "ENABLE" : "DISABLE");

	/* thses gpio should be set according to the active level in dt defines */
	if(priv->pwr_gpio) {
		gpiod_direction_output(priv->pwr_gpio, on);
	}

	msleep(3);

	if (priv->pwdn_gpio) {
		gpiod_direction_output(priv->pwdn_gpio, !on);
	}

	msleep(20);
	return 0;
}

static int bf2013_reset(struct i2c_client *client)
{
	printk("-----------> enter func %s\n",__func__);
	struct bf2013_priv *priv = to_bf2013(client);

	if(priv->resetb_gpio) {
		gpiod_direction_output(priv->resetb_gpio, 1);
		msleep(20);
		gpiod_direction_output(priv->resetb_gpio, 0);
		msleep(20);

	}

	return 0;
}

static int bf2013_s_power(struct v4l2_subdev *sd, int on)
{
	printk("-----------> enter func %s\n",__func__);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct bf2013_priv *priv = to_bf2013(client);

	if(priv->power){
		return priv->power(client, on);
	} else {
		dev_err(&client->dev, "bf2013_s_power failde");
		return -EINVAL;
	}

}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int bf2013_g_register(struct v4l2_subdev *sd,
			     struct v4l2_dbg_register *reg)
{
	printk("-----------> enter func %s\n",__func__);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	reg->size = 1;
	if (reg->reg > 0xff)
		return -EINVAL;

	ret = bf2013_read_reg(client, reg->reg);
	if (ret < 0)
		return ret;

	reg->val = ret;

	return 0;
}

static int bf2013_s_register(struct v4l2_subdev *sd,
			     const struct v4l2_dbg_register *reg)
{
	printk("-----------> enter func %s\n",__func__);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (reg->reg > 0xff ||
	    reg->val > 0xff)
		return -EINVAL;

	return bf2013_write_reg(client, reg->reg, reg->val);
}
#endif

/*
 * soc_camera_ops functions
 */
static int bf2013_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client  *client = v4l2_get_subdevdata(sd);

	if (!enable ) {
		dev_info(&client->dev, "stream down\n");
		return 0;
	}

	dev_info(&client->dev, "stream on\n");

	return 0;
}

/* Select the nearest higher resolution for capture */
static const struct bf2013_win_size *bf2013_select_win(u32 code, u32 *width, u32 *height)
{
	printk("-----------> enter func %s\n",__func__);
	int i;

	for (i = 0; i < ARRAY_SIZE(bf2013_supported_win_sizes); i++) {
		if ((*width >= bf2013_supported_win_sizes[i].width) &&
		    (*height >= bf2013_supported_win_sizes[i].height) &&
		    (code == bf2013_supported_win_sizes[i].mbus_code)) {
			*width = bf2013_supported_win_sizes[i].width;
			*height = bf2013_supported_win_sizes[i].height;
			return &bf2013_supported_win_sizes[i];
		}
	}
	return NULL;
}

static int bf2013_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	printk("-----------> enter func %s\n",__func__);
	struct i2c_client  *client = v4l2_get_subdevdata(sd);
	struct bf2013_priv *priv = to_bf2013(client);

	if(!priv->win)
		return -EINVAL;

	sel->r.top = 0;
	sel->r.left = 0;
	sel->r.width = priv->win->width;
	sel->r.height = priv->win->height;

	return 0;
}

static int bf2013_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	printk("-----------> enter func %s\n",__func__);
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client  *client = v4l2_get_subdevdata(sd);
	struct bf2013_priv *priv = to_bf2013(client);

	if (format->pad)
		return -EINVAL;
	if(priv->win) {
		mf->width = priv->win->width;
		mf->height = priv->win->height;
	} else {
		mf->width = BF2013_DEFAULT_WIDTH;
		mf->height = BF2013_DEFAULT_HEIGHT;
	}
	mf->code = priv->cfmt_code;

	mf->colorspace = V4L2_COLORSPACE_JPEG;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

static int bf2013_set_params(struct i2c_client *client, u32 *width, u32 *height, u32 code)
{
	printk("-----------> enter func %s\n",__func__);
	struct bf2013_priv       *priv = to_bf2013(client);
	int ret;
	u8 value;

	/* select win */
	priv->win = bf2013_select_win(code, width, height);

	/* select format */
	priv->cfmt_code = 0;

	ret = bf2013_write_array(client, bf2013_init_regs);
	if (ret < 0)
		goto err;

	//set ae enable
	value = bf2013_read_reg(client,0x13);
	bf2013_write_reg(client ,0x13,value | 0x05);

	//set brightness
	ret = bf2013_write_array(client, bf2013_brightness_tab[1]);
	if (ret < 0)
		goto err;

	//set contrast 对比
	ret = bf2013_write_array(client, bf2013_contrast_tab[3]);
	if (ret < 0)
		goto err;

	//set work mode
	ret = bf2013_write_array(client,bf2013_mode_tab[0]);//normal
	if (ret < 0)
		goto err;

	//set image effect
	ret = bf2013_write_array(client,bf2013_image_effect_tab[0]); //normal
	if (ret < 0)
		goto err;

	//set awb mod
	ret = bf2013_write_array(client,bf2013_awb_tab[0]);
	if (ret < 0)
		goto err;

	//set ev mod
	ret = bf2013_write_array(client,bf2013_ev_tab[5]); //0
	if (ret < 0)
		goto err;

	//set anto ficker mod
	ret = bf2013_write_array(client,bf2013_anti_flicker_mode_tab[0]);
	if (ret < 0)
		goto err;

	//set video mod
	ret = bf2013_write_array(client,bf2013_video_mode_nor_tab[0]); // normal mod
	if (ret < 0)
		goto err;



	priv->cfmt_code = code;
	*width = priv->win->width;
	*height = priv->win->height;

	return 0;

err:
	printk("-----------------err err err\n");
	dev_err(&client->dev, "%s: Error %d", __func__, ret);
	bf2013_reset(client);
	priv->win = NULL;

	return ret;
}

static int bf2013_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	printk("-----------> enter func %s\n",__func__);
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct bf2013_priv *priv = to_bf2013(client);

	if (format->pad)
		return -EINVAL;

	/*
	 * select suitable win, but don't store it
	 */
	priv->win = bf2013_select_win(mf->code, &mf->width, &mf->height);
	if(!priv->win)
		return -EINVAL;

	mf->field	= V4L2_FIELD_NONE;
	mf->colorspace = V4L2_COLORSPACE_JPEG;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		bf2013_set_params(client, &mf->width, &mf->height, mf->code);
	if(cfg)
		cfg->try_fmt = *mf;
	return 0;
}

static int bf2013_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= ARRAY_SIZE(bf2013_supported_win_sizes))
		return -EINVAL;

	code->code = bf2013_supported_win_sizes[code->index].mbus_code;
	return 0;
}

static int bf2013_enum_frame_size(struct v4l2_subdev *sd,
		       struct v4l2_subdev_pad_config *cfg,
		       struct v4l2_subdev_frame_size_enum *fse)
{
	__u32 index = fse->index;

	if(index >= N_WIN_SIZES)
		return -EINVAL;

	fse->code = bf2013_supported_win_sizes[index].mbus_code;
	fse->min_width = bf2013_supported_win_sizes[index].width;
	fse->max_width = fse->min_width;
	fse->min_height = bf2013_supported_win_sizes[index].height;
	fse->max_height = fse->min_height;
	return 0;
}

static int bf2013_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
		struct v4l2_mbus_config *cfg)
{
	/* int dts*/
	return 0;
}

static struct v4l2_subdev_core_ops bf2013_subdev_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	= bf2013_g_register,
	.s_register	= bf2013_s_register,
#endif
	.s_power	= bf2013_s_power,
};

static struct v4l2_subdev_video_ops bf2013_subdev_video_ops = {
	.s_stream	= bf2013_s_stream,
};

static const struct v4l2_subdev_pad_ops bf2013_subdev_pad_ops = {
	.enum_mbus_code = bf2013_enum_mbus_code,
	.enum_frame_size = bf2013_enum_frame_size,
	.get_fmt	= bf2013_get_fmt,
	.set_fmt	= bf2013_set_fmt,
	.get_mbus_config = bf2013_g_mbus_config,
	.get_selection	= bf2013_get_selection,
};

static struct v4l2_subdev_ops bf2013_subdev_ops = {
	.core	= &bf2013_subdev_core_ops,
	.video	= &bf2013_subdev_video_ops,
	.pad	= &bf2013_subdev_pad_ops,
};

static int bf2013_video_probe(struct i2c_client *client)
{
	unsigned char pid = 0, ver = 0;
	int ret = 0;
	struct bf2013_priv *priv = to_bf2013(client);

	ret = bf2013_s_power(&priv->subdev, 1);
	if (ret < 0)
		return ret;

	bf2013_reset(client);
	/*
	 * check and show product ID and manufacturer ID
	 */

	pid = bf2013_read_reg(client, PID_BME_REG);
	if (pid != PID_VAL) {
		dev_err(&client->dev, "read sensor %s pid_val %x is error\n",
				client->name, pid);
		ret = -EINVAL;
		goto done;
	}


	ver = bf2013_read_reg(client, VER_BME_REG);
	if (ver != VER_VAL) {
		dev_err(&client->dev, "read sensor %s ver_val %x is error\n",
				client->name, ver);
		ret = -EINVAL;
		goto done;
	}

	dev_info(&client->dev, "read sensor %s id pid:0x%x,ver:%x successed!\n",
			client->name, pid, ver);

done:
	return ret;
}

static int bf2013_probe_dt(struct i2c_client *client,
		struct bf2013_priv *priv)
{
	struct device_node *np = client->dev.of_node;
	priv->resetb_gpio = devm_gpiod_get_optional(&client->dev, "resetb",
			GPIOD_OUT_LOW);
	if (!priv->resetb_gpio)
		dev_dbg(&client->dev, "resetb gpio is not assigned!\n");
	else if (IS_ERR(priv->resetb_gpio))
		return PTR_ERR(priv->resetb_gpio);

	/* Request the power down GPIO asserted */
	priv->pwdn_gpio = devm_gpiod_get_optional(&client->dev, "pwdn",
			GPIOD_OUT_LOW);
	if (!priv->pwdn_gpio)
		dev_dbg(&client->dev, "pwdn gpio is not assigned!\n");
	else if (IS_ERR(priv->pwdn_gpio))
		return PTR_ERR(priv->pwdn_gpio);

	/* Request the power down GPIO asserted */
	priv->pwr_gpio = devm_gpiod_get_optional(&client->dev, "pwr-en",
			GPIOD_OUT_HIGH);
	if (!priv->pwr_gpio)
		dev_dbg(&client->dev, "vcc_en gpio is not assigned!\n");
	else if (IS_ERR(priv->pwr_gpio))
		return PTR_ERR(priv->pwr_gpio);

	/* Initialize the soc_camera_subdev_desc */
	priv->power = bf2013_hw_power;

	return 0;
}

/*
 * i2c_driver functions
 */
static int bf2013_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct bf2013_priv	*priv;
	struct i2c_adapter	*adapter = to_i2c_adapter(client->dev.parent);
	int	ret;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&adapter->dev,
			"bf2013: I2C-Adapter doesn't support SMBUS\n");
		return -EIO;
	}

	priv = devm_kzalloc(&client->dev, sizeof(struct bf2013_priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&adapter->dev,
			"Failed to allocate memory for private data!\n");
		return -ENOMEM;
	}

	priv->mclk = v4l2_clk_get(&client->dev, "gate_cim_mclk");
	if (IS_ERR(priv->clk)){
		dev_err(&adapter->dev, "mclk_get err!\n");
		return -EPROBE_DEFER;
	}

	ret = v4l2_clk_enable(priv->mclk);
	if (ret){
		dev_err(&adapter->dev, "mclk_enable err!\n");
		return -EPROBE_DEFER;
	}

	priv->clk = v4l2_clk_get(&client->dev, "div_cim");
	if (IS_ERR(priv->clk))
		return -EPROBE_DEFER;

	v4l2_clk_set_rate(priv->clk, 24000000);

	ret = bf2013_probe_dt(client, priv);
	if (ret)
		goto err_probe_dt;

	v4l2_i2c_subdev_init(&priv->subdev, client, &bf2013_subdev_ops);

	ret = bf2013_video_probe(client);
	if (ret < 0)
		goto err_detect;

	ret = v4l2_async_register_subdev(&priv->subdev);
	if (ret < 0)
		goto err_async_register_subdev;

	dev_info(&adapter->dev, "bf2013 Probed\n");

	return 0;

err_async_register_subdev:
err_detect:
	v4l2_device_unregister_subdev(&priv->subdev);
err_probe_dt:
	v4l2_clk_put(priv->clk);
	v4l2_clk_put(priv->mclk);
	devm_kfree(&client->dev, priv);
	return ret;
}

static int bf2013_remove(struct i2c_client *client)
{
	struct bf2013_priv       *priv = to_bf2013(client);

	v4l2_async_unregister_subdev(&priv->subdev);
	v4l2_clk_put(priv->clk);
	v4l2_clk_put(priv->mclk);
	v4l2_device_unregister_subdev(&priv->subdev);
	devm_kfree(&client->dev, priv);
	return 0;
}

static const struct i2c_device_id bf2013_id[] = {
	{ "bf2013",  0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bf2013_id);
static const struct of_device_id bf2013_of_match[] = {
	{.compatible = "vga_cmos_bf2013", },
	{},
};
MODULE_DEVICE_TABLE(of, bf2013_of_match);
static struct i2c_driver bf2013_i2c_driver = {
	.driver = {
		.name = "vga_cmos_bf2013",
		.of_match_table = of_match_ptr(bf2013_of_match),
	},
	.probe    = bf2013_probe,
	.remove   = bf2013_remove,
	.id_table = bf2013_id,
};

module_i2c_driver(bf2013_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for smartsens BF2013 sensor");
MODULE_LICENSE("GPL v2");
