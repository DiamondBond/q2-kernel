/*
 * driver/video/fbdev/ingenic/x2000_v12/displays/panel-pj8801h01.c
 * Copyright (C) 2016 Ingenic Semiconductor Inc.
 * This program is free software, you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
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
#include <linux/fb.h>
#include <linux/backlight.h>

#include "../ingenicfb.h"
#include "../jz_dsim.h"

static char panel_pj8801h01_debug = 0;
#define PANEL_DEBUG_MSG(msg...)			\
	do {					\
		if (panel_pj8801h01_debug)		\
			printk(">>>>>>>>>>>>>>>>PANEL pj8801h01: " msg);		\
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

	struct board_gpio lcd_en;
	struct board_gpio lcd_rst;
	struct board_gpio lcd_avdd_en;
	struct board_gpio lcd_vcc_en;
	struct board_gpio lcd_te;

	struct mipi_dsim_lcd_device *dsim_dev;
};

static struct panel_dev *panel;

#define lcd_to_master(a)	(a->dsim_dev->master)
#define lcd_to_master_ops(a)	((lcd_to_master(a))->master_ops)

#define PANEL_PJ8801H01_ID	(0xFFFFFF)

struct pj8801h01 {
	struct device *dev;
	unsigned int power;
	unsigned int id;

	struct lcd_device *ld;
	struct backlight_device *bd;

	struct mipi_dsim_lcd_device *dsim_dev;

};

/* Display Initial Code Setting */
static struct dsi_cmd_packet fitipower_pj8801h01_480_1920_4lane_cmd_list[] = {
	{0x39,0x02,0x00,{0xb0,0x5a}},  // stop reload
                                   //
	{0x39,0x02,0x00,{0xb1,0x04}},  // bank 4
	{0x39,0x02,0x00,{0x00,0x3f}},

	{0x39,0x02,0x00,{0xb1,0x07}},  // bank 7
	{0x39,0x02,0x00,{0x0c,0x0d}},
	{0x39,0x02,0x00,{0x0d,0xca}},

	{0x39,0x02,0x00,{0xb1,0x06}},  // bank 6
	{0x39,0x02,0x00,{0x00,0xf9}},
	{0x39,0x02,0x00,{0x01,0x37}},
	{0x39,0x02,0x00,{0x02,0x73}},
	{0x39,0x02,0x00,{0x03,0xb3}},
	{0x39,0x02,0x00,{0x04,0x29}},
	{0x39,0x02,0x00,{0x05,0x80}},
	{0x39,0x02,0x00,{0x06,0x07}},
	{0x39,0x02,0x00,{0x07,0x8a}},
	{0x39,0x02,0x00,{0x08,0x8d}},
	{0x39,0x02,0x00,{0x09,0xbf}},
	{0x39,0x02,0x00,{0x0a,0x69}},
	{0x39,0x02,0x00,{0x0b,0xfe}},

	{0x39,0x02,0x00,{0xb1,0x08}}, // bank 8
	{0x39,0x02,0x00,{0x00,0xde}},
	{0x39,0x02,0x00,{0x01,0x80}},
	{0x39,0x02,0x00,{0x02,0x80}},
	{0x39,0x02,0x00,{0x03,0xf0}},
	{0x39,0x02,0x00,{0x04,0xd9}},
	{0x39,0x02,0x00,{0x05,0xc8}},
	{0x39,0x02,0x00,{0x06,0xba}},
	{0x39,0x02,0x00,{0x07,0xaf}},
	{0x39,0x02,0x00,{0x08,0xa6}},
	{0x39,0x02,0x00,{0x09,0x9e}},
	{0x39,0x02,0x00,{0x0a,0x98}},
	{0x39,0x02,0x00,{0x0b,0x92}},
	{0x39,0x02,0x00,{0x0c,0x8d}},
	{0x39,0x02,0x00,{0x0d,0x88}},
	{0x39,0x02,0x00,{0x0e,0x84}},
	{0x39,0x02,0x00,{0x0f,0xff}},
	{0x39,0x02,0x00,{0x10,0x6f}},
	{0x39,0x02,0x00,{0x11,0xe6}},
	{0x39,0x02,0x00,{0x12,0xf0}},
	{0x39,0x02,0x00,{0x13,0xff}},
	{0x39,0x02,0x00,{0x14,0xcc}},
	{0x39,0x02,0x00,{0x15,0x0c}},
	{0x39,0x02,0x00,{0x16,0xff}},
	{0x39,0x02,0x00,{0x17,0x6f}},
	{0x39,0x02,0x00,{0x18,0xa6}},
	{0x39,0x02,0x00,{0x19,0xf0}},
	{0x39,0x02,0x00,{0x1a,0xff}},
	{0x39,0x02,0x00,{0x1b,0x0f}},
	{0x39,0x02,0x00,{0x1c,0x00}},
	{0x39,0x02,0x00,{0x1d,0x0f}},
	{0x39,0x02,0x00,{0x1e,0x00}},
	{0x39,0x02,0x00,{0x1f,0xfc}},
	{0x39,0x02,0x00,{0x20,0xff}},

