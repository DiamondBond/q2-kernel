
/*
 * module_drivers/drivers/misc/st7789p3.c
 * active panel using spi interface to do init
 */


#if 0
&spi0 {
	status = "okay";
	pinctrl-names = "default";
	pinctrl-0 = <&spi0_pb_tx>;
	spi-max-frequency = <50000000>;
	num-cs = <1>;
	cs-gpios = <0>;
	/* cs-gpios = <0>, <0>; */
	/* cs-gpios = <&gpb 17 GPIO_ACTIVE_HIGH INGENIC_GPIO_NOBIAS>, <&gpb 16 GPIO_ACTIVE_HIGH INGENI*/
	ingenic,chnl = <0>;
	ingenic,allow_cs_same = <1>;
	ingenic,bus_num = <0>;
	ingenic,has_dma_support = <1>;
	ingenic,spi-src-clk = <1>;/*0.ext; 1.ssi*/

	/* Add SPI interface device */
	st7789p3_spirgb_misc@0 {
		compatible = "ingenic,st7789p3_spirgb_misc";
		status = "okay";
		spi-max-frequency = <25000000>;
		reg = <0>;
		te-gpio = <&gpb 25 IRQ_TYPE_EDGE_FALLING INGENIC_GPIO_NOBIAS>;
		ingenic,rst-gpio = <&gpb 27 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>;
		ingenic,lcd-dcx-gpio = <&gpb 1 GPIO_ACTIVE_HIGH INGENIC_GPIO_NOBIAS>;
		// ingenic,pintest-gpio = <&gpb 4 GPIO_ACTIVE_HIGH INGENIC_GPIO_NOBIAS>; // 测试性能pin
		//		ingenic,cs-gpio = <&gpb 0 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>;
	};
};
#endif

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/spi/spi.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>
#include <linux/lcd.h>

#include "../ingenicfb.h"
#include "../dpu_ctrl.h"
#define DRV_NAME	"st7789p3_lcd"

#define LCD_W 240
#define LCD_H 320

struct st7789p3_plat_data {
	struct spi_device *spi;
	struct miscdevice mdev;
	struct gpio_desc *rst;
	struct gpio_desc *dcx;
	struct gpio_desc *vdd_en;
	struct gpio_desc *backlight_en;
	struct gpio_desc *te;
	struct gpio_desc *cs;
	struct completion complete;

	int  irq;
	int full_flush;
	int frame_top;
	int frame_right;

	int frame_left;
	int frame_bottom;
//	u16 frame_buf[LCD_W * LCD_H];
    u16 *cur_frame_buf;
	int cur_frame_size;

};

struct st7789p3_plat_data *plat_data;
struct gpio_desc {
	short gpio;
};

struct panel_init_data
{
    u8 cmd;
    u8 *data;
    u8 data_len;
};

static struct fb_videomode panel_modes = {
	.name = "fitipower_dummypanel-lcd",
	.xres = 240,
	.yres = 320,

	.refresh = 70,
	.left_margin = 100,	//hbp
	.right_margin = 100,	//hfp
	.hsync_len = 10,	//hsync
	.upper_margin = 30,	//vbp
	.lower_margin = 20,	//vfp
	.vsync_len = 2,	//vsync
	.sync                   = FB_SYNC_HOR_HIGH_ACT & FB_SYNC_VERT_HIGH_ACT,
	.vmode                  = FB_VMODE_NONINTERLACED,
	.flag = 0,
};
void spi_pandisplay(void*panel, void *update_buf, struct rect_frame *rect_frame);
static ssize_t lcd_spi_write(const char *buf, size_t size);

static struct lcd_panel_ops st7789p3_ops = {
    .pan_display = (void*)spi_pandisplay,
};

static struct lcd_panel lcd_panel = {
    .name = "st7789p3",
	.num_modes = 1,
	.modes = &panel_modes,

