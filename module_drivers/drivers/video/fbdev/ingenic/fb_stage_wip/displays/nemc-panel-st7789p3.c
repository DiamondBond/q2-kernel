#include "../ingenicfb.h"

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/string.h>
#define DRV_NAME "ST7789P3_LCD"

#define LCD_W 240
#define LCD_H 320

#define REG_SMC0R1 0x14
#define REG_SMC0R2 0x18
#define REG_SACR1 0x34
#define REG_SACR2 0x38
#define REG_SMC1R1 0x54
#define REG_SMC1R2 0x58

#define NEMC_PADDR 0x1b000000
#define NEMC_SIZE 0x1000000
/*
 * devicetree reference:
 *    &pinctrl {
 *       nemc_data_pin:nemc-data-pin {
 *           ingenic,pinmux = <&gpb 20 27>,<&gpb 29 29>;
 *       	ingenic,pinmux-funcsel = <PINCTL_FUNCTION3>;
 *           ingenic,pincfg = <&gpb 20 27 PINCFG_PACK(PINCTL_CFG_BIAS_BUS_HOLD,
 * 0)>,<&gpb 29 29 PINCFG_PACK(PINCTL_CFG_BIAS_BUS_HOLD, 0)>;
 *       };
 *   };
 *
 *   / {
 *
 *       display-dbi {
 *       	compatible = "simple-bus";
 *       	#interrupt-cells = <1>;
 *       	#address-cells = <1>;
 *       	#size-cells = <1>;
 *       	ranges = <>;
 *       	panel_st7789p3 {
 *         		compatible = "ingenic,nemc_st7789p3";
 *         		status = "okay";
 *         		pinctrl-names = "default";
 *         		pinctrl-0 = <&nemc_data_pin>;
 *                 reg = <0x13410000 0x1000>;
 *                 rot_angle = <270>; // support 0 90 or 270;
 *                 rst-gpio = <&gpc 28 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>;
 *                 te-gpio = <&gpc 26 IRQ_TYPE_EDGE_FALLING
 * INGENIC_GPIO_NOBIAS>; rs-gpio = <&gpc 27 GPIO_ACTIVE_LOW
 * INGENIC_GPIO_NOBIAS>; rd-gpio = <&gpb 28 GPIO_ACTIVE_LOW
 * INGENIC_GPIO_NOBIAS>; cs-gpio = <&gpc 30 GPIO_ACTIVE_LOW
 * INGENIC_GPIO_NOBIAS>; led_en-gpio = <&gpa 2 GPIO_ACTIVE_LOW
 * INGENIC_GPIO_NOBIAS>; dmas = <&pdma
 * INGENIC_DMA_TYPE(INGENIC_DMA_REQ_AUTO_TX)>; dma-names = "tx"; clocks =
 * <&clock CLK_GATE_NEMC>; clock-names = "gate_nemc";
 *                 memory-region=<&reserved_memory>;
 *         		port {
 *         			panel_st7789p3_ep: endpoint {
 *         				   remote-endpoint = <&dpu_out_ep>;
 *         		   };
 *         		};
 *      	};
 *       };
 *   };
 *
 *   &dpu {
 *       status = "okay";
 *       	ingenic,disable-rdma-fb = <0>;
 *       	ingenic,rot_angle = <0>;
 *
 *       	ingenic,layer-exported = <0 1 1 0>;
 *       	ingenic,layer-frames   = <3 3 1 2>;
 *           ingenic,layer-framesize = <1728 1280>, <1728 1280>, <320 240>, <320
 * 240>; layer,color_mode        = <4 6 1 0>; layer,src-size          = <320
 * 240>, <1280 1728>, <320 240>, <320 240>; layer,target-size       = <320 240>,
 * <320 240>, <320 240>, <320 240>; layer,target-pos        = <0 0>, <0 0>, <0
 * 0>, <0 0>; layer,enable            = <0 1 1 0>; layer,zorder            = <1
 * 2 3 0>; ingenic,logo-pan-layer  = <0>;
 *
 *
 *       port {
 *       	dpu_out_ep: endpoint {
 *       	remote-endpoint = <&panel_st7789p3_ep>;
 *           };
 *       };
 *   };
 */

static const struct regmap_config nemc_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = REG_SMC1R2,
};

