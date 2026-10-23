/*
 * A V4L2 driver for OmniVision gc5603 cameras.
 *
 * Copyright 2006 One Laptop Per Child Association, Inc.  Written
 * by Jonathan Corbet with substantial inspiration from Mark
 * McClelland's ovcamchip code.
 *
 * Copyright 2006-7 Jonathan Corbet <corbet@lwn.net>
 *
 * This file may be distributed under the terms of the GNU General
 * Public License, version 2.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-clk.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-image-sizes.h>
#include <linux/of_gpio.h>
#include <linux/clk.h>
#include <isp-sensor.h>

#define GC5603_REG_CHIP_ID_HIGH         0x03f0
#define GC5603_REG_CHIP_ID_LOW          0x03f1

#define GC5603_CHIP_ID_H	(0x56)
#define GC5603_CHIP_ID_L	(0x03)
#define GC5603_REG_END		0xffff
#define GC5603_REG_DELAY	0x0000

static bool debug;
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

struct gc5603_win_size {
	unsigned int width;
	unsigned int height;
	unsigned int mbus_code;
	struct sensor_info sensor_info;
	enum v4l2_colorspace colorspace;
	void *regs;
};

struct gc5603_gpio {
	int pin;
	int active_level;
};

struct gc5603_info {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *gain;
	struct v4l2_ctrl *again;

	struct v4l2_clk *clk;
	struct v4l2_clk *mclk;

	struct v4l2_ctrl *exposure;

	struct media_pad pad;

	struct v4l2_subdev_format *format;	/*current fmt.*/
	struct gc5603_win_size *win;

	struct gc5603_gpio reset;
	struct gc5603_gpio pwdn;
	struct gc5603_gpio led;
};

/*
 * the part of driver maybe modify about different sensor and different board.
 */
struct again_lut {
	int index;
	unsigned int pga0;
	unsigned int pga1;
	unsigned int pga2;
	unsigned int reg3;
	unsigned int reg4;
	unsigned int col1;
	unsigned int col0;
	unsigned int gain;
};

static struct again_lut gc5603_again_lut[] = {
   //0614, 0615, 0225, 1467  1468, 00b8, 00b9
	{0, 0x00, 0x00, 0x04, 0x15, 0x15, 0x01, 0x00, 0},  //1.0
	{1, 0x90, 0x02, 0x04, 0x15, 0x15, 0x01, 0x0A, 13727},  //1.15625
	{2, 0x00, 0x00, 0x00, 0x15, 0x15, 0x01, 0x12, 23433},  //1.28125
	{3, 0x90, 0x02, 0x00, 0x15, 0x15, 0x01, 0x20, 38336},  //1.5
	{4, 0x01, 0x00, 0x00, 0x15, 0x15, 0x01, 0x30, 52911},  //1.75
	{5, 0x91, 0x02, 0x00, 0x15, 0x15, 0x02, 0x05, 69159},  //2.078125
	{6, 0x02, 0x00, 0x00, 0x15, 0x15, 0x02, 0x19, 82404},  //2.390625
	{7, 0x92, 0x02, 0x00, 0x16, 0x16, 0x02, 0x3F, 103378},  //2.984375
	{8, 0x03, 0x00, 0x00, 0x16, 0x16, 0x03, 0x20, 118447},  //3.5
	{9, 0x93, 0x02, 0x00, 0x17, 0x17, 0x04, 0x0A, 134695},  //4.15625
	{10,0x00, 0x00, 0x01, 0x18, 0x18, 0x05, 0x02, 152759},  //5.03125
	{11,0x90, 0x02, 0x01, 0x19, 0x19, 0x05, 0x39, 167669},  //5.890625
	{12, 0x01, 0x00, 0x01, 0x19, 0x19, 0x06, 0x3C, 183135}, //6.9375
	{13, 0x91, 0x02, 0x01, 0x19, 0x19, 0x08, 0x0D, 198979}, //8.203125
	{14,0x02, 0x00, 0x01, 0x1a, 0x1a, 0x09, 0x21, 213012},  //9.515625
	{15, 0x92, 0x02, 0x01, 0x1a, 0x1a, 0x0B, 0x0F, 227386}, //11.078125
	{16, 0x03, 0x00, 0x01, 0x1c, 0x1c, 0x0D, 0x17, 245090}, //13.359375
	{17, 0x93, 0x02, 0x01, 0x1c, 0x1c, 0x0F, 0x33, 260936}, //15.796875
	{18, 0x04, 0x00, 0x01, 0x1d, 0x1d, 0x12, 0x30, 277140}, //18.75
	{19, 0x94, 0x02, 0x01, 0x1d, 0x1d, 0x16, 0x10, 293322}, //22.25
	{20, 0x05, 0x00, 0x01, 0x1e, 0x1e, 0x1A, 0x19, 309458}, //26.390625
	{21, 0x95, 0x02, 0x01, 0x1e, 0x1e, 0x1F, 0x13, 325579}, //31.296875
	{22, 0x06, 0x00, 0x01, 0x20, 0x20, 0x25, 0x08, 341726}, //37.125
	{23, 0x96, 0x02, 0x01, 0x20, 0x20, 0x2C, 0x03, 357890}, //44.046875
	{24, 0xb6, 0x04, 0x01, 0x20, 0x20, 0x34, 0x0F, 374009}, //52.234375
	{25, 0x86, 0x06, 0x01, 0x20, 0x20, 0x3D, 0x3D, 388967}, //61.1875
	};
 unsigned int gainLevelTable[27] = {
			64,
			74,
			82,
			96,
			112,
			133,
			153,
			191,
			224,
			266,
			322,
			377,
			444,
			525,
			609,
			719,
			855,
			1011,
			1200,
			1424,
			1689,
			2003,
			2376,
			2819,
			3343,
			3965,
			0xffffffff,
  };