	.lcd_type = LCD_TYPE_SPI_1WIRE,
	.bpp = 16,
	.width = LCD_W,
	.height = LCD_H,
	.dither_enable = 0,
	.dither.dither_red = 0,
	.dither.dither_green = 0,
	.dither.dither_blue = 0,

    .ops = &st7789p3_ops,
};

#define DCX(plat,v) gpiod_direction_output(plat->dcx, v)
#define RESET(plat,v) gpiod_direction_output(plat->rst, v)
#define CS(plat, v)
//gpiod_direction_output(plat->cs, v);
#define INIT_ENTRY(c, ...) {                                    \
        .cmd = c,                                               \
        .data = (u8[]){__VA_ARGS__},                            \
        .data_len = sizeof((u8[]){__VA_ARGS__}) / sizeof(u8)    \
    }

static struct panel_init_data st7789p3_init_data[] = {
   INIT_ENTRY(0x36,0x00),
   INIT_ENTRY(0x3A,0x55,),
   INIT_ENTRY(0xB2,0x0C,0x0C,0x00,0x33,0x33),
   INIT_ENTRY(0xB7,0x75),
   INIT_ENTRY(0xBB,0X12),
   INIT_ENTRY(0xC0,0x2C),
   INIT_ENTRY(0xC2,0x01),
   INIT_ENTRY(0xC3,0x0f),
   INIT_ENTRY(0xC4,0x20),
   INIT_ENTRY(0xC6,0x0f),
   INIT_ENTRY(0xD0,0xA4,0xA1),
   INIT_ENTRY(0xD6,0xA1),
   INIT_ENTRY(0x35,0x00),   // TE on
   INIT_ENTRY(0xE0,0xF0,0x06,0x0D,0x08,0x08,0x05,0x35,0x44,0x4A,0x38,0x13,0x11,0x2E,0x33),
   INIT_ENTRY(0xE1,0xF0,0x0D,0x12,0x0D,0x0C,0x27,0x34,0x43,0x4A,0x36,0x10,0x12,0x2C,0x34),
};


static int SPI_WriteCommand(struct st7789p3_plat_data *plat,uint8_t cmd)
{
	int ret;
	DCX(plat,0);
	ret = spi_write(plat->spi,&cmd,1);
	if (ret < 0) {
		pr_err("SPI Write failed: %d\n", ret);
		return ret;
	}
	DCX(plat,1);
	return ret;
}

static int SPI_WriteData(struct st7789p3_plat_data *plat,uint8_t data)
{
	int ret;
	ret = spi_write(plat->spi,&data,1);
	if (ret < 0) {
		pr_err("SPI Write failed: %d\n", ret);
		return ret;
	}
	return 0;
}

static int SPI_WriteDatas(struct st7789p3_plat_data *plat,uint8_t *data, int size)
{
	int ret, i;

    for (i = 0; i < size; i++) {
        ret = spi_write(plat->spi,&data[i],1);
        if (ret < 0) {
            pr_err("SPI Write failed: %d\n", ret);
            return ret;
        }
    }
	return 0;
}

static int SPI_WriteData_16(struct st7789p3_plat_data *plat,uint16_t data)
{
	data = cpu_to_be16(data);
	return spi_write(plat->spi, &data, 2);
}

static int SPI_WriteData_16_Array(
		struct st7789p3_plat_data *plat,
		uint16_t *data,
		size_t len
		) {
	int i ,ret;
	// 分配临时缓冲区（每个 uint16_t 转为 2 字节）
	uint8_t *tx_buf = (uint8_t *)kmalloc(len * sizeof(uint16_t), GFP_KERNEL);
	if (!tx_buf) {
		return -ENOMEM; // 内存分配失败
	}

	// 批量转换并填充缓冲区
	for (i = 0; i < len; i++) {
		uint16_t be_data = cpu_to_be16(data[i]);
		memcpy(&tx_buf[i * 2], &be_data, sizeof(be_data));
	}

	// 发送数据并释放资源
	ret = spi_write(plat->spi, tx_buf, len * sizeof(uint16_t));
	kfree(tx_buf);
	return ret;
}

