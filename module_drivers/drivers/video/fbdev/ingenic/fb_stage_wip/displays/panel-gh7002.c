/*
 * driver/video/fbdev/ingenic/x2000_v12/displays/panel-gh7002.c
 * Copyright (C) 2016 Ingenic Semiconductor Inc.
 * This program is free software, you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#if 0
&dpu {
	status = "okay";
	ingenic,disable-rdma-fb = <1>;
	ingenic,compfb_wback_out = <0>;
	ingenic,rot_angle = <0>;
	/*Defines the init state of composer fb export infomations.*/
	ingenic,layer-exported = <1 0 0 0>;
	ingenic,layer-frames   = <2 2 2 2>;
	ingenic,layer-framesize = <1024 600>, <1024 600>, <320 240>, <320 240>;   /*Max framesize for each layer.*/
	layer,color_mode	= <0 0 0 0>;					/*src fmt,*/
	layer,src-size       	= <1024 600>, <1024 600>, <320 240>, <240 200>;	/*Layer src size should smaller than framesize*/
	layer,target-size	= <1024 600>, <1024 600>, <160 240>, <240 200>;	/*Target Size should smaller than src_size.*/
	layer,target-pos	= <0 0>, <0 640>, <340 480>, <100 980>;	/*target pos , the start point of the target panel.*/
	layer,enable		= <1 0 0 0>;					/*layer enabled or disabled.*/
	ingenic,logo-pan-layer	= <0>;						/*on which layer should init logo pan on.*/
	memory-region=<&reserved_memory>;
	port {
		dpu_out_ep: endpoint {
			remote-endpoint = <&panel_gh7002_ep>;
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
		panel_gh7002 {
			compatible = "ingenic,gh7002";
			status = "okay";
			pinctrl-names = "default";
			ingenic,rst-gpio = <&gpe 9 GPIO_ACTIVE_HIGH INGENIC_GPIO_NOBIAS>;
			port {
				panel_gh7002_ep: endpoint {
					remote-endpoint = <&dpu_out_ep>;
				};
			};
		};
	};

	backlight {
		compatible = "pwm-backlight";
	        pinctrl-names = "default";
	        pinctrl-0 = <&pwm13_pc>;
		pwms = <&pwm 13 1000000>; /* arg1: pwm channel id [0~15]. arg2: period in ns. */
		brightness-levels = <0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15>;
		default-brightness-level = <10>;
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



static char panel_gh7002_debug = 0;
#define PANEL_DEBUG_MSG(msg...)			\
	do {					\
		if (panel_gh7002_debug)		\
			printk(">>>>>>>>>>>>>>>>PANEL gh7002: " msg);		\
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

static struct panel_dev *panel;

#define lcd_to_master(a)	(a->dsim_dev->master)
#define lcd_to_master_ops(a)	((lcd_to_master(a))->master_ops)

#define PANEL_gh7002_ID	(0x000000)

struct gh7002 {
	struct device *dev;
	unsigned int power;
	unsigned int id;

	struct lcd_device *ld;
	struct backlight_device *bd;

	struct mipi_dsim_lcd_device *dsim_dev;

};

static struct dsi_cmd_packet fitipower_gh7002_1024_600_4lane_cmd_list[] = {
	{0x39,0x02,0x00,{0xee,0x01}}, // ENTER PAGE1
	{0x39,0x02,0x00,{0xea,0x07}},
	{0x39,0x02,0x00,{0xeb,0x12}},
	{0x39,0x02,0x00,{0x0a,0x50}}, // vcom,默认值为0X21
	{0x39,0x02,0x00,{0x13,0x14}},
	{0x39,0x02,0x00,{0x17,0x35}},  // vddd
	{0x39,0x02,0x00,{0x1d,0x44}},  // vghpump  33
	{0x39,0x02,0x00,{0x21,0x01}},
	{0x39,0x02,0x00,{0x28,0x2a}}, //vgh  2A  30  25
	{0x39,0x02,0x00,{0x29,0x0D}}, //vgl 0C  14   24
	{0x39,0x02,0x00,{0x2a,0x63}},  // 63
	{0x39,0x02,0x00,{0x2f,0xf3}},   // pwr en
	{0x39,0x02,0x00,{0x45,0x10}},  //bit4(l/r), BIT5(UP/DN),
	{0x39,0x02,0x00,{0x05,0x12}}, //OSC
	{0x39,0x02,0x00,{0xee,0x02}}, // ENTER PAGE2
	{0x39,0x02,0x00,{0x39,0x90}}, //VSPNR
	{0x39,0x02,0x00,{0x00,0x00}},
	{0x39,0x02,0x00,{0x01,0x0d}},
	{0x39,0x02,0x00,{0x02,0x12}},//8
	{0x39,0x02,0x00,{0x03,0x08}},//12
	{0x39,0x02,0x00,{0x04,0x0E}},//28
	{0x39,0x02,0x00,{0x05,0x30}},//52
	{0x39,0x02,0x00,{0x06,0x0B}},//76
	{0x39,0x02,0x00,{0x07,0x0D}},//100
	{0x39,0x02,0x00,{0x08,0x0E}},//131
	{0x39,0x02,0x00,{0x09,0x0D}},//155
	{0x39,0x02,0x00,{0x0A,0x10}},//179
	{0x39,0x02,0x00,{0x0b,0x4D}},//203
	{0x39,0x02,0x00,{0x0c,0x13}},//227
	{0x39,0x02,0x00,{0x0d,0x19}},//243
	{0x39,0x02,0x00,{0x0e,0x2e}},//247
	{0x39,0x02,0x00,{0x0f,0x33}},//251
	{0x39,0x02,0x00,{0x10,0x3F}},//255
	{0x39,0x02,0x00,{0x20,0x00}},
	{0x39,0x02,0x00,{0x21,0x0d}},
	{0x39,0x02,0x00,{0x22,0x12}},
	{0x39,0x02,0x00,{0x23,0x08}},
	{0x39,0x02,0x00,{0x24,0x0E}},
	{0x39,0x02,0x00,{0x25,0x30}},
	{0x39,0x02,0x00,{0x26,0x0B}},
	{0x39,0x02,0x00,{0x27,0x0D}},
	{0x39,0x02,0x00,{0x28,0x0E}},
	{0x39,0x02,0x00,{0x29,0x0D}},
	{0x39,0x02,0x00,{0x2A,0x10}},
	{0x39,0x02,0x00,{0x2b,0x4D}},
	{0x39,0x02,0x00,{0x2c,0x13}},
	{0x39,0x02,0x00,{0x2d,0x19}},
	{0x39,0x02,0x00,{0x2e,0x2e}},
	{0x39,0x02,0x00,{0x2f,0x33}},
	{0x39,0x02,0x00,{0x30,0x3F}},
	{0x39,0x02,0x00,{0xee,0x04}}, // ENTER PAGE4
	{0x39,0x02,0x00,{0x00,0x05}}, // 05=512  SOURCE chane
	{0x39,0x02,0x00,{0x01,0x01}}, //  GAT
	{0x39,0x02,0x00,{0x02,0x2C}}, //  GAT
	{0x39,0x02,0x00,{0x03,0x04}}, // SOURCE H
	{0x39,0x02,0x00,{0x04,0x00}}, // SOURCE H
	{0x39,0x02,0x00,{0x06,0x06}},
	{0x39,0x02,0x00,{0x07,0x05}}, // sstp
	{0x39,0x02,0x00,{0x08,0x1A}},
	{0x39,0x02,0x00,{0x09,0x20}},  // pol
	{0x39,0x02,0x00,{0x0a,0x0A}},  //+-1 或 +-2
	{0x39,0x02,0x00,{0x0b,0x00}},
	{0x39,0x02,0x00,{0x0f,0x0a}},
	{0x39,0x02,0x00,{0x20,0x40}}, //old=00
	{0x39,0x02,0x00,{0x24,0x08}},
	{0x39,0x02,0x00,{0x40,0x80}}, //80 bist=00
	{0x39,0x02,0x00,{0x41,0x55}},
	{0x39,0x02,0x00,{0xee,0x05}}, // ENTER PAGE5
	{0x39,0x02,0x00,{0x00,0x01}}, //Stva
	{0x39,0x02,0x00,{0x01,0x05}},
	{0x39,0x02,0x00,{0x02,0x15}}, //??
	{0x39,0x02,0x00,{0x03,0x05}},
	{0x39,0x02,0x00,{0x07,0xEB}}, //Stvb
	{0x39,0x02,0x00,{0x08,0xEF}},
	{0x39,0x02,0x00,{0x09,0x44}},//CLK
	{0x39,0x02,0x00,{0x10,0x03}}, //CLKA
	{0x39,0x02,0x00,{0x11,0x07}},
	{0x39,0x02,0x00,{0x12,0x55}},
	{0x39,0x02,0x00,{0x13,0x05}},
	{0x39,0x02,0x00,{0x19,0xBB}},
	{0x39,0x02,0x00,{0x1a,0x74}},
	{0x39,0x02,0x00,{0x30,0x01}},
	{0x39,0x02,0x00,{0x31,0x01}},
	{0x39,0x02,0x00,{0x32,0x00}},
	{0x39,0x02,0x00,{0x33,0x14}},
	{0x39,0x02,0x00,{0x34,0x14}},
	{0x39,0x02,0x00,{0x35,0x78}},
	{0x39,0x02,0x00,{0x36,0x01}},
	{0x39,0x02,0x00,{0x37,0x01}},
	{0x39,0x02,0x00,{0x38,0x00}},
	{0x39,0x02,0x00,{0x39,0x14}},
	{0x39,0x02,0x00,{0x3A,0x14}},
	{0x39,0x02,0x00,{0x40,0x55}},   //
	{0x39,0x02,0x00,{0x41,0x00}},
	{0x39,0x02,0x00,{0x43,0x11}},
	{0x39,0x02,0x00,{0x44,0x01}},
	{0x39,0x02,0x00,{0x45,0x81}},
	{0x39,0x02,0x00,{0x46,0x06}},
	{0x39,0x02,0x00,{0x47,0x00}},
	{0x39,0x02,0x00,{0xee,0x06}}, //PAGE6  GIP back
	{0x39,0x02,0x00,{0x00,0x01}},
	{0x39,0x02,0x00,{0x02,0x45}},
	{0x39,0x02,0x00,{0x06,0xcd}},
	{0x39,0x02,0x00,{0x08,0x67}},
	{0x39,0x02,0x00,{0x09,0x45}},
	{0x39,0x02,0x00,{0x0a,0x23}},
	{0x39,0x02,0x00,{0x0b,0x01}},
	{0x39,0x02,0x00,{0xee,0x07}}, //PAGE7
	{0x39,0x02,0x00,{0x00,0x01}},
	{0x39,0x02,0x00,{0x01,0x05}},
	{0x39,0x02,0x00,{0x02,0x0c}},
	{0x39,0x02,0x00,{0x03,0x0d}},
	{0x39,0x02,0x00,{0x04,0x3c}},
	{0x39,0x02,0x00,{0x05,0x3c}},
	{0x39,0x02,0x00,{0x06,0x21}},
	{0x39,0x02,0x00,{0x07,0x20}},
	{0x39,0x02,0x00,{0x08,0x12}},
	{0x39,0x02,0x00,{0x09,0x12}},
	{0x39,0x02,0x00,{0x0A,0x10}},
	{0x39,0x02,0x00,{0x0b,0x10}},
	{0x39,0x02,0x00,{0x0c,0x16}},
	{0x39,0x02,0x00,{0x0d,0x16}},
	{0x39,0x02,0x00,{0x0e,0x14}},
	{0x39,0x02,0x00,{0x0f,0x14}},
	{0x39,0x02,0x00,{0x10,0x3c}},
	{0x39,0x02,0x00,{0x11,0x3c}},
	{0x39,0x02,0x00,{0x12,0x3c}},
	{0x39,0x02,0x00,{0x13,0x3c}},
	{0x39,0x02,0x00,{0x14,0x3c}},
	{0x39,0x02,0x00,{0x15,0x3c}},
	{0x39,0x02,0x00,{0x20,0x00}},
	{0x39,0x02,0x00,{0x21,0x04}},
	{0x39,0x02,0x00,{0x22,0x0c}},
	{0x39,0x02,0x00,{0x23,0x0d}},
	{0x39,0x02,0x00,{0x24,0x3c}},
	{0x39,0x02,0x00,{0x25,0x3c}},
	{0x39,0x02,0x00,{0x26,0x21}},
	{0x39,0x02,0x00,{0x27,0x20}},
	{0x39,0x02,0x00,{0x28,0x13}},
	{0x39,0x02,0x00,{0x29,0x13}},
	{0x39,0x02,0x00,{0x2A,0x11}},
	{0x39,0x02,0x00,{0x2b,0x11}},
	{0x39,0x02,0x00,{0x2c,0x17}},
	{0x39,0x02,0x00,{0x2d,0x17}},
	{0x39,0x02,0x00,{0x2e,0x15}},
	{0x39,0x02,0x00,{0x2f,0x15}},
	{0x39,0x02,0x00,{0x30,0x3c}},
	{0x39,0x02,0x00,{0x31,0x3c}},
	{0x39,0x02,0x00,{0x32,0x3c}},
	{0x39,0x02,0x00,{0x33,0x3c}},
	{0x39,0x02,0x00,{0x34,0x3c}},
	{0x39,0x02,0x00,{0x35,0x3c}},
	{0x39,0x02,0x00,{0xee,0x08}}, //PAGE8
	{0x39,0x02,0x00,{0x10,0x00}},
	{0x39,0x02,0x00,{0x12,0xdA}}, //VDDH
	{0x39,0x02,0x00,{0x13,0x1c}}, //
	{0x39,0x02,0x00,{0x17,0x23}}, //
	{0x39,0x02,0x00,{0x18,0x30}},
	{0x39,0x02,0x00,{0x20,0x80}},  //auto lane
	{0x39,0x02,0x00,{0xee,0x0f}}, //PAGEf
	{0x39,0x02,0x00,{0x00,0x01}}, // dual gate en
	{0x39,0x02,0x00,{0x01,0x10}},
	{0x39,0x02,0x00,{0xee,0x04}}, //PAGE4
	{0x39,0x02,0x00,{0x23,0x38}},	//不需下11/29
	{0x39,0x02,0x00,{0xee,0x00}}, // ENTER PAGE0
	{0x39,0x02,0x00,{0xea,0x00}},
	{0x39,0x02,0x00,{0xeb,0x00}},
	{0x39,0x02,0x00,{0x36,0x02}},  //00
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

	if(panel_id == PANEL_gh7002_ID)
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
	int num_of_param;
	struct dsi_cmd_packet *param_list = NULL;

	param_list = &fitipower_gh7002_1024_600_4lane_cmd_list[0];
	num_of_param = ARRAY_SIZE(fitipower_gh7002_1024_600_4lane_cmd_list);

	for (i = 0; i < num_of_param; i++)
	{
		ret = ops->cmd_write(dsi,  param_list[i]);
		if (ret < 0)
			return ret;
	}
	return ret;
}

static int panel_dev_set_sequence(struct mipi_dsim_lcd_device *dsim_dev)
{
	int ret = 0;
	struct gh7002 *lcd = dev_get_drvdata(&dsim_dev->dev);
	PANEL_DEBUG_MSG("enter %s %d \n",__func__, __LINE__);

	ret = panel_dev_panel_init(panel);
	panel_dev_sleep_out(panel);
	msleep(120);
	panel_dev_display_on(panel);
	msleep(20);

	lcd->power = FB_BLANK_UNBLANK;
	return ret;
}


static struct fb_videomode panel_modes[] = {
	[0] = {
		.name = "fitipower_gh7002-lcd",
		.xres = 1024,
		.yres = 600,

		.refresh = 60,
		.left_margin = 80,	//hbp
		.right_margin = 120,	//hfp
		.hsync_len = 80,	//hsync

		.upper_margin = 8,	//vbp
		.lower_margin = 16,	//vfp
		.vsync_len = 8,	//vsync
		.sync                   = FB_SYNC_HOR_HIGH_ACT & FB_SYNC_VERT_HIGH_ACT,
		.vmode                  = FB_VMODE_NONINTERLACED,
		.flag = 0,
	},
};

static struct jzdsi_data jzdsi_pdata[] = {
	[0] = {
		.modes = &panel_modes[0],
		.video_config.no_of_lanes = 4,
		.video_config.virtual_channel = 0,
		.video_config.color_coding = COLOR_CODE_24BIT,
		.video_config.video_mode = VIDEO_BURST_WITH_SYNC_PULSES,
		.video_config.receive_ack_packets = 1,	/* enable receiving of ack packets */
		.video_config.is_18_loosely = 0,
		.video_config.data_en_polarity = 1,
		.video_config.byte_clock = 0, // driver will auto calculate byte_clock.
		.video_config.byte_clock_coef = MIPI_PHY_BYTE_CLK_COEF_MUL6_DIV5, // byte_clock *6/5.
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

static struct tft_config gh7002_cfg = {
	.pix_clk_inv = 0,
	.de_dl = 0,
	.sync_dl = 0,
	.color_even = TFT_LCD_COLOR_EVEN_RGB,
	.color_odd = TFT_LCD_COLOR_ODD_RGB,
	.mode = TFT_LCD_MODE_PARALLEL_888,
};

static struct lcd_panel lcd_panel = {
	.num_modes = ARRAY_SIZE(panel_modes),
	.modes = panel_modes,
	.dsi_pdata = jzdsi_pdata,

	.lcd_type = LCD_TYPE_MIPI_TFT,
	.tft_config = &gh7002_cfg,
	.bpp = 24,
	.width = 154,
	.height = 85,
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
* @ pannel_gh7002_lcd_ops, register to kernel common backlight/lcd.c framworks.
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
		gpio_direction_output(panel->rst.gpio, !panel->rst.active_level);
	} else {
		dev_warn(dev, "invalid gpio rst.gpio: %d\n", panel->rst.gpio);
	}
#if 0
	panel->vdd_en.gpio = of_get_named_gpio_flags(np, "ingenic,vdd-en-gpio", 0, &flags);
	if(gpio_is_valid(panel->vdd_en.gpio)) {
		panel->vdd_en.active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		ret = gpio_request_one(panel->vdd_en.gpio, GPIOF_DIR_OUT, "vdd_en");
		if(ret < 0) {
			dev_err(dev, "Failed to request vdd_en pin!\n");
			goto err_request_lcd_vdd;
		}
		gpio_direction_output(panel->vdd_en.gpio, panel->vdd_en.active_level);
	} else {
		dev_warn(dev, "invalid gpio vdd_en.gpio: %d\n", panel->vdd_en.gpio);
	}

	 panel->lcd_pwm.gpio = of_get_named_gpio_flags(np, "ingenic,lcd-pwm-gpio", 0, &flags);
	 if(gpio_is_valid(panel->lcd_pwm.gpio)) {
	 	panel->lcd_pwm.active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
	 	ret = gpio_request_one(panel->lcd_pwm.gpio, GPIOF_DIR_OUT, "lcd-pwm");
	 	if(ret < 0) {
	 		dev_err(dev, "Failed to request lcd-pwm pin!\n");
	 		goto err_request_lcd_pwm;
	 	}
	 } else {
	 	dev_warn(dev, "invalid gpio lcd-pwm.gpio: %d\n", panel->lcd_pwm.gpio);
	 }
#endif
	return 0;

#if 0
err_request_lcd_pwm:
	if(gpio_is_valid(panel->vdd_en.gpio))
		gpio_free(panel->vdd_en.gpio);
err_request_lcd_vdd:
	if(gpio_is_valid(panel->rst.gpio))
		gpio_free(panel->rst.gpio);
	return ret;
#endif
}

static int panel_dev_probe(struct mipi_dsim_lcd_device *dsim_dev)
{
	struct gh7002 *lcd;

	lcd = devm_kzalloc(&dsim_dev->dev, sizeof(struct gh7002), GFP_KERNEL);
	if (!lcd)
	{
		dev_err(&dsim_dev->dev, "failed to allocate fitipower_gh7002 structure.\n");
		return -ENOMEM;
	}

	lcd->dsim_dev = dsim_dev;
	lcd->dev = &dsim_dev->dev;

	lcd->ld = lcd_device_register("fitipower_gh7002", lcd->dev, lcd,
	                              &panel_lcd_ops);
	if (IS_ERR(lcd->ld))
	{
		dev_err(lcd->dev, "failed to register lcd ops.\n");
		return PTR_ERR(lcd->ld);
	}

	dev_set_drvdata(&dsim_dev->dev, lcd);


	dev_dbg(lcd->dev, "probed fitipower_gh7002 panel driver.\n");


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


	if(panel_id == PANEL_gh7002_ID) {
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
	.name = "fitipower_gh7002-lcd",
	.id = -1,

	.power_on = panel_dev_power_on,
	.set_sequence = panel_dev_set_sequence,
	.probe = panel_dev_probe,
	.suspend = panel_suspend,
	.resume = panel_resume,
//	.probe_dev_id = panel_dev_read_id,
//	.query_status = panel_query_status,
	.set_display_on = panel_set_display_on,
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

static struct mipi_dsim_lcd_device panel_dev_device={
	.name		= "fitipower_gh7002-lcd",
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
	{ .compatible = "ingenic,gh7002", },
	{},
};

static struct platform_driver panel_driver = {
	.probe		= panel_probe,
	.remove		= panel_remove,
	.driver		= {
	lane2_pn_swap;
	lane3_pn_swap;
		.name	= "gh7002",
		.of_match_table = panel_of_match,
#ifdef CONFIG_PM
		.pm = &panel_pm_ops,
#endif
	},
};

module_platform_driver(panel_driver);

MODULE_LICENSE("GPL");
