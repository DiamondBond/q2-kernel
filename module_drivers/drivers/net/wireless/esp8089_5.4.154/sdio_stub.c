/* Copyright (c) 2008 -2014 Espressif System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 *  sdio stub code for RK
 */

#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/mmc/host.h>
#if defined(BCM5830x) || defined(MDM9x07)
#include <linux/mmc/sdhci.h>
#endif
#if defined(SSD222)
#include <linux/of_platform.h>
//#include "ms_sdmmc_lnx.h"
#endif

#if defined(BCM5830x)
#define AON 167
extern void iproc_sdhci_card_emulate(void);
#endif

#if defined(MDM9x07)
static struct mmc_host *wlan_mmc = NULL;
static char esp_init_conf_path[256];
#endif

#if defined(IMX6UL)
extern struct mmc_host *primary_wifi_host;
static struct mmc_host *wlan_mmc = NULL;
#endif

#if defined(SSD222)
static struct mmc_host *wlan_mmc = NULL;
#endif
extern int ingenic_sdio_wlan_esp8089_scan_card(void);
static int esp_reset_gpio = -1;
void sif_platform_card_remove(struct esp_sdio_ctrl *sif_sctrl, unsigned enable)
{
	if(sif_sctrl){
		sdio_claim_host(sif_sctrl->func);
		if(enable)
			sif_sctrl->func->card->host->caps &= ~MMC_CAP_NONREMOVABLE;
		else
			sif_sctrl->func->card->host->caps |= MMC_CAP_NONREMOVABLE;
		sdio_release_host(sif_sctrl->func);
   }
}
static int cnt = 0;
int sif_platform_rescan_card(unsigned insert)
{
#if defined(BCM5830x)
    printk("%s: %d\n", __func__, insert);
	mdelay(10);

    iproc_sdhci_card_emulate();

    return 1;
#elif defined(MDM9x07) || defined(IMX6UL) || defined(SSD222)
    int count = 100;

	printk("%s: %d\n", __func__, insert);
	mdelay(10);

	if(wlan_mmc) {
		wlan_mmc->rescan_disable = 0;
		wlan_mmc->rescan_entered = 0;
		mmc_detect_change(wlan_mmc, 0);
	} else {
		printk("%s  wlan_mmc is null,carddetect failed \n ",__func__);
	}

    do {
        if (wlan_mmc->card && mmc_card_present(wlan_mmc->card)) {
            printk("%s %d sdio card detected.\n", __func__, __LINE__);
            return 1;
        }
        msleep(10);
    } while (count--);

    printk("%s %d sdio card no exist.\n", __func__, __LINE__);
    return 0;
#else
    if(ingenic_sdio_wlan_esp8089_scan_card()) {
        printk("esp8089_power_onoff failed !!!\n");
        return 0;
    }
    return 1;
#endif
}

void sif_platform_target_poweroff(void)
{
    printk("ESP8089 poweroff %d\n", esp_reset_gpio);
    gpio_direction_output(esp_reset_gpio, 0);
#if defined(BCM5830x)
    gpio_direction_output(esp_reset_gpio, 0);
    __gpio_set_value(esp_reset_gpio, 0);
#elif defined(MDM9x07)
    gpio_direction_output(esp_reset_gpio, 1);
#elif defined(IMX6UL)
    gpio_direction_output(esp_reset_gpio, 0);
#elif defined(SSD222)
    gpio_direction_output(esp_reset_gpio, 0);
#else
#endif
    mdelay(100);
}

void sif_platform_target_poweron(void)
{
    printk("ESP8089 poweron %d\n", esp_reset_gpio);
    gpio_direction_output(esp_reset_gpio, 1);
#if defined(BCM5830x)
    gpio_direction_output(esp_reset_gpio, 1);
    __gpio_set_value(esp_reset_gpio, 1);
#elif defined(MDM9x07)
    gpio_direction_output(esp_reset_gpio, 0);
#elif defined(IMX6UL)
    gpio_direction_output(esp_reset_gpio, 1);
#elif defined(SSD222)
    gpio_direction_output(esp_reset_gpio, 1);
#else
#endif
    mdelay(100);
}

void sif_platform_reset_target(void)
{
	printk("ESP8089 reset %d\n", esp_reset_gpio);
	sif_platform_target_poweroff();
	sif_platform_target_poweron();
}

void sif_platform_target_speed(int high_speed)
{
}

void sif_platform_check_r1_ready(struct esp_pub *epub)
{
}

#ifdef ESP_ACK_INTERRUPT
extern void sdmmc_ack_interrupt(struct mmc_host *mmc);