static void Lcd_Address_Set(struct st7789p3_plat_data *plat,uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2) {
	SPI_WriteCommand(plat,0x2a);
	SPI_WriteData_16(plat,x1);
	SPI_WriteData_16(plat,x2);

	SPI_WriteCommand(plat,0x2b);
	SPI_WriteData_16(plat,y1);
	SPI_WriteData_16(plat,y2);

	SPI_WriteCommand(plat,0x2c);
}

static void st7789p3_panel_init(struct st7789p3_plat_data *plat)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(st7789p3_init_data); i++) {
        SPI_WriteCommand(plat, st7789p3_init_data[i].cmd);
        SPI_WriteDatas(plat, st7789p3_init_data[i].data, st7789p3_init_data[i].data_len);
    }
}

static int Initial_IC(struct st7789p3_plat_data *plat)
{
	RESET(plat,1);	//dts 配置pin 为低电平，reset 为 1生效。
	udelay(100000);
	RESET(plat,0);
	udelay(120000);
	SPI_WriteCommand(plat,0x11);
	udelay(120000);

    st7789p3_panel_init(plat);

	SPI_WriteCommand(plat,0x21);
	SPI_WriteCommand(plat,0x29);
	SPI_WriteCommand(plat,0x2c);
	udelay(120000);

	return 0;
}

void spi_pandisplay(void*panel, void *update_buf, struct rect_frame *rect_frame)
{
	int size = (LCD_W * LCD_H * sizeof(u16) + 63) / 64 * 64;
	if (!plat_data->full_flush && rect_frame != NULL) {
		plat_data->frame_left = rect_frame->left;
		plat_data->frame_top  = rect_frame->top;
		plat_data->frame_right = rect_frame->right;
		plat_data->frame_bottom = rect_frame->bottom;
	}

	if (rect_frame->left == 0 && rect_frame->top == 0 && rect_frame->right == LCD_W && rect_frame->bottom == LCD_H){
		plat_data->full_flush = 1;
	}

    lcd_spi_write(update_buf, size);
}

static struct file_operations lcd_misc_fops = {
//	.write		= lcd_write,
};

static int of_lcd_panel_parse(struct st7789p3_plat_data *lcd_panel)
{
	struct device *dev = &lcd_panel->spi->dev;
	struct device_node *np = dev->of_node;
	int ret = 0;
#define PANEL_GPIO_GET(gpiodesc,name) do{								\
	if(ret == 0) {													\
		gpiodesc = devm_gpiod_get_optional(dev,name, GPIOD_ASIS); \
		if(IS_ERR_OR_NULL(gpiodesc))							\
		{														\
			gpiodesc = NULL;									\
			ret = -1;											\
			dev_err(dev,"Failed to request %s!\n",name);		\
		}														\
	}															\
}while(0)
	PANEL_GPIO_GET(lcd_panel->rst,"ingenic,rst");
	PANEL_GPIO_GET(lcd_panel->dcx,"ingenic,lcd-dcx");
	//	PANEL_GPIO_GET(lcd_panel->cs,"ingenic,cs");
	//	PANEL_GPIO_GET(test,"ingenic,pintest");
	lcd_panel->te = devm_gpiod_get_optional(&lcd_panel->spi->dev,"te", GPIOD_IN);
if(IS_ERR_OR_NULL(lcd_panel->te))
{
	lcd_panel->te = NULL;
	ret =-1;
	dev_err(dev, "No valid irq gpio");
}

return ret;
}

