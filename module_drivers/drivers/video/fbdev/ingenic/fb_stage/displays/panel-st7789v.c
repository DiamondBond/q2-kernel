/*
 * driver/video/fbdev/ingenic/fb_v12/displays/st7789v.c
 *
 * Copyright (C) 2016 Ingenic Semiconductor Inc.
 *
 * Author:clwang<chunlei.wang@ingenic.com>
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
#include "../dpu_ctrl.h"

#define DATA_WIDTH_8BIT 1

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

	struct board_gpio rst;
	struct board_gpio vdd_en;
	struct board_gpio rd;
};

static struct smart_lcd_data_table st7789v_8_bit_data_table[] = {
/* LCD init code */
	{SMART_CONFIG_CMD,  0x11},
	{SMART_CONFIG_CMD,  0x35},
	{SMART_CONFIG_PRM,  0x00},
	{SMART_CONFIG_CMD,  0x36},//Display Setting
	{SMART_CONFIG_PRM,  0x00},
	{SMART_CONFIG_CMD,  0x3A},
	{SMART_CONFIG_PRM,  0x05},
	{SMART_CONFIG_CMD,  0xB2},
	{SMART_CONFIG_PRM,  0x0C},
	{SMART_CONFIG_PRM,  0x0C},
	{SMART_CONFIG_PRM,  0x00},
	{SMART_CONFIG_PRM,  0x33},
	{SMART_CONFIG_PRM,  0x33},
	{SMART_CONFIG_CMD,  0xB7},
	{SMART_CONFIG_PRM,  0x75},
	{SMART_CONFIG_CMD,  0xBB},
	{SMART_CONFIG_PRM,  0x19},
	{SMART_CONFIG_CMD,  0xC0},
	{SMART_CONFIG_PRM,  0x2C},
	{SMART_CONFIG_CMD,  0xC2},
	{SMART_CONFIG_PRM,  0x01},
	{SMART_CONFIG_CMD,  0xC3},
	{SMART_CONFIG_PRM,  0x0C},
	{SMART_CONFIG_CMD,  0xC4},
	{SMART_CONFIG_PRM,  0x20},
	{SMART_CONFIG_CMD,  0xC6},
	{SMART_CONFIG_PRM,  0x0F},
	{SMART_CONFIG_CMD,  0xD0},
	{SMART_CONFIG_PRM,  0xA4},
	{SMART_CONFIG_PRM,  0xA1},
	{SMART_CONFIG_CMD,  0x21},
	{SMART_CONFIG_CMD,  0xE0},//Gamma setting
	{SMART_CONFIG_PRM,  0xD0},
	{SMART_CONFIG_PRM,  0x04},
	{SMART_CONFIG_PRM,  0x0C},
	{SMART_CONFIG_PRM,  0x0E},
	{SMART_CONFIG_PRM,  0x0E},
	{SMART_CONFIG_PRM,  0x29},
	{SMART_CONFIG_PRM,  0x37},
	{SMART_CONFIG_PRM,  0x44},
	{SMART_CONFIG_PRM,  0x47},
	{SMART_CONFIG_PRM,  0x0B},
	{SMART_CONFIG_PRM,  0x17},
	{SMART_CONFIG_PRM,  0x16},
	{SMART_CONFIG_PRM,  0x1B},
	{SMART_CONFIG_PRM,  0x1F},
	{SMART_CONFIG_CMD,  0xE1},
	{SMART_CONFIG_PRM,  0xD0},
	{SMART_CONFIG_PRM,  0x04},
	{SMART_CONFIG_PRM,  0x0C},
	{SMART_CONFIG_PRM,  0x0E},
	{SMART_CONFIG_PRM,  0x0F},
	{SMART_CONFIG_PRM,  0x29},
	{SMART_CONFIG_PRM,  0x37},
	{SMART_CONFIG_PRM,  0x44},
	{SMART_CONFIG_PRM,  0x4A},
	{SMART_CONFIG_PRM,  0x0C},
	{SMART_CONFIG_PRM,  0x17},
	{SMART_CONFIG_PRM,  0x16},
	{SMART_CONFIG_PRM,  0x1B},
	{SMART_CONFIG_PRM,  0x1F},
	{SMART_CONFIG_CMD,  0x29},
	{SMART_CONFIG_CMD,  0x2A},
	{SMART_CONFIG_PRM,  0x0 },//Xstart
	{SMART_CONFIG_PRM,  0x0 },
	{SMART_CONFIG_PRM,  0x0 },//Xend
	{SMART_CONFIG_PRM,  0xEF},
	{SMART_CONFIG_CMD,  0x2B},
	{SMART_CONFIG_PRM,  0x0 },//Ystart
	{SMART_CONFIG_PRM,  0x0 },
	{SMART_CONFIG_PRM,  0x01},//Yend
	{SMART_CONFIG_PRM,  0x3F},
	{SMART_CONFIG_CMD,  0x2C},
};

