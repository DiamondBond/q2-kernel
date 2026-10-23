/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ov5648 driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 add poweron function.
 * V0.0X01.0X02 fix mclk issue when probe multiple camera.
 * V0.0X01.0X03 add enum_frame_interval function.
 * V0.0X01.0X04 add quick stream on/off
 * V0.0X01.0X05 add function g_mbus_config
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-clk.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-image-sizes.h>
#include <linux/of_gpio.h>
#include <linux/clk.h>
#include <isp-sensor.h>
#include <linux/unistd.h>

#define OV5648_CHIP_ID_H	(0x56)
#define OV5648_CHIP_ID_L	(0x48)
#define OV5648_REG_END		0xffff
#define OV5648_REG_DELAY	0x0000

static bool debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

struct ov5648_win_size {
	unsigned int width;
	unsigned int height;
	unsigned int mbus_code;
	struct sensor_info sensor_info;
	enum v4l2_colorspace colorspace;
	void *regs;
};

struct ov5648_gpio {
	int pin;
	int active_level;
};

struct ov5648_supply{
	struct regulator *avdd;
	struct regulator *dvdd;
	struct regulator *dovdd;
};

struct ov5648_info {
	struct ov5648_supply supply;
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	struct media_pad pad;
	struct v4l2_ctrl *gain;
	struct v4l2_ctrl *again;
	struct v4l2_ctrl *exposure;
	struct v4l2_clk *clk;
	struct clk *sclka;
	struct v4l2_subdev_format *format;	/*current fmt.*/
	struct ov5648_win_size *win;
	struct ov5648_gpio reset;
	struct ov5648_gpio pwdn;
};

static inline struct ov5648_info *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct ov5648_info, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct ov5648_info, hdl)->sd;
}

/*
 * the part of driver maybe modify about different sensor and different board.
 */
struct again_lut {
	unsigned int value;
	unsigned int gain;
};

static struct again_lut ov5648_again_lut[] = {
	{0x10, 0},
	{0x11, 5732},
	{0x12, 11136},
	{0x13, 16248},
	{0x14, 21098},
	{0x15, 25711},
	{0x16, 30109},
	{0x17, 34312},
	{0x18, 38336},
	{0x19, 42196},
	{0x1a, 45904},
	{0x1b, 49472},
	{0x1c, 52911},
	{0x1d, 56229},
	{0x1e, 59434},
	{0x1f, 62534},
	{0x20, 65536},
	{0x21, 68445},
	{0x22, 71268},
	{0x23, 74009},
	{0x24, 76672},
	{0x25, 79263},
	{0x26, 81784},
	{0x27, 84240},
	{0x28, 86634},
	{0x29, 88969},
	{0x2a, 91247},
	{0x2b, 93472},
	{0x2c, 95645},
	{0x2d, 97770},
	{0x2e, 99848},
	{0x2f, 101882},
	{0x30, 103872},
	{0x31, 105822},
	{0x32, 107732},
	{0x33, 109604},
	{0x34, 111440},
	{0x35, 113241},
	{0x36, 115008},
	{0x37, 116743},
	{0x38, 118447},
	{0x39, 120120},
	{0x3a, 121765},
	{0x3b, 123381},
	{0x3c, 124970},
	{0x3d, 126533},
	{0x3e, 128070},
	{0x3f, 129583},
	{0x40, 131072},
	{0x41, 132538},
	{0x42, 133981},
	{0x43, 135403},
	{0x44, 136804},
	{0x45, 138184},
	{0x46, 139545},
	{0x47, 140886},
	{0x48, 142208},
	{0x49, 143512},
	{0x4a, 144799},
	{0x4b, 146068},
	{0x4c, 147320},
	{0x4d, 148556},
	{0x4e, 149776},
	{0x4f, 150981},
	{0x50, 152170},
	{0x51, 153344},
	{0x52, 154505},
	{0x53, 155651},
	{0x54, 156783},
	{0x55, 157902},
	{0x56, 159008},
	{0x57, 160101},
	{0x58, 161181},
	{0x59, 162250},
	{0x5a, 163306},
	{0x5b, 164351},
	{0x5c, 165384},
	{0x5d, 166406},
	{0x5e, 167418},
	{0x5f, 168418},
	{0x60, 169408},
	{0x61, 170388},
	{0x62, 171358},
	{0x63, 172318},
	{0x64, 173268},
	{0x65, 174209},
	{0x66, 175140},
	{0x67, 176062},
	{0x68, 176976},
	{0x69, 177881},
	{0x6a, 178777},
	{0x6b, 179665},
	{0x6c, 180544},
	{0x6d, 181416},
	{0x6e, 182279},
	{0x6f, 183135},
	{0x70, 183983},
	{0x71, 184823},
	{0x72, 185656},
	{0x73, 186482},
	{0x74, 187301},
	{0x75, 188112},
	{0x76, 188917},
	{0x77, 189715},
	{0x78, 190506},
	{0x79, 191291},
	{0x7a, 192069},
	{0x7b, 192841},
	{0x7c, 193606},
	{0x7d, 194366},
	{0x7e, 195119},
	{0x7f, 195866},
	{0x80, 196608},
};

struct regval_list {
	uint16_t reg_num;
	unsigned char value;
};

/*
 * Xclk 24Mhz
 * Pclk 84Mhz
 * linelength 2816(0xb00)
 * framelength 1984(0x7c0)
 * grabwindow_width 2592
 * grabwindow_height 1944
 * max_framerate 15fps
 * mipi_datarate per lane 420Mbps
 */
