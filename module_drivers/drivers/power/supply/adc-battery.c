#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/iio/consumer.h>
#include <linux/power_supply.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/delay.h>
/*
 dts config the reference is as follows

/ {

 battery_monitor {
		compatible = "ingenic,battery-monitor";
		adc-channel = <13>;  // ADC通道号
		vref-microvolts = <1800000>;  // 参考电压 (微伏)

		// 引用已存在的ADC控制器
		io-channels = <&sadc 1>;
		io-channel-names = "voltage" ;

		// 定义充电状态GPIO
		charging-gpios = <&gpc 4 IRQ_TYPE_EDGE_RISING INGENIC_GPIO_NOBIAS>;

		// 定义充电满状态GPIO
		charged-gpios = <&gpc 22 IRQ_TYPE_EDGE_FALLING INGENIC_GPIO_NOBIAS>;

		// 电池基本参数
		vref-mv = <1800>;				// 参考电压(mV)
		full-voltage-mv = <8400>;      // 满电电压 (mV)
		empty-voltage-mv = <4200>;     // 空电电压 (mV)
		capacity-mah = <800>;         // 电池容量 (mAh)
		voltage-sample-count = <5>;    // 采样次数
		voltage-sample-delay-ms = <20>; // 采样间隔 (ms)
		adc-resolution = <12>;			// adc 位数

		voltage-divider = <57 10>; // 分子/分母，例如47/10 两个电阻表示分压比为 57:10
		// 放电曲线表（电压-电量对照表）
		discharge-table = <
			8300 100
			8200 90
			8180 80
			8170 70
			8160 60
			8150 50
			8140 40
			8130 30
			8120 20
			8110 10
			8000 0
				>;

		// 充电曲线表（电压-电量对照表）
		charge-table = <
			8400 100
			7800 90
			7000 80
			6600 70
			6200 60
			5800 50
			5400 40
			5000 30
			4600 20
			4500 10
			4200 0
				>;
	};
};

	&sadc{
		status = "okay";
			pinctrl-names = "default";
				pinctrl-0 = <&aux12_pe>,<&aux13_pe>;
					seq1 = <12 13>;
			seq1-cont-interval = <10>;
			ingenic,has_dma_support = <1>;
	};

 *
 *
 * */

#define DEFAULT_VOLTAGE_SAMPLE_COUNT 5
#define DEFAULT_VOLTAGE_SAMPLE_DELAY_MS 100
#define MAX_TABLE_POINTS 20

struct battery_lookup_point {
	int voltage_mv;
	int capacity_percent;
};

struct battery_calibration {
	bool calibrated;
	bool user_triggered;
};

struct battery_config {
	int full_voltage_mv;
	int empty_voltage_mv;
	int capacity_mah;
	int voltage_sample_count;
	int voltage_sample_delay_ms;
	int current_sample_count;
	int current_sample_delay_ms;
	int temp_sample_count;
	int temp_sample_delay_ms;
	int vref_mv;
	int adc_resolution;

	int current_offset;
	int current_scale;

	int temp_offset;
	int temp_scale;

	struct battery_lookup_point discharge_table[MAX_TABLE_POINTS];
	int discharge_table_size;

	struct battery_lookup_point charge_table[MAX_TABLE_POINTS];
	int charge_table_size;

	unsigned int divider_numerator;
	unsigned int divider_denominator;
};


struct battery_info {
	struct device		*dev ;
	struct iio_channel *iio_chan;
	struct power_supply *psy;
	struct delayed_work update_work;

	struct gpio_desc *gpio_charging;
	struct gpio_desc *gpio_charged;
	int charging_irq;
	int charged_irq;

	struct battery_config config;
	struct battery_calibration cal;

	int voltage_mv;
	int current_full_voltage_mv;
	int current_full_voltage_adc;
	int capacity_percent;
	bool online;
	bool charging;
	bool charged;
	bool calibration_pending;
	bool calibration_requested;
	int  calibration_user_set;
	int resistance_error_factor_scaled;
	struct kobject *cal_kobj;
};

static enum power_supply_property battery_props[] = {
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CALIBRATE,
};