void sif_platform_ack_interrupt(struct esp_pub *epub)
{
        struct esp_sdio_ctrl *sctrl = NULL;
        struct sdio_func *func = NULL;

	if (epub == NULL) {
        	ESSERT(epub != NULL);
		return;
	}
        sctrl = (struct esp_sdio_ctrl *)epub->sif;
        func = sctrl->func;
	if (func == NULL) {
        	ESSERT(func != NULL);
		return;
	}

        sdmmc_ack_interrupt(func->card->host);
}
#endif //ESP_ACK_INTERRUPT

void sif_platform_init_gpio(void)
{
#if defined(BCM5830x)
    esp_reset_gpio = AON + 1;
#elif defined(MDM9x07)
    struct sdhci_host *host;
    const __be32 *prop;
    char prop_name[32];
    const char *prop_str;
    int len;

    struct device *dev = bus_find_device_by_name(&platform_bus_type, NULL, "7824900.sdhci");
    struct platform_device * sdio1_device = to_platform_device(dev);
    struct device_node *np = dev->of_node;
    if (dev == NULL) {
        printk("%s  wlan_mmc is null, init failed \n ", __func__);
        return;
    }

    host = platform_get_drvdata(sdio1_device);
    wlan_mmc = (struct mmc_host *)container_of(host, struct mmc_host, private);
    BUG_ON(!wlan_mmc);  snprintf(prop_name, sizeof(prop_name), "sdio_wifi,wifi_name");
    if (of_property_read_string(np, prop_name, &prop_str)) {
        printk("%s error: no %s property\n", __func__, prop_name);
    } else {
        printk("%s: %s[%s]\n", __func__, prop_name, prop_str);
    }
    snprintf(prop_name, sizeof(prop_name), "sdio_wifi,wifi_pwd");
    prop = of_get_property(np, prop_name, &len);
    if (!prop || (len != (sizeof(__be32)))) {
        printk("%s error: %s %s property\n",  __func__, prop ? "invalid" : "no", prop_name);
    } else {
        esp_reset_gpio = be32_to_cpup(&prop[0]);
        printk("%s: %s[%d]\n", __func__, prop_name, esp_reset_gpio);
    }

    memset(esp_init_conf_path, 0, sizeof(esp_init_conf_path));
    snprintf(prop_name, sizeof(prop_name), "sdio_wifi,init_conf_path");
    if (of_property_read_string(np, prop_name, &prop_str)) {
        printk("%s error: no %s property\n", __func__, prop_name);
    } else {
        printk("%s: %s[%s]\n", __func__, prop_name, prop_str);
        strncpy(esp_init_conf_path, prop_str, sizeof(esp_init_conf_path)-1);
        request_init_conf();
    }
#elif defined(IMX6UL)
    wlan_mmc = primary_wifi_host;
    esp_reset_gpio = 114;
#elif defined(SSD222)
    int len;
    struct device_node *np = NULL;
    char prop_name[32];
    const char *prop_str;
    const __be32 *prop;
    struct ms_sdmmc_host *host;
    struct ms_sdmmc_slot *slot;

    np = of_find_compatible_node(NULL, NULL, "sstar,sdmmc");
    if (np == NULL) {
        printk("%s  device_node is null, init failed \n ", __func__);
        return;
    }

    struct platform_device* sdio1_device = of_find_device_by_node(np);
    host = platform_get_drvdata(sdio1_device);
    slot = host->sdmmc_slot[0];
    wlan_mmc = (struct mmc_host *)container_of((void*)slot, struct mmc_host, private);
    BUG_ON(!wlan_mmc);

    //esp_reset_gpio = 36;

    snprintf(prop_name, sizeof(prop_name), "sdio_wifi,wifi_name");
    if (of_property_read_string(np, prop_name, &prop_str)) {
        printk("%s error: no %s property\n", __func__, prop_name);
    } else {
        printk("%s: %s[%s]\n", __func__, prop_name, prop_str);
    }
    snprintf(prop_name, sizeof(prop_name), "sdio_wifi,wifi_pwd");
    prop = of_get_property(np, prop_name, &len);
    if (!prop || (len != (sizeof(__be32)))) {
        printk("%s error: %s %s property\n",  __func__, prop ? "invalid" : "no", prop_name);
    } else {
        esp_reset_gpio = be32_to_cpup(&prop[0]);
        printk("%s: %s[%d]\n", __func__, prop_name, esp_reset_gpio);
    }

#else
    esp_reset_gpio = -1;
#endif
    esp_reset_gpio = 77;
    gpio_request(esp_reset_gpio, "esp_reset");
}