static struct regval_list ov5648_2592_1944_15fps_mipi_init_regs[] = {
	{0x0100, 0x00},
	{0x3001, 0x00},
	{0x3002, 0x00},
	{0x3011, 0x02},
	{0x3017, 0x05},
	{0x3018, 0x4c}, //bit[7:5] 001: 1lane;010: 2lane
	{0x301c, 0xd2},
	{0x3022, 0x00},
	{0x3034, 0x1a},
	{0x3035, 0x21},
	{0x3036, 0x69},
	{0x3037, 0x03},
	{0x3038, 0x00},
	{0x3039, 0x00},
	{0x303a, 0x00},
	{0x303b, 0x19},
	{0x303c, 0x11},
	{0x303d, 0x30},
	{0x3105, 0x11},
	{0x3106, 0x05},
	{0x3304, 0x28},
	{0x3305, 0x41},
	{0x3306, 0x30},
	{0x3308, 0x00},
	{0x3309, 0xc8},
	{0x330a, 0x01},
	{0x330b, 0x90},
	{0x330c, 0x02},
	{0x330d, 0x58},
	{0x330e, 0x03},
	{0x330f, 0x20},
	{0x3300, 0x00},
	{0x3500, 0x00},
	{0x3501, 0x3d},
	{0x3502, 0x00},
	{0x3503, 0x07},
	{0x350a, 0x00},
	{0x350b, 0x40},
	{0x3601, 0x33},
	{0x3602, 0x00},
	{0x3611, 0x0e},
	{0x3612, 0x2b},
	{0x3614, 0x50},
	{0x3620, 0x33},
	{0x3622, 0x00},
	{0x3630, 0xad},
	{0x3631, 0x00},
	{0x3632, 0x94},
	{0x3633, 0x17},
	{0x3634, 0x14},
	{0x3704, 0xc0},
	{0x3705, 0x2a},
	{0x3708, 0x66},
	{0x3709, 0x52},
	{0x370b, 0x23},
	{0x370c, 0xcf},
	{0x370d, 0x00},
	{0x370e, 0x00},
	{0x371c, 0x07},
	{0x3739, 0xd2},
	{0x373c, 0x00},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x0a},
	{0x3805, 0x3f},
	{0x3806, 0x07},
	{0x3807, 0xa3},
	{0x3808, 0x05},
	{0x3809, 0x10},
	{0x380a, 0x03},
	{0x380b, 0xcc},
	{0x380c, 0x0b},
	{0x380d, 0x00},
	{0x380e, 0x03},
	{0x380f, 0xe0},
	{0x3810, 0x00},
	{0x3811, 0x08},
	{0x3812, 0x00},
	{0x3813, 0x04},
	{0x3814, 0x31},
	{0x3815, 0x31},
	{0x3817, 0x00},
	{0x3820, 0x08},
	{0x3821, 0x07},
	{0x3826, 0x03},
	{0x3829, 0x00},
	{0x382b, 0x0b},
	{0x3830, 0x00},
	{0x3836, 0x00},
	{0x3837, 0x00},
	{0x3838, 0x00},
	{0x3839, 0x04},
	{0x383a, 0x00},
	{0x383b, 0x01},
	{0x3b00, 0x00},
	{0x3b02, 0x08},
	{0x3b03, 0x00},
	{0x3b04, 0x04},
	{0x3b05, 0x00},
	{0x3b06, 0x04},
	{0x3b07, 0x08},
	{0x3b08, 0x00},
	{0x3b09, 0x02},
	{0x3b0a, 0x04},
	{0x3b0b, 0x00},
	{0x3b0c, 0x3d},
	{0x3f01, 0x0d},
	{0x3f0f, 0xf5},
	{0x4000, 0x89},
	{0x4001, 0x02},
	{0x4002, 0x45},
	{0x4004, 0x02},
	{0x4005, 0x18},
	{0x4006, 0x08},
	{0x4007, 0x10},
	{0x4008, 0x00},
	{0x4050, 0x6e},
	{0x4051, 0x8f},
	{0x4300, 0xf8},
	{0x4303, 0xff},
	{0x4304, 0x00},
	{0x4307, 0xff},
	{0x4520, 0x00},
	{0x4521, 0x00},
	{0x4511, 0x22},
	{0x4801, 0x0f},
	{0x4814, 0x2a},
	{0x481f, 0x3c},
	{0x4823, 0x3c},
	{0x4826, 0x00},
	{0x481b, 0x3c},
	{0x4827, 0x32},
	{0x4837, 0x18},
	{0x4b00, 0x06},
	{0x4b01, 0x0a},
	{0x4b04, 0x10},
	{0x5000, 0xff},
	{0x5001, 0x00},
	{0x5002, 0x41},
	{0x5003, 0x0a},
	{0x5004, 0x00},
	{0x5043, 0x00},
	{0x5013, 0x00},
	{0x501f, 0x03},
	{0x503d, 0x00},
	{0x5780, 0xfc},
	{0x5781, 0x1f},
	{0x5782, 0x03},
	{0x5786, 0x20},
	{0x5787, 0x40},
	{0x5788, 0x08},
	{0x5789, 0x08},
	{0x578a, 0x02},
	{0x578b, 0x01},
	{0x578c, 0x01},
	{0x578d, 0x0c},
	{0x578e, 0x02},
	{0x578f, 0x01},
	{0x5790, 0x01},
	{0x5a00, 0x08},
	{0x5b00, 0x01},
	{0x5b01, 0x40},
	{0x5b02, 0x00},
	{0x5b03, 0xf0},

	// 2592x1944 15fps 2 lane MIPI 420Mbps/lane
	{0x0100, 0x00},
	{0x3501, 0x7b}, // exposure
	{0x3502, 0x00}, // exposure
	{0x3708, 0x63},
	{0x3709, 0x12},
	{0x370c, 0xcc}, // changed by AM05d
//	{0x3800, 0x02}, // xstart = 0
//	{0x3801, 0x20}, // xstart = 544
	{0x3800, 0x00}, // xstart = 0
	{0x3801, 0x00}, // xstart
	{0x3802, 0x00}, // ystart = 0
	{0x3803, 0x00}, // ystart
	{0x3804, 0x0a}, // xend = 2623
	{0x3805, 0x3f}, // xend
	{0x3806, 0x07}, // yend = 1955
	{0x3807, 0xa3}, // yend
//	{0x3808, 0x08}, // x output size = 2048
//	{0x3809, 0x00}, // x output size
	{0x3808, 0x0a}, // x output size = 2592
	{0x3809, 0x20}, // x output size
	{0x380a, 0x07}, // y output size = 1944
	{0x380b, 0x98}, // y output size
	{0x380c, 0x0b}, // hts = 2816
	{0x380d, 0x00}, // hts
	{0x380e, 0x07}, // vts = 1984
	{0x380f, 0xc0}, // vts
	{0x3810, 0x00}, // isp x win = 16
	{0x3811, 0x10}, // isp x win
	{0x3812, 0x00}, // isp y win = 6
	{0x3813, 0x06}, // isp y win
	{0x3814, 0x11}, // x inc
	{0x3815, 0x11}, // y inc
	{0x3817, 0x00}, // hsync start
	{0x3820, 0x40}, // flip off, v bin off
	{0x3821, 0x06}, // mirror on, v bin off
	{0x4004, 0x04}, // black line number
	{0x4005, 0x1a}, // blc always update
	{0x350b, 0x40}, // gain = 4x
	{0x4837, 0x17}, // MIPI global timing
//	{0x503d, 0x80}, // test pattern

	{OV5648_REG_END, 0x00},
};

