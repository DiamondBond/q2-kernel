/*
 * driver/video/fbdev/ingenic/x2000_v12/displays/panel-lt9611.c
 * Copyright (C) 2016 Ingenic Semiconductor Inc.
 * This program is free software, you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#if 0
/*reference device tree*/
 &dpu {
         /*1080p:1920 h X1080 v*/
         status = "okay";
         ingenic,disable-rdma-fb = <1>;
         ingenic,rot_angle = <0>;
         /*Defines the init state of composer fb export infomations.*/
         ingenic,layer-exported = <1 0 0 0>;
         ingenic,layer-frames   = <2 2 2 2>;
         ingenic,layer-framesize = <1920 1080>, <1920 1080>, <320 240>, <320 240>;   /*Max framesize for each layer.*/
         layer,color_mode        = <0 0 0 0>;                                    /*src fmt,*/
         layer,src-size          = <1920 1080>, <1920 1080>, <320 240>, <240 200>;       /*Layer src size should smaller than framesize*/
         layer,target-size       = <1920 1080>, <720 640>, <160 240>, <240 200>; /*Target Size should smaller than src_size.*/
         layer,target-pos        = <0 0>, <0 640>, <340 480>, <100 980>; /*target pos , the start point of the target panel.*/
         layer,enable            = <1 0 0 0>;                                    /*layer enabled or disabled.*/
         ingenic,logo-pan-layer  = <0>;                                          /*on which layer should init logo pan on.*/
         memory-region=<&reserved_memory>;

         port {
                 dpu_out_ep: endpoint {
                         remote-endpoint = <&lt9611_ep>;
             };
         };
 };

  display-dbi {
          compatible = "simple-bus";
          #interrupt-cells = <1>;
          #address-cells = <1>;
          #size-cells = <1>;
          ranges = <>;
          panel_fw050 {
                  compatible = "ingenic,lt9611";
                  status = "okay";
                  pinctrl-names = "default";
                  ingenic,vdd-en-gpio = <&gpb 19 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>;
                  ingenic,rst-gpio = <&gpb 18 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>;
                  ingenic,lcd-pwm-gpio = <&gpb 17 GPIO_ACTIVE_HIGH INGENIC_GPIO_NOBIAS>;
                  port {
                          lt9611_ep: endpoint {
                                  remote-endpoint = <&dpu_out_ep>;
                          };
                  };
          };
  };
#endif

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/pwm_backlight.h>
#include <linux/delay.h>
#include <linux/lcd.h>
#include <linux/of_gpio.h>
#include <linux/fb.h>
#include <linux/backlight.h>

#include "../ingenicfb.h"
#include "../jz_dsim.h"

#define MIPI_4LANE_MODE 1

static char panel_lt9611_debug = 1;
#define PANEL_DEBUG_MSG(msg...)			\
	do {					\
		if (panel_lt9611_debug)		\
			printk(">>>>>>>>>>>>>>>>PANEL lt9611: " msg);		\
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
	struct board_gpio vdd_en;
	struct board_gpio rst;
	struct board_gpio lcd_te;
	struct board_gpio lcd_pwm;

	struct mipi_dsim_lcd_device *dsim_dev;
};

struct panel_dev *panel;

#define lcd_to_master(a)	(a->dsim_dev->master)
#define lcd_to_master_ops(a)	((lcd_to_master(a))->master_ops)

#define PANEL_LT9611_ID	(0x98815c)

struct lt9611 {
	struct device *dev;
	unsigned int power;
	unsigned int id;

	struct lcd_device *ld;
	struct backlight_device *bd;

	struct mipi_dsim_lcd_device *dsim_dev;

};


static void panel_dev_sleep_in(struct panel_dev *lcd)
{
}

static void panel_dev_display_off(struct panel_dev *lcd)
{
}

