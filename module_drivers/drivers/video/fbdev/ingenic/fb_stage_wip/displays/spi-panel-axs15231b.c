
/*
 * active panel using spi interface to do init
 */

#if 0
&spi0 {
	status = "okay";
	pinctrl-names = "default";
	pinctrl-0 = <&spi0_pa>;
	spi-max-frequency = <50000000>;
	num-cs = <1>;
	ingenic,chnl = <0>;
	ingenic,allow_cs_same = <1>;
	ingenic,bus_num = <0>;
	ingenic,spi-src-clk = <1>;/*0.ext; 1.ssi*/

	/* Add SPI interface device */
	axs1523b_spi_lcd: axs1523b_spi_lcd@0 {
		compatible = "ingenic,axs15231b_spi_rgb565";
		spi-max-frequency = <50000000>;
		reg = <0>;
		ingenic,rst-gpio = <&gpa 17 GPIO_ACTIVE_HIGH INGENIC_GPIO_NOBIAS>;
		ingenic,lcd-dcx-gpio = <&gpa 25 GPIO_ACTIVE_HIGH INGENIC_GPIO_NOBIAS>;
		ingenic,lcd-pwr-gpio = <&gpa 16 GPIO_ACTIVE_HIGH INGENIC_GPIO_NOBIAS>;
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

#define DRV_NAME	"axs15231b_lcd"

#define LCD_W 172
#define LCD_H 560

struct axs15231b_plat_data {
	struct spi_device *spi;
	struct gpio_desc *rst;
	struct gpio_desc *dcx;
	struct gpio_desc *pwr;
	struct gpio_desc *te;
	struct gpio_desc *cs;
	uint8_t *tx_buffer;
	int tx_buffer_size;
	int per_frame;
};

struct axs15231b_plat_data *plat_data;

struct gpio_desc {
	short gpio;
};

static struct fb_videomode panel_modes = {
	.name = "fitipower_dummypanel-lcd",
	.xres = LCD_W,
	.yres = LCD_H,

	.refresh = 60,
};

void spi_pandisplay(void*panel, void *update_buf, struct rect_frame *rect_frame);

static struct lcd_panel_ops axs15231b_ops = {
	.pan_display = (void*)spi_pandisplay,
};

static struct lcd_panel lcd_panel = {
	.name = "axs15231b",
	.num_modes = 1,
	.modes = &panel_modes,

	.lcd_type = LCD_TYPE_SPI_1WIRE,
	.bpp = 16,
	.width = LCD_W,
	.height = LCD_H,

	.ops = &axs15231b_ops,
};

#define DCX(plat,v) gpiod_direction_output(plat->dcx, v)
#define RESET(plat,v) gpiod_direction_output(plat->rst, v)
#define PWR(plat,v) gpiod_direction_output(plat->pwr, v)

static int SPI_WriteCommand(struct axs15231b_plat_data *plat,uint8_t cmd) {
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

static int SPI_WriteData(struct axs15231b_plat_data *plat,uint8_t data) {
	int ret;
	ret = spi_write(plat->spi,&data,1);
	if (ret < 0) {
		pr_err("SPI Write failed: %d\n", ret);
		return ret;
	}
	return 0;
}

static int SPI_flush_frame(struct axs15231b_plat_data *plat,uint16_t *data,size_t len) {
	int i ,ret = 0;
	uint8_t *tx_buf = plat->tx_buffer;

	for (i = 0; i < len /2 ; i++) {
		uint16_t be_data = cpu_to_be16(data[i]);
		memcpy(&tx_buf[i * 2], &be_data, sizeof(be_data));
	}

	ret = spi_write(plat->spi, tx_buf, len);
	if(ret){
		printk("spi tx err %s\n",__func__);
		return ret;
	}

	return ret;
}

static int Initial_IC(struct axs15231b_plat_data *plat) {
	RESET(plat,1);
	udelay(130000);
	RESET(plat,0);
	udelay(130000);
	RESET(plat,1);
	udelay(220000);

	SPI_WriteCommand(plat,0x02);
	SPI_WriteData(plat,0x00);
	SPI_WriteCommand(plat,0x11);
	SPI_WriteData(plat,0x00);

	udelay(200000);
	SPI_WriteCommand(plat,0x02);
	SPI_WriteData(plat,0x00);
	SPI_WriteCommand(plat,0x29);
	SPI_WriteData(plat,0x00);
	udelay(200000);

	SPI_WriteCommand(plat,0x36);
	SPI_WriteData(plat,0x08);

	SPI_WriteCommand(plat,0x35);
	SPI_WriteData(plat,0x0);

	SPI_WriteCommand(plat,0x2a);
	SPI_WriteData(plat,0x00);
	SPI_WriteData(plat,0x00);
	SPI_WriteData(plat,0x00);
	SPI_WriteData(plat,0xab);

	SPI_WriteCommand(plat,0x2b);
	SPI_WriteData(plat,0x00);
	SPI_WriteData(plat,0x00);
	SPI_WriteData(plat,0x02);
	SPI_WriteData(plat,0x2f);

	return 0;
}

void spi_pandisplay(void*panel, void *update_buf, struct rect_frame *rect_frame) {
	plat_data->per_frame += rect_frame->frame_size;
	if(plat_data->per_frame == rect_frame->right *rect_frame->bottom *2){
		SPI_WriteCommand(plat_data,0x2c);
		SPI_flush_frame(plat_data, update_buf, plat_data->per_frame);
		plat_data->per_frame = 0;
	}
}

static int of_lcd_panel_parse(struct axs15231b_plat_data *lcd_panel) {
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
	PANEL_GPIO_GET(lcd_panel->pwr,"ingenic,lcd-pwr");

	return ret;
	}

static int axs15231b_probe(struct spi_device *spi){
	int ret;
	printk("axs15231b_probe enter !!\n");

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

	plat_data->tx_buffer_size = LCD_H * LCD_W *2;
	plat_data->tx_buffer = devm_kzalloc(&spi->dev, plat_data->tx_buffer_size, GFP_KERNEL);
	if (!plat_data->tx_buffer) {
		return -ENOMEM;
	}

	ret = of_lcd_panel_parse(plat_data);
	if(ret == 0) {
		PWR(plat_data,0);
		RESET(plat_data,0);
		udelay(200000);
		ret = Initial_IC(plat_data);
	}


	ret = ingenicfb_register_panel(&lcd_panel);
	if(ret < 0) {
		dev_err(&spi->dev, "Failed to ingenicfb register spi panel!\n");
		return ret;
	}

err:
	printk("axs15231b_probe end!! %s\n",ret == 0 ? "OK" : "ERROR");
	return ret;
}


static const struct of_device_id panel_of_match[] = {
	{ .compatible = "ingenic,axs15231b_spi_rgb565", },
	{},
};

static struct spi_driver panel_axs15231b_driver = {
	.probe		= axs15231b_probe,
	.driver		= {
		.name	= DRV_NAME,
		.owner  = THIS_MODULE,
		.of_match_table	= panel_of_match,
	},
};
module_spi_driver(panel_axs15231b_driver);


MODULE_DESCRIPTION("Panel driver for axs15231b");
MODULE_LICENSE("GPL");