struct panel_plat_data {
	struct device *dev;
	struct gpio_desc *rst;
	struct gpio_desc *rs;
	struct gpio_desc *cs;
	struct gpio_desc *te;
	struct gpio_desc *rd;
	struct gpio_desc *led_en;
	struct resource *mem_res;
	struct regmap *map;
	struct clk *clk_gate;
	struct dma_chan *dma_tx;
	struct scatterlist sg_buf;
	struct dma_async_tx_descriptor *dma_desc;
	int rotate;
	void __iomem *nemc_addr;
	int irq;
	int do_flush;
	u16 *frame;
	dma_addr_t pframe;
	struct completion dma_complete;
};
struct panel_plat_data *plat_panel = NULL;
struct panel_init_data {
	u8 cmd;
	u8 *data;
	u8 data_len;
};

#define INIT_ENTRY(c, ...)                                             \
	{                                                              \
		.cmd = c, .data = (u8[]){ __VA_ARGS__ },               \
		.data_len = sizeof((u8[]){ __VA_ARGS__ }) / sizeof(u8) \
	}

static struct panel_init_data fw035_8bit_init_data[] = {
	INIT_ENTRY(0xE0,
		   0x00,
		   0x07,
		   0x10,
		   0x09,
		   0x17,
		   0x0B,
		   0x40,
		   0x8A,
		   0x4B,
		   0x0A,
		   0x0D,
		   0x0F,
		   0x15,
		   0x16,
		   0x0F),
	INIT_ENTRY(0xE1,
		   0x00,
		   0x1A,
		   0x1B,
		   0x02,
		   0x0D,
		   0x05,
		   0x30,
		   0x35,
		   0x43,
		   0x02,
		   0x0A,
		   0x09,
		   0x32,
		   0x36,
		   0x0F),
	INIT_ENTRY(0xB1, 0xA0, 0x11),
	INIT_ENTRY(0xB4, 0x02),
	INIT_ENTRY(0xC0, 0x17, 0x15),
	INIT_ENTRY(0xC1, 0x41),
	INIT_ENTRY(0xC5, 0x00, 0x0A, 0x80),
	INIT_ENTRY(0xB6, 0x02),
	INIT_ENTRY(0x36, 0x48),
	INIT_ENTRY(0x3A, 0x55),
	INIT_ENTRY(0xE9, 0x00),
	INIT_ENTRY(0xF7, 0xA9, 0x51, 0x2C, 0x82),
	//   INIT_ENTRY(0x35,0x00),//TE on
};

static struct panel_init_data RT20QV227A_init_data[] = {
	INIT_ENTRY(0xB2, 0x0C, 0x0C, 0x00, 0x33, 0x33),
	INIT_ENTRY(0x35, 0x00), // TE on
	INIT_ENTRY(0x36, 0x00),
	INIT_ENTRY(0x3A, 0x05),
	INIT_ENTRY(0xB7, 0x74),
	INIT_ENTRY(0xBB, 0x1E),

	INIT_ENTRY(0xC0, 0x2C),
	INIT_ENTRY(0xC2, 0x01),
	INIT_ENTRY(0xC3, 0x19),
	INIT_ENTRY(0xC4, 0x20),
	INIT_ENTRY(0xC6, 0x0F),

	// INIT_ENTRY(0xD0,0xA7,0xA1),
	INIT_ENTRY(0xD0, 0xA4, 0xA1),
	INIT_ENTRY(0xD6, 0xA1),
	INIT_ENTRY(0xE0,
		   0xF0,
		   0x08,
		   0x0E,
		   0x09,
		   0x08,
		   0x16,
		   0x33,
		   0x43,
		   0x4A,
		   0x38,
		   0x14,
		   0x14,
		   0x2E,
		   0x32),
	INIT_ENTRY(0xE1,
		   0xF0,
		   0x0A,
		   0x10,
		   0x0A,
		   0x09,
		   0x04,
		   0x33,
		   0x33,
		   0x49,
		   0x0A,
		   0x16,
		   0x15,
		   0x2C,
		   0x30),
	INIT_ENTRY(0x21),
};

