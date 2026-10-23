/** @file woal_pcie.c
 *
 *  @brief This file contains PCIE IF (interface) module
 *  related functions.
 *
 * Copyright (C) 2016-2023, WuQi Ltd.
 *
 * This software file (the "File") is distributed by WuQi Ltd.
 * Under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 *
 */

#include <linux/version.h>
#include <linux/of_gpio.h>
#ifdef CONFIG_RK3588_ENABLE_WAKEUP_OOB
//#include <linux/wakelock.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)
#include <linux/wakelock.h>
#else
#include <linux/pm_wakeup.h>
#endif

#endif
#include "plat_rk3588.h"
#include "wq_log.h"
#include "fw_log.h"

#ifdef CONFIG_RK3588_ENABLE_WAKEUP_OOB
/* dts node */
#define PALT_DT_COMPAT_ENTRY "wuqi,wq9201s_wlan"
#define GPIO_WL_REG_ON_PROPNAME "ingenic,sdio-reset"
#define GPIO_WL_HOST_WAKE_PROPNAME "ingenic,sdio-irq"
#define OOB_WAKE_LOCK_TOMEOUT 5 * HZ
struct wakeup_source *oob_wakelock;

static irqreturn_t wlan_oob_irq_isr(int irq, void *data)
{
	struct wq_core *wq_core = (struct wq_core *)data;

	disable_irq_nosync(wq_core->oob_irq_num);
	wq_core->oob_irq_wake_enabled = 0x00;
	WQ_DBG(DM_TRBUS, DL_INF, "%s:WLAN WAKE HOST IRQ ISR\n", __FUNCTION__);

	/* call thread irq */
	return IRQ_WAKE_THREAD;
}

static irqreturn_t wlan_oob_irq(int irq, void *data)
{
	u64 diff = 0x00;
	struct wq_core *wq_core = (struct wq_core *)data;

	/* get current time */
	diff = local_clock() - wq_core->last_oob_irq_isr_time;
	WQ_DBG(DM_TRBUS, DL_INF, "%s diff: %llu\n", __FUNCTION__, diff);

	/* debounce */
	if (diff < (1000 * 1000)) { /* 1ms */
		enable_irq(wq_core->oob_irq_num);
		wq_core->oob_irq_wake_enabled = 0x01;
		WQ_DBG(DM_TRBUS, DL_INF, "%s:debounce!!!\n", __FUNCTION__);
		return IRQ_HANDLED;
	}

	WQ_DBG(DM_TRBUS, DL_INF, "%s:WLAN WAKE HOST IRQ Thread\n",
	       __FUNCTION__);

	/* wakeup host */
	//wake_lock_timeout(&wq_core->oob_wakelock, OOB_WAKE_LOCK_TOMEOUT);
	__pm_wakeup_event(oob_wakelock, OOB_WAKE_LOCK_TOMEOUT);

	/* enable irq */
	enable_irq(wq_core->oob_irq_num);
	wq_core->oob_irq_wake_enabled = 0x01;
	wq_core->last_oob_irq_isr_time = local_clock();
	return IRQ_HANDLED;
}