static inline struct gc5603_info *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct gc5603_info, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct gc5603_info, hdl)->sd;
}
struct regval_list {
	uint16_t reg_num;
	unsigned char value;
};
static struct regval_list gc5603_init_regs_2048_1666_30fps_mipi_ori[] = {
	{0x03fe,0xf0},
	{0x03fe,0x00},
	{0x03fe,0x10},
	{0x03fe,0x00},
	{0x0a38,0x02},
	{0x0a38,0x03},
	{0x0a20,0x07},
	{0x061b,0x03},
	{0x061c,0x50},
	{0x061d,0x05},
	{0x061e,0x7e},
	{0x061f,0x03},
	{0x0a21,0x08},
	{0x0a34,0x40},
	{0x0a35,0x11},
	{0x0a36,0x6a},
	{0x0a37,0x03},
	{0x0314,0x50},
	{0x0315,0x32},
	{0x031c,0xce},
	{0x0219,0x47},
	{0x0342,0x04},
	{0x0343,0xb0},
	{0x0340,0x06},
	{0x0341,0xd6},
	{0x0345,0x02},
	{0x0347,0x02},
	{0x0348,0x0b},
	{0x0349,0x98},
	{0x034a,0x06},
	{0x034b,0x8a},
	{0x0094,0x08},
	{0x0095,0x00},
	{0x0096,0x06},
	{0x0097,0x82},
	{0x0099,0x04},
	{0x009a,0x01},
	{0x009b,0xcc},
	{0x060c,0x01},
	{0x060e,0xd2},
	{0x060f,0x05},
	{0x070c,0x01},
	{0x070e,0xd2},
	{0x070f,0x05},
	{0x0709,0x40},
	{0x0719,0x40},
	{0x0909,0x07},
	{0x0902,0x04},
	{0x0904,0x0b},
	{0x0907,0x54},
	{0x0908,0x06},
	{0x0903,0x9d},
	{0x072a,0x1c},
	{0x072b,0x1c},
	{0x0724,0x2b},
	{0x0727,0x2b},
	{0x1466,0x18},
	{0x1467,0x15},
	{0x1468,0x15},
	{0x1469,0x70},
	{0x146a,0xe8},
	{0x0707,0x07},
	{0x0737,0x0f},
	{0x0704,0x01},
	{0x0706,0x02},
	{0x0716,0x02},
	{0x0708,0xc8},
	{0x0718,0xc8},
	{0x061a,0x02},
	{0x1430,0x80},
	{0x1407,0x10},
	{0x1408,0x16},
	{0x1409,0x03},
	{0x1438,0x01},
	{0x02ce,0x03},
	{0x0245,0xc9},
	{0x023a,0x08},
	{0x02cd,0x88},
	{0x0612,0x02},
	{0x0613,0xc7},
	{0x0243,0x03},
	{0x0089,0x03},
	{0x0002,0xab},
	{0x0040,0xa3},
	{0x0075,0x64},
	{0x0004,0x0f},
	{0x0053,0x0a},
	{0x0205,0x0c},
	{0x0a67,0x80},
	{0x0a54,0x0e},
	{0x0a65,0x10},
	{0x0a98,0x04},
	{0x05be,0x00},
	{0x05a9,0x01},
	{0x0023,0x00},
	{0x0022,0x00},
	{0x0025,0x00},
	{0x0024,0x00},
	{0x0028,0x0b},
	{0x0029,0x98},
	{0x002a,0x06},
	{0x002b,0x86},
	{0x0a83,0xe0},
	{0x0a72,0x02},
	{0x0a73,0x60},
	{0x0a75,0x41},
	{0x0a70,0x03},
	{0x0a5a,0x80},
	{0x0181,0x30},
	{0x0182,0x05},
	{0x0185,0x01},
	{0x0180,0x46},
	{0x0100,0x08},
	{0x010d,0x00},
	{0x010e,0x0A},
	{0x0113,0x02},
	{0x0114,0x01},
	{0x0115,0x10},

	{0x0202,0x05},
	{0x0203,0x27},