static struct panel_init_data UT024BQV277N_init_data[] = {
    INIT_ENTRY(0xDF, 0x5A, 0x69, 0x02, 0x09),          // 命令0xDF + 4个数据
    INIT_ENTRY(0x36, 0x00),                            // 命令0x36 + 1个数据
    INIT_ENTRY(0x3A, 0x55),                            // 命令0x3A + 1个数据
    INIT_ENTRY(0xB2, 0x0C, 0x0C, 0x00, 0x33, 0x33),    // 命令0xB2 + 5个数据
    INIT_ENTRY(0xB7, 0x54),                            // 命令0xB7 + 1个数据
    INIT_ENTRY(0xBB, 0x1A),                            // 命令0xBB（VCOM 设置）+ 1个数据
    INIT_ENTRY(0xC0, 0x2C),                            // 命令0xC0 + 1个数据
    INIT_ENTRY(0xC2, 0x01),                            // 命令0xC2 + 1个数据
    INIT_ENTRY(0xC3, 0x10),                            // 命令0xC3 + 1个数据（10F）
    INIT_ENTRY(0xC6, 0x0F),                            // 命令0xC6 + 1个数据
    INIT_ENTRY(0xD0, 0xA7),                            // 命令0xD0 + 1个数据
    INIT_ENTRY(0xD0, 0xA4, 0xA1),                      // 命令0xD0 + 2个数据
    INIT_ENTRY(0xD6, 0xA1),                            // 命令0xD6 + 1个数据
    // 命令0xE0 + 14个数据（Gamma参数）
    INIT_ENTRY(0xE0, 0xF0, 0x02, 0x07, 0x05, 0x06, 0x14, 0x2F, 0x54, 0x46, 0x38, 0x13, 0x11, 0x2E, 0x35),
    // 命令0xE1 + 14个数据（Gamma参数）
    INIT_ENTRY(0xE1, 0xF0, 0x08, 0x0C, 0x0C, 0x09, 0x05, 0x2F, 0x43, 0x46, 0x36, 0x10, 0x12, 0x2C, 0x32),
    INIT_ENTRY(0x35, 0x00),                            // 命令0x35 + 1个数据
    INIT_ENTRY(0x21),                                  // 命令0x21（无后续数据）
};

#define CONFIG_PANEL_INIT_DATA UT024BQV277N_init_data

static void send_cmd(struct panel_plat_data *panel, u8 cmd)
{
	volatile u16 *addr = (u16 *)panel->nemc_addr;
	gpiod_direction_output(panel->rs, 1);
    *addr = cmd << 8;
	gpiod_direction_output(panel->rs, 0);
	udelay(200);
}

static void send_dat(struct panel_plat_data *panel, u8 *data, int size)
{
	volatile u16 *addr = (u16 *)panel->nemc_addr;
	int i;
	for (i = 0; i < size; i++) {
		*addr = data[i] << 8;
	}
}

static void fw035_panel_init(struct panel_plat_data *panel)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(fw035_8bit_init_data); i++) {
		send_cmd(panel, fw035_8bit_init_data[i].cmd);
		send_dat(panel,
			 fw035_8bit_init_data[i].data,
			 fw035_8bit_init_data[i].data_len);
	}
}

static void rt20qv227a_panel_init(struct panel_plat_data *panel)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(RT20QV227A_init_data); i++) {
		send_cmd(panel, RT20QV227A_init_data[i].cmd);
		if (RT20QV227A_init_data[i].data_len)
			send_dat(panel,
				 RT20QV227A_init_data[i].data,
				 RT20QV227A_init_data[i].data_len);
	}
}

static void panel_init_param(struct panel_plat_data *panel)
{
	int i;
	send_cmd(panel,0x11);
    msleep(120);
	for (i = 0; i < ARRAY_SIZE(CONFIG_PANEL_INIT_DATA); i++) {
		send_cmd(panel, CONFIG_PANEL_INIT_DATA[i].cmd);
		if (CONFIG_PANEL_INIT_DATA[i].data_len)
			send_dat(panel,
				 CONFIG_PANEL_INIT_DATA[i].data,
				 CONFIG_PANEL_INIT_DATA[i].data_len);
	}
    //	msleep(50);
    //  send_cmd(panel, 0x29);
}


static void enter_sleep(struct panel_plat_data *panel)
{
	send_cmd(panel, 0x28);
	msleep(10);
	send_cmd(panel, 0x10);
	msleep(120);
}

static void exit_sleep(struct panel_plat_data *panel)
{
	send_cmd(panel, 0x11);
	msleep(120);
	send_cmd(panel, 0x29);
}

