/*
 * module_drivers/drivers/video/fbdev/ingenic/fb_stage_wip/displays/panel-M140IMN476-N010.c
 *
 * Copyright (C) 2016 Ingenic Semiconductor Inc.
 *
 * This program is free software, you can redistribute it and/or modify it
 *
 * under the terms of the GNU General Public License version 2 as published by
 *
 * the Free Software Foundation.
 */
/* dts:
 * lt8911@29{
 *     status = "okay";
 *     compatible = "mipi2edp,lt8911";
 *     reg = <0x29>;
 *     lt8911,reset-gpio = <&gpc 14 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>;
 *     // lt8911,lcd_en-gpio = <&gpb 8 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>;
 *     // lt8911,bl_en-gpio = <&gpc 7 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>;
 *     // lt8911,pwm-gpio = <&gpc 10 GPIO_ACTIVE_HIGH INGENIC_GPIO_NOBIAS>;
 * };
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/pwm_backlight.h>
#include <linux/delay.h>
#include <linux/lcd.h>
#include <linux/of_gpio.h>
#include <linux/i2c.h>

#include "../ingenicfb.h"
#define _eDP_2G7_
// #define  _read_edid_ // read eDP panel EDID
#define _link_train_enable_
// #define _Test_Pattern_
#define _htotal_stable_check_
// #define _Msa_Active_Only_
#define SCRAMBLE_MODE 0x00
#define MSA_SW_MODE 0x80
#define EDP_IDLE_PTN_OFF 0x00
#define _MIPI_data_PN_Swap_En 0xF0
#define _MIPI_data_PN_Swap_Dis 0x00

#define _MIPI_data_PN_ _MIPI_data_PN_Swap_Dis

#define _No_swap_ 0x00	   // 3210 default
#define _MIPI_data_3210_ 0 // default
#define _MIPI_data_0123_ 21
#define _MIPI_data_2103_ 20

#define _MIPI_data_sequence_ _No_swap_

#define _eDP_data_PN_Swap_En 0xF0 // Please refer to the notes below
#define _eDP_data_PN_Swap_Dis 0x00

#define _eDP_data_PN_ _eDP_data_PN_Swap_Dis // defailt ; disable

#define _Lane0_data_ 0
#define _Lane1_data_ 1
#define _Lane2_data_ 2
#define _Lane3_data_ 3
#define _MIPI_Lane_ 4 // 3 /2 / 1

#define _eDP_data3_select_ (_Lane3_data_ << 6) // default; _Lane3_data_select_ is lane3
#define _eDP_data2_select_ (_Lane2_data_ << 4) // default; _Lane2_data_select_ is lane2

#define _eDP_data1_select_ (_Lane1_data_ << 2) // default; _Lane1_data_select_ is lane1
#define _eDP_data0_select_ (_Lane0_data_ << 0) // default; _Lane0_data_select_ is lane0

#define _eDP_data_No_swap_ 0xe4 // default

#define _eDP_data_sequence_ _eDP_data_No_swap_ // default, no swap

#define _Nvid 0 // 0: 0x0080,default

#ifdef _read_edid_ // read eDP panel EDID

static uint8_t EDID_DATA[128] = {0};
static uint16_t EDID_Timing[11] = {0};

static bool EDID_Reply = 0;
#endif

struct video_timing
{
	unsigned short hfp;
	unsigned short hs;
	unsigned short hbp;
	unsigned short hact;
	unsigned short htotal;
	unsigned short vfp;
	unsigned short vs;
	unsigned short vbp;
	unsigned short vact;
	unsigned short vtotal;
	unsigned int pclk_khz;
};

enum
{
	hfp = 0,
	hs,
	hbp,
	hact,
	htotal,
	vfp,
	vs,
	vbp,
	vact,
	vtotal,
	pclk_10khz
};

#define _1366x768_EDP_Panel_

#ifdef _1366x768_EDP_Panel_
#define PCR_PLL_PREDIV 0x40
#define PCR_M 0x17 // 74M	          //hfp, hs, hbp,hact,htotal,vfp, vs, vbp, vact,vtotal,
#define eDP_lane 1
static struct video_timing video = {65, 32, 65, 1366, 1528, 8, 6, 8, 768, 790, 72427};
#define FPS 60
#define HACT 1366
#define VACT 768
#define HFP 65
#define HBP 65
#define HS 32
#define VFP 8
#define VBP 8
#define VS 6

#define _6bit_

#endif

// sfr PIF = 0xEC;
struct board_gpio
{
	short gpio;
	short active_level;
};
struct panel_dev
{
	/* ingenic frame buffer */
	struct device *dev;
	struct lcd_panel *panel;

	/* common lcd framework */
	struct lcd_device *lcd;
	int power;

	struct board_gpio reset;
	struct board_gpio lcd_en;
	struct board_gpio bl_en;

	int i2c_id;
	struct i2c_client *client;
	char *panel_name;
};

////////////////////////////LT8911 begin/////////////////////////////////////////////
// #include "lt8911.h"
/******************* MIPI Input Config ********************/
/******************* MIPI Input Config ********************/

/******************* Edp Output Config ********************/

// #include "lt8911.c"
// #define DEBUG

#ifdef DEBUG
#define debug(info, ...) printk(info, ##__VA_ARGS__)
#else
#define debug(info, ...)
#endif

#define HTOTAL (HFP + HACT + HBP + HS)
#define VTOTAL (VFP + VACT + VBP + VS)

#define PIXCLK (HTOTAL * VTOTAL * FPS / 1000)

// hfp, hs, hbp, hact, htotal, vfp, vs, vbp, vact, vtotal, pixclk

////////////////////////////LT8911 end/////////////////////////////////////////////

static struct panel_dev *lt8911_info;

static void panel_enable(struct lcd_panel *panel)
{
}

static void panel_disable(struct lcd_panel *panel)
{
}

static struct lcd_panel_ops panel_ops = {
    .enable = (void *)panel_enable,
    .disable = (void *)panel_disable,
};

static struct fb_videomode panel_modes = {
    .name = "lt8911",
    .refresh = FPS,
    .xres = HACT,
    .yres = VACT,
    .left_margin = HBP,
    .right_margin = HFP,
    .upper_margin = VBP,
    .lower_margin = VFP,

    .hsync_len = HS,
    .vsync_len = VS,
    .sync = FB_SYNC_HOR_HIGH_ACT & FB_SYNC_VERT_HIGH_ACT,
    .vmode = FB_VMODE_NONINTERLACED,
    .flag = 0,
};