	{0x05be,0x01},
	{0x0a70,0x00},
	{0x0080,0x02},
	{0x0a67,0x00},
	{0x0052,0x02},
	{0x0076,0x01},
	{0x021a,0x10},
	{0x0049,0x0f},
	{0x004a,0x3c},
	{0x004b,0x00},
	{0x0430,0x25},
	{0x0431,0x25},
	{0x0432,0x25},
	{0x0433,0x25},
	{0x0434,0x59},
	{0x0435,0x59},
	{0x0436,0x59},
	{0x0437,0x59},
	{0x0100,0x09},
	{GC5603_REG_END, 0x00},	/* END MARKER */

};
static struct regval_list gc5603_init_regs_2160_1666_30fps_mipi_ori[] = {
	{0x03fe,0xf0},
	{0x03fe,0x00},
	{0x03fe,0x10},
	{0x03fe,0x00},
	{0x0a38,0x02},
	{0x0a38,0x03},
	{0x0a20,0x07},
	{0x061b,0x03},
	{0x061c,0x50},
	{0x061d,0x05},
	{0x061e,0x7e},
	{0x061f,0x03},
	{0x0a21,0x08},
	{0x0a34,0x40},
	{0x0a35,0x11},
	{0x0a36,0x6a},
	{0x0a37,0x03},
	{0x0314,0x50},
	{0x0315,0x32},
	{0x031c,0xce},
	{0x0219,0x47},
	{0x0342,0x04},
	{0x0343,0xb0},
	{0x0340,0x06},
	{0x0341,0xd6},
	{0x0345,0x02},
	{0x0347,0x02},
	{0x0348,0x0b},
	{0x0349,0x98},
	{0x034a,0x06},
	{0x034b,0x8a},
	{0x0094,0x08},
	{0x0095,0x70},
	{0x0096,0x06},
	{0x0097,0x82},
	{0x0099,0x04},
	{0x009a,0x01},
	{0x009b,0x94},
	{0x060c,0x01},
	{0x060e,0xd2},
	{0x060f,0x05},
	{0x070c,0x01},
	{0x070e,0xd2},
	{0x070f,0x05},
	{0x0709,0x40},
	{0x0719,0x40},
	{0x0909,0x07},
	{0x0902,0x04},
	{0x0904,0x0b},
	{0x0907,0x54},
	{0x0908,0x06},
	{0x0903,0x9d},
	{0x072a,0x1c},
	{0x072b,0x1c},
	{0x0724,0x2b},
	{0x0727,0x2b},
	{0x1466,0x18},
	{0x1467,0x15},
	{0x1468,0x15},
	{0x1469,0x70},
	{0x146a,0xe8},
	{0x0707,0x07},
	{0x0737,0x0f},
	{0x0704,0x01},
	{0x0706,0x02},
	{0x0716,0x02},
	{0x0708,0xc8},
	{0x0718,0xc8},
	{0x061a,0x02},
	{0x1430,0x80},
	{0x1407,0x10},
	{0x1408,0x16},
	{0x1409,0x03},
	{0x1438,0x01},
	{0x02ce,0x03},
	{0x0245,0xc9},
	{0x023a,0x08},
	{0x02cd,0x88},
	{0x0612,0x02},
	{0x0613,0xc7},
	{0x0243,0x03},
	{0x0089,0x03},
	{0x0002,0xab},
	{0x0040,0xa3},
	{0x0075,0x64},
	{0x0004,0x0f},
	{0x0053,0x0a},
	{0x0205,0x0c},
	{0x0a67,0x80},
	{0x0a54,0x0e},
	{0x0a65,0x10},
	{0x0a98,0x04},
	{0x05be,0x00},
	{0x05a9,0x01},
	{0x0023,0x00},
	{0x0022,0x00},
	{0x0025,0x00},
	{0x0024,0x00},
	{0x0028,0x0b},
	{0x0029,0x98},
	{0x002a,0x06},
	{0x002b,0x86},
	{0x0a83,0xe0},
	{0x0a72,0x02},
	{0x0a73,0x60},
	{0x0a75,0x41},
	{0x0a70,0x03},
	{0x0a5a,0x80},
	{0x0181,0x30},
	{0x0182,0x05},
	{0x0185,0x01},
	{0x0180,0x46},
	{0x0100,0x08},
	{0x010d,0x8c},
	{0x010e,0x0A},
	{0x0113,0x02},
	{0x0114,0x01},
	{0x0115,0x10},

	{0x0202,0x05},
	{0x0203,0x27},

	{0x05be,0x01},
	{0x0a70,0x00},
	{0x0080,0x02},
	{0x0a67,0x00},
	{0x0052,0x02},
	{0x0076,0x01},
	{0x021a,0x10},
	{0x0049,0x0f},
	{0x004a,0x3c},
	{0x004b,0x00},
	{0x0430,0x25},
	{0x0431,0x25},
	{0x0432,0x25},
	{0x0433,0x25},
	{0x0434,0x59},
	{0x0435,0x59},
	{0x0436,0x59},
	{0x0437,0x59},
	{0x0100,0x09},
       {GC5603_REG_END, 0x00},	/* END MARKER */

};
static struct regval_list gc5603_init_regs_2960_1666_30fps_mipi[] = {

