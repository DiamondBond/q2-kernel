/** @file woal_sdio.c
 *
 *  @brief This file contains SDIO MMC IF (interface) module
 *  related functions.
 *
 * Copyright (C) 2016-2022, WuQi Ltd.
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
/****************************************************
Change log:
	10/28/2022: Initial creation
****************************************************/

extern int ingenic_sdio_wlan_wuqi_scan_card(void);
#define WQ_LOG_DM DM_TRBUS

#include <linux/module.h>
#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sd.h>

#include "sdio.h"
#include "wq_log.h"
#include "bmi_core.h"
#include "sdio.h"
#include "fw_log.h"
#include "wq_profiling.h"

#include "../../../platform/plat_rk3588.h"
#ifdef CONFIG_SPACEMIT_K1_SDIO_WAR
#include "plat_spacemit_k1.h"
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
#define MMC_CARD_REMOVED (1 << 4) /* card has been removed */
#define mmc_card_removed(c) ((c) && ((c)->state & MMC_CARD_REMOVED))
#endif

#define SDIO_FLOW_CTRL_THRESHOLD_STOP 200
#define SDIO_FLOW_CTRL_THRESHOLD_RESTART 100
#define WQ_SDIO_AUTUSUSPEND_DELAY_MS 1000

#ifndef MMC_CMD_RETRIES
#define MMC_CMD_RETRIES 3
#endif

#define PM_ACK_BIT(func_num) (BIT((func_num)-1))

#define SDIO_PM_MSGS                                                           \
	X(SDIO_PM_MSG_RPM_SUSPEND)                                             \
	X(SDIO_PM_MSG_SYS_SUSPEND)                                             \
	X(SDIO_PM_MSG_RPM_RESUME)                                              \
	X(SDIO_PM_MSG_SYS_RESUME)                                              \
	X(SDIO_PM_MSG_ACK_CLR)                                                 \
	X(SDIO_PM_MSG_CHIP_SLEEP_FORBID)                                       \
	X(SDIO_PM_MSG_CHIP_SLEEP_FORBID_CLR)                                   \
	X(SDIO_PM_MSG_CHIP_SLEEP_ALLOW)

//generate enums
#define X(VALUE) VALUE,
typedef enum { SDIO_PM_MSGS MSG_COUNT } sdio_pm_msg_id;
#undef X

// generate names of enum
#define X(VALUE) #VALUE,
const char *pm_msg_names[] = { SDIO_PM_MSGS };
#undef X

static const char *get_pm_msg_name(sdio_pm_msg_id msg)
{
	if (msg >= 0 && msg < MSG_COUNT) {
		return pm_msg_names[msg];
	}
	return "UNKNOWN";
}

char *fw_dtop_sdio = NULL;
module_param(fw_dtop_sdio, charp, 0);
MODULE_PARM_DESC(fw_dtop_sdio, "dtop firmware name for sdio, default: null.");

char *fw_wifi_sdio = NULL;
module_param(fw_wifi_sdio, charp, 0);
MODULE_PARM_DESC(fw_wifi_sdio, "wifi firmware name for sdio, default: null.");

int fpga_platform = 0;
module_param(fpga_platform, int, 0);
MODULE_PARM_DESC(fpga_platform, "sdio smoke need enable: fpga_platform = 0");

extern int sdio_ut_mode;

static void wq_sdio_dump_cccr_regs(struct sdio_func *func)
{
#define SDIO_CCCR_REG_ITEM(_reg)                                               \
	{                                                                      \
		SDIO_CCCR_##_reg, #_reg                                        \
	}
	static const struct {
		u8 addr;
		const char *name;
	} cccrs[] = {
		SDIO_CCCR_REG_ITEM(IO_ENABLE),
		SDIO_CCCR_REG_ITEM(IO_READY),
		SDIO_CCCR_REG_ITEM(INT_ENABLE),
		SDIO_CCCR_REG_ITEM(INT_PENDING),
		SDIO_CCCR_REG_ITEM(WUQI_ACCU_LEN_INT),
		SDIO_CCCR_REG_ITEM(WUQI_BUF_AVAIL_INT),
	};
#undef SDIO_CCCR_REG_ITEM

	unsigned i;

	/* avoid "scheduling while atomic" */
	if (in_atomic())
		return;

	WQ_DBG(DM_TRBUS, DL_INF,
	       "=== SDIO Card Common Control Registers ===\n");

	sdio_claim_host(func);
	for (i = 0; i < ARRAY_SIZE(cccrs); i++) {
		int ret;
		u8 val = sdio_f0_readb(func, cccrs[i].addr, &ret);

		if (ret == 0) {
			WQ_DBG(DM_TRBUS, DL_INF, "CCCR[0x%02x] %s: 0x%x\n",
			       cccrs[i].addr, cccrs[i].name, val);
		} else {
			WQ_DBG(DM_TRBUS, DL_ERR,
			       "CCCR[0x%02x] %s read failed (%d)!\n",
			       cccrs[i].addr, cccrs[i].name, ret);
		}
	}
	sdio_release_host(func);
}

static bool runtime_inited;

static int wq_sdio_send_pm_message(struct wq_func *wq_func,
				   sdio_pm_msg_id pm_msg)
{
	int ret = 0;
	u8 val = 0;
	u8 tmp = 0;
	u8 func_num = 0;

	struct sdio_func *func;
	if ((!wq_func) || (wq_func->func == NULL)) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: wq_sdio or func is NULL!\n",
		       __func__);
		return -EINVAL;
	}
	func = wq_func->func;
	func_num = wq_func->func_num;
	val = BIT(func_num - 1);
	switch (pm_msg) {
	case SDIO_PM_MSG_RPM_SUSPEND:
	case SDIO_PM_MSG_CHIP_SLEEP_ALLOW:
		ret = wq_sdio_cmd52_write(func, SDIO_CCCR_SUSPEND_INT, val);
		break;
	case SDIO_PM_MSG_SYS_SUSPEND:
		wq_sdio_cmd52_read(func, SDIO_CCCR_WUQI_RCV2, &tmp);
		tmp |= (1 << 4);
		wq_sdio_cmd52_write(func, SDIO_CCCR_WUQI_RCV2, tmp);
		ret = wq_sdio_cmd52_write(func, SDIO_CCCR_SUSPEND_INT, val);
		break;
	case SDIO_PM_MSG_RPM_RESUME:
	case SDIO_PM_MSG_CHIP_SLEEP_FORBID:
		ret = wq_sdio_cmd52_write(func, SDIO_CCCR_RESUME_INT, val);
		break;
	case SDIO_PM_MSG_SYS_RESUME:
		wq_sdio_cmd52_read(func, SDIO_CCCR_WUQI_RCV2, &tmp);
		tmp &= 0x0F;
		wq_sdio_cmd52_write(func, SDIO_CCCR_WUQI_RCV2, tmp);
		ret = wq_sdio_cmd52_write(func, SDIO_CCCR_RESUME_INT, val);
		break;
	case SDIO_PM_MSG_ACK_CLR:
		ret = wq_sdio_cmd52_write(func, SDIO_CCCR_RESUME_ACK_CLR_INT,
					  val);
		break;
	default:
		break;
	}
	WQ_DBG(DM_TRBUS, DL_WRN, "sdio_pm_msg %s sent\n", get_pm_msg_name(pm_msg));
	BUG_ON(ret);

	return ret;
}

#define MAX_POLL_TIMES 2000
static int wq_sdio_wait_pm_ack(struct wq_func *wq_func, sdio_pm_msg_id pm_msg)
{
	int ret = 0, times = 0;
	u8 ack = 0;
	while (times++ < MAX_POLL_TIMES) {
		udelay(50);
		ret = wq_sdio_cmd52_read(wq_func->func,
					 SDIO_CCCR_RESUME_ACK_INT, &ack);
		if (ack == PM_ACK_BIT(wq_func->func_num)) {
			wq_sdio_send_pm_message(wq_func, SDIO_PM_MSG_ACK_CLR);
			break;
		}
	}
	BUG_ON(!ack);
	return ret;
}

#if defined(MMC_PM_KEEP_POWER)
static __maybe_unused int wq_sdio_pm_prepare(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct wq_sdio *wq_sdio = sdio_get_drvdata(func);
	struct wq_func *wlan_func = &wq_sdio->wlan;

	// only wlan func do suspend
	if (func != wq_sdio->wlan.func) {
		WQ_DBG(DM_TRBUS, DL_WRN, "not wlan func, do not need prepare\n");
		return 0;
	}

	// If runtime suspend, recovery to active
	if (!pm_runtime_active(dev)) {
		WQ_DBG(DM_TRBUS, DL_WRN,
		       "sdio rpm not active at system suspend\n");
		atomic_set(&wq_sdio->pm_status, SDIO_RESUMING);
		wq_sdio_send_pm_message(wlan_func, SDIO_PM_MSG_RPM_RESUME);
		wq_sdio_wait_pm_ack(wlan_func, SDIO_PM_MSG_RPM_RESUME);
		atomic_set(&wq_sdio->pm_status, SDIO_ACTIVE);
		pm_runtime_set_active(dev);
		pm_runtime_mark_last_busy(dev);
	}

	return 0;
}

