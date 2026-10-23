/*
 * driver/video/fbdev/ingenic/x2000_v12/displays/panel-hj070.c
 * Copyright (C) 2016 Ingenic Semiconductor Inc.
 * This program is free software, you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/lcd.h>
#include <linux/of_gpio.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <soc/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>

#include "../ingenicfb.h"
#include "../jz_dsim.h"



static char panel_hj070_debug = 1;
#define PANEL_DEBUG_MSG(msg...)			\
	do {					\
		if (panel_hj070_debug)		\
			printk(">>>>>>>>>>>>>>>>PANEL hj070: " msg);		\
	} while(0)

struct panel_dev {
	/* ingenic frame buffer */
	struct device *dev;
	struct lcd_panel *panel;

	/* common lcd framework */
	struct lcd_device *lcd;
	int power;

	struct gpio_desc *reset_gpio;
	struct gpio_desc *stbyb_gpio;
	struct gpio_desc *power_en_gpio;

};

struct panel_dev *panel;

struct hj070 {
	struct device *dev;
	unsigned int power;
	unsigned int id;

	struct lcd_device *ld;
};

static struct fb_videomode panel_modes = {
	.name = "lvds-hj070-lcd",
	.refresh = 60,
	.xres = 1024,
	.yres = 600,
	.pixclock = 51000,//KHz
	.left_margin = 160,	//hbp
	.right_margin = 160,	//hfp
	.hsync_len = 24,	//hs

	.upper_margin = 23,	//vbp
	.lower_margin = 12,	//vfp
	.vsync_len = 2,		//vs
	.vmode = FB_VMODE_INTERLACED,
	.flag = 0,

};

struct jzdsi_data jzdsi_pdata = {
	.modes = &panel_modes,
	.video_config.no_of_lanes = 4,
	.video_config.pixel_clock = 51000,	// must set this clk same as pixclock
	.video_config.virtual_channel = 0,
	.video_config.color_coding = COLOR_CODE_24BIT,

	.video_config.video_mode = VIDEO_BURST_WITH_SYNC_PULSES,

	.video_config.receive_ack_packets = 0,	/* enable receiving of ack packets */
	.video_config.is_18_loosely = 0,

	.video_config.h_polarity = 0,
	.video_config.v_polarity = 0,
	.video_config.data_en_polarity = 0,

	.video_config.byte_clock = 0, // driver will auto calculate byte_clock.
	.dsi_config.max_hs_to_lp_cycles = 100,
	.dsi_config.max_lp_to_hs_cycles = 40,
	.dsi_config.max_bta_cycles = 4095,
	.dsi_config.color_mode_polarity = 1,
	.dsi_config.shut_down_polarity = 1,
	.dsi_config.max_bps = 2750,
	.bpp_info = 24,

};

static struct tft_config hj070_cfg = {
	.pix_clk_inv = 0,
	.de_dl = 0,
	.sync_dl = 0,
	.vsync_dl = 0,
	.color_even = TFT_LCD_COLOR_EVEN_RGB,
	.color_odd = TFT_LCD_COLOR_ODD_RGB,
	.mode = TFT_LCD_MODE_PARALLEL_888,
};

struct lcd_panel lcd_panel = {
	.num_modes = 1,
	.modes = &panel_modes,
	.dsi_pdata = &jzdsi_pdata,

	.lcd_type = LCD_TYPE_LVDS_VESA,
	.tft_config = &hj070_cfg,
	.bpp = 24,
	.dither_enable = 0,
	.dither.dither_red = 0,
	.dither.dither_green = 0,
	.dither.dither_blue = 0,
};

void hj070_reset(struct panel_dev *hj070_info)
{
	gpiod_set_value(hj070_info->reset_gpio, 0);
	mdelay(10);
	gpiod_set_value(hj070_info->reset_gpio, 1);
	mdelay(10);
	gpiod_set_value(hj070_info->reset_gpio, 0);
	mdelay(10);
}

#define POWER_IS_ON(pwr)        ((pwr) <= FB_BLANK_NORMAL)
static int panel_set_power(struct lcd_device *lcd, int power)
{
	struct panel_dev *hj070_info = lcd_get_data(lcd);

	if(POWER_IS_ON(power) && !POWER_IS_ON(hj070_info->power)) {
		gpiod_set_value(hj070_info->power_en_gpio, 1);
		mdelay(15);
		hj070_reset(hj070_info);
		mdelay(500);
	}
	hj070_info->power = power;
	return 0;
}

