/*
 * driver/video/fbdev/ingenic/x2000_v12/displays/panel-s231aj1.c
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

#include "../ingenicfb.h"
#include "../jz_dsim.h"



static char panel_s231aj1_debug = 1;
#define PANEL_DEBUG_MSG(msg...)			\
	do {					\
		if (panel_s231aj1_debug)		\
			printk(">>>>>>>>>>>>>>>>PANEL s231aj1: " msg);		\
	} while(0)

struct board_gpio {
	short gpio;
	short active_level;
};

struct panel_dev {
	/* ingenic frame buffer */
	struct device *dev;
	struct lcd_panel *panel;

	/* common lcd framework */
	struct lcd_device *lcd;
	struct backlight_device *backlight;
	int power;

	struct regulator *vcc;
	// struct board_gpio vdd_en;
	struct regulator *vdd_en;
	struct board_gpio rst;
	struct board_gpio lcd_te;
	struct board_gpio lcd_pwm;

	struct mipi_dsim_lcd_device *dsim_dev;
};

struct panel_dev *panel;

#define lcd_to_master(a)	(a->dsim_dev->master)
#define lcd_to_master_ops(a)	((lcd_to_master(a))->master_ops)

struct s231aj1 {
	struct device *dev;
	unsigned int power;
	unsigned int id;

	struct lcd_device *ld;
	struct backlight_device *bd;

	struct mipi_dsim_lcd_device *dsim_dev;

};

static void lcd_power_on(struct lcd_panel *ppanel)
{
	struct regulator *vdd_en = panel->vdd_en;
	regulator_enable(vdd_en);
}

static void lcd_power_off(struct lcd_panel *ppanel)
{
	struct regulator *vdd_en = panel->vdd_en;
	regulator_disable(vdd_en);
}

static struct lcd_panel_ops panel_ops = {
	.enable = (void*)lcd_power_on,
	.disable = (void*)lcd_power_off,
};

static struct fb_videomode panel_modes = {
	.name = "fitipower_s231aj1-lcd",
	//.refresh = 60,
	.xres = 256,
	.yres = 1920,
	.pixclock = (78000),//KHz
	.left_margin = 210,	// hbp
	.right_margin = 210,	//hfp
	.hsync_len = 19,		//hs

	.upper_margin = 45,	//vbp
	.lower_margin = 45,	//vfp
	.vsync_len = 10,		//vs
	.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.vmode = FB_VMODE_INTERLACED,
	.flag = 0,

};

struct jzdsi_data jzdsi_pdata = {
	.modes = &panel_modes,
	.video_config.pixel_clock = 78000,	// must set this clk same as pixclock
	.video_config.virtual_channel = 0,
	.video_config.color_coding = COLOR_CODE_24BIT,

	.video_config.video_mode = VIDEO_BURST_WITH_SYNC_PULSES,
	//.video_config.video_mode = VIDEO_NON_BURST_WITH_SYNC_PULSES,

	.video_config.receive_ack_packets = 0,	/* enable receiving of ack packets */
	.video_config.is_18_loosely = 0,

	.video_config.h_polarity = 0,
	.video_config.v_polarity = 0,
	.video_config.data_en_polarity = 0,

	.video_config.byte_clock = 0, // driver will auto calculate byte_clock.
	.video_config.byte_clock_coef = MIPI_PHY_BYTE_CLK_COEF_MUL6_DIV5,
	.dsi_config.max_lanes = 4,
	.dsi_config.max_hs_to_lp_cycles = 100,
	.dsi_config.max_lp_to_hs_cycles = 40,
	.dsi_config.max_bta_cycles = 4095,
	.dsi_config.color_mode_polarity = 1,
	.dsi_config.shut_down_polarity = 1,
	.dsi_config.max_bps = 2750,
	.bpp_info = 24,

};

static struct tft_config s231aj1_cfg = {
	.pix_clk_inv = 0,
	.de_dl = 0,
	.sync_dl = 0,
	.vsync_dl = 0,
	.color_even = TFT_LCD_COLOR_EVEN_RGB,
	.color_odd = TFT_LCD_COLOR_ODD_RGB,
	//.mode = TFT_LCD_MODE_PARALLEL_666,
	.mode = TFT_LCD_MODE_PARALLEL_888,
};