static void panel_dev_power_on(struct mipi_dsim_lcd_device *dsim_dev, int power)
{
	struct board_gpio *vdd_en = &panel->vdd_en;
	struct board_gpio *rst = &panel->rst;
	struct board_gpio *lcd_te = &panel->lcd_te;
	struct board_gpio *pwm = &panel->lcd_pwm;
	PANEL_DEBUG_MSG("enter %s %d \n",__func__, __LINE__);

	gpio_direction_output(vdd_en->gpio, vdd_en->active_level);
	if(gpio_is_valid(pwm->gpio))
		gpio_direction_output(pwm->gpio, pwm->active_level);

	if (gpio_is_valid(lcd_te->gpio)) {
		gpio_direction_input(lcd_te->gpio);
	}
	msleep(50);
	gpio_direction_output(rst->gpio, 0);
	msleep(10);
	gpio_direction_output(rst->gpio, 1);
	msleep(120);

	panel->power = power;
}

static int panel_dev_read_id(struct mipi_dsim_lcd_device *dsim_dev, unsigned char *buf, int count)
{
	int panel_id = 0;

	buf[0] = 0x98;
	buf[1] = 0x81;
	buf[2] = 0x5c;
	panel_id = (buf[0] << 16) + (buf[1] << 8) + buf[2];
	printk("----------%s, (%d) id[0]: %x id[1]: %x, id[2]: %x, id:%08x \n", __func__, __LINE__, buf[0], buf[1], buf[2], panel_id);

	if(panel_id == PANEL_LT9611_ID)
		return 0;
	else
		return -1;
}

static struct fb_videomode panel_modes[] = {
	[0] = {
		.name = "lontium_lt9611-lcd",
#if 0
		.xres = 720,
		.yres = 1280,
#else
		.xres = 1920,
		.yres = 1080,
#endif

#ifdef MIPI_4LANE_MODE
		.refresh = 60,
#else
		.refresh = 30,
#endif
		.left_margin = 148,	//hbp
		.right_margin = 88,	//hfp
		.hsync_len = 44,	//hsync

		.upper_margin = 36,	//vbp
		.lower_margin = 4,	//vfp
		.vsync_len = 5,	//vsync
		.sync                   = FB_SYNC_HOR_HIGH_ACT & FB_SYNC_VERT_HIGH_ACT,
		.vmode                  = FB_VMODE_NONINTERLACED,
		.flag = 0,
	},
};

struct jzdsi_data jzdsi_pdata[] = {
	[0] = {
		.modes = &panel_modes[0],
#ifdef MIPI_4LANE_MODE
		.video_config.no_of_lanes = 4,
#else
		.video_config.no_of_lanes = 2,
#endif
		.video_config.virtual_channel = 0,
		.video_config.color_coding = COLOR_CODE_24BIT,
		.video_config.video_mode = VIDEO_BURST_WITH_SYNC_PULSES,
		.video_config.receive_ack_packets = 0,	/* enable receiving of ack packets */
		.video_config.is_18_loosely = 0,
		.video_config.data_en_polarity = 1,
#if 0
		.video_config.byte_clock = 55744, // driver will auto calculate byte_clock.720*1080
#else
		.video_config.byte_clock = 111375, // 1920*1080,real mipi clock:891Mhz
#endif
		.video_config.lane0_pn_swap = 0,
		.video_config.lane1_pn_swap = 0,
		.video_config.lane2_pn_swap = 0,
		.video_config.lane3_pn_swap = 0,
		.video_config.clk_lane_pn_swap = 0,

		.dsi_config.max_lanes = 4,
		.dsi_config.max_hs_to_lp_cycles = 100,
		.dsi_config.max_lp_to_hs_cycles = 40,
		.dsi_config.max_bta_cycles = 4095,
		.dsi_config.color_mode_polarity = 1,
		.dsi_config.shut_down_polarity = 1,
		.dsi_config.max_bps = 2750,
		.bpp_info = 24,
	},
};


static struct tft_config lt9611_cfg = {
	.pix_clk_inv = 0,
	.de_dl = 0,
	.sync_dl = 0,
	.color_even = TFT_LCD_COLOR_EVEN_RGB,
	.color_odd = TFT_LCD_COLOR_ODD_RGB,
	.mode = TFT_LCD_MODE_PARALLEL_888,
};