	{0x03fe,0xf0},
	{0x03fe,0x00},
	{0x03fe,0x10},
	{0x03fe,0x00},
	{0x0a38,0x02},
	{0x0a38,0x03},
	{0x0a20,0x07},
	{0x061b,0x03},
	{0x061c,0x50},
	{0x061d,0x05},
	{0x061e,0x7e},
	{0x061f,0x03},
	{0x0a21,0x08},
	{0x0a34,0x40},
	{0x0a35,0x11},
	{0x0a36,0x6a},
	{0x0a37,0x03},
	{0x0314,0x50},
	{0x0315,0x32},
	{0x031c,0xce},
	{0x0219,0x47},
	{0x0342,0x04},
	{0x0343,0xb0},
	{0x0340,0x06},
	{0x0341,0xd6},
	{0x0345,0x02},
	{0x0347,0x02},
	{0x0348,0x0b},
	{0x0349,0x98},
	{0x034a,0x06},
	{0x034b,0x8a},
	{0x0094,0x0b},
	{0x0095,0x90},
	{0x0096,0x06},
	{0x0097,0x82},
	{0x0099,0x04},
	{0x009b,0x04},
	{0x060c,0x01},
	{0x060e,0xd2},
	{0x060f,0x05},
	{0x070c,0x01},
	{0x070e,0xd2},
	{0x070f,0x05},
	{0x0709,0x40},
	{0x0719,0x40},
	{0x0909,0x07},
	{0x0902,0x04},
	{0x0904,0x0b},
	{0x0907,0x54},
	{0x0908,0x06},
	{0x0903,0x9d},
	{0x072a,0x1c},
	{0x072b,0x1c},
	{0x0724,0x2b},
	{0x0727,0x2b},
	{0x1466,0x18},
	{0x1467,0x15},
	{0x1468,0x15},
	{0x1469,0x70},
	{0x146a,0xe8},
	{0x0707,0x07},
	{0x0737,0x0f},
	{0x0704,0x01},
	{0x0706,0x02},
	{0x0716,0x02},
	{0x0708,0xc8},
	{0x0718,0xc8},
	{0x061a,0x02},
	{0x1430,0x80},
	{0x1407,0x10},
	{0x1408,0x16},
	{0x1409,0x03},
	{0x1438,0x01},
	{0x02ce,0x03},
	{0x0245,0xc9},
	{0x023a,0x08},
	{0x02cd,0x88},
	{0x0612,0x02},
	{0x0613,0xc7},
	{0x0243,0x03},
	{0x0089,0x03},
	{0x0002,0xab},
	{0x0040,0xa3},
	{0x0075,0x64},
	{0x0004,0x0f},
	{0x0053,0x0a},
	{0x0205,0x0c},
	{0x0a67,0x80},
	{0x0a54,0x0e},
	{0x0a65,0x10},
	{0x0a98,0x04},
	{0x05be,0x00},
	{0x05a9,0x01},
	{0x0023,0x00},
	{0x0022,0x00},
	{0x0025,0x00},
	{0x0024,0x00},
	{0x0028,0x0b},
	{0x0029,0x98},
	{0x002a,0x06},
	{0x002b,0x86},
	{0x0a83,0xe0},
	{0x0a72,0x02},
	{0x0a73,0x60},
	{0x0a75,0x41},
	{0x0a70,0x03},
	{0x0a5a,0x80},
	{0x0181,0x30},
	{0x0182,0x05},
	{0x0185,0x01},
	{0x0180,0x46},
	{0x0100,0x08},
	{0x010d,0x74},
	{0x010e,0x0e},
	{0x0113,0x02},
	{0x0114,0x01},
	{0x0115,0x10},
	{0x05be,0x01},
	{0x0a70,0x00},
	{0x0080,0x02},
	{0x0a67,0x00},
	{0x0052,0x02},
	{0x0076,0x01},
	{0x021a,0x10},
	{0x0049,0x0f}, //darkrow select
	{0x004a,0x3c},
	{0x004b,0x00},
	{0x0430,0x25},
	{0x0431,0x25},
	{0x0432,0x25},
	{0x0433,0x25},
	{0x0434,0x59},
	{0x0435,0x59},
	{0x0436,0x59},
	{0x0437,0x59},
	{0x0100,0x09},

	{GC5603_REG_END, 0x00},	/* END MARKER */

#if 0
       /****system****/
       {0x03fe, 0xf0},
       {0x03fe, 0xf0},
       {0x03fe, 0xf0},
       {0x03fe, 0x00},
       {0x03f2, 0x00},
       {0x03f3, 0x00},
       {0x03f4, 0x36},
       {0x03f5, 0xc0},
       {0x03f6, 0x0B},
       {0x03f7, 0x01},
      // {0x03f8, 0x58}, //24Mhz
       {0x03f8, 0x58},  //27Mhz
       {0x03f9, 0x40},
       {0x03fc, 0x8e},
       /****CISCTL & NALOG****/
       {0x0087, 0x18},
       {0x00ee, 0x30},
       {0x00d0, 0xbf},
       {0x01a0, 0x00},
       {0x01a4, 0x40},
       {0x01a5, 0x40},
       {0x01a6, 0x40},
       {0x01af, 0x09},
       {0x0001, 0x00},
       {0x0002, 0x02},
       {0x0003, 0x04},
       {0x0004, 0x02},
       {0x0005, 0x02},
       {0x0006, 0x94},
       {0x0007, 0x00},
       {0x0008, 0x11},
       {0x0009, 0x00},
       {0x000a, 0x02},
       {0x000b, 0x00},
       {0x000c, 0x04},
       {0x000d, 0x04},
       {0x000e, 0x40},
       {0x000f, 0x07},
       {0x0010, 0x8c},
       {0x0013, 0x15},
       {0x0019, 0x0c},
       {0x0041, 0x04},
	{0x0042, 0xE2},   //frame length 0x04e2=1250

      // {0x0041, 0x05},    //or dorp
      // {0x0042, 0xE6},    //frame length 0x05E6=1350

       {0x0053, 0x60},
       {0x008d, 0x92},
       {0x0090, 0x00},
       {0x00c7, 0xe1},
       {0x001b, 0x73},
       {0x0028, 0x0d},
       {0x0029, 0x24},
       {0x002b, 0x04},
       {0x002e, 0x23},
       {0x0037, 0x03},
       {0x0043, 0x04},
       {0x0044, 0x28},
       {0x004a, 0x01},
       {0x004b, 0x20},
       {0x0055, 0x28},
       {0x0066, 0x3f},
       {0x0068, 0x3f},
       {0x006b, 0x44},
       {0x0077, 0x00},
       {0x0078, 0x20},
       {0x007c, 0xa1},
       {0x00ce, 0x7c},
       {0x00d3, 0xd4},
       {0x00e6, 0x50},
       /****gain****/
       {0x00b6, 0xc0},
       {0x00b0, 0x68},    //0x60

       {0x00b3,0x00},
       {0x00b8,0x01},
       {0x00b9,0x00},
       {0x00b1,0x01},
       {0x00b2,0x00},

