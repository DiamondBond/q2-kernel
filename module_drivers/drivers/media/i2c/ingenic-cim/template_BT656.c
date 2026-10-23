/*
 * BT656 Camera Driver
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

#include <linux/clk.h>
#include <linux/regulator/consumer.h>


#define  BT656_DEFAULT_WIDTH    720
#define  BT656_DEFAULT_HEIGHT   480


/*
 * Struct
 */
struct BT656_win_size {
	char *name;
	int width;
	int height;
	unsigned int mbus_code;
};

struct BT656_priv {
	struct v4l2_subdev		subdev;
	struct v4l2_ctrl_handler	hdl;
	u32	cfmt_code;
	struct v4l2_clk			*clk;
	struct v4l2_clk			*mclk;
	const struct BT656_win_size	*win;

	struct v4l2_subdev_platform_data *sd_pdata;

	struct gpio_desc *resetb_gpio;
	struct gpio_desc *vcc_en_gpio;
};

unsigned short BT656_read_reg(struct i2c_client *client, unsigned char reg, unsigned char *value)
{
	struct i2c_msg msg[2] = {
		[0] = {
			.addr   = client->addr,
			.flags  = 0,
			.len    = 1,
			.buf    = &reg,
		},
		[1] = {
			.addr   = client->addr,
			.flags  = I2C_M_RD,
			.len    = 1,
			.buf    = value,
		}
	};
	int ret;
	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret > 0)
		ret = 0;

	return ret;
}

int BT656_write_reg(struct i2c_client *client, unsigned char reg, unsigned short value)
{
	unsigned char buf[2] = {reg, value};
	struct i2c_msg msg = {
		.addr   = client->addr,
		.flags  = 0,
		.len    = 2,
		.buf    = buf,
	};
	int ret;
	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret > 0)
		ret = 0;

	return ret;

}

static struct BT656_win_size BT656_supported_win_sizes[] = {
	{
		.name = "bt656",
		.width = 720,
		.height = 480,
		.mbus_code = MEDIA_BUS_FMT_UYVY8_2X8,
	},
};

#define N_WIN_SIZES (ARRAY_SIZE(BT656_supported_win_sizes))

/*
 * General functions
 */
static struct BT656_priv *to_BT656(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct BT656_priv,
			subdev);
}

static int BT656_reset(struct i2c_client *client)
{
	struct BT656_priv *priv = to_BT656(client);

	if(priv->vcc_en_gpio) {
		gpiod_direction_output(priv->vcc_en_gpio, 1);
	}

	return 0;
}

static int BT656_reset(struct i2c_client *client)
{
	struct BT656_priv *priv = to_BT656(client);

	if(priv->resetb_gpio) {
		gpiod_direction_output(priv->resetb_gpio, 0);
		msleep(10);
		gpiod_direction_output(priv->resetb_gpio, 1);
		msleep(10);
		gpiod_direction_output(priv->resetb_gpio, 0);
		msleep(10);
	}

	return 0;
}

/*video_ops*/
static int BT656_s_stream(struct v4l2_subdev *sd, int enable)
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
static const struct BT656_win_size *BT656_select_win(u32 *code, u32 *width, u32 *height)
{
	int i, default_size = ARRAY_SIZE(BT656_supported_win_sizes) - 1;

	for (i = 0; i < ARRAY_SIZE(BT656_supported_win_sizes); i++) {
		if ((*width >= BT656_supported_win_sizes[i].width) &&
				(*height >= BT656_supported_win_sizes[i].height) &&
				(*code == BT656_supported_win_sizes[i].mbus_code)) {
			*width = BT656_supported_win_sizes[i].width;
			*height = BT656_supported_win_sizes[i].height;
			return &BT656_supported_win_sizes[i];
		}
	}

	return NULL;
}