static struct regval_list ov5648_480_1944_15fps_mipi_init_regs[] = {
	{0x0100, 0x00},
	{0x3001, 0x00},
	{0x3002, 0x00},
	{0x3011, 0x02},
	{0x3017, 0x05},
	{0x3018, 0x4c}, //bit[7:5] 001: 1lane;010: 2lane
	{0x301c, 0xd2},
	{0x3022, 0x00},
	{0x3034, 0x1a},
	{0x3035, 0x21},
	{0x3036, 0x69},
	{0x3037, 0x03},
	{0x3038, 0x00},
	{0x3039, 0x00},
	{0x303a, 0x00},
	{0x303b, 0x19},
	{0x303c, 0x11},
	{0x303d, 0x30},
	{0x3105, 0x11},
	{0x3106, 0x05},
	{0x3304, 0x28},
	{0x3305, 0x41},
	{0x3306, 0x30},
	{0x3308, 0x00},
	{0x3309, 0xc8},
	{0x330a, 0x01},
	{0x330b, 0x90},
	{0x330c, 0x02},
	{0x330d, 0x58},
	{0x330e, 0x03},
	{0x330f, 0x20},
	{0x3300, 0x00},
	{0x3500, 0x00},
	{0x3501, 0x3d},
	{0x3502, 0x00},
	{0x3503, 0x07},
	{0x350a, 0x00},
	{0x350b, 0x40},
	{0x3601, 0x33},
	{0x3602, 0x00},
	{0x3611, 0x0e},
	{0x3612, 0x2b},
	{0x3614, 0x50},
	{0x3620, 0x33},
	{0x3622, 0x00},
	{0x3630, 0xad},
	{0x3631, 0x00},
	{0x3632, 0x94},
	{0x3633, 0x17},
	{0x3634, 0x14},
	{0x3704, 0xc0},
	{0x3705, 0x2a},
	{0x3708, 0x66},
	{0x3709, 0x52},
	{0x370b, 0x23},
	{0x370c, 0xcf},
	{0x370d, 0x00},
	{0x370e, 0x00},
	{0x371c, 0x07},
	{0x3739, 0xd2},
	{0x373c, 0x00},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x0a},
	{0x3805, 0x3f},
	{0x3806, 0x07},
	{0x3807, 0xa3},
	{0x3808, 0x05},
	{0x3809, 0x10},
	{0x380a, 0x03},
	{0x380b, 0xcc},
	{0x380c, 0x0b},
	{0x380d, 0x00},
	{0x380e, 0x03},
	{0x380f, 0xe0},
	{0x3810, 0x00},
	{0x3811, 0x08},
	{0x3812, 0x00},
	{0x3813, 0x04},
	{0x3814, 0x31},
	{0x3815, 0x31},
	{0x3817, 0x00},
	{0x3820, 0x08},
	{0x3821, 0x07},
	{0x3826, 0x03},
	{0x3829, 0x00},
	{0x382b, 0x0b},
	{0x3830, 0x00},
	{0x3836, 0x00},
	{0x3837, 0x00},
	{0x3838, 0x00},
	{0x3839, 0x04},
	{0x383a, 0x00},
	{0x383b, 0x01},
	{0x3b00, 0x00},
	{0x3b02, 0x08},
	{0x3b03, 0x00},
	{0x3b04, 0x04},
	{0x3b05, 0x00},
	{0x3b06, 0x04},
	{0x3b07, 0x08},
	{0x3b08, 0x00},
	{0x3b09, 0x02},
	{0x3b0a, 0x04},
	{0x3b0b, 0x00},
	{0x3b0c, 0x3d},
	{0x3f01, 0x0d},
	{0x3f0f, 0xf5},
	{0x4000, 0x89},
	{0x4001, 0x02},
	{0x4002, 0x45},
	{0x4004, 0x02},
	{0x4005, 0x18},
	{0x4006, 0x08},
	{0x4007, 0x10},
	{0x4008, 0x00},
	{0x4050, 0x6e},
	{0x4051, 0x8f},
	{0x4300, 0xf8},
	{0x4303, 0xff},
	{0x4304, 0x00},
	{0x4307, 0xff},
	{0x4520, 0x00},
	{0x4521, 0x00},
	{0x4511, 0x22},
	{0x4801, 0x0f},
	{0x4814, 0x2a},
	{0x481f, 0x3c},
	{0x4823, 0x3c},
	{0x4826, 0x00},
	{0x481b, 0x3c},
	{0x4827, 0x32},
	{0x4837, 0x18},
	{0x4b00, 0x06},
	{0x4b01, 0x0a},
	{0x4b04, 0x10},
	{0x5000, 0xff},
	{0x5001, 0x00},
	{0x5002, 0x41},
	{0x5003, 0x0a},
	{0x5004, 0x00},
	{0x5043, 0x00},
	{0x5013, 0x00},
	{0x501f, 0x03},
	{0x503d, 0x00},
	{0x5780, 0xfc},
	{0x5781, 0x1f},
	{0x5782, 0x03},
	{0x5786, 0x20},
	{0x5787, 0x40},
	{0x5788, 0x08},
	{0x5789, 0x08},
	{0x578a, 0x02},
	{0x578b, 0x01},
	{0x578c, 0x01},
	{0x578d, 0x0c},
	{0x578e, 0x02},
	{0x578f, 0x01},
	{0x5790, 0x01},
	{0x5a00, 0x08},
	{0x5b00, 0x01},
	{0x5b01, 0x40},
	{0x5b02, 0x00},
	{0x5b03, 0xf0},

	// 2592x1944 15fps 2 lane MIPI 420Mbps/lane
	{0x0100, 0x00},
	{0x3501, 0x7b}, // exposure
	{0x3502, 0x00}, // exposure
	{0x3708, 0x63},
	{0x3709, 0x12},
	{0x370c, 0xcc}, // changed by AM05d
	{0x3800, 0x04}, // xstart = 1088
	{0x3801, 0x40}, // xstart
	{0x3802, 0x00}, // ystart = 0
	{0x3803, 0x00}, // ystart
	{0x3804, 0x0a}, // xend = 2623
	{0x3805, 0x3f}, // xend
	{0x3806, 0x07}, // yend = 1955
	{0x3807, 0xa3}, // yend
	{0x3808, 0x01}, // x output size = 480
	{0x3809, 0xE0}, // x output size
	{0x380a, 0x07}, // y output size = 1944
	{0x380b, 0x98}, // y output size
	{0x380c, 0x0b}, // hts = 2816
	{0x380d, 0x00}, // hts
	{0x380e, 0x07}, // vts = 1984
	{0x380f, 0xc0}, // vts
	{0x3810, 0x00}, // isp x win = 16
	{0x3811, 0x10}, // isp x win
	{0x3812, 0x00}, // isp y win = 6
	{0x3813, 0x06}, // isp y win
	{0x3814, 0x11}, // x inc
	{0x3815, 0x11}, // y inc
	{0x3817, 0x00}, // hsync start
	{0x3820, 0x40}, // flip off, v bin off
	{0x3821, 0x06}, // mirror on, v bin off
	{0x4004, 0x04}, // black line number
	{0x4005, 0x1a}, // blc always update
	{0x350b, 0x40}, // gain = 4x
	{0x4837, 0x17}, // MIPI global timing
