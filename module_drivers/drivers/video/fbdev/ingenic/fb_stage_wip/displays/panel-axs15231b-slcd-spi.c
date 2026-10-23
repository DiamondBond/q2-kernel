/*
 * driver/video/fbdev/ingenic/fb_stage_wip/displays/panel-axs15231b-slcd-spi-mod.c
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
/*
	display-dbi {
		compatible = "simple-bus";
		#interrupt-cells = <1>;
		#address-cells = <1>;
		#size-cells = <1>;
		ranges = <>;
		panel_axs15231b {
			compatible = "ingenic,spi_slcd_axs15231b";
			status = "okay";
			pinctrl-names = "default";
			pinctrl-0 = <&smart_lcd_spi> ,<&smart_lcd_pa_te>;
			ingenic,rst-gpio = <&gpa 17 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>;
			ingenic,pwr-gpio = <&gpa 16 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>;
			port {
				panel_axs15231b_ep: endpoint {
					   remote-endpoint = <&dpu_out_ep>;
			   };
			};
		};
	};

	backlight {
            status = "okay";
	    compatible = "pwm-backlight";
	    pinctrl-names = "default";
	    pwms = <&pwm 5 1000000>;
	    brightness-levels = <0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15>;
	    default-brightness-level = <15>;
	};

};

&dpu {
	status = "okay";
	port {
		dpu_out_ep: endpoint {
		remote-endpoint = <&panel_axs15231b_ep>;
	    };
	};
};
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
	struct board_gpio pwr;
};

static struct smart_lcd_data_table axs15231b_data_table[] = {
    /* LCD init code */
	{SMART_CONFIG_CMD, 0x02},
	{SMART_CONFIG_PRM, 0x00},


	{SMART_CONFIG_CMD, 0x11},
	{SMART_CONFIG_PRM, 0x00},

	{SMART_CONFIG_UDELAY, 200000},

	{SMART_CONFIG_CMD, 0x02},
	{SMART_CONFIG_PRM, 0x00},
	{SMART_CONFIG_CMD, 0x29},
	{SMART_CONFIG_PRM, 0x00},

	{SMART_CONFIG_CMD, 0x35},
	{SMART_CONFIG_PRM, 0x00},

	{SMART_CONFIG_CMD, 0x2a},
	{SMART_CONFIG_PRM, 0x00},
	{SMART_CONFIG_PRM, 0x00},
	{SMART_CONFIG_PRM, 0x00},
	{SMART_CONFIG_PRM, 0xab},

	{SMART_CONFIG_CMD, 0x2b},
	{SMART_CONFIG_PRM, 0x00},
	{SMART_CONFIG_PRM, 0x00},
	{SMART_CONFIG_PRM, 0x02},
	{SMART_CONFIG_PRM, 0x2f},

	{SMART_CONFIG_UDELAY, 200000},

	{SMART_CONFIG_CMD, 0x2c},
};


static struct fb_videomode panel_modes[] = {
	[0] = {
		.name = "172x560",
		.refresh = 60,
		.xres = 172,
		.yres = 560,
		.pixclock = KHZ2PICOS(30000),
		.sync = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
		.vmode = FB_VMODE_NONINTERLACED,
		.flag = 0,
	},
};

struct smart_config axs15231b_cfg = {
	.te_anti_jit = 1,
	.te_md = 1,
	.te_dp = 0,
	.te_switch = 1,
	.cs_en = 1,
	.dc_md = 0,
	.wr_md = 0,
	.smart_type = SMART_LCD_TYPE_SPI_4,
	.pix_fmt = SMART_LCD_FORMAT_565,
	.data_table = axs15231b_data_table,
	.length_data_table = ARRAY_SIZE(axs15231b_data_table),
};

struct lcd_panel lcd_panel = {
	.name = "axs15231b",
	.num_modes = ARRAY_SIZE(panel_modes),
	.modes = panel_modes,
	.lcd_type = LCD_TYPE_SLCD,
	.bpp = 16,
	.width = 172,
	.height = 560,

	.smart_config = &axs15231b_cfg,
};

static void panel_power_reset(struct board_gpio *rst)
{
	gpio_direction_output(rst->gpio, 0);
	mdelay(200);
	gpio_direction_output(rst->gpio, 1);
	mdelay(15);
}

#define POWER_IS_ON(pwr)        ((pwr) <= FB_BLANK_NORMAL)
static int panel_set_power(struct lcd_device *lcd, int power)
{
	struct panel_dev *panel = lcd_get_data(lcd);
	struct board_gpio *pwr = &panel->pwr;
	struct board_gpio *rst = &panel->rst;

	if(POWER_IS_ON(power) && !POWER_IS_ON(panel->power)) {
		gpio_direction_output(pwr->gpio, 0);
		gpio_direction_output(rst->gpio, 1);
		mdelay(5);
		panel_power_reset(rst);

	}
	if(!POWER_IS_ON(power) && POWER_IS_ON(panel->power)) {
		gpio_direction_output(rst->gpio, 0);
		gpio_direction_output(pwr->gpio, 1);
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

	panel->pwr.gpio = of_get_named_gpio_flags(np, "ingenic,pwr-gpio", 0, &flags);
	if(gpio_is_valid(panel->pwr.gpio)) {
		panel->pwr.active_level = OF_GPIO_ACTIVE_LOW ? 0 : 1;
		ret = gpio_request_one(panel->pwr.gpio, GPIOF_DIR_OUT, "pwr");
		if(ret < 0) {
			dev_err(dev, "Failed to request pwr pin!\n");
			return ret;
		}
	} else {
		dev_warn(dev, "invalid gpio pwr.gpio: %d\n", panel->pwr.gpio);
	}

	panel->rst.gpio = of_get_named_gpio_flags(np, "ingenic,rst-gpio", 0, &flags);
	if(gpio_is_valid(panel->rst.gpio)) {
		panel->rst.active_level = OF_GPIO_ACTIVE_LOW ? 0 : 1;
		ret = gpio_request_one(panel->rst.gpio, GPIOF_DIR_OUT, "rst");
		if(ret < 0) {
			dev_err(dev, "Failed to request rst pin!\n");
			return ret;
		}
	} else {
		dev_warn(dev, "invalid gpio rst.gpio: %d\n", panel->rst.gpio);
	}

	return 0;
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
	{ .compatible = "ingenic,spi_slcd_axs15231b", },
	{},
};

static struct platform_driver panel_driver = {
	.probe		= panel_probe,
	.remove		= panel_remove,
	.driver		= {
		.name	= "axs15231b",
		.of_match_table = panel_of_match,
#ifdef CONFIG_PM
		.pm = &panel_pm_ops,
#endif
	},
};

module_platform_driver(panel_driver);
MODULE_LICENSE("GPL");
