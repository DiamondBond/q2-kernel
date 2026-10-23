/*
 * panel-GM8775C.c
 *
 * @brief
 * @author MPU系统软件部团队 Yolanda
 *
 * @copyright 版权所有 (北京君正集成电路股份有限公司) {2025}
 * @copyright Copyright© 2025 Ingenic Semiconductor Co.,Ltd
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/pwm_backlight.h>
#include <linux/delay.h>
#include <linux/lcd.h>
#include <linux/of.h>

#include "../ingenicfb.h"

#define PANEL_GM8775C_DEBUG
#ifdef PANEL_GM8775C_DEBUG
#define PANEL_DEBUG_MSG(msg...)			\
 printk("PANEL GM8775C: " msg)
#else
#define PANEL_DEBUG_MSG(msg...)			\
 do { } while(0)
#endif

struct panel_dev {
	/* ingenic frame buffer */
	struct i2c_client *client;
	struct device *dev;
	struct lcd_panel *panel;
	/* common lcd framework */
	struct lcd_device *lcd;
	int power;
	int i2c_id;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *lcd_en_gpio;
	struct gpio_desc *power_en_gpio;
	char *panel_name;
};

/* Panel timing specifications for M215O30 1920x1080 */
#define FPS    60    /* Refresh rate: 60Hz */
#define HACT   1920  /* Horizontal active pixels */
#define VACT   1080  /* Vertical active lines */

#define HFP    88    /* Horizontal front porch */
#define HBP    148   /* Horizontal back porch */
#define HS     44    /* Horizontal sync width */

#define VFP    4     /* Vertical front porch */
#define VBP    36    /* Vertical back porch */
#define VS     5     /* Vertical sync width */

////////////////////////////GM8775C begin/////////////////////////////////////////////

static unsigned int i2c_write(struct i2c_client *client, unsigned char addr, unsigned char value);
static unsigned char i2c_read(struct i2c_client *client, u8 addr);

static void GM8775C_SystemInt(struct i2c_client *client)
{
	i2c_write(client,0x00,0xAA);
	i2c_write(client,0x48,0x02);
	i2c_write(client,0xB6,0x20);
	i2c_write(client,0x01,0x80);
	i2c_write(client,0x02,0x38);
	i2c_write(client,0x03,0x47);
	i2c_write(client,0x04,0x58);
	i2c_write(client,0x05,0x2C);
	i2c_write(client,0x06,0x94);
	i2c_write(client,0x07,0x00);
	i2c_write(client,0x08,0x04);
	i2c_write(client,0x09,0x05);
	i2c_write(client,0x0A,0x24);
	i2c_write(client,0x0B,0x02);
	i2c_write(client,0x0C,0x52);
	i2c_write(client,0x0D,0x01);
	i2c_write(client,0x0E,0x80);
	i2c_write(client,0x0F,0x20);
	i2c_write(client,0x10,0x20);
	i2c_write(client,0x11,0x03);
	i2c_write(client,0x12,0x1B);
	i2c_write(client,0x13,0x63);
	i2c_write(client,0x14,0x01);
	i2c_write(client,0x15,0x23);
	i2c_write(client,0x16,0x40);
	i2c_write(client,0x17,0x00);
	i2c_write(client,0x18,0x01);
	i2c_write(client,0x19,0x23);
	i2c_write(client,0x1A,0x40);
	i2c_write(client,0x1B,0x00);
	i2c_write(client,0x1E,0x46);
	i2c_write(client,0x51,0x30);
	i2c_write(client,0x88,0x40);
	i2c_write(client,0x1F,0x10);
	i2c_write(client,0x2A,0x01);
}

int GM8775C_MIPI2LVDS_Config(struct i2c_client *client)
{
	GM8775C_SystemInt(client);
    return 0;
}

////////////////////////////GM8775C end/////////////////////////////////////////////

static struct panel_dev *gm8775c_info;

static void panel_enable(struct lcd_panel *panel)
{
}

static void panel_disable(struct lcd_panel *panel)
{
}

static struct lcd_panel_ops panel_ops = {
	.enable  = (void*)panel_enable,
	.disable = (void*)panel_disable,
};

static struct fb_videomode panel_modes = {
	.name                   = "gm8775c-M215O30",
	.refresh                = FPS,
	.xres                   = HACT,
	.yres                   = VACT,
	.left_margin            = HBP,
	.right_margin           = HFP,
	.upper_margin           = VBP,
	.lower_margin           = VFP,

	.hsync_len              = HS,
	.vsync_len              = VS,
	.sync                   = FB_SYNC_HOR_HIGH_ACT & FB_SYNC_VERT_HIGH_ACT,
	.vmode                  = FB_VMODE_NONINTERLACED,
	.flag                   = 0,
};