static int Initial_IC(struct panel_plat_data *panel)
{
	if (panel->led_en)
		gpiod_direction_output(panel->led_en, 1);
	gpiod_direction_output(panel->rst, 0);
	msleep(10);
	gpiod_direction_output(panel->rst, 1);
	msleep(100);
	gpiod_direction_output(panel->rst, 0);
	if (panel->cs)
		gpiod_direction_output(panel->cs, 1);
	if (panel->rd)
		gpiod_direction_output(panel->rd, 0);
	//    fw035_panel_init(panel);
	//    rt20qv227a_panel_init(panel);
    	panel_init_param(panel);
    	exit_sleep(panel);
	//    set_display_rect(panel,0,0,320,480);
	send_cmd(panel, 0x2c);
	if (panel->cs) {
		gpiod_direction_output(panel->cs, 0);
		gpiod_direction_output(panel->cs, 1);
	}
	{
		int i;
		volatile u16 *addr = (u16 *)panel->nemc_addr;
		for (i = 0; i < 320 * 480 * 2; i++) {
			*addr = 0xf << 8;
		}
	}
	if (panel->cs)
		gpiod_direction_output(panel->cs, 0);
	//    msleep(100);
	return 0;
}

static int of_lcd_panel_parse(struct panel_plat_data *lcd_panel)
{
	struct device *dev = lcd_panel->dev;
	int ret = 0;
#define PANEL_GPIO_GET(gpiodesc, name)                                         \
	do {                                                                   \
			gpiodesc = devm_gpiod_get_optional(dev,                \
							   name,               \
							   GPIOD_ASIS);        \
			if (IS_ERR_OR_NULL(gpiodesc)) {                        \
				gpiodesc = NULL;                               \
				dev_err(dev, "Failed to request %s!\n", name); \
			}                                                      \
	} while (0)
	PANEL_GPIO_GET(lcd_panel->cs, "cs");
	PANEL_GPIO_GET(lcd_panel->rd, "rd");
	PANEL_GPIO_GET(lcd_panel->led_en, "led_en");
	PANEL_GPIO_GET(lcd_panel->rs, "rs");
	PANEL_GPIO_GET(lcd_panel->rst, "rst");
	lcd_panel->te = devm_gpiod_get_optional(dev, "te", GPIOD_IN);
	if (IS_ERR_OR_NULL(lcd_panel->te)) {
		lcd_panel->te = NULL;
		ret = -1;
		dev_err(dev, "No valid irq gpio");
	}

	if (of_property_read_u32(dev->of_node,
				 "rot_angle",
				 &lcd_panel->rotate)) {
		lcd_panel->rotate = 0;
	}
	dev_info(dev, "NEMC LCD Rotate: %d\n", lcd_panel->rotate);
	return ret;
}

// DMA 完成回调函数
static void dma_callback(void *param)
{
	struct panel_plat_data *panel = (struct panel_plat_data *)param;
	// 标记 DMA 传输完成
	//gpiod_direction_output(panel->cs, 0);
	complete(&panel->dma_complete);
	panel->do_flush = 0;
	if (panel->cs)
        	gpiod_direction_output(panel->cs, 0);
}

static int dma_send_data(struct panel_plat_data *panel)
{
	int ret;
	dma_cookie_t cookie;
	struct scatterlist *sg = &panel->sg_buf;

	// 准备 DMA 描述符
	panel->dma_desc = dmaengine_prep_slave_sg(panel->dma_tx,
						  sg,
						  1,
						  DMA_MEM_TO_MEM,
						  DMA_PREP_INTERRUPT);
	if (!panel->dma_desc) {
		dev_err(panel->dev, "Failed to prepare DMA descriptor\n");
		goto error;
	}

	panel->dma_desc->callback = dma_callback;
	panel->dma_desc->callback_param = panel;
	cookie = dmaengine_submit(panel->dma_desc);
	ret = dma_submit_error(cookie);
	if (ret) {
		dev_err(panel->dev, "Failed to submit DMA transfer\n");
		dmaengine_terminate_sync(panel->dma_tx);
	} else
		dma_async_issue_pending(panel->dma_tx);
error:
	return ret;
}

static int dma_tx_init(struct panel_plat_data *panel)
{
	int ret;
	struct dma_slave_caps caps;
	struct device *dev = panel->dev;
	struct scatterlist *sg = &panel->sg_buf;
	panel->dma_tx = dma_request_slave_channel(dev, "tx");
	if (!panel->dma_tx)
		return -ENODEV;

	ret = dma_get_slave_caps(panel->dma_tx, &caps);
	if (ret) {
		dev_err(dev, "Unable to get slave caps: %d\n", ret);
	}
	panel->frame = dma_alloc_noncoherent(dev,
					     LCD_W * LCD_H * 4,
					     &panel->pframe,
					     DMA_BIDIRECTIONAL,
					     GFP_KERNEL);
	if (!panel->frame) {
		dev_err(dev, "Failed to allocate DMA coherent buffer\n");
		return -ENOMEM;
	}
	// 初始化 scatterlist
	sg_init_one(sg, panel->frame, LCD_W * LCD_H * 4);
	sg->dma_address = panel->pframe;
	{
		struct dma_slave_config cfg = {
			.direction = DMA_MEM_TO_MEM,
			.src_addr = panel->pframe,
			.dst_addr = NEMC_PADDR,
			.src_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES,
			.dst_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES,
			.src_maxburst = cfg.dst_maxburst = 1,
			.slave_id = 8,
		};
		printk("panel->pframe: %x\n", panel->pframe);
		ret = dmaengine_slave_config(panel->dma_tx, &cfg);
		if (ret) {
			printk("dmaengine slave config failed\n");
			return ret;
		}
	}
	return ret;
}