struct jzdsi_data jzdsi_pdata = {
    .modes = &panel_modes,
    .video_config.no_of_lanes = 4,
    .video_config.virtual_channel = 0,
    .video_config.color_coding = COLOR_CODE_24BIT,
    .video_config.video_mode = VIDEO_BURST_WITH_SYNC_PULSES,
    .video_config.receive_ack_packets = 0, /* enable receiving of ack packets */
    .video_config.is_18_loosely = 0,
    .video_config.data_en_polarity = 1,
    .video_config.byte_clock = 63000, // 148500, // driver will auto calculate byte_clock.
    .video_config.byte_clock_coef = MIPI_PHY_BYTE_CLK_COEF_MUL1,

    .dsi_config.max_lanes = 4,
    .dsi_config.max_hs_to_lp_cycles = 100,
    .dsi_config.max_lp_to_hs_cycles = 40,
    .dsi_config.max_bta_cycles = 4095,
    .dsi_config.color_mode_polarity = 1,
    .dsi_config.shut_down_polarity = 1,
    .dsi_config.max_bps = 2750,
    .bpp_info = 24,
};

static struct tft_config m140imn476_cfg = {
    .pix_clk_inv = 0,
    .de_dl = 0,
    .sync_dl = 0,
    .color_even = TFT_LCD_COLOR_EVEN_RGB,
    .color_odd = TFT_LCD_COLOR_ODD_RGB,
    .mode = TFT_LCD_MODE_PARALLEL_888,
};

static struct lcd_panel lcd_panel[] = {
    [0] = {
	.name = "lt8911",
	.modes = &panel_modes,
	.num_modes = 1,
	.dsi_pdata = &jzdsi_pdata,

	.bpp = 24,
	.width = 1366,
	.height = 768,

	.lcd_type = LCD_TYPE_MIPI_TFT,

	.tft_config = &m140imn476_cfg,
	.dsi_pdata = &jzdsi_pdata,

	.dither_enable = 0,
	.dither.dither_red = 0,
	.dither.dither_green = 0,
	.dither.dither_blue = 0,

	.ops = &panel_ops,
    },
};

static unsigned char i2c_read(struct i2c_client *client, unsigned char addr)
{
	return i2c_smbus_read_byte_data(client, addr);
}

static unsigned int i2c_write(struct i2c_client *client, unsigned char addr, unsigned char value)
{
	return i2c_smbus_write_byte_data(client, addr, value);
}

static void lt8911_reset(struct panel_dev *lt8911_info)
{
	struct board_gpio *rst = &lt8911_info->reset;

	if (gpio_is_valid(rst->gpio))
	{
		gpio_direction_output(rst->gpio, 1);
		mdelay(10);
		gpio_direction_output(rst->gpio, 0);
		mdelay(20);
		gpio_direction_output(rst->gpio, 1);
		mdelay(20);
	}
}

static void LT8911_ChipID(struct i2c_client *client)
{
	uint8_t value[3];
	i2c_write(client, 0xff, 0x81); // register bank
	i2c_write(client, 0x08, 0x7f);
	value[0] = i2c_read(client, 0x00);
	value[1] = i2c_read(client, 0x01);
	value[2] = i2c_read(client, 0x02);
	printk("value[0] = %x, value[1] = %x, value[2] = %x\n", value[0], value[1], value[2]);
}

static void LT8911EXB_read_edid(struct i2c_client *client)
{
#ifdef _read_edid_
	uint8_t reg, i, j;
	//	bool	aux_reply, aux_ack, aux_nack, aux_defer;
	i2c_write(client, 0xff, 0xac);
	i2c_write(client, 0x00, 0x20); // Soft Link train
	i2c_write(client, 0xff, 0xa6);
	i2c_write(client, 0x2a, 0x01);

	/*set edid offset addr*/
	i2c_write(client, 0x2b, 0x40); // CMD
	i2c_write(client, 0x2b, 0x00); // addr[15:8]
	i2c_write(client, 0x2b, 0x50); // addr[7:0]
	i2c_write(client, 0x2b, 0x00); // data lenth
	i2c_write(client, 0x2b, 0x00); // data lenth
	i2c_write(client, 0x2c, 0x00); // start Aux read edid

	printk("Read eDP EDID......\n");

	mdelay(20); // more than 10ms
	reg = i2c_read(client, 0x25);
	if ((reg & 0x0f) == 0x0c)
	{
		for (j = 0; j < 8; j++)
		{
			if (j == 7)
			{
				i2c_write(client, 0x2b, 0x10); // MOT
			}
			else
			{
				i2c_write(client, 0x2b, 0x50);
			}

			i2c_write(client, 0x2b, 0x00);
			i2c_write(client, 0x2b, 0x50);
			i2c_write(client, 0x2b, 0x0f);
			i2c_write(client, 0x2c, 0x00); // start Aux read edid
			mdelay(50);		       // more than 50ms

			if (i2c_read(client, 0x39) == 0x31)
			{
				i2c_read(client, 0x2b);
				for (i = 0; i < 16; i++)
				{
					EDID_DATA[j * 16 + i] = i2c_read(client, 0x2b);
				}

				EDID_Reply = 1;
			}
			else
			{
				EDID_Reply = 0;
				printk("no_reply\n");
				return;
			}
		}

		for (i = 0; i < 128; i += 16) // print edid data
		{
			printk("\t%d, \t%d, \t%d, \t%d, \t%d, \t%d, \t%d, \t%d, \t%d, \t%d, \t%d, \t%d, \t%d, \t%d, \t%d, \t%d\n\n",
			       EDID_DATA[i], EDID_DATA[i + 1], EDID_DATA[i + 2], EDID_DATA[i + 3], EDID_DATA[i + 4],
			       EDID_DATA[i + 5], EDID_DATA[i + 6], EDID_DATA[i + 7], EDID_DATA[i + 8], EDID_DATA[i + 9],
			       EDID_DATA[i + 10], EDID_DATA[i + 11], EDID_DATA[i + 12], EDID_DATA[i + 13], EDID_DATA[i + 14],
			       EDID_DATA[i + 15]);
		}

		printk("eDP Timing = { H_FP / H_pluse / H_BP / H_act / H_tol / V_FP / V_pluse / V_BP / V_act / V_tol / D_CLK };\n");
		EDID_Timing[hfp] = ((EDID_DATA[0x41] & 0xC0) * 4 + EDID_DATA[0x3e]);
		EDID_Timing[hs] = ((EDID_DATA[0x41] & 0x30) * 16 + EDID_DATA[0x3f]);
		EDID_Timing[hbp] = (((EDID_DATA[0x3a] & 0x0f) * 0x100 + EDID_DATA[0x39]) - ((EDID_DATA[0x41] & 0x30) * 16 + EDID_DATA[0x3f]) - ((EDID_DATA[0x41] & 0xC0) * 4 + EDID_DATA[0x3e]));
		EDID_Timing[hact] = ((EDID_DATA[0x3a] & 0xf0) * 16 + EDID_DATA[0x38]);
		EDID_Timing[htotal] = ((EDID_DATA[0x3a] & 0xf0) * 16 + EDID_DATA[0x38] + ((EDID_DATA[0x3a] & 0x0f) * 0x100 + EDID_DATA[0x39]));
		EDID_Timing[vfp] = ((EDID_DATA[0x41] & 0x0c) * 4 + (EDID_DATA[0x40] & 0xf0) / 16);
		EDID_Timing[vs] = ((EDID_DATA[0x41] & 0x03) * 16 + (EDID_DATA[0x40] & 0x0f));
		EDID_Timing[vbp] = (((EDID_DATA[0x3d] & 0x03) * 0x100 + EDID_DATA[0x3c]) - ((EDID_DATA[0x41] & 0x03) * 16 + (EDID_DATA[0x40] & 0x0f)) - ((EDID_DATA[0x41] & 0x0c) * 4 + (EDID_DATA[0x40] & 0xf0) / 16));
		EDID_Timing[vact] = ((EDID_DATA[0x3d] & 0xf0) * 16 + EDID_DATA[0x3b]);
		EDID_Timing[vtotal] = ((EDID_DATA[0x3d] & 0xf0) * 16 + EDID_DATA[0x3b] + ((EDID_DATA[0x3d] & 0x03) * 0x100 + EDID_DATA[0x3c]));
		EDID_Timing[pclk_10khz] = (EDID_DATA[0x37] * 0x100 + EDID_DATA[0x36]);
		printk("eDp Timing = {%d, \t%d, \t%d, \t%d, \t%d, \t%d, \t%d, \t%d, \t%d, \t%d, \t%d}\n", (uint32_t)EDID_Timing[hfp],
		       (uint32_t)EDID_Timing[hs], (uint32_t)EDID_Timing[hbp], (uint32_t)EDID_Timing[hact],
		       (uint32_t)EDID_Timing[htotal], (uint32_t)EDID_Timing[vfp],
		       (uint32_t)EDID_Timing[vs], (uint32_t)EDID_Timing[vbp], (uint32_t)EDID_Timing[vact],
		       (uint32_t)EDID_Timing[vtotal], (uint32_t)EDID_Timing[pclk_10khz]); // HFB
	}

	return;

#endif
}