       /*****isp****/
       //{0x0101, 0x0c},
       {0x0102, 0x89},
       {0x0104, 0x01},
       {0x010f, 0x00},
       {0x0158, 0x00},
       {/*dark sun*/},
       {0x0123, 0x08},
       {0x0123, 0x00},
       {0x0120, 0x01},
       {0x0121, 0x04},
       {0x0122, 0xd8},
       {0x0124, 0x03},
       {0x0125, 0xff},
       {0x001a, 0x8c},
       {0x00c6, 0xe0},
       /****blk****/
       {0x0026, 0x30},
       {0x0142, 0x00},
       {0x0149, 0x1e},
       {0x014a, 0x0f},
       {0x014b, 0x00},
       {0x0155, 0x07},
       {0x0414, 0x78},
       {0x0415, 0x78},
       {0x0416, 0x78},
       {0x0417, 0x78},
       {0x0454, 0x78},
       {0x0455, 0x78},
       {0x0456, 0x78},
       {0x0457, 0x78},
       {0x04e0, 0x18},
       /****window****/
       {0x0192, 0x02},
       {0x0194, 0x03},
       {0x0195, 0x04},
       {0x0196, 0x38},
       {0x0197, 0x07},
       {0x0198, 0x80},
       /****DVP & MIP*****/
       {0x019a, 0x06},
       {0x007b, 0x2a},
       {0x0023, 0x2d},
       {0x0201, 0x27},
       {0x0202, 0x56},
       //{0x0203, 0xb6},     //try 0xce or 0x8e  or drop
       {0x0203, 0x8e},     //try 0xce or 0x8e  or drop
       {0x0212, 0x80},
       {0x0213, 0x07},
       {0x0215, 0x10},
       {0x003e, 0x91},
       {GC5603_REG_END, 0x00},	/* END MARKER */

#endif
};

/*
 * the part of driver was fixed.
 */

static struct regval_list gc5603_stream_on_mipi[] = {
	{GC5603_REG_END, 0x00},	/* END MARKER */
};

static struct regval_list gc5603_stream_off_mipi[] = {
	{GC5603_REG_END, 0x00},	/* END MARKER */
};
int gc5603_read(struct v4l2_subdev *sd, unsigned short reg,
		unsigned char *value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	uint8_t buf[2]={(reg>>8)&0xff,reg&0xff};
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
	int ret;
	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret > 0)
		ret = 0;

	return ret;
}

static int gc5603_write(struct v4l2_subdev *sd, unsigned short reg,
			unsigned char value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	uint8_t buf[3]={(reg>>8)&0xff,reg&0xff,value};
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

static int gc5603_read_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	int ret;
	unsigned char val;
	while (vals->reg_num != GC5603_REG_END) {
		if (vals->reg_num == GC5603_REG_DELAY) {
				msleep(vals->value);
		} else {
			ret = gc5603_read(sd, vals->reg_num, &val);
			if (ret < 0)
			{
				printk("array: 0x%02x, 0x%02x\n",vals->reg_num, val);
				return ret;
			}
		}
		vals++;
	}
	return 0;
}
static int gc5603_write_array(struct v4l2_subdev *sd, struct regval_list *vals)
{
	int ret;
	while (vals->reg_num != GC5603_REG_END) {
		if (vals->reg_num == GC5603_REG_DELAY) {
				msleep(vals->value);
		} else {
			ret = gc5603_write(sd, vals->reg_num, vals->value);
			if (ret < 0)
				return ret;
		}
		vals++;
	}
//	gc5603_read_array(sd,1);
	return 0;
}




/*
 * Stuff that knows about the sensor.
 */
static int gc5603_lightup(struct v4l2_subdev *sd, u32 val)
{
	struct gc5603_info *info = to_state(sd);
	if(val) {
		gpio_direction_output(info->led.pin, info->led.active_level);
	} else {
		gpio_direction_output(info->led.pin, !info->led.active_level);
	}
	return 0;
}

static int gc5603_reset(struct v4l2_subdev *sd, u32 val)
{
	struct gc5603_info *info = to_state(sd);

	if(val) {
		gpio_direction_output(info->reset.pin, info->reset.active_level);
		msleep(10);
		gpio_direction_output(info->reset.pin, !info->reset.active_level);
		msleep(20);
		gpio_direction_output(info->reset.pin, info->reset.active_level);
		msleep(20);
	}
	return 0;
}

static int gc5603_power(struct v4l2_subdev *sd, u32 val)
{
	struct gc5603_info *info = to_state(sd);

	if(val) {
		gpio_direction_output(info->pwdn.pin, !info->pwdn.active_level);
		msleep(10);
		gpio_direction_output(info->pwdn.pin, info->pwdn.active_level);
		msleep(10);
	}
	return 0;
}

static int gc5603_init(struct v4l2_subdev *sd, u32 val)
{
	struct gc5603_info *info = to_state(sd);
	int ret = 0;

//	struct regval_list*reg = info->win->regs;
	ret = gc5603_write_array(sd, info->win->regs);

	return ret;
}

static int gc5603_detect(struct v4l2_subdev *sd, unsigned int *ident)
{
	unsigned char v;
	int ret;


	ret = gc5603_read(sd, GC5603_REG_CHIP_ID_HIGH, &v);
	if (ret < 0)
		return ret;
	if (v != GC5603_CHIP_ID_H)
		return -ENODEV;
	*ident = v;

	ret = gc5603_read(sd, GC5603_REG_CHIP_ID_LOW, &v);
	if (ret < 0)
		return ret;
	if (v != GC5603_CHIP_ID_L)
		return -ENODEV;
	*ident = (*ident << 8) | v;

	printk(" gc5603 id %x\n", *ident);
	return 0;
}


