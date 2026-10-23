// SPDX-License-Identifier: GPL-2.0
/*
 * GM8773C single MIPI input to dual MIPI output panel driver.
 *
 * The downstream panels are two JD9365DA 800x1280 modules.  The DPU/DSI side
 * exposes them as one 1600x1280 surface and GM8773C splits it left/right.
 */

#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/lcd.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>

#include "../ingenicfb.h"
#include "../jz_dsim.h"
#include "gm8773c.h"

#define DRIVER_NAME			"gm8773c"
#define GM8773C_PASSWORD		0xaa
#define GM8773C_PANEL_NAME		"gm8773c-jd9365da"

#define GM8773C_TX_HACTIVE		800
#define GM8773C_TX_VACTIVE		1280
#define GM8773C_TX_HFP			100
#define GM8773C_TX_HSW			10
#define GM8773C_TX_HBP			100
#define GM8773C_TX_VFP			20
#define GM8773C_TX_VSW			2
#define GM8773C_TX_VBP			30
#define GM8773C_TX_HTOTAL		(GM8773C_TX_HACTIVE + GM8773C_TX_HFP + \
					 GM8773C_TX_HSW + GM8773C_TX_HBP)
#define GM8773C_TX_VTOTAL		(GM8773C_TX_VACTIVE + GM8773C_TX_VFP + \
					 GM8773C_TX_VSW + GM8773C_TX_VBP)
#define GM8773C_MIPI_RX_CLK	900
#define GM8773C_REFCLK_SELECT		REFCLK_MIPI
#define GM8773C_REF_DIV			16
#define GM8773C_REF_INT			40
#define GM8773C_REF_POSTDIV		1
#define MIPI_OUT_CLK  ((((GM8773C_MIPI_RX_CLK / GM8773C_REF_DIV) * GM8773C_REF_INT) * GM8773C_REF_POSTDIV)/2)
#define GM8773C_DISPLAY_MODE		left_and_right_mode
#define GM8773C_EOTP_ENABLE		1
#define GM8773C_MIPI_MODE		SYNC_EVENT
#define GM8773C_TX_CLOCK_MODE		0x00
#define GM8773C_MIPI_TX_LPX		0x05

#define GM8773C_DCS_MAX_PAYLOAD		16

struct board_gpio {
	short gpio;
	short active_level;
};

struct gm8773c_jd9365da {
	struct device *dev;
	unsigned int power;
	unsigned int id;

	struct lcd_device *ld;
	struct backlight_device *bd;

	struct mipi_dsim_lcd_device *dsim_dev;

};

struct panel_dev {
	struct device *dev;
	struct lcd_panel *panel;
	struct lcd_device *lcd;
	int power;
	struct gpio_desc *reset1;
	struct gpio_desc *reset2;
	struct gpio_desc *vdden1;
	struct gpio_desc *vdden2;

	struct gpio_desc *pwm1;
	struct gpio_desc *pwm2;
	int i2c_id;
	struct i2c_client *client;
	char *panel_name;
	struct mipi_dsim_lcd_device *dsim_dev;
};

struct gm8773c_dcs_setting_entry {
	enum dcs_cmd_name cmd_name;
	enum dcs_tx0_cmd_num tx0_lpnum;
	enum dcs_tx1_cmd_num tx1_lpnum;
	u8 count;
	u8 para_list[GM8773C_DCS_MAX_PAYLOAD];
	enum mipi_send_tx channel_lp;
};

static struct panel_dev *gm8773c_info;

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

#define GM8773C_DCS_LONG2(id, cmd, val) \
	{ LONG_WRITE_CMD##id, TX0_LP_NUM1, TX1_LP_NUM1, 10, \
	  { 0x39, 0x02, 0x00, cmd, val, 0x39, 0x02, 0x00, cmd, val }, \
	  TX0_AND_TX1_SEND }

#define GM8773C_DCS_LONG4(id, b0, b1, b2, b3) \
	{ LONG_WRITE_CMD##id, TX0_LP_NUM1, TX1_LP_NUM1, 14, \
	  { 0x39, 0x04, 0x00, b0, b1, b2, b3, \
	    0x39, 0x04, 0x00, b0, b1, b2, b3 }, \
	  TX0_AND_TX1_SEND }