	{0x39,0x02,0x00,{0xB1,0x09}}, // bank 9
	{0x39,0x02,0x00,{0x00,0x35}},
	{0x39,0x02,0x00,{0x01,0x4C}},
	{0x39,0x02,0x00,{0x02,0x9E}},
	{0x39,0x02,0x00,{0x03,0xA6}},
	{0x39,0x02,0x00,{0x04,0x00}},
	{0x39,0x02,0x00,{0x05,0x9D}},
	{0x39,0x02,0x00,{0x06,0x00}},
	{0x39,0x02,0x00,{0x07,0x00}},
	{0x39,0x02,0x00,{0x08,0xB8}},
	{0x39,0x02,0x00,{0x09,0xDD}},
	{0x39,0x02,0x00,{0x0A,0x6E}},
	{0x39,0x02,0x00,{0x0B,0x37}},
	{0x39,0x02,0x00,{0x0C,0x00}},
	{0x39,0x02,0x00,{0x0D,0x00}},
	{0x39,0x02,0x00,{0x0E,0x00}},
	{0x39,0x02,0x00,{0x0F,0x90}},
	{0x39,0x02,0x00,{0x10,0x91}},
	{0x39,0x02,0x00,{0x11,0xC9}},
	{0x39,0x02,0x00,{0x12,0xC8}},
	{0x39,0x02,0x00,{0x13,0x00}},
	{0x39,0x02,0x00,{0x14,0x00}},
	{0x39,0x02,0x00,{0x15,0x00}},
	{0x39,0x02,0x00,{0x16,0x00}},
	{0x39,0x02,0x00,{0x17,0x37}},
	{0x39,0x02,0x00,{0x18,0x40}},
	{0x39,0x02,0x00,{0x19,0x00}},
	{0x39,0x02,0x00,{0x1A,0x6E}},
	{0x39,0x02,0x00,{0x1B,0x6E}},
	{0x39,0x02,0x00,{0x1C,0x6E}},
	{0x39,0x02,0x00,{0x1D,0x6E}},
	{0x39,0x02,0x00,{0x1E,0x6E}},
	{0x39,0x02,0x00,{0x1F,0xB7}},
	{0x39,0x02,0x00,{0x20,0xDB}},
	{0x39,0x02,0x00,{0x21,0xED}},
	{0x39,0x02,0x00,{0x22,0xE6}},
	{0x39,0x02,0x00,{0x23,0xE6}},
	{0x39,0x02,0x00,{0x24,0xE6}},
	{0x39,0x02,0x00,{0x25,0x76}},
	{0x39,0x02,0x00,{0x26,0xBB}},
	{0x39,0x02,0x00,{0x27,0xDD}},
	{0x39,0x02,0x00,{0x28,0x6E}},
	{0x39,0x02,0x00,{0x29,0x32}},
	{0x39,0x02,0x00,{0x2A,0x32}},
	{0x39,0x02,0x00,{0x2B,0x32}},
	{0x39,0x02,0x00,{0x2C,0x32}},
	{0x39,0x02,0x00,{0x2D,0x32}},
	{0x39,0x02,0x00,{0x2E,0x99}},
	{0x39,0x02,0x00,{0x2F,0x4C}},
	{0x39,0x02,0x00,{0x30,0x26}},
	{0x39,0x02,0x00,{0x31,0x93}},
	{0x39,0x02,0x00,{0x32,0xC9}},
	{0x39,0x02,0x00,{0x33,0x64}},
	{0x39,0x02,0x00,{0x34,0x32}},
	{0x39,0x02,0x00,{0x35,0x99}},
	{0x39,0x02,0x00,{0x36,0x4C}},
	{0x39,0x02,0x00,{0x37,0x06}},
	{0x39,0x02,0x00,{0x38,0x40}},
	{0x39,0x02,0x00,{0x39,0x0A}},
	{0x39,0x02,0x00,{0x3A,0x0F}},
	{0x39,0x02,0x00,{0x3B,0x0C}},
	{0x39,0x02,0x00,{0x3C,0x82}},
	{0x39,0x02,0x00,{0x3D,0xDE}},
	{0x39,0x02,0x00,{0x3E,0xF9}},
	{0x39,0x02,0x00,{0x3F,0xD6}},
	{0x39,0x02,0x00,{0x40,0x19}},
	{0x39,0x02,0x00,{0x41,0x5F}},
	{0x39,0x02,0x00,{0x42,0x5B}},
	{0x39,0x02,0x00,{0x43,0x23}},
	{0x39,0x02,0x00,{0x44,0x94}},
	{0x39,0x02,0x00,{0x45,0x10}},
	{0x39,0x02,0x00,{0x46,0x42}},
	{0x39,0x02,0x00,{0x47,0x08}},
	{0x39,0x02,0x00,{0x48,0x20}},
	{0x39,0x02,0x00,{0x49,0xE8}},
	{0x39,0x02,0x00,{0x4A,0x8D}},
	{0x39,0x02,0x00,{0x4B,0x2D}},
	{0x39,0x02,0x00,{0x4C,0x95}},
	{0x39,0x02,0x00,{0x4D,0xD0}},
	{0x39,0x02,0x00,{0x4E,0x31}},
	{0x39,0x02,0x00,{0x4F,0x25}},
	{0x39,0x02,0x00,{0x50,0x02}},
	{0x39,0x02,0x00,{0x51,0x09}},
	{0x39,0x02,0x00,{0x52,0x21}},
	{0x39,0x02,0x00,{0x53,0x84}},
	{0x39,0x02,0x00,{0x54,0x00}},
	{0x39,0x02,0x00,{0x55,0x00}},
	{0x39,0x02,0x00,{0x56,0xF0}},
	{0x39,0x02,0x00,{0x57,0x04}},
	{0x39,0x02,0x00,{0x58,0xFD}},
	{0x39,0x02,0x00,{0x59,0xFF}},