static struct gc5603_win_size gc5603_win_sizes[] = {
	{
		.sensor_info.mipi_cfg.clk		= 800,
//		.sensor_info.mipi_cfg.twidth		= 2160,
		.sensor_info.mipi_cfg.twidth		= 2048,
		.sensor_info.mipi_cfg.theight		= 1666,
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
		.sensor_info.mipi_cfg.data_type_value	= 0,
		.sensor_info.mipi_cfg.del_start		= 0,
		.sensor_info.mipi_cfg.sensor_frame_mode	= TX_SENSOR_DEFAULT_FRAME_MODE,
		.sensor_info.mipi_cfg.sensor_fid_mode	= 0,
		.sensor_info.mipi_cfg.sensor_mode		= TX_SENSOR_DEFAULT_MODE,
		.sensor_info.mipi_cfg.sensor_csi_fmt	= TX_SENSOR_RAW10,

		.width				= 2048,
//		.width				= 2160,
		.height				= 1666,
		.sensor_info.fps				= 30 << 16 | 1,
		.mbus_code	= MEDIA_BUS_FMT_SRGGB10_1X10,
		.colorspace	= V4L2_COLORSPACE_SRGB,
		.regs 		= gc5603_init_regs_2048_1666_30fps_mipi_ori,
//		.regs 		= gc5603_init_regs_2160_1666_30fps_mipi_ori,

	},
};

#if 0
static int gc5603_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index >= N_gc5603_FMTS)
		return -EINVAL;

	code->code = gc5603_formats[code->index].mbus_code;
	return 0;
}
#endif

/*
 * Set a format.
 */
static int gc5603_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{

	if (format->pad)
		return -EINVAL;

	return 0;
}

static int gc5603_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct gc5603_info *info = to_state(sd);
	struct gc5603_win_size *wsize = info->win;
	struct v4l2_mbus_framefmt *fmt = &format->format;
	int ret = 0;

	if(!info->win) {
		dev_err(sd->dev, "sensor win_size not set!\n");
		return -EINVAL;
	}

	fmt->width = wsize->width;
	fmt->height = wsize->height;
	fmt->code = wsize->mbus_code;
	fmt->colorspace = wsize->colorspace;
	*(unsigned int *)fmt->reserved = &wsize->sensor_info;

	printk("----%s, %d, width: %d, height: %d, code: %x\n",
			__func__, __LINE__, fmt->width, fmt->height, fmt->code);

	return ret;
}