// Calculate and save the resistance error compensation coefficient
static void calculate_resistor_error_compensation(struct battery_info *info, int actual_adc, int full_voltage_mv)
{
	int theoretical_full_adc = (full_voltage_mv *
			(1 << info->config.adc_resolution) *
			info->config.divider_denominator) /
		(info->config.vref_mv * info->config.divider_numerator);
	if(actual_adc == 0) {
		dev_err(info->dev,"Actual adc val can't zero");
		return;
	}
	info->resistance_error_factor_scaled =  (theoretical_full_adc * 1024) / actual_adc;

	dev_info(info->dev, "Resistance error compensation factor: %d (scaled by 1024)\n",
			              info->resistance_error_factor_scaled);
}

// Convert ADC raw values to voltage (considering calibration and voltage division)
static int adc_to_voltage(struct battery_info *info, int adc_raw, int full_voltage_adc, int full_voltage_mv)
{
	int voltage_mv;

	if (full_voltage_adc == 0 || info->config.divider_denominator == 0) {
		dev_err(info->dev, "Full voltage adc and divider_denominator can't zero");
		return -1;
	}

	if (info->cal.calibrated) {
		// Linear mapping: The ratio of the current ADC value to the full charge ADC value,
		// mapped to the full charge voltage
		voltage_mv = (adc_raw * full_voltage_mv) / full_voltage_adc;
		voltage_mv = ((voltage_mv * info->resistance_error_factor_scaled)+512) >> 10; // Compensation rounding
//		dev_info(info->dev, "this is calibrated\n");
	} else {
		// Calculate ADC voltage using default reference values
		voltage_mv = (adc_raw * info->config.vref_mv) / (1 << info->config.adc_resolution);
		voltage_mv = (voltage_mv * info->config.divider_numerator) / info->config.divider_denominator;
//		dev_info(info->dev, "this is default\n");
	}

//	dev_info(info->dev, "voltage_mv:%d\n", voltage_mv);
	return voltage_mv;
}

// Using the lookup table method to calculate the percentage of electricity consumption
static int voltage_to_percentage(struct battery_info *info, int voltage_mv)
{
	const struct battery_lookup_point *table;
	int table_size;
	int i;


	if (info->charging) {
		table = info->config.charge_table;
		table_size = info->config.charge_table_size;
	} else {
		table = info->config.discharge_table;
		table_size = info->config.discharge_table_size;
	}


	if (voltage_mv >= table[0].voltage_mv) {
		return table[0].capacity_percent;
	}

	if (voltage_mv <= table[table_size-1].voltage_mv) {
		return table[table_size-1].capacity_percent;
	}

	for (i = 0; i < table_size-1; i++) {
		if (voltage_mv >= table[i+1].voltage_mv &&
				voltage_mv <= table[i].voltage_mv) {
			// Linear interpolation calculation
			return table[i+1].capacity_percent +
				((voltage_mv - table[i+1].voltage_mv) *
				 (table[i].capacity_percent - table[i+1].capacity_percent) /
				 (table[i].voltage_mv - table[i+1].voltage_mv));
		}
	}

	return 0;
}

// Read ADC voltage value (with filtering)
static int read_battery_voltage(struct battery_info *info)
{
	int i, ret, sum = 0;
	int samples[info->config.voltage_sample_count];
	int adc_raw;

	for (i = 0; i < info->config.voltage_sample_count; i++) {
		ret = iio_read_channel_raw(info->iio_chan, &samples[i]);
		if (ret < 0) {
			dev_err(info->dev, "Failed to read ADC: %d\n", ret);
			return ret;
		}

		msleep(info->config.voltage_sample_delay_ms);
	}

	for (i = 0; i < info->config.voltage_sample_count; i++) {
		sum += (samples[i] & 0xFFF);  // Remove the bare data value of the number of channels.
	}

	adc_raw = sum / info->config.voltage_sample_count;
	return adc_to_voltage(info, adc_raw, info->current_full_voltage_adc, info->current_full_voltage_mv);
}