static int panel_get_power(struct lcd_device *lcd)
{
	struct panel_dev *hj070_info = lcd_get_data(lcd);

	return hj070_info->power;
}

/*
* @ pannel_hj070_lcd_ops, register to kernel common backlight/lcd.c framworks.
*/
static struct lcd_ops panel_lcd_ops = {
	.set_power = panel_set_power,
	.get_power = panel_get_power,
};

static int of_panel_parse(struct device *dev)
{
	struct panel_dev *hj070_info = dev_get_drvdata(dev);
	int ret = 0;

	hj070_info->reset_gpio = devm_gpiod_get(dev, "hj070,reset", GPIOD_OUT_HIGH);
	if (IS_ERR(hj070_info->reset_gpio)) {
		dev_err(dev, "Failed to get reset-gpio\n");
		return PTR_ERR(hj070_info->reset_gpio);
	}

	hj070_info->stbyb_gpio = devm_gpiod_get(dev, "hj070,stbyb", GPIOD_OUT_HIGH);
	if (IS_ERR(hj070_info->stbyb_gpio)) {
		dev_err(dev, "Failed to get stbyb-gpio\n");
		return PTR_ERR(hj070_info->stbyb_gpio);
	}

	hj070_info->power_en_gpio = devm_gpiod_get(dev, "hj070,power_en", GPIOD_OUT_HIGH);
	if (IS_ERR(hj070_info->power_en_gpio)) {
		dev_err(dev, "Failed to get power_en-gpio\n");
		ret = PTR_ERR(hj070_info->power_en_gpio);
		return ret;
	}

	return 0;
}

static int panel_suspend(struct mipi_dsim_lcd_device *dsim_dev)
{
	return 0;
}

static int panel_resume(struct mipi_dsim_lcd_device *dsim_dev)
{
	return 0;
}


/*
* @panel_probe

* 	1. Register to ingenicfb.
* 	2. Register to lcd.
* 	3. Register to backlight if possible.

* @pdev

* @Return -
*/
static int panel_probe(struct platform_device *pdev)
{
	int ret = 0;
	panel = kzalloc(sizeof(struct panel_dev), GFP_KERNEL);
	if(panel == NULL) {
		dev_err(&pdev->dev, "Faile to alloc memory!");
		return -ENOMEM;
	}
	panel->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, panel);

	ret = of_panel_parse(&pdev->dev);
	if(ret < 0) {
		goto err_of_parse;
	}

	panel->lcd = lcd_device_register("panel_lcd", panel->dev, panel, &panel_lcd_ops);
	if(IS_ERR_OR_NULL(panel->lcd)) {
		dev_err(panel->dev, "Error register lcd!\n");
		ret = -EINVAL;
		goto err_of_parse;
	}
	ret = ingenicfb_register_panel(&lcd_panel);
	if(ret < 0) {
		dev_err(&pdev->dev, "Failed to register lcd panel!\n");
		goto err_of_parse;
	}

	panel->power = FB_BLANK_POWERDOWN;
	if (panel_set_power(panel->lcd, FB_BLANK_UNBLANK))
		goto err_lcd_register;

	return 0;

err_lcd_register:
	lcd_device_unregister(panel->lcd);
err_of_parse:

	kfree(panel);
	return ret;
}

static int panel_remove(struct platform_device *pdev)
{
	struct panel_dev *hj070_info = dev_get_drvdata(&pdev->dev);
	panel_set_power(hj070_info->lcd, FB_BLANK_POWERDOWN);
	lcd_device_unregister(hj070_info->lcd);
	kfree(hj070_info);

	return 0;
}

static const struct of_device_id panel_of_match[] = {
	{ .compatible = "lvds,hj070", },
	{},
};

static struct platform_driver panel_driver = {
	.probe		= panel_probe,
	.remove		= panel_remove,
	.driver		= {
		.name	= "hj070",
		.of_match_table = panel_of_match,
	},
};

module_platform_driver(panel_driver);

MODULE_LICENSE("GPL");