static int gc5603_s_brightness(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int gc5603_s_contrast(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int gc5603_s_hflip(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int gc5603_s_vflip(struct v4l2_subdev *sd, int value)
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
static int gc5603_g_gain(struct v4l2_subdev *sd, __s32 *value)
{
	int ret = 0;
	unsigned char gain = 0;

	*value = gain;
	return ret;
}

static int gc5603_s_gain(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static unsigned int again_to_regval(int gain)
{
	struct again_lut *lut = NULL;
	int i = 0;
//	int ret = ARRAY_SIZE(gc5603_again_lut);
	for(i = 0; i < ARRAY_SIZE(gc5603_again_lut); i++) {
		lut = &gc5603_again_lut[i];

		if(gain <= lut->gain) {
			return lut->index;
		}
	}

	/*last value.*/
	return lut->index;
}
static int regval_to_again(unsigned int pga0,unsigned int pga1,unsigned int pga2,unsigned int reg3,unsigned int reg4,unsigned int col1,unsigned int col0)
{
	struct again_lut *lut = NULL;
	int i = 0;
	for(i = 0; i < ARRAY_SIZE(gc5603_again_lut); i++) {
		lut = &gc5603_again_lut[i];

		if(pga0 == lut->pga0 && pga1 == lut->pga1 && pga2 == lut->pga2 && reg3 == lut->reg3 && reg4 == lut->reg4 && col1 == lut->col1 && col0 == lut->col0) {
			return lut->gain;
		}
	}
	printk("%s, %d regval  not mapped to isp gain\n", __func__, __LINE__);
	return -EINVAL;
}
static int gc5603_g_again(struct v4l2_subdev *sd, __s32 *value)
{
	char v = 0;
	unsigned int pga0,pga1,pga2 = 0;
	unsigned int reg3,reg4 = 0;
	unsigned int col0,col1 = 0;
	unsigned int preh,prel = 0;
	int ret = 0;


	ret = gc5603_read(sd, 0x0614, &v);
	pga0 = v ;
	ret = gc5603_read(sd, 0x0615, &v);
	pga1 = v ;
	ret = gc5603_read(sd, 0x0225, &v);
	pga2 = v ;
	ret = gc5603_read(sd, 0x1467, &v);
	reg3 = v ;
	ret = gc5603_read(sd, 0x1468, &v);
	reg4 = v ;
	ret = gc5603_read(sd, 0x00b8, &v);
	col1 = v ;
	ret = gc5603_read(sd, 0x00b9, &v);
	col0 = v ;
	ret = gc5603_read(sd, 0x0064, &v);
	preh = v>>6 ;
	ret = gc5603_read(sd, 0x0065, &v);
	prel = (v&0x3f)<<2 ;
	printk("--------->pga0 = 0x%02x,pga1=0x%02x,pga2=0x%02x,reg3=0x%02x,reg4=0x%02x,col1=0x%02x,col0=0x%02x,preh=0x%02x,prel=0x%02x\n",pga0,pga1,pga2,reg3,reg4,col1,col0,preh,prel);
	*value = regval_to_again(pga0,pga1,pga2,reg3,reg4,col1,col0);

	return ret;

}
/*set analog gain db value, map value to sensor register.*/
static int gc5603_s_again(struct v4l2_subdev *sd, int again)
{
#if 1
	struct gc5603_info *info = to_state(sd);
	struct again_lut *val_lut = gc5603_again_lut;
	unsigned int reg_value = 0;
	unsigned int tol_dig_gain = 0;
	int ret = 0;
	if(again < info->again->minimum || again > info->again->maximum) {
		/* use default value. */
		reg_value = again_to_regval(info->again->default_value);
	} else {
		reg_value = again_to_regval(again);
	}
	tol_dig_gain = again*64/val_lut[reg_value].gain;
	ret = gc5603_write(sd, 0x031d ,0x2d);
	ret += gc5603_write(sd, 0x0614, val_lut[reg_value].pga0);
	ret += gc5603_write(sd, 0x0615, val_lut[reg_value].pga1);
	ret += gc5603_write(sd, 0x0225, val_lut[reg_value].pga2);
	ret += gc5603_write(sd, 0x031d, 0x28);

	ret += gc5603_write(sd, 0x1467, val_lut[reg_value].reg3);
	ret += gc5603_write(sd, 0x1468, val_lut[reg_value].reg4);
	ret += gc5603_write(sd, 0x00b8, val_lut[reg_value].col1);
	ret += gc5603_write(sd, 0x00b9, val_lut[reg_value].col0);

	ret += gc5603_write(sd, 0x0064, (tol_dig_gain >>6));
	ret += gc5603_write(sd, 0x0065, (tol_dig_gain &0x3f)<<2);
	printk("---->set again:tol_dig_gain =  0x%x,reg_value=%d,\n",tol_dig_gain,reg_value);
	if(ret < 0){
		printk("%s : gc5603_write error %d~~~~\n",__func__,__LINE__);
		return ret;
	}
	msleep(0.5);
	ret = gc5603_g_again(sd, &info->again->val);
	if(ret < 0){
		printk("%s : gc5603_get again error %d~~~~\n",__func__,__LINE__);
		return ret;
	}

#endif
	return 0;

}

/*
 * Tweak autogain.
 */
static int gc5603_s_autogain(struct v4l2_subdev *sd, int value)
{
	int ret = 0;

	return ret;
}

static int gc5603_s_exp(struct v4l2_subdev *sd, int value)
{
	int ret = 0;
#if 1
	ret = gc5603_write(sd, 0x0203, value&0xff);
	ret += gc5603_write(sd, 0x0202, ((value>>8)&0x3f));
#endif
#if 0
	unsigned char v ;
	unsigned int *exp_val;
	ret = gc5603_read(sd,0x0202,&v);
	if (ret <0)
		return ret;
	printk("----->reg exposure 0x0202 is :0x%x.....0x0203 is:0x%x\n",v);
	ret = gc5603_read(sd,0x0203,&v);
	if (ret <0)
		return ret;
	printk("----->reg exposure 0x0203 is :0x%x.....0x0203 is:0x%x\n",v);
#endif
	return ret;
}

static int gc5603_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct gc5603_info *info = to_state(sd);

	switch (ctrl->id) {
		case V4L2_CID_AUTOGAIN:
			return gc5603_g_gain(sd, &info->gain->val);
		case V4L2_CID_ANALOGUE_GAIN:
			return gc5603_g_again(sd, &info->again->val);
	}
	return -EINVAL;
}

static int gc5603_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct gc5603_info *info = to_state(sd);

	switch (ctrl->id) {
		case V4L2_CID_BRIGHTNESS:
			return gc5603_s_brightness(sd, ctrl->val);
		case V4L2_CID_CONTRAST:
			return gc5603_s_contrast(sd, ctrl->val);
		case V4L2_CID_VFLIP:
			return gc5603_s_vflip(sd, ctrl->val);
		case V4L2_CID_HFLIP:
			return gc5603_s_hflip(sd, ctrl->val);
		case V4L2_CID_AUTOGAIN:
			/* Only set manual gain if auto gain is not explicitly
			   turned on. */
			if (!ctrl->val) {
				/* gc5603_s_gain turns off auto gain */
				return gc5603_s_gain(sd, info->gain->val);
			}
			return gc5603_s_autogain(sd, ctrl->val);
		case V4L2_CID_GAIN:
			return gc5603_s_gain(sd, ctrl->val);
		case V4L2_CID_ANALOGUE_GAIN:
			return gc5603_s_again(sd, ctrl->val);
		case V4L2_CID_EXPOSURE:
			return gc5603_s_exp(sd, ctrl->val);
	}
	return -EINVAL;
}

static const struct v4l2_ctrl_ops gc5603_ctrl_ops = {
	.s_ctrl = gc5603_s_ctrl,
	.g_volatile_ctrl = gc5603_g_volatile_ctrl,
};

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int gc5603_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	unsigned char val = 0;
	int ret;

	ret = gc5603_read(sd, reg->reg & 0xffff, &val);
	reg->val = val;
	reg->size = 1;
	return ret;
}

static int gc5603_s_register(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg)
{
	gc5603_write(sd, reg->reg & 0xffff, reg->val & 0xff);
	return 0;
}
#endif

int gc5603_s_stream(struct v4l2_subdev *sd, int enable)
{
	int ret = 0;
	if (enable) {
		ret = gc5603_write_array(sd, gc5603_stream_on_mipi);
		printk("gc5603 stream on\n");

	}
	else {
		ret = gc5603_write_array(sd, gc5603_stream_off_mipi);
		printk("gc5603 stream off\n");
	}
	return ret;
}
/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops gc5603_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = gc5603_g_register,
	.s_register = gc5603_s_register,
#endif
//	.g_ext_ctrls = v4l2_subdev_g_ext_ctrls,
//	.try_ext_ctrls = v4l2_subdev_try_ext_ctrls,
//	.s_ext_ctrls = v4l2_subdev_s_ext_ctrls,
//	.g_ctrl = v4l2_subdev_g_ctrl,
//	.s_ctrl = v4l2_subdev_s_ctrl,
//	.queryctrl = v4l2_subdev_queryctrl,
//	.querymenu = v4l2_subdev_querymenu,

};