static void LT8911EXB_eDP_Video_cfg(struct i2c_client *client, struct video_timing *video_format)
{
	int pclk_khz;
	uint8_t dessc_m;
	// edp msa
	i2c_write(client, 0xff, 0xa8);
	i2c_write(client, 0x2d, 0x88); // bit[7]：1 = register msa, 0 = hardware msa
#ifdef _Msa_Active_Only_
	i2c_write(client, 0x05, 0x00);
	i2c_write(client, 0x06, 0x00); // htotal
	i2c_write(client, 0x07, 0x00);
	i2c_write(client, 0x08, 0x00); // h_start
	i2c_write(client, 0x09, 0x00);
	i2c_write(client, 0x0a, 0x00); // hsa
	i2c_write(client, 0x0b, (uint8_t)(video_format->hact / 256));
	i2c_write(client, 0x0c, (uint8_t)(video_format->hact % 256)); // hactive
	i2c_write(client, 0x0d, 0x00);
	i2c_write(client, 0x0e, 0x00); // vtotal
	i2c_write(client, 0x11, 0x00);
	i2c_write(client, 0x12, 0x00);
	i2c_write(client, 0x14, 0x00);
	i2c_write(client, 0x15, (uint8_t)(video_format->vact / 256));
	i2c_write(client, 0x16, (uint8_t)(video_format->vact % 256)); // vactive
#else
	i2c_write(client, 0x05, (uint8_t)(video_format->htotal / 256));
	i2c_write(client, 0x06, (uint8_t)(video_format->htotal % 256)); // htotal
	i2c_write(client, 0x07, (uint8_t)((video_format->hs + video_format->hbp) / 256));
	i2c_write(client, 0x08, (uint8_t)((video_format->hs + video_format->hbp) % 256)); // h_start
	i2c_write(client, 0x09, (uint8_t)(video_format->hs / 256));
	i2c_write(client, 0x0a, (uint8_t)(video_format->hs % 256)); // hsa
	i2c_write(client, 0x0b, (uint8_t)(video_format->hact / 256));
	i2c_write(client, 0x0c, (uint8_t)(video_format->hact % 256)); // hactive
	i2c_write(client, 0x0d, (uint8_t)(video_format->vtotal / 256));
	i2c_write(client, 0x0e, (uint8_t)(video_format->vtotal % 256)); // vtotal
	i2c_write(client, 0x11, (uint8_t)((video_format->vs + video_format->vbp) / 256));
	i2c_write(client, 0x12, (uint8_t)((video_format->vs + video_format->vbp) % 256)); // v_start
	i2c_write(client, 0x14, (uint8_t)(video_format->vs % 256));			  // vsa
	i2c_write(client, 0x15, (uint8_t)(video_format->vact / 256));
	i2c_write(client, 0x16, (uint8_t)(video_format->vact % 256)); // vactive
#endif
	// ldvd de only mode to regenerate h/v sync.
	i2c_write(client, 0xff, 0xd8);
	i2c_write(client, 0x20, (uint8_t)(video_format->hfp / 256));
	i2c_write(client, 0x21, (uint8_t)(video_format->hfp % 256));
	i2c_write(client, 0x22, (uint8_t)(video_format->hs / 256));
	i2c_write(client, 0x23, (uint8_t)(video_format->hs % 256));
	i2c_write(client, 0x24, (uint8_t)(video_format->htotal / 256));
	i2c_write(client, 0x25, (uint8_t)(video_format->htotal % 256));
	i2c_write(client, 0x26, (uint8_t)(video_format->vfp % 256));
	i2c_write(client, 0x27, (uint8_t)(video_format->vs % 256));
	// de-sscpll to gererate pixel clock for pattern
	pclk_khz = video_format->pclk_khz;
	dessc_m = (pclk_khz * 4) / (25 * 1000);
	i2c_write(client, 0xff, 0x85);
	i2c_write(client, 0xaa, dessc_m); // MK[30:24]
	//	i2c_write(0xab,0xba);	//MK[23:16]
	//	i2c_write(0xac,0xe1);	//MK[15:8]
	//
	//      i2c_write(client,0xad,0x47);  //MK[7:0]
}