static const struct gm8773c_dcs_setting_entry dcs_custom_table[] = {
	GM8773C_DCS_LONG4(1, 0xff, 0x98, 0x81, 0x03),
	GM8773C_DCS_LONG2(2, 0x01, 0x00),
	GM8773C_DCS_LONG2(3, 0x02, 0x00),
	GM8773C_DCS_LONG2(4, 0x03, 0x53),
	GM8773C_DCS_LONG2(5, 0x04, 0xd3),
	GM8773C_DCS_LONG2(6, 0x05, 0x00),
	GM8773C_DCS_LONG2(7, 0x06, 0x0d),
	GM8773C_DCS_LONG2(8, 0x07, 0x08),
	GM8773C_DCS_LONG2(9, 0x08, 0x00),
	GM8773C_DCS_LONG2(10, 0x09, 0x00),
	GM8773C_DCS_LONG2(11, 0x0a, 0x00),
	GM8773C_DCS_LONG2(12, 0x0b, 0x00),
	GM8773C_DCS_LONG2(13, 0x0c, 0x00),
	GM8773C_DCS_LONG2(14, 0x0d, 0x00),
	GM8773C_DCS_LONG2(15, 0x0e, 0x00),
	GM8773C_DCS_LONG2(16, 0x0f, 0x28),
	GM8773C_DCS_LONG2(17, 0x10, 0x28),
	GM8773C_DCS_LONG2(18, 0x11, 0x00),
	GM8773C_DCS_LONG2(19, 0x12, 0x00),
	GM8773C_DCS_LONG2(20, 0x13, 0x00),
	GM8773C_DCS_LONG2(21, 0x14, 0x00),
	GM8773C_DCS_LONG2(22, 0x15, 0x00),
	GM8773C_DCS_LONG2(23, 0x16, 0x00),
	GM8773C_DCS_LONG2(24, 0x17, 0x00),
	GM8773C_DCS_LONG2(25, 0x18, 0x00),
	GM8773C_DCS_LONG2(26, 0x19, 0x00),
	GM8773C_DCS_LONG2(27, 0x1a, 0x00),
	GM8773C_DCS_LONG2(28, 0x1b, 0x00),
	GM8773C_DCS_LONG2(29, 0x1c, 0x00),
	GM8773C_DCS_LONG2(30, 0x1d, 0x00),
	GM8773C_DCS_LONG2(31, 0x1e, 0x40),
	GM8773C_DCS_LONG2(32, 0x1f, 0x80),
	GM8773C_DCS_LONG2(33, 0x20, 0x06),
	GM8773C_DCS_LONG2(34, 0x21, 0x01),
	GM8773C_DCS_LONG2(35, 0x22, 0x00),
	GM8773C_DCS_LONG2(36, 0x23, 0x00),
	GM8773C_DCS_LONG2(37, 0x24, 0x00),
	GM8773C_DCS_LONG2(38, 0x25, 0x00),
	GM8773C_DCS_LONG2(39, 0x26, 0x00),
	GM8773C_DCS_LONG2(40, 0x27, 0x00),
	GM8773C_DCS_LONG2(41, 0x28, 0x33),
	GM8773C_DCS_LONG2(42, 0x29, 0x33),
	GM8773C_DCS_LONG2(43, 0x2a, 0x00),
	GM8773C_DCS_LONG2(44, 0x2b, 0x00),
	GM8773C_DCS_LONG2(45, 0x2c, 0x00),
	GM8773C_DCS_LONG2(46, 0x2d, 0x00),
	GM8773C_DCS_LONG2(47, 0x2e, 0x00),
	GM8773C_DCS_LONG2(48, 0x2f, 0x00),
	GM8773C_DCS_LONG2(49, 0x30, 0x00),
	GM8773C_DCS_LONG2(50, 0x31, 0x00),
	GM8773C_DCS_LONG2(51, 0x32, 0x00),
	GM8773C_DCS_LONG2(52, 0x32, 0x00),
	GM8773C_DCS_LONG2(53, 0x33, 0x00),
	GM8773C_DCS_LONG2(54, 0x34, 0x03),
	GM8773C_DCS_LONG2(55, 0x35, 0x00),
	GM8773C_DCS_LONG2(56, 0x36, 0x00),
	GM8773C_DCS_LONG2(57, 0x37, 0x00),
	GM8773C_DCS_LONG2(58, 0x38, 0x96),
	GM8773C_DCS_LONG2(59, 0x39, 0x00),
	GM8773C_DCS_LONG2(60, 0x3a, 0x00),
	GM8773C_DCS_LONG2(61, 0x3b, 0x00),
	GM8773C_DCS_LONG2(62, 0x3c, 0x00),
	GM8773C_DCS_LONG2(63, 0x3d, 0x00),
	GM8773C_DCS_LONG2(64, 0x3e, 0x00),
	GM8773C_DCS_LONG2(65, 0x3f, 0x00),
	GM8773C_DCS_LONG2(66, 0x40, 0x00),
	GM8773C_DCS_LONG2(67, 0x41, 0x00),
	GM8773C_DCS_LONG2(68, 0x42, 0x00),
	GM8773C_DCS_LONG2(69, 0x43, 0x00),
	GM8773C_DCS_LONG2(70, 0x44, 0x00),
	GM8773C_DCS_LONG2(71, 0x50, 0x00),
	GM8773C_DCS_LONG2(72, 0x51, 0x23),
	GM8773C_DCS_LONG2(73, 0x52, 0x45),
	GM8773C_DCS_LONG2(74, 0x53, 0x67),
	GM8773C_DCS_LONG2(75, 0x54, 0x89),
	GM8773C_DCS_LONG2(76, 0x55, 0xab),
	GM8773C_DCS_LONG2(77, 0x56, 0x01),
	GM8773C_DCS_LONG2(78, 0x57, 0x23),
	GM8773C_DCS_LONG2(79, 0x58, 0x45),
	GM8773C_DCS_LONG2(80, 0x59, 0x67),
	GM8773C_DCS_LONG2(81, 0x5a, 0x89),
	GM8773C_DCS_LONG2(82, 0x5b, 0xab),
	GM8773C_DCS_LONG2(83, 0x5c, 0xcd),
	GM8773C_DCS_LONG2(84, 0x5d, 0xef),
	GM8773C_DCS_LONG2(85, 0x5e, 0x00),
	GM8773C_DCS_LONG2(86, 0x5f, 0x08),
	GM8773C_DCS_LONG2(87, 0x60, 0x08),
	GM8773C_DCS_LONG2(88, 0x61, 0x06),
	GM8773C_DCS_LONG2(89, 0x62, 0x06),
	GM8773C_DCS_LONG2(90, 0x63, 0x01),
	GM8773C_DCS_LONG2(91, 0x64, 0x01),
	GM8773C_DCS_LONG2(92, 0x65, 0x00),
	GM8773C_DCS_LONG2(93, 0x66, 0x00),
	GM8773C_DCS_LONG2(94, 0x67, 0x02),
	GM8773C_DCS_LONG2(95, 0x68, 0x15),
	GM8773C_DCS_LONG2(96, 0x69, 0x15),
	GM8773C_DCS_LONG2(97, 0x6a, 0x14),
	GM8773C_DCS_LONG2(98, 0x6b, 0x14),
	GM8773C_DCS_LONG2(99, 0x6c, 0x0d),
	GM8773C_DCS_LONG2(100, 0x6d, 0x0d),
	GM8773C_DCS_LONG2(101, 0x6e, 0x0c),
	GM8773C_DCS_LONG2(102, 0x6f, 0x0c),
	GM8773C_DCS_LONG2(103, 0x70, 0x0f),
	GM8773C_DCS_LONG2(104, 0x71, 0x0f),
	GM8773C_DCS_LONG2(105, 0x72, 0x0e),
	GM8773C_DCS_LONG2(106, 0x73, 0x0e),
	GM8773C_DCS_LONG2(107, 0x74, 0x02),
	GM8773C_DCS_LONG2(108, 0x75, 0x08),
	GM8773C_DCS_LONG2(109, 0x76, 0x08),
	GM8773C_DCS_LONG2(110, 0x77, 0x06),
	GM8773C_DCS_LONG2(111, 0x78, 0x06),
	GM8773C_DCS_LONG2(112, 0x79, 0x01),
	GM8773C_DCS_LONG2(113, 0x7a, 0x01),
	GM8773C_DCS_LONG2(114, 0x7b, 0x00),
	GM8773C_DCS_LONG2(115, 0x7c, 0x00),
	GM8773C_DCS_LONG2(116, 0x7d, 0x02),
	GM8773C_DCS_LONG2(117, 0x7e, 0x15),
	GM8773C_DCS_LONG2(118, 0x7f, 0x15),
	GM8773C_DCS_LONG2(119, 0x80, 0x14),
	GM8773C_DCS_LONG2(120, 0x81, 0x14),
	GM8773C_DCS_LONG2(121, 0x82, 0x0d),
	GM8773C_DCS_LONG2(122, 0x83, 0x0d),
	GM8773C_DCS_LONG2(123, 0x84, 0x0c),
	GM8773C_DCS_LONG2(124, 0x85, 0x0c),
	GM8773C_DCS_LONG2(125, 0x86, 0x0f),
	GM8773C_DCS_LONG2(126, 0x87, 0x0f),
	GM8773C_DCS_LONG2(127, 0x88, 0x0e),
	GM8773C_DCS_LONG2(128, 0x89, 0x0e),
	GM8773C_DCS_LONG2(129, 0x8a, 0x02),
	GM8773C_DCS_LONG4(130, 0xff, 0x98, 0x81, 0x04),
	GM8773C_DCS_LONG2(131, 0x6e, 0x2b),
	GM8773C_DCS_LONG2(132, 0x6f, 0x37),
	GM8773C_DCS_LONG2(133, 0x3a, 0xa4),
	GM8773C_DCS_LONG2(134, 0x8d, 0x1a),
	GM8773C_DCS_LONG2(135, 0x87, 0xba),
	GM8773C_DCS_LONG2(136, 0xb2, 0xd1),
	GM8773C_DCS_LONG2(137, 0x88, 0x0b),
	GM8773C_DCS_LONG2(138, 0x38, 0x01),
	GM8773C_DCS_LONG2(139, 0x39, 0x00),
	GM8773C_DCS_LONG2(140, 0xb5, 0x07),
	GM8773C_DCS_LONG2(141, 0x31, 0x75),
	GM8773C_DCS_LONG2(142, 0x3b, 0x98),
	GM8773C_DCS_LONG4(143, 0xff, 0x98, 0x81, 0x01),
	GM8773C_DCS_LONG2(144, 0x22, 0x0a),
	GM8773C_DCS_LONG2(145, 0x31, 0x00),
	GM8773C_DCS_LONG2(146, 0x53, 0x4d),
	GM8773C_DCS_LONG2(147, 0x55, 0x4d),
	GM8773C_DCS_LONG2(148, 0x50, 0x95),
	GM8773C_DCS_LONG2(149, 0x51, 0x9a),
	GM8773C_DCS_LONG2(150, 0x60, 0x10),
	GM8773C_DCS_LONG2(151, 0x62, 0x20),
	GM8773C_DCS_LONG2(152, 0xa0, 0x00),
	GM8773C_DCS_LONG2(153, 0xa1, 0x01),
	GM8773C_DCS_LONG2(154, 0xa2, 0x18),
	GM8773C_DCS_LONG2(155, 0xa3, 0x1a),
	GM8773C_DCS_LONG2(156, 0xa4, 0x1e),
	GM8773C_DCS_LONG2(157, 0xa5, 0x31),
	GM8773C_DCS_LONG2(158, 0xa6, 0x25),
	GM8773C_DCS_LONG2(159, 0xa7, 0x24),
	GM8773C_DCS_LONG2(160, 0xa8, 0x8b),
	GM8773C_DCS_LONG2(161, 0xa9, 0x1f),
	GM8773C_DCS_LONG2(162, 0xaa, 0x2a),
	GM8773C_DCS_LONG2(163, 0xab, 0x76),
	GM8773C_DCS_LONG2(164, 0xac, 0x1a),
	GM8773C_DCS_LONG2(165, 0xad, 0x18),
	GM8773C_DCS_LONG2(166, 0xae, 0x4b),
	GM8773C_DCS_LONG2(167, 0xaf, 0x1f),
	GM8773C_DCS_LONG2(168, 0xb0, 0x27),
	GM8773C_DCS_LONG2(169, 0xb1, 0x49),
	GM8773C_DCS_LONG2(170, 0xb2, 0x56),
	GM8773C_DCS_LONG2(171, 0xb3, 0x23),
	GM8773C_DCS_LONG2(172, 0xc0, 0x00),
	GM8773C_DCS_LONG2(173, 0xc1, 0x2b),
	GM8773C_DCS_LONG2(174, 0xc2, 0x38),
	GM8773C_DCS_LONG2(175, 0xc3, 0x11),
	GM8773C_DCS_LONG2(176, 0xc4, 0x13),
	GM8773C_DCS_LONG2(177, 0xc5, 0x25),
	GM8773C_DCS_LONG2(178, 0xc6, 0x1b),
	GM8773C_DCS_LONG2(179, 0xc7, 0x1e),
	GM8773C_DCS_LONG2(180, 0xc8, 0x9f),
	GM8773C_DCS_LONG2(181, 0xc9, 0x1c),
	GM8773C_DCS_LONG2(182, 0xca, 0x2a),
	GM8773C_DCS_LONG2(183, 0xcb, 0x89),
	GM8773C_DCS_LONG2(184, 0xcc, 0x1b),
	GM8773C_DCS_LONG2(185, 0xcd, 0x1a),
	GM8773C_DCS_LONG2(186, 0xce, 0x4f),
	GM8773C_DCS_LONG2(187, 0xcf, 0x27),
	GM8773C_DCS_LONG2(188, 0xd0, 0x2d),
	GM8773C_DCS_LONG2(189, 0xd1, 0x51),
	GM8773C_DCS_LONG2(190, 0xd2, 0x5f),
	GM8773C_DCS_LONG2(191, 0xd3, 0x23),
	GM8773C_DCS_LONG4(192, 0xff, 0x98, 0x81, 0x00),
	GM8773C_DCS_LONG2(193, 0x36, 0x03),
};