	{0x39,0x02,0x00,{0xB1,0x0A}}, // bank 10
	{0x39,0x02,0x00,{0x00,0xC1}},
	{0x39,0x02,0x00,{0x01,0x03}},
	{0x39,0x02,0x00,{0x02,0xF8}},
	{0x39,0x02,0x00,{0x03,0x38}},
	{0x39,0x02,0x00,{0x04,0x01}},
	{0x39,0x02,0x00,{0x05,0xFD}},
	{0x39,0x02,0x00,{0x06,0x15}},
	{0x39,0x02,0x00,{0x07,0x02}},
	{0x39,0x02,0x00,{0x08,0x00}},
	{0x39,0x02,0x00,{0x09,0x60}},
	{0x39,0x02,0x00,{0x0A,0xE2}},
	{0x39,0x02,0x00,{0x0B,0x9F}},
	{0x39,0x02,0x00,{0x0C,0x7F}},
	{0x39,0x02,0x00,{0x0D,0x68}},
	{0x39,0x02,0x00,{0x0E,0xC3}},
	{0x39,0x02,0x00,{0x0F,0x3F}},
	{0x39,0x02,0x00,{0x10,0x07}},
	{0x39,0x02,0x00,{0x11,0xC5}},
	{0x39,0x02,0x00,{0x12,0xC4}},
	{0x39,0x02,0x00,{0x13,0xFE}},
	{0x39,0x02,0x00,{0x14,0x00}},
	{0x39,0x02,0x00,{0x15,0x01}},
	{0x39,0x02,0x00,{0x16,0x00}},
	{0x39,0x02,0x00,{0x17,0x2F}},
	{0x39,0x02,0x00,{0x18,0x00}},
	{0x39,0x02,0x00,{0x19,0xE0}},
	{0x39,0x02,0x00,{0x1A,0x01}},
	{0x39,0x02,0x00,{0x1B,0xFC}},
	{0x39,0x02,0x00,{0x1C,0xAB}},
	{0x39,0x02,0x00,{0x1D,0x74}},
	{0x39,0x02,0x00,{0x1E,0xF0}},
	{0x39,0x02,0x00,{0x1F,0xE0}},
	{0x39,0x02,0x00,{0x20,0x05}},
	{0x39,0x02,0x00,{0x21,0xF0}},
	{0x39,0x02,0x00,{0x22,0x23}},
	{0x39,0x02,0x00,{0x23,0x1E}},
	{0x39,0x02,0x00,{0x24,0xEF}},
	{0x39,0x02,0x00,{0x25,0x20}},