static void LT8911_MIPI_Video_Timing(struct i2c_client *client, struct video_timing *video_format)
{
	i2c_write(client, 0xff, 0xd0);
	i2c_write(client, 0x0d, (uint8_t)(video_format->vtotal / 256));
	i2c_write(client, 0x0e, (uint8_t)(video_format->vtotal % 256)); // vtotal
	i2c_write(client, 0x0f, (uint8_t)(video_format->vact / 256));
	i2c_write(client, 0x10, (uint8_t)(video_format->vact % 256)); // vactive
	i2c_write(client, 0x11, (uint8_t)(video_format->htotal / 256));
	i2c_write(client, 0x12, (uint8_t)(video_format->htotal % 256)); // htotal
	i2c_write(client, 0x13, (uint8_t)(video_format->hact / 256));
	i2c_write(client, 0x14, (uint8_t)(video_format->hact % 256)); // hactive
	i2c_write(client, 0x15, (uint8_t)(video_format->vs % 256));   // vsa
	i2c_write(client, 0x16, (uint8_t)(video_format->hs % 256));   // hsa
	i2c_write(client, 0x17, (uint8_t)(video_format->vfp / 256));
	i2c_write(client, 0x18, (uint8_t)(video_format->vfp % 256)); // vfp
	i2c_write(client, 0x19, (uint8_t)(video_format->hfp / 256));
	i2c_write(client, 0x1a, (uint8_t)(video_format->hfp % 256)); // hfp
}