static int gm8773c_write(struct i2c_client *client, u8 reg, u8 val)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0)
		dev_err(&client->dev, "write reg 0x%02x val 0x%02x failed: %d\n",
			reg, val, ret);

	return ret;
}

static int gm8773c_read(struct i2c_client *client, u8 reg, u8 *val)
{
	int ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		dev_err(&client->dev, "read reg 0x%02x failed: %d\n", reg, ret);
		return ret;
	}

	*val = ret & 0xff;
	return 0;
}

static int gm8773c_write_payload(struct i2c_client *client, u8 reg,
				 const u8 *data, u8 len)
{
	u8 buf[GM8773C_DCS_MAX_PAYLOAD + 1];
	struct i2c_msg msg;
	int ret;

	if (len > GM8773C_DCS_MAX_PAYLOAD)
		return -EINVAL;

	buf[0] = reg;
	memcpy(&buf[1], data, len);

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = len + 1;
	msg.buf = buf;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret == 1)
		return 0;

	if (ret >= 0)
		ret = -EIO;

	dev_err(&client->dev, "write reg 0x%02x payload failed: %d\n", reg, ret);
	return ret;
}

static int gm8773c_send_lp_both(struct i2c_client *client, const u8 *cmd,
				u8 cmd_len)
{
	u8 payload[GM8773C_DCS_MAX_PAYLOAD];
	int ret;

	if (cmd_len * 2 > GM8773C_DCS_MAX_PAYLOAD)
		return -EINVAL;

	memcpy(payload, cmd, cmd_len);
	memcpy(payload + cmd_len, cmd, cmd_len);

	ret = gm8773c_write(client, 0xed, TX0_LP_NUM1);
	if (ret < 0)
		return ret;
	ret = gm8773c_write(client, 0xee, TX1_LP_NUM1);
	if (ret < 0)
		return ret;
	ret = gm8773c_write_payload(client, 0xef, payload, cmd_len * 2);
	if (ret < 0)
		return ret;

	return gm8773c_write(client, 0xfb, TX0_AND_TX1_SEND);
}