static __maybe_unused int wq_sdio_pm_suspend(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct wq_sdio *wq_sdio = sdio_get_drvdata(func);
	struct wq_func *wlan_func = &wq_sdio->wlan;
	struct rwnx_hw *rwnx_hw;
	struct rwnx_vif *vif;
	mmc_pm_flag_t pm_flags = 0;
	int ret = 0;
	u8 func_num = 0;
	func_num = wlan_func->func_num;
	WQ_DBG(DM_TRBUS, DL_WRN, "record func_num:%d, dev func_num:%d\n", func_num, func->num);

	// only wlan func do suspend
	if (func != wq_sdio->wlan.func) {
		WQ_DBG(DM_TRBUS, DL_WRN, "not wlan func, do not need suspend\n");
		return 0;
	}

	func_num = wq_sdio->wlan.func_num;
	WQ_DBG(DM_TRBUS, DL_WRN, "record func_num:%d, dev func_num:%d\n", func_num, func->num);

	WQ_DBG(DM_TRBUS, DL_WRN, "sys suspend enter\n");

	rwnx_hw = wiphy_priv(wq_sdio->core.hw->wiphy);

	// check host pm caps
	pm_flags = sdio_get_host_pm_caps(func);
	WQ_DBG(DM_TRBUS, DL_WRN, "%s: suspend: PM flags = 0x%x\n",
	       sdio_func_id(func), pm_flags);

	if (pm_flags & MMC_PM_KEEP_POWER) {
		if (sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER)) {
			WQ_DBG(DM_TRBUS, DL_ERR, "set pm_flags failed");
			return -EINVAL;
		}
	} else {
		return -ENOTSUPP;
	}

	wq_core_deep_suspend_set(&wq_sdio->core, 1);

	// Only sta mode supports suspend
	list_for_each_entry (vif, &rwnx_hw->vifs, list) {
		if (RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_STATION && vif->up) {
			WQ_DBG(DM_TRBUS, DL_WRN,
			       "not support mode(%d), failed to suspend\n",
			       RWNX_VIF_TYPE(vif));
			wq_core_deep_suspend_set(&wq_sdio->core, 0);
			return -ENOTSUPP;
		}
	}

	// check pm_status
	if (atomic_read(&wq_sdio->pm_status) != SDIO_ACTIVE) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: sdio status:%d\n", __func__, atomic_read(&wq_sdio->pm_status));
		wq_core_deep_suspend_set(&wq_sdio->core, 0);
		return -EBUSY;
	}

	// ask wlan to suspend
	ret = wq_wlan_suspend(&wq_sdio->core);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_WRN, "failed to suspend wlan: %d\n", ret);
		wq_core_deep_suspend_set(&wq_sdio->core, 0);
		return ret;
	}
#ifdef CONFIG_ROCK_CHIP_WLAN_RECOVERY
	if (!fpga_platform)
		cancel_delayed_work_sync(&wq_sdio->detect_work);
#endif

	// ask driver to suspend
	atomic_set(&wq_sdio->pm_status, SDIO_SYS_SUSPENDING);
	wq_sdio_send_pm_message(wlan_func, SDIO_PM_MSG_SYS_SUSPEND);
	wq_sdio_wait_pm_ack(wlan_func, SDIO_PM_MSG_SYS_SUSPEND);

	atomic_set(&wq_sdio->pm_status, SDIO_SYS_SUSPENDED);
	WQ_DBG(DM_TRBUS, DL_WRN, "sys suspend end\n");

	return ret;
}

static __maybe_unused int wq_sdio_pm_resume(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct wq_sdio *wq_sdio = sdio_get_drvdata(func);
	struct wq_func *wlan_func = &wq_sdio->wlan;
	mmc_pm_flag_t pm_flags = 0;
	int ret = 0;
	u8 func_num = 0;
	func_num = wlan_func->func_num;

	// only wlan func do suspend
	if (func != wq_sdio->wlan.func) {
		WQ_DBG(DM_TRBUS, DL_WRN, "not wlan func, do not need resume\n");
		return 0;
	}

	func_num = wq_sdio->wlan.func_num;

	WQ_DBG(DM_TRBUS, DL_WRN, "sys resume enter\n");

	pm_flags = sdio_get_host_pm_caps(func);
	WQ_DBG(DM_TRBUS, DL_WRN, "%s: resume: PM flags = 0x%x\n",
	       sdio_func_id(func), pm_flags);

	// ask driver to active
	atomic_set(&wq_sdio->pm_status, SDIO_RESUMING);
	wq_sdio_send_pm_message(wlan_func, SDIO_PM_MSG_SYS_RESUME);
	wq_sdio_wait_pm_ack(wlan_func, SDIO_PM_MSG_SYS_RESUME);
	atomic_set(&wq_sdio->pm_status, SDIO_ACTIVE);

	wq_core_deep_suspend_set(&wq_sdio->core, 0);

	ret = wq_wlan_resume(&wq_sdio->core);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_WRN, "failed to resume wlan: %d\n", ret);
	}
#ifdef CONFIG_ROCK_CHIP_WLAN_RECOVERY
	if (!fpga_platform)
		schedule_delayed_work(&wq_sdio->detect_work,
				      msecs_to_jiffies(1000));
#endif

	WQ_DBG(DM_TRBUS, DL_WRN, "sys resume end\n");
	return ret;
}
#endif /* MMC_PM_KEEP_POWER*/

static __maybe_unused int wq_sdio_runtime_suspend(struct device *dev)
{
	struct wq_sdio *wq_sdio = sdio_get_drvdata(dev_to_sdio_func(dev));
	struct wq_func *wlan_func = &wq_sdio->wlan;

	if (!runtime_inited) {
		return 0;
	}
	WQ_DBG(DM_TRBUS, DL_WRN, "runtime suspend enter\n");
	if (atomic_read(&wq_sdio->pm_status) != SDIO_ACTIVE) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: sdio not active\n", __func__);
		return -EBUSY;
	}
	atomic_set(&wq_sdio->pm_status, SDIO_RPM_SUSPENDING);
	wq_sdio_send_pm_message(wlan_func, SDIO_PM_MSG_RPM_SUSPEND);
	wq_sdio_wait_pm_ack(wlan_func, SDIO_PM_MSG_RPM_SUSPEND);
	atomic_set(&wq_sdio->pm_status, SDIO_RPM_SUSPENDED);
	WQ_DBG(DM_TRBUS, DL_WRN, "runtime suspend end\n");
	return 0;
}

static __maybe_unused int wq_sdio_runtime_resume(struct device *dev)
{
	int ret = 0;
	struct wq_sdio *wq_sdio = sdio_get_drvdata(dev_to_sdio_func(dev));
	struct wq_func *wlan_func = &wq_sdio->wlan;
	if (!runtime_inited) {
		return ret;
	}
	WQ_DBG(DM_TRBUS, DL_WRN, "runtime resume enter\n");
	atomic_set(&wq_sdio->pm_status, SDIO_RESUMING);
	wq_sdio_send_pm_message(wlan_func, SDIO_PM_MSG_RPM_RESUME);
	wq_sdio_wait_pm_ack(wlan_func, SDIO_PM_MSG_RPM_RESUME);
	atomic_set(&wq_sdio->pm_status, SDIO_ACTIVE);
	pm_runtime_mark_last_busy(dev);
	wq_func_main_trigger(&wq_sdio->wlan);
	if (wq_sdio->wlan_msg_en) {
		wq_func_main_trigger(&wq_sdio->wlan_msg);
	}
	WQ_DBG(DM_TRBUS, DL_WRN, "runtime resume end\n");
	return ret;
}

/**
 * wq_sdio_runtime_init() - Initialize Runtime PM
 * @dev: device structure
 * @delay: delay to be configured for autosuspend
 *
 * This function will init all the Runtime PM config.
 *
 * Return: void
 */
static __maybe_unused void wq_sdio_runtime_init(struct device *dev, int delay)
{
	struct wq_sdio *wq_sdio = sdio_get_drvdata(dev_to_sdio_func(dev));
	atomic_set(&wq_sdio->pm_status, SDIO_ACTIVE);
	pm_runtime_set_active(dev);
	if (!pm_runtime_active(dev)) {
		pm_runtime_set_active(dev);
	}
	if (!pm_runtime_enabled(dev)) {
		pm_runtime_enable(dev);
	}
	pm_runtime_set_autosuspend_delay(dev, delay);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_forbid(dev);
	runtime_inited = true;
	WQ_DBG(DM_TRBUS, DL_WRN,
	       "dev %s sdio runtime pm init end, usage_cnt=%d\n", dev_name(dev),
	       dev->power.usage_count.counter);
}

static int wq_sdio_autopm_get_async(struct wq_core *core)
{
	WQ_DBG(DM_TRBUS, DL_VRB, "runtime get\n");
	return pm_runtime_get(core->dev);
}

static void wq_sdio_autopm_put_async(struct wq_core *core)
{
	pm_runtime_mark_last_busy(core->dev);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0)
	__pm_runtime_put_autosuspend(core->dev);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0) */
	pm_runtime_put_autosuspend(core->dev);
#endif
	WQ_DBG(DM_TRBUS, DL_VRB, "runtime put\n");
}

static bool wq_sdio_is_pm_active(struct wq_core *core)
{
	struct wq_sdio *wq_sdio = (struct wq_sdio *)core;
	return !atomic_read(&wq_sdio->pm_status);
}

static void wq_sdio_rpm_allow(struct wq_core *core)
{
	pm_runtime_allow(core->dev);
	WQ_DBG(DM_TRBUS, DL_WRN, "runtime allow usage_cnt %d\n",
	       core->dev->power.usage_count.counter);
}

#define MAX_POLL_TRIES 10

int __wq_sdio_cmd52_read(struct sdio_func *func, int addr, u8 *val)
{
	int ret = 0;

	*val = sdio_f0_readb(func, addr, &ret);

	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: Cannot read addr 0x%x ret %d\n",
		       __func__, addr, ret);
	}

	return ret;
}

int wq_sdio_cmd52_read(struct sdio_func *func, int addr, u8 *val)
{
	int ret;

	sdio_claim_host(func);
	ret = __wq_sdio_cmd52_read(func, addr, val);
	sdio_release_host(func);

	if (ret == 0)
		WQ_DBG(DM_TRBUS, DL_VRB, "%s: read addr 0x%x = 0x%x\n",
			__func__, addr, *val);

	return ret;
}