static void LT8911_Init(struct i2c_client *client, struct video_timing *video_format)
{
	int i;
	uint8_t pcr_pll_postdiv;
	uint8_t pcr_m;
	/* init */
	i2c_write(client, 0xff, 0x81);
	i2c_write(client, 0x08, 0x7f); // i2c over aux issue
	i2c_write(client, 0x49, 0xff); // enable 0x87xx

	i2c_write(client, 0xff, 0x82); // GPIO test output
	i2c_write(client, 0x5a, 0x0e);

	// for power consumption//
	i2c_write(client, 0xff, 0x81);
	i2c_write(client, 0x05, 0x06);
	i2c_write(client, 0x43, 0x00);
	i2c_write(client, 0x44, 0x1f);
#if (eDP_lane == 4)
	i2c_write(client, 0x45, 0xff);
	i2c_write(client, 0x46, 0xfe);
#else
	i2c_write(client, 0x45, 0xf7);
	i2c_write(client, 0x46, 0xf6);
#endif
	i2c_write(client, 0x49, 0x7f);

	i2c_write(client, 0xff, 0x82);
#if (eDP_lane == 2) // 2 lane eDP
	i2c_write(client, 0x12, 0x33);
#elif (eDP_lane == 1) // 1 lane eDP
	i2c_write(client, 0x12, 0x11);
#elif (eDP_lane == 4) // 4 lane eDP
	i2c_write(client, 0x12, 0xff);
#endif

	/* mipi Rx analog */
	i2c_write(client, 0xff, 0x82);
	i2c_write(client, 0x32, 0x51);
	i2c_write(client, 0x35, 0x62); // EQ current 0x42
	i2c_write(client, 0x3a, 0x33); // 0x77:EQ 12.5db  ,0x33:EQ 6.5db
	i2c_write(client, 0x3b, 0x33); // 0x77:EQ 12.5db  ,0x33:EQ 6.5db
	i2c_write(client, 0x4c, 0x0c);
	i2c_write(client, 0x4d, 0x00);

	/* dessc_pcr  pll analog */
	i2c_write(client, 0xff, 0x82);
	i2c_write(client, 0x6a, 0x43);		 // final setting: 0x40
	i2c_write(client, 0x6b, PCR_PLL_PREDIV); // 0x44:pre-div = 2

	if (video_format->pclk_khz < 88000)
	{
		i2c_write(client, 0x6e, 0x82); // 0x44:pre-div = 2 ,pixel_clk=44~ 88MHz
		pcr_pll_postdiv = 0x08;
	}
	else if (video_format->pclk_khz < 176000)
	{
		i2c_write(client, 0x6e, 0x81);
		pcr_pll_postdiv = 0x04;
	}
	else
	{
		i2c_write(client, 0x6e, 0x80);
		pcr_pll_postdiv = 0x02;
	}
	pcr_m = (uint8_t)(video_format->pclk_khz * pcr_pll_postdiv / 25 / 1000);
	printk("pcr_m = %d\n", pcr_m);
	printk("pcr_pll_postdiv = %d\n", pcr_pll_postdiv);

	/* dessc pll digital */
	i2c_write(client, 0xff, 0x85);
	i2c_write(client, 0xa9, 0x31);
	i2c_write(client, 0xaa, 0x17);
	i2c_write(client, 0xab, 0xba);
	i2c_write(client, 0xac, 0xe1);
	i2c_write(client, 0xad, 0x47);
	i2c_write(client, 0xae, 0x01);
	i2c_write(client, 0xae, 0x11);

	/* Digital Top */
	i2c_write(client, 0xff, 0x85);
	i2c_write(client, 0xc0, 0x01); // select mipi Rx
#ifdef _6bit_
	i2c_write(client, 0xb0, 0xd0); // enable dither
#else
	i2c_write(client, 0xb0, 0x00); // disable dither
#endif

	/* mipi Rx Digital */
	i2c_write(client, 0xff, 0xd0);
	i2c_write(client, 0x00, _MIPI_data_PN_ + _MIPI_Lane_ % 4); // 0x03: 3lanes, 0x00: 4lanes
	i2c_write(client, 0x02, 0x08);				   // settle
	i2c_write(client, 0x03, _MIPI_data_sequence_);		   // default is 0x00
	i2c_write(client, 0x08, 0x00);

	i2c_write(client, 0x0c, 0x80); // fifo position
	i2c_write(client, 0x1c, 0x80); // fifo position
	i2c_write(client, 0x24, 0x70); // pcr mode( de hs vs)

	// i2c_write(0x2d, 0x1f); //M up limit
	i2c_write(client, 0x31, 0x0a); // M down limit

	/*stage1 hs mode*/
	i2c_write(client, 0x25, 0x90); // line limit
	i2c_write(client, 0x2a, 0x3a); // step in limit
	i2c_write(client, 0x21, 0x4f); // hs_step
	i2c_write(client, 0x22, 0xff);

	/*stage2 de mode*/
	i2c_write(client, 0x0a, 0x02); // de adjust pre line
	i2c_write(client, 0x38, 0x02); // de_threshold 1
	i2c_write(client, 0x39, 0x04); // de_threshold 2
	i2c_write(client, 0x3a, 0x08); // de_threshold 3
	i2c_write(client, 0x3b, 0x10); // de_threshold 4

	i2c_write(client, 0x3f, 0x04); // de_step 1
	i2c_write(client, 0x40, 0x08); // de_step 2
	i2c_write(client, 0x41, 0x10); // de_step 3
	i2c_write(client, 0x42, 0x60); // de_step 4

	/*stage2 hs mode*/
	i2c_write(client, 0x1e, 0x01); // hs threshold
	i2c_write(client, 0x23, 0xf0); // hs step

	i2c_write(client, 0x2b, 0x80); // stable out // V1.8 20200417

#ifdef _Test_Pattern_
	i2c_write(client, 0x26, (pcr_m | 0x80));
#else
	i2c_write(client, 0x26, pcr_m);
#endif

	//	#ifndef _1080P_eDP_Panel_
	//	LT8911_MIPI_Video_Timing(&video); //defualt setting is 1080P
	//	#endif

	LT8911_MIPI_Video_Timing(client, &video); // defualt setting is 1080P

	i2c_write(client, 0xff, 0x81); // PCR reset
	i2c_write(client, 0x03, 0x7b);
	i2c_write(client, 0x03, 0xff);

#ifdef _eDP_2G7_
	/* Txpll 2.7G*/
	i2c_write(client, 0xff, 0x87);
	i2c_write(client, 0x19, 0x31);
	i2c_write(client, 0x1a, 0x1b);
	i2c_write(client, 0x1b, 0x00); // sync_k [7:0]
	i2c_write(client, 0x1c, 0x00); // sync_k [13:8]

	i2c_write(client, 0xff, 0x82);
	i2c_write(client, 0x09, 0x00);

	i2c_write(client, 0x02, 0x42);
	i2c_write(client, 0x03, 0x00);
	i2c_write(client, 0x03, 0x01);
	i2c_write(client, 0x0a, 0x1b);
	i2c_write(client, 0x04, 0x2a);
	i2c_write(client, 0xff, 0x87);
	i2c_write(client, 0x0c, 0x10);

	i2c_write(client, 0xff, 0x81);
	i2c_write(client, 0x09, 0xfc);
	i2c_write(client, 0x09, 0xfd);

	i2c_write(client, 0xff, 0x87);
	i2c_write(client, 0x0c, 0x11);

	i2c_write(client, 0xff, 0x87);
	i2c_write(client, 0x13, 0x83);
	i2c_write(client, 0x14, 0x41);
	i2c_write(client, 0x16, 0x0a);
	i2c_write(client, 0x19, 0x33);
#endif

#ifdef _eDP_1G62_
	i2c_write(client, 0xff, 0x87);
	i2c_write(client, 0x19, 0x31);
	i2c_write(client, 0x1a, 0x20); // sync m
	i2c_write(client, 0x1b, 0x19); // sync_k [7:0]
	i2c_write(client, 0x1c, 0x99); // sync_k [13:8]

	// txpll Analog
	i2c_write(client, 0xff, 0x82);
	i2c_write(client, 0x09, 0x00); // div hardware mode, for ssc.
	i2c_write(client, 0x02, 0x42);
	i2c_write(client, 0x03, 0x00); // txpll en = 0
	i2c_write(client, 0x03, 0x01); // txpll en = 1

	i2c_write(client, 0xff, 0x87);
	i2c_write(client, 0x0c, 0x10); // cal en = 0

	i2c_write(client, 0xff, 0x81);
	i2c_write(client, 0x09, 0xfc);
	i2c_write(client, 0x09, 0xfd);

	i2c_write(client, 0xff, 0x87);
	i2c_write(client, 0x0c, 0x11); // cal en = 1

	// ssc
	i2c_write(client, 0xff, 0x87);
	i2c_write(client, 0x13, 0x83);
	i2c_write(client, 0x14, 0x41);
	i2c_write(client, 0x16, 0x0a);
	i2c_write(client, 0x18, 0x0a);
	i2c_write(client, 0x19, 0x33);
#endif

	i2c_write(client, 0xff, 0x87);

	for (i = 0; i < 5; i++) // Check Tx PLL
	{
		mdelay(5);
		if (i2c_read(client, 0x37) & 0x02)
		{
			printk("LT8911 tx pll locked\n");
			i2c_write(client, 0xff, 0x87);
			i2c_write(client, 0x1a, 0x36);
			i2c_write(client, 0xff, 0x82);
			i2c_write(client, 0x0a, 0x36);
			i2c_write(client, 0x04, 0x3a);
			break;
		}
		else
		{
			printk("LT8911 tx pll unlocked\n");
			i2c_write(client, 0xff, 0x81);
			i2c_write(client, 0x09, 0xfc);
			i2c_write(client, 0x09, 0xfd);
			i2c_write(client, 0xff, 0x87);
			i2c_write(client, 0x0c, 0x10);
			i2c_write(client, 0x0c, 0x11);
		}
	}

	i2c_write(client, 0xff, 0xac);		      // Change Reg bank
	i2c_write(client, 0x15, _eDP_data_sequence_); // eDP data swap
	i2c_write(client, 0x16, _eDP_data_PN_);	      // eDP P / N swap

	// AUX reset
	i2c_write(client, 0xff, 0x81); // Change Reg bank
	i2c_write(client, 0x07, 0xfe);
	i2c_write(client, 0x07, 0xff);
	i2c_write(client, 0x0a, 0xfc);
	i2c_write(client, 0x0a, 0xfe);

	/* tx phy */
	i2c_write(client, 0xff, 0x82); // Change Reg bank
	i2c_write(client, 0x11, 0x00);
	i2c_write(client, 0x13, 0x10);
	i2c_write(client, 0x14, 0x0c);
	i2c_write(client, 0x14, 0x08);
	i2c_write(client, 0x13, 0x20);

	i2c_write(client, 0xff, 0x82); // Change Reg bank
	i2c_write(client, 0x0e, 0x35);
	// i2c_write(0x12,0xff);
	// i2c_write(client,0xff,0x80);
	// i2c_write(client,0x40,0x22);

	/*eDP Tx Digital */
	i2c_write(client, 0xff, 0xa8);
#ifdef _Test_Pattern_
	i2c_write(client, 0x24, 0x50);
	i2c_write(client, 0x25, 0x70);
	i2c_write(client, 0x27, 0x50); // 0x50:Pattern; 0x10:mipi video
	i2c_write(client, 0x2d, 0x88); //  block
#else
	i2c_write(client, 0x27, 0x10); // 0x50:Pattern; 0x10:mipi video
	// i2c_write(0x27,0x50); //0x50:Pattern; 0x10:mipi video
#endif

#ifdef _6bit_
	i2c_write(client, 0x17, 0x00);
	i2c_write(client, 0x18, 0x00);
#else
	// _8bit_
	i2c_write(client, 0x17, 0x10);
	i2c_write(client, 0x18, 0x20);
#endif

	//	#ifndef _1080P_eDP_Panel_
	//	LT8911_eDP_Video_Timing(&video);
	//	#endif

	/* nvid */
	i2c_write(client, 0xff, 0xa0); // nvid = 0x080000;
	i2c_write(client, 0x00, 0x00);
	i2c_write(client, 0x01, 0x80);
}