static void dma_tx_deinit(struct panel_plat_data *panel)
{
	if (panel->frame)
		dma_free_noncoherent(panel->dev,
				     LCD_W * LCD_H * 4,
				     panel->frame,
				     panel->pframe,
				     DMA_BIDIRECTIONAL);

	if (panel->dma_tx)
		dma_release_channel(panel->dma_tx);
}

static void send_test_color(struct panel_plat_data *panel)
{
	u32 color[8] = {
		0xffffff, 0x000000, 0xff0000, 0x800000,
		0x00ff00, 0x008000, 0x0000ff, 0x000080,
	};
	int cw = LCD_W / 8;
	u32 *pix = (u32 *)panel->frame;
	int y, i, x;
	for (y = 0; y < LCD_H; y++) {
		for (i = 0; i < 8; i++) {
			for (x = 0; x < cw; x++) {
				u16 c = ((color[i] >> 19) & 0x1f) << 11;
				c |= ((color[i] >> 10) & 0x3f) << 5;
				c |= ((color[i] >> 3) & 0x1f) << 0;
				pix[i * cw + x + y * LCD_W] =
					((c & 0xff00) << 16) |
					((c & 0xff) << 8); // 0xff000000;//
			}
		}
	}
	dma_cache_wback((unsigned long)panel->frame, LCD_H * LCD_W * 4);
	if (panel->cs)
		gpiod_direction_output(panel->cs, 1);

	if (0) {
		volatile u16 *addr = (u16 *)panel->nemc_addr;
		u16 *d = (u16 *)panel->frame;
		int i;
		for (i = 0; i < LCD_H * LCD_W; i++) {
			*addr = d[i * 2 + 1];
            		*addr = d[i * 2];
		}

	} else {
		dma_send_data(panel);
	}
}

static void cpu_send_data(struct panel_plat_data *panel)
{
	volatile u16 *addr = (u16 *)panel->nemc_addr;
	u16 *d = (u16 *)panel->frame;
	int i;
    for (i = 0; i < LCD_H * LCD_W; i++) {
		if (i < 4)
            		printk("%04x\n",d[i]);
		*addr = d[i * 2 + 1];
		*addr = d[i * 2];
    }
}

static irqreturn_t te_thread_irq(int irq, void *handle)
{
	struct panel_plat_data *plat = handle;
	// u16 *framebuf = (u16*)plat->frame_buf;
	// printk("te enable\n");
	if (plat->do_flush == 1) {
		if (plat->cs)
            gpiod_direction_output(plat->cs, 1);
     /* 	 cpu_send_data(plat); */
	 /* plat->do_flush = 0; */
		send_cmd(plat, 0x2c);
        	dma_send_data(plat);
		plat->do_flush = 2;
	}
	return IRQ_HANDLED;
}

#define ROTATE_90_DST(x, y) ((LCD_H - (x + 1) * 4) * LCD_W + y)
#define ROTATE_270_DST(x, y) ((x * 4) * LCD_W + (LCD_W - 1 - y))