static ssize_t lcd_spi_write(const char *buf, size_t size)
{
	int r, s ;
	plat_data->cur_frame_buf = (u16 *) buf;

	if (plat_data->full_flush)
		s = (LCD_W * LCD_H * sizeof(u16) + 63) / 64 * 64;
	else
		s = size;

    if (plat_data->cur_frame_buf != NULL) {
        r = wait_for_completion_interruptible_timeout(&plat_data->complete,msecs_to_jiffies(600));
        if(r < 0)
            return s;
        plat_data->cur_frame_size = s;
    } else
        return -1;
    return s;
}


static irqreturn_t te_thread_irq(int irq, void *handle)
{
	struct st7789p3_plat_data *plat = handle;
	u16 *framebuf;
	if(plat->cur_frame_size > 0) {
        framebuf = (u16 *)plat_data->cur_frame_buf;
#if 1
//		Lcd_Address_Set(plat,plat->frame_left,plat->frame_top,plat->frame_right,plat->frame_bottom);
		SPI_WriteData_16_Array(plat, framebuf, plat->cur_frame_size / sizeof(uint16_t));
#else
		/* 16 bits transfer: Turn on this switch when the spi dma burst is more than 1 and can be used.
		 * When spi dma burst is 1, it has been verified that it can be
		 */
		struct spi_transfer	t = {
			.tx_buf		= framebuf,
			.len		= plat->cur_frame_size,
			.bits_per_word = 16,
		};
		spi_sync_transfer(plat->spi, &t, 1);
#endif
		plat->cur_frame_size = 0;
		complete(&plat->complete);

	}
	return IRQ_HANDLED;
}
static int st7789p3_probe(struct spi_device *spi)
{
	int ret;
	printk("st7789p3_probe begin!!\n");

	/* setup spi related info */
	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;
	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "spi setup failed %d", ret);
		return ret;
	}

	plat_data = devm_kzalloc(&spi->dev,sizeof(*plat_data), GFP_KERNEL);
	if (plat_data == NULL)
		return -ENOMEM;

	plat_data->spi = spi;

	plat_data->mdev.minor = MISC_DYNAMIC_MINOR;
	plat_data->mdev.name = DRV_NAME;
	plat_data->mdev.fops = &lcd_misc_fops;


	ret = of_lcd_panel_parse(plat_data);

	if (ret == 0) {
		plat_data->irq = gpiod_to_irq(plat_data->te);
		ret = devm_request_threaded_irq(&spi->dev, plat_data->irq,
				NULL,te_thread_irq,
				IRQF_ONESHOT | IRQF_TRIGGER_FALLING,
				DRV_NAME, plat_data);
		if (ret) {
			dev_err(&spi->dev, "Failed to request irq %d: %d\n",plat_data->irq, ret);
			goto err;
		}
	}

	if(ret == 0) {
		RESET(plat_data,0);
		ret = Initial_IC(plat_data);

		ret = ret || misc_register(&plat_data->mdev);
		if (ret < 0) {
			dev_err(&spi->dev,"lcd misc_register failed\n");
			ret = -EINVAL;
            goto err;
		}
	}

	init_completion(&plat_data->complete);
	complete(&plat_data->complete);
	plat_data->full_flush = 1;

	ret = ingenicfb_register_panel(&lcd_panel);
	if(ret < 0) {
		dev_err(&spi->dev, "Failed to ingenicfb register spi panel!\n");
	}

err:
	printk("st7789p3_probe end!! %s\n",ret == 0 ? "OK" : "ERROR");
	return ret;
}

static const struct of_device_id panel_of_match[] = {
	{ .compatible = "ingenic,st7789p3_spirgb_misc", },
	{},
};

static struct spi_driver panel_st7789p3_driver = {
	.probe		= st7789p3_probe,
	.driver		= {
		.name	= DRV_NAME,
		.owner  = THIS_MODULE,
		.of_match_table	= panel_of_match,
	},
};
module_spi_driver(panel_st7789p3_driver);


MODULE_DESCRIPTION("Panel driver for st7789p3");
MODULE_LICENSE("GPL");