#if (eDP_lane == 4)
static uint8_t DpcdRead(struct i2c_client *client, uint32_t Address)
{
	uint8_t DpcdValue = 0x00;
	uint8_t AddressH = 0x0f & (Address >> 16);
	uint8_t AddressM = 0xff & (Address >> 8);
	uint8_t AddressL = 0xff & Address;
	char value;
	i2c_write(client, 0xff, 0xac);
	i2c_write(client, 0x00, 0x20); // Soft Link train
	i2c_write(client, 0xff, 0xa6);
	i2c_write(client, 0x2a, 0x01);

	i2c_write(client, 0xff, 0xa6);
	i2c_write(client, 0x2b, (0x90 | AddressH)); // CMD
	i2c_write(client, 0x2b, AddressM);	    // addr[15:8]
	i2c_write(client, 0x2b, AddressL);	    // addr[7:0]
	i2c_write(client, 0x2b, 0x00);		    // data lenth
	i2c_write(client, 0x2c, 0x00);		    // start Aux read edid

	mdelay(10); // more than 10ms
	if ((i2c_read(client, 0x25) & 0x0f) == 0x0c)
	{
		value = i2c_read(client, 0x39);
		if (value == 0x22)
		{
			i2c_read(client, 0x2b);
			DpcdValue = i2c_read(client, 0x2b);
		}
		else if ((value & 0x0f) == 0x0a)
			goto reply_nack;
		else if ((value & 0x0f) == 0x09)
			goto reply_defer;
		else
			goto no_reply;
	}
	else
	{
		i2c_write(client, 0xff, 0x81); // change bank
		i2c_write(client, 0x07, 0xfe);
		i2c_write(client, 0x07, 0xff);
		i2c_write(client, 0x0a, 0xfc);
		i2c_write(client, 0x0a, 0xfe);
	}

	printk("DpcdRead: %x%x%x= %x \n", AddressH, AddressM, AddressL, DpcdValue);
	return DpcdValue;
	/*error handle*/
no_reply:
	printk("DpcdRead error: no_reply \n");
	return 0;
reply_nack:
	printk("DpcdRead error: reply_nack \n");
	return 0;
reply_defer:
	printk("DpcdRead error: reply_nack \n");
	return 0;
}

static bool DpcdWrite(struct i2c_client *client, uint32_t Address, uint8_t value)
{
	uint8_t AddressH = 0x0f & (Address >> 16);
	uint8_t AddressM = 0xff & (Address >> 8);
	uint8_t AddressL = 0xff & Address;
	uint8_t reg;
	printk("DpcdWrite: %#x,%#x,%#x= %#x \n", AddressH, AddressM, AddressL, value);

	i2c_write(client, 0xff, 0xa6);
	i2c_write(client, 0x2b, (0x80 | AddressH)); // CMD
	i2c_write(client, 0x2b, AddressM);	    // addr[15:8]
	i2c_write(client, 0x2b, AddressL);	    // addr[7:0]
	i2c_write(client, 0x2b, 0x00);		    // data lenth
	i2c_write(client, 0x2b, value);		    // data lenth
	i2c_write(client, 0x2c, 0x00);		    // start Aux2 read edid

	mdelay(20); // more than 10ms
	if ((i2c_read(client, 0x25) & 0x0f) == 0x0c)
	{
		return 0;
	}
	else if ((reg & 0x0f) == 0x0a)
		goto reply_nack;
	else if ((reg & 0x0f) == 0x09)
		goto reply_defer;
	else
		goto no_reply;

	/*error handle*/
no_reply:
	printk("DpcdRead error: no_reply \n");
	return 1;
reply_nack:
	printk("DpcdRead error: reply_nack \n");
	return 1;
reply_defer:
	printk("DpcdRead error: reply_nack \n");
	return 1;
}
#endif

static void LT8911_LinkTrain(struct i2c_client *client)
{

	i2c_write(client, 0xff, 0x81);
	i2c_write(client, 0x06, 0xdf); // rset VID TX
	i2c_write(client, 0x06, 0xff);

	i2c_write(client, 0xff, 0x85);

	i2c_write(client, 0xa1, 0x02); // DP scramble mode;

	/* Aux operater init */
#if (eDP_lane == 4)
	i2c_write(client, 0xff, 0xac);
	i2c_write(client, 0x00, 0x20); // Soft Link train
	i2c_write(client, 0xff, 0xa6);
	i2c_write(client, 0x2a, 0x01);

	DpcdWrite(0x0101, 0x84);
	mdelay(10);
#endif

	/* Aux setup */
	i2c_write(client, 0xff, 0xac);
	i2c_write(client, 0x00, 0x60); // Soft Link train
	i2c_write(client, 0xff, 0xa6);
	i2c_write(client, 0x2a, 0x00);

	i2c_write(client, 0xff, 0x81);
	i2c_write(client, 0x07, 0xfe);
	i2c_write(client, 0x07, 0xff);
	i2c_write(client, 0x0a, 0xfc);
	i2c_write(client, 0x0a, 0xfe);

	/* link train */

	i2c_write(client, 0xff, 0x85);
	i2c_write(client, 0x1a, eDP_lane);

#ifdef _link_train_enable_
	i2c_write(client, 0xff, 0xac);
	i2c_write(client, 0x00, 0x64);
	i2c_write(client, 0x01, 0x0a);
	i2c_write(client, 0x0c, 0x85);
	i2c_write(client, 0x0c, 0xc5);
	printk("LT8911 link trian: hardware linktrain start...\n");
	//		mdelay(500);
#else
	i2c_write(client, 0xff, 0xac);
	i2c_write(client, 0x00, 0x00);
	i2c_write(client, 0x01, 0x0a);
	i2c_write(client, 0x14, 0x80);
	i2c_write(client, 0x14, 0x81);
	mdelay(50);
	i2c_write(client, 0x14, 0x84);
	mdelay(50);
	i2c_write(client, 0x14, 0xc0);
	printk("LT8911 link trian: no handshake linktrain\n");
#endif

}