static int gm8773c_password(struct i2c_client *client)
{
	u8 val;
	int ret;

	ret = gm8773c_write(client, PASSWORD_REG, GM8773C_PASSWORD);
	if (ret < 0)
		return ret;

	ret = gm8773c_read(client, PASSWORD_REG, &val);
	if (ret < 0)
		return ret;

	if (val != GM8773C_PASSWORD)
		dev_warn(&client->dev, "password readback mismatch: 0x%02x\n", val);

	return 0;
}

static int gm8773c_pllsetting(struct i2c_client *client)
{
	int ret;
	u8 ref_div;
	u8 postdiv;

	if (gm8773c_basic_cfg.refclk_div <= 15)
		ref_div = gm8773c_basic_cfg.refclk_div & 0xff;
	else if (gm8773c_basic_cfg.refclk_div == 16)
		ref_div = 0x00;
	else
		ref_div = (((gm8773c_basic_cfg.refclk_div) / 2) & 0x0f) | 0x10;

	ret = gm8773c_write(client, 0x01, ref_div);
	if (ret < 0)
		return ret;

	ret = gm8773c_write(client, 0x02, gm8773c_basic_cfg.refclk_int & 0xff);
	if (ret < 0)
		return ret;

	switch (gm8773c_basic_cfg.refclk_postdiv) {
	case 1:
		postdiv = 0x40;
		break;
	case 2:
		postdiv = 0x48;
		break;
	case 4:
		postdiv = 0x50;
		break;
	case 8:
		postdiv = 0x58;
		break;
	default:
		dev_err(&client->dev, "invalid refclk_postdiv %d\n",
			gm8773c_basic_cfg.refclk_postdiv);
		return -EINVAL;
	}

	ret = gm8773c_write(client, 0x2c, postdiv);
	if (ret < 0)
		return ret;

	return gm8773c_write(client, 0x7f, 0x10);
}