static void perform_calibration(struct battery_info *info)
{
	int i, ret, sum = 0;
	int samples[info->config.voltage_sample_count];
	dev_info(info->dev, "Performing ADC calibration...\n");

	for (i = 0; i < info->config.voltage_sample_count; i++) {
		ret = iio_read_channel_raw(info->iio_chan, &samples[i]);
		if (ret < 0) {
			dev_err(&info->psy->dev, "Calibration failed: %d\n", ret);
			return;
		}

		msleep(info->config.voltage_sample_delay_ms);
	}

	for (i = 0; i < info->config.voltage_sample_count; i++)
		sum += samples[i] & 0xFFF;


	info->cal.calibrated = true;
	info->cal.user_triggered = info->calibration_requested;
	info->calibration_requested = false;

	if(info->calibration_user_set > 0) {
		info->current_full_voltage_mv = info->calibration_user_set;
		info->calibration_user_set = 0;
	} else {
		info->current_full_voltage_mv = info->config.full_voltage_mv;
	}

	info->current_full_voltage_adc = (info->current_full_voltage_mv * (1 << info->config.adc_resolution) *
		info->config.divider_denominator) /(info->config.vref_mv * info->config.divider_numerator);

	calculate_resistor_error_compensation(info,sum / info->config.voltage_sample_count, info->current_full_voltage_mv);
	dev_info(info->dev, "Calibration completed: full voltage ADC=%d\n",
			info->current_full_voltage_adc);
}

static void update_battery_status(struct work_struct *work)
{
	struct battery_info *info = container_of(to_delayed_work(work),
			struct battery_info, update_work);
	int voltage_mv;
	enum power_supply_property prop;
	int props[] = {
		POWER_SUPPLY_PROP_CAPACITY,
		POWER_SUPPLY_PROP_VOLTAGE_NOW,
		POWER_SUPPLY_PROP_STATUS,
		POWER_SUPPLY_PROP_ONLINE,
		POWER_SUPPLY_PROP_PRESENT,
		POWER_SUPPLY_PROP_CHARGE_FULL,
	};

	if (info->gpio_charging) {
		info->charging = gpiod_get_value(info->gpio_charging);
	}

	voltage_mv = read_battery_voltage(info);
	if (voltage_mv < 0) {
		info->online = false;
	} else {
		info->voltage_mv = voltage_mv;
		if ((info->charging && (info->capacity_percent < voltage_to_percentage(info, voltage_mv)))
				|| (!info->charging && (info->capacity_percent > voltage_to_percentage(info, voltage_mv)))
				|| 0 == info->capacity_percent) {
			info->capacity_percent = voltage_to_percentage(info, voltage_mv);
		}
		info->online = true;
	}

	if (info->gpio_charged) {
		info->charged = gpiod_get_value(info->gpio_charged);
	}

	if ((!info->charged && !info->charging && !info->cal.calibrated) ||
			info->calibration_requested == true ) {
		if (!info->calibration_pending) {
			info->calibration_pending = true;
			dev_info(info->dev, "Calibration pending...\n");
		} else {
			perform_calibration(info);
			info->calibration_pending = false;
		}
	}

	for (prop = 0; prop < ARRAY_SIZE(props); prop++)
		power_supply_changed(info->psy);

	schedule_delayed_work(&info->update_work, msecs_to_jiffies(5000));
}

static irqreturn_t charging_irq_handler(int irq, void *dev_id)
{
	struct battery_info *info = dev_id;

	cancel_delayed_work_sync(&info->update_work);
	schedule_delayed_work(&info->update_work, 0);

	return IRQ_HANDLED;
}

static irqreturn_t charged_irq_handler(int irq, void *dev_id)
{
	struct battery_info *info = dev_id;

	cancel_delayed_work_sync(&info->update_work);
	schedule_delayed_work(&info->update_work, 0);

	return IRQ_HANDLED;
}

static int battery_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct battery_info *info = power_supply_get_drvdata(psy);
	int ret = 0;

	switch (psp) {
		case POWER_SUPPLY_PROP_CAPACITY:
			val->intval = info->capacity_percent;
			break;

		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			val->intval = info->voltage_mv;
			break;

		case POWER_SUPPLY_PROP_STATUS:
			if (!info->online)
				val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
			else if (info->charging || !info->charged)
				val->intval = !info->charged ?
					POWER_SUPPLY_STATUS_FULL :
					POWER_SUPPLY_STATUS_CHARGING;
			else
				val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
			break;

		case POWER_SUPPLY_PROP_PRESENT:
		case POWER_SUPPLY_PROP_ONLINE:
			val->intval = info->online;
			break;

		case POWER_SUPPLY_PROP_TECHNOLOGY:
			val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
			break;

		case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
			val->intval = info->config.capacity_mah * info->config.full_voltage_mv;
			break;

		case POWER_SUPPLY_PROP_CHARGE_FULL:
			val->intval = info->charged;
			break;

		case POWER_SUPPLY_PROP_CALIBRATE:
			val->intval = info->cal.calibrated;
			break;

		default:
			ret = -EINVAL;
			break;
	}

	return ret;
}
#define to_battery_info_device(kobj) dev_get_drvdata(container_of(kobj->parent, struct device, kobj))