static void LT8911EX_link_train_result(struct i2c_client *client)
{
	uint8_t i, reg;
	i2c_write(client, 0xff, 0xac);
	for (i = 0; i < 10; i++)
	{
		reg = i2c_read(client, 0x82);
		//  Debug_DispStrNum( "\r\n0x82 = ", reg );
		if (reg & 0x20)
		{
			if ((reg & 0x1f) == 0x1e)
			{
				printk("Link train success, 0x82 = 0x%02x\n", reg);
			}
			else
			{
				printk("Link train fail, 0x82 = 0x%02x\n", reg);
			}

			printk("panel link rate: 0x%02x\n", i2c_read(client, 0x83));
			printk("panel link count: 0x%02x\n", i2c_read(client, 0x84));
			return;
		}
		else
		{
			printk("link trian on going...\n");
		}
		mdelay(100);
	}
}

/* mipi should be ready before configuring below video check setting*/
static void LT8911_video_check(struct i2c_client *client)
{
	// uint8_t temp;
	int reg;
	/* mipi byte clk check*/
	i2c_write(client, 0xff, 0x85);
	i2c_write(client, 0x1d, 0x00); // FM select byte clk
	i2c_write(client, 0x40, 0xf7);
	i2c_write(client, 0x41, 0x30);

	i2c_write(client, 0xa1, 0x02); // video chech from mipi

	i2c_write(client, 0xff, 0x81); // video check rst
	i2c_write(client, 0x09, 0x7d);
	i2c_write(client, 0x09, 0xfd);

	i2c_write(client, 0xff, 0x85);
	mdelay(20);
	if (i2c_read(client, 0x50) == 0x03)
	{
		reg = i2c_read(client, 0x4d);
		reg = reg * 256 + i2c_read(client, 0x4e);
		reg = reg * 256 + i2c_read(client, 0x4f);

		printk("video check: mipi clk = %d\n", reg);
	}
	else
		printk("video check: mipi clk unstable\n");

	/* mipi vtotal check*/
	reg = i2c_read(client, 0x76);
	reg = reg * 256 + i2c_read(client, 0x77);

	printk("video check: Vtotal = %d\n", reg);

	/* mipi word count check*/
	i2c_write(client, 0xff, 0xd0);
	reg = i2c_read(client, 0x82);
	reg = reg * 256 + i2c_read(client, 0x83);
	reg = reg / 3;
	printk("video check: Hact(word counter) = %d\n", reg);
	// printdec_uint32_t(reg);

	/* mipi Vact check*/
	reg = i2c_read(client, 0x85);
	reg = reg * 256 + i2c_read(client, 0x86);

	printk("video check: Vact = %d\n", reg);
	// printdec_uint32_t(reg);
}

enum
{
	_Level0_ = 0, // 27.8 mA  0x83/0x00
	_Level1_,     // 26.2 mA  0x82/0xe0
	_Level2_,     // 24.6 mA  0x82/0xc0
	_Level3_,     // 23 mA    0x82/0xa0
	_Level4_,     // 21.4 mA  0x82/0x80
	_Level5_,     // 18.2 mA  0x82/0x40
	_Level6_,     // 16.6 mA  0x82/0x20
	_Level7_,     // 15mA     0x82/0x00  // level 1
	_Level8_,     // 12.8mA   0x81/0x00  // level 2
	_Level9_,     // 11.2mA   0x80/0xe0  // level 3
	_Level10_,    // 9.6mA    0x80/0xc0  // level 4
	_Level11_,    // 8mA      0x80/0xa0  // level 5
	_Level12_,    // 6mA      0x80/0x80  // level 6
};

static uint8_t Swing_Setting1[] = {0x83, 0x82, 0x82, 0x82, 0x82, 0x82, 0x82, 0x82, 0x81, 0x80, 0x80, 0x80, 0x80};
static uint8_t Swing_Setting2[] = {0x00, 0xe0, 0xc0, 0xa0, 0x80, 0x40, 0x20, 0x00, 0x00, 0xe0, 0xc0, 0xa0, 0x80};

static uint8_t Level = _Level7_; // normal

static void LT8911EX_TxSwingPreSet(struct i2c_client *client)
{
	i2c_write(client, 0xFF, 0x82);
	i2c_write(client, 0x22, Swing_Setting1[Level]); // lane 0 tap0
	i2c_write(client, 0x23, Swing_Setting2[Level]);
	i2c_write(client, 0x24, 0x80); // lane 0 tap1
	i2c_write(client, 0x25, 0x00);

#if (eDP_lane >= 2)
	i2c_write(client, 0x26, Swing_Setting1[Level]); // lane 1 tap0
	i2c_write(client, 0x27, Swing_Setting2[Level]);
	i2c_write(client, 0x28, 0x80); // lane 1 tap1
	i2c_write(client, 0x29, 0x00);

#if (eDP_lane == 4)
	i2c_write(client, 0x2a, Swing_Setting1[Level]); // lane 2 tap0
	i2c_write(client, 0x2b, Swing_Setting2[Level]);
	i2c_write(client, 0x2c, 0x80); // lane 2 tap1
	i2c_write(client, 0x2d, 0x00);
	i2c_write(client, 0x2e, Swing_Setting1[Level]); // lane 3 tap0
	i2c_write(client, 0x2f, Swing_Setting2[Level]);
	i2c_write(client, 0x30, 0x80); // lane 3 tap1
	i2c_write(client, 0x31, 0x00);
#endif
#endif
}

static void LT8911_pcr_status(struct i2c_client *client)
{
	uint8_t reg;

	i2c_write(client, 0xff, 0xd0);
	reg = i2c_read(client, 0x87);

	printk("Reg0xD087 =	0x%02x\n", reg);
	if (reg & 0x10)
	{
		printk("PCR Clock stable\n");
	}
	else
	{
		printk("PCR Clock unstable\n");
	}
}