static int BT656_set_params(struct i2c_client *client, u32 *width, u32 *height, u32 code)
{
	struct BT656_priv       *priv = to_BT656(client);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	int ret;

	/* initialize the sensor with default data */
	dev_dbg(&client->dev, "%s: Init default", __func__);

	/* set size win */
	dev_dbg(&client->dev, "priv->win.width = %d\n", priv->win->width);
	dev_dbg(&client->dev, "priv->win.height = %d\n", priv->win->height);


	priv->cfmt_code = code;
	*width = priv->win->width;
	*height = priv->win->height;

	return 0;

err:
	dev_err(&client->dev, "%s: Error %d", __func__, ret);
	priv->win = NULL;

	return ret;
}

/*pad_ops*/
static int BT656_get_selection(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_selection *sel)
{
	struct i2c_client  *client = v4l2_get_subdevdata(sd);
	struct BT656_priv *priv = to_BT656(client);

	if(!priv->win)
		return -EINVAL;

	sel->r.top = 0;
	sel->r.left = 0;
	sel->r.width = priv->win->width;
	sel->r.height = priv->win->height;

	return 0;
}

static int BT656_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client  *client = v4l2_get_subdevdata(sd);
	struct BT656_priv *priv = to_BT656(client);

	if (format->pad)
		return -EINVAL;

	if(priv->win){
		mf->width = priv->win->width;
		mf->height = priv->win->height;
	} else {
		mf->width = BT656_DEFAULT_WIDTH;
		mf->height = BT656_DEFAULT_HEIGHT;
	}
	mf->code = priv->cfmt_code;

	mf->colorspace = V4L2_COLORSPACE_JPEG;
	mf->field	= V4L2_FIELD_NONE;//Image 逐行输出

	return 0;
}

static int BT656_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct BT656_priv       *priv = to_BT656(client);

	if (format->pad)
		return -EINVAL;

	priv->win = BT656_select_win(&mf->code, &mf->width, &mf->height);
	if(!priv->win)
		return -EINVAL;

	//	mf->field	= V4L2_FIELD_NONE;//Image 逐行输出
	mf->field       = V4L2_FIELD_INTERLACED;// Image 隔行
	mf->colorspace  = V4L2_COLORSPACE_JPEG;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		BT656_set_params(client, &mf->width,
				&mf->height, mf->code);
	if(cfg)
		cfg->try_fmt = *mf;
	return 0;
}

static int BT656_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= ARRAY_SIZE(BT656_supported_win_sizes))
		return -EINVAL;

	code->code = BT656_supported_win_sizes[code->index].mbus_code;
	return 0;
}

static int BT656_enum_frame_size(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_frame_size_enum *fse)
{
	int i, j;
	int num_valid = -1;
	__u32 index = fse->index;

	if(index >= N_WIN_SIZES)
		return -EINVAL;

	fse->code = BT656_supported_win_sizes[index].mbus_code;
	fse->min_width = BT656_supported_win_sizes[index].width;
	fse->max_width = fse->min_width;
	fse->min_height = BT656_supported_win_sizes[index].height;
	fse->max_height = fse->min_height;
	return 0;

}

static int BT656_detect(struct i2c_client *client, unsigned int *ident)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	unsigned char v;
	int ret;

	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	return 0;

}

static struct v4l2_subdev_video_ops BT656_subdev_video_ops = {
	.s_stream	= BT656_s_stream,
};

static const struct v4l2_subdev_pad_ops BT656_subdev_pad_ops = {
	.enum_mbus_code = BT656_enum_mbus_code,
	.enum_frame_size = BT656_enum_frame_size,
	.get_fmt	= BT656_get_fmt,
	.set_fmt	= BT656_set_fmt,
	.get_selection	= BT656_get_selection,
};

static struct v4l2_subdev_ops BT656_subdev_ops = {
	.video	= &BT656_subdev_video_ops,
	.pad	= &BT656_subdev_pad_ops,
};