//	{0x503d, 0x80}, // test pattern

	{OV5648_REG_END, 0x00},
};


/*
 * Xclk 24Mhz
 * Pclk 84Mhz
 * linelength 2816(0xb00)
 * framelength 992(0x3e0)
 * grabwindow_width 1296
 * grabwindow_height 972
 * max_framerate 30fps
 * mipi_datarate per lane 420Mbps
 */
static struct regval_list ov5648_1296_972_30fps_mipi_init_regs[] = {
	{0x0100, 0x00},
	{0x3001, 0x00},
	{0x3002, 0x00},
	{0x3011, 0x02},
	{0x3017, 0x05},
	{0x3018, 0x4c}, //bit[7:5] 001: 1lane;010: 2lane
	{0x301c, 0xd2},
	{0x3022, 0x00},
	{0x3034, 0x1a},
	{0x3035, 0x21},
	{0x3036, 0x69},
	{0x3037, 0x03},
	{0x3038, 0x00},
	{0x3039, 0x00},
	{0x303a, 0x00},
	{0x303b, 0x19},
	{0x303c, 0x11},
	{0x303d, 0x30},
	{0x3105, 0x11},
	{0x3106, 0x05},
	{0x3304, 0x28},
	{0x3305, 0x41},
	{0x3306, 0x30},
	{0x3308, 0x00},
	{0x3309, 0xc8},
	{0x330a, 0x01},
	{0x330b, 0x90},
	{0x330c, 0x02},
	{0x330d, 0x58},
	{0x330e, 0x03},
	{0x330f, 0x20},
	{0x3300, 0x00},
	{0x3500, 0x00},
	{0x3501, 0x3d},
	{0x3502, 0x00},
	{0x3503, 0x07},
	{0x350a, 0x00},
	{0x350b, 0x40},
	{0x3601, 0x33},
	{0x3602, 0x00},
	{0x3611, 0x0e},
	{0x3612, 0x2b},
	{0x3614, 0x50},
	{0x3620, 0x33},
	{0x3622, 0x00},
	{0x3630, 0xad},
	{0x3631, 0x00},
	{0x3632, 0x94},
	{0x3633, 0x17},
	{0x3634, 0x14},
	{0x3704, 0xc0},
	{0x3705, 0x2a},
	{0x3708, 0x66},
	{0x3709, 0x52},
	{0x370b, 0x23},
	{0x370c, 0xcf},
	{0x370d, 0x00},
	{0x370e, 0x00},
	{0x371c, 0x07},
	{0x3739, 0xd2},
	{0x373c, 0x00},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x0a},
	{0x3805, 0x3f},
	{0x3806, 0x07},
	{0x3807, 0xa3},
	{0x3808, 0x05},
	{0x3809, 0x10},
	{0x380a, 0x03},
	{0x380b, 0xcc},
	{0x380c, 0x0b},
	{0x380d, 0x00},
	{0x380e, 0x03},
	{0x380f, 0xe0},
	{0x3810, 0x00},
	{0x3811, 0x08},
	{0x3812, 0x00},
	{0x3813, 0x04},
	{0x3814, 0x31},
	{0x3815, 0x31},
	{0x3817, 0x00},
	{0x3820, 0x08},
	{0x3821, 0x07},
	{0x3826, 0x03},
	{0x3829, 0x00},
	{0x382b, 0x0b},
	{0x3830, 0x00},
	{0x3836, 0x00},
	{0x3837, 0x00},
	{0x3838, 0x00},
	{0x3839, 0x04},
	{0x383a, 0x00},
	{0x383b, 0x01},
	{0x3b00, 0x00},
	{0x3b02, 0x08},
	{0x3b03, 0x00},
	{0x3b04, 0x04},
	{0x3b05, 0x00},
	{0x3b06, 0x04},
	{0x3b07, 0x08},
	{0x3b08, 0x00},
	{0x3b09, 0x02},
	{0x3b0a, 0x04},
	{0x3b0b, 0x00},
	{0x3b0c, 0x3d},
	{0x3f01, 0x0d},
	{0x3f0f, 0xf5},
	{0x4000, 0x89},
	{0x4001, 0x02},
	{0x4002, 0x45},
	{0x4004, 0x02},
	{0x4005, 0x18},
	{0x4006, 0x08},
	{0x4007, 0x10},
	{0x4008, 0x00},
	{0x4050, 0x6e},
	{0x4051, 0x8f},
	{0x4300, 0xf8},
	{0x4303, 0xff},
	{0x4304, 0x00},
	{0x4307, 0xff},
	{0x4520, 0x00},
	{0x4521, 0x00},
	{0x4511, 0x22},
	{0x4801, 0x0f},
	{0x4814, 0x2a},
	{0x481f, 0x3c},
	{0x4823, 0x3c},
	{0x4826, 0x00},
	{0x481b, 0x3c},
	{0x4827, 0x32},
	{0x4837, 0x18},
	{0x4b00, 0x06},
	{0x4b01, 0x0a},
	{0x4b04, 0x10},
	{0x5000, 0xff},
	{0x5001, 0x00},
	{0x5002, 0x41},
	{0x5003, 0x0a},
	{0x5004, 0x00},
	{0x5043, 0x00},
	{0x5013, 0x00},
	{0x501f, 0x03},
	{0x503d, 0x00},
	{0x5780, 0xfc},
	{0x5781, 0x1f},
	{0x5782, 0x03},
	{0x5786, 0x20},
	{0x5787, 0x40},
	{0x5788, 0x08},
	{0x5789, 0x08},
	{0x578a, 0x02},
	{0x578b, 0x01},
	{0x578c, 0x01},
	{0x578d, 0x0c},
	{0x578e, 0x02},
	{0x578f, 0x01},
	{0x5790, 0x01},
	{0x5a00, 0x08},
	{0x5b00, 0x01},
	{0x5b01, 0x40},
	{0x5b02, 0x00},
	{0x5b03, 0xf0},

	// 1296x972 30fps 2 lane MIPI 420Mbps/lane
	{0x0100, 0x00},
	{0x3501, 0x3d}, // exposure
	{0x3502, 0x00}, // exposure
	{0x3708, 0x66},
	{0x3709, 0x52},
	{0x370c, 0xcf},
	{0x3800, 0x00}, // xstart = 0
	{0x3801, 0x00}, // x start
	{0x3802, 0x00}, // y start = 0
	{0x3803, 0x00}, // y start
	{0x3804, 0x0a}, // xend = 2623
	{0x3805, 0x3f}, // xend
	{0x3806, 0x07}, // yend = 1955
	{0x3807, 0xa3}, // yend
	{0x3808, 0x05}, // x output size = 1296
	{0x3809, 0x10}, // x output size
	{0x380a, 0x03}, // y output size = 972
	{0x380b, 0xcc}, // y output size
	{0x380c, 0x0b}, // hts = 2816
	{0x380d, 0x00}, // hts
	{0x380e, 0x03}, // vts = 992
	{0x380f, 0xe0}, // vts
	{0x3810, 0x00}, // isp x win = 8
	{0x3811, 0x08}, // isp x win
	{0x3812, 0x00}, // isp y win = 4
	{0x3813, 0x04}, // isp y win
	{0x3814, 0x31}, // x inc
	{0x3815, 0x31}, // y inc
	{0x3817, 0x00}, // hsync start
	{0x3820, 0x08}, // flip off, v bin off
	{0x3821, 0x07}, // mirror on, h bin on
	{0x4004, 0x02}, // black line number
	{0x4005, 0x18}, // blc level trigger
	{0x350b, 0x80}, // gain = 8x
	{0x4837, 0x17}, // MIPI global timing
	//{0x0100, 0x01},

	{OV5648_REG_END, 0x00},
};