struct jzdsi_data jzdsi_pdata = {
	.modes = &panel_modes,
	.video_config.no_of_lanes = 4,
	.video_config.virtual_channel = 0,
	.video_config.color_coding = COLOR_CODE_24BIT,
	.video_config.video_mode = VIDEO_BURST_WITH_SYNC_PULSES,
	.video_config.receive_ack_packets = 0,
	.video_config.is_18_loosely = 0,
	.video_config.data_en_polarity = 1,
	.video_config.byte_clock = 0, // driver will auto calculate byte_clock.
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

static struct tft_config gm8775c_cfg = {
	.pix_clk_inv = 0,
	.de_dl = 0,
	.sync_dl = 0,
	.color_even = TFT_LCD_COLOR_EVEN_RGB,
	.color_odd = TFT_LCD_COLOR_ODD_RGB,
	.mode = TFT_LCD_MODE_PARALLEL_888,
};

struct lcd_panel lcd_panel[] = {
	[0] = {
		.name = "GM8775C",
		.modes = &panel_modes,
		.num_modes = 1,
		.dsi_pdata = &jzdsi_pdata,

		.bpp = 24,
		.width = 699,
		.height = 197,

		.lcd_type = LCD_TYPE_MIPI_TFT,
		.tft_config = &gm8775c_cfg,

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

static unsigned int i2c_write(struct i2c_client *client,unsigned char addr,unsigned char value)
{
	return i2c_smbus_write_byte_data(client, addr, value);
}

void gm8775c_reset(struct panel_dev *gm8775c_info)
{
	gpiod_set_value(gm8775c_info->reset_gpio, 1);
	mdelay(1);
	gpiod_set_value(gm8775c_info->reset_gpio, 0);
	mdelay(100);
	gpiod_set_value(gm8775c_info->reset_gpio, 1);
	mdelay(100);
}

#define POWER_IS_ON(pwr)        ((pwr) <= FB_BLANK_NORMAL)
static int panel_set_power(struct lcd_device *lcd, int power)
{
	struct panel_dev *gm8775c_info = lcd_get_data(lcd);
	struct i2c_client *client = gm8775c_info->client;

	if(POWER_IS_ON(power) && !POWER_IS_ON(gm8775c_info->power)) {
		gpiod_set_value(gm8775c_info->power_en_gpio, 1);
		mdelay(15);
		gpiod_set_value(gm8775c_info->lcd_en_gpio, 1);
		mdelay(15);
		gm8775c_reset(gm8775c_info);
		mdelay(500);

		if (GM8775C_MIPI2LVDS_Config(client)) {
			dev_err(&client->dev, "Failed to configure GM8775C\n");
			goto err_config;
		}
	}
	gm8775c_info->power = power;
	return 0;

err_config:
	/* Power down on configuration failure */
	gpiod_set_value(gm8775c_info->lcd_en_gpio, 0);
	gpiod_set_value(gm8775c_info->power_en_gpio, 0);
	return -EIO;
}

static int panel_get_power(struct lcd_device *lcd)
{
	struct panel_dev *gm8775c_info = lcd_get_data(lcd);
	return gm8775c_info->power;
}

static struct lcd_ops panel_lcd_ops = {
	.set_power = panel_set_power,
	.get_power = panel_get_power,
};


static int of_panel_parse(struct device *dev)
{
	struct panel_dev *gm8775c_info = dev_get_drvdata(dev);
	int ret = 0;

	gm8775c_info->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(gm8775c_info->reset_gpio)) {
		dev_err(dev, "Failed to get reset-gpio\n");
		return PTR_ERR(gm8775c_info->reset_gpio);
	}

	/* Get power_en GPIO - start with power disabled (physically high for ACTIVE_LOW) */
	gm8775c_info->power_en_gpio = devm_gpiod_get(dev, "power_en", GPIOD_OUT_HIGH);
	if (IS_ERR(gm8775c_info->power_en_gpio)) {
		dev_err(dev, "Failed to get power_en-gpio\n");
		ret = PTR_ERR(gm8775c_info->power_en_gpio);
		goto err_reset_gpio;
	}

	gm8775c_info->lcd_en_gpio = devm_gpiod_get(dev, "lcd_en", GPIOD_ASIS);
	if (IS_ERR(gm8775c_info->lcd_en_gpio)) {
		dev_err(dev, "Failed to get lcd_en-gpio\n");
		ret = PTR_ERR(gm8775c_info->lcd_en_gpio);
		goto err_power_en_gpio;
	}

	return 0;

err_power_en_gpio:
	devm_gpiod_put(dev, gm8775c_info->power_en_gpio);
err_reset_gpio:
	devm_gpiod_put(dev, gm8775c_info->reset_gpio);
	return ret;
}

static struct mipi_dsim_lcd_driver panel_dev_dsim_ddi_driver = {
	.name = "gm8775c-M215O30",
	.id = -1,
};


struct mipi_dsim_lcd_device panel_dev_device={
	.name		= "gm8775c-M215O30",
	.id = 0,
};

static int gm8775c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0, i;
	struct device *dev = &client->dev;
	struct device_node *np = dev->of_node;

