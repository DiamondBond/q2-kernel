
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
	int do_flush;
	int frame_top;
	int frame_right;

	int frame_left;
	int frame_bottom;
	u16 frame_buf[LCD_W * LCD_H];
	int cur_frame_size;

};
struct gpio_desc {
	short gpio;
};

#define DCX(plat,v) gpiod_direction_output(plat->dcx, v)
#define RESET(plat,v) gpiod_direction_output(plat->rst, v)
#define CS(plat, v) 
//gpiod_direction_output(plat->cs, v);

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
	int i;
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
    int ret = spi_write(plat->spi, tx_buf, len * sizeof(uint16_t));
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


static int Initial_IC(struct st7789p3_plat_data *plat)
{
	RESET(plat,1);	//dts 配置pin 为低电平，reset 为 1生效。
	udelay(100000);
	RESET(plat,0);
	udelay(120000);
	SPI_WriteCommand(plat,0x11);
	udelay(120000);

	SPI_WriteCommand(plat,0x36);
	SPI_WriteData(plat,0x00);

	SPI_WriteCommand(plat,0x3a);	// 16bit 565
	SPI_WriteData(plat,0x55);

	SPI_WriteCommand(plat,0xB2);
	SPI_WriteData(plat,0x0C);
	SPI_WriteData(plat,0x0C);
	SPI_WriteData(plat,0x00);
	SPI_WriteData(plat,0x33);
	SPI_WriteData(plat,0x33);

	SPI_WriteCommand(plat,0xB7);
	SPI_WriteData(plat,0x75);

	SPI_WriteCommand(plat,0xBB);
	SPI_WriteData(plat,0x12);

	SPI_WriteCommand(plat,0xC0);
	SPI_WriteData(plat,0x2c);

	SPI_WriteCommand(plat,0xC2);
	SPI_WriteData(plat,0x01);

	SPI_WriteCommand(plat,0xC3);
	SPI_WriteData(plat,0x0f);

	SPI_WriteCommand(plat,0xC4);
	SPI_WriteData(plat,0x20);

	SPI_WriteCommand(plat,0xC6);
	SPI_WriteData(plat,0x0F);

	SPI_WriteCommand(plat,0xD0);
	SPI_WriteData(plat,0xA4);
	SPI_WriteData(plat,0xA1);

	SPI_WriteCommand(plat,0xD6);
	SPI_WriteData(plat,0xA1);

	SPI_WriteCommand(plat,0x35);	// TE 撕裂效果线
	SPI_WriteData(plat,0x00);

	SPI_WriteCommand(plat,0xE0);
	SPI_WriteData(plat,0xF0);
	SPI_WriteData(plat,0x06);
	SPI_WriteData(plat,0x0D);
	SPI_WriteData(plat,0x08);
	SPI_WriteData(plat,0x08);
	SPI_WriteData(plat,0x05);
	SPI_WriteData(plat,0x35);
	SPI_WriteData(plat,0x44);
	SPI_WriteData(plat,0x4A);
	SPI_WriteData(plat,0x38);
	SPI_WriteData(plat,0x13);
	SPI_WriteData(plat,0x11);
	SPI_WriteData(plat,0x2E);
	SPI_WriteData(plat,0x33);

	SPI_WriteCommand(plat,0xE1);
	SPI_WriteData(plat,0xF0);
	SPI_WriteData(plat,0x0D);
	SPI_WriteData(plat,0x12);
	SPI_WriteData(plat,0x0D);
	SPI_WriteData(plat,0x0C);
	SPI_WriteData(plat,0x27);
	SPI_WriteData(plat,0x34);
	SPI_WriteData(plat,0x43);
	SPI_WriteData(plat,0x4A);
	SPI_WriteData(plat,0x36);
	SPI_WriteData(plat,0x10);
	SPI_WriteData(plat,0x12);
	SPI_WriteData(plat,0x2C);
	SPI_WriteData(plat,0x34);

	SPI_WriteCommand(plat,0x21);

	SPI_WriteCommand(plat,0x29);

	udelay(120000);

	return 0;
}

