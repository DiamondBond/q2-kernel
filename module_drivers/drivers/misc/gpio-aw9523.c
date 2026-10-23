/*
&i2c3 {
    clock-frequency = <100000>;
    pinctrl-0 = <&i2c3_pc>;
    timeout = <1000>;
    pinctrl-names = "default";
    status = "okay";

    aw9523_0:aw9523_0@5b {
		label = "aw_gpio0";
		compatible = "ingenic,aw9523_gpio";
		aw9523-reset-gpios = <&gpc 4 GPIO_ACTIVE_HIGH INGENIC_GPIO_NOBIAS>;
		reg = <0x5b>;
		gpio-controller;
		#gpio-cells = <2>;
		#address-cells = <1>;
		#size-cells = <1>;
    };
};
&msc0 {
	status = "okay";
	pinctrl-names ="default";
	pinctrl-0 = <&msc0_4bit>;

	cap-mmc-highspeed;
	max-frequency = <50000000>;
	bus-width = <4>;
	non-removable;
	voltage-ranges = <1800 3300>;

	ingenic,sdio_clk = <1>;
	keep-power-in-suspend;


	ingenic,wp-gpios = <0>;
	ingneic,cd-gpios = <0>;
	ingenic,rst-gpios = <0>;
	ingenic,removal-manual;

	bcmdhd_wlan: bcmdhd_wlan {
		 compatible = "android,bcmdhd_wlan";
		 ingenic,sdio-reset = <&aw9523_0 3 GPIO_ACTIVE_HIGH>;
    };
};
*/

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/gpio/driver.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>

#define AW9523_REG_CHIPID 0x10
#define AW9523_CHIPID 0x23

struct aw9523_gpio {
	struct gpio_desc *aw9523_rst_gpio;
	struct i2c_client *aw9523_client;
	struct gpio_chip aw9523_gpio_chip;
};

static u8 reg_read(struct aw9523_gpio *aw9523_gpio_dev, u8 reg)
{
        u8 buf;
        int ret;
        struct i2c_msg msgs[] = {
                {
                        .addr  = aw9523_gpio_dev->aw9523_client->addr,
                        .flags = 0,
                        .len   = 1,
                        .buf   = &reg,
                },
		{
                        .addr  = aw9523_gpio_dev->aw9523_client->addr,
                        .flags = I2C_M_RD,
                        .len   = 1,
                        .buf   = &buf,
                },
        };

        ret = i2c_transfer(aw9523_gpio_dev->aw9523_client->adapter, msgs, ARRAY_SIZE(msgs));
        if (ret < 0) {
                dev_err(&aw9523_gpio_dev->aw9523_client->dev, "aw9523 Reading register %x from %x failed\n",
                         reg, aw9523_gpio_dev->aw9523_client->addr);
                return ret;
        }

        return buf;
}

static int aw9523_read_reg(struct aw9523_gpio *aw9523_gpio_dev, u8 reg)
{
	int rc;

	rc = reg_read(aw9523_gpio_dev,reg);

	dev_err(&aw9523_gpio_dev->aw9523_client->dev,"aw9523 read reg = 0x%02x, value = 0x%02x\n",reg,rc);
	rc = i2c_smbus_read_byte_data(aw9523_gpio_dev->aw9523_client, reg);
	if (rc < 0) {
		dev_err(&aw9523_gpio_dev->aw9523_client->dev,
		"<%s> can't read from %02x, i2c addr= %02x\n", __func__, reg, aw9523_gpio_dev->aw9523_client->addr);
		return -rc;
	}
	return rc;
}

static int aw9523_write_reg(struct aw9523_gpio *aw9523_gpio_dev, u8 reg, u8 val)
{
	int rc;

	rc = i2c_smbus_write_byte_data(aw9523_gpio_dev->aw9523_client, reg,
				       val);
	if (rc < 0) {
		dev_err(&aw9523_gpio_dev->aw9523_client->dev,
			"<%s> can't write %02x to %02x\n", __func__, val, reg);
		return -rc;
	}
	return 0;
}