int wq_core_register_oob_wakeup_host(struct wq_core *wq_core)
{
	char wlan_node[32];
	struct device_node *root_node = NULL;
	int err = 0;
//	int gpio_wl_reg_on = -1;
	int gpio_wl_host_wake = -1;
	int host_oob_irq = -1;
	u32 host_oob_irq_flags = 0;

	/* get gpio by device tree node */
	strcpy(wlan_node, PALT_DT_COMPAT_ENTRY);
	root_node = of_find_compatible_node(NULL, NULL, wlan_node);

	WQ_DBG(DM_TRBUS, DL_INF, " ======== Get GPIO from DTS(%s) ========\n",
	       wlan_node);
	if (root_node) {
//		gpio_wl_reg_on = of_get_named_gpio(root_node,
//						   GPIO_WL_REG_ON_PROPNAME, 0);
		gpio_wl_host_wake = of_get_named_gpio(
			root_node, GPIO_WL_HOST_WAKE_PROPNAME, 0);
	} else {
		WQ_DBG(DM_TRBUS, DL_INF,
		       " ======== Get GPIO from DTS(%s) failed ========\n",
		       wlan_node);
		return -1;
	}

	/* config the reg on gpio */
//	if (gpio_wl_reg_on >= 0) {
//		err = gpio_request(
//			gpio_wl_reg_on,
//			"wl_reg_on"); /* cat /sys/kernel/debug/gpio */
//		if (err < 0) {
//			WQ_DBG(DM_TRBUS, DL_ERR,
//			       "%s: gpio_request(%d) for WL_REG_ON failed %d\n",
//			       __FUNCTION__, gpio_wl_reg_on, err);
//			return -2;
//		}
//		/* set wlan wakeup gpio as input */
//		WQ_DBG(DM_TRBUS, DL_INF,
//		       " ======== PULL WL_REG_ON(%d) HIGH! ========\n",
//		       gpio_wl_reg_on);
//		err = gpio_direction_output(gpio_wl_reg_on, 1);
//		if (err) {
//			WQ_DBG(DM_TRBUS, DL_ERR,
//			       "%s: WL_REG_ON didn't output high\n",
//			       __FUNCTION__);
//			return -2;
//		}
//		wq_core->gpio_wl_reg_on = gpio_wl_reg_on;
//	}

	/* config the wlan gpio */
	if (gpio_wl_host_wake >= 0) {
		err = gpio_request(gpio_wl_host_wake, "wl_wake_host");
		if (err < 0) {
			WQ_DBG(DM_TRBUS, DL_ERR,
			       "%s: gpio_request(%d) for WL_HOST_WAKE failed %d\n",
			       __FUNCTION__, gpio_wl_host_wake, err);
			return -3;
		}
		/* set wlan wakeup gpio as input */
		err = gpio_direction_input(gpio_wl_host_wake);
		if (err < 0) {
			WQ_DBG(DM_TRBUS, DL_ERR,
			       "%s: gpio_direction_input(%d) for WL_HOST_WAKE failed %d\n",
			       __FUNCTION__, gpio_wl_host_wake, err);
			gpio_free(gpio_wl_host_wake);
			return -3;
		}

		/* map gpio to irq */
		host_oob_irq = gpio_to_irq(gpio_wl_host_wake);
		if (host_oob_irq < 0) {
			WQ_DBG(DM_TRBUS, DL_ERR,
			       "%s: gpio_to_irq(%d) for WL_HOST_WAKE failed %d\n",
			       __FUNCTION__, gpio_wl_host_wake, host_oob_irq);
			gpio_free(gpio_wl_host_wake);
			return -4;
		}

#ifdef EN_DEBOUNCE
		/* debounce */
		err = gpio_set_debounce(gpio_wl_host_wake, 200);
		if (err < 0) {
			WQ_DBG(DM_TRBUS, DL_ERR,
			       "%s: gpio_set_debounce(%d) for WL_HOST_WAKE failed %d\n",
			       __FUNCTION__, gpio_wl_host_wake, err);
			return -5;
		}
#endif
		wq_core->gpio_wl_host_wake = gpio_wl_host_wake;
	}

	/* reg wakeup irq as wakeup */
	host_oob_irq_flags = IRQF_TRIGGER_RISING | IRQF_ONESHOT;
	host_oob_irq_flags &= IRQF_TRIGGER_MASK;
	wq_core->oob_irq_num = host_oob_irq;
	wq_core->oob_irq_flags = host_oob_irq_flags;
	WQ_DBG(DM_TRBUS, DL_INF,
	       "%s: wl_wake_host=%d, oob_irq=%d, oob_irq_flags=0x%x\n",
	       __FUNCTION__, gpio_wl_host_wake, host_oob_irq,
	       host_oob_irq_flags);
//	WQ_DBG(DM_TRBUS, DL_INF, "%s: wl_reg_on=%d\n", __FUNCTION__,
//	       gpio_wl_reg_on);

	/* reg wakeup irq handler */
	err = request_threaded_irq(wq_core->oob_irq_num, wlan_oob_irq_isr,
				   wlan_oob_irq, wq_core->oob_irq_flags,
				   "wq_core_host_wake", wq_core);

	if (err) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: request_irq failed with %d\n",
		       __FUNCTION__, err);
		free_irq(wq_core->oob_irq_num, wq_core);
		return -5;
	}

	/* gpio irq as wakeup src */
	WQ_DBG(DM_TRBUS, DL_INF, "%s: enable_irq_wake\n", __FUNCTION__);
	err = enable_irq_wake(wq_core->oob_irq_num);
	if (!err) {
		wq_core->oob_irq_wake_enabled = 0x01;
	} else {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: enable_irq_wake failed with %d\n",
		       __FUNCTION__, err);
		wq_core->oob_irq_wake_enabled = 0x00;
		return -6;
	}

	wq_core->oob_irq_registered = 0x01;
//	wake_lock_init(&wq_core->oob_wakelock, WAKE_LOCK_SUSPEND, "wlan_oob_wakelock");
	oob_wakelock = wakeup_source_register(NULL,"wlan_oob_wakelock");

	WQ_DBG(DM_TRBUS, DL_INF,
	       "%s: system config wl_reg_on and wl_wake_host success!\n",
	       __FUNCTION__);
	return 0;
}
WQ_PLAT_RK3588_API(wq_core_register_oob_wakeup_host);

void wq_core_unregister_oob_wakeup_host(struct wq_core *wq_core)
{
	WQ_DBG(DM_TRBUS, DL_INF,
	       "%s: system unregister wl_reg_on and wl_wake_host function\n",
	       __FUNCTION__);

	free_irq(wq_core->oob_irq_num, wq_core);
	/* disable gpio wake */
	disable_irq_wake(wq_core->oob_irq_num);

	/* free gpio */
//	gpio_free(wq_core->gpio_wl_reg_on);
	gpio_free(wq_core->gpio_wl_host_wake);
//	wake_lock_destroy(&wq_core->oob_wakelock);
	wakeup_source_unregister(oob_wakelock);

	wq_core->oob_irq_num = 0x00;
	wq_core->oob_irq_flags = 0x00;
//	wq_core->gpio_wl_reg_on = 0x00;
	wq_core->gpio_wl_host_wake = 0x00;
	wq_core->oob_irq_wake_enabled = false;
	wq_core->oob_irq_registered = false;
	wq_core->last_oob_irq_isr_time = 0x00;
}
WQ_PLAT_RK3588_API(wq_core_unregister_oob_wakeup_host);
#endif