static ssize_t store_trigger_calibration(struct kobject *kobj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct battery_info *info = to_battery_info_device(kobj);
	unsigned long value;
	int ret;

	ret = kstrtoul(buf, 10, &value);
	if (ret)
		return ret;

	if (value != 0) {
		info->calibration_user_set = value;
		info->calibration_requested = true;
		schedule_delayed_work(&info->update_work, msecs_to_jiffies(1000));
	}

	return count;
}

static ssize_t store_calibration_value(struct kobject *kobj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct battery_info *info = to_battery_info_device(kobj);
	unsigned long value;
	int ret;

	ret = kstrtoul(buf, 10, &value);
	if (ret)
		return ret;

	if (value != 0) {
		info->resistance_error_factor_scaled = value;
		info->cal.calibrated = true;
		//	info->cal.user_triggered = true;
		schedule_delayed_work(&info->update_work, msecs_to_jiffies(1000));
	}
	dev_info(info->dev, "Manual calibration set: ADC=%lu\n", value);

	return count;
}

static ssize_t show_voltage_divider(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct battery_info *info = to_battery_info_device(kobj);

	return sprintf(buf, "%u/%u\n",
			info->config.divider_numerator,
			info->config.divider_denominator);
}

static ssize_t store_voltage_divider(struct kobject *kobj,
		struct kobj_attribute *attr,
		const char *buf, size_t count)
{
	struct battery_info *info = to_battery_info_device(kobj);
	unsigned long numerator, denominator;
	char *end;

	// format:"numerator/denominator"
	numerator = simple_strtoul(buf, &end, 10);
	if (*end != '/')
		return -EINVAL;

	denominator = simple_strtoul(end + 1, NULL, 10);
	if (denominator == 0)
		return -EINVAL;

	info->config.divider_numerator = numerator;
	info->config.divider_denominator = denominator;

	dev_info(info->dev, "Voltage divider set: %lu/%lu\n",
			numerator, denominator);

	return count;
}
static ssize_t show_calibration_val(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct battery_info *info = to_battery_info_device(kobj);

	return sprintf(buf, "%u\n",
			info->resistance_error_factor_scaled);
}



static struct kobj_attribute trigger_calibration_attr =
__ATTR(trigger, 0200, NULL, store_trigger_calibration);

static struct kobj_attribute calibration_value_attr =
__ATTR(value, 0644, show_calibration_val, store_calibration_value);

static struct kobj_attribute voltage_divider_attr =
__ATTR(divider, 0644, show_voltage_divider, store_voltage_divider);

static struct attribute *calibration_attrs[] = {
	&trigger_calibration_attr.attr,
	&calibration_value_attr.attr,
	&voltage_divider_attr.attr,
	NULL,
};

static struct attribute_group calibration_attr_group = {
	.attrs = calibration_attrs,
	.name = "calibration",
};

static struct power_supply_desc battery_desc = {
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = battery_props,
	.num_properties = ARRAY_SIZE(battery_props),
	.get_property = battery_get_property,
};