static int gm8773c_sync_dly(struct i2c_client *client)
{
	int ret;
	u16 sync_dly;

	sync_dly = ((gm8773c_basic_cfg.tx0_htotal / 2) + 200) & 0x0fff;

	ret = gm8773c_write(client, 0xbd,
			    ((gm8773c_basic_cfg.tx1_vactive & 0x0f00) >> 8) |
			    ((sync_dly & 0x0f00) >> 4));
	if (ret < 0)
		return ret;

	return gm8773c_write(client, 0xbe, sync_dly & 0x00ff);
}
#define GM8773C_BIST 0
#define RED
#if GM8773C_BIST
static int gm8773c_bist(struct i2c_client *client)
{
	int bist_line_length;
#ifdef GREEN
	gm8773c_write(client,0x3F,0xFF);
	gm8773c_write(client,0x40,0x00);
	gm8773c_write(client,0x41,0x00);
#endif
#ifdef BLUE
	gm8773c_write(client,0x3F,0x00);
	gm8773c_write(client,0x40,0xFF);
	gm8773c_write(client,0x41,0x00);
#endif
#ifdef RED
	gm8773c_write(client,0x3F,0x00);
	gm8773c_write(client,0x40,0x00);
	gm8773c_write(client,0x41,0xFF);
#endif
	bist_line_length = (16666*MIPI_OUT_CLK)/(GM8773C_TX_VTOTAL);
	gm8773c_write(client,0x42,bist_line_length & 0x00FF);
	gm8773c_write(client,0x43,((bist_line_length & 0xFF00) >> 8));
	return 0;
}
#endif

static int gm8773c_display_timing(struct i2c_client *client)
{
	int ret;

	ret = gm8773c_write(client, 0xb6, gm8773c_basic_cfg.vsync_width & 0xff);
	if (ret < 0)
		return ret;
	ret = gm8773c_write(client, 0xb7, gm8773c_basic_cfg.vback_porch & 0xff);
	if (ret < 0)
		return ret;
	ret = gm8773c_write(client, 0xb8,
			    ((gm8773c_basic_cfg.vback_porch & 0xf00) >> 8) |
			    ((gm8773c_basic_cfg.vsync_width & 0xf00) >> 4));
	if (ret < 0)
		return ret;
	ret = gm8773c_write(client, 0xb9, gm8773c_basic_cfg.tx0_vactive & 0xff);
	if (ret < 0)
		return ret;
	ret = gm8773c_write(client, 0xba, gm8773c_basic_cfg.vfront_porch & 0xff);
	if (ret < 0)
		return ret;
	ret = gm8773c_write(client, 0xbb,
			    (gm8773c_basic_cfg.tx0_vactive & 0x0f00) >> 4);
	if (ret < 0)
		return ret;
	ret = gm8773c_write(client, 0xbc, gm8773c_basic_cfg.tx1_vactive & 0xff);
	if (ret < 0)
		return ret;
	ret = gm8773c_sync_dly(client);
	if (ret < 0)
		return ret;
	ret = gm8773c_write(client, 0xb3, gm8773c_basic_cfg.tx0_hactive & 0xff);
	if (ret < 0)
		return ret;
	ret = gm8773c_write(client, 0xb4, gm8773c_basic_cfg.tx1_hactive & 0xff);
	if (ret < 0)
		return ret;
	ret = gm8773c_write(client, 0xb5,
			    ((gm8773c_basic_cfg.tx1_hactive & 0x0f00) >> 4) |
			    ((gm8773c_basic_cfg.tx0_hactive & 0x0f00) >> 8));
	if (ret < 0)
		return ret;
	ret = gm8773c_write(client, 0xbf, gm8773c_basic_cfg.hsync_width & 0xff);
	if (ret < 0)
		return ret;
	ret = gm8773c_write(client, 0xc0,
			    (gm8773c_basic_cfg.hsync_width & 0xff00) >> 8);
	if (ret < 0)
		return ret;
	ret = gm8773c_write(client, 0xc1, gm8773c_basic_cfg.hback_porch & 0xff);
	if (ret < 0)
		return ret;
	ret = gm8773c_write(client, 0xc2,
			    (gm8773c_basic_cfg.hback_porch & 0xff00) >> 8);
	if (ret < 0)
		return ret;
	ret = gm8773c_write(client, 0xc3, gm8773c_basic_cfg.hfront_porch & 0xff);
	if (ret < 0)
		return ret;

	ret = gm8773c_write(client, 0xc4, (gm8773c_basic_cfg.hfront_porch & 0xff00) >> 8);
	if (ret < 0)
		return ret;
#if GM8773C_BIST
	gm8773c_bist(client);
#endif
	return  0;
}

static int gm8773c_display_mode(struct i2c_client *client)
{
	int ret;
	u8 eotp = gm8773c_basic_cfg.eotp ? 0x10 : 0x00;

	ret = gm8773c_write(client, 0xaa, gm8773c_basic_cfg.display_mode | eotp);
	if (ret < 0)
		return ret;
	ret = gm8773c_write(client, 0xab, gm8773c_basic_cfg.mipi_tx_lpx_num);
	if (ret < 0)
		return ret;
	ret = gm8773c_write(client, 0xa1, GM8773C_MIPI_MODE | GM8773C_TX_CLOCK_MODE);
	if (ret < 0)
		return ret;

	return gm8773c_write(client, 0x06, 0x11);
}