int wq_sdio_extend_mem_readb(struct sdio_func *func, int addr, u8 *val)
{
	int ret = 0;

	sdio_claim_host(func);
	*val = sdio_readb(func, addr, &ret);
	sdio_release_host(func);

	/* ret: Flag Data for IO RW DIRECT SD Response */
	if (ret & 0xCF) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "%s: Cannot read addr 0x%x. (erro:%d)\n", __func__, addr,
		       ret);
		return ret;
	}

	return -EIO;
}

int __wq_sdio_cmd52_write(struct sdio_func *func, int addr, u8 val)
{
	int ret = 0;

	/* Perform actual write only if val is provided */
	sdio_f0_writeb(func, val, addr, &ret);

	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: Cannot write 0x%x = 0x%x\n",
		       __func__, addr, val);
	}

	return ret;
}

int wq_sdio_cmd52_write(struct sdio_func *func, int addr, u8 val)
{
	int ret = 0;

	sdio_claim_host(func);
	/* Perform actual write only if val is provided */
	ret = __wq_sdio_cmd52_write(func, addr, val);
	sdio_release_host(func);

	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: Cannot write 0x%x = 0x%x\n",
		       __func__, addr, val);
	}

	return ret;
}

static int __wq_sdio_cmd52_rwm(struct wq_sdio *wq_sdio, struct sdio_func *func,
			     u8 addr, u8 set, u8 reset, u8 *save)
{
	int ret;
	u8 val;

	ret = __wq_sdio_cmd52_read(func, addr, &val);
	if (ret == 0) {
		u8 val2 = val;

		val2 |= set;
		val2 &= ~reset;
		if (val2 != val)
			ret = __wq_sdio_cmd52_write(func, addr, val2);
		if (save && ret == 0)
			*save = val2;
	}
	return ret;
}

int wq_sdio_intr_en(struct wq_sdio *wq_sdio, struct wq_func *wq_func, bool en)
{
#define WQ_SDIO_WLAN_INTR_MASTER BIT(0)
	struct sdio_func *func = wq_func->func;
	int ret;
	u8 func_num;
	u8 set;
	u8 reset;

	sdio_claim_host(func);

	func_num = wq_func->func_num;
	set = en ? (WQ_SDIO_WLAN_INTR_MASTER | BIT(func_num)) : 0;
	reset = en ? 0 : BIT(func_num);

	ret = __wq_sdio_cmd52_rwm(wq_sdio, func, SDIO_CCCR_INT_ENABLE, set,
					reset, &wq_sdio->intr);

	sdio_release_host(func);

	return ret;
}

static int wq_sdio_get_host_maxsize_status(struct wq_func *wq_func)
{
	struct sdio_func *func = wq_func->func;
	u8 ret = 0;

	if (func->card->host->max_seg_size == WQ_SDIO_64K_MAX_SEG_SIZE) {
		ret = WQ_SDIO_HOST_RX_MAX_SIZE_64K;
	} else if (func->card->host->max_seg_size ==
		   WQ_SDIO_128K_MAX_SEG_SIZE) {
		ret = WQ_SDIO_HOST_RX_MAX_SIZE_128K;
	} else {
		ret = WQ_SDIO_HOST_RX_MAX_SIZE_OTHER;
	}

	return ret;
}

void wq_sdio_set_host_rx_max_size(struct wq_func *wq_func)
{
	struct sdio_func *func = wq_func->func;
	uint8_t ret = 0;

	ret = wq_sdio_get_host_maxsize_status(wq_func);

	wq_sdio_cmd52_write(func, SDIO_CCCR_WUQI_RCV2, ret);
}

int wq_sdio_intr_accu_len_en(struct wq_sdio *wq_sdio, struct wq_func *wq_func,
                             bool en)
{
	struct sdio_func *func = wq_func->func;
	u8 func_num;
	int ret = 0;

	sdio_claim_host(func);

	func_num = wq_func->func_num;

	if (!en == !(wq_sdio->intr_accu_len & BIT(func_num - 1))) {
		goto exit;
	}

	ret = __wq_sdio_cmd52_write(func,
				en ? SDIO_CCCR_WUQI_ACCU_LEN_INT_SET : SDIO_CCCR_WUQI_ACCU_LEN_INT_CLR,
				BIT(func_num - 1));
	__wq_sdio_cmd52_read(func, SDIO_CCCR_WUQI_ACCU_LEN_INT, &wq_sdio->intr_accu_len);

exit:
	sdio_release_host(func);

	return ret;
}

int wq_sdio_intr_buf_avail_en(struct wq_sdio *wq_sdio, struct wq_func *wq_func,
                              bool en)
{
	struct sdio_func *func = wq_func->func;
	u8 func_num;
	int ret = 0;

	sdio_claim_host(func);

	func_num = wq_func->func_num;

	if (!en == !(wq_sdio->intr_buf_avail & BIT(func_num - 1))) {
		goto exit;
	}

	ret = __wq_sdio_cmd52_write(func,
				en ? SDIO_CCCR_WUQI_BUF_AVAIL_INT_SET : SDIO_CCCR_WUQI_BUF_AVAIL_INT_CLR,
				BIT(func_num - 1));
	__wq_sdio_cmd52_read(func, SDIO_CCCR_WUQI_BUF_AVAIL_INT, &wq_sdio->intr_buf_avail);

exit:
	sdio_release_host(func);

	return ret;
}

int wq_sdio_get_fw_cfg_mode(struct wq_sdio *wq_sdio, struct wq_func *wq_func)
{
	struct sdio_func *func = wq_func->func;
	u8 cfg_mode = 0;
	int ret;

	ret = wq_sdio_cmd52_read(func, SDIO_CCCR_WUQI_CFG_MODE, &cfg_mode);

	wq_sdio->fw_cfg_mode = cfg_mode;

	WQ_DBG(DM_GENERIC, DL_WRN, "%s: cfg_mode %d\n", __func__, cfg_mode);

	return ret;
}

static void wq_func_workq_deinit(struct wq_func *wq_func)
{
#ifdef SDIO_MAIN_KTHREAD
	wq_thread_deinit(&wq_func->adma.maink);
#else
	wq_workq_deinit(&wq_func->adma.mainq);
#endif

#ifdef SDIO_RX_AGGR_MODE
	wq_workq_deinit(&wq_func->adma.rxq);
#endif
}

static void wq_sdio_func_queue_deinit(struct wq_func *wq_func)
{
	struct wq_skbreq *req;
	struct wq_sdio *wq_sdio = wq_func->wq_sdio;

	/* msgq deinit */
	if (!wq_list_is_empty(&wq_func->q.msgout)) {
		WQ_DBG(DM_GENERIC, DL_WRN, "%s: func %s msgout num %d\n", __func__, wq_func->name, wq_func->q.msgout.num);

		while ((req = wq_skbreq_dequeue(&wq_func->q.msgout)) != NULL) {
			wq_skbreq_free(&wq_sdio->pools.msgout, req);
		}
	}

	/* pktq deinit */
	if (!wq_list_is_empty(&wq_func->q.pktout)) {
		WQ_DBG(DM_GENERIC, DL_WRN, "%s: func %s pktout num %d\n", __func__, wq_func->name, wq_func->q.pktout.num);

		while ((req = wq_skbreq_dequeue(&wq_func->q.pktout)) != NULL) {
			wq_skbreq_free(&wq_sdio->pools.pktout, req);
		}
	}

#ifdef SDIO_RX_AGGR_MODE
	/* aggrin deinit */
	if (!wq_list_is_empty(&wq_func->q.aggrin)) {
		WQ_DBG(DM_GENERIC, DL_WRN, "%s: func %s aggrin num %d\n", __func__, wq_func->name, wq_func->q.aggrin.num);

		while ((req = wq_skbreq_dequeue(&wq_func->q.aggrin)) != NULL) {
			wq_skbreq_free(&wq_sdio->pools.aggrin, req);
		}
	}
#endif

#ifdef SDIO_TX_AGGR_MODE
	/* aggrout deinit */
	if (!wq_list_is_empty(&wq_func->q.aggrout)) {
		WQ_DBG(DM_GENERIC, DL_WRN, "%s: func %s aggrout num %d\n", __func__, wq_func->name, wq_func->q.aggrout.num);

		while ((req = wq_skbreq_dequeue(&wq_func->q.aggrout)) != NULL) {
			wq_skbreq_free(&wq_sdio->pools.aggrout, req);
		}
	}
#endif
}

static void wq_sdio_func_deinit(struct wq_func *wq_func)
{
	struct sdio_func *func = wq_func->func;

	WQ_DBG(DM_GENERIC, DL_WRN, "%s: func %s\n", __func__, wq_func->name);

	sdio_claim_host(func);

	sdio_release_irq(func);
	sdio_disable_func(func);

	sdio_release_host(func);

	wq_func_workq_deinit(wq_func);
	kfree(wq_func->adma.all_info);
	if (wq_func->adma.aggr.tx)
		dev_kfree_skb_any(wq_func->adma.aggr.tx);
	if (wq_func->adma.aggr.rx)
		dev_kfree_skb_any(wq_func->adma.aggr.rx);

	wq_sdio_func_queue_deinit(wq_func);

	sdio_set_drvdata(func, NULL);
}

