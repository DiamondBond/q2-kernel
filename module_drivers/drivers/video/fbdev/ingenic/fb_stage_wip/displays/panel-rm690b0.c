#if 0
/*      disable-rdma-fb   compfb_wback_out   rot_angle
  *              0               0               0               rdma模式
  *              1               0               0               直通模式
  *              1               1               0               comp合成写会rdma显示
  *              1               0               rot             comp合成旋转显示（auto）
  *              1               1               rot             comp合成旋转显示（manual）
  * wip驱动使用
  */
/*oled 屏该屏没有操控背光*/
 &dpu {
         status = "okay";
         ingenic,disable-rdma-fb = <1>;
         ingenic,compfb_wback_out = <1>;
         ingenic,rot_angle = <0>;
         /*Defines the init state of composer fb export infomations.*/
         ingenic,layer-exported = <1 0 0 0>;
         ingenic,layer-frames   = <2 2 2 2>;
         ingenic,layer-framesize = <450 600>, <450 600>, <450 600>, <0 0>;   /*Max framesize for each layer.*/
         layer,src-size          = <450 600>, <450 600>, <450 600>, <0 0>;   /*Layer src size should smaller than framesize*/
         layer,target-size       = <450 600>, <450 600>, <450 600>, <0 0>;   /*Target Size should smaller than src_size.*/
         layer,target-pos        = <0 0>, <0 0>, <0 0>, <0 0>; /*target pos , the start point of the target panel.*/
         layer,enable            = <1 0 0 0>;                                    /*layer enabled or disabled.*/
         ingenic,logo-pan-layer  = <0>;                                          /*on which layer should init logo pan on.*/
         memory-region=<&reserved_memory>;
         port {
                 dpu_out_ep: endpoint {
                         remote-endpoint = <&panel_rm690b0_ep>;
             };
         };
 };
 / {
         display-dbi {
                 compatible = "simple-bus";
                 #interrupt-cells = <1>;
                 #address-cells = <1>;
                 #size-cells = <1>;
                 ranges = <>;
                 panel_rm690b0 {
                         compatible = "ingenic,rm690b0";
                         status = "okay";
                         pinctrl-names = "default";
                         ingenic,rst-gpio = <&gpb 20 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>;

                         port {
                                 panel_rm690b0_ep: endpoint {
                                         remote-endpoint = <&dpu_out_ep>;
                                 };
                         };
                 };

         };
};
#endif