static int BT656_probe(struct i2c_client *client, const struct i2c_device_id *did)
{
	struct BT656_priv	*priv;
	struct i2c_adapter	*adapter = to_i2c_adapter(client->dev.parent);
	int	ret;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&adapter->dev,
				"BT656: I2C-Adapter doesn't support SMBUS\n");
		return -EIO;
	}

	priv = devm_kzalloc(&client->dev, sizeof(struct BT656_priv), GFP_KERNEL);
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


	/* Request the power vcc-en GPIO asserted */
	priv->vcc_en_gpio = devm_gpiod_get_optional(&client->dev, "vcc-en",
			GPIOD_OUT_HIGH);
	if (!priv->vcc_en_gpio)
		dev_dbg(&client->dev, "vcc_en gpio is not assigned!\n");
	else if (IS_ERR(priv->vcc_en_gpio))
		return PTR_ERR(priv->vcc_en_gpio);

	/* Request the power resetb GPIO asserted */
	priv->resetb_gpio = devm_gpiod_get_optional(&client->dev, "resetb",
			GPIOD_OUT_LOW);
	if (!priv->resetb_gpio)
		dev_dbg(&client->dev, "vcc_en gpio is not assigned!\n");
	else if (IS_ERR(priv->resetb_gpio))
		return PTR_ERR(priv->resetb_gpio);


	if(priv->vcc_en_gpio) {
		gpiod_direction_output(priv->vcc_en_gpio, 1);
	}

	msleep(10);

	if(priv->resetb_gpio) {
		gpiod_direction_output(priv->resetb_gpio, 0);
		msleep(10);
		gpiod_direction_output(priv->resetb_gpio, 1);
		msleep(10);
		gpiod_direction_output(priv->resetb_gpio, 0);
		msleep(10);
	}


	v4l2_i2c_subdev_init(&priv->subdev, client, &BT656_subdev_ops);

#if 0
	/* add handler */
	v4l2_ctrl_handler_init(&priv->hdl, 2);
	priv->subdev.ctrl_handler = &priv->hdl;
	if (priv->hdl.error) {
		ret = priv->hdl.error;
		goto err_hdl;
	}
	ret = v4l2_ctrl_handler_setup(&priv->hdl);
#endif

#if 1
	ret = BT656_detect(client, NULL);
	if(ret < 0){
		v4l_err(client,"chip found @ 0x%x (%s) is not an BT656 chip.\n",
				client->addr << 1, client->adapter->name);
		goto err_detect;
	}
#endif

	ret = v4l2_async_register_subdev(&priv->subdev);
	if (ret < 0)
		goto err_async_register_subdev;

	ret = BT656_write_reg(client, 0x03, 0x0d);

	dev_info(&adapter->dev, "BT656 probed\n");
	return 0;

err_async_register_subdev:
err_detect:
	v4l2_ctrl_handler_free(&priv->hdl);
err_hdl:
	v4l2_device_unregister_subdev(&priv->subdev);
err_probe_dt:
	v4l2_clk_put(priv->clk);
	v4l2_clk_put(priv->mclk);
	devm_kfree(&client->dev, priv);
	return ret;
}

static int BT656_remove(struct i2c_client *client)
{
	struct BT656_priv       *priv = to_BT656(client);

	v4l2_async_unregister_subdev(&priv->subdev);
	v4l2_ctrl_handler_free(&priv->hdl);
	v4l2_device_unregister_subdev(&priv->subdev);
	v4l2_clk_put(priv->clk);
	devm_kfree(&client->dev, priv);
	return 0;
}

static const struct i2c_device_id BT656_id[] = {
	{ "template-BT656",  0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, BT656_id);
static const struct of_device_id BT656_of_match[] = {
	{.compatible = "BT656,template", },
	{},
};
MODULE_DEVICE_TABLE(of, BT656_of_match);
static struct i2c_driver BT656_i2c_driver = {
	.driver = {
		.name = "template-BT656",
		.of_match_table = of_match_ptr(BT656_of_match),
	},
	.probe    = BT656_probe,
	.remove   = BT656_remove,
	.id_table = BT656_id,
};
module_i2c_driver(BT656_i2c_driver);

MODULE_DESCRIPTION("Camera driver for BT656 sensor");
MODULE_LICENSE("GPL v2");