static int wq_sdio_set_bus_speed_mode(struct sdio_func *func, u8 bus_speed)
{
	struct mmc_card *card = func->card;
	struct mmc_host *host = card->host;
	struct mmc_ios *ios = &host->ios;
	unsigned int timing;
	int err;
	unsigned char speed, uhs_support;
	unsigned int hz;

	/*
	 * If the host doesn't support any of the UHS-I modes, fallback on
	 * default speed.
	 */
	if (!(host->caps &
	      (MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 | MMC_CAP_UHS_SDR50 |
	       MMC_CAP_UHS_SDR104 | MMC_CAP_UHS_DDR50))) {
		WQ_DBG(DM_TRBUS, DL_INF, "%s, uhs not support\n", __func__);
		return 0;
	}

	timing = MMC_TIMING_UHS_SDR12;
	if ((card->host->caps & (MMC_CAP_UHS_SDR104 | MMC_CAP_UHS_SDR50)) &&
	    (bus_speed == SDIO_SPEED_SDR50)) {
		WQ_DBG(DM_TRBUS, DL_INF,
		       "%s, support SDIO_SPEED_SDR50 bus_speed 0x%x\n",
		       __func__, bus_speed);
		timing = MMC_TIMING_UHS_SDR50;
		card->sw_caps.uhs_max_dtr = UHS_SDR50_MAX_DTR;
		card->sd_bus_speed = UHS_SDR50_BUS_SPEED;
	} else if ((card->host->caps & MMC_CAP_UHS_SDR104) &&
		   (bus_speed == SDIO_SPEED_SDR104)) {
		WQ_DBG(DM_TRBUS, DL_INF,
		       "%s, support SDIO_SPEED_SDR104 bus_speed 0x%x\n",
		       __func__, bus_speed);
		timing = MMC_TIMING_UHS_SDR104;
		card->sw_caps.uhs_max_dtr = UHS_SDR104_MAX_DTR;
		card->sd_bus_speed = UHS_SDR104_BUS_SPEED;
	} else if ((card->host->caps & MMC_CAP_UHS_DDR50) &&
		   (bus_speed == SDIO_SPEED_DDR50)) {
		WQ_DBG(DM_TRBUS, DL_INF,
		       "%s, support SDIO_SPEED_DDR50 bus_speed 0x%x\n",
		       __func__, bus_speed);
		timing = MMC_TIMING_UHS_DDR50;
		card->sw_caps.uhs_max_dtr = UHS_DDR50_MAX_DTR;
		card->sd_bus_speed = UHS_DDR50_BUS_SPEED;
	} else if ((card->host->caps & (MMC_CAP_UHS_SDR104 | MMC_CAP_UHS_SDR50 |
					MMC_CAP_UHS_SDR25)) &&
		   (bus_speed == SDIO_SPEED_SDR25)) {
		WQ_DBG(DM_TRBUS, DL_INF,
		       "%s, support SDIO_SPEED_SDR25 bus_speed 0x%x\n",
		       __func__, bus_speed);
		timing = MMC_TIMING_UHS_SDR25;
		card->sw_caps.uhs_max_dtr = UHS_SDR25_MAX_DTR;
		card->sd_bus_speed = UHS_SDR25_BUS_SPEED;
	} else if ((card->host->caps &
		    (MMC_CAP_UHS_SDR104 | MMC_CAP_UHS_SDR50 |
		     MMC_CAP_UHS_SDR25 | MMC_CAP_UHS_SDR12)) &&
		   (bus_speed == SDIO_SPEED_SDR12)) {
		timing = MMC_TIMING_UHS_SDR12;
		card->sw_caps.uhs_max_dtr = UHS_SDR12_MAX_DTR;
		card->sd_bus_speed = UHS_SDR12_BUS_SPEED;
	} else {
		WQ_DBG(DM_TRBUS, DL_INF, "%s, bus_speed 0x%x not support\n",
		       __func__, bus_speed);
		return 0;
	}
	WQ_DBG(DM_TRBUS, DL_INF, "%s, support bus_speed 0x%x\n", __func__,
	       bus_speed);

	uhs_support = sdio_f0_readb(func, SDIO_CCCR_UHSI_SUPPORT, &err);
	if (err) {
		WQ_DBG(DM_TRBUS, DL_WRN,
		       "%s, sdio_f0_readb SDIO_CCCR_UHSI_SUPPORT failed[%d]\n",
		       __func__, err);
		return err;
	}

	WQ_DBG(DM_TRBUS, DL_INF, "%s, uhs_support 0x%x\n", __func__,
	       uhs_support);

	speed = sdio_f0_readb(func, SDIO_CCCR_BUS_SPEED_SEL, &err);
	if (err) {
		WQ_DBG(DM_TRBUS, DL_WRN,
		       "%s, read SDIO_CCCR_BUS_SPEED_SEL failed[%d]\n",
		       __func__, err);
		return err;
	}

	WQ_DBG(DM_TRBUS, DL_INF, "%s, old speed 0x%x\n", __func__, speed);

	speed &= ~SDIO_SPEED_BSS_MASK;
	speed |= bus_speed;

	WQ_DBG(DM_TRBUS, DL_INF, "fpga platform set new speed 0x%x\n", speed);

	sdio_f0_writeb(func, speed, SDIO_CCCR_BUS_SPEED_SEL, &err);
	if (err) {
		WQ_DBG(DM_TRBUS, DL_WRN,
		       "%s, sdio_f0_writeb SDIO_CCCR_BUS_SPEED_SEL failed[%d]\n",
		       __func__, err);
		return err;
	}

	if (bus_speed) {
		/* mmc set timing */
		host->ios.timing = timing;
		host->ops->set_ios(host, ios);

		/* mmc set clock */
		hz = card->sw_caps.uhs_max_dtr;
		if (hz > host->f_max)
			hz = host->f_max;

		host->ios.clock = hz;
		host->ops->set_ios(host, ios);
	}

	return 0;
}

static int wq_sdio_func_init(struct wq_sdio *wq_sdio, struct wq_func *wq_func,
			     struct sdio_func *func, const char *name,
			     size_t adma_len)
{
	int ret = 0;

	ENTER();

	wq_func->name = name;
	wq_func->wq_sdio = wq_sdio;
	wq_func->func = func;
	wq_func->func_num = func->num;

	INIT_WQ_LIST_HEAD(&wq_func->q.pktout);
	INIT_WQ_LIST_HEAD(&wq_func->q.msgout);

#ifdef SDIO_RX_AGGR_MODE
	INIT_WQ_LIST_HEAD(&wq_func->q.aggrin);
#endif

#ifdef SDIO_TX_AGGR_MODE
	INIT_WQ_LIST_HEAD(&wq_func->q.aggrout);
#endif

	wq_func->adma.aggr.tx = __dev_alloc_skb(adma_len, GFP_KERNEL);
	if (!wq_func->adma.aggr.tx) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "%s: sdio tx skb __dev_alloc_skb failed\n", __func__);
		return -ENOMEM;
	}

	wq_func->adma.aggr.rx = __dev_alloc_skb(SDIO_ADMA_RX_LEN_MAX, GFP_KERNEL);
	if (!wq_func->adma.aggr.rx) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "%s: sdio rx skb __dev_alloc_skb failed\n", __func__);
		return -ENOMEM;
	}

	BUILD_BUG_ON(sizeof(*wq_func->adma.all_info) != SDIO_ADMA_INFO_LEN);
	wq_func->adma.all_info =
		kzalloc(sizeof(*wq_func->adma.all_info), GFP_KERNEL);
	if (!wq_func->adma.all_info) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: sdio alloc adma info failed\n",
		       __func__);
		goto free_tx_skb;
	}

	memset(&wq_func->adma.info, 0, sizeof(struct wq_sdio_adma_info));
	wq_func->adma.info.tx_need_sync = true;

#ifdef SDIO_MAIN_KTHREAD
	ret = wq_thread_init(&wq_func->adma.maink, wq_sdio_adma_process,
			     "wq_sdio_adma_%s", wq_func->name);
#else
	ret = wq_workq_init(&wq_func->adma.mainq,
			    WQ_HIGHPRI | WQ_MEM_RECLAIM | WQ_UNBOUND,
			    wq_sdio_adma_process, "wq_sdio_adma_%s",
			    wq_func->name);
#endif
	if (ret)
		goto workq_deinit;

#ifdef SDIO_RX_AGGR_MODE
	ret = wq_workq_init(&wq_func->adma.rxq, WQ_MEM_RECLAIM | WQ_UNBOUND,
			    wq_sdio_adma_rx_process, "wq_sdio_adma_rx_%s",
			    wq_func->name);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)
	func->card->quirks |= MMC_QUIRK_LENIENT_FN0;
#endif

#ifdef MMC_QUIRK_BLKSZ_FOR_BYTE_MODE
	/* The byte mode patch is available in kernel MMC driver which fixes
	 * one issue in MP-A transfer. bit1: use func->cur_blksize for byte mode
	 */
	func->card->quirks |= MMC_QUIRK_BLKSZ_FOR_BYTE_MODE;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
	/* wait for chip fully wake up */
	if (!func->enable_timeout)
		func->enable_timeout = 200;
#endif

	WQ_DBG(DM_TRBUS, DL_INF, "%s: func num %d multi_block %d blksize: %d\n",
	       __func__, func->num, func->card->cccr.multi_block,
	       func->cur_blksize);

	sdio_claim_host(func);

	if (fpga_platform) {
		/* Sometimes data cannot be received on the fpga, please enable this function */
		if (strcmp(name, "dtop") == 0) {
			wq_sdio_set_bus_speed_mode(func, SDIO_SPEED_SDR12);
		}
	}

	ret = sdio_enable_func(func);
	if (ret) {
		ret = -EIO;
		WQ_DBG(DM_TRBUS, DL_ERR, "sdio_enable_func(%d) failed: ret=%d\n",
		       func->num, ret);
		goto release_host;
	}

	/* Request the SDIO IRQ */
	ret = sdio_claim_irq(func, wq_sdio_interrupt);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR, "sdio_claim_irq(%d) failed: ret=%d\n",
		       func->num, ret);
		goto disable_func;
	}

	/* Set block size */
	ret = sdio_set_block_size(func, WQ_SDIO_BLOCK_SIZE);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "sdio_set_block_seize(%d): cannot set SDIO block size\n", func->num);
		ret = -1;
		goto release_irq;
	}

	sdio_release_host(func);

	sdio_set_drvdata(func, wq_sdio); /* for wq_sdio_interrupt */

	wq_sdio_intr_accu_len_en(wq_sdio, wq_func, true);

	LEAVE();
	return 0;

release_irq:
	sdio_release_irq(func);
disable_func:
	sdio_disable_func(func);
release_host:
	sdio_release_host(func);