static int aw9523_check_chipid(struct aw9523_gpio *aw9523_gpio_dev)
{
	u8 val;
	u8 cnt;

	for (cnt = 5; cnt > 0; cnt--) {
		val = aw9523_read_reg(aw9523_gpio_dev, AW9523_REG_CHIPID);
		dev_err(&aw9523_gpio_dev->aw9523_client->dev,
			"<%s> aw9523 chip id %0x\n", __func__, val);
		if (val == AW9523_CHIPID)
			return 0;
	}
	return -EINVAL;
}
static int aw9523_gpio_get_value(struct aw9523_gpio *aw9523_gpio_dev, unsigned port)
{
	u8 val;
	int value;

	if (port < 0 || port > 15) {
		dev_err(&aw9523_gpio_dev->aw9523_client->dev,
			"<%s> invalid port: %d\n", __func__, port);
		return -EINVAL;
	}

	val = (u8)aw9523_read_reg(aw9523_gpio_dev, 0x02 + port / 8);
	value = (val >> port) & 1;
	return value;
}

static int aw9523_gpio_set_value(struct aw9523_gpio *aw9523_gpio_dev, unsigned port,
			  int value)
{
	u8 val;

	if (port < 0 || port > 15) {
		dev_err(&aw9523_gpio_dev->aw9523_client->dev,
			"<%s> invalid port: %d\n", __func__, port);
		return -EINVAL;
	}

	if (value != 0 && value != 1) {
		dev_err(&aw9523_gpio_dev->aw9523_client->dev,
			"<%s> invalid value\n", __func__);
		return -EINVAL;
	}

	val = (u8)aw9523_read_reg(aw9523_gpio_dev, 0x02 + port / 8);

	if (value == 0)
		val &= ~(1 << (port % 8));
	else
		val |= 1 << (port % 8);

	dev_dbg(&aw9523_gpio_dev->aw9523_client->dev,
			"<%s> begin to write reg\n", __func__);

	return aw9523_write_reg(aw9523_gpio_dev, 0x02 + port / 8, val);
}

static int aw9523_gpio_set_direction_out(struct aw9523_gpio *aw9523_gpio_dev, unsigned port,
					int value)
{
	u8 val;
	if (port < 0 || port > 15) {
		dev_err(&aw9523_gpio_dev->aw9523_client->dev,
			"<%s> invalid port: %d\n", __func__, port);
		return -EINVAL;
	}

	val = (u8)aw9523_read_reg(aw9523_gpio_dev, 0x12 + port / 8);
	val |= 1 << (port % 8);
	aw9523_write_reg(aw9523_gpio_dev, 0x12 + port / 8, val);

	val = (u8)aw9523_read_reg(aw9523_gpio_dev, 0x04 + port / 8);
	val &= ~(1 << (port % 8));
	return aw9523_write_reg(aw9523_gpio_dev, 0x04 + port / 8, val);
}

static int aw9523_gpio_set_direction_input(struct aw9523_gpio *aw9523_gpio_dev, unsigned port)
{
	u8 val;
	if (port < 0 || port > 15) {
		dev_err(&aw9523_gpio_dev->aw9523_client->dev,
			"<%s> invalid port: %d\n", __func__, port);
		return -EINVAL;
	}

	val = (u8)aw9523_read_reg(aw9523_gpio_dev, 0x12 + port / 8);
	val |= 1 << (port % 8);
	aw9523_write_reg(aw9523_gpio_dev, 0x12 + port / 8, val);

	val = (u8)aw9523_read_reg(aw9523_gpio_dev, 0x04 + port / 8);
	val |= 1 << (port % 8);
	return aw9523_write_reg(aw9523_gpio_dev, 0x04 + port / 8, val);
}

int aw9523_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	int value;
	struct aw9523_gpio *aw9523_gpio_dev =
		container_of(chip, struct aw9523_gpio, aw9523_gpio_chip);

	value = aw9523_gpio_get_value(aw9523_gpio_dev, offset);

	return value;
}

void aw9523_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct aw9523_gpio *aw9523_gpio_dev =
		container_of(chip, struct aw9523_gpio, aw9523_gpio_chip);
	aw9523_gpio_set_value(aw9523_gpio_dev, offset, value);
}

static int aw9523_gpio_direction_out(struct gpio_chip *chip, unsigned offset,
				     int value)
{

	struct aw9523_gpio *aw9523_gpio_dev =
		container_of(chip, struct aw9523_gpio, aw9523_gpio_chip);

	dev_err(&aw9523_gpio_dev->aw9523_client->dev,
		"%s:offset = %d,value = %d\n", __func__, offset, value);

	aw9523_gpio_set_direction_out(aw9523_gpio_dev, offset, value);
	return 0;
}