struct lcd_panel lcd_panel = {
	.num_modes = ARRAY_SIZE(panel_modes),
	.modes = panel_modes,
	.dsi_pdata = jzdsi_pdata,

	.lcd_type = LCD_TYPE_MIPI_TFT,
	.tft_config = &lt9611_cfg,
	.bpp = 24,
	.width = 62,
	.height = 110,
	.dither_enable = 0,
	.dither.dither_red = 0,
	.dither.dither_green = 0,
	.dither.dither_blue = 0,
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
* @ pannel_lt9611_lcd_ops, register to kernel common backlight/lcd.c framworks.
*/
static struct lcd_ops panel_lcd_ops = {
	.set_power = panel_set_power,
	.get_power = panel_get_power,
};

static int of_panel_parse(struct device *dev)
{
//	struct panel_dev *panel = dev_get_drvdata(dev);
//	struct device_node *np = dev->of_node;
//	enum of_gpio_flags flags;
	int ret = 0;
	PANEL_DEBUG_MSG("enter %s %d \n",__func__, __LINE__);
	/*
	panel->rst.gpio = of_get_named_gpio_flags(np, "ingenic,rst-gpio", 0, &flags);
	if(gpio_is_valid(panel->rst.gpio)) {
		panel->rst.active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		dev_info(dev, "rst-gpio: %d, active_level: %d, flags: 0x%x\n", panel->rst.gpio, panel->rst.active_level, flags);
		gpio_direction_output(panel->rst.gpio, !panel->rst.active_level);
		gpio_direction_output(panel->rst.gpio, panel->rst.active_level);
		ret = gpio_request_one(panel->rst.gpio, GPIOF_DIR_OUT, "rst");
		if(ret < 0) {
			dev_err(dev, "Failed to request rst pin!\n");
			return ret;
		}
	} else {
		dev_warn(dev, "invalid gpio rst.gpio: %d\n", panel->rst.gpio);
	}

	panel->vdd_en.gpio = of_get_named_gpio_flags(np, "ingenic,vdd-en-gpio", 0, &flags);
	if(gpio_is_valid(panel->vdd_en.gpio)) {
		panel->vdd_en.active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		dev_info(dev, "en-gpio: %d, active_level: %d, flags: 0x%x\n", panel->vdd_en.gpio, panel->vdd_en.active_level, flags);
		gpio_direction_output(panel->vdd_en.gpio, !panel->vdd_en.active_level);
		ret = gpio_request_one(panel->vdd_en.gpio, GPIOF_DIR_OUT, "vdd_en");
		if(ret < 0) {
			dev_err(dev, "Failed to request vdd_en pin!\n");
			goto err_request_lcd_vdd;
		}
	} else {
		dev_warn(dev, "invalid gpio vdd_en.gpio: %d\n", panel->vdd_en.gpio);
	}

	 panel->lcd_pwm.gpio = of_get_named_gpio_flags(np, "ingenic,lcd-pwm-gpio", 0, &flags);
	 if(gpio_is_valid(panel->lcd_pwm.gpio)) {
	 	panel->lcd_pwm.active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		dev_info(dev, "pwm-gpio: %d, active_level: %d, flags: 0x%x\n", panel->lcd_pwm.gpio, panel->lcd_pwm.active_level, flags);
	 	ret = gpio_request_one(panel->lcd_pwm.gpio, GPIOF_DIR_OUT, "lcd-pwm");
	 	if(ret < 0) {
	 		dev_err(dev, "Failed to request lcd-pwm pin!\n");
	 		goto err_request_lcd_pwm;
	 	}
	 } else {
	 	dev_warn(dev, "invalid gpio lcd-pwm.gpio: %d\n", panel->lcd_pwm.gpio);
	 }
	*/
	return 0;

//err_request_lcd_pwm:
//	if(gpio_is_valid(panel->vdd_en.gpio))
//		gpio_free(panel->vdd_en.gpio);
//err_request_lcd_vdd:
//	if(gpio_is_valid(panel->rst.gpio))
//		gpio_free(panel->rst.gpio);
	return ret;
}

static int panel_dev_probe(struct mipi_dsim_lcd_device *dsim_dev)
{
	struct lt9611 *lcd;
	char buffer[4];
	int ret;
	ret = panel_dev_read_id(dsim_dev, buffer, 4);
	if(ret < 0) {
		return -EINVAL;
	}
	lcd = devm_kzalloc(&dsim_dev->dev, sizeof(struct lt9611), GFP_KERNEL);
	if (!lcd)
	{
		dev_err(&dsim_dev->dev, "failed to allocate lontium_lt9611 structure.\n");
		return -ENOMEM;
	}

	lcd->dsim_dev = dsim_dev;
	lcd->dev = &dsim_dev->dev;

	lcd->ld = lcd_device_register("lontium_lt9611", lcd->dev, lcd,
	                              &panel_lcd_ops);
	if (IS_ERR(lcd->ld))
	{
		dev_err(lcd->dev, "failed to register lcd ops.\n");
		return PTR_ERR(lcd->ld);
	}

	dev_set_drvdata(&dsim_dev->dev, lcd);

	dev_info(lcd->dev, "probed lt9611 driver.\n");

	panel->dsim_dev = dsim_dev;
	panel->lcd = lcd->ld;

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

static struct mipi_dsim_lcd_driver panel_dev_dsim_ddi_driver = {
	.name = "lontium_lt9611-lcd",
	.id = -1,

	.power_on = panel_dev_power_on,
	.probe = panel_dev_probe,
	.suspend = panel_suspend,
	.resume = panel_resume,
};

#ifdef CONFIG_PM
static int panel_pm_suspend(struct device *dev)
{
	struct board_gpio *vdd_en = &panel->vdd_en;

	panel_dev_display_off(panel);
	panel_dev_sleep_in(panel);
	gpio_direction_output(vdd_en->gpio, !vdd_en->active_level);

	return 0;
}

static int panel_pm_resume(struct device *dev)
{
	struct board_gpio *vdd_en = &panel->vdd_en;

	gpio_direction_output(vdd_en->gpio, vdd_en->active_level);

	return 0;
}

static const struct dev_pm_ops panel_pm_ops = {
	.suspend = panel_pm_suspend,
	.resume = panel_pm_resume,
};
#endif

struct mipi_dsim_lcd_device panel_dev_device={
	.name		= "lontium_lt9611-lcd",
	.id = 0,
};

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
	mipi_dsi_register_lcd_device(&panel_dev_device);
	mipi_dsi_register_lcd_driver(&panel_dev_dsim_ddi_driver);

	ret = ingenicfb_register_panel(&lcd_panel);
	if(ret < 0) {
		dev_err(&pdev->dev, "Failed to register lcd panel!\n");
		goto err_of_parse;
	}

	return 0;

err_of_parse:
	if(gpio_is_valid(panel->lcd_pwm.gpio))
		gpio_free(panel->lcd_pwm.gpio);
	if(gpio_is_valid(panel->rst.gpio))
		gpio_free(panel->rst.gpio);
	if(gpio_is_valid(panel->vdd_en.gpio))
		gpio_free(panel->vdd_en.gpio);

	kfree(panel);
	return ret;
}

static int panel_remove(struct platform_device *pdev)
{
	struct panel_dev *panel = dev_get_drvdata(&pdev->dev);

	lcd_device_unregister(panel->lcd);
	device_unregister(&panel_dev_device.dev);
	gpio_free(panel->rst.gpio);
	gpio_free(panel->vdd_en.gpio);
	return 0;
}

static const struct of_device_id panel_of_match[] = {
	{ .compatible = "ingenic,lt9611", },
	{},
};

static struct platform_driver panel_driver = {
	.probe		= panel_probe,
	.remove		= panel_remove,
	.driver		= {
		.name	= "lt9611",
		.of_match_table = panel_of_match,
#ifdef CONFIG_PM
		.pm = &panel_pm_ops,
#endif
	},
};

module_platform_driver(panel_driver);

MODULE_LICENSE("GPL");
