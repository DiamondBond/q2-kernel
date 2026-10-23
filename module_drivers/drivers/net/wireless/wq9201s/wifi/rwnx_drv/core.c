#include <linux/module.h>
#include <linux/rtnetlink.h>

#include "rwnx_main.h"
#include "hif_api.h"
#include "wq_tx_credit.h"
#include "core.h"
#include "wq_ipc.h"
#include "wq_profiling.h"
#include "wq_log.h"
#include "wq_wifi_dbg.h"
#include "wq_fw.h"
#include "bmi_core.h"
#include "bmi_cmd.h"
#include "mp_test.h"
#include "wq_version_gen.h"
#include "wlan_ioctl.h"

// the handle for wq core, only one core for the driver
static struct wq_core *this_wq_core;
extern int force_reset;

struct wq_core *wq_core_get(void)
{
	return this_wq_core;
}
WQ_CORE_API(wq_core_get);

static const char *hif_names[] = {
	[WQ_HIF_NONE] = NULL,
	[WQ_HIF_USB] = "usb",
	[WQ_HIF_SDIO] = "sdio",
	[WQ_HIF_PCIE] = "pcie",
};

static const char *wphy_profiles[] = {
	[WQ_WPHY_PF_DEFAULT] = "",	    [WQ_WPHY_PF_QFN_USB] = "qfn_usb",
	[WQ_WPHY_PF_QFN_SDIO] = "qfn_sdio", [WQ_WPHY_PF_QFN_PCIE] = "qfn_pcie",
	[WQ_WPHY_PF_BGA_PCIE] = "bga_pcie",
};

const char *wq_wphy_profile_name(enum wq_wphy_profile profile)
{
	return wphy_profiles[(profile < WQ_WPHY_PF_MAX) ? profile :
								WQ_WPHY_PF_DEFAULT];
}
WQ_CORE_API(wq_wphy_profile_name);

static const char *core_states[] = { [WQ_CORE_STATE_DETACH] = "DETACH",
				     [WQ_CORE_STATE_FW_CRASHED] = "FW_CRASHED",
				     [WQ_CORE_STATE_HIF_NREADY] = "HIF_NREADY",
				     [WQ_CORE_STATE_HIF_READY] = "HIF_READY",
				     [WQ_CORE_STATE_FW_DL] = "FW_DL",
				     [WQ_CORE_STATE_FW_DL_DONE] = "FW_DL_DONE",
				     [WQ_CORE_STATE_WLAN_FW_READY] =
					     "WLAN_FW_READY",
				     [WQ_CORE_STATE_WIF_NREADY] = "WIF_NREADY",
				     [WQ_CORE_STATE_WIF_READY] = "WIF_READY",

				     [WQ_CORE_STATE_MAX] =
					     "!!!INVALID STATE!!!" };

const char *wq_core_state2name(enum wq_core_state state)
{
	return core_states[(state < WQ_CORE_STATE_MAX) ? state :
							       WQ_CORE_STATE_MAX];
}
WQ_CORE_API(wq_core_state2name);

void wq_core_state_set(struct wq_core *core, enum wq_core_state state)
{
	if (core->state == state) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: core state is already \"%s\"!\n",
		       __func__, wq_core_state2name(state));
		return;
	}
	switch (state) {
	case WQ_CORE_STATE_DETACH:
		/* try to shutdown device while unloading driver */
		if (core->state >= WQ_CORE_STATE_HIF_READY)
			wq_wlan_unregister(core);
		break;
	default:
		break;
	}
	/* FIXME: more sanity check while state transfer */
	WQ_DBG(DM_TRBUS, DL_INF, "%s: core state \"%s\" ==> \"%s\".\n",
	       __func__, wq_core_state2name(core->state),
	       wq_core_state2name(state));
	core->state = state;
}
WQ_CORE_API(wq_core_state_set);

void wq_hif_ops_setup(struct wq_hif_ops *ops);

struct wq_core *wq_core_create(struct wq_hif_ops *hif_ops, struct device *dev,
			       enum wq_wphy_profile wprofile, unsigned size)
{
	struct wq_core *core;
	u8 bundle_num;