static struct smart_lcd_data_table st7789v_16_bit_data_table[] = {
/* LCD init code */
	{SMART_CONFIG_CMD,   0xB0},
	{SMART_CONFIG_PRM,  0x00},
	{SMART_CONFIG_PRM,  0xF0},

	{SMART_CONFIG_CMD,   0xB1},
	{SMART_CONFIG_PRM,  0x00},
	{SMART_CONFIG_PRM,  0x03},
	{SMART_CONFIG_PRM,  0x1F},

	{SMART_CONFIG_CMD,   0xB2},
	{SMART_CONFIG_PRM,  0x0C},
	{SMART_CONFIG_PRM,  0x0C},
	{SMART_CONFIG_PRM,  0x00},
	{SMART_CONFIG_PRM,  0x33},
	{SMART_CONFIG_PRM,  0x33},

	{SMART_CONFIG_CMD,  0x35},
	{SMART_CONFIG_PRM, 0x00},

	{SMART_CONFIG_CMD,  0x36},
	{SMART_CONFIG_PRM, 0x00},

	{SMART_CONFIG_CMD,  0x3A},//8bit/16bit rgb565 65k colors 3Ah="05h"
	{SMART_CONFIG_PRM, 0x05},

	{SMART_CONFIG_CMD,  0xB7},
	{SMART_CONFIG_PRM, 0x35},

	{SMART_CONFIG_CMD,  0xBB},
	{SMART_CONFIG_PRM, 0x34},

	{SMART_CONFIG_CMD,  0xC0},
	{SMART_CONFIG_PRM, 0x2C},

	{SMART_CONFIG_CMD,  0xC2},
	{SMART_CONFIG_PRM, 0x01},

	{SMART_CONFIG_CMD,  0xC3},
	{SMART_CONFIG_PRM, 0x13},

	{SMART_CONFIG_CMD,  0xC4},
	{SMART_CONFIG_PRM, 0x20},

	{SMART_CONFIG_CMD,  0xC6},
	{SMART_CONFIG_PRM, 0x0F},//frame rate = 60hz

	{SMART_CONFIG_CMD,  0xD0},
	{SMART_CONFIG_PRM, 0xA4},
	{SMART_CONFIG_PRM, 0xA1},

	{SMART_CONFIG_CMD,  0xD6},
	{SMART_CONFIG_PRM, 0xA1},

	{SMART_CONFIG_CMD,  0x51},
	{SMART_CONFIG_PRM, 0xff},

	{SMART_CONFIG_CMD,  0x53},
	{SMART_CONFIG_PRM, 0x2c},

	{SMART_CONFIG_CMD,  0x55},
	{SMART_CONFIG_PRM, 0x02},

	{SMART_CONFIG_CMD,  0x21},
	{SMART_CONFIG_UDELAY, 120000},

	{SMART_CONFIG_CMD,  0x11},
	{SMART_CONFIG_UDELAY, 1200000},

	{SMART_CONFIG_CMD,  0x29},
	{SMART_CONFIG_UDELAY, 10000},

	{SMART_CONFIG_CMD,  0x2C},
};

