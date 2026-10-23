#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/sdio.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>

#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/rtc.h>

#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include<linux/regulator/consumer.h>

#if (defined(CONFIG_INGENIC_MMC_MMC0) || defined(CONFIG_INGENIC_MMC_MMC1))
#include "ingenic_mmc.h"
#else
#include "sdhci-ingenic.h"
#endif

struct wifi_data {
	int sdio_index;
	/* struct wake_lock                wifi_wake_lock; */
	/* struct regulator                *wifi_vbat; */
	/* struct regulator                *wifi_vddio; */
	uint         wifi_reset;
	uint         wifi_reset_flags;
	uint         wifi_irq;
	uint         wifi_irq_flags;
	uint         wifi_sdiosel;
	uint         wifi_sdiosel_flags;
	uint         wifi_pwron;
	uint         wifi_pwron_flags;
	struct pinctrl *pctrl;
	atomic_t rtc32k_ref;
	struct regulator *wlreg_on;
	struct regulator *wl_vccio;
	struct clk *clk;
};

struct wifi_data_atbm {
	int sdio_index;
	uint         wifi_reset;
	uint         wifi_reset_flags;
	uint         wifi_pwron;
	uint         wifi_pwron_flags;
};

#define WIFI_DATA_DEFAULT { \
	    .sdio_index = 0, \
	    .wifi_reset = -1, \
	    .wifi_reset_flags = -1, \
	    .wifi_irq = -1, \
	    .wifi_irq_flags = -1, \
	    .wifi_sdiosel = -1, \
	    .wifi_sdiosel_flags = -1, \
	    .wifi_pwron = -1, \
	    .wifi_pwron_flags = -1, \
	    .pctrl = NULL, \
	    .rtc32k_ref = ATOMIC_INIT(0), \
	    .wlreg_on = NULL, \
	    .wl_vccio = NULL, \
	    .clk = NULL \
}

#define RESET  0
#define NORMAL 1

struct wifi_data wifi_data = WIFI_DATA_DEFAULT;
struct wifi_data_atbm wifi_data_atbm;
static int rtc32k_init(struct device *dev, struct wifi_data *wdata)
{
	__maybe_unused struct rtc_device *rtc_pdev = NULL;
	__maybe_unused struct of_phandle_args phandle;
	atomic_set(&wdata->rtc32k_ref, 0);
	wdata->pctrl = NULL;
	wdata->clk = NULL;
#ifdef CONFIG_RTC_DRV_PCF8563

	rtc_pdev = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc_pdev) {
		printk("%s is loaded!!!\n", CONFIG_RTC_HCTOSYS_DEVICE);
	} else {
		printk("%s is not loaded!!!\n", CONFIG_RTC_HCTOSYS_DEVICE);
		return -1;
	}

	{
		phandle.np = of_find_compatible_node(NULL, NULL, "nxp,pcf8563");
		if (phandle.np) {
			wdata->clk = of_clk_get_from_provider(&phandle);
		}
	}
#else
	wdata->pctrl = devm_pinctrl_get(dev);
#endif
	return 0;
}

static int init_wifi_gpio(struct device *dev, uint gpio, uint flags, const char *name)
{
    if (gpio == (uint)-1) {
        printk("GPIO %s not used (value: %d)\n", name, gpio);
        return 0;
    }

    if (devm_gpio_request(dev, gpio, name)) {
        printk("ERROR: no %s pin available!!\n", name);
        return -EINVAL;
    }

    gpio_direction_output(gpio, !flags);
    return 0;
}

void rtc32k_enable(void)
{

	if (atomic_inc_return(&wifi_data.rtc32k_ref) == 1) {
		if(wifi_data.clk)
		{
			printk("@@ rtc.pcf8563 %s @@\n", __func__);
			clk_prepare_enable(wifi_data.clk);
		}
		if(wifi_data.pctrl)
		{
			struct pinctrl_state *state = NULL;
			struct pinctrl *p = wifi_data.pctrl;
			state = pinctrl_lookup_state(p, "enable");
			if (!IS_ERR_OR_NULL(state)) {
				pinctrl_select_state(p, state);
			}
		}
	}
}
EXPORT_SYMBOL(rtc32k_enable);
void rtc32k_disable(void)
{
	if (atomic_dec_return(&wifi_data.rtc32k_ref) == 0) {
		if(wifi_data.clk){
			printk("@@ rtc.pcf8563 %s @@\n", __func__);
			clk_disable_unprepare(wifi_data.clk);
		}
		if(wifi_data.pctrl) {
			struct pinctrl_state *state = NULL;
			struct pinctrl *p = wifi_data.pctrl;
			state = pinctrl_lookup_state(p, "disable");
			if (!IS_ERR_OR_NULL(state)) {
				pinctrl_select_state(p, state);
			}
		}
	}
}
EXPORT_SYMBOL(rtc32k_disable);
static const struct of_device_id wlan_ingenic_of_match[] = {
	{.compatible = "android,bcmdhd_wlan"},
	{.compatible = "ws_wlan"},
	{.compatible = "rtk,rtl8723ds_wlan"},
	{.compatible = "atbm,atbm_wlan"},
	{.compatible = "seekwave,ea6xxxx_wlan"},
	{.compatible = "rf,aic8800_wlan"},
	{.compatible = "zt,zt9101_wlan"},
	{.compatible = "nxp,wlan"},
	{},
};