	BUG_ON(!hif_ops);
	BUG_ON(hif_ops->hif <= WQ_HIF_NONE || hif_ops->hif >= WQ_HIF_MAX);
	BUG_ON((hif_ops->autopm_enable &&
		(!hif_ops->autopm_get || !hif_ops->autopm_put)) ||
	       (!hif_ops->autopm_enable &&
		(hif_ops->autopm_get || hif_ops->autopm_put)));
	BUG_ON(!hif_ops->hif_tx);
	BUG_ON(!hif_ops->bmi_cmd || !hif_ops->bmi_xfer);
	BUG_ON(!hif_ops->bmi_exchange);

	wq_hif_ops_setup(hif_ops);

	core = kzalloc(size, GFP_KERNEL);
	if (!core) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: no memory (%d bytes)!\n",
		       __func__, size);
		return NULL;
	}

	core->hif_name = hif_names[hif_ops->hif];
	core->hif_ops = hif_ops;
	core->wphy_profile = wprofile;
	core->dev = dev;
	core->state = WQ_CORE_STATE_HIF_NREADY;
	//20/40/80M will all be limited to mcs7 by this feature, for usb the fw is now been hacked to limit Rx MCS only at 80M.
	core->config.up_to_mcs7 = false;
	core->config.force_rx_use_tasklet = hif_ops->hif == WQ_HIF_USB;
	if (hif_ops->hif == WQ_HIF_PCIE) {
		core->config.dma_map = true;
		core->config.rx_ll = true;
		if (hif_ops->low_speed_mode) {
			bundle_num = min((u8)NX_LL_LOW_SPEED_TX_BUNDLE_MAX,
					 (u8)wq_conf.tx_bundle_max);
		} else {
			bundle_num = wq_conf.tx_bundle_max;
		}
		core->config.tx_bundle_max =
			min((u8)NX_TX_PAYLOAD_MAX, bundle_num);
		core->config.tx_bundle_expire_ns =
			NSEC_PER_USEC * wq_conf.tx_bundle_expire_us;
		core->config.tx_tcpack_expire_ns =
			NSEC_PER_USEC * wq_conf.tx_tcpack_expire_us;
	} else if (hif_ops->hif == WQ_HIF_USB) {
		core->config.napi_enable = true;
		core->config.force_rx_use_tasklet = true;
	} else if (hif_ops->hif == WQ_HIF_SDIO) {
		core->config.napi_enable = true;
		core->config.force_rx_use_tasklet = true;
		core->config.ipc_rx_pkt_use_wq = true;
	}

	init_completion(&core->fw_ready);

	wq_profiling_init();

	if (wq_ipc_init(core)) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: ipc attach failed\n", __func__);
		kfree(core);
		return NULL;
	}

	if (wq_dnld_init(core)) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: wq dnld alloc failed\n",
		       __func__);
		kfree(core);
		return NULL;
	}

	this_wq_core = core;

	return core;
}
WQ_CORE_API(wq_core_create);

void wq_core_destroy(struct wq_core *core)
{
	wq_ipc_deinit(core);
	wq_dnld_deinit(core);
	kfree(core);
}
WQ_CORE_API(wq_core_destroy);

int wq_core_suspend_set(struct wq_core *core, int enable)
{
	int ret = 0;

	BUG_ON(core->state < WQ_CORE_STATE_FW_DL_DONE);

	enable = !!enable;
	core->flags.suspend = enable;

	if (core->hw) {
		ret = rwnx_send_me_set_bus_pwr_state(core->hw, enable);
		if (ret) {
			WQ_DBG(DM_TRBUS, DL_ERR, "%s: fail to %s wlan!\n",
			       __func__, enable ? "suspend" : "resume");
		}
	}

	/* FIXME: add BMI command to make DTOP suspend or not */

	return ret;
}
WQ_CORE_API(wq_core_suspend_set);

int wq_core_deep_suspend_set(struct wq_core *core, int enable)
{
	core->in_deep_suspendding = !!enable;

	return 0;
}
WQ_CORE_API(wq_core_deep_suspend_set);

int wq_core_is_in_deep_suspend(struct wq_core *core)
{
	return core->in_deep_suspendding;
}
WQ_CORE_API(wq_core_is_in_deep_suspend);

