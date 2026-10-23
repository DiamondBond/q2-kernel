/*
 * driver/video/fbdev/ingenic/fb_stage/displays/panel-st7789p3.c
 *
 * Copyright (C) 2016 Ingenic Semiconductor Inc.
 *
 * Author:zhxiao<zhihao.xiao@ingenic.com>
 *
 * This program is free software, you can redistribute it and/or modify it
 *
 * under the terms of the GNU General Public License version 2 as published by
 *
 * the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/pwm_backlight.h>
#include <linux/delay.h>
#include <linux/lcd.h>
#include <linux/of_gpio.h>

#include "../ingenicfb.h"

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
	int power;

	struct board_gpio cs;
	struct board_gpio rst;
};

static struct smart_lcd_data_table st7789p3_data_table[] = {

    /* LCD init code */
	{SMART_CONFIG_UDELAY, 120000},
	{SMART_CONFIG_CMD, 0x11},
	{SMART_CONFIG_UDELAY, 120000},

	{SMART_CONFIG_CMD, 0xB2},
	{SMART_CONFIG_PRM, 0x0C},
	{SMART_CONFIG_PRM, 0x0C},
	{SMART_CONFIG_PRM, 0x00},
	{SMART_CONFIG_PRM, 0x33},
	{SMART_CONFIG_PRM, 0x33},

	{SMART_CONFIG_CMD, 0x35},
	{SMART_CONFIG_PRM, 0x00},

	{SMART_CONFIG_CMD, 0xB0},
	{SMART_CONFIG_PRM, 0x40},
	{SMART_CONFIG_PRM, 0x10},
	{SMART_CONFIG_PRM, 0x12},

	{SMART_CONFIG_CMD, 0xB1},
	{SMART_CONFIG_PRM, 0x11},
	{SMART_CONFIG_PRM, 0xF4},

	{SMART_CONFIG_CMD, 0x36},
	{SMART_CONFIG_PRM, 0x00},

	{SMART_CONFIG_CMD, 0x3A},
	{SMART_CONFIG_PRM, 0x05},

	{SMART_CONFIG_CMD, 0xB7},
	{SMART_CONFIG_PRM, 0x06},

	{SMART_CONFIG_CMD, 0xBB},
	{SMART_CONFIG_PRM, 0x20},

	{SMART_CONFIG_CMD, 0xC0},
	{SMART_CONFIG_PRM, 0x2C},

	{SMART_CONFIG_CMD, 0xC2},
	{SMART_CONFIG_PRM, 0x01},

	{SMART_CONFIG_CMD, 0xC3},
	{SMART_CONFIG_PRM, 0x08},

	{SMART_CONFIG_CMD, 0xC6},
	{SMART_CONFIG_PRM, 0x0F},

	{SMART_CONFIG_CMD, 0xD0},
	{SMART_CONFIG_PRM, 0xA7},

	{SMART_CONFIG_CMD, 0xD0},
	{SMART_CONFIG_PRM, 0xA4},
	{SMART_CONFIG_PRM, 0xA1},

	{SMART_CONFIG_CMD, 0xD6},
	{SMART_CONFIG_PRM, 0xA1},

	{SMART_CONFIG_CMD, 0xE0},
	{SMART_CONFIG_PRM, 0xF0},
	{SMART_CONFIG_PRM, 0x06},
	{SMART_CONFIG_PRM, 0x0E},
	{SMART_CONFIG_PRM, 0x08},
	{SMART_CONFIG_PRM, 0x09},
	{SMART_CONFIG_PRM, 0x17},
	{SMART_CONFIG_PRM, 0x37},
	{SMART_CONFIG_PRM, 0x54},
	{SMART_CONFIG_PRM, 0x4C},
	{SMART_CONFIG_PRM, 0x39},
	{SMART_CONFIG_PRM, 0x14},
	{SMART_CONFIG_PRM, 0x12},
	{SMART_CONFIG_PRM, 0x2F},
	{SMART_CONFIG_PRM, 0x34},

	{SMART_CONFIG_CMD, 0xE1},
	{SMART_CONFIG_PRM, 0xF0},
	{SMART_CONFIG_PRM, 0x0F},
	{SMART_CONFIG_PRM, 0x14},
	{SMART_CONFIG_PRM, 0x0C},
	{SMART_CONFIG_PRM, 0x0A},
	{SMART_CONFIG_PRM, 0x04},
	{SMART_CONFIG_PRM, 0x37},
	{SMART_CONFIG_PRM, 0x43},
	{SMART_CONFIG_PRM, 0x4C},
	{SMART_CONFIG_PRM, 0x36},
	{SMART_CONFIG_PRM, 0x12},
	{SMART_CONFIG_PRM, 0x14},
	{SMART_CONFIG_PRM, 0x2D},
	{SMART_CONFIG_PRM, 0x36},

	{SMART_CONFIG_CMD, 0x21},

	{SMART_CONFIG_CMD, 0x29},

	{SMART_CONFIG_CMD, 0x2C},

	{SMART_CONFIG_UDELAY, 120000},

};


static struct fb_videomode panel_modes[] = {
	[0] = {
		.name = "240x320",
		.refresh = 60,
		.xres = 240,
		.yres = 320,
		.pixclock = KHZ2PICOS(30000),
		.left_margin = 0,
		.right_margin = 0,
		.upper_margin = 0,
		.lower_margin = 0,
		.hsync_len = 0,
		.vsync_len = 0,
		.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		.vmode = FB_VMODE_NONINTERLACED,
		.flag = 0,
	},
};