/*用做局刷时连接用户层的矩形坐标点*/
struct rect_frame
{
	int left,top;
	int right,bottom;
	u16 buffer[0];
};


static int panel_set_power(struct st7789p3_plat_data *plat, int power)
{
	int ret = 0;
	if(power)
	{
		gpiod_direction_output(plat->rst, 1);
		ret = Initial_IC(plat);
		if(ret != 0)
			power = 0;
	}
	if(power == 0)
		gpiod_direction_output(plat->vdd_en, 0);
	return ret;
};


/* 可以全屏刷新，给用户层提供接口 */
static ssize_t lcd_write(struct file *file, const char *buf, size_t size, loff_t *pos)
{
	struct miscdevice *mdev = file->private_data;
	struct st7789p3_plat_data *plat = container_of(mdev, struct st7789p3_plat_data, mdev);
	int ret = 0;
	int w,y,h;
	struct rect_frame *frame = (struct rect_frame*)buf;
	u16 *framebuf = frame->buffer;


	w = frame->right - frame->left;
	h = frame->bottom - frame->top;
	if(w * h <= 0 || w >= LCD_W || h >= LCD_H)
		printk("ERROR size no enough ,size:%d ",size);
	else {
		u16 *dst = plat->frame_buf;
        int r;
		w += 1;
		h += 1;
        r = wait_for_completion_interruptible_timeout(&plat->complete,msecs_to_jiffies(600));
        if(r < 0) {
            return size;
        } else if(r == 0) {
            panel_set_power(plat,0);
            panel_set_power(plat,1);
        };


        plat->frame_top = frame->top;
        plat->frame_left = frame->left;
        plat->frame_right = frame->right;
        plat->frame_bottom = frame->bottom;

        for(y = plat->frame_top;y <= plat->frame_bottom;y++) {
			ret = copy_from_user(dst,&framebuf[y * LCD_W + plat->frame_left],w * sizeof(u16));
			if (ret) {
				dev_err(&plat->spi->dev, "copy_from_user failed.");
				return -EPERM;
			}
			dst += w;
		}
		plat->cur_frame_size = (w * h * sizeof(u16) + 63) / 64 * 64;
	}
	return size;
}

static struct file_operations lcd_misc_fops = {
	.write		= lcd_write,
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
	lcd_panel->te = devm_gpiod_get_optional(&lcd_panel->spi->dev,"te", GPIOD_IN);
	if(IS_ERR_OR_NULL(lcd_panel->te))
	{
		lcd_panel->te = NULL;
		ret =-1;
		dev_err(dev, "No valid irq gpio");
	}

	return ret;
}

static irqreturn_t te_thread_irq(int irq, void *handle)
{
	struct st7789p3_plat_data *plat = handle;
	u16 *framebuf = plat->frame_buf;
    if(plat->cur_frame_size > 0) {
//		printk("te->rect: %d %d %d %d\n",plat->frame_top,plat->frame_left,plat->frame_bottom,plat->frame_right);
		Lcd_Address_Set(plat,plat->frame_left,plat->frame_top,plat->frame_right,plat->frame_bottom);
		SPI_WriteData_16_Array(plat, framebuf, plat->cur_frame_size / sizeof(uint16_t));
        plat->cur_frame_size = 0;
        complete(&plat->complete);

	}
	return IRQ_HANDLED;
}

static int st7789p3_probe(struct spi_device *spi)
{
	int ret;
	struct st7789p3_plat_data *plat_data;
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
			return ret;
		}
	}


	if(ret == 0) {
		RESET(plat_data,0);
		ret = Initial_IC(plat_data);

		ret = ret || misc_register(&plat_data->mdev);
		if (ret < 0) {
			dev_err(&spi->dev,"lcd misc_register failed\n");
			ret = -EINVAL;
		}
	}

	init_completion(&plat_data->complete);
	complete(&plat_data->complete);
	plat_data->do_flush = 0;

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