int wlan_regulator_get(struct device *dev)
{
	int ret;

	wifi_data.wlreg_on= devm_regulator_get(dev, "wlreg_on");

	if (IS_ERR(wifi_data.wlreg_on)) {
		printk("wlreg_on is error %s, %s, %d\n", __FILE__, __func__, __LINE__);
		if (PTR_ERR(wifi_data.wlreg_on) == -EPROBE_DEFER) {
			return -EPROBE_DEFER;
		}
	}

	if (!IS_ERR(wifi_data.wlreg_on)) {
		ret = regulator_enable(wifi_data.wlreg_on);
		if (ret) {
			dev_err(dev, "wlreg_on enable failed\n");
			return ret;
		}
	} else {
		dev_err(dev, "wlreg_on is invalid\n");
	}

	wifi_data.wl_vccio = devm_regulator_get(dev, "wl_vccio");

	if (IS_ERR(wifi_data.wl_vccio)) {
		printk("wl_vccio is error %s, %s, %d\n", __FILE__, __func__, __LINE__);
		if (PTR_ERR(wifi_data.wl_vccio) == -EPROBE_DEFER) {
			return -EPROBE_DEFER;
		}
	}

	if (!IS_ERR(wifi_data.wl_vccio)) {
		ret = regulator_enable(wifi_data.wl_vccio);
		if (ret) {
			dev_err(dev, "wl_vccio enable failed\n");
			return ret;
		}
	} else {
		dev_err(dev, "wl_vccio is invalid\n");
	}

	return 0;
}
int wlan_regulator_put(struct device *dev)
{
	int ret;

	if (!IS_ERR(wifi_data.wlreg_on)) {
		ret = regulator_disable(wifi_data.wlreg_on);
		if (ret) {
			dev_err(dev, "wlreg_on enable failed\n");
			return ret;
		}
		devm_regulator_put(wifi_data.wlreg_on);
	} else {
		dev_err(dev, "wlreg_on is invalid\n");
	}

	if (!IS_ERR(wifi_data.wl_vccio)) {
		ret = regulator_disable(wifi_data.wl_vccio);
		if (ret) {
			dev_err(dev, "wl_vccio enable failed\n");
			return ret;
		}
		devm_regulator_put(wifi_data.wl_vccio);
	} else {
		dev_err(dev, "wl_vccio is invalid\n");
	}

	return 0;
}