static int gm8773c_dcs_regist_ef(struct i2c_client *client)
{
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(dcs_custom_table); i++) {
		const struct gm8773c_dcs_setting_entry *entry = &dcs_custom_table[i];

		if (entry->cmd_name == DELAY) {
			mdelay(entry->para_list[0]);
			continue;
		}

		ret = gm8773c_write(client, 0xed, entry->tx0_lpnum);
		if (ret < 0)
			return ret;
		ret = gm8773c_write(client, 0xee, entry->tx1_lpnum);
		if (ret < 0)
			return ret;
		ret = gm8773c_write_payload(client, 0xef,
					    entry->para_list, entry->count);
		if (ret < 0)
			return ret;
		ret = gm8773c_write(client, 0xfb, entry->channel_lp);
		if (ret < 0)
			return ret;
//		mdelay(1);
	}

	return 0;
}

static int gm8773c_jd9365da_display_on(struct i2c_client *client)
{
	static const u8 sleep_out[] = { 0x05, 0x11, 0x00 };
	static const u8 display_on[] = { 0x05, 0x29, 0x00 };
	int ret;

	ret = gm8773c_send_lp_both(client, sleep_out, sizeof(sleep_out));
	if (ret < 0)
		return ret;
	mdelay(10);

	ret = gm8773c_send_lp_both(client, display_on, sizeof(display_on));
	if (ret < 0)
		return ret;
	mdelay(5);

	return 0;
}

static int gm8773c_set_output(struct i2c_client *client)
{
	return gm8773c_write(client, 0xff, 0x01);
}

static int gm8773c_debug(struct i2c_client *client)
{
	printk("tx0_hactive   = %d    tx0_vactive =%d", gm8773c_basic_cfg.tx0_hactive,gm8773c_basic_cfg.tx0_vactive);
	printk("hsync_width   = %d", gm8773c_basic_cfg.hsync_width);
	printk("hback_porch   = %d",gm8773c_basic_cfg.hback_porch);
	printk("tx0_htotal    = %d    tx0_vtotal  =%d", gm8773c_basic_cfg.tx0_htotal, gm8773c_basic_cfg.tx0_vtotal);
	printk("tx1_hactive   = %d    tx1_vactive =%d", gm8773c_basic_cfg.tx1_hactive ,gm8773c_basic_cfg.tx1_vactive);
	printk("mipi_rx_clk   = %d", gm8773c_basic_cfg.mipi_rx_clk);
	printk("refclk_div    = %d", gm8773c_basic_cfg.refclk_div);
	printk("refclk_int    = %d", gm8773c_basic_cfg.refclk_int);
	printk("refclk_postdiv= %d", gm8773c_basic_cfg.refclk_postdiv);
	return 0;
}

static int gm8773c_bridge_init(struct i2c_client *client)
{
	int ret;

	gm8773c_debug(client);
	ret = gm8773c_password(client);
	 gm8773c_write(client, 0xff, 0x0);
	if (ret < 0)
		return ret;
	ret = gm8773c_write(client, 0x14, GM8773C_REFCLK_SELECT);
	if (ret < 0)
		return ret;
	ret = gm8773c_pllsetting(client);
	if (ret < 0)
		return ret;
	ret = gm8773c_display_timing(client);
	if (ret < 0)
		return ret;
	ret = gm8773c_display_mode(client);
	if (ret < 0)
		return ret;
	ret = gm8773c_dcs_regist_ef(client);
	if (ret < 0)
		return ret;
	ret = gm8773c_jd9365da_display_on(client);
	if (ret < 0)
		return ret;
	return gm8773c_set_output(client);
}

static int gm8773c_config_init(struct i2c_client *client)
{
	gm8773c_basic_cfg.tx0_hactive = GM8773C_TX_HACTIVE;
	gm8773c_basic_cfg.tx1_hactive = GM8773C_TX_HACTIVE;
	gm8773c_basic_cfg.hfront_porch = GM8773C_TX_HFP;
	gm8773c_basic_cfg.hsync_width = GM8773C_TX_HSW;
	gm8773c_basic_cfg.hback_porch = GM8773C_TX_HBP;
	gm8773c_basic_cfg.tx0_htotal = GM8773C_TX_HTOTAL;
	gm8773c_basic_cfg.tx0_vactive = GM8773C_TX_VACTIVE;
	gm8773c_basic_cfg.tx1_vactive = GM8773C_TX_VACTIVE;
	gm8773c_basic_cfg.vfront_porch = GM8773C_TX_VFP;
	gm8773c_basic_cfg.vsync_width = GM8773C_TX_VSW;
	gm8773c_basic_cfg.vback_porch = GM8773C_TX_VBP;
	gm8773c_basic_cfg.tx0_vtotal = GM8773C_TX_VTOTAL;
	gm8773c_basic_cfg.mipi_rx_clk = GM8773C_MIPI_RX_CLK;
	gm8773c_basic_cfg.select_refclk = GM8773C_REFCLK_SELECT;
	gm8773c_basic_cfg.refclk_div = GM8773C_REF_DIV;
	gm8773c_basic_cfg.refclk_int = GM8773C_REF_INT;
	gm8773c_basic_cfg.refclk_postdiv = GM8773C_REF_POSTDIV;
	gm8773c_basic_cfg.mipi_tx_lpx_num = GM8773C_MIPI_TX_LPX;
	gm8773c_basic_cfg.display_mode = GM8773C_DISPLAY_MODE;
	gm8773c_basic_cfg.eotp = GM8773C_EOTP_ENABLE;

	return gm8773c_bridge_init(client);
}
static void gm8773c_reset_panels(struct panel_dev *panel)
{
	gpiod_set_value_cansleep(panel->reset1, 0);
	gpiod_set_value_cansleep(panel->reset2, 0);
	mdelay(10);
	gpiod_set_value_cansleep(panel->vdden1, 1);
	gpiod_set_value_cansleep(panel->vdden2, 1);
	mdelay(10);
	gpiod_set_value_cansleep(panel->vdden1, 0);
	gpiod_set_value_cansleep(panel->vdden2, 0);

    if (panel->pwm1)
        gpiod_set_value_cansleep(panel->pwm1, 1);
    if (panel->pwm2)
        gpiod_set_value_cansleep(panel->pwm2, 1);

	mdelay(50);
	gpiod_set_value_cansleep(panel->reset1, 1);
	gpiod_set_value_cansleep(panel->reset2, 1);
	mdelay(120);
}