/*
 * the part of driver was fixed.
 */
static struct regval_list ov5648_stream_on_mipi[] = {
	{0x0100,0x01},
	{OV5648_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list ov5648_stream_off_mipi[] = {
	{0x0100,0x00},
	{OV5648_REG_END, 0x00},	/* END MARKER */
};

static int ov5648_read(struct v4l2_subdev *sd, unsigned short reg,
		unsigned char *value)
{
	struct ov5648_info *info = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	uint8_t buf[2] = {(reg>>8)&0xff, reg&0xff};
	struct i2c_msg msg[2] = {
		[0] = {
			.addr	= client->addr,
			.flags	= 0,
			.len	= 2,
			.buf	= buf,
		},
		[1] = {
			.addr	= client->addr,
			.flags	= I2C_M_RD,
			.len	= 1,
			.buf	= value,
		}
	};
	int ret = 0;

	printk("i2c device addr : 0x%x~~~~~~~~~~~~~\n",client->addr);
	ret = i2c_transfer(client->adapter, msg, 2);
	if(ret > 0)
		ret = 0;

	return ret;
}

static int ov5648_write(struct v4l2_subdev *sd, unsigned short reg,
		unsigned char value)
{
	struct ov5648_info *info = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	uint8_t buf[3] = {(reg>>8)&0xff, reg&0xff, value};
	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.len	= 3,
		.buf	= buf,
	};
	int ret;
	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret > 0)
		ret = 0;

	return ret;
}

static int ov5648_read_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	int ret;
	unsigned char val;
	while (vals->reg_num != OV5648_REG_END) {
		if (vals->reg_num == OV5648_REG_DELAY) {
			msleep(vals->value);
		} else {
			ret = ov5648_read(sd, vals->reg_num, &val);
			if (ret < 0)
				return ret;
		}
		vals++;
	}

	return 0;
}

/*
 * Write a list of register settings; ff/ff stops the process.
 */
static int ov5648_write_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	int ret;
	while (vals->reg_num != OV5648_REG_END) {
		if (vals->reg_num == OV5648_REG_DELAY) {
			msleep(vals->value);
		} else {
			ret = ov5648_write(sd, vals->reg_num, vals->value);
			if (ret < 0)
				return ret;
		}
		vals++;
	}

	return 0;
}

/*
 * Stuff that knows about the sensor.
 */
static int ov5648_reset(struct v4l2_subdev *sd, u32 val)
{
	struct ov5648_info *info = to_state(sd);

	if(val) {
		gpio_direction_output(info->reset.pin, info->reset.active_level);
		msleep(10);
		gpio_direction_output(info->reset.pin, !info->reset.active_level);
		msleep(10);
	}

	return 0;
}

static int ov5648_pwdn(struct v4l2_subdev *sd, u32 val)
{
	struct ov5648_info *info = to_state(sd);

	if(val) {
		gpio_direction_output(info->pwdn.pin, !info->pwdn.active_level);
		msleep(10);
		gpio_direction_output(info->pwdn.pin, info->pwdn.active_level);
		msleep(10);
	}

	return 0;
}

static int ov5648_init(struct v4l2_subdev *sd, u32 val)
{
	struct ov5648_info *info = to_state(sd);
	int ret = 0;

	ret = ov5648_write_array(sd, info->win->regs);

	return ret;
}