struct lcd_panel lcd_panel = {
	.num_modes = 1,
	.modes = &panel_modes,
	.dsi_pdata = &jzdsi_pdata,

	.lcd_type = LCD_TYPE_LVDS_VESA,
	.tft_config = &s231aj1_cfg,
	.bpp = 24,
	.width = 256,
	.height = 1920,
	.dither_enable = 0,
	.dither.dither_red = 0,
	.dither.dither_green = 0,
	.dither.dither_blue = 0,
	.ops = &panel_ops,
};

#define POWER_IS_ON(pwr)        ((pwr) <= FB_BLANK_NORMAL)
static int panel_set_power(struct lcd_device *lcd, int power)
{
        return 0;
}

static int panel_get_power(struct lcd_device *lcd)
{
	struct panel_dev *panel = lcd_get_data(lcd);

	return panel->power;
}

/*
* @ pannel_s231aj1_lcd_ops, register to kernel common backlight/lcd.c framworks.
*/
static struct lcd_ops panel_lcd_ops = {
	.set_power = panel_set_power,
	.get_power = panel_get_power,
};

static int of_panel_parse(struct device *dev)
{
	struct panel_dev *panel = dev_get_drvdata(dev);
	struct device_node *np = dev->of_node;
	enum of_gpio_flags flags;
	int ret = 0;
	PANEL_DEBUG_MSG("enter %s %d \n",__func__, __LINE__);
	panel->vdd_en = devm_regulator_get_optional(dev, "lcd_vdd_en");

	if (IS_ERR(panel->vdd_en)) {
		if (PTR_ERR(panel->vdd_en) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		PANEL_DEBUG_MSG("No vdd_en regulator found\n");
	}


	return 0;
}

static int panel_dev_probe(struct mipi_dsim_lcd_device *dsim_dev)
{
	struct s231aj1 *lcd;
	char buffer[4];
	int ret;

	lcd = devm_kzalloc(&dsim_dev->dev, sizeof(struct s231aj1), GFP_KERNEL);
	if (!lcd)
	{
		dev_err(&dsim_dev->dev, "failed to allocate fitipower_s231aj1 structure.\n");
		return -ENOMEM;
	}

	lcd->dsim_dev = dsim_dev;
	lcd->dev = &dsim_dev->dev;

	lcd->ld = lcd_device_register("fitipower_s231aj1", lcd->dev, lcd,
	                              &panel_lcd_ops);
	if (IS_ERR(lcd->ld))
	{
		dev_err(lcd->dev, "failed to register lcd ops.\n");
		return PTR_ERR(lcd->ld);
	}

	dev_set_drvdata(&dsim_dev->dev, lcd);


	dev_dbg(lcd->dev, "probed fitipower_s231aj1 panel driver.\n");


	panel->dsim_dev = dsim_dev;


	return 0;

}

static int panel_suspend(struct mipi_dsim_lcd_device *dsim_dev)
{
	struct regulator *vdd_en = panel->vdd_en;

	regulator_disable(vdd_en);
	return 0;
}

static int panel_resume(struct mipi_dsim_lcd_device *dsim_dev)
{
	struct regulator *vdd_en = panel->vdd_en;

	regulator_enable(vdd_en);
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
	PANEL_DEBUG_MSG("enter %s %d \n",__func__, __LINE__);

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


	ret = ingenicfb_register_panel(&lcd_panel);
	if(ret < 0) {
		dev_err(&pdev->dev, "Failed to register lcd panel!\n");
		goto err_of_parse;
	}

	return 0;

err_of_parse:

	kfree(panel);
	return ret;
}

static int panel_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id panel_of_match[] = {
	{ .compatible = "ingenic,s231aj1", },
	{},
};

static struct platform_driver panel_driver = {
	.probe		= panel_probe,
	.remove		= panel_remove,
	.driver		= {
		.name	= "s231aj1",
		.of_match_table = panel_of_match,
	},
};

module_platform_driver(panel_driver);

MODULE_LICENSE("GPL");