workq_deinit:
	wq_func_workq_deinit(wq_func);
	kfree(wq_func->adma.all_info);
	wq_func->adma.all_info = NULL;
free_tx_skb:
	dev_kfree_skb_any(wq_func->adma.aggr.tx);
	wq_func->adma.aggr.tx = NULL;

	LEAVE();
	return ret;
}

static void wq_sdio_sw_deinit(struct wq_sdio *wq_sdio)
{
	if (wq_sdio->bmi.reply) {
		dev_kfree_skb_any(wq_sdio->bmi.reply);
		wq_sdio->bmi.reply = NULL;
	}

	wq_sdio_pools_deinit(wq_sdio);
}

static bool wq_sdio_bus_dead(struct wq_core *core)
{
	struct wq_sdio *wq_sdio = container_of(core, struct wq_sdio, core);
	return wq_sdio->bus_dead;
}

static int wq_sdio_bus_alive(struct mmc_host *host)
{
	struct mmc_card *card;
	struct mmc_command cmd = {};
	card = host->card;
	cmd.opcode = MMC_SELECT_CARD;

	if (card) {
		cmd.arg = card->rca << 16;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;
	} else {
		cmd.arg = 0;
		cmd.flags = MMC_RSP_NONE | MMC_CMD_AC;
	}

	return mmc_wait_for_cmd(host, &cmd, MMC_CMD_RETRIES);
}

static void __maybe_unused wq_sdio_detect(struct work_struct *work_data)
{
	int ret = 0;
	struct wq_sdio *wq_sdio;
	struct mmc_host *host;
	struct delayed_work *dwork;
	dwork = container_of(work_data, struct delayed_work, work);
	wq_sdio = container_of(dwork, struct wq_sdio, detect_work);
	host = wq_sdio->dtop.func->card->host;
	sdio_claim_host(wq_sdio->dtop.func);
	ret = wq_sdio_bus_alive(host);
	sdio_release_host(wq_sdio->dtop.func);
	if (likely(!ret)) {
		schedule_delayed_work(&wq_sdio->detect_work,
				      msecs_to_jiffies(500));
		WQ_DBG(DM_TRBUS, DL_VRB, "%s, sdio alive\n", __func__);
	} else {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s, sdio bus dead\n", __func__);
		wq_sdio->bus_dead = true;
		wq_wlan_handle_bus_recovery(&wq_sdio->core);
	}
}

static int wq_sdio_sw_init(struct wq_sdio *wq_sdio)
{
	spin_lock_init(&wq_sdio->bmi.lock);
#ifdef USE_COMPLETE
	init_completion(&wq_sdio->bmi.completion);
#else
	init_waitqueue_head(&wq_sdio->bmi.wait_q);
#endif

	atomic_set(&wq_sdio->wlan_stats.rx_total_cnt, 0);
	atomic_set(&wq_sdio->wlan_stats.rx_pkt_total_num, 0);
	atomic_set(&wq_sdio->wlan_stats.rx_pkt_total_bytes, 0);
	atomic_set(&wq_sdio->wlan_stats.rx_adma_total_cnt, 0);

	atomic_set(&wq_sdio->wlan_stats.tx_total_cnt, 0);
	atomic_set(&wq_sdio->wlan_stats.tx_pkt_total_num, 0);
	atomic_set(&wq_sdio->wlan_stats.tx_pkt_total_bytes, 0);
	atomic_set(&wq_sdio->wlan_stats.tx_buf_avail_valid_cnt, 0);
	atomic_set(&wq_sdio->wlan_stats.tx_buf_avail_valid_pkt_cnt, 0);
	atomic_set(&wq_sdio->wlan_stats.tx_buf_avail_zero_cnt, 0);
	atomic_set(&wq_sdio->wlan_stats.tx_buf_avail_zero_pkt_cnt, 0);
	atomic_set(&wq_sdio->wlan_stats.tx_buf_avail_wr_cnt, 0);
	wq_sdio->wlan_stats.tx_aggr_avg_cnt = 0;

	atomic_set(&wq_sdio->wlan_stats.main_kthread_time, 0);
	atomic_set(&wq_sdio->wlan_stats.main_tx_time, 0);
	atomic_set(&wq_sdio->wlan_stats.main_rx_time, 0);
	atomic_set(&wq_sdio->wlan_stats.main_rx_adma_time, 0);
	atomic_set(&wq_sdio->wlan_stats.main_tx_aggr_time, 0);
	atomic_set(&wq_sdio->wlan_stats.rx_workq_time, 0);
	atomic_set(&wq_sdio->wlan_stats.rx_deaggr_time, 0);
	atomic_set(&wq_sdio->wlan_stats.rx_htc_time, 0);
	atomic_set(&wq_sdio->wlan_stats.rx_workq_tx_aggr_time, 0);
	atomic_set(&wq_sdio->wlan_stats.tx_xmit_aggr_time, 0);
	wq_sdio->wlan_stats.rx_aggr_avg_cnt = 0;

#ifdef CONFIG_ROCK_CHIP_WLAN_RECOVERY
	if (!fpga_platform)
		INIT_DELAYED_WORK(&wq_sdio->detect_work, wq_sdio_detect);
#endif

	return wq_sdio_pools_init(wq_sdio);
}

static const struct dev_pm_ops wq_sdio_pm_ops = {
#if defined(MMC_PM_KEEP_POWER)
	.prepare = wq_sdio_pm_prepare,
	SET_SYSTEM_SLEEP_PM_OPS(wq_sdio_pm_suspend, wq_sdio_pm_resume)
#endif
	SET_RUNTIME_PM_OPS(wq_sdio_runtime_suspend,
				wq_sdio_runtime_resume, NULL)
};

static const enum wq_sdio_vqid virt_qid_map[] = {
	WQ_SDIO_VQID_PKTOUT_BK, WQ_SDIO_VQID_PKTOUT_BE,
	WQ_SDIO_VQID_PKTOUT_VI, WQ_SDIO_VQID_PKTOUT_VO,

	WQ_SDIO_VQID_MSGOUT,
};

static int wq_sdio_tx_msgq(struct wq_core *core, enum wq_hif_qid qid,
			   struct sk_buff_head *skbq)
{
	struct wq_sdio *wq_sdio = container_of(core, struct wq_sdio, core);
	struct wq_func *wq_func;
	struct wq_skbreq *req;
	struct sk_buff *skb;

	if (!core) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: core is NULL!\n", __func__);
		return -1;
	}

	if (wq_sdio->wlan_msg_en) {
		wq_func = &wq_sdio->wlan_msg;
	} else {
		wq_func = &wq_sdio->wlan;
	}

	while ((skb = __skb_dequeue(skbq))) {
		req = wq_skbreq_alloc(&wq_sdio->pools.msgout);
		if (!req) {
			WQ_DBG(DM_TRBUS, DL_ERR,
			       "%s: req is null, tx_buffer_avail %d\n",
			       __func__,
			      wq_func->adma.info.tx_buffer_avail);
			__skb_queue_head(skbq, skb);
			return -1;
		}

		req->skb = skb;
		req->virt_qid = WQ_SDIO_VQID_MSGOUT;
		wq_skbreq_enqueue(&wq_func->q.msgout, req);
	}

	if (atomic_read(&wq_sdio->pm_status) == SDIO_ACTIVE) {
		wq_func_main_trigger(wq_func);
	}

	return 0;
}

static int wq_sdio_tx_pktq(struct wq_core *core, enum wq_hif_qid qid,
			   struct sk_buff_head *skbq)
{
	struct wq_sdio *wq_sdio = container_of(core, struct wq_sdio, core);
	struct wq_func *wq_func = &wq_sdio->wlan;
	struct rwnx_hw *rwnx_hw = wq_sdio->core.hw;
	struct wq_skbreq *req;
	struct sk_buff *skb;
#ifdef SDIO_TX_AGGR_MODE
	int ret = 0;
	u64 time_start_us = 0, time_end_us = 0;
	int aggr_size = wq_sdio_adma_aggr_size(wq_func);
#endif

	BUG_ON(qid >= ARRAY_SIZE(virt_qid_map));

	if (!core) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: core is NULL!\n", __func__);
		return -1;
	}

	while ((skb = __skb_dequeue(skbq))) {
		/* send packet by pktout free list wq_skbreq */
		req = wq_skbreq_alloc(&wq_sdio->pools.pktout);
		if (!req) {
			WQ_DBG(DM_TRBUS, DL_ERR,
			       "%s: req is null, tx_buffer_avail %d\n",
			       __func__,
			       wq_func->adma.info.tx_buffer_avail);
			__skb_queue_head(skbq, skb);
			return -1;
		}

		req->skb = skb;
		req->virt_qid = virt_qid_map[qid];

		wq_skbreq_enqueue(&wq_func->q.pktout, req);
	}

#ifdef SDIO_TX_AGGR_MODE
	time_start_us = (u64)ktime_to_us(ktime_get());

	if (aggr_size) {
		if (rwnx_hw && (rwnx_hw->tx_throughput > SDIO_TP_THRESHOLD)) {
			if (wq_func->q.pktout.num >= aggr_size) {
				wq_sdio_adma_tx_pkt_aggr(wq_func, SDIO_ADMA_TX_MAX_AGGR_SIZE);
			}
		} else {
			wq_sdio_adma_tx_pkt_aggr(wq_func, SDIO_ADMA_TX_MAX_AGGR_SIZE);
		}
	}

	time_end_us = (u64)ktime_to_us(ktime_get());
	atomic_add((u32)(time_end_us - time_start_us), &wq_sdio->wlan_stats.tx_xmit_aggr_time);
#endif

	if (atomic_read(&wq_sdio->pm_status) == SDIO_ACTIVE) {
		wq_func_main_trigger(wq_func);
	}

	return ret;
}

static int wq_sdio_tx(struct wq_core *core, enum wq_hif_qid qid,
		      struct sk_buff_head *skbq)
{
	int ret;
	struct wq_sdio *wq_sdio = container_of(core, struct wq_sdio, core);