static int ov5648_detect(struct v4l2_subdev *sd, unsigned int *ident)
{
	unsigned char v;
	int ret;
	ret = ov5648_read(sd, 0x300a, &v);
	printk("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != OV5648_CHIP_ID_H)
		return -ENODEV;
	*ident = v;

	ret = ov5648_read(sd, 0x300b, &v);
	printk("-----%s: %d ret = %d, v = 0x%02x\n", __func__, __LINE__, ret,v);
	if (ret < 0)
		return ret;
	if (v != OV5648_CHIP_ID_L)
		return -ENODEV;
	*ident = (*ident << 8) | v;

	return 0;
}

static struct ov5648_win_size ov5648_win_sizes[] = {
	{
		.width				= 480,
		.height				= 1944,
		.sensor_info.fps			= 15 << 16 | 1,
		.sensor_info.wdr_en				= 0,
		.sensor_info.mipi_cfg.clk			= 600,
		.sensor_info.mipi_cfg.twidth		= 480,
		.sensor_info.mipi_cfg.theight		= 1944,
		.sensor_info.total_height		= 0x7c0,
		.sensor_info.mipi_cfg.mipi_mode		= SENSOR_MIPI_OTHER_MODE,
		.sensor_info.mipi_cfg.mipi_vcomp_en		= 0,
		.sensor_info.mipi_cfg.mipi_hcomp_en		= 0,
		.sensor_info.mipi_cfg.mipi_crop_start0x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start0y	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start1x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start1y	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start2x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start2y	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start3x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start3y	= 0,
		.sensor_info.mipi_cfg.hcrop_diff_en		= 0,
		.sensor_info.mipi_cfg.line_sync_mode	= 0,
		.sensor_info.mipi_cfg.work_start_flag	= 0,
		.sensor_info.mipi_cfg.data_type_en		= 0,
		.sensor_info.mipi_cfg.data_type_value	= RAW10,
		.sensor_info.mipi_cfg.del_start		= 0,
		.sensor_info.mipi_cfg.sensor_frame_mode	= TX_SENSOR_DEFAULT_FRAME_MODE,
		.sensor_info.mipi_cfg.sensor_fid_mode	= 0,
		.sensor_info.mipi_cfg.sensor_mode		= TX_SENSOR_DEFAULT_MODE,
		.sensor_info.mipi_cfg.sensor_csi_fmt		= TX_SENSOR_RAW10,
		.mbus_code	= MEDIA_BUS_FMT_SBGGR10_1X10,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.regs 		= ov5648_480_1944_15fps_mipi_init_regs,


	},
	{
		.width				= 2592,
		.height				= 1944,
		.sensor_info.fps			= 15 << 16 | 1,
		.sensor_info.wdr_en				= 0,
		.sensor_info.mipi_cfg.clk			= 600,
		.sensor_info.mipi_cfg.twidth		= 2592,
		.sensor_info.mipi_cfg.theight		= 1944,
		.sensor_info.total_height		= 0x7c0,
		.sensor_info.mipi_cfg.mipi_mode		= SENSOR_MIPI_OTHER_MODE,
		.sensor_info.mipi_cfg.mipi_vcomp_en		= 0,
		.sensor_info.mipi_cfg.mipi_hcomp_en		= 0,
		.sensor_info.mipi_cfg.mipi_crop_start0x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start0y	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start1x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start1y	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start2x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start2y	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start3x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start3y	= 0,
		.sensor_info.mipi_cfg.hcrop_diff_en		= 0,
		.sensor_info.mipi_cfg.line_sync_mode	= 0,
		.sensor_info.mipi_cfg.work_start_flag	= 0,
		.sensor_info.mipi_cfg.data_type_en		= 0,
		.sensor_info.mipi_cfg.data_type_value	= RAW10,
		.sensor_info.mipi_cfg.del_start		= 0,
		.sensor_info.mipi_cfg.sensor_frame_mode	= TX_SENSOR_DEFAULT_FRAME_MODE,
		.sensor_info.mipi_cfg.sensor_fid_mode	= 0,
		.sensor_info.mipi_cfg.sensor_mode		= TX_SENSOR_DEFAULT_MODE,
		.sensor_info.mipi_cfg.sensor_csi_fmt		= TX_SENSOR_RAW10,
		.mbus_code	= MEDIA_BUS_FMT_SBGGR10_1X10,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.regs 		= ov5648_2592_1944_15fps_mipi_init_regs,


	},
	{
		.width				= 1296,
		.height				= 972,
		.sensor_info.fps			= 30 << 16 | 1,
		.sensor_info.wdr_en				= 0,
		.sensor_info.mipi_cfg.clk			= 600,
		.width					= 1296,
		.height					= 972,
		.sensor_info.mipi_cfg.twidth		= 1296,
		.sensor_info.mipi_cfg.theight		= 972,
		.sensor_info.total_height		= 0x3e0,
		.sensor_info.mipi_cfg.mipi_mode		= SENSOR_MIPI_OTHER_MODE,
		.sensor_info.mipi_cfg.mipi_vcomp_en		= 0,
		.sensor_info.mipi_cfg.mipi_hcomp_en		= 0,
		.sensor_info.mipi_cfg.mipi_crop_start0x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start0y	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start1x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start1y	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start2x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start2y	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start3x	= 0,
		.sensor_info.mipi_cfg.mipi_crop_start3y	= 0,
		.sensor_info.mipi_cfg.hcrop_diff_en		= 0,
		.sensor_info.mipi_cfg.line_sync_mode	= 0,
		.sensor_info.mipi_cfg.work_start_flag	= 0,
		.sensor_info.mipi_cfg.data_type_en		= 0,
		.sensor_info.mipi_cfg.data_type_value	= RAW10,
		.sensor_info.mipi_cfg.del_start		= 0,
		.sensor_info.mipi_cfg.sensor_frame_mode	= TX_SENSOR_DEFAULT_FRAME_MODE,
		.sensor_info.mipi_cfg.sensor_fid_mode	= 0,
		.sensor_info.mipi_cfg.sensor_mode		= TX_SENSOR_DEFAULT_MODE,
		.sensor_info.mipi_cfg.sensor_csi_fmt		= TX_SENSOR_RAW10,
		.mbus_code	= MEDIA_BUS_FMT_SBGGR10_1X10,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.regs					= ov5648_1296_972_30fps_mipi_init_regs,
	},
};

#if 0
static int ov5648_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= N_ov5648_FMTS)
		return -EINVAL;

	code->code = ov5648_formats[code->index].mbus_code;

	return 0;
}
#endif

/*
 * Set a format.
 */
static int ov5648_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct ov5648_format_struct *ovfmt;
	struct ov5648_win_size *wsize;
	struct ov5648_info *info = to_state(sd);
	int ret;

	if (format->pad)
		return -EINVAL;

	return 0;
}

static int ov5648_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct ov5648_info *info = to_state(sd);
	struct v4l2_mbus_framefmt *fmt = &format->format;
	int ret = 0;

	if(!info->win) {
		dev_err(sd->dev, "sensor win_size not set!\n");
		return -EINVAL;
	}

	fmt->width = info->win->width;
	fmt->height = info->win->height;
	fmt->code = info->win->mbus_code;
	fmt->colorspace = info->win->colorspace;
	*(unsigned int *)fmt->reserved = &info->win->sensor_info;

//	printk("----%s, %d, width: %d, height: %d, code: %x\n",
//			__func__, __LINE__, fmt->width, fmt->height, fmt->code);

	return ret;
}