static struct fb_videomode panel_modes = {
	.name = GM8773C_PANEL_NAME,
	.refresh = 60,
	.xres = GM8773C_TX_HACTIVE * 2,
	.yres = GM8773C_TX_VACTIVE,
	.left_margin = GM8773C_TX_HBP,
	.right_margin = GM8773C_TX_HFP,
	.hsync_len = GM8773C_TX_HSW,
	.upper_margin = GM8773C_TX_VBP,
	.lower_margin = GM8773C_TX_VFP,
	.vsync_len = GM8773C_TX_VSW,
	.sync = FB_SYNC_HOR_HIGH_ACT & FB_SYNC_VERT_HIGH_ACT,
	.vmode = FB_VMODE_NONINTERLACED,
	.flag = 0,
};

static struct jzdsi_data jzdsi_pdata = {
	.modes = &panel_modes,
	.video_config.no_of_lanes = 4,
	.video_config.virtual_channel = 0,
	.video_config.color_coding = COLOR_CODE_24BIT,
	.video_config.video_mode = VIDEO_BURST_WITH_SYNC_PULSES,
	.video_config.receive_ack_packets = 0,
	.video_config.is_18_loosely = 0,
	.video_config.data_en_polarity = 1,
	.video_config.byte_clock = 0,
	.video_config.byte_clock_coef = MIPI_PHY_BYTE_CLK_COEF_MUL6_DIV5,

	.dsi_config.max_lanes = 4,
	.dsi_config.max_hs_to_lp_cycles = 100,
	.dsi_config.max_lp_to_hs_cycles = 40,
	.dsi_config.max_bta_cycles = 4095,
	.dsi_config.color_mode_polarity = 1,
	.dsi_config.shut_down_polarity = 1,
	.dsi_config.max_bps = 2750,
	.bpp_info = 24,
};

static struct tft_config gm8773c_cfg = {
	.pix_clk_inv = 0,
	.de_dl = 0,
	.sync_dl = 0,
	.color_even = TFT_LCD_COLOR_EVEN_RGB,
	.color_odd = TFT_LCD_COLOR_ODD_RGB,
	.mode = TFT_LCD_MODE_PARALLEL_888,
};

static struct lcd_panel lcd_panel[] = {
	[0] = {
		.name = GM8773C_PANEL_NAME,
		.num_modes = 1,
		.modes = &panel_modes,
		.dsi_pdata = &jzdsi_pdata,
		.lcd_type = LCD_TYPE_MIPI_TFT,
		.tft_config = &gm8773c_cfg,
		.bpp = 24,
		.width = 124,
		.height = 110,
		.dither_enable = 0,
		.dither.dither_red = 0,
		.dither.dither_green = 0,
		.dither.dither_blue = 0,
		.ops = &panel_ops,
	},
};

#define POWER_IS_ON(pwr)	((pwr) <= FB_BLANK_NORMAL)

static int panel_set_power(struct lcd_device *lcd, int power)
{
	return 0;
}

static int panel_get_power(struct lcd_device *lcd)
{
	struct panel_dev *info = lcd_get_data(lcd);

	return info->power;
}

static struct lcd_ops panel_lcd_ops = {
	.set_power = panel_set_power,
	.get_power = panel_get_power,
};

static void panel_dev_power_on(struct mipi_dsim_lcd_device *dsim_dev, int power)
{
	struct panel_dev *info = gm8773c_info;
	int ret;
	gm8773c_reset_panels(info);

	info->power = power;
}

static int panel_dev_bridge_init(struct mipi_dsim_lcd_device *dsim_dev)
{
	struct panel_dev *info = gm8773c_info;
	int ret;
	ret = gm8773c_config_init(info->client);
	if (ret < 0) {
		dev_err(info->dev, "failed to configure GM8773C: %d\n", ret);
		return ret;
	}
	return 0;
}
static int panel_dev_probe(struct mipi_dsim_lcd_device *dsim_dev)
{
	struct gm8773c_jd9365da *lcd;
	int ret;
	lcd = devm_kzalloc(&dsim_dev->dev, sizeof(struct gm8773c_jd9365da), GFP_KERNEL);
	if (!lcd)
	{
		dev_err(&dsim_dev->dev, "failed to allocate gm8773c-jd9365da structure.\n");
		return -ENOMEM;
	}

	lcd->dsim_dev = dsim_dev;
	lcd->dev = &dsim_dev->dev;

	lcd->ld = lcd_device_register("gm8773c-jd9365da", lcd->dev, lcd,
				      &panel_lcd_ops);
	if (IS_ERR(lcd->ld))
	{
		dev_err(lcd->dev, "failed to register lcd ops.\n");
		return PTR_ERR(lcd->ld);
	}

	dev_set_drvdata(&dsim_dev->dev, lcd);


	dev_dbg(lcd->dev, "probed gm8773c-jd9365da panel driver.\n");


	gm8773c_info->dsim_dev = dsim_dev;


	return 0;

}