int ingenic_sdio_wlan_init(struct device *dev, int index)
{
	struct device_node *np = dev->of_node, *cnp;
	unsigned int flags, gpio;
	int ret = 0;
	for_each_child_of_node(np, cnp) {
		if(of_device_is_compatible(cnp, "ws_wlan")) {
			printk("----ws_wlan!\n");
			wifi_data.wifi_reset = of_get_named_gpio_flags(cnp, "ingenic,sdio-reset", 0, &flags);
			wifi_data.wifi_reset_flags = flags;
		}
		if(of_device_is_compatible(cnp, "android,bcmdhd_wlan")) {
			printk("----android,bcmdhd_wlan!\n");
			wifi_data.wifi_reset = of_get_named_gpio_flags(cnp, "ingenic,sdio-reset", 0, &flags);
			wifi_data.wifi_reset_flags = flags;
			wifi_data.wifi_irq = of_get_named_gpio_flags(cnp, "ingenic,sdio-irq", 0, &flags);
			wifi_data.wifi_irq_flags = flags;
			wifi_data.wifi_pwron = of_get_named_gpio_flags(cnp, "ingenic,sdio-poweron", 0, &flags);
			wifi_data.wifi_pwron_flags = flags;
			gpio = wifi_data.wifi_pwron;
			if (gpio_is_valid(wifi_data.wifi_pwron)) {
				if (devm_gpio_request(dev, gpio, "wifi_power")) {
					printk("ERROR: wifi_power request fail!!\n");
					return -EINVAL;
				}
				gpio_direction_output(gpio, !wifi_data.wifi_pwron_flags);
			}else{
				dev_info(dev, "INFO: No valid GPIO for wifi_power (got %d)\n", wifi_data.wifi_pwron);
			}
		}
		if(of_device_is_compatible(cnp, "rtk,rtl8723ds_wlan")) {
			printk("----rtk,rtl8723ds_wlan\n");
			wifi_data.wifi_reset = of_get_named_gpio_flags(cnp, "ingenic,sdio-reset", 0, &flags);
			wifi_data.wifi_reset_flags = flags;
			wifi_data.wifi_irq = of_get_named_gpio_flags(cnp, "ingenic,sdio-irq", 0, &flags);
			wifi_data.wifi_irq_flags = flags;
		}
		if(of_device_is_compatible(cnp, "atbm,atbm_wlan")) {
			printk("----atbm,atbm_wlan\n");
			wifi_data.wifi_reset = of_get_named_gpio_flags(cnp, "ingenic,sdio-reset", 0, &flags);
			wifi_data.wifi_reset_flags = flags;
			wifi_data.wifi_irq = of_get_named_gpio_flags(cnp, "ingenic,sdio-irq", 0, &flags);
			wifi_data.wifi_irq_flags = flags;
		}
	        if(of_device_is_compatible(cnp, "seekwave,ea6xxxx_wlan")) {
			printk("----seekwave,ea6xxxx_wlan\n");
			wifi_data.wifi_sdiosel = of_get_named_gpio_flags(cnp, "ingenic,sdio-bootsel", 0, &flags);
			wifi_data.wifi_sdiosel_flags = flags;
			wifi_data.wifi_reset = of_get_named_gpio_flags(cnp, "ingenic,sdio-reset", 0, &flags);
			wifi_data.wifi_reset_flags = flags;
			wifi_data.wifi_irq = of_get_named_gpio_flags(cnp, "ingenic,sdio-irq", 0, &flags);
			wifi_data.wifi_irq_flags = flags;
		}
	        if(of_device_is_compatible(cnp, "seekwave,swt6621_wlan")) {
			printk("----seekwave,swt6621_wlan\n");
			wifi_data.wifi_reset = of_get_named_gpio_flags(cnp, "ingenic,sdio-reset", 0, &flags);
			wifi_data.wifi_reset_flags = flags;
		}
		if(of_device_is_compatible(cnp, "rf,aic8800_wlan")) {
			printk("-----aic8800_wlan---------\n");
			wifi_data.wifi_pwron = of_get_named_gpio_flags(cnp, "ingenic,wifi-power",0 ,&flags);
			wifi_data.wifi_pwron_flags = flags;
			gpio = wifi_data.wifi_pwron;
			if (devm_gpio_request(dev, gpio, "wifi_power")) {
				printk("ERROR: wifi_power request fail!!\n");
				return -EINVAL;
			}
			gpio_direction_output(gpio, !wifi_data.wifi_pwron_flags);


			wifi_data.wifi_reset = of_get_named_gpio_flags(cnp, "ingenic,sdio-reset", 0, &flags);
			wifi_data.wifi_reset_flags = flags;
			wifi_data.wifi_irq = of_get_named_gpio_flags(cnp, "ingenic,sdio-irq", 0, &flags);
			wifi_data.wifi_irq_flags = flags;
		}
		if(of_device_is_compatible(cnp, "zt,zt9101_wlan")) {
			printk("----zt,zt9101_wlan!\n");
			wifi_data.wifi_reset = of_get_named_gpio_flags(cnp, "ingenic,sdio-reset", 0, &flags);
			wifi_data.wifi_reset_flags = flags;
			wifi_data.wifi_irq = of_get_named_gpio_flags(cnp, "ingenic,sdio-irq", 0, &flags);
			wifi_data.wifi_irq_flags = flags;
		}
		if(of_device_is_compatible(cnp, "nxp,wlan")) {
			printk("----nxp,88W8987_wlan\n");
			wifi_data.wifi_reset = of_get_named_gpio_flags(cnp, "ingenic,sdio-reset", 0, &flags);
			wifi_data.wifi_reset_flags = flags;
			wifi_data.wifi_irq = of_get_named_gpio_flags(cnp, "ingenic,sdio-irq", 0, &flags);
			wifi_data.wifi_irq_flags = flags;
		}
		if(of_device_is_compatible(cnp, "wuqi,wq9201s_wlan")) {
			printk("----wuqi,wq9201s_wlan\n");
			  wifi_data.wifi_reset = of_get_named_gpio_flags(cnp, "ingenic,sdio-reset", 0, &flags);
			  wifi_data.wifi_reset_flags = flags;
		}
	}

	wlan_regulator_get(dev);

	ret = rtc32k_init(dev, &wifi_data);
	if(ret) {
		dev_err(dev, "Failed to init rtc32k clock!\n");
		return ret;
	}

	ret = init_wifi_gpio(dev, wifi_data.wifi_sdiosel, wifi_data.wifi_sdiosel_flags, "wifi_sdiosel");
	if (ret)
		return ret;

	ret = init_wifi_gpio(dev, wifi_data.wifi_reset, wifi_data.wifi_reset_flags, "wifi_reset");
	if (ret)
		return ret;


	wifi_data.sdio_index = index;

	return 0;
}
EXPORT_SYMBOL(ingenic_sdio_wlan_init);