static const struct v4l2_subdev_video_ops gc5603_video_ops = {
	.s_stream = gc5603_s_stream,
};

static const struct v4l2_subdev_pad_ops gc5603_pad_ops = {
	//.enum_frame_interval = gc5603_enum_frame_interval,
	//.num_frame_size = gc5603_enum_frame_size,
	//.enum_mbus_code = gc5603_enum_mbus_code,
	.set_fmt = gc5603_set_fmt,
	.get_fmt = gc5603_get_fmt,
};

static const struct v4l2_subdev_ops gc5603_ops = {
	.core = &gc5603_core_ops,
	.video = &gc5603_video_ops,
	.pad = &gc5603_pad_ops,
};

/* ----------------------------------------------------------------------- */
static int gc5603_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct gc5603_info *info;
	int ret;
	unsigned int ident = 0;
	int gpio = -EINVAL;
	unsigned int flags;
	info = devm_kzalloc(&client->dev, sizeof(*info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;
	sd = &info->sd;

	gpio = of_get_named_gpio_flags(client->dev.of_node, "ingenic,led-gpio", 0, &flags);
	if(gpio_is_valid(gpio)) {
		info->led.pin = gpio;
		info->led.active_level = (flags == OF_GPIO_ACTIVE_LOW) ? 0 : 1;
	}
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

	gc5603_power(sd, 1);
	gc5603_reset(sd, 1);

	v4l2_i2c_subdev_init(sd, client, &gc5603_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	/*clk*/
	info->mclk = v4l2_clk_get(&client->dev, "gate_cim_mclk");
	if (IS_ERR(info->mclk)) {
		ret = PTR_ERR(info->mclk);
		goto err_mclkget;
	}
	ret = v4l2_clk_enable(info->mclk);
	if(ret)
		dev_err(sd->dev, "mclk_enable err!\n");

	info->clk = v4l2_clk_get(&client->dev, "div_cim");
	if (IS_ERR(info->clk)) {
		ret = PTR_ERR(info->clk);
		goto err_clkget;
	}


	ret = v4l2_clk_set_rate(info->clk, 24000000);
	if(ret)
		dev_err(sd->dev, "clk_set_rate err!\n");

	ret = v4l2_clk_enable(info->clk);
	if(ret)
		dev_err(sd->dev, "clk_enable err!\n");

#if 1
	/* Make sure it's an gc5603 */
	ret = gc5603_detect(sd, &ident);
	if (ret) {
		v4l_err(client,
				"chip found @ 0x%x (%s) is not an gc5603 chip.\n",
				client->addr << 1, client->adapter->name);
		return ret;
	}
#endif
#if 1
	/*IRCUT ctl 0:off 1:on*/
	v4l_info(client, "chip found @ 0x%02x (%s)\n",
			client->addr << 1, client->adapter->name);
	v4l2_ctrl_handler_init(&info->hdl, 8);
	v4l2_ctrl_new_std(&info->hdl, &gc5603_ctrl_ops,
			V4L2_CID_BRIGHTNESS, 0, 255, 1, 128);
	v4l2_ctrl_new_std(&info->hdl, &gc5603_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 127, 1, 64);
	v4l2_ctrl_new_std(&info->hdl, &gc5603_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&info->hdl, &gc5603_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
	info->gain = v4l2_ctrl_new_std(&info->hdl, &gc5603_ctrl_ops,
			V4L2_CID_GAIN, 0, 255, 1, 128);
	info->again = v4l2_ctrl_new_std(&info->hdl, &gc5603_ctrl_ops,
			V4L2_CID_ANALOGUE_GAIN, 0, 388967, 1,118400);

	/*unit exposure lines: */
	info->exposure = v4l2_ctrl_new_std(&info->hdl, &gc5603_ctrl_ops,
			V4L2_CID_EXPOSURE, 0, 1742, 1, 500);//frame_lenth-8 = shutter time

	sd->ctrl_handler = &info->hdl;
	if (info->hdl.error) {
		int err = info->hdl.error;
		v4l2_ctrl_handler_free(&info->hdl);
		return err;
	}
	v4l2_ctrl_handler_setup(&info->hdl);

	info->win = &gc5603_win_sizes[0];

	ret = gc5603_init(sd, 1);
	if (ret < 0){
		goto err_init;
	}
	info->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&info->sd.entity, 1, &info->pad);
	if(ret < 0) {
		goto err_entity_init;
	}
	ret = v4l2_async_register_subdev(&info->sd);
	if (ret < 0)
		goto err_videoprobe;

	dev_info(&client->dev, "gc5603 Probed\n");
	return 0;
#endif
err_videoprobe:
err_entity_init:
	v4l2_clk_put(info->clk);
err_clkget:
	v4l2_clk_put(info->mclk);
err_init:
err_mclkget:
	return ret;
}


static int gc5603_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc5603_info *info = to_state(sd);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&info->hdl);
	v4l2_clk_put(info->clk);
	return 0;
}

static const struct i2c_device_id gc5603_id[] = {
	{ "gc5603", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, gc5603_id);

static const struct of_device_id gc5603_of_match[] = {
	{.compatible = "galaxy,gc5603", },
	{},
};
MODULE_DEVICE_TABLE(of, gc5603_of_match);


static struct i2c_driver gc5603_driver = {
	.driver = {
		.name	= "gc5603",
		.of_match_table = of_match_ptr(gc5603_of_match),
	},
	.probe		= gc5603_probe,
	.remove		= gc5603_remove,
	.id_table	= gc5603_id,
};

module_i2c_driver(gc5603_driver);
MODULE_DESCRIPTION("A low-level driver for gc5603 sensors");
MODULE_LICENSE("GPL");