/*
 * driver/video/fbdev/ingenic/x2000_v12/displays/panel-rm690b0.c
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

#include "../ingenicfb.h"
#include "../jz_dsim.h"


static char panel_rm690b0_debug = 0;
#define PANEL_DEBUG_MSG(msg...)			\
	do {					\
		if (panel_rm690b0_debug)		\
			printk(">>>>>>>>>>>>>>>>PANEL rm690b0: " msg);		\
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

	struct board_gpio rst;

	struct mipi_dsim_lcd_device *dsim_dev;
};

static struct panel_dev *panel;

#define lcd_to_master(a)	(a->dsim_dev->master)
#define lcd_to_master_ops(a)	((lcd_to_master(a))->master_ops)

#define PANEL_rm690b0_ID	(0x00008000)

struct rm690b0 {
	struct device *dev;
	unsigned int power;
	unsigned int id;

	struct lcd_device *ld;
	struct backlight_device *bd;

	struct mipi_dsim_lcd_device *dsim_dev;

};

static struct dsi_cmd_packet fitipower_rm690b0_450_600_1lane_cmd_list[] =
{
	{0x39, 0x02, 0x00, {0xFE, 0x82}},
	{0x39, 0x02, 0x00, {0x09, 0x37}},
	{0x39, 0x02, 0x00, {0xFE, 0x00}},
	{0x39, 0x05, 0x00, {0x2A, 0x00, 0x10, 0x01, 0xD1}},
	{0x39, 0x05, 0x00, {0x2B, 0x00, 0x00, 0x02, 0x57}},
	{0x39, 0x05, 0x00, {0x30, 0x00, 0x01, 0x02, 0x56}},
	{0x39, 0x02, 0x00, {0x35, 0x00}},
	{0x39, 0x02, 0x00, {0xC2, 0x00}},
	{0x39, 0x02, 0x00, {0x51, 0xFF}},
	{0x39, 0x02, 0x00, {0x12, 0x00}},
};

static void panel_dev_sleep_in(struct panel_dev *lcd)
{
	struct dsi_master_ops *ops = lcd_to_master_ops(lcd);
	struct dsi_cmd_packet data_to_send = {0x05, 0x10, 0x00};
	PANEL_DEBUG_MSG("enter %s %d \n",__func__, __LINE__);

	ops->cmd_write(lcd_to_master(lcd), data_to_send);
}

static void panel_dev_sleep_out(struct panel_dev *lcd)
{
	struct dsi_master_ops *ops = lcd_to_master_ops(lcd);
	struct dsi_cmd_packet data_to_send = {0x05, 0x11, 0x00};
	PANEL_DEBUG_MSG("enter %s %d \n",__func__, __LINE__);

	ops->cmd_write(lcd_to_master(lcd), data_to_send);
}

static void panel_dev_display_on(struct panel_dev *lcd)
{
	struct dsi_master_ops *ops = lcd_to_master_ops(lcd);
	struct dsi_cmd_packet data_to_send = {0x05, 0x29, 0x00};
	PANEL_DEBUG_MSG("enter %s %d \n",__func__, __LINE__);

	ops->cmd_write(lcd_to_master(lcd), data_to_send);
}

static void panel_dev_display_off(struct panel_dev *lcd)
{
	struct dsi_master_ops *ops = lcd_to_master_ops(lcd);
	struct dsi_cmd_packet data_to_send = {0x05, 0x28, 0x00};
	PANEL_DEBUG_MSG("enter %s %d \n",__func__, __LINE__);

	ops->cmd_write(lcd_to_master(lcd), data_to_send);
}

static void panel_dev_power_on(struct mipi_dsim_lcd_device *dsim_dev, int power)
{
	struct board_gpio *rst = &panel->rst;
	PANEL_DEBUG_MSG("enter %s %d \n",__func__, __LINE__);

	msleep(50);
	gpio_direction_output(rst->gpio, 0);
	msleep(10);
	gpio_direction_output(rst->gpio, 1);
	msleep(120);

	panel->power = power;
}

static int panel_dev_read_id(struct mipi_dsim_lcd_device *dsim_dev)
{
	unsigned char buf[3] = {-1,-1,-1};
	struct dsi_master_ops *ops = dsim_dev->master->master_ops;

	struct dsi_cmd_packet data_to_send0 = {0x14, 0xDA, 0x0};
	struct dsi_cmd_packet data_to_send1 = {0x14, 0xDB, 0x0};
	struct dsi_cmd_packet data_to_send2 = {0x14, 0xDC, 0x0};
	int ret = 0;
	int panel_id = 0;

	panel_dev_power_on(dsim_dev, 1);
	ret = ops->cmd_read(dsim_dev->master, data_to_send0, &buf[0], 1);
	ret = ops->cmd_read(dsim_dev->master, data_to_send1, &buf[1], 1);
	ret = ops->cmd_read(dsim_dev->master, data_to_send2, &buf[2], 1);
	panel_dev_power_on(dsim_dev, 0);

	panel_id = (buf[0] << 16) + (buf[1] << 8) + buf[2];
	printk("----------%s, (%d) id[0]: %x id[1]: %x, id[2]: %x, id:%08x \n", __func__, __LINE__, buf[0], buf[1], buf[2], panel_id);

	if(panel_id == PANEL_rm690b0_ID)
		return 0;
	else
		return -1;
}

static int panel_dev_panel_init(struct panel_dev *lcd)
{
	int ret = 0;
	struct dsi_master_ops *ops = lcd_to_master_ops(lcd);
	struct dsi_device *dsi = lcd_to_master(lcd);
	int  i;

	for (i = 0; i < ARRAY_SIZE(fitipower_rm690b0_450_600_1lane_cmd_list); i++)
	{
		ret = ops->cmd_write(dsi,fitipower_rm690b0_450_600_1lane_cmd_list[i]);
		if(ret < 0)
			return ret;
		msleep(10);
	}

	return ret;
}

static int panel_dev_set_sequence(struct mipi_dsim_lcd_device *dsim_dev)
{
	int ret = 0;
	struct rm690b0 *lcd = dev_get_drvdata(&dsim_dev->dev);
	PANEL_DEBUG_MSG("enter %s %d \n",__func__, __LINE__);

	ret = panel_dev_panel_init(panel);
	panel_dev_sleep_out(panel);
	msleep(120);
	panel_dev_display_on(panel);
	msleep(5);

	lcd->power = FB_BLANK_UNBLANK;
	return ret;
}


static struct fb_videomode panel_modes = {
		.name = "fitipower_rm690b0-lcd",
		.xres = 450,
		.yres = 600,

		.refresh = 60,
		.left_margin = 20,	//hbp
		.right_margin = 10,	//hfp
		.hsync_len = 10,	//hsync

		.upper_margin = 2,	//vbp
		.lower_margin = 4,	//vfp
		.vsync_len = 2,	//vsync
		.sync                   = FB_SYNC_HOR_HIGH_ACT & FB_SYNC_VERT_HIGH_ACT,
		.vmode                  = FB_VMODE_NONINTERLACED,
		.flag = 0,
};

static struct jzdsi_data jzdsi_pdata = {
		.modes = &panel_modes,
		.video_config.no_of_lanes = 1,
		.video_config.virtual_channel = 0,
		.video_config.color_coding = COLOR_CODE_24BIT,
		.video_config.video_mode = VIDEO_BURST_WITH_SYNC_PULSES,
		.video_config.receive_ack_packets = 1,	/* enable receiving of ack packets */
		.video_config.is_18_loosely = 0,
		.video_config.data_en_polarity = 1,
		.video_config.byte_clock = 0, // driver will auto calculate byte_clock.
		.video_config.byte_clock_coef = MIPI_PHY_BYTE_CLK_COEF_MUL3_DIV2, // byte_clock *3/2.
		.video_config.lane0_pn_swap = 0,
		.video_config.lane1_pn_swap = 0,
		.video_config.lane2_pn_swap = 0,
		.video_config.lane3_pn_swap = 0,
		.video_config.clk_lane_pn_swap = 0,

		.dsi_config.max_lanes = 1,
		.dsi_config.max_hs_to_lp_cycles = 100,
		.dsi_config.max_lp_to_hs_cycles = 40,
		.dsi_config.max_bta_cycles = 4095,
		.dsi_config.color_mode_polarity = 1,
		.dsi_config.shut_down_polarity = 1,
		.dsi_config.max_bps = 2750,
		.bpp_info = 24,
};