	gm8775c_info = kzalloc(sizeof(struct panel_dev), GFP_KERNEL);
	if(gm8775c_info == NULL) {
		dev_err(dev, "Failed to alloc memory!");
		return -ENOMEM;
	}
	gm8775c_info->dev = dev;
	gm8775c_info->client = client;
    gm8775c_info->panel_name = "GM8775C";
	dev_set_drvdata(dev, gm8775c_info);

	ret = of_panel_parse(dev);
	if(ret < 0) {
		goto err_of_parse;
	}
	// register lcd device.
	gm8775c_info->lcd = lcd_device_register("panel_lcd", dev, gm8775c_info, &panel_lcd_ops);
	if(IS_ERR_OR_NULL(gm8775c_info->lcd)) {
		dev_err(dev, "Error register lcd!\n");
		ret = -EINVAL;
		goto err_of_parse;
	}

	{
		const char *tmp_name = NULL;
		if (of_property_read_string(np, "gm8775c,panel_name", &tmp_name) == 0 && tmp_name) {
			gm8775c_info->panel_name = (char *)tmp_name;
		}
	}

	if (gm8775c_info->panel_name) {
		for (i = 0; i < ARRAY_SIZE(lcd_panel); ++i) {
			if (strcmp(lcd_panel[i].name, gm8775c_info->panel_name) == 0)
				break;
		}
		if (i == ARRAY_SIZE(lcd_panel)) {
			PANEL_DEBUG_MSG("\033[31munsupport panel!\033[0m\n");
			ret = -ENODEV;
			goto err_lcd_register;
		}
	} else {
		PANEL_DEBUG_MSG("\033[31mno found specid lcd panel!\033[0m\n");
		ret = -EINVAL;
		goto err_lcd_register;
	}

	mipi_dsi_register_lcd_device(&panel_dev_device);
	mipi_dsi_register_lcd_driver(&panel_dev_dsim_ddi_driver);
	ret = ingenicfb_register_panel(&lcd_panel[i]);
	if(ret < 0) {
		dev_err(dev, "Failed to register lcd panel!\n");
		goto err_lcd_register;
	}

	// register ingenicfb device.
	/* TODO: should this power status sync from uboot */
	gm8775c_info->power = FB_BLANK_POWERDOWN;
	if (panel_set_power(gm8775c_info->lcd, FB_BLANK_UNBLANK))
		goto err_lcd_register;

	PANEL_DEBUG_MSG("\033[32mregister %s sucess.\033[0m\n", gm8775c_info->panel_name);

	return 0;

err_lcd_register:
	lcd_device_unregister(gm8775c_info->lcd);
err_of_parse:
	kfree(gm8775c_info);
	PANEL_DEBUG_MSG("\033[31mregister %s dailed.\033[0m\n", gm8775c_info->panel_name);
	return ret;
}

static int gm8775c_remove(struct i2c_client *client)
{
	struct panel_dev *gm8775c_info = dev_get_drvdata(&client->dev);

	panel_set_power(gm8775c_info->lcd, FB_BLANK_POWERDOWN);

	lcd_device_unregister(gm8775c_info->lcd);

	kfree(gm8775c_info);

	return 0;
}

static const struct of_device_id gm8775c_match_table[] = {
		{.compatible = "mipi2lvds,gm8775c",},
		{ },
};
MODULE_DEVICE_TABLE(of, gm8775c_match_table);

static const struct i2c_device_id gm8775c_id[] = {
	{ "gm8775c", },
	{ }
};
MODULE_DEVICE_TABLE(i2c, gm8775c_id);

static struct i2c_driver gm8775c_driver = {
    .probe      = gm8775c_probe,
    .remove     = gm8775c_remove,
    .driver = {
        .name     = "gm8775c",
        .owner    = THIS_MODULE,
		.of_match_table = of_match_ptr(gm8775c_match_table),
    },
	.id_table = gm8775c_id,
};

static int __init gm8775c_init(void)
{
	int ret;
	PANEL_DEBUG_MSG("\033[33m%s\033[0m\n", "gm8775c driver installing...");
	ret = i2c_add_driver(&gm8775c_driver);
    if ( ret != 0 ) {
        PANEL_DEBUG_MSG("gm8775c driver init failed!\n");
    }

    return ret;
}

static void __exit gm8775c_exit(void)
{
	PANEL_DEBUG_MSG("gm8775c driver exited.\n");
	i2c_del_driver(&gm8775c_driver);
}

module_init(gm8775c_init);
module_exit(gm8775c_exit);

MODULE_DESCRIPTION("GM8775C Driver");
MODULE_LICENSE("GPL");