	{0x39,0x02,0x00,{0xB1,0x00}}, // Stop reload off
	{0x39,0x02,0x00,{0xB0,0xA5}}, // Stop reload off
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

static void panel_dev_power_on(struct mipi_dsim_lcd_device *dsim_dev, int power)
{
	struct board_gpio *lcd_en = &panel->lcd_en;
	struct board_gpio *lcd_rst = &panel->lcd_rst;
	struct board_gpio *lcd_avdd_en = &panel->lcd_avdd_en;
	struct board_gpio *lcd_vcc_en = &panel->lcd_vcc_en;
	struct board_gpio *lcd_te = &panel->lcd_te;
	PANEL_DEBUG_MSG("enter %s %d \n",__func__, __LINE__);

    if (power) {
        if (gpio_is_valid(lcd_en->gpio)) {
            gpio_direction_output(lcd_en->gpio, lcd_en->active_level);
        }

        msleep(10);

        if (gpio_is_valid(lcd_vcc_en->gpio)) {
            gpio_direction_output(lcd_vcc_en->gpio, lcd_vcc_en->active_level);
        }

        msleep(20);

        if (gpio_is_valid(lcd_avdd_en->gpio)) {
            gpio_direction_output(lcd_avdd_en->gpio, lcd_avdd_en->active_level);
        }

        if (gpio_is_valid(lcd_te->gpio)) {
            gpio_direction_input(lcd_te->gpio);
        }

        gpio_direction_output(lcd_rst->gpio, lcd_rst->active_level);
        msleep(20);
        gpio_direction_output(lcd_rst->gpio, !lcd_rst->active_level);
        msleep(20);
    }

	panel->power = power;
}

static int panel_dev_read_id(struct mipi_dsim_lcd_device *dsim_dev)
{
	unsigned char buf[3] = {0, 0, 0};
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

	panel_id = (buf[0] << 16) + (buf[1] << 8) + buf[2];
	printk("----------%s, (%d) id[0]: %x id[1]: %x, id[2]: %x, id:%08x \n", __func__, __LINE__, buf[0], buf[1], buf[2], panel_id);

	if(panel_id == PANEL_PJ8801H01_ID)
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

    param_list = &fitipower_pj8801h01_480_1920_4lane_cmd_list[0];
    num_of_param = ARRAY_SIZE(fitipower_pj8801h01_480_1920_4lane_cmd_list);

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
	struct pj8801h01 *lcd = dev_get_drvdata(&dsim_dev->dev);
	PANEL_DEBUG_MSG("enter %s %d \n",__func__, __LINE__);

	ret = panel_dev_panel_init(panel);
	panel_dev_sleep_out(panel);
	msleep(20);

	lcd->power = FB_BLANK_UNBLANK;
	return ret;
}


static struct fb_videomode panel_modes = {
		.name = "fitipower_pj8801h01-lcd",
		.xres = 480,
		.yres = 1920,