	BUG_ON(!skbq);
	if(wq_sdio->bus_dead) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: fw crashed!\n", __func__);
		return -ENXIO;
	}

	if (qid == WQ_QID_MSG)
		ret = wq_sdio_tx_msgq(core, qid, skbq);
	else
		ret = wq_sdio_tx_pktq(core, qid, skbq);

	return ret;
}

static void wq_sdio_dump_stats(struct wq_sdio *wq_sdio)
{
	struct wq_func *wq_func = &wq_sdio->wlan;
	struct rwnx_hw *rwnx_hw = wq_sdio->core.hw;

	u32 rx_total_cnt, rx_pkt_total_num, rx_adma_total_cnt;
	u64 rx_pkt_total_bytes, rx_avg_pkt_bytes, rx_avg_time, rx_adma_avg_time;

	u32 tx_total_cnt, tx_pkt_total_num;
	u64 tx_pkt_total_bytes, tx_avg_pkt_bytes, tx_avg_time;
	u32 tx_buf_avail_valid_cnt, tx_buf_avail_valid_pkt_cnt, tx_buf_avail_zero_cnt, tx_buf_avail_zero_pkt_cnt, tx_buf_avail_wr_cnt;

	u32 main_kthread_time, main_rx_adma_time, main_tx_time, main_rx_time, main_tx_aggr_time;
	u32 rx_workq_time, tx_xmit_aggr_time, rx_deaggr_time, rx_htc_time;

	/* sdio rx statistics */
	rx_total_cnt = atomic_read(&wq_sdio->wlan_stats.rx_total_cnt);
	rx_pkt_total_num = atomic_read(&wq_sdio->wlan_stats.rx_pkt_total_num);
	rx_pkt_total_bytes = atomic_read(&wq_sdio->wlan_stats.rx_pkt_total_bytes);
	main_rx_time = atomic_read(&wq_sdio->wlan_stats.main_rx_time);
	main_rx_adma_time = atomic_read(&wq_sdio->wlan_stats.main_rx_adma_time);
	main_tx_aggr_time = atomic_read(&wq_sdio->wlan_stats.main_tx_aggr_time);

	rx_avg_pkt_bytes = rx_pkt_total_bytes;
	do_div(rx_avg_pkt_bytes, rx_total_cnt);
	rx_avg_time = main_rx_time;
	do_div(rx_avg_time, rx_total_cnt);

	WQ_DBG(DM_TRBUS, DL_ERR,
			"%s: rx_total_cnt %u, rx_pkt_total_num %u, rx_avg_pkt_cnt %u, rx_avg_pkt_bytes %llu, main_rx_time %u, rx_avg_time %llu\n",
			__func__, rx_total_cnt, rx_pkt_total_num,
			(rx_pkt_total_num / rx_total_cnt), rx_avg_pkt_bytes, main_rx_time, rx_avg_time);

	atomic_set(&wq_sdio->wlan_stats.rx_total_cnt, 0);
	atomic_set(&wq_sdio->wlan_stats.rx_pkt_total_num, 0);
	atomic_set(&wq_sdio->wlan_stats.rx_pkt_total_bytes, 0);
	atomic_set(&wq_sdio->wlan_stats.main_rx_time, 0);


	/* sdio rx adma statistics */
	rx_adma_total_cnt = atomic_read(&wq_sdio->wlan_stats.rx_adma_total_cnt);
	rx_adma_avg_time = main_rx_adma_time;
	do_div(rx_adma_avg_time, rx_adma_total_cnt);

	WQ_DBG(DM_TRBUS, DL_ERR,
			"%s: rx_adma_total_cnt %u, main_rx_adma_time %u, rx_adma_avg_time %llu, rx_aggr_avg_cnt %u/%u\n",
			__func__, rx_adma_total_cnt, main_rx_adma_time, rx_adma_avg_time, wq_sdio->wlan_stats.rx_aggr_avg_cnt, wq_sdio->pools.aggrin.num);

	atomic_set(&wq_sdio->wlan_stats.main_rx_adma_time, 0);
	atomic_set(&wq_sdio->wlan_stats.rx_adma_total_cnt, 0);


	/* sdio tx statistics */
	tx_total_cnt = atomic_read(&wq_sdio->wlan_stats.tx_total_cnt);
	tx_pkt_total_num = atomic_read(&wq_sdio->wlan_stats.tx_pkt_total_num);
	tx_pkt_total_bytes = atomic_read(&wq_sdio->wlan_stats.tx_pkt_total_bytes);
	main_tx_time = atomic_read(&wq_sdio->wlan_stats.main_tx_time);

	tx_avg_pkt_bytes = tx_pkt_total_bytes;
	do_div(tx_avg_pkt_bytes, tx_total_cnt);

	tx_avg_time = main_tx_time;
	do_div(tx_avg_time, tx_total_cnt);

	WQ_DBG(DM_TRBUS, DL_ERR,
			"%s: tx_total_cnt %u, tx_pkt_total_num %u, tx_avg_pkt_cnt %u, tx_avg_pkt_bytes %llu, main_tx_time %u, tx_avg_time %llu, tx_aggr_avg_cnt %u/%u\n",
			__func__, tx_total_cnt, tx_pkt_total_num,
			(tx_pkt_total_num / tx_total_cnt), tx_avg_pkt_bytes, main_tx_time, tx_avg_time, wq_sdio->wlan_stats.tx_aggr_avg_cnt, wq_sdio->pools.aggrout.num);

	atomic_set(&wq_sdio->wlan_stats.tx_total_cnt, 0);
	atomic_set(&wq_sdio->wlan_stats.tx_pkt_total_num, 0);
	atomic_set(&wq_sdio->wlan_stats.tx_pkt_total_bytes, 0);
	atomic_set(&wq_sdio->wlan_stats.main_tx_time, 0);


	/* sdio adma hw statistics */
	tx_buf_avail_valid_cnt = atomic_read(&wq_sdio->wlan_stats.tx_buf_avail_valid_cnt);
	tx_buf_avail_valid_pkt_cnt = atomic_read(&wq_sdio->wlan_stats.tx_buf_avail_valid_pkt_cnt);
	tx_buf_avail_zero_cnt = atomic_read(&wq_sdio->wlan_stats.tx_buf_avail_zero_cnt);
	tx_buf_avail_zero_pkt_cnt = atomic_read(&wq_sdio->wlan_stats.tx_buf_avail_zero_pkt_cnt);
	tx_buf_avail_wr_cnt = atomic_read(&wq_sdio->wlan_stats.tx_buf_avail_wr_cnt);

	WQ_DBG(DM_IPC, DL_ERR,
			"%s: tx_buf_avail_valid_cnt %u/%u, tx_buf_avail_zero_cnt %u/%u, tx_buf_avail_wr_cnt %u, adma tx_buf_avail 0x%x - 0x%x = %u, rx_len 0x%x - 0x%x = %u\n",
			__func__, tx_buf_avail_valid_cnt, tx_buf_avail_valid_pkt_cnt, tx_buf_avail_zero_cnt, tx_buf_avail_zero_pkt_cnt,
			tx_buf_avail_wr_cnt, wq_func->adma.info.tx_accu_cnt, wq_func->adma.info.tx_bus_cnt, wq_func->adma.info.tx_buffer_avail,
			wq_func->adma.info.rx_accu_len, wq_func->adma.info.rx_bus_len, wq_func->adma.info.rx_len);

	atomic_set(&wq_sdio->wlan_stats.tx_buf_avail_valid_cnt, 0);
	atomic_set(&wq_sdio->wlan_stats.tx_buf_avail_valid_pkt_cnt, 0);
	atomic_set(&wq_sdio->wlan_stats.tx_buf_avail_zero_cnt, 0);
	atomic_set(&wq_sdio->wlan_stats.tx_buf_avail_zero_pkt_cnt, 0);
	atomic_set(&wq_sdio->wlan_stats.tx_buf_avail_wr_cnt, 0);

	/* sdio kthread and workq time statistics */
	main_kthread_time = atomic_read(&wq_sdio->wlan_stats.main_kthread_time);
	rx_workq_time = atomic_read(&wq_sdio->wlan_stats.rx_workq_time);
	tx_xmit_aggr_time = atomic_read(&wq_sdio->wlan_stats.tx_xmit_aggr_time);
	rx_deaggr_time = atomic_read(&wq_sdio->wlan_stats.rx_deaggr_time);
	rx_htc_time = atomic_read(&wq_sdio->wlan_stats.rx_htc_time);

	WQ_DBG(DM_IPC, DL_ERR,
			"%s: main_kthread_time %u, main_tx_aggr_time %u, rx_workq_time %u, rx_deaggr_time %u, rx_htc_time %u, tx_xmit_aggr_time %u\n",
			__func__, main_kthread_time, main_tx_aggr_time, rx_workq_time, rx_deaggr_time, rx_htc_time, tx_xmit_aggr_time);

	atomic_set(&wq_sdio->wlan_stats.main_kthread_time, 0);
	atomic_set(&wq_sdio->wlan_stats.main_tx_aggr_time, 0);
	atomic_set(&wq_sdio->wlan_stats.rx_workq_time, 0);
	atomic_set(&wq_sdio->wlan_stats.tx_xmit_aggr_time, 0);
	atomic_set(&wq_sdio->wlan_stats.rx_deaggr_time, 0);
	atomic_set(&wq_sdio->wlan_stats.rx_htc_time, 0);
	atomic_set(&wq_sdio->wlan_stats.rx_workq_tx_aggr_time, 0);

	if (rwnx_hw) {
		u32 napi_rx_time = 0, rx_reorder_time = 0, ipc_rx_pkt_time = 0, ipc_rx_msg_time = 0, htc_rxq_time = 0, htc_rxq_decap_time = 0;

		napi_rx_time = atomic_read(&rwnx_hw->napi_rx_time);
		ipc_rx_pkt_time = atomic_read(&rwnx_hw->ipc_rx_pkt_time);
		ipc_rx_msg_time = atomic_read(&rwnx_hw->ipc_rx_msg_time);
		rx_reorder_time = atomic_read(&rwnx_hw->rx_reorder_time);
		htc_rxq_time = atomic_read(&rwnx_hw->htc_rxq_time);
		htc_rxq_decap_time = atomic_read(&rwnx_hw->htc_rxq_decap_time);

		WQ_DBG(DM_IPC, DL_ERR,
			"%s: napi_rx_time %u, ipc_rx_pkt_time %u, ipc_rx_msg_time %u, rx_reorder_time %u, htc_rxq_time %u, htc_rxq_decap_time %u\n",
			__func__, napi_rx_time, ipc_rx_pkt_time, ipc_rx_msg_time, rx_reorder_time, htc_rxq_time, htc_rxq_decap_time);

		atomic_set(&rwnx_hw->napi_rx_time, 0);
		atomic_set(&rwnx_hw->ipc_rx_pkt_time, 0);
		atomic_set(&rwnx_hw->ipc_rx_msg_time, 0);
		atomic_set(&rwnx_hw->rx_reorder_time, 0);
		atomic_set(&rwnx_hw->htc_rxq_time, 0);
		atomic_set(&rwnx_hw->htc_rxq_decap_time, 0);
	}
}