int wq_core_wait_data_path_idle(struct wq_core *core)
{
	unsigned long t = msecs_to_jiffies(3000) + jiffies;

	do {
		if (wq_ipc_msdu_tx_is_all_done(core)) {
			return 1;
		}
	} while (!time_after(jiffies, t));

	return 0;
}
WQ_CORE_API(wq_core_wait_data_path_idle);

int wq_wlan_fw_ready(struct wq_core *core, unsigned long msecs)
{
	unsigned long total;
	unsigned long rem;

	if (wq_conf.loadfw_only)
		return 0;

	total = msecs_to_jiffies(msecs);
	rem = wait_for_completion_timeout(&core->fw_ready, total);

	if (!rem) {
		WQ_ERROR(FW_NOT_READY);
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: wait device ready timeout!\n",
		       __func__);
		return -ETIMEDOUT;
	}
	WQ_DBG(DM_TRBUS, DL_ERR, "%s: device is ready in %d ms!\n", __func__,
	       jiffies_to_msecs(total - rem));
	return 0;
}
WQ_CORE_API(wq_wlan_fw_ready);

static void __maybe_unused wq_bus_recovery_process(struct work_struct *work_data)
{
	struct wq_core *core;
	struct rwnx_vif *rwnx_vif, *tmp;
	core = container_of(work_data, struct wq_core, wq_bus_recovery_work);
	rtnl_lock();
	list_for_each_entry_safe (rwnx_vif, tmp, &core->hw->vifs, list) {
		WQ_DBG(DM_TRBUS, DL_WRN, "%s\n", rwnx_vif->wdev.netdev->name);
		if (!strncmp(rwnx_vif->wdev.netdev->name, "wlan", 4)) {
			wq_subsystem_restart_evt(core->hw->wiphy,
						 &rwnx_vif->wdev, NULL, 0);
			break;
		}
	}
	rtnl_unlock();
}

void wq_wlan_handle_bus_recovery(struct wq_core *core)
{
	schedule_work(&core->wq_bus_recovery_work);
}
WQ_CORE_API(wq_wlan_handle_bus_recovery);

int wq_wlan_create(struct wq_core *core, int tx_credit_enable)
{
	int ret;

	if (wq_conf.loadfw_only) {
		wq_mp_test_proc_init(core);
		return 0;
	}

	wq_core_state_set(core, WQ_CORE_STATE_WIF_NREADY);
	ret = rwnx_cfg80211_init(core);

	WQ_DBG(DM_TRBUS, DL_WRN, "%s: hw=0x%p\n", __func__, core->hw);
	if (ret == 0) {
		wq_core_state_set(core, WQ_CORE_STATE_WIF_READY);
		rwnx_credit_enable(core->hw, tx_credit_enable);
		if (core->hif_ops->hif == WQ_HIF_PCIE) {
			core->hw->amsdu_param.max_packets_num = AMSDU_MAX_PTK_NUM_PCIE;
			core->hw->amsdu_param.max_len = RWNX_TX_TCPACK_AMSDU_LEN_MAX;
		}
		rwnx_set_txq_flow_ctrl_threshlod(
			core->hw, core->hif_ops->txq_stop_threshlod,
			core->hif_ops->txq_restart_threshlod);
	} else {
		wq_core_state_set(core, WQ_CORE_STATE_HIF_READY);
		WQ_ERROR(WIFI_ATTACH_FAIL);
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: rwnx_cfg80211_init failed(%d)\n",
		       __func__, ret);
	}

	wq_core_deep_suspend_set(core, 0);
	if (wq_conf.ps_mode & BIT(1)) {
		hif_autopm_allow(core);
	}
	INIT_WORK(&core->wq_bus_recovery_work, wq_bus_recovery_process);

	return ret;
}
WQ_CORE_API(wq_wlan_create);

int wq_wlan_suspend(struct wq_core *core)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
	if (NULL == core->hw) {
		return 0;
	}
	return rwnx_cfg80211_suspend(core->hw->wiphy,
				     core->hw->wiphy->wowlan_config);
#else
	return 0;
#endif
}
WQ_CORE_API(wq_wlan_suspend);