static struct tft_config rm690b0_cfg = {
	.pix_clk_inv = 0,
	.de_dl = 0,
	.sync_dl = 0,
	.color_even = TFT_LCD_COLOR_EVEN_RGB,
	.color_odd = TFT_LCD_COLOR_ODD_RGB,
	.mode = TFT_LCD_MODE_PARALLEL_888,
};

static struct lcd_panel lcd_panel = {
	.num_modes = 1,
	.modes = &panel_modes,
	.dsi_pdata = &jzdsi_pdata,

	.lcd_type = LCD_TYPE_MIPI_TFT,
	.tft_config = &rm690b0_cfg,
	.bpp = 24,
	.width = 48,
	.height = 63,
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
* @ pannel_rm690b0_lcd_ops, register to kernel common backlight/lcd.c framworks.
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

	panel->rst.gpio = of_get_named_gpio_flags(np, "ingenic,rst-gpio", 0, &flags);
	if(gpio_is_valid(panel->rst.gpio)) {
		panel->rst.active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		ret = gpio_request_one(panel->rst.gpio, GPIOF_DIR_OUT, "rst");
		if(ret < 0) {
			dev_err(dev, "Failed to request rst pin!\n");
			return ret;
		}
		gpio_direction_output(panel->rst.gpio, panel->rst.active_level);
	} else {
		dev_warn(dev, "invalid gpio rst.gpio: %d\n", panel->rst.gpio);
	}

	return 0;
}

