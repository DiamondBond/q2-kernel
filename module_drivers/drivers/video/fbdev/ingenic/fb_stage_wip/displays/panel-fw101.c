/*
 * driver/video/fbdev/ingenic/x2000_v12/displays/panel-fw101.c
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

//#define MIPI_4LANE_MODE 1

static char panel_fw101_debug = 0;
#define PANEL_DEBUG_MSG(msg...)			\
	do {					\
		if (panel_fw101_debug)		\
			printk(">>>>>>>>>>>>>>>>PANEL FW101: " msg);		\
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

#define PANEL_fw101_ID	(0x00000001)

struct fw101 {
	struct device *dev;
	unsigned int power;
	unsigned int id;

	struct lcd_device *ld;
	struct backlight_device *bd;

	struct mipi_dsim_lcd_device *dsim_dev;

};

static struct dsi_cmd_packet ili9881c_fw101_4lane_cmd_list[] = {
    /*page 03*/
    {0x39, 0x04, 0x00, {0xFF, 0x98, 0x81, 0x03}},
    {0x39, 0x02, 0x00, {0x01, 0x00}},//---GIP_1----
    {0x39, 0x02, 0x00, {0x02, 0x00}},
    {0x39, 0x02, 0x00, {0x03, 0x53}},  //STVA=STV2_4
    {0x39, 0x02, 0x00, {0x04, 0xD3}},  //STVB=STV1_3
    {0x39, 0x02, 0x00, {0x05, 0x00}},
    {0x39, 0x02, 0x00, {0x06, 0x0D}},  //STVA_Rise
    {0x39, 0x02, 0x00, {0x07, 0x08}},  //STVB_Rise
    {0x39, 0x02, 0x00, {0x08, 0x00}},
    {0x39, 0x02, 0x00, {0x09, 0x00}},
    {0x39, 0x02, 0x00, {0x0A, 0x00}},
    {0x39, 0x02, 0x00, {0x0B, 0x00}},
    {0x39, 0x02, 0x00, {0x0C, 0x00}},
    {0x39, 0x02, 0x00, {0x0D, 0x00}},
    {0x39, 0x02, 0x00, {0x0E, 0x00}},
    {0x39, 0x02, 0x00, {0x0F, 0x28}},  //CLW1(ALR) Duty=45%
    {0x39, 0x02, 0x00, {0x10, 0x28}},  //CLW2(ARR) Duty=45%
    {0x39, 0x02, 0x00, {0x11, 0x00}},
    {0x39, 0x02, 0x00, {0x12, 0x00}},
    {0x39, 0x02, 0x00, {0x13, 0x00}},  //CLWX(ATF)
    {0x39, 0x02, 0x00, {0x14, 0x00}},
    {0x39, 0x02, 0x00, {0x15, 0x00}},
    {0x39, 0x02, 0x00, {0x16, 0x00}},
    {0x39, 0x02, 0x00, {0x17, 0x00}},
    {0x39, 0x02, 0x00, {0x18, 0x00}},
    {0x39, 0x02, 0x00, {0x19, 0x00}},
    {0x39, 0x02, 0x00, {0x1A, 0x00}},
    {0x39, 0x02, 0x00, {0x1B, 0x00}},
    {0x39, 0x02, 0x00, {0x1C, 0x00}},
    {0x39, 0x02, 0x00, {0x1D, 0x00}},
    {0x39, 0x02, 0x00, {0x1E, 0x40}},  //CLKA 40自动反 C0手动反(X8参考CLKB)
    {0x39, 0x02, 0x00, {0x1F, 0x80}},
    {0x39, 0x02, 0x00, {0x20, 0x06}},  //CLKA_Rise
    {0x39, 0x02, 0x00, {0x21, 0x01}},  //CLKA_Fall
    {0x39, 0x02, 0x00, {0x22, 0x00}},
    {0x39, 0x02, 0x00, {0x23, 0x00}},
    {0x39, 0x02, 0x00, {0x24, 0x00}},
    {0x39, 0x02, 0x00, {0x25, 0x00}},
    {0x39, 0x02, 0x00, {0x26, 0x00}},
    {0x39, 0x02, 0x00, {0x27, 0x00}},
    {0x39, 0x02, 0x00, {0x28, 0x33}},  //CLK Phase
    {0x39, 0x02, 0x00, {0x29, 0x33}},  //CLK overlap
    {0x39, 0x02, 0x00, {0x2A, 0x00}},
    {0x39, 0x02, 0x00, {0x2B, 0x00}},
    {0x39, 0x02, 0x00, {0x2C, 0x00}},
    {0x39, 0x02, 0x00, {0x2D, 0x00}},
    {0x39, 0x02, 0x00, {0x2E, 0x00}},
    {0x39, 0x02, 0x00, {0x2F, 0x00}},
    {0x39, 0x02, 0x00, {0x30, 0x00}},
    {0x39, 0x02, 0x00, {0x31, 0x00}},
    {0x39, 0x02, 0x00, {0x32, 0x00}},
    {0x39, 0x02, 0x00, {0x33, 0x00}},
    {0x39, 0x02, 0x00, {0x34, 0x03}},  //VDD1&2 overlap 0us
    {0x39, 0x02, 0x00, {0x35, 0x00}},
    {0x39, 0x02, 0x00, {0x36, 0x00}},
    {0x39, 0x02, 0x00, {0x37, 0x00}},
    {0x39, 0x02, 0x00, {0x38, 0x96}},  //VDD1&2 toggle 2.5sec
    {0x39, 0x02, 0x00, {0x39, 0x00}},
    {0x39, 0x02, 0x00, {0x3A, 0x00}},
    {0x39, 0x02, 0x00, {0x3B, 0x00}},
    {0x39, 0x02, 0x00, {0x3C, 0x00}},
    {0x39, 0x02, 0x00, {0x3D, 0x00}},
    {0x39, 0x02, 0x00, {0x3E, 0x00}},
    {0x39, 0x02, 0x00, {0x3F, 0x00}},
    {0x39, 0x02, 0x00, {0x40, 0x00}},
    {0x39, 0x02, 0x00, {0x41, 0x00}},
    {0x39, 0x02, 0x00, {0x42, 0x00}},
    {0x39, 0x02, 0x00, {0x43, 0x00}},
    {0x39, 0x02, 0x00, {0x44, 0x00}},

    {0x39, 0x02, 0x00, {0x50, 0x00}},//---GIP_2----
    {0x39, 0x02, 0x00, {0x51, 0x23}},
    {0x39, 0x02, 0x00, {0x52, 0x45}},
    {0x39, 0x02, 0x00, {0x53, 0x67}},
    {0x39, 0x02, 0x00, {0x54, 0x89}},
    {0x39, 0x02, 0x00, {0x55, 0xAB}},
    {0x39, 0x02, 0x00, {0x56, 0x01}},
    {0x39, 0x02, 0x00, {0x57, 0x23}},
    {0x39, 0x02, 0x00, {0x58, 0x45}},
    {0x39, 0x02, 0x00, {0x59, 0x67}},
    {0x39, 0x02, 0x00, {0x5A, 0x89}},
    {0x39, 0x02, 0x00, {0x5B, 0xAB}},
    {0x39, 0x02, 0x00, {0x5C, 0xCD}},
    {0x39, 0x02, 0x00, {0x5D, 0xEF}},

    {0x39, 0x02, 0x00, {0x5E, 0x00}},//---GIP_3----
    {0x39, 0x02, 0x00, {0x5F, 0x08}},  //FW_CGOUT_L[1]    STV3
    {0x39, 0x02, 0x00, {0x60, 0x08}},  //FW_CGOUT_L[2]    STV3
    {0x39, 0x02, 0x00, {0x61, 0x06}},  //FW_CGOUT_L[3]    STV4
    {0x39, 0x02, 0x00, {0x62, 0x06}},  //FW_CGOUT_L[4]    STV4
    {0x39, 0x02, 0x00, {0x63, 0x01}},  //FW_CGOUT_L[5]    VDS
    {0x39, 0x02, 0x00, {0x64, 0x01}},  //FW_CGOUT_L[6]    VDS
    {0x39, 0x02, 0x00, {0x65, 0x00}},  //FW_CGOUT_L[7]    VSD
    {0x39, 0x02, 0x00, {0x66, 0x00}},  //FW_CGOUT_L[8]    VSD
    {0x39, 0x02, 0x00, {0x67, 0x02}},  //FW_CGOUT_L[9]
    {0x39, 0x02, 0x00, {0x68, 0x15}},  //FW_CGOUT_L[10]   VDD2
    {0x39, 0x02, 0x00, {0x69, 0x15}},  //FW_CGOUT_L[11]   VDD2
    {0x39, 0x02, 0x00, {0x6A, 0x14}},  //FW_CGOUT_L[12]   VDD1
    {0x39, 0x02, 0x00, {0x6B, 0x14}},  //FW_CGOUT_L[13]   VDD1
    {0x39, 0x02, 0x00, {0x6C, 0x0D}},  //FW_CGOUT_L[14]   CLK8
    {0x39, 0x02, 0x00, {0x6D, 0x0D}},  //FW_CGOUT_L[15]   CLK8
    {0x39, 0x02, 0x00, {0x6E, 0x0C}},  //FW_CGOUT_L[16]   CLK6
    {0x39, 0x02, 0x00, {0x6F, 0x0C}},  //FW_CGOUT_L[17]   CLK6
    {0x39, 0x02, 0x00, {0x70, 0x0F}},  //FW_CGOUT_L[18]   CLK4
    {0x39, 0x02, 0x00, {0x71, 0x0F}},  //FW_CGOUT_L[19]   CLK4
    {0x39, 0x02, 0x00, {0x72, 0x0E}},  //FW_CGOUT_L[20]   CLK2
    {0x39, 0x02, 0x00, {0x73, 0x0E}},  //FW_CGOUT_L[21]   CLK2
    {0x39, 0x02, 0x00, {0x74, 0x02}},  //FW_CGOUT_L[22]   VGL

    {0x39, 0x02, 0x00, {0x75, 0x08}},  //BW_CGOUT_L[1]
    {0x39, 0x02, 0x00, {0x76, 0x08}},  //BW_CGOUT_L[2]
    {0x39, 0x02, 0x00, {0x77, 0x06}},  //BW_CGOUT_L[3]
    {0x39, 0x02, 0x00, {0x78, 0x06}},  //BW_CGOUT_L[4]
    {0x39, 0x02, 0x00, {0x79, 0x01}},  //BW_CGOUT_L[5]
    {0x39, 0x02, 0x00, {0x7A, 0x01}},  //BW_CGOUT_L[6]
    {0x39, 0x02, 0x00, {0x7B, 0x00}},  //BW_CGOUT_L[7]
    {0x39, 0x02, 0x00, {0x7C, 0x00}},  //BW_CGOUT_L[8]
    {0x39, 0x02, 0x00, {0x7D, 0x02}},  //BW_CGOUT_L[9]
    {0x39, 0x02, 0x00, {0x7E, 0x15}},  //BW_CGOUT_L[10]
    {0x39, 0x02, 0x00, {0x7F, 0x15}},  //BW_CGOUT_L[11]
    {0x39, 0x02, 0x00, {0x80, 0x14}},  //BW_CGOUT_L[12]
    {0x39, 0x02, 0x00, {0x81, 0x14}},  //BW_CGOUT_L[13]
    {0x39, 0x02, 0x00, {0x82, 0x0D}},  //BW_CGOUT_L[14]
    {0x39, 0x02, 0x00, {0x83, 0x0D}},  //BW_CGOUT_L[15]
    {0x39, 0x02, 0x00, {0x84, 0x0C}},  //BW_CGOUT_L[16]
    {0x39, 0x02, 0x00, {0x85, 0x0C}},  //BW_CGOUT_L[17]
    {0x39, 0x02, 0x00, {0x86, 0x0F}},  //BW_CGOUT_L[18]
    {0x39, 0x02, 0x00, {0x87, 0x0F}},  //BW_CGOUT_L[19]
    {0x39, 0x02, 0x00, {0x88, 0x0E}},  //BW_CGOUT_L[20]
    {0x39, 0x02, 0x00, {0x89, 0x0E}},  //BW_CGOUT_L[21]
    {0x39, 0x02, 0x00, {0x8A, 0x02}},  //BW_CGOUT_L[22]

    /*page 04*/
    {0x39, 0x04, 0x00, {0xFF, 0x98, 0x81, 0x04}},
    // {0x39, 0x02, 0x00, {0x00, 0x80}}, //set 2 line mode
    {0x39, 0x02, 0x00, {0x6E, 0x2B}},    //VGH 15V
    {0x39, 0x02, 0x00, {0x6F, 0x37}},    //reg vcl + pumping ratio VGH=3x VGL=-3x
    {0x39, 0x02, 0x00, {0x3A, 0xA4}},    //POWER SAVING
    {0x39, 0x02, 0x00, {0x8D, 0x1A}},    //VGL -11V
    {0x39, 0x02, 0x00, {0x87, 0xBA}},    //ESD
    {0x39, 0x02, 0x00, {0xB2, 0xD1}},
    {0x39, 0x02, 0x00, {0x88, 0x0B}},
    {0x39, 0x02, 0x00, {0x38, 0x01}},
    {0x39, 0x02, 0x00, {0x39, 0x00}},
    {0x39, 0x02, 0x00, {0xB5, 0x07}},
    {0x39, 0x02, 0x00, {0x31, 0x75}},
    {0x39, 0x02, 0x00, {0x3B, 0x98}},

    /*page 01*/
    {0x39, 0x04, 0x00, {0xFF, 0x98, 0x81, 0x01}},
    {0x39, 0x02, 0x00, {0x22, 0X0A}},  //BGR, SS
    {0x39, 0x02, 0x00, {0x31, 0X00}},  //Column inversion
    {0x39, 0x02, 0x00, {0x53, 0X40}},  //VCOM1
    {0x39, 0x02, 0x00, {0x55, 0X40}},  //VCOM2
    {0x39, 0x02, 0x00, {0x50, 0X99}},  //VREG1OUT 4.5V
    {0x39, 0x02, 0x00, {0x51, 0X94}},  //VREG2OUT -4.5V
    {0x39, 0x02, 0x00, {0x60, 0X10}},  //SDT 3.5us
    {0x39, 0x02, 0x00, {0x62, 0X20}},


    //-----------------ILI9881C gamma setting------------------//
    {0x39, 0x02, 0x00, {0xA0, 0x00}},
    {0x39, 0x02, 0x00, {0xA1, 0x00}},
    {0x39, 0x02, 0x00, {0xA2, 0x15}},
    {0x39, 0x02, 0x00, {0xA3, 0x14}},
    {0x39, 0x02, 0x00, {0xA4, 0x1B}},
    {0x39, 0x02, 0x00, {0xA5, 0x2F}},
    {0x39, 0x02, 0x00, {0xA6, 0x25}},
    {0x39, 0x02, 0x00, {0xA7, 0x24}},
    {0x39, 0x02, 0x00, {0xA8, 0x80}},
    {0x39, 0x02, 0x00, {0xA9, 0x1F}},
    {0x39, 0x02, 0x00, {0xAA, 0x2C}},
    {0x39, 0x02, 0x00, {0xAB, 0x6C}},
    {0x39, 0x02, 0x00, {0xAC, 0x16}},
    {0x39, 0x02, 0x00, {0xAD, 0x14}},
    {0x39, 0x02, 0x00, {0xAE, 0x4D}},
    {0x39, 0x02, 0x00, {0xAF, 0x20}},
    {0x39, 0x02, 0x00, {0xB0, 0x29}},
    {0x39, 0x02, 0x00, {0xB1, 0x4F}},
    {0x39, 0x02, 0x00, {0xB2, 0x5F}},
    {0x39, 0x02, 0x00, {0xB3, 0x23}},

    {0x39, 0x02, 0x00, {0xC0, 0x00}},
    {0x39, 0x02, 0x00, {0xC1, 0x2E}},
    {0x39, 0x02, 0x00, {0xC2, 0x3B}},
    {0x39, 0x02, 0x00, {0xC3, 0x15}},
    {0x39, 0x02, 0x00, {0xC4, 0x16}},
    {0x39, 0x02, 0x00, {0xC5, 0x28}},
    {0x39, 0x02, 0x00, {0xC6, 0x1A}},
    {0x39, 0x02, 0x00, {0xC7, 0x1C}},
    {0x39, 0x02, 0x00, {0xC8, 0xA7}},
    {0x39, 0x02, 0x00, {0xC9, 0x1B}},
    {0x39, 0x02, 0x00, {0xCA, 0x28}},
    {0x39, 0x02, 0x00, {0xCB, 0x92}},
    {0x39, 0x02, 0x00, {0xCC, 0x1F}},
    {0x39, 0x02, 0x00, {0xCD, 0x1C}},
    {0x39, 0x02, 0x00, {0xCE, 0x4B}},
    {0x39, 0x02, 0x00, {0xCF, 0x1F}},
    {0x39, 0x02, 0x00, {0xD0, 0x28}},
    {0x39, 0x02, 0x00, {0xD1, 0x4E}},
    {0x39, 0x02, 0x00, {0xD2, 0x5C}},
    {0x39, 0x02, 0x00, {0xD3, 0x23}},

    {0x39, 0x04, 0x00, {0xFF, 0x98, 0x81, 0x00}},
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

	if(panel_id == PANEL_fw101_ID)
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

		param_list = &ili9881c_fw101_4lane_cmd_list[0];
		num_of_param = ARRAY_SIZE(ili9881c_fw101_4lane_cmd_list);
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
	struct fw101 *lcd = dev_get_drvdata(&dsim_dev->dev);
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
	.name = "ili9881c_fw101-lcd",
	.xres = 800,
	.yres = 1280,

	.refresh = 60,
	.left_margin = 120,	//hbp
	.right_margin = 100,	//hfp
	.hsync_len = 10,	//hsync
	.upper_margin = 25,	//vbp
	.lower_margin = 20,	//vfp
	.vsync_len = 10,	//vsync
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