static void rotate_90_degree(u32 *dst, u32 *src)
{
	int x, y, i;
	for (i = 0; i < (LCD_H / 4) / 8 * 8; i += 8) {
		for (y = 0; y < LCD_W; y++) {
			for (x = i; x < i + 8; x++) {
				u32 s0 = src[y * (LCD_H / 2) + x * 2];
				u32 s1 = src[y * (LCD_H / 2) + x * 2 + 1];
				int dst_index = ROTATE_90_DST(x, y);
				// 每个 dst 赋值添加高低16位交换（short序调整）

				dst[dst_index + 3 * LCD_W] =

					(((s0 << 8) & 0xff00) |
					 ((s0 << 16) & 0xff000000))
						<< 16 |
					(((s0 << 8) & 0xff00) |
					 ((s0 << 16) & 0xff000000)) >>
						16;


				dst[dst_index + 2 * LCD_W] = (((s0 >> 8) & 0xff00) | (s0 & 0xff000000)) << 16 |
											 (((s0 >> 8) & 0xff00) | (s0 & 0xff000000)) >> 16;
				dst[dst_index + 1 * LCD_W] = (((s1 << 8) & 0xff00) | ((s1 << 16) & 0xff000000)) << 16 |
											 (((s1 << 8) & 0xff00) | ((s1 << 16) & 0xff000000)) >> 16;
				dst[dst_index + 0 * LCD_W] = (((s1 >> 8) & 0xff00) | (s1 & 0xff000000)) << 16 |
											 (((s1 >> 8) & 0xff00) | (s1 & 0xff000000)) >> 16;
			}
		}
	}
	if (i < LCD_H / 4) {
		for (y = 0; y < LCD_W; y++) {
			for (x = i; x < LCD_H / 4; x++) {
				u32 s0 = src[y * (LCD_H / 2) + x * 2];
				u32 s1 = src[y * (LCD_H / 2) + x * 2 + 1];
				int dst_index = ROTATE_90_DST(x, y);
				// 每个 dst 赋值添加高低16位交换（short序调整）
				dst[dst_index + 3 * LCD_W] = (((s0 << 8) & 0xff00) | ((s0 << 16) & 0xff000000)) << 16 |
											 (((s0 << 8) & 0xff00) | ((s0 << 16) & 0xff000000)) >> 16;
				dst[dst_index + 2 * LCD_W] = (((s0 >> 8) & 0xff00) | (s0 & 0xff000000)) << 16 |
											 (((s0 >> 8) & 0xff00) | (s0 & 0xff000000)) >> 16;
				dst[dst_index + 1 * LCD_W] = (((s1 << 8) & 0xff00) | ((s1 << 16) & 0xff000000)) << 16 |
											 (((s1 << 8) & 0xff00) | ((s1 << 16) & 0xff000000)) >> 16;
				dst[dst_index + 0 * LCD_W] = (((s1 >> 8) & 0xff00) | (s1 & 0xff000000)) << 16 |
											 (((s1 >> 8) & 0xff00) | (s1 & 0xff000000)) >> 16;
			}
		}
	}
}

static void rotate_270_degree(u32 *dst, u32 *src)
{
	int x, y, i;
	for (i = 0; i < (LCD_H / 4) / 8 * 8; i += 8) {
		for (y = 0; y < LCD_W; y++) {
			for (x = i; x < i + 8; x++) {
				u32 s0 = src[y * (LCD_H / 2) + x * 2];
				u32 s1 = src[y * (LCD_H / 2) + x * 2 + 1];
				int dst_index = ROTATE_270_DST(x, y);
				// 每个 dst 赋值添加高低16位交换（short序调整）
				dst[dst_index + 0 * LCD_W] = (((s0 << 8) & 0xff00) | ((s0 << 16) & 0xff000000)) << 16 |
											 (((s0 << 8) & 0xff00) | ((s0 << 16) & 0xff000000)) >> 16;
				dst[dst_index + 1 * LCD_W] = (((s0 >> 8) & 0xff00) | (s0 & 0xff000000)) << 16 |
											 (((s0 >> 8) & 0xff00) | (s0 & 0xff000000)) >> 16;
				dst[dst_index + 2 * LCD_W] = (((s1 << 8) & 0xff00) | ((s1 << 16) & 0xff000000)) << 16 |
											 (((s1 << 8) & 0xff00) | ((s1 << 16) & 0xff000000)) >> 16;
				dst[dst_index + 3 * LCD_W] = (((s1 >> 8) & 0xff00) | (s1 & 0xff000000)) << 16 |
											 (((s1 >> 8) & 0xff00) | (s1 & 0xff000000)) >> 16;
			}
		}
	}
	if (i < LCD_H / 4) {
		for (y = 0; y < LCD_W; y++) {
			for (x = i; x < LCD_H / 4; x++) {
				u32 s0 = src[y * (LCD_H / 2) + x * 2];
				u32 s1 = src[y * (LCD_H / 2) + x * 2 + 1];
				int dst_index = ROTATE_270_DST(x, y);
				// 每个 dst 赋值添加高低16位交换（short序调整）
				dst[dst_index + 0 * LCD_W] = (((s0 << 8) & 0xff00) | ((s0 << 16) & 0xff000000)) << 16 |
											 (((s0 << 8) & 0xff00) | ((s0 << 16) & 0xff000000)) >> 16;
				dst[dst_index + 1 * LCD_W] = (((s0 >> 8) & 0xff00) | (s0 & 0xff000000)) << 16 |
											 (((s0 >> 8) & 0xff00) | (s0 & 0xff000000)) >> 16;
				dst[dst_index + 2 * LCD_W] = (((s1 << 8) & 0xff00) | ((s1 << 16) & 0xff000000)) << 16 |
											 (((s1 << 8) & 0xff00) | ((s1 << 16) & 0xff000000)) >> 16;
				dst[dst_index + 3 * LCD_W] = (((s1 >> 8) & 0xff00) | (s1 & 0xff000000)) << 16 |
											 (((s1 >> 8) & 0xff00) | (s1 & 0xff000000)) >> 16;
			}
		}
	}
}
static void rotate_0_degree(u32 *dst, u32 *src)
{
    int x, y;
    for (y = 0; y < LCD_H; y++) {
        for (x = 0; x < LCD_W / 4; x++) {
            u32 s0 = src[0];
            u32 s1 = src[1];

            // 原逻辑计算临时值，再交换高低16位（调整short序）
            u32 temp0 = ((s0 << 8) & 0xff00) | ((s0 << 16) & 0xff000000);
            dst[0] = (temp0 << 16) | (temp0 >> 16);  // 交换高低short

            u32 temp1 = ((s0 >> 8) & 0xff00) | (s0 & 0xff000000);
            dst[1] = (temp1 << 16) | (temp1 >> 16);  // 交换高低short

            u32 temp2 = ((s1 << 8) & 0xff00) | ((s1 << 16) & 0xff000000);
            dst[2] = (temp2 << 16) | (temp2 >> 16);  // 交换高低short

            u32 temp3 = ((s1 >> 8) & 0xff00) | (s1 & 0xff000000);
            dst[3] = (temp3 << 16) | (temp3 >> 16);  // 交换高低short

            dst += 4;
            src += 2;
        }
    }
}