static int panel_dev_probe(struct mipi_dsim_lcd_device *dsim_dev)
{
	struct rm690b0 *lcd;

	lcd = devm_kzalloc(&dsim_dev->dev, sizeof(struct rm690b0), GFP_KERNEL);
	if (!lcd)
	{
		dev_err(&dsim_dev->dev, "failed to allocate fitipower_rm690b0 structure.\n");
		return -ENOMEM;
	}

	lcd->dsim_dev = dsim_dev;
	lcd->dev = &dsim_dev->dev;

	lcd->ld = lcd_device_register("fitipower_rm690b0", lcd->dev, lcd,
	                              &panel_lcd_ops);
	if (IS_ERR(lcd->ld))
	{
		dev_err(lcd->dev, "failed to register lcd ops.\n");
		return PTR_ERR(lcd->ld);
	}

	dev_set_drvdata(&dsim_dev->dev, lcd);


	dev_dbg(lcd->dev, "probed fitipower_rm690b0 panel driver.\n");


	panel->dsim_dev = dsim_dev;


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
	查询目标状态，反应屏幕当前的工作状态
	返回值0: 代表工作正常
	返回值-值: 代表工作异常，需要控制器端进行干预，恢复或者重新初始化屏幕.

	这里查询 屏幕ID仅作示例,
*/
static int panel_query_status(struct mipi_dsim_lcd_device *dsim_dev)
{
	unsigned char buf[3] = {-1,-1,-1};
	struct dsi_master_ops *ops = dsim_dev->master->master_ops;

	struct dsi_cmd_packet data_to_send0 = {0x14, 0xDA, 0x0};
	struct dsi_cmd_packet data_to_send1 = {0x14, 0xDB, 0x0};
	struct dsi_cmd_packet data_to_send2 = {0x14, 0xDC, 0x0};
	int ret = 0;
	int panel_id = 0;

	ret = ops->cmd_read(dsim_dev->master, data_to_send0, &buf[0], 1);
	ret = ops->cmd_read(dsim_dev->master, data_to_send1, &buf[1], 1);
	ret = ops->cmd_read(dsim_dev->master, data_to_send2, &buf[2], 1);

	panel_id = (buf[0] << 16) + (buf[1] << 8) + buf[2];
	printk("panel_id: %x\n", panel_id);


	if(panel_id == PANEL_rm690b0_ID) {
		return 0;
	} else {
		return -EINVAL;
	}
}

/*
	set_display_on 接口，用于控制屏幕显示和关闭，仅作调试使用.
*/
static int panel_set_display_on(struct mipi_dsim_lcd_device *dsim_dev, int on)
{
	if(on) {
		panel_dev_display_on(panel);
	} else {
		panel_dev_display_off(panel);
	}

	return 0;
}

static struct mipi_dsim_lcd_driver panel_dev_dsim_ddi_driver = {
	.name = "fitipower_rm690b0-lcd",
	.id = -1,

	.power_on = panel_dev_power_on,
	.set_sequence = panel_dev_set_sequence,
	.probe = panel_dev_probe,
	.suspend = panel_suspend,
	.resume = panel_resume,
	.probe_dev_id = panel_dev_read_id,
	.query_status = panel_query_status,
	.set_display_on = panel_set_display_on,
};

#ifdef CONFIG_PM
static int panel_pm_suspend(struct device *dev)
{
	panel_dev_display_off(panel);
	panel_dev_sleep_in(panel);
	return 0;
}

static int panel_pm_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops panel_pm_ops = {
	.suspend = panel_pm_suspend,
	.resume = panel_pm_resume,
};
#endif

static struct mipi_dsim_lcd_device panel_dev_device={
	.name		= "fitipower_rm690b0-lcd",
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
	if(gpio_is_valid(panel->rst.gpio))
		gpio_free(panel->rst.gpio);

	kfree(panel);
	return ret;
}

static int panel_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id panel_of_match[] = {
	{ .compatible = "ingenic,rm690b0", },
	{},
};

static struct platform_driver panel_driver = {
	.probe		= panel_probe,
	.remove		= panel_remove,
	.driver		= {
		.name	= "rm690b0",
		.of_match_table = panel_of_match,
#ifdef CONFIG_PM
		.pm = &panel_pm_ops,
#endif
	},
};

module_platform_driver(panel_driver);

MODULE_LICENSE("GPL");