static int panel_suspend(struct mipi_dsim_lcd_device *dsim_dev)
{
	struct panel_dev *panel = gm8773c_info;
	gpiod_set_value_cansleep(panel->reset1, 1);
	gpiod_set_value_cansleep(panel->reset2, 1);
    if (panel->pwm1)
        gpiod_set_value_cansleep(panel->pwm1, 0);
    if (panel->pwm2)
        gpiod_set_value_cansleep(panel->pwm2, 0);
	return 0;
}

static int panel_resume(struct mipi_dsim_lcd_device *dsim_dev)
{
	struct panel_dev *panel = gm8773c_info;
	gpiod_set_value_cansleep(panel->reset1, 0);
	gpiod_set_value_cansleep(panel->reset2, 0);
    if (panel->pwm1)
        gpiod_set_value_cansleep(panel->pwm1, 1);
    if (panel->pwm2)
        gpiod_set_value_cansleep(panel->pwm2, 1);
	return 0;
}

static struct mipi_dsim_lcd_driver panel_dev_dsim_ddi_driver = {
	.name = GM8773C_PANEL_NAME,
	.id = -1,
	.power_on = panel_dev_power_on,
	.bridge_init = panel_dev_bridge_init,
	.probe = panel_dev_probe,
	.suspend = panel_suspend,
	.resume = panel_resume,
};

static struct mipi_dsim_lcd_device panel_dev_device = {
	.name = GM8773C_PANEL_NAME,
	.id = 0,
};

static int of_panel_parse(struct device *dev)
{
	struct panel_dev *panel = dev_get_drvdata(dev);
	panel->reset1 = devm_gpiod_get_optional(dev, "jd9365da,reset1", GPIOD_OUT_HIGH);
	if(IS_ERR(panel->reset1)){
		dev_err(dev, "Failed to request rst pin!\n");
		return PTR_ERR(panel->reset1);
	}
	panel->reset2 = devm_gpiod_get_optional(dev, "jd9365da,reset2", GPIOD_OUT_HIGH);
	if(IS_ERR(panel->reset2)){
		dev_err(dev, "Failed to request rst pin!\n");
		return PTR_ERR(panel->reset2);
	}
	panel->pwm1 = devm_gpiod_get_optional(dev, "jd9365da,pwm1", GPIOD_OUT_HIGH);
	if(IS_ERR(panel->pwm1)){
        panel->pwm1 = NULL;
		dev_warn(dev, "Failed to request pwm1 pin!\n");
	}
	panel->pwm2 = devm_gpiod_get_optional(dev, "jd9365da,pwm2", GPIOD_OUT_HIGH);
	if(IS_ERR(panel->pwm2)){
        panel->pwm2 = NULL;
		dev_warn(dev, "Failed to request pwm2 pin!\n");
	}
	panel->vdden1 = devm_gpiod_get_optional(dev, "jd9365da,vdden1", GPIOD_OUT_HIGH);
	if(IS_ERR(panel->vdden1)){
		dev_err(dev, "Failed to request vdden pin!\n");
		return PTR_ERR(panel->vdden1);
	}
	panel->vdden2 = devm_gpiod_get_optional(dev, "jd9365da,vdden2", GPIOD_OUT_HIGH);
	if(IS_ERR(panel->vdden2)){
		dev_err(dev, "Failed to request vdden pin!\n");
		return PTR_ERR(panel->vdden2);
	}

	return 0;
}

static int gm8773c_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct panel_dev *info;
	int ret;
	int i;
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &client->dev;
	info->client = client;
	info->panel_name = GM8773C_PANEL_NAME;
	info->power = FB_BLANK_POWERDOWN;
	dev_set_drvdata(&client->dev, info);
	gm8773c_info = info;

	ret = of_panel_parse(&client->dev);
	if (ret < 0)
		goto err_free_info;

	ret = mipi_dsi_register_lcd_device(&panel_dev_device);

	ret = mipi_dsi_register_lcd_driver(&panel_dev_dsim_ddi_driver);

	ret = ingenicfb_register_panel(&lcd_panel[i]);

	dev_info(&client->dev, "registered %s\n", info->panel_name);
	return 0;

err_free_info:
	gm8773c_info = NULL;
	kfree(info);
	return ret;
}

static int gm8773c_remove(struct i2c_client *client)
{
	struct panel_dev *info = dev_get_drvdata(&client->dev);

	if (!info)
		return 0;

	if (info->lcd)
		lcd_device_unregister(info->lcd);

	if (gm8773c_info == info)
		gm8773c_info = NULL;

	kfree(info);
	return 0;
}

static const struct of_device_id gm8773c_of_match[] = {
	{ .compatible = "ingenic,gm8773c" },
	{ }
};
MODULE_DEVICE_TABLE(of, gm8773c_of_match);

static const struct i2c_device_id gm8773c_id[] = {
	{ "gm8773c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, gm8773c_id);

static struct i2c_driver gm8773c_driver = {
	.probe = gm8773c_probe,
	.remove = gm8773c_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(gm8773c_of_match),
	},
	.id_table = gm8773c_id,
};

static int __init gm8773c_init(void)
{
	int ret;

	printk("gm8773c driver installing...\n");
	ret = i2c_add_driver(&gm8773c_driver);
	if (ret)
		printk("gm8773c driver init failed: %d\n", ret);

	return ret;
}

static void __exit gm8773c_exit(void)
{
	printk("gm8773c driver exited.\n");
	i2c_del_driver(&gm8773c_driver);
}

module_init(gm8773c_init);
module_exit(gm8773c_exit);

MODULE_DESCRIPTION("GM8773C dual MIPI JD9365DA panel driver");
MODULE_LICENSE("GPL");