		.refresh = 60,
		.left_margin = 30,	//hbp
		.right_margin = 30,	//hfp
		.hsync_len = 30,	//hsync

		.upper_margin = 6,	//vbp
		.lower_margin = 6,	//vfp
		.vsync_len = 6,	//vsync
		.sync                   = FB_SYNC_HOR_HIGH_ACT & FB_SYNC_VERT_HIGH_ACT,
		.vmode                  = FB_VMODE_NONINTERLACED,
		.flag = 0,
};

static struct jzdsi_data jzdsi_pdata = {
		.modes = &panel_modes,
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
};

static struct tft_config pj8801h01_cfg = {
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
	.tft_config = &pj8801h01_cfg,
	.bpp = 24,
	.width = 480,
	.height = 1920,
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
* @ pannel_pj8801h01_lcd_ops, register to kernel common backlight/lcd.c framworks.
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

	panel->lcd_en.gpio = of_get_named_gpio_flags(np, "ingenic,lcd-en-gpio", 0, &flags);
	if(gpio_is_valid(panel->lcd_en.gpio)) {
		panel->lcd_en.active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		ret = gpio_request_one(panel->lcd_en.gpio, GPIOF_DIR_OUT, "lcd_en");
		if(ret < 0) {
			dev_err(dev, "Failed to request lcd_en pin!\n");
			return ret;
		}
		gpio_direction_output(panel->lcd_en.gpio, panel->lcd_en.active_level);
	} else {
		dev_warn(dev, "invalid gpio lcd_en.gpio: %d\n", panel->lcd_en.gpio);
	}

	panel->lcd_rst.gpio = of_get_named_gpio_flags(np, "ingenic,lcd-rst-gpio", 0, &flags);
	if(gpio_is_valid(panel->lcd_rst.gpio)) {
		panel->lcd_rst.active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		ret = gpio_request_one(panel->lcd_rst.gpio, GPIOF_DIR_OUT, "lcd_rst");
		if(ret < 0) {
			dev_err(dev, "Failed to request lcd_rst pin!\n");
			goto err_request_lcd_rst;
		}
		gpio_direction_output(panel->lcd_rst.gpio, !panel->lcd_rst.active_level);
	} else {
		dev_warn(dev, "invalid gpio lcd_rst.gpio: %d\n", panel->lcd_rst.gpio);
	}

	panel->lcd_avdd_en.gpio = of_get_named_gpio_flags(np, "ingenic,lcd-avdd-en-gpio", 0, &flags);
	if(gpio_is_valid(panel->lcd_avdd_en.gpio)) {
		panel->lcd_avdd_en.active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		ret = gpio_request_one(panel->lcd_avdd_en.gpio, GPIOF_DIR_OUT, "lcd_avdd_en");
		if(ret < 0) {
			dev_err(dev, "Failed to request lcd_avdd_en pin!\n");
			goto err_request_lcd_avdd_en;
		}
		gpio_direction_output(panel->lcd_avdd_en.gpio, !panel->lcd_avdd_en.active_level);
	} else {
		dev_warn(dev, "invalid gpio avdd_en.gpio: %d\n", panel->lcd_avdd_en.gpio);
	}