int ingenic_sdio_wlan_deinit(struct device *dev)
{
	wlan_regulator_put(dev);
	return 0;
}
EXPORT_SYMBOL(ingenic_sdio_wlan_deinit);

int ingenic_sdio_wlan_get_irq(unsigned long *flag)
{
	*flag = wifi_data.wifi_irq_flags;
	return wifi_data.wifi_irq;
}
EXPORT_SYMBOL(ingenic_sdio_wlan_get_irq);
int ingenic_sdio_wlan_get_regon(unsigned long *flag)
{
	*flag = wifi_data.wifi_reset_flags;
	return wifi_data.wifi_reset;
}
EXPORT_SYMBOL(ingenic_sdio_wlan_get_regon);
int ingenic_sdio_wlan_power_onoff(int onoff, int flag)
{
	int err = 0;
	int reset = wifi_data.wifi_reset;
	if(!reset)
		return -EINVAL;
	if(onoff) {
		printk("reset %d wlan power on:%d\n", reset, flag);
		if (!IS_ERR(wifi_data.wlreg_on)) {
			err = regulator_enable(wifi_data.wlreg_on);
			if (err) {
				printk("wifi_data.wlreg_on enable failed\n");
				return err;
			}
		}
		if (!IS_ERR(wifi_data.wl_vccio)) {
			err = regulator_enable(wifi_data.wl_vccio);
			if (err) {
				printk("wifi_data.wl_vccio enable failed\n");
				return err;
			}
		}
		rtc32k_enable();
		switch(flag) {
			case RESET:
				ingenic_mmc_clk_ctrl(wifi_data.sdio_index, 1);
				gpio_set_value(reset, 0);
				msleep(10);
				gpio_set_value(reset, 1);
				break;
			case NORMAL:
				gpio_set_value(reset, 1);
				ingenic_mmc_manual_detect(wifi_data.sdio_index, 1);
				break;
		}
	} else {
		printk("wlan power off:%d\n", flag);
		switch(flag) {
			case RESET:
				gpio_set_value(reset, 0);
				break;
			case NORMAL:
				gpio_set_value(reset, 0);
				break;
		}
		rtc32k_disable();
		if (!IS_ERR(wifi_data.wlreg_on)) {
			err = regulator_disable(wifi_data.wlreg_on);
			if (err) {
				printk("wifi_data.wlreg_on disable failed\n");
				return err;
			}
		}
		if (!IS_ERR(wifi_data.wl_vccio)) {
			err = regulator_disable(wifi_data.wl_vccio);
			if (err) {
				printk("wifi_data.wl_vccio disable failed\n");
				return err;
			}
		}
	}
	if (err < 0) {
		printk("%s: regulator enable/disable failed", __FUNCTION__);
		return -1;
	}
	return 0;

}
EXPORT_SYMBOL(ingenic_sdio_wlan_power_onoff);
int ingenic_sdio_wlan_seekwave_power_onoff(int onoff, int flag)
{
	int err = 0;
	int reset = wifi_data.wifi_reset;
	if(!reset)
		return -EINVAL;
	if(onoff) {
		printk("reset %d wlan power on:%d\n", reset, flag);
		if (!IS_ERR(wifi_data.wlreg_on)) {
			err = regulator_enable(wifi_data.wlreg_on);
			if (err) {
				printk("wifi_data.wlreg_on enable failed\n");
				return err;
			}
		}
		if (!IS_ERR(wifi_data.wl_vccio)) {
			err = regulator_enable(wifi_data.wl_vccio);
			if (err) {
				printk("wifi_data.wl_vccio enable failed\n");
				return err;
			}
		}
		rtc32k_enable();
		switch(flag) {
			case RESET:
				ingenic_mmc_clk_ctrl(wifi_data.sdio_index, 1);
				gpio_set_value(reset, 0);
				msleep(50);
				gpio_set_value(reset, 1);
				break;
			case NORMAL:
				gpio_set_value(reset, 1);
				ingenic_mmc_manual_detect(wifi_data.sdio_index, 1);
				break;
		}
	} else {
		printk("wlan power off:%d\n", flag);
		switch(flag) {
			case RESET:
				gpio_set_value(reset, 0);
				break;
			case NORMAL:
				gpio_set_value(reset, 0);
				break;
		}
		rtc32k_disable();
		if (!IS_ERR(wifi_data.wlreg_on)) {
			err = regulator_disable(wifi_data.wlreg_on);
			if (err) {
				printk("wifi_data.wlreg_on disable failed\n");
				return err;
			}
		}
		if (!IS_ERR(wifi_data.wl_vccio)) {
			err = regulator_disable(wifi_data.wl_vccio);
			if (err) {
				printk("wifi_data.wl_vccio disable failed\n");
				return err;
			}
		}
	}
	if (err < 0) {
		printk("%s: regulator enable/disable failed", __FUNCTION__);
		return -1;
	}
	return 0;

}
EXPORT_SYMBOL(ingenic_sdio_wlan_seekwave_power_onoff);