static void wq_sdio_dump_info(struct wq_core *core)
{
	struct wq_sdio *wq_sdio = container_of(core, struct wq_sdio, core);

	WQ_DBG(DM_TRBUS, DL_WRN,
	       "interrupt snap shot: 0x%x (accu_len 0x%x, intr_buf_avail: 0x%x)\n",
	       wq_sdio->intr, wq_sdio->intr_accu_len, wq_sdio->intr_buf_avail);

	wq_sdio_dump_cccr_regs(wq_sdio->dtop.func);

	wq_sdio_dump_stats(wq_sdio);
}

static int hif_get_hdr_sz_sdio(struct wq_core *core)
{
	return sizeof(struct wq_hif_hdr);
}

static struct wq_hif_ops wq_sdio_ops = {
	.hif = WQ_HIF_SDIO,
	.txq_stop_threshlod = SDIO_FLOW_CTRL_THRESHOLD_STOP,
	.txq_restart_threshlod = SDIO_FLOW_CTRL_THRESHOLD_RESTART,
	.autopm_get_async = wq_sdio_autopm_get_async,
	.autopm_put_async = wq_sdio_autopm_put_async,
	.autopm_is_bus_active = wq_sdio_is_pm_active,
	.autopm_allow = wq_sdio_rpm_allow,

	.hif_tx = wq_sdio_tx,
	.hif_get_hdr_sz = hif_get_hdr_sz_sdio,

	.dump_info = wq_sdio_dump_info,

	.bmi_cmd = wq_sdio_bmi_cmd,
	.bmi_xfer = wq_sdio_bmi_xfer,
	.bmi_exchange = wq_sdio_bmi_exchange,
	.hif_bus_dead = wq_sdio_bus_dead,
};

static void wq_sdio_set_host_flags(struct mmc_host *host)
{
	if ((host->caps & MMC_CAP_NEEDS_POLL) && mmc_card_is_removable(host))
		return;

	if (!(host->caps & MMC_CAP_NEEDS_POLL)) {
		WQ_DBG(DM_TRBUS, DL_INF, "SDIO host %s caps: + NEEDS_POLL.\n",
		       mmc_hostname(host));
		host->caps |= MMC_CAP_NEEDS_POLL;
	}
	if (!mmc_card_is_removable(host)) {
		WQ_DBG(DM_TRBUS, DL_INF, "SDIO host %s caps: - NONREMOVABLE.\n",
		       mmc_hostname(host));
		host->caps &= ~MMC_CAP_NONREMOVABLE;
	}
}
static void wq_sdio_clear_host_flags(struct mmc_host *host)
{
	if (!(host->caps & MMC_CAP_NEEDS_POLL) && !mmc_card_is_removable(host))
		return;

	WQ_DBG(DM_TRBUS, DL_INF, "SDIO host %s caps: 0x%x%s%s%s.\n",
	       mmc_hostname(host), host->caps,
	       (host->caps & MMC_CAP_HW_RESET) ? " HW_RESET" : "",
	       (host->caps & MMC_CAP_NEEDS_POLL) ? " NEEDS_POLL" : "",
	       (host->caps & MMC_CAP_NONREMOVABLE) ? " NONREMOVABLE" : "");
	if (host->caps & MMC_CAP_NEEDS_POLL) {
		WQ_DBG(DM_TRBUS, DL_INF, "SDIO host %s caps: - NEEDS_POLL.\n",
		       mmc_hostname(host));
		host->caps &= ~MMC_CAP_NEEDS_POLL;
	}
	if (mmc_card_is_removable(host)) {
		WQ_DBG(DM_TRBUS, DL_INF, "SDIO host %s caps: + NONREMOVABLE.\n",
		       mmc_hostname(host));
		host->caps |= MMC_CAP_NONREMOVABLE;
	}
}
static void wq_sdio_blk_count_adjust(struct mmc_host *host)
{
	/**
	* NB: each sdio block is limited at 256, extend max block count to 511 (default: 256),
	* so max RX length can be over 64k (256 * 511 = 130816).
	*/
#define WQ_SDIO_MAX_BLK_COUNT 511U
	if (host->max_blk_count < WQ_SDIO_MAX_BLK_COUNT) {
		WQ_DBG(DM_TRBUS, DL_INF,
		       "SDIO host %s cap: max_blk_count %d ==> %d.\n",
		       mmc_hostname(host), host->max_blk_count,
		       WQ_SDIO_MAX_BLK_COUNT);
		host->max_blk_count = WQ_SDIO_MAX_BLK_COUNT;
	}
}

static void wq_sdio_forbid_chip_sleep(struct wq_sdio *wq_sdio)
{
	struct wq_func *dtop_func = &wq_sdio->dtop;
	if (wq_conf.loadfw_only)
		return;
	wq_sdio_send_pm_message(dtop_func, SDIO_PM_MSG_CHIP_SLEEP_FORBID);
	wq_sdio_wait_pm_ack(dtop_func, SDIO_PM_MSG_CHIP_SLEEP_FORBID);
}
static void wq_sdio_allow_chip_sleep(struct wq_sdio *wq_sdio)
{
	struct wq_func *dtop_func = &wq_sdio->dtop;
	if (wq_conf.loadfw_only)
		return;
	wq_sdio_send_pm_message(dtop_func, SDIO_PM_MSG_CHIP_SLEEP_ALLOW);
	wq_sdio_wait_pm_ack(dtop_func, SDIO_PM_MSG_CHIP_SLEEP_ALLOW);
}

static int __wq_sdio_probe(struct sdio_func *dtop, struct sdio_func *wlan, struct sdio_func *wlan_msg)
{
	struct mmc_host *host = dtop->card->host;
	int ret = 0;
	struct wq_sdio *wq_sdio;

	ENTER();

	WQ_DBG(DM_TRBUS, DL_ERR, "%s: host caps 0x%x, caps2 0x%x, pm_caps 0x%x\n", __func__, host->caps, host->caps2, host->pm_caps);

	wq_sdio_blk_count_adjust(host);
	// clear the MMC_CAP_NEEDS_POLL flag to prevent host auto_scan which 
	// will destroy the pipeline of sdio bus recovery.
	wq_sdio_clear_host_flags(host);

	wq_sdio = (struct wq_sdio *)wq_core_create(&wq_sdio_ops, &wlan->dev,
						   WQ_WPHY_PF_QFN_SDIO,
						   sizeof(struct wq_sdio));
	if (!wq_sdio) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "Failed to allocate memory in probe function!\n");
		return -ENOMEM;
	}

	if (wq_sdio_sw_init(wq_sdio) != 0) {
		ret = -1;
		goto fail1;
	}
#ifdef CONFIG_ROCK_CHIP_WLAN_RECOVERY
	if (!fpga_platform){
		schedule_delayed_work(&wq_sdio->detect_work,
				      msecs_to_jiffies(1000));
	}
	else{
		mmc_detect_change(host, msecs_to_jiffies(200));
	}
#else
	mmc_detect_change(host, msecs_to_jiffies(200));
#endif

#ifdef SDIO_WLAN_MSG_MODE
	if (wlan_msg) {
		wq_sdio->wlan_msg_en = true;
	} else {
		wq_sdio->wlan_msg_en = false;
	}
#else
	wq_sdio->wlan_msg_en = false;
#endif

	if (wq_sdio_func_init(wq_sdio, &wq_sdio->dtop, dtop, "dtop",
			      SDIO_ADMA_DTOP_TX_LEN_MAX) < 0) {
		WQ_DBG(DM_TRBUS, DL_ERR, "wq_sdio_func_init dtop failed\n");
		wq_sdio_func_deinit(&wq_sdio->dtop);
		ret = -1;
		goto fail1;
	}

	if (wq_sdio_func_init(wq_sdio, &wq_sdio->wlan, wlan, "wlan",
			      SDIO_ADMA_WLAN_TX_LEN_MAX) < 0) {
		WQ_DBG(DM_TRBUS, DL_ERR, "wq_sdio_func_init wlan failed\n");
		ret = -1;
		goto deinit_dtop;
	}

	if (wq_sdio->wlan_msg_en) {
		if (wq_sdio_func_init(wq_sdio, &wq_sdio->wlan_msg, wlan_msg, "wlan_msg",
					SDIO_ADMA_DTOP_TX_LEN_MAX) < 0) {
			WQ_DBG(DM_TRBUS, DL_ERR, "wq_sdio_func_init wlan_msg failed\n");
			ret = -1;
			goto deinit_wlan;
		}
	}

	wq_sdio_set_host_rx_max_size(&wq_sdio->dtop);
	wq_sdio_intr_en(wq_sdio, &wq_sdio->dtop, true);
	wq_sdio_dump_info(&wq_sdio->core);

	ret = wq_fw_name_update(&wq_sdio->core, fw_dtop_sdio, fw_wifi_sdio);
	if (ret)
		goto deinit_wlan_msg;