int wq_wlan_resume(struct wq_core *core)
{
	if (NULL == core->hw) {
		return 0;
	}
	return rwnx_cfg80211_resume(core->hw->wiphy);
}
WQ_CORE_API(wq_wlan_resume);

int wq_wlan_shutdown(struct wq_core *core)
{
	if (NULL == core->hw) {
		return 0;
	}

	rwnx_cfg80211_timer_shutdown(core->hw);

	return 0;
}
WQ_CORE_API(wq_wlan_shutdown);

void wq_wlan_unregister(struct wq_core *core)
{
	if (wq_conf.loadfw_only)
		wq_mp_test_proc_deinit(core);

	if (wq_core_is_detached(core))
		return;

	if (core->hw)
		rwnx_wdev_unregister(core->hw);

	if (core->hif_ops->hif == WQ_HIF_PCIE) {
		bmi_unload_wifi(core);
	}

	if (force_reset) {
		bmi_unload_dtop(core);
	}

	wq_core_state_set(core, WQ_CORE_STATE_HIF_NREADY);
}
WQ_CORE_API(wq_wlan_unregister);

void wq_wlan_destroy(struct wq_core *core)
{
	struct rwnx_hw *hw = core->hw;

	if (core->wq_bus_recovery_work.func)
		cancel_work_sync(&core->wq_bus_recovery_work);

	if (hw) {
		rwnx_cfg80211_deinit(hw);
		core->hw = NULL;
	}
	/* FIXME: if only unregister wlan interface, should return WQ_CORE_STATE_HIF_READY */
	wq_core_state_set(core, WQ_CORE_STATE_DETACH);
}
WQ_CORE_API(wq_wlan_destroy);

void wq_wlan_unload_wifi(struct wq_core *core)
{
	bmi_unload_wifi(core);
}
WQ_CORE_API(wq_wlan_unload_wifi);

void wq_wlan_cmd_mgr_drain(struct wq_core *core)
{
	struct rwnx_hw *hw = core->hw;
	if (hw) {
		hw->cmd_mgr.state = RWNX_CMD_MGR_STATE_DETACH;
		rwnx_cmd_mgr_drain(&hw->cmd_mgr);
	}
}
WQ_CORE_API(wq_wlan_cmd_mgr_drain);

void wq_dev_restart(struct wq_core *core)
{
	// unload dtop and restart target.
	bmi_unload_dtop(core);
}
WQ_CORE_API(wq_dev_restart);

int wq_core_get_sys_state(struct wq_core *core, u32 *run_state)
{
	return bmi_get_sys_state(core, WQ_FW_DTOP, run_state);
}
WQ_CORE_API(wq_core_get_sys_state);

int wq_core_sys_suspend(struct wq_core *core)
{
	return bmi_system_suspend(core);
}
WQ_CORE_API(wq_core_sys_suspend);

int wq_core_sys_resume(struct wq_core *core)
{
	return bmi_system_resume(core);
}
WQ_CORE_API(wq_core_sys_resume);

static int wq_module_init_cnt = 0;

int __init wq_module_init(void)
{
	if (!wq_module_init_cnt++) {
		WQ_DBG(DM_GENERIC, DL_WRN, "%s: " WQ_WLAN_BUILD "\n", __func__);
		WQ_DBG(DM_GENERIC, DL_WRN, "%s: " WQ_WLAN_VERSION "\n",
		       __func__);

		wq_config_load(NULL);
		wq_config_dump();
	}

	return 0;
}
WQ_CORE_API(wq_module_init);

void __exit wq_module_exit(void)
{
	if (--wq_module_init_cnt)
		return;

	WQ_DBG(DM_GENERIC, DL_WRN, "%s\n", __func__);
}
WQ_CORE_API(wq_module_exit);

#if !defined(WQ_WLAN_ALL_IN_ONE)

module_init(wq_module_init);
module_exit(wq_module_exit);

MODULE_AUTHOR("WuQi Technologies");
MODULE_DESCRIPTION("Core module for WuQi 802.11ax WLAN devices");
MODULE_LICENSE("Dual BSD/GPL");
#ifdef MODULE_IMPORT_NS
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
#endif

#endif