int ingenic_sdio_wlan_ws73_power_onoff(int onoff, int flag)
{
	int err = 0;
	int reset = wifi_data.wifi_reset;
	if(!reset)
		return -EINVAL;
	if(onoff) {
		switch(flag) {
			case RESET:
				ingenic_mmc_clk_ctrl(wifi_data.sdio_index, 1);
				gpio_set_value(reset, 0);
				msleep(50);
				gpio_set_value(reset, 1);
				break;
			case NORMAL:
				gpio_set_value(reset, 1);
				msleep(50);
				ingenic_mmc_manual_detect(wifi_data.sdio_index, 1);
				break;
		}
	} else {
		switch(flag) {
			case RESET:
				gpio_set_value(reset, 0);
				break;
			case NORMAL:
				gpio_set_value(reset, 0);
				break;
		}
		rtc32k_disable();
	}
	if (err < 0) {
		printk("%s: regulator enable/disable failed", __FUNCTION__);
		return -1;
	}
	return 0;

}
EXPORT_SYMBOL(ingenic_sdio_wlan_ws73_power_onoff);

int ingenic_sdio_wlan_atbm_power_onoff(int onoff, int flag)
{
	int err = 0;
	int reset = wifi_data_atbm.wifi_reset;
	int pwron = wifi_data_atbm.wifi_pwron;

	if(onoff) {
		switch(flag) {
			case RESET:
				ingenic_mmc_clk_ctrl(wifi_data_atbm.sdio_index, 1);
				gpio_set_value(pwron, 1);
				msleep(10);
				gpio_set_value(reset, 0);
				msleep(10);
				gpio_set_value(reset, 1);
				break;
			case NORMAL:
				gpio_set_value(pwron, 1);
				msleep(10);
				gpio_set_value(reset, 1);
				msleep(10);
				ingenic_mmc_manual_detect(wifi_data_atbm.sdio_index, 1);
				break;
		}
	} else {
		printk("wlan power off:%d\n", flag);
		switch(flag) {
			case RESET:
				gpio_set_value(reset, 0);
				gpio_set_value(pwron, 0);
				break;
			case NORMAL:
				gpio_set_value(reset, 0);
				gpio_set_value(pwron, 0);
				break;
		}
	}
	if (err < 0) {
		printk("%s: regulator enable/disable failed", __FUNCTION__);
		return -1;
	}
	return 0;

}
EXPORT_SYMBOL(ingenic_sdio_wlan_atbm_power_onoff);

int ingenic_sdio_wlan_esp8089_scan_card(void)
{
	int ret = 0;
	ret = ingenic_mmc_manual_detect(wifi_data_atbm.sdio_index, 1);
	if(ret < 0)
		printk("manual detect failed !!!\n");
	return ret;

}
EXPORT_SYMBOL(ingenic_sdio_wlan_esp8089_scan_card);

int ingenic_sdio_wlan_wuqi_scan_card(void)
{
	int ret = 0;
	int reset = wifi_data.wifi_reset;
	gpio_set_value(reset, 1);
	msleep(10);
	ret = ingenic_mmc_manual_detect(wifi_data.sdio_index, 1);
	if(ret < 0)
		printk("manual detect failed !!!\n");
	return ret;

}
EXPORT_SYMBOL(ingenic_sdio_wlan_wuqi_scan_card);