static void nemc_pandisplay(void *panel_context,
			    void *update_buf,
			    struct rect_frame *rect_frame)
{
	struct panel_plat_data *panel = plat_panel;
	u32 *src = (u32 *)update_buf;
	u32 *dst = (u32 *)panel->frame;
	if (panel->do_flush) {
		int r = wait_for_completion_interruptible_timeout(
			&panel->dma_complete,
			msecs_to_jiffies(600));
		if (r < 0) {
			return;
		} else if (r == 0) {
			// TODO: reset
		};
	}
	if (panel->rotate == 90)
		rotate_90_degree(dst, src);
	else if (panel->rotate == 270)
		rotate_270_degree(dst, src);
	else
		rotate_0_degree(dst, src);

	dma_cache_wback((unsigned long)panel->frame, LCD_H * LCD_W * 4);
	panel->do_flush = 1;
}

static struct lcd_panel_ops st7789p3_ops = {
	.pan_display = (void *)nemc_pandisplay,
};

static struct fb_videomode panel_modes = {
	.name = "dummypanel-lcd",
	.xres = 240,
	.yres = 320,

	.refresh = 60,
	.left_margin = 0,  // hbp
	.right_margin = 0, // hfp
	.hsync_len = 0,	   // hsync
	.upper_margin = 0, // vbp
	.lower_margin = 0, // vfp
	.vsync_len = 0,	   // vsync
	.sync = FB_SYNC_HOR_HIGH_ACT & FB_SYNC_VERT_HIGH_ACT,
	.vmode = FB_VMODE_NONINTERLACED,
	.flag = 0,
};

static struct lcd_panel lcd_panel = {
	.name = "st7789p3",
	.num_modes = 1,
	.modes = &panel_modes,

	.lcd_type = LCD_TYPE_NEMC_H8WIRE,
	.bpp = 16,
	.width = LCD_W,
	.height = LCD_H,
	.dither_enable = 1,
	.dither.dither_red = 1,
	.dither.dither_green = 1,
	.dither.dither_blue = 1,

	.ops = &st7789p3_ops,
};