	panel->lcd_vcc_en.gpio = of_get_named_gpio_flags(np, "ingenic,lcd-vcc-en-gpio", 0, &flags);
	if(gpio_is_valid(panel->lcd_vcc_en.gpio)) {
		panel->lcd_vcc_en.active_level = (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		ret = gpio_request_one(panel->lcd_vcc_en.gpio, GPIOF_DIR_OUT, "lcd_vcc_en");
		if(ret < 0) {
			dev_err(dev, "Failed to request lcd_vcc_en pin!\n");
			goto err_request_lcd_vcc_en;
		}
		gpio_direction_output(panel->lcd_vcc_en.gpio, !panel->lcd_vcc_en.active_level);
	} else {
		dev_warn(dev, "invalid gpio lcd_vcc_en.gpio: %d\n", panel->lcd_vcc_en.gpio);
	}


	return 0;

err_request_lcd_vcc_en:
	if(gpio_is_valid(panel->lcd_avdd_en.gpio))
		gpio_free(panel->lcd_avdd_en.gpio);
err_request_lcd_avdd_en:
	if(gpio_is_valid(panel->lcd_rst.gpio))
		gpio_free(panel->lcd_rst.gpio);
err_request_lcd_rst:
	if(gpio_is_valid(panel->lcd_en.gpio))
		gpio_free(panel->lcd_en.gpio);
	return ret;
}

static int panel_dev_probe(struct mipi_dsim_lcd_device *dsim_dev)
{
	struct pj8801h01 *lcd;

	lcd = devm_kzalloc(&dsim_dev->dev, sizeof(struct pj8801h01), GFP_KERNEL);
	if (!lcd)
	{
		dev_err(&dsim_dev->dev, "failed to allocate fitipower_pj8801h01 structure.\n");
		return -ENOMEM;
	}

	lcd->dsim_dev = dsim_dev;
	lcd->dev = &dsim_dev->dev;

	lcd->ld = lcd_device_register("fitipower_pj8801h01", lcd->dev, lcd,
	                              &panel_lcd_ops);
	if (IS_ERR(lcd->ld))
	{
		dev_err(lcd->dev, "failed to register lcd ops.\n");
		return PTR_ERR(lcd->ld);
	}

	dev_set_drvdata(&dsim_dev->dev, lcd);


	dev_dbg(lcd->dev, "probed fitipower_pj8801h01 panel driver.\n");


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

static struct mipi_dsim_lcd_driver panel_dev_dsim_ddi_driver = {
	.name = "fitipower_pj8801h01-lcd",
	.id = -1,

	.power_on = panel_dev_power_on,
	.set_sequence = panel_dev_set_sequence,
	.probe = panel_dev_probe,
	.suspend = panel_suspend,
	.resume = panel_resume,
	.probe_dev_id = panel_dev_read_id,
};

#ifdef CONFIG_PM
static int panel_pm_suspend(struct device *dev)
{
	struct board_gpio *lcd_vcc_en = &panel->lcd_vcc_en;
	struct board_gpio *lcd_avdd_en = &panel->lcd_avdd_en;

	panel_dev_sleep_in(panel);
	gpio_direction_output(lcd_vcc_en->gpio, !lcd_vcc_en->active_level);
	gpio_direction_output(lcd_avdd_en->gpio, !lcd_avdd_en->active_level);

	return 0;
}

static int panel_pm_resume(struct device *dev)
{
	struct board_gpio *lcd_vcc_en = &panel->lcd_vcc_en;
	struct board_gpio *lcd_avdd_en = &panel->lcd_avdd_en;

	gpio_direction_output(lcd_vcc_en->gpio, lcd_vcc_en->active_level);
	gpio_direction_output(lcd_avdd_en->gpio, lcd_avdd_en->active_level);

	return 0;
}

static const struct dev_pm_ops panel_pm_ops = {
	.suspend = panel_pm_suspend,
	.resume = panel_pm_resume,
};
#endif

static struct mipi_dsim_lcd_device panel_dev_device={
	.name		= "fitipower_pj8801h01-lcd",
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

    dev_info(&pdev->dev, "panel probe success\n");

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
	{ .compatible = "ingenic,pj8801h01", },
	{},
};

static struct platform_driver panel_driver = {
	.probe		= panel_probe,
	.remove		= panel_remove,
	.driver		= {
		.name	= "pj8801h01",
		.of_match_table = panel_of_match,
#ifdef CONFIG_PM
		.pm = &panel_pm_ops,
#endif
	},
};

module_platform_driver(panel_driver);

MODULE_LICENSE("GPL");