static int aw9523_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{

	struct aw9523_gpio *aw9523_gpio_dev =
		container_of(chip, struct aw9523_gpio, aw9523_gpio_chip);

	dev_err(&aw9523_gpio_dev->aw9523_client->dev,
		"%s:offset = %d\n", __func__, offset);

	aw9523_gpio_set_direction_input(aw9523_gpio_dev, offset);
	return 0;
}

void aw9523_gpio_reg_init(struct aw9523_gpio *aw9523_gpio_dev){
	aw9523_write_reg(aw9523_gpio_dev, 0x11, 0x10);
	aw9523_write_reg(aw9523_gpio_dev, 0x12, 0xff);
	aw9523_write_reg(aw9523_gpio_dev, 0x13, 0xff);
	aw9523_write_reg(aw9523_gpio_dev, 0x04, 0x00);
	aw9523_write_reg(aw9523_gpio_dev, 0x05, 0x00);
}

static int aw9523_gpio_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	int ret;
	struct aw9523_gpio *aw9523_gpio_dev;

	aw9523_gpio_dev = devm_kzalloc(
		&client->dev, (sizeof(struct aw9523_gpio)), GFP_KERNEL);
	if (!aw9523_gpio_dev)
		return -ENOMEM;

	aw9523_gpio_dev->aw9523_client = client;
	aw9523_gpio_dev->aw9523_rst_gpio =
		devm_gpiod_get_optional(&client->dev, "aw9523-reset", 0);
	if (!aw9523_gpio_dev->aw9523_rst_gpio) {
		dev_err(&client->dev, "invalid  reset gpio\n");
		return -1;
	} else {
		gpiod_direction_output(aw9523_gpio_dev->aw9523_rst_gpio, 1);
		msleep(20);
		gpiod_direction_output(aw9523_gpio_dev->aw9523_rst_gpio, 0);
		msleep(20);
		gpiod_direction_output(aw9523_gpio_dev->aw9523_rst_gpio, 1);
		msleep(20);
	}

	aw9523_gpio_dev->aw9523_gpio_chip.label = "aw_gpio0";
	aw9523_gpio_dev->aw9523_gpio_chip.owner = THIS_MODULE;
	aw9523_gpio_dev->aw9523_gpio_chip.direction_output =
		aw9523_gpio_direction_out;
	aw9523_gpio_dev->aw9523_gpio_chip.direction_input  =
		aw9523_gpio_direction_input;
	aw9523_gpio_dev->aw9523_gpio_chip.set = aw9523_gpio_set;
	aw9523_gpio_dev->aw9523_gpio_chip.get = aw9523_gpio_get;
	aw9523_gpio_dev->aw9523_gpio_chip.ngpio = 16;
	aw9523_gpio_dev->aw9523_gpio_chip.parent = &client->dev;
	aw9523_gpio_dev->aw9523_gpio_chip.base = 160;

	ret = devm_gpiochip_add_data(&client->dev,
				     &(aw9523_gpio_dev->aw9523_gpio_chip),
				     &(aw9523_gpio_dev->aw9523_gpio_chip));
	if (ret) {
		dev_err(&client->dev, "devm_gpiochip_add_data error\n");
		return -EINVAL;
	}

	ret = aw9523_check_chipid(aw9523_gpio_dev);
	if (ret) {
		dev_err(&client->dev, "Check chip id error\n");
		return -EINVAL;
	}
	aw9523_gpio_reg_init(aw9523_gpio_dev);
	i2c_set_clientdata(client, aw9523_gpio_dev);

	dev_err(&client->dev, "aw9523_probe succeeded\n");

	return 0;
}

static int aw9523_gpio_remove(struct i2c_client *client)
{
	return 0;
}

static const struct of_device_id aw9523_match_table[] = {
	{.compatible = "ingenic,aw9523_gpio",},
	{},
};
MODULE_DEVICE_TABLE(of, aw9523_match_table);

static const struct i2c_device_id aw9523_id[] = {
	{ "ingenic,aw9523_gpio", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, aw9523_id);

static struct i2c_driver aw9523_driver = {
	.driver = {
		.name		= "aw9523-gpio",
		.owner		= THIS_MODULE,
		.of_match_table = aw9523_match_table,
	},
	.probe    = aw9523_gpio_probe,
	.remove   = aw9523_gpio_remove,
	.id_table = aw9523_id,
};

module_i2c_driver(aw9523_driver);
MODULE_DESCRIPTION("AW9523 GPIO Driver");
MODULE_LICENSE("GPL V2");