static int ov5648_s_brightness(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int ov5648_s_contrast(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int ov5648_s_hflip(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int ov5648_s_vflip(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

/*
 * GAIN is split between REG_GAIN and REG_VREF[7:6].  If one believes
 * the data sheet, the VREF parts should be the most significant, but
 * experience shows otherwise.  There seems to be little value in
 * messing with the VREF bits, so we leave them alone.
 */
static int ov5648_g_gain(struct v4l2_subdev *sd, __s32 *value)
{
	int ret = 0;
	unsigned char gain = 0;

	*value = gain;
	return ret;
}

static int ov5648_s_gain(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static unsigned int again_to_regval(int gain)
{
	struct again_lut *lut = NULL;
	int i = 0;
	for(i = 0; i < ARRAY_SIZE(ov5648_again_lut); i++) {
		lut = &ov5648_again_lut[i];
		if(gain <= lut->gain) {
			return lut->value;
		}
	}

	/*last value.*/
	return lut->value;
}

static int ov5648_g_again(struct v4l2_subdev *sd, __s32 *value)
{
	struct ov5648_info *info = to_state(sd);
	unsigned char v = 0;
	unsigned int reg_val = 0;
	int ret = 0;

	return ret;
}
/*set analog gain db value, map value to sensor register.*/
static int ov5648_s_again(struct v4l2_subdev *sd, int value)
{
	struct ov5648_info *info = to_state(sd);
	unsigned int again;
	int ret = 0;

	if(value < info->again->minimum || value > info->again->maximum) {
		/* use default value. */
		again = again_to_regval(info->again->default_value);
	} else {
		again = again_to_regval(value);
	}

	ret = ov5648_write(sd, 0x350a, (again >> 8) & 0x3);
	ret = ov5648_write(sd, 0x350b, again & 0xff);
	if(ret < 0){
		printk("%s : ov5648_write error %d~~~~\n",__func__,__LINE__);
		return ret;
	}

	return 0;
}

/*
 * Tweak autogain.
 */
static int ov5648_s_autogain(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int ov5648_s_exp(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	value *= 16;
	ret =  ov5648_write(sd, 0x3500, ((value>>16)&0x0f));
	ret =  ov5648_write(sd, 0x3501, ((value>>8)&0xff));
	ret += ov5648_write(sd, 0x3502, value&0xff);

	return ret;
}

static int ov5648_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct ov5648_info *info = to_state(sd);

	switch (ctrl->id) {
		case V4L2_CID_AUTOGAIN:
			return ov5648_g_gain(sd, &info->gain->val);
		case V4L2_CID_ANALOGUE_GAIN:
			return ov5648_g_again(sd, &info->again->val);
	}

	return -EINVAL;
}

static int ov5648_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct ov5648_info *info = to_state(sd);

	switch (ctrl->id) {
		case V4L2_CID_BRIGHTNESS:
			return ov5648_s_brightness(sd, ctrl->val);
		case V4L2_CID_CONTRAST:
			return ov5648_s_contrast(sd, ctrl->val);
		case V4L2_CID_VFLIP:
			return ov5648_s_vflip(sd, ctrl->val);
		case V4L2_CID_HFLIP:
			return ov5648_s_hflip(sd, ctrl->val);
		case V4L2_CID_AUTOGAIN:
			/* Only set manual gain if auto gain is not explicitly
			   turned on. */
			if (!ctrl->val) {
				/* ov5648_s_gain turns off auto gain */
				return ov5648_s_gain(sd, info->gain->val);
			}
			return ov5648_s_autogain(sd, ctrl->val);
		case V4L2_CID_GAIN:
			return ov5648_s_gain(sd, ctrl->val);
		case V4L2_CID_ANALOGUE_GAIN:
			return ov5648_s_again(sd, ctrl->val);
		case V4L2_CID_EXPOSURE:
			return ov5648_s_exp(sd, ctrl->val);
	}

	return -EINVAL;
}

static const struct v4l2_ctrl_ops ov5648_ctrl_ops = {
	.s_ctrl = ov5648_s_ctrl,
	.g_volatile_ctrl = ov5648_g_volatile_ctrl,
};

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int ov5648_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	unsigned char val = 0;
	int ret;

	ret = ov5648_read(sd, reg->reg & 0xffff, &val);
	reg->val = val;
	reg->size = 1;

	return ret;
}

static int ov5648_s_register(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg)
{
	ov5648_write(sd, reg->reg & 0xffff, reg->val & 0xff);

	return 0;
}
#endif

static int ov5648_power(struct v4l2_subdev *sd, int on)
{
	struct ov5648_info *info = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	info->supply.avdd  = devm_regulator_get_optional(&client->dev, "avdd");
	info->supply.dvdd  = devm_regulator_get_optional(&client->dev, "dovdd");
	info->supply.dovdd = devm_regulator_get_optional(&client->dev, "dvdd");

	if (IS_ERR(info->supply.avdd)||IS_ERR(info->supply.dvdd)
			||IS_ERR(info->supply.dovdd)) {
        printk("%d %d %d\n", IS_ERR(info->supply.avdd), IS_ERR(info->supply.dvdd), IS_ERR(info->supply.dovdd));
		if ((PTR_ERR(info->supply.avdd) == -EPROBE_DEFER)
				||(PTR_ERR(info->supply.avdd) == -EPROBE_DEFER)
				||(PTR_ERR(info->supply.avdd) == -EPROBE_DEFER))
			return -EPROBE_DEFER;
		printk("No ov5648 vdd regulator found\n");
	}

   //     printk("%d %d %d\n", IS_ERR(info->supply.avdd), IS_ERR(info->supply.dvdd), IS_ERR(info->supply.dovdd));
	if ((!IS_ERR(info->supply.avdd))&&(!IS_ERR(info->supply.avdd))
			&&(!IS_ERR(info->supply.avdd)))  {
		if(on){
			ret = regulator_enable(info->supply.dovdd);
			msleep(1);
			ret = regulator_enable(info->supply.dvdd);
			msleep(1);
			ret = regulator_enable(info->supply.avdd);
			if (ret)
				dev_err(&client->dev, "ov5648 vdd supply disable failed\n");
		}
		else{
			ret = regulator_disable(info->supply.avdd);
			ret = regulator_disable(info->supply.dvdd);
			ret = regulator_disable(info->supply.dovdd);
			if (ret)
				dev_err(&client->dev, "ov5648 vdd supply disable failed\n");
		}
	}else{
		dev_err(&client->dev, "ov5648 vdd supply IS_ERR failed\n");
	}

	return ret;
}

int ov5648_s_stream(struct v4l2_subdev *sd, int enable)
{
	int ret = 0;
	if (enable) {
		ret = ov5648_write_array(sd, ov5648_stream_on_mipi);
		printk("ov5648 stream on\n");

	}
	else {
		ret = ov5648_write_array(sd, ov5648_stream_off_mipi);
		printk("ov5648 stream off\n");
	}

	return ret;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops ov5648_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = ov5648_g_register,
	.s_register = ov5648_s_register,
#endif

};

static const struct v4l2_subdev_video_ops ov5648_video_ops = {
	.s_stream = ov5648_s_stream,
};

static const struct v4l2_subdev_pad_ops ov5648_pad_ops = {
	.set_fmt = ov5648_set_fmt,
	.get_fmt = ov5648_get_fmt,
};

static const struct v4l2_subdev_ops ov5648_ops = {
	.core = &ov5648_core_ops,
	.video = &ov5648_video_ops,
	.pad = &ov5648_pad_ops,
};

/* ----------------------------------------------------------------------- */

static int ov5648_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct ov5648_info *info;
	unsigned int ident = 0;
	int gpio = -EINVAL;
	unsigned int flags;
	int mclk_index;
	unsigned long rate;
	char id_div[9];
	char id_mux[9];
	int ret;
	info = devm_kzalloc(&client->dev, sizeof(*info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;

	sd = &info->sd;

	gpio = of_get_named_gpio_flags(client->dev.of_node, "ingenic,rst-gpio", 0, &flags);
	if(gpio_is_valid(gpio)) {
		info->reset.pin = gpio;
		info->reset.active_level = (flags == OF_GPIO_ACTIVE_LOW) ? 0 : 1;
	}
	gpio = of_get_named_gpio_flags(client->dev.of_node, "ingenic,pwdn-gpio", 0, &flags);
	if(gpio_is_valid(gpio)) {
		info->pwdn.pin = gpio;
		info->pwdn.active_level = (flags == OF_GPIO_ACTIVE_LOW) ? 0 : 1;
	}

	v4l2_i2c_subdev_init(sd, client, &ov5648_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	ov5648_power(sd,1);
	ov5648_pwdn(sd, 1);
	ov5648_reset(sd, 1);
	msleep(1);

	of_property_read_u32(client->dev.of_node, "ingenic,mclk", &mclk_index);
	if(mclk_index == 0) {
		memcpy(id_div, "div_cim0", sizeof(id_div));
		memcpy(id_mux, "mux_cim0", sizeof(id_mux));
	} else if(mclk_index == 1) {
		memcpy(id_div, "div_cim1", sizeof(id_div));
		memcpy(id_mux, "mux_cim1", sizeof(id_mux));
	} else if(mclk_index == 2) {
		memcpy(id_div, "div_cim2", sizeof(id_div));
		memcpy(id_mux, "mux_cim2", sizeof(id_mux));
	} else
		printk("Unkonwn mclk index\n");

	info->clk = v4l2_clk_get(&client->dev, id_div);
	if (IS_ERR(info->clk)) {
		ret = PTR_ERR(info->clk);
		goto err_clkget;
	}
	info->sclka = devm_clk_get(&client->dev, id_mux);

	rate = v4l2_clk_get_rate(info->clk);
	if (((rate / 1000) % 24000) != 0) {
		ret = clk_set_parent(info->sclka, clk_get(NULL, "epll"));
		info->sclka = devm_clk_get(&client->dev, "epll");
		if (IS_ERR(info->sclka)) {
			pr_err("get sclka failed\n");
		} else {
			rate = clk_get_rate(info->sclka);
			if (((rate / 1000) % 24000) != 0) {
				clk_set_rate(info->sclka, 120000000);
			}
		}
	}

	ret = v4l2_clk_set_rate(info->clk, 24000000);
	if(ret)
		dev_err(sd->dev, "clk_set_rate err!\n");

	ret = v4l2_clk_enable(info->clk);
	if(ret)
		dev_err(sd->dev, "clk_enable err!\n");

#if 1
	/* Make sure it's an ov5648 */
	ret = ov5648_detect(sd, &ident);
	if (ret) {
		v4l_err(client,
				"chip found @ 0x%x (%s) is not an ov5648 chip.\n",
				client->addr << 1, client->adapter->name);
		return ret;
	}
#endif

	/*IRCUT ctl 0:off 1:on*/
	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);

	v4l2_ctrl_handler_init(&info->hdl, 8);
	v4l2_ctrl_new_std(&info->hdl, &ov5648_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 255, 1, 128);
	v4l2_ctrl_new_std(&info->hdl, &ov5648_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 127, 1, 64);
	v4l2_ctrl_new_std(&info->hdl, &ov5648_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&info->hdl, &ov5648_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
	info->gain = v4l2_ctrl_new_std(&info->hdl, &ov5648_ctrl_ops,
			V4L2_CID_GAIN, 0, 255, 1, 128);
	info->again = v4l2_ctrl_new_std(&info->hdl, &ov5648_ctrl_ops,
			V4L2_CID_ANALOGUE_GAIN, 0, 196608, 1, 1000);
	/*unit exposure lines: */
	info->exposure = v4l2_ctrl_new_std(&info->hdl, &ov5648_ctrl_ops,
			V4L2_CID_EXPOSURE, 4, 0x7c0-4, 1, 100);

	sd->ctrl_handler = &info->hdl;
	if (info->hdl.error) {
		int err = info->hdl.error;
		v4l2_ctrl_handler_free(&info->hdl);
		return err;
	}
	v4l2_ctrl_handler_setup(&info->hdl);

	info->win = &ov5648_win_sizes[0];

	ov5648_init(sd, 1);

	info->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&info->sd.entity, 1, &info->pad);
	if(ret < 0) {
		goto err_entity_init;
	}
	info->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	ret = v4l2_async_register_subdev(&info->sd);
	if (ret < 0)
		goto err_videoprobe;

	dev_info(&client->dev, "ov5648 Probed\n");

	return 0;

err_videoprobe:
err_entity_init:
	v4l2_clk_put(info->clk);
err_clkget:
	return ret;
}

static int ov5648_suspend(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov5648_info *info = to_state(sd);
	int ret;

	if (!IS_ERR(info->supply.avdd)) {
		ret = regulator_disable(info->supply.avdd);
		if (ret) {
			dev_err(dev, "ov5648 avdd supply disable failed\n");
			return ret;
		}
	}
	if (!IS_ERR(info->supply.dvdd)) {
		ret = regulator_disable(info->supply.dvdd);
		if (ret) {
			dev_err(dev, "ov5648 dvdd supply disable failed\n");
			return ret;
		}
	}
	if (!IS_ERR(info->supply.dovdd)) {
		ret = regulator_disable(info->supply.dovdd);
		if (ret) {
			dev_err(dev, "ov5648 dovdd supply disable failed\n");
			return ret;
		}
	}

	v4l2_clk_disable(info->clk);
	return 0;
}

static int ov5648_resume(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov5648_info *info = to_state(sd);
	int ret;

	if (!IS_ERR(info->supply.avdd)) {
		ret = regulator_enable(info->supply.avdd);
		if (ret) {
			dev_err(dev, "ov5648 avdd supply enable failed\n");
		}
	}
	if (!IS_ERR(info->supply.dvdd)) {
		ret = regulator_enable(info->supply.dvdd);
		if (ret) {
			dev_err(dev, "ov5648 dvdd supply enable failed\n");
		}
	}
	if (!IS_ERR(info->supply.dovdd)) {
		ret = regulator_enable(info->supply.dovdd);
		if (ret) {
			dev_err(dev, "ov5648 dovdd supply enable failed\n");
		}
	}

	v4l2_clk_enable(info->clk);
	ov5648_reset(sd, 1);
	ov5648_init(sd, 1);
	return 0;
}

static int ov5648_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov5648_info *info = to_state(sd);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&info->hdl);
	v4l2_clk_put(info->clk);

	return 0;
}

static const struct i2c_device_id ov5648_id[] = {
	{ "ov5648", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, ov5648_id);

static const struct of_device_id ov5648_of_match[] = {
	{.compatible = "ovti,ov5648", },
	{},
};
MODULE_DEVICE_TABLE(of, ov5648_of_match);

const struct dev_pm_ops ov5648_pm =
{
	.suspend = ov5648_suspend,
	.resume = ov5648_resume,
};

static struct i2c_driver ov5648_driver = {
	.driver = {
		.name	= "ov5648",
		.of_match_table = of_match_ptr(ov5648_of_match),
		.pm = &ov5648_pm,
	},
	.probe		= ov5648_probe,
	.remove		= ov5648_remove,
	.id_table	= ov5648_id,
};

module_i2c_driver(ov5648_driver);
MODULE_DESCRIPTION("A low-level driver for ov5648 sensors");
MODULE_LICENSE("GPL");