static int LT8911_Config(struct i2c_client *client)
{
	LT8911_ChipID(client);
	// LT8911_SetVideoTiming(client,&video);
	LT8911EXB_eDP_Video_cfg(client, &video);
	LT8911_Init(client, &video);
	LT8911EXB_read_edid(client); // for debug

	LT8911EX_TxSwingPreSet(client);
	LT8911_LinkTrain(client);

	LT8911EX_link_train_result(client); // for debug
	LT8911_video_check(client);

	LT8911_pcr_status(client); // just for Check PCR CLK
	return 0;
}
#define POWER_IS_ON(pwr) ((pwr) <= FB_BLANK_NORMAL)
static int panel_set_power(struct lcd_device *lcd, int power)
{
	struct panel_dev *lt8911_info = lcd_get_data(lcd);
	struct i2c_client *client = lt8911_info->client;

	struct board_gpio *lcd_en = &lt8911_info->lcd_en;
	struct board_gpio *bl_en = &lt8911_info->bl_en;
	struct board_gpio *rst = &lt8911_info->reset;

    if (power == 0) {
        if (gpio_is_valid(rst->gpio))
            gpio_direction_output(rst->gpio, 0);
        if (gpio_is_valid(lcd_en->gpio))
            gpio_direction_output(lcd_en->gpio, 0);
	  if (gpio_is_valid(bl_en->gpio))
		gpio_direction_output(bl_en->gpio, 0);

        mdelay(15);
        lt8911_reset(lt8911_info);
        LT8911_Config(client);

        if (gpio_is_valid(bl_en->gpio))
            gpio_direction_output(bl_en->gpio, 1);
    } else {
        if (gpio_is_valid(lcd_en->gpio))
            gpio_direction_output(lcd_en->gpio, 0);
        if (gpio_is_valid(bl_en->gpio))
            gpio_direction_output(bl_en->gpio, 0);
    }

	lt8911_info->power = power;
	return 0;
}

static int panel_get_power(struct lcd_device *lcd)
{
	struct panel_dev *lt8911_info = lcd_get_data(lcd);

	return lt8911_info->power;
}

static struct lcd_ops panel_lcd_ops = {
    .set_power = panel_set_power,
    .get_power = panel_get_power,
};

static int _of_get_named_gpio_lvl(struct device *dev, short *gpio, short *lvl, char *of_name)
{
	int ret = 0;
	enum of_gpio_flags flags;

	/* devm_gpio_request_one */
	*gpio = of_get_named_gpio_flags(dev->of_node, of_name, 0, &flags);
	if (gpio_is_valid(*gpio))
	{
		*lvl = (flags == OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		ret = gpio_request_one(*gpio, GPIOF_DIR_OUT, of_name);
		if (ret < 0)
		{
			dev_err(dev, "Failed to request reset pin!\n");
			return ret;
		}
	}
	else
	{
		dev_warn(dev, "invalid gpio: %s\n", of_name);
		return -1;
	}
	return 0;
}

static int of_panel_parse(struct device *dev)
{
	struct panel_dev *lt8911_info = dev_get_drvdata(dev);

    /* only warning */
	_of_get_named_gpio_lvl(dev,
					  &lt8911_info->reset.gpio,
					  &lt8911_info->reset.active_level,
					  "lt8911,reset-gpio");

	_of_get_named_gpio_lvl(dev,
					  &lt8911_info->lcd_en.gpio,
					  &lt8911_info->lcd_en.active_level,
					  "lt8911,lcd_en-gpio");

	_of_get_named_gpio_lvl(dev,
				&lt8911_info->bl_en.gpio,
				&lt8911_info->bl_en.active_level,
				"lt8911,bl_en-gpio");

	return 0;
}

static struct mipi_dsim_lcd_driver panel_dev_dsim_ddi_driver = {
    .name = "lt8911",
    .id = -1,
};

static struct mipi_dsim_lcd_device panel_dev_device = {
    .name = "lt8911",
    .id = 0,
};

static int lt8911_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0, i;
	struct device *dev = &client->dev;

	lt8911_info = kzalloc(sizeof(struct panel_dev), GFP_KERNEL);
	if (lt8911_info == NULL)
	{
		dev_err(dev, "Failed to alloc memory!");
		return -ENOMEM;
	}
	lt8911_info->dev = dev;
	lt8911_info->client = client;
	lt8911_info->panel_name = "m140imn476-n010";
	dev_set_drvdata(dev, lt8911_info);

	// set dev_info.
	ret = of_panel_parse(dev);
	if (ret < 0)
	{
		goto err_of_parse;
	}

	// register lcd device.
	lt8911_info->lcd = lcd_device_register("panel_lcd", dev, lt8911_info, &panel_lcd_ops);
	if (IS_ERR_OR_NULL(lt8911_info->lcd))
	{
		dev_err(dev, "Error register lcd!\n");
		ret = -EINVAL;
		goto err_of_parse;
	}

	mipi_dsi_register_lcd_device(&panel_dev_device);
	mipi_dsi_register_lcd_driver(&panel_dev_dsim_ddi_driver);

	ret = ingenicfb_register_panel(&lcd_panel[i]);
	if (ret < 0)
	{
		dev_err(dev, "Failed to register lcd panel!\n");
		goto err_lcd_register;
	}

	// register ingenicfb device.
	/* TODO: should this power status sync from uboot */
	lt8911_info->power = FB_BLANK_POWERDOWN;
	if (panel_set_power(lt8911_info->lcd, FB_BLANK_UNBLANK))
		goto err_lcd_register;

	printk("\033[32mregister %s sucess.\033[0m\n", lt8911_info->panel_name);

	return 0;

err_lcd_register:
	lcd_device_unregister(lt8911_info->lcd);
err_of_parse:
	kfree(lt8911_info);
	printk("\033[31mregister %s dailed.\033[0m\n", lt8911_info->panel_name);
	return ret;
}

static int lt8911_remove(struct i2c_client *client)
{
	return 0;
}

static const struct of_device_id lt8911_match_table[] = {
    {
	.compatible = "mipi2edp,lt8911",
    },
    {},
};
MODULE_DEVICE_TABLE(of, lt8911_match_table);

static const struct i2c_device_id lt8911_id[] = {
    {
	"lt8911",
    },
    {}};
MODULE_DEVICE_TABLE(i2c, lt8911_id);

static struct i2c_driver lt8911_driver = {
    .probe = lt8911_probe,
    .remove = lt8911_remove,
    .driver = {
	.name = "lt8911",
	.owner = THIS_MODULE,
#ifdef CONFIG_OF
	.of_match_table = of_match_ptr(lt8911_match_table),
#endif
    },
    .id_table = lt8911_id,
};

static int __init lt8911_init(void)
{
	int ret;
	printk("lt8911 driver installing...");
	ret = i2c_add_driver(&lt8911_driver);
	if (ret != 0)
	{
		printk("lt8911 driver init failed!\n");
	}

	return ret;
}

static void __exit lt8911_exit(void)
{
	printk("lt8911 driver exited.\n");
	i2c_del_driver(&lt8911_driver);
}

/* module_i2c_driver(lt8911_driver); */
module_init(lt8911_init);
module_exit(lt8911_exit);

MODULE_DESCRIPTION("LT8911 Driver");
MODULE_LICENSE("GPL");