static int parse_battery_config(struct device *dev, struct battery_config *config)
{
	struct device_node *np = dev->of_node;
	const __be32 *table_data;
	int count, i, ret;
	unsigned int divider[2];

	ret = of_property_read_u32(np, "full-voltage-mv", &config->full_voltage_mv);
	if (ret) {
		dev_err(dev, "Missing full-voltage-mv property\n");
		return ret;
	}

	ret = of_property_read_u32(np, "empty-voltage-mv", &config->empty_voltage_mv);
	if (ret) {
		dev_err(dev, "Missing empty-voltage-mv property\n");
		return ret;
	}

	ret = of_property_read_u32(np, "capacity-mah", &config->capacity_mah);
	if (ret) {
		dev_err(dev, "Missing capacity-mah property\n");
		return ret;
	}

	ret = of_property_read_u32(np, "vref-mv", &config->vref_mv);
	if (ret) {
		dev_err(dev, "Missing vref-mv property, using default 3300mV\n");
		config->vref_mv = 1800;
	}

	ret = of_property_read_u32(np, "adc-resolution", &config->adc_resolution);
	if (ret) {
		dev_err(dev, "Missing adc-resolution property, using default 12 bits\n");
		config->adc_resolution = 12;
	}

	of_property_read_u32(np, "voltage-sample-count",
			&config->voltage_sample_count);
	if (!config->voltage_sample_count)
		config->voltage_sample_count = DEFAULT_VOLTAGE_SAMPLE_COUNT;

	of_property_read_u32(np, "voltage-sample-delay-ms",
			&config->voltage_sample_delay_ms);
	if (!config->voltage_sample_delay_ms)
		config->voltage_sample_delay_ms = DEFAULT_VOLTAGE_SAMPLE_DELAY_MS;

	ret = of_property_read_u32_array(np, "voltage-divider", divider, 2);
	if (ret == 0) {
		config->divider_numerator = divider[0];
		config->divider_denominator = divider[1];
		dev_info(dev, "Voltage divider ratio: %u/%u\n",
				            config->divider_numerator, config->divider_denominator);
	} else {
		config->divider_numerator = 1;
		config->divider_denominator = 1;
		dev_info(dev, "Using default voltage divider ratio: 1/1\n");
	}

	table_data = of_get_property(np, "discharge-table", &count);
	if (!table_data || count <= 0) {
		dev_err(dev, "Missing discharge-table property\n");
		return -EINVAL;
	}

	if ((count / sizeof(__be32)) % 2 != 0) {
		dev_err(dev, "Invalid discharge-table size\n");
		return -EINVAL;
	}

	config->discharge_table_size = (count / sizeof(__be32)) / 2;

	if (config->discharge_table_size > MAX_TABLE_POINTS) {
		dev_err(dev, "Discharge table exceeds maximum size\n");
		return -EINVAL;
	}

	for (i = 0; i < config->discharge_table_size; i++) {
		config->discharge_table[i].voltage_mv = be32_to_cpup(&table_data[i*2]);
		config->discharge_table[i].capacity_percent = be32_to_cpup(&table_data[i*2+1]);
	}

	table_data = of_get_property(np, "charge-table", &count);
	if (!table_data || count <= 0) {
		dev_err(dev, "Missing charge-table property\n");
		return -EINVAL;
	}

	if ((count / sizeof(__be32)) % 2 != 0) {
		dev_err(dev, "Invalid charge-table size\n");
		return -EINVAL;
	}

	config->charge_table_size = (count / sizeof(__be32)) / 2;

	if (config->charge_table_size > MAX_TABLE_POINTS) {
		dev_err(dev, "Charge table exceeds maximum size\n");
		return -EINVAL;
	}

	for (i = 0; i < config->charge_table_size; i++) {
		config->charge_table[i].voltage_mv = be32_to_cpup(&table_data[i*2]);
		config->charge_table[i].capacity_percent = be32_to_cpup(&table_data[i*2+1]);
	}

//	dev_info(dev, "Battery config loaded: %d/%d table points, divider: %u/%u\n",
//			config->discharge_table_size, config->charge_table_size,
//			config->divider_numerator, config->divider_denominator);
	return 0;
}