static struct fb_videomode panel_modes[] = {
	[0] = {
		.name = "240x320",
		.refresh = 60,
		.xres = 240,
		.yres = 320,
		.pixclock = KHZ2PICOS(13825),
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

struct smart_config st7789v_cfg = {
	.te_anti_jit = 1,
	.te_md = 0,
	.te_switch = 0,
	.dc_md = 0,
	.wr_md = 1,
	.te_dp = 1,
	.cs_en = 1,
	.smart_type = SMART_LCD_TYPE_8080,
	.pix_fmt = SMART_LCD_FORMAT_565,
	.write_gram_cmd = 0x2c,
#if DATA_WIDTH_8BIT
	.dwidth = SMART_LCD_DWIDTH_8_BIT,
	.cwidth = SMART_LCD_CWIDTH_8_BIT,
	.bus_width = 8,
	.data_table = st7789v_8_bit_data_table,
	.length_data_table = ARRAY_SIZE(st7789v_8_bit_data_table),
#else
	.dwidth = SMART_LCD_DWIDTH_16_BIT,
	.cwidth = SMART_LCD_CWIDTH_16_BIT,
	.bus_width = 16,
	.data_table = st7789v_16_bit_data_table,
	.length_data_table = ARRAY_SIZE(st7789v_16_bit_data_table),
#endif
};

static struct lcd_device *lcd_power = NULL;

static void panel_power_reset(struct board_gpio *rst)
{
	gpio_direction_output(rst->gpio, 0);
	mdelay(20);
	gpio_direction_output(rst->gpio, 1);
	mdelay(20);
}

#define POWER_IS_ON(pwr)        ((pwr) <= FB_BLANK_NORMAL)
static int panel_set_power(struct lcd_device *lcd, int power)
{
	struct panel_dev *panel = lcd_get_data(lcd);
	struct board_gpio *rst = &panel->rst;
	struct board_gpio *vdd_en = &panel->vdd_en;
	struct board_gpio *rd = &panel->rd;

	if(POWER_IS_ON(power) && !POWER_IS_ON(panel->power)) {
		gpio_direction_output(rd->gpio, 1);
		gpio_direction_output(vdd_en->gpio, 0);
		gpio_direction_output(rst->gpio, 1);
		panel_power_reset(rst);
	}
	if(!POWER_IS_ON(power) && POWER_IS_ON(panel->power)) {
		gpio_direction_output(rst->gpio, 0);
		gpio_direction_output(vdd_en->gpio, 1);
		gpio_direction_output(rd->gpio, 0);
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
/*	.set_power = panel_set_power,*/
/*	.get_power = panel_get_power,*/
};

static int panel_power_off(struct lcd_panel *lcd_panel)
{
	if(lcd_power)
		panel_set_power(lcd_power, FB_BLANK_POWERDOWN);

	return 0;
}

static int panel_power_on(struct lcd_panel *lcd_panel)
{
	if(lcd_power)
		panel_set_power(lcd_power, FB_BLANK_UNBLANK);

	return 0;
}

static struct lcd_panel_ops st7789v_ops = {
	.disable  = (void*)panel_power_off,
	.enable = (void*)panel_power_on,
};

struct lcd_panel lcd_panel = {
	.name = "st7789v",
	.num_modes = ARRAY_SIZE(panel_modes),
	.modes = panel_modes,
	.lcd_type = LCD_TYPE_SLCD,
	.width = 240,
	.height = 320,

	.smart_config = &st7789v_cfg,

	.dither_enable = 0,
	.dither.dither_red = 0,
	.dither.dither_green = 0,
	.dither.dither_blue = 0,

	.ops = &st7789v_ops,
};

static int of_panel_parse(struct device *dev)
{
	struct panel_dev *panel = dev_get_drvdata(dev);
	struct device_node *np = dev->of_node;
	enum of_gpio_flags flags;
	int ret = 0;

	panel->rd.gpio = of_get_named_gpio_flags(np, "ingenic,rd-gpio", 0, &flags);
	if(gpio_is_valid(panel->rd.gpio)) {
		panel->rd.active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		ret = gpio_request_one(panel->rd.gpio, GPIOF_DIR_OUT, "rd");
		if(ret < 0) {
			dev_err(dev, "Failed to request rd pin!\n");
			goto err_request_rst;
		}
	} else {
		dev_warn(dev, "invalid gpio rd.gpio: %d\n", panel->rd.gpio);
	}

	panel->rst.gpio = of_get_named_gpio_flags(np, "ingenic,rst-gpio", 0, &flags);
	if(gpio_is_valid(panel->rst.gpio)) {
		panel->rst.active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		ret = gpio_request_one(panel->rst.gpio, GPIOF_DIR_OUT, "rst");
		if(ret < 0) {
			dev_err(dev, "Failed to request rst pin!\n");
			goto err_request_rst;
		}
	} else {
		dev_warn(dev, "invalid gpio rst.gpio: %d\n", panel->rst.gpio);
	}

	panel->vdd_en.gpio = of_get_named_gpio_flags(np, "ingenic,vdd-en-gpio", 0, &flags);
	if(gpio_is_valid(panel->vdd_en.gpio)) {
		panel->vdd_en.active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		ret = gpio_request_one(panel->vdd_en.gpio, GPIOF_DIR_OUT, "vdd_en");
		if(ret < 0) {
			dev_err(dev, "Failed to request vdd_en pin!\n");
			goto err_request_rst;
		}
	} else {
		dev_warn(dev, "invalid gpio vdd_en.gpio: %d\n", panel->vdd_en.gpio);
	}



	return 0;
err_request_rst:
	printk("failed to request io\n");
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

	lcd_power = panel->lcd;

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
	{ .compatible = "ingenic,st7789v", },
	{},
};

static struct platform_driver panel_driver = {
	.probe		= panel_probe,
	.remove		= panel_remove,
	.driver		= {
		.name	= "st7789v",
		.of_match_table = panel_of_match,
#ifdef CONFIG_PM
		.pm = &panel_pm_ops,
#endif
	},
};

module_platform_driver(panel_driver);
MODULE_LICENSE("GPL");