#ifdef CONFIG_SPACEMIT_K1_SDIO_WAR
	wq_sdio_config_k1_autoclock(false);
#endif
	/* Init FW*/
	if (wq_fw_dtop_init(&wq_sdio->core) != 0) {
		ret = -1;
		goto deinit_wlan_msg;
	}
	wq_sdio_forbid_chip_sleep(wq_sdio);

	/* get fw cfg mode */
	wq_sdio_get_fw_cfg_mode(wq_sdio, &wq_sdio->dtop);

	wq_fw_log_proc_init(&wq_sdio->core);



#ifdef CONFIG_RK3588_ENABLE_WAKEUP_OOB
	/* register oob wakeup way */
	WQ_DBG(DM_TRBUS, DL_INF, "%s wq core config oob wakeup method...\n",
	       __FUNCTION__);
	ret = wq_core_register_oob_wakeup_host(&wq_sdio->core);
	if (ret < 0) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "%s wq core oob wakeup register failed: %d\n",
		       __FUNCTION__, ret);
//		goto err_release;
	}
#endif





	if (wq_fw_init(&wq_sdio->core) != 0) {
		WQ_DBG(DM_TRBUS, DL_ERR, "Firmware Init Failed\n");
		ret = -1;
		goto deinit_wlan_msg;
	}

	if (sdio_ut_mode) {
		wq_sdio_ut_init(wq_sdio);
	} else {
		ret = wq_wlan_fw_ready(&wq_sdio->core, 8000);
		if (ret)
			goto deinit_wlan_msg;

		wq_sdio_runtime_init(&wlan->dev, WQ_SDIO_AUTUSUSPEND_DELAY_MS);
		/* disable SDIO tx credit, it doesn't work for now */
		ret = wq_wlan_create(&wq_sdio->core, 0);
		if (ret)
			goto shutdown_wlan;
	}

#ifdef CONFIG_SPACEMIT_K1_SDIO_WAR
	wq_sdio_config_k1_autoclock(true);
#endif
	wq_sdio_allow_chip_sleep(wq_sdio);

	LEAVE();

	return ret;

shutdown_wlan:
	wq_wlan_unregister(&wq_sdio->core);
deinit_wlan_msg:
	if (wq_sdio->wlan_msg_en) {
		wq_sdio_func_deinit(&wq_sdio->wlan_msg);
	}
deinit_wlan:
	wq_sdio_func_deinit(&wq_sdio->wlan);
deinit_dtop:
	wq_sdio_func_deinit(&wq_sdio->dtop);
fail1:
	wq_sdio_sw_deinit(wq_sdio);

	wq_core_destroy(&wq_sdio->core);

	return ret;
}

static int wq_sdio_probe(struct sdio_func *func,
			 const struct sdio_device_id *id)
{
	static struct sdio_func *funcs[3];
	struct mmc_card *card = func->card;
	static int func_cnt = 0;

	ENTER();

	WQ_DBG(DM_TRBUS, DL_WRN,
	       "vendor=0x%4.04X device=0x%4.04X class=%d function=%d sdio_funcs=%d\n",
	       func->vendor, func->device, func->class, func->num, card->sdio_funcs);

	if ((func->num != SDIO_FUNC_WIFI) && (func->num != SDIO_FUNC_DTOP) && (func->num != SDIO_FUNC_WIFI_MSG)) {
		WQ_DBG(DM_TRBUS, DL_WRN,
		       "sdio function=%d, skipped. Only probe WiFi function %d & %d & %d.\n",
		       func->num, SDIO_FUNC_WIFI, SDIO_FUNC_DTOP, SDIO_FUNC_WIFI_MSG);
		return -ENOENT;
	}

	/* FIXME: protection is required. */
	if (funcs[func->num - 1]) {
		WQ_DBG(DM_TRBUS, DL_ERR, "previous sdio funcs[%d] = %p.\n",
		       func->num - 1, funcs[func->num - 1]);
		return -EEXIST;
	}

	funcs[func->num - 1] = func;
	func_cnt++;

	if ((card->sdio_funcs == 7) && (func_cnt == 3)) {
		int ret = __wq_sdio_probe(funcs[0], funcs[1], funcs[2]);

		funcs[0] = NULL;
		funcs[1] = NULL;
		funcs[2] = NULL;
		return ret;
	} else if ((card->sdio_funcs == 2) && (func_cnt == 2)) {
		int ret = __wq_sdio_probe(funcs[0], funcs[1], NULL);

		funcs[0] = NULL;
		funcs[1] = NULL;
		funcs[2] = NULL;
		return ret;
	}

	return 0;
}

static void wq_sdio_remove(struct sdio_func *func)
{
#define SDIO_AUTO_SCAN_PERIOD 250
	struct wq_sdio *wq_sdio = sdio_get_drvdata(func);
	struct mmc_card *card = func->card;

	ENTER();

	WQ_DBG(DM_TRBUS, DL_WRN, "%s: SDIO func=%d wq_sdio %-p\n", __func__,
	       func->num, wq_sdio);

	if (!wq_sdio)
		return;

	if (!sdio_get_drvdata(wq_sdio->dtop.func)) {
		WQ_DBG(DM_TRBUS, DL_VRB, "%s: dtop is removed\n", __func__);
		return;
	}
	if (!sdio_get_drvdata(wq_sdio->wlan.func)) {
		WQ_DBG(DM_TRBUS, DL_VRB, "%s: wlan is removed\n", __func__);
		return;
	}

#ifdef CONFIG_ROCK_CHIP_WLAN_RECOVERY
	if (!fpga_platform)
		cancel_delayed_work_sync(&wq_sdio->detect_work);
#endif

	wq_sdio_set_host_flags(func->card->host);
	mmc_detect_change(func->card->host,
			  msecs_to_jiffies(SDIO_AUTO_SCAN_PERIOD));

	/* stop msg tx */
	wq_wlan_cmd_mgr_drain(&wq_sdio->core);

	/* stop wlan func */
	wq_core_state_set(&wq_sdio->core, WQ_CORE_STATE_HIF_NREADY);

	/* sdio not need unload wifi when card removed */
	if (mmc_card_removed(card)) {
		WQ_DBG(DM_TRBUS, DL_WRN, "%s: card removed\n", __func__);
	} else if (!wq_sdio->bus_dead) {
		wq_wlan_unload_wifi(&wq_sdio->core);
	}

	if (sdio_ut_mode) {
		wq_sdio_ut_deinit(wq_sdio);
	} else {
		wq_wlan_unregister(&wq_sdio->core);
		wq_wlan_destroy(&wq_sdio->core);
	}

#ifdef CONFIG_RK3588_ENABLE_WAKEUP_OOB
	wq_core_unregister_oob_wakeup_host(&wq_sdio->core);
	
#endif
	wq_fw_log_proc_deinit(&wq_sdio->core);

	wq_sdio_func_deinit(&wq_sdio->wlan);
	wq_sdio_func_deinit(&wq_sdio->dtop);

	if (wq_sdio->wlan_msg_en) {
		wq_sdio_func_deinit(&wq_sdio->wlan_msg);
	}

	wq_sdio_sw_deinit(wq_sdio);

	wq_core_destroy(&wq_sdio->core);
	LEAVE();
}

static const struct sdio_device_id wq_sdio_devices[] = {
	{ .class = WUQI_DTOP_CLASS_ID,
	  .vendor = WUQI_DTOP_VENDOR_ID,
	  .device = WUQI_DTOP_DEVICE_ID },
	{ .class = WUQI_WIFI_CLASS_ID,
	  .vendor = WUQI_WIFI_VENDOR_ID,
	  .device = WUQI_WIFI_DEVICE_ID },
	{ .class = WUQI_WIFI_MSG_CLASS_ID,
	  .vendor = WUQI_WIFI_MSG_VENDOR_ID,
	  .device = WUQI_WIFI_MSG_DEVICE_ID },
	{},
};

MODULE_DEVICE_TABLE(sdio, wq_sdio_devices);

static struct sdio_driver wq_sdio_driver = {
	.name = "wq_sdio",
	.id_table = wq_sdio_devices,
	.probe = wq_sdio_probe,
	.remove = wq_sdio_remove,
#ifdef CONFIG_PM
	.drv.pm = &wq_sdio_pm_ops,
#endif
};

int __init wq_sdio_init(void)
{
	int ret;

//	ingenic_sdio_wlan_wuqi_scan_card();
//	printk("xxxxxxxxxxxxxxxxxxxxxxxx func = %s,line = %d  xxxxxxxxxxxxxxxxxxxxx\n",__func__,__LINE__);
//	printk("xxxxxxxxxxxxxxxxxxxxxxxx func = %s,line = %d  xxxxxxxxxxxxxxxxxxxxx\n",__func__,__LINE__);
	wq_module_init();
	ret = sdio_register_driver(&wq_sdio_driver);
	if (ret)
		WQ_DBG(DM_TRBUS, DL_INF, "SDIO Driver Registration Failed\n");

	return ret;
}

void __exit wq_sdio_exit(void)
{
	sdio_unregister_driver(&wq_sdio_driver);
	wq_module_exit();
}

#ifndef WQ_WLAN_ALL_IN_ONE
module_init(wq_sdio_init);
module_exit(wq_sdio_exit);
#endif

MODULE_AUTHOR("WuQi Technologies");
MODULE_DESCRIPTION("Driver support for WuQi 802.11ax WLAN SDIO devices");
MODULE_LICENSE("Dual BSD/GPL");
#ifdef MODULE_IMPORT_NS
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
#endif