static int battery_probe(struct platform_device *pdev)
{
	struct battery_info *info;
	struct power_supply_config psy_cfg = {};
	int ret;

	printk("========>>>> battery_probe\n");

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;


	ret = parse_battery_config(&pdev->dev, &info->config);
	if (ret) {
		dev_err(&pdev->dev, "Failed to parse battery config: %d\n", ret);
		return ret;
	}

	info->resistance_error_factor_scaled = (1 << 10);


	info->iio_chan = devm_iio_channel_get(&pdev->dev, "voltage");
	if (IS_ERR(info->iio_chan)) {
		ret = PTR_ERR(info->iio_chan);
		dev_err(&pdev->dev, "Failed to get IIO channel: %d\n", ret);
		return ret;
	}


	info->gpio_charging = devm_gpiod_get_optional(&pdev->dev,
			"charging", GPIOD_IN);
	if (IS_ERR(info->gpio_charging)) {
		ret = PTR_ERR(info->gpio_charging);
		dev_err(&pdev->dev, "Failed to get charging GPIO: %d\n", ret);
		return ret;
	}

	info->gpio_charged = devm_gpiod_get_optional(&pdev->dev,
			"charged", GPIOD_IN);
	if (IS_ERR(info->gpio_charged)) {
		ret = PTR_ERR(info->gpio_charged);
		dev_err(&pdev->dev, "Failed to get charged GPIO: %d\n", ret);
		return ret;
	}

	if (info->gpio_charging) {
		info->charging_irq = gpiod_to_irq(info->gpio_charging);
		if (info->charging_irq < 0) {
			ret = info->charging_irq;
			dev_err(&pdev->dev, "Failed to get charging IRQ: %d\n", ret);
			return ret;
		}

		ret = devm_request_threaded_irq(&pdev->dev, info->charging_irq,
				NULL, charging_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
				IRQF_ONESHOT,
				"battery_charging", info);
		if (ret) {
			dev_err(&pdev->dev, "Failed to request charging IRQ: %d\n", ret);
			return ret;
		}
	}

	if (info->gpio_charged) {
		info->charged_irq = gpiod_to_irq(info->gpio_charged);
		if (info->charged_irq < 0) {
			ret = info->charged_irq;
			dev_err(&pdev->dev, "Failed to get charged IRQ: %d\n", ret);
			return ret;
		}

		ret = devm_request_threaded_irq(&pdev->dev, info->charged_irq,
				NULL, charged_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING |
				IRQF_ONESHOT,
				"battery_charged", info);
		if (ret) {
			dev_err(&pdev->dev, "Failed to request charged IRQ: %d\n", ret);
			return ret;
		}
	}

	info->cal_kobj = kobject_create_and_add("battery_cal", &pdev->dev.kobj);
	if (!info->cal_kobj) {
		dev_err(&pdev->dev, "Failed to create calibration sysfs directory\n");
		return -ENOMEM;
	}
	ret = sysfs_create_group(info->cal_kobj, &calibration_attr_group);
	if (ret) {
		dev_err(&pdev->dev, "Failed to create calibration sysfs attributes: %d\n", ret);
		kobject_put(info->cal_kobj);
		return ret;
	}

	psy_cfg.drv_data = info;
	psy_cfg.of_node = pdev->dev.of_node;

	info->psy = power_supply_register(&pdev->dev, &battery_desc, &psy_cfg);
	if (IS_ERR(info->psy)) {
		ret = PTR_ERR(info->psy);
		dev_err(&pdev->dev, "Failed to register power supply: %d\n", ret);
		sysfs_remove_group(info->cal_kobj, &calibration_attr_group);
		kobject_put(info->cal_kobj);
		return ret;
	}

	info->current_full_voltage_mv = info->config.full_voltage_mv;
	info->current_full_voltage_adc = (info->current_full_voltage_mv * (1 << info->config.adc_resolution) *
				info->config.divider_denominator) /(info->config.vref_mv * info->config.divider_numerator);

	platform_set_drvdata(pdev, info);
	INIT_DELAYED_WORK(&info->update_work, update_battery_status);

	schedule_delayed_work(&info->update_work, 0);

	dev_info(&pdev->dev, "Battery driver success registered \n");
	return 0;
}

static int battery_remove(struct platform_device *pdev)
{
	struct battery_info *info = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&info->update_work);

	sysfs_remove_group(info->cal_kobj, &calibration_attr_group);
	kobject_put(info->cal_kobj);

	power_supply_unregister(info->psy);

	return 0;
}

static const struct of_device_id battery_of_match[] = {
	{ .compatible = "ingenic,battery-monitor", },
	{},
};
MODULE_DEVICE_TABLE(of, battery_of_match);

static struct platform_driver battery_driver = {
	.driver = {
		.name = "battery-monitor",
		.of_match_table = battery_of_match,
	},
	.probe = battery_probe,
	.remove = battery_remove,
};
module_platform_driver(battery_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ingenic");
MODULE_DESCRIPTION("Battery Monitor Driver with Voltage Divider and Calibration Support");