struct smart_config st7789p3_cfg = {
	.te_anti_jit = 1,
	.te_md = 1,
	.te_dp = 1,
	.te_switch = 0,
	.dc_md = 0,
	.wr_md = 1,
	.smart_type = SMART_LCD_TYPE_SPI_4,
	.pix_fmt = SMART_LCD_FORMAT_565,

	.write_gram_cmd = 0x22222222,
	.data_table = st7789p3_data_table,
	.length_data_table = ARRAY_SIZE(st7789p3_data_table),
};

struct lcd_panel lcd_panel = {
	.name = "st7789p3",
	.num_modes = ARRAY_SIZE(panel_modes),
	.modes = panel_modes,
	.lcd_type = LCD_TYPE_SLCD,
	.width = 240,
	.height = 320,

	.smart_config = &st7789p3_cfg,
};

static void panel_power_reset(struct board_gpio *rst)
{
	gpio_direction_output(rst->gpio, 0);
	mdelay(20);
	gpio_direction_output(rst->gpio, 1);
	mdelay(10);
}

#define POWER_IS_ON(pwr)        ((pwr) <= FB_BLANK_NORMAL)
static int panel_set_power(struct lcd_device *lcd, int power)
{
	struct panel_dev *panel = lcd_get_data(lcd);
	struct board_gpio *cs = &panel->cs;
	struct board_gpio *rst = &panel->rst;

	if(POWER_IS_ON(power) && !POWER_IS_ON(panel->power)) {
		gpio_direction_output(rst->gpio, 1);
		gpio_direction_output(cs->gpio, 1);
		mdelay(5);
		panel_power_reset(rst);
		gpio_direction_output(cs->gpio, 0);
	}
	if(!POWER_IS_ON(power) && POWER_IS_ON(panel->power)) {
		gpio_direction_output(cs->gpio, 0);
		gpio_direction_output(rst->gpio, 0);
	}

	panel->power = power;
        return 0;
}

static int panel_get_power(struct lcd_device *lcd)
{
	struct panel_dev *panel = lcd_get_data(lcd);

	return panel->power;
}

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

	panel->cs.gpio = of_get_named_gpio_flags(np, "ingenic,cs-gpio", 0, &flags);
	if(gpio_is_valid(panel->cs.gpio)) {
		panel->cs.active_level = OF_GPIO_ACTIVE_LOW ? 0 : 1;
		ret = gpio_request_one(panel->cs.gpio, GPIOF_DIR_OUT, "cs");
		if(ret < 0) {
			dev_err(dev, "Failed to request cs pin!\n");
			return ret;
		}
	} else {
		dev_warn(dev, "invalid gpio cs.gpio: %d\n", panel->cs.gpio);
	}

	panel->rst.gpio = of_get_named_gpio_flags(np, "ingenic,rst-gpio", 0, &flags);
	if(gpio_is_valid(panel->rst.gpio)) {
		panel->rst.active_level = OF_GPIO_ACTIVE_LOW ? 0 : 1;
		ret = gpio_request_one(panel->rst.gpio, GPIOF_DIR_OUT, "rst");
		if(ret < 0) {
			dev_err(dev, "Failed to request rst pin!\n");
			goto err_request_rst;
		}
	} else {
		dev_warn(dev, "invalid gpio rst.gpio: %d\n", panel->rst.gpio);
	}

	return 0;
err_request_rst:
	gpio_free(panel->cs.gpio);
	return ret;
}
/**
* @panel_probe
*
* 	1. Register to ingenicfb.
* 	2. Register to lcd.
* 	3. Register to backlight if possible.
*
* @pdev
*
* @Return -
*/
static int panel_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct panel_dev *panel;

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

	panel->lcd = lcd_device_register("panel_lcd", &pdev->dev, panel, &panel_lcd_ops);
	if(IS_ERR_OR_NULL(panel->lcd)) {
		dev_err(&pdev->dev, "Error register lcd!\n");
		ret = -EINVAL;
		goto err_of_parse;
	}

	/* TODO: should this power status sync from uboot */
	panel->power = FB_BLANK_POWERDOWN;
	panel_set_power(panel->lcd, FB_BLANK_UNBLANK);

	ret = ingenicfb_register_panel(&lcd_panel);
	if(ret < 0) {
		dev_err(&pdev->dev, "Failed to register lcd panel!\n");
		goto err_lcd_register;
	}

	return 0;

err_lcd_register:
	lcd_device_unregister(panel->lcd);
err_of_parse:
	kfree(panel);
	return ret;
}

static int panel_remove(struct platform_device *pdev)
{
	struct panel_dev *panel = dev_get_drvdata(&pdev->dev);

	panel_set_power(panel->lcd, FB_BLANK_POWERDOWN);
	return 0;
}


#ifdef CONFIG_PM
static int panel_suspend(struct device *dev)
{
	struct panel_dev *panel = dev_get_drvdata(dev);

	panel_set_power(panel->lcd, FB_BLANK_POWERDOWN);
	return 0;
}

static int panel_resume(struct device *dev)
{
	struct panel_dev *panel = dev_get_drvdata(dev);

	panel_set_power(panel->lcd, FB_BLANK_UNBLANK);
	return 0;
}

static const struct dev_pm_ops panel_pm_ops = {
	.suspend = panel_suspend,
	.resume = panel_resume,
};
#endif
static const struct of_device_id panel_of_match[] = {
	{ .compatible = "ingenic,st7789p3", },
	{},
};

static struct platform_driver panel_driver = {
	.probe		= panel_probe,
	.remove		= panel_remove,
	.driver		= {
		.name	= "st7789p3",
		.of_match_table = panel_of_match,
#ifdef CONFIG_PM
		.pm = &panel_pm_ops,
#endif
	},
};

module_platform_driver(panel_driver);
MODULE_LICENSE("GPL");