static struct tft_config fw101_cfg = {
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
	.tft_config = &fw101_cfg,
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
* @ pannel_fw101_lcd_ops, register to kernel common backlight/lcd.c framworks.
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
	 	ret = gpio_request_one(panel->lcd_pwm.gpio, GPIOF_DIR_OUT, "lcd-pwm");
	 	if(ret < 0) {
	 		dev_err(dev, "Failed to request lcd-pwm pin!\n");
	 		goto err_request_lcd_pwm;
	 	}
	 } else {
	 	dev_warn(dev, "invalid gpio lcd-pwm.gpio: %d\n", panel->lcd_pwm.gpio);
	 }

	return 0;

err_request_lcd_pwm:
	if(gpio_is_valid(panel->vdd_en.gpio))
		gpio_free(panel->vdd_en.gpio);
err_request_lcd_vdd:
	if(gpio_is_valid(panel->rst.gpio))
		gpio_free(panel->rst.gpio);
	return ret;
}

static int panel_dev_probe(struct mipi_dsim_lcd_device *dsim_dev)
{
	struct fw101 *lcd;

	lcd = devm_kzalloc(&dsim_dev->dev, sizeof(struct fw101), GFP_KERNEL);
	if (!lcd)
	{
		dev_err(&dsim_dev->dev, "failed to allocate ili9881c_fw101 structure.\n");
		return -ENOMEM;
	}

	lcd->dsim_dev = dsim_dev;
	lcd->dev = &dsim_dev->dev;

	lcd->ld = lcd_device_register("ili9881c_fw101", lcd->dev, lcd,
	                              &panel_lcd_ops);
	if (IS_ERR(lcd->ld))
	{
		dev_err(lcd->dev, "failed to register lcd ops.\n");
		return PTR_ERR(lcd->ld);
	}

	dev_set_drvdata(&dsim_dev->dev, lcd);


	dev_dbg(lcd->dev, "probed ili9881c_fw101 panel driver.\n");


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
	printk("panel_id: %x\n", buf[0]);

	if(panel_id == PANEL_fw101_ID) {
		return 0;
	} else {
		return -EINVAL;
	}
	return 0;
}

static struct mipi_dsim_lcd_driver panel_dev_dsim_ddi_driver = {
	.name = "ili9881c_fw101-lcd",
	.id = -1,

	.power_on = panel_dev_power_on,
	.set_sequence = panel_dev_set_sequence,
	.probe = panel_dev_probe,
	.suspend = panel_suspend,
	.resume = panel_resume,
	.query_status = panel_query_status,
	.probe_dev_id = panel_dev_read_id,
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
	.name		= "ili9881c_fw101-lcd",
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
	return 0;
}

static const struct of_device_id panel_of_match[] = {
	{ .compatible = "ingenic,fw101", },
	{},
};

static struct platform_driver panel_driver = {
	.probe		= panel_probe,
	.remove		= panel_remove,
	.driver		= {
		.name	= "fw101",
		.of_match_table = panel_of_match,
#ifdef CONFIG_PM
		.pm = &panel_pm_ops,
#endif
	},
};

module_platform_driver(panel_driver);

MODULE_LICENSE("GPL");