static int panel_st7789p3_probe(struct platform_device *pdev)
{
	int ret;
	struct panel_plat_data *panel;
	void __iomem *base;

	printk("st7789p3_probe begin!!\n");
	panel = devm_kzalloc(&pdev->dev, sizeof(*panel), GFP_KERNEL);
	if (panel == NULL)
		return -ENOMEM;
	plat_panel = panel;
	panel->dev = &pdev->dev;
	of_reserved_mem_device_init(&pdev->dev);

	ret = of_lcd_panel_parse(panel);

	if (ret == 0) {
		panel->irq = gpiod_to_irq(panel->te);
		ret = devm_request_threaded_irq(panel->dev,
						panel->irq,
						NULL,
						te_thread_irq,
						IRQF_ONESHOT |
							IRQF_TRIGGER_FALLING,
						DRV_NAME,
						panel);
		if (ret) {
			dev_err(panel->dev,
				"Failed to request irq %d: %d\n",
				panel->irq,
				ret);
			return ret;
		}
	}

	if (panel->rotate == 90 || panel->rotate == 270) {
		int w;
		int i;
		w = lcd_panel.width;
		lcd_panel.width = lcd_panel.height;
		lcd_panel.height = w;
		for (i = 0; i < lcd_panel.num_modes; i++) {
			w = lcd_panel.modes[i].xres;
			lcd_panel.modes[i].xres = lcd_panel.modes[i].yres;
			lcd_panel.modes[i].yres = w;
		}
	}

	base = devm_platform_get_and_ioremap_resource(pdev, 0, &panel->mem_res);
	if (IS_ERR(base))
		return PTR_ERR(base);
	panel->map =
		devm_regmap_init_mmio(panel->dev, base, &nemc_regmap_config);
	if (IS_ERR(panel->map))
		return PTR_ERR(panel->map);

	panel->nemc_addr = devm_ioremap(panel->dev, NEMC_PADDR, NEMC_SIZE);
	if (IS_ERR_OR_NULL(panel->nemc_addr)) {
		dev_err(panel->dev, "panel ioremap failed!\n");
		return -EBUSY;
	}
	panel->clk_gate = of_clk_get(panel->dev->of_node, 0);
	if (IS_ERR(panel->clk_gate)) {
		return dev_err_probe(panel->dev,
				     PTR_ERR(panel->clk_gate),
				     "Unable to get clock gate!\n");
	}
	ret = clk_prepare_enable(panel->clk_gate);
	if (ret)
		return ret;
	ret = dma_tx_init(panel);
	if (ret != 0) {
		return ret;
	}
	// 初始化完成量
	init_completion(&panel->dma_complete);

	{
		// 设置16bit输出
		u32 val = 0;
		regmap_read(panel->map, REG_SMC0R1, &val);
		/* printk("%x %x\n",val,*(unsigned int*)0xb3410014); */
		val &= ~(3 << 6);
		val |= 1 << 6;
		regmap_write(panel->map, REG_SMC0R1, val);
		val = 0;
		regmap_write(panel->map, REG_SMC1R1, val); // 去掉wait pin功能
	}

	if (ret == 0) {
		ret = Initial_IC(panel);
		if (ret < 0) {
			dev_err(panel->dev, "lcd misc_register failed\n");
			return -EINVAL;
		}
	}
	if(1) {
		// 设置16bit输出
		u32 val = 0;
		/* regmap_read(panel->map, REG_SMC0R1,&val); */
		/* printk("%x %x\n",val,*(unsigned int*)0xb3410014); */
		val = 0x020c0b00; // 所有读时序全部为0 WE
				  // 底电平为0xc,高电平由0x2,0xb
		val &= ~(3 << 6);
		val |= 1 << 6;
		regmap_write(panel->map, REG_SMC0R1, val);
	}
	//    dma_send_data(panel);
	panel->do_flush = 0;
	platform_set_drvdata(pdev, panel);
	ret = ingenicfb_register_panel(&lcd_panel);
	dev_info(panel->dev,
		 "NEMC st7789p3_probe end!! %s\n",
		 ret == 0 ? "OK" : "ERROR");
	return ret;
}

static int panel_st7789p3_remove(struct platform_device *dev)
{
	struct panel_plat_data *panel = platform_get_drvdata(dev);
	dma_tx_deinit(panel);
	return 0;
}

static const struct of_device_id panel_of_match[] = {
	{
		.compatible = "ingenic,nemc_st7789p3",
	},
	{},
};

MODULE_DEVICE_TABLE(of, panel_of_match);

static struct platform_driver panel_st7789p3_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(panel_of_match),
	},
	.probe		= panel_st7789p3_probe,
	.remove		= panel_st7789p3_remove,
};

module_platform_driver(panel_st7789p3_driver);

MODULE_DESCRIPTION("NEMC Panel driver for st7789p3");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");
