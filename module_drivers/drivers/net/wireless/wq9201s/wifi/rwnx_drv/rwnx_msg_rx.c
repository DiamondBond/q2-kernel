/**
 ****************************************************************************************
 *
 * @file rwnx_msg_rx.c
 *
 * @brief RX function definitions
 *
 * Copyright (C) RivieraWaves 2012-2020
 *
 ****************************************************************************************
 */
#include <linux/ieee80211.h>
#include "rwnx_main.h"
#include "rwnx_defs.h"
#include "rwnx_tx.h"
#ifdef CONFIG_RWNX_BFMER
#include "rwnx_bfmer.h"
#endif //(CONFIG_RWNX_BFMER)
#include "rwnx_debugfs.h"
#include "rwnx_msg_tx.h"
#include "rwnx_tdls.h"
#include "rwnx_events.h"
#include "rwnx_compat.h"
#include "wq_profiling.h"
#include "wq_log.h"
#include "wq_wifi_dbg.h"
#include "coex.h"
#include "fw_api/wifi/mac/wowlan.h"
#include "rwnx_reg_data.h"
#include "fw_api/wifi/htc/e2a_event.h"
#include "ieee80211_extap.h"

#define TRACER_TRIGGER_REG  0x0C3001DC
#define TRACER_INFO_LINUX  "/opt/tracer_dump.bin"
#define TRACER_INFO_ANDROID  "/data/tracer_dump.bin"
#define TRACER_REG_LINUX  "/opt/tracer_reg.txt"
#define TRACER_REG_ANDROID  "/data/tracer_reg.txt"

struct wq_dbg_connect_time dbg_connect_time;

extern int rwnx_send_chan_pwr_info_req(struct rwnx_hw *rwnx_hw,
				       struct rwnx_vif *rwnx_vif, u8 *pwr,
				       u8 band, u32 freq);
extern void rwnx_store_chan_pwr_tab(struct rwnx_vif *vif, u8 band, u32 freq,
				    u8 *pwr_tab);

static int rwnx_freq_to_idx(struct rwnx_hw *rwnx_hw, int freq)
{
	struct ieee80211_supported_band *sband;
	int band, ch, idx = 0;

	for (band = NL80211_BAND_2GHZ; band < NUM_NL80211_BANDS; band++) {
		sband = rwnx_hw->wiphy->bands[band];
		if (!sband) {
			continue;
		}

		for (ch = 0; ch < sband->n_channels; ch++, idx++) {
			if (sband->channels[ch].center_freq == freq) {
				goto exit;
			}
		}
	}

	BUG_ON(1);

exit:
	// Channel has been found, return the index
	return idx;
}

/***************************************************************************
 * Messages from MM task
 **************************************************************************/
static inline int rwnx_rx_chan_pre_switch_ind(struct rwnx_hw *rwnx_hw,
					      struct rwnx_cmd *cmd,
					      struct ipc_e2a_msg *msg)
{
	struct rwnx_vif *rwnx_vif;
	int chan_idx =
		((struct mm_channel_pre_switch_ind *)msg->param)->chan_index;

	ENTER();

	PROFILING_SET(SW_PROF_CHAN_PRE_SWITCH);

	list_for_each_entry (rwnx_vif, &rwnx_hw->vifs, list) {
		if (rwnx_vif->up && rwnx_vif->ch_index == chan_idx) {
			rwnx_txq_vif_stop(rwnx_vif, RWNX_TXQ_STOP_CHAN,
					  rwnx_hw);
		}
	}

	PROFILING_CLR(SW_PROF_CHAN_PRE_SWITCH);

	return 0;
}

static inline int rwnx_rx_chan_switch_ind(struct rwnx_hw *rwnx_hw,
					  struct rwnx_cmd *cmd,
					  struct ipc_e2a_msg *msg)
{
	struct rwnx_vif *rwnx_vif;
	int chan_idx = ((struct mm_channel_switch_ind *)msg->param)->chan_index;
	bool roc_req = ((struct mm_channel_switch_ind *)msg->param)->roc;
	bool roc_tdls = ((struct mm_channel_switch_ind *)msg->param)->roc_tdls;

	ENTER();
	PROFILING_SET(SW_PROF_CHAN_SWITCH_IND);

	if (roc_tdls) {
		u8 vif_index =
			((struct mm_channel_switch_ind *)msg->param)->vif_index;
		list_for_each_entry (rwnx_vif, &rwnx_hw->vifs, list) {
			if (rwnx_vif->vif_index == vif_index) {
				rwnx_vif->roc_tdls = true;
				rwnx_txq_tdls_sta_start(
					rwnx_vif, RWNX_TXQ_STOP_CHAN, rwnx_hw);
			}
		}
	} else if (!roc_req) {
		list_for_each_entry (rwnx_vif, &rwnx_hw->vifs, list) {
			if (rwnx_vif->up && rwnx_vif->ch_index == chan_idx) {
				rwnx_txq_vif_start(rwnx_vif, RWNX_TXQ_STOP_CHAN,
						   rwnx_hw);
			}
		}
	} else {
		struct rwnx_roc *roc = rwnx_hw->roc;
		rwnx_vif = roc->vif;

		trace_switch_roc(rwnx_vif->vif_index);

		if (!roc->internal) {
			// If RoC has been started by the user space, inform it that we have
			// switched on the requested off-channel
			WQ_DBG(DM_GENERIC, DL_WRN, "ready_on_channel\n");
			cfg80211_ready_on_channel(&rwnx_vif->wdev,
						  (u64)(rwnx_hw->roc_cookie),
						  roc->chan, roc->duration,
						  GFP_ATOMIC);
		}

		// Keep in mind that we have switched on the channel
		roc->on_chan = true;
		// Enable traffic on OFF channel queue
		rwnx_txq_offchan_start(rwnx_hw);
	}

	rwnx_hw->cur_chanctx = chan_idx;
	rwnx_radar_detection_enable_on_cur_channel(rwnx_hw);

	PROFILING_CLR(SW_PROF_CHAN_SWITCH_IND);
	return 0;
}

static inline int rwnx_rx_tdls_chan_switch_cfm(struct rwnx_hw *rwnx_hw,
					       struct rwnx_cmd *cmd,
					       struct ipc_e2a_msg *msg)
{
	return 0;
}

static inline int rwnx_rx_tdls_chan_switch_ind(struct rwnx_hw *rwnx_hw,
					       struct rwnx_cmd *cmd,
					       struct ipc_e2a_msg *msg)
{
	// Enable traffic on OFF channel queue
	rwnx_txq_offchan_start(rwnx_hw);

	return 0;
}

static inline int rwnx_rx_tdls_chan_switch_base_ind(struct rwnx_hw *rwnx_hw,
						    struct rwnx_cmd *cmd,
						    struct ipc_e2a_msg *msg)
{
	struct rwnx_vif *rwnx_vif;
	u8 vif_index =
		((struct tdls_chan_switch_base_ind *)msg->param)->vif_index;

	ENTER();

	list_for_each_entry (rwnx_vif, &rwnx_hw->vifs, list) {
		if (rwnx_vif->vif_index == vif_index) {
			rwnx_vif->roc_tdls = false;
			rwnx_txq_tdls_sta_stop(rwnx_vif, RWNX_TXQ_STOP_CHAN,
					       rwnx_hw);
		}
	}
	return 0;
}

static inline int rwnx_rx_tdls_peer_ps_ind(struct rwnx_hw *rwnx_hw,
					   struct rwnx_cmd *cmd,
					   struct ipc_e2a_msg *msg)
{
	struct rwnx_vif *rwnx_vif;
	u8 vif_index = ((struct tdls_peer_ps_ind *)msg->param)->vif_index;
	bool ps_on = ((struct tdls_peer_ps_ind *)msg->param)->ps_on;

	list_for_each_entry (rwnx_vif, &rwnx_hw->vifs, list) {
		if (rwnx_vif->vif_index == vif_index) {
			rwnx_vif->sta.tdls_sta->tdls.ps_on = ps_on;
			// Update PS status for the TDLS station
			rwnx_ps_bh_enable(rwnx_hw, rwnx_vif->sta.tdls_sta,
					  ps_on);
		}
	}

	return 0;
}

static inline int rwnx_rx_remain_on_channel_exp_ind(struct rwnx_hw *rwnx_hw,
						    struct rwnx_cmd *cmd,
						    struct ipc_e2a_msg *msg)
{
	struct rwnx_roc *roc;
	struct rwnx_vif *rwnx_vif;

	ENTER();
	roc = rwnx_hw->roc;
	if (!roc)
		return 0;
	rwnx_vif = roc->vif;

	trace_roc_exp(rwnx_vif->vif_index);

	WQ_DBG(DM_GENERIC, DL_WRN, "roc exp internal=%d on_chan=%d\n",
	       roc->internal, roc->on_chan);

	del_timer(&rwnx_hw->roc_timer);

	if (!roc->internal && roc->on_chan) {
		// If RoC has been started by the user space and hasn't been cancelled,
		// inform it that off-channel period has expired
		cfg80211_remain_on_channel_expired(&rwnx_vif->wdev,
						   (u64)(rwnx_hw->roc_cookie),
						   roc->chan, GFP_ATOMIC);
	}

	rwnx_txq_offchan_deinit(rwnx_vif);

	// Increase the cookie (cannot be zero)
	rwnx_hw->roc_cookie++;
	if (rwnx_hw->roc_cookie == 0)
		rwnx_hw->roc_cookie = 1;

	kfree(roc);
	rwnx_hw->roc = NULL;
	complete(&rwnx_hw->roc_wait);
	mutex_unlock(&rwnx_hw->mutex);
	return 0;
}

void rwnx_roc_timeout(struct timer_list *t)
{
	struct rwnx_hw *rwnx_hw = from_timer(rwnx_hw, t, roc_timer);

	//schedule Remain on channel timeout, delete mutex lock
	WQ_DBG(DM_IPC, DL_WRN,
	       "Remain on channel duration exp, send EXP IND to Host\n");
	rwnx_rx_remain_on_channel_exp_ind(rwnx_hw, NULL, NULL);
}

static inline int rwnx_rx_p2p_vif_ps_change_ind(struct rwnx_hw *rwnx_hw,
						struct rwnx_cmd *cmd,
						struct ipc_e2a_msg *msg)
{
	int vif_idx =
		((struct mm_p2p_vif_ps_change_ind *)msg->param)->vif_index;
	int ps_state =
		((struct mm_p2p_vif_ps_change_ind *)msg->param)->ps_state;

	struct rwnx_vif *vif_entry;

	//ENTER();
	WQ_DBG(DM_GENERIC, DL_WRN, "[%d]p2p ps_mode %s.\n", vif_idx,
	       (ps_state == PS_MODE_OFF) ? "OFF" : "ON");

	vif_entry = rwnx_hw->vif_table[vif_idx];

	if (vif_entry) {
		goto found_vif;
	}

	goto exit;

found_vif:

	if (ps_state == PS_MODE_OFF) {
		// Start TX queues for provided VIF
		rwnx_txq_vif_start(vif_entry, RWNX_TXQ_STOP_VIF_PS, rwnx_hw);
	} else {
		// Stop TX queues for provided VIF
		rwnx_txq_vif_stop(vif_entry, RWNX_TXQ_STOP_VIF_PS, rwnx_hw);
	}

exit:
	return 0;
}

static inline int rwnx_rx_channel_survey_ind(struct rwnx_hw *rwnx_hw,
					     struct rwnx_cmd *cmd,
					     struct ipc_e2a_msg *msg)
{
	struct mm_channel_survey_ind *ind =
		(struct mm_channel_survey_ind *)msg->param;
	// Get the channel index
	int idx = rwnx_freq_to_idx(rwnx_hw, ind->freq);
	// Get the survey
	struct rwnx_survey_info *rwnx_survey;

	ENTER();

	if (idx > ARRAY_SIZE(rwnx_hw->survey))
		return 0;

	rwnx_survey = &rwnx_hw->survey[idx];

	// Store the received parameters
	rwnx_survey->chan_time_ms = ind->chan_time_ms;
	rwnx_survey->chan_time_busy_ms = ind->chan_time_busy_ms;
	rwnx_survey->noise_dbm = ind->noise_dbm;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
	rwnx_survey->filled = (SURVEY_INFO_TIME | SURVEY_INFO_TIME_BUSY);
#else
	rwnx_survey->filled =
		(SURVEY_INFO_CHANNEL_TIME | SURVEY_INFO_CHANNEL_TIME_BUSY);
#endif

	if (ind->noise_dbm != 0) {
		rwnx_survey->filled |= SURVEY_INFO_NOISE_DBM;
	}

	return 0;
}

static inline int rwnx_rx_p2p_noa_upd_ind(struct rwnx_hw *rwnx_hw,
					  struct rwnx_cmd *cmd,
					  struct ipc_e2a_msg *msg)
{
	return 0;
}

static inline int rwnx_rx_rssi_status_ind(struct rwnx_hw *rwnx_hw,
					  struct rwnx_cmd *cmd,
					  struct ipc_e2a_msg *msg)
{
	struct mm_rssi_status_ind *ind =
		(struct mm_rssi_status_ind *)msg->param;
	int vif_idx = ind->vif_index;
	bool rssi_status = ind->rssi_status;

	struct rwnx_vif *vif_entry;

	ENTER();

	WQ_DBG(DM_GENERIC, DL_WRN, "rwnx_rx_rssi_status_ind %d", ind->rssi);

	vif_entry = rwnx_hw->vif_table[vif_idx];
	if (vif_entry) {
		cfg80211_cqm_rssi_notify(
			vif_entry->ndev,
			rssi_status ? NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW :
					    NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH,
			ind->rssi, GFP_ATOMIC);
	}

	return 0;
}

static inline int rwnx_rx_pktloss_notify_ind(struct rwnx_hw *rwnx_hw,
					     struct rwnx_cmd *cmd,
					     struct ipc_e2a_msg *msg)
{
	struct mm_pktloss_ind *ind = (struct mm_pktloss_ind *)msg->param;
	struct rwnx_vif *vif_entry;
	int vif_idx = ind->vif_index;

	ENTER();

	vif_entry = rwnx_hw->vif_table[vif_idx];
	if (vif_entry) {
		cfg80211_cqm_pktloss_notify(vif_entry->ndev,
					    (const u8 *)&ind->mac_addr,
					    ind->num_packets, GFP_ATOMIC);
	}

	return 0;
}

static inline int rwnx_rx_nss_update_ind(struct rwnx_hw *rwnx_hw,
					 struct rwnx_cmd *cmd,
					 struct ipc_e2a_msg *msg)
{
	struct mm_nss_update_ind *ind = (struct mm_nss_update_ind *)msg->param;
	uint8_t nss = ind->nss;
	struct rwnx_vif *vif;

	ENTER();

	if (rwnx_hw->mod_params.nss == nss) {
		WQ_DBG(DM_GENERIC, DL_WRN,
		       "rwnx_rx_nss_update_ind idx: nss not changed");
		return 0;
	}
	rwnx_hw->mod_params.nss = nss;
	rwnx_hw->version_cfm.nss = nss;
	rwnx_hw->mod_params.use_80 = (nss == 2) ? true : false;

	// Look for VIF entry
	list_for_each_entry (vif, &rwnx_hw->vifs, list) {
		if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_AP ||
		    RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_GO) {
			vif->ap.dbdc_mode = ind->mode;
			vif->ap.nss_idx = vif->vif_index;
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "%s, update nss: %d, mode is %d", __func__, nss,
			       ind->mode);
			schedule_work(&rwnx_hw->update_nss_task);
		}
	}

	return 0;
}

static inline int rwnx_rx_coex_info_update_ind(struct rwnx_hw *rwnx_hw,
					       struct rwnx_cmd *cmd,
					       struct ipc_e2a_msg *msg)
{
	ENTER();
	coex_msg_parse((struct mm_coex_info_upd *)msg->param);

	return 0;
}

static inline int rwnx_rx_csa_counter_ind(struct rwnx_hw *rwnx_hw,
					  struct rwnx_cmd *cmd,
					  struct ipc_e2a_msg *msg)
{
	struct mm_csa_counter_ind *ind =
		(struct mm_csa_counter_ind *)msg->param;
	struct rwnx_vif *vif;
	bool found = false;

	ENTER();

	// Look for VIF entry
	list_for_each_entry (vif, &rwnx_hw->vifs, list) {
		if (vif->vif_index == ind->vif_index) {
			found = true;
			break;
		}
	}

	if (found) {
		if (vif->ap.csa)
			vif->ap.csa->count = ind->csa_count;
		else
			netdev_err(vif->ndev,
				   "CSA counter update but no active CSA");
	}

	return 0;
}

void rwnx_disconnect_task(struct work_struct *w)
{
	struct rwnx_hw *rwnx_hw =
		container_of(w, struct rwnx_hw, disconnect_task);
	struct rwnx_vif *rwnx_vif = rwnx_hw->csa_vif;

	if ((rwnx_vif != NULL) &&
	    (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION)) {
		if ((rwnx_vif->up == true) &&
		    (rwnx_vif->b_disconnecting == false) &&
		    (rwnx_vif->sta.ap != NULL) && (rwnx_vif->sta.ap->valid)) {
			WQ_DBG(DM_GENERIC, DL_WRN, "%s:disconnect vid=%d\n",
			       __func__, rwnx_vif->vif_index);

			rwnx_send_sm_disconnect_req(rwnx_hw, rwnx_vif,
						    WLAN_REASON_DEAUTH_LEAVING);
		}
	}
}

static inline int rwnx_rx_csa_finish_ind(struct rwnx_hw *rwnx_hw,
					 struct rwnx_cmd *cmd,
					 struct ipc_e2a_msg *msg)
{
	struct mm_csa_finish_ind *ind = (struct mm_csa_finish_ind *)msg->param;
	struct rwnx_vif *vif;
	bool found = false;

	ENTER();

	// Look for VIF entry
	list_for_each_entry (vif, &rwnx_hw->vifs, list) {
		if (vif->vif_index == ind->vif_index) {
			found = true;
			break;
		}
	}

	if (found) {
		if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_AP ||
		    RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_GO) {
			if (vif->ap.csa) {
				vif->ap.csa->status = ind->status;
				vif->ap.csa->ch_idx = ind->chan_idx;
				schedule_work(&vif->ap.csa->work);
			} else
				netdev_err(
					vif->ndev,
					"CSA finish indication but no active CSA");
		} else {
			if (ind->status == 0) {
				rwnx_chanctx_unlink(vif);
				rwnx_chanctx_link(vif, ind->chan_idx, NULL);
				if (rwnx_hw->cur_chanctx == ind->chan_idx) {
					rwnx_radar_detection_enable_on_cur_channel(
						rwnx_hw);
					rwnx_txq_vif_start(vif,
							   RWNX_TXQ_STOP_CHAN,
							   rwnx_hw);
				} else
					rwnx_txq_vif_stop(vif,
							  RWNX_TXQ_STOP_CHAN,
							  rwnx_hw);
			}
			rwnx_hw->csa_vif = vif;
			schedule_work(&rwnx_hw->disconnect_task);
		}
	}

	return 0;
}

static inline int rwnx_rx_csa_traffic_ind(struct rwnx_hw *rwnx_hw,
					  struct rwnx_cmd *cmd,
					  struct ipc_e2a_msg *msg)
{
	struct mm_csa_traffic_ind *ind =
		(struct mm_csa_traffic_ind *)msg->param;
	struct rwnx_vif *vif;
	bool found = false;

	ENTER();

	// Look for VIF entry
	list_for_each_entry (vif, &rwnx_hw->vifs, list) {
		if (vif->vif_index == ind->vif_index) {
			found = true;
			break;
		}
	}

	if (found) {
		if (ind->enable)
			rwnx_txq_vif_start(vif, RWNX_TXQ_STOP_CSA, rwnx_hw);
		else
			rwnx_txq_vif_stop(vif, RWNX_TXQ_STOP_CSA, rwnx_hw);
	}

	return 0;
}

static inline int rwnx_rx_ps_change_ind(struct rwnx_hw *rwnx_hw,
					struct rwnx_cmd *cmd,
					struct ipc_e2a_msg *msg)
{
	struct mm_ps_change_ind *ind = (struct mm_ps_change_ind *)msg->param;
	struct rwnx_sta *sta = &rwnx_hw->sta_table[ind->sta_idx];

	ENTER();

	if (ind->sta_idx >= (NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX)) {
		wiphy_err(rwnx_hw->wiphy,
			  "Invalid sta index reported by fw %d\n",
			  ind->sta_idx);
		return 1;
	}

	netdev_dbg(rwnx_hw->vif_table[sta->vif_idx]->ndev,
		   "Sta %d, change PS mode to %s", sta->sta_idx,
		   ind->ps_state ? "ON" : "OFF");

	WQ_DBG(DM_GENERIC, DL_WRN,
	       "Sta %d, MAC %02x%02x%02x%02x%02x%02x, change PS mode to %s, %d",
	       sta->sta_idx, sta->mac_addr[0], sta->mac_addr[1],
	       sta->mac_addr[2], sta->mac_addr[3], sta->mac_addr[4],
	       sta->mac_addr[5], ind->ps_state ? "ON" : "OFF", sta->valid);

	if (sta->valid) {
		rwnx_ps_bh_enable(rwnx_hw, sta, ind->ps_state);
	} else if (test_bit(RWNX_DEV_ADDING_STA, &rwnx_hw->flags)) {
		sta->ps.active = ind->ps_state ? true : false;
	} else {
		netdev_err(rwnx_hw->vif_table[sta->vif_idx]->ndev,
			   "Ignore PS mode change on invalid sta\n");
	}

	return 0;
}

static inline int rwnx_rx_traffic_req_ind(struct rwnx_hw *rwnx_hw,
					  struct rwnx_cmd *cmd,
					  struct ipc_e2a_msg *msg)
{
	struct mm_traffic_req_ind *ind =
		(struct mm_traffic_req_ind *)msg->param;
	struct rwnx_sta *sta = &rwnx_hw->sta_table[ind->sta_idx];

	ENTER();

	netdev_dbg(rwnx_hw->vif_table[sta->vif_idx]->ndev,
		   "Sta %d, asked for %d pkt", sta->sta_idx, ind->pkt_cnt);

	rwnx_ps_bh_traffic_req(rwnx_hw, sta, ind->pkt_cnt,
			       ind->uapsd ? UAPSD_ID : LEGACY_PS_ID);

	return 0;
}

/***************************************************************************
 * Messages from SCANU task
 **************************************************************************/
static inline int rwnx_rx_scanu_start_cfm(struct rwnx_hw *rwnx_hw,
					  struct rwnx_cmd *cmd,
					  struct ipc_e2a_msg *msg)
{
	ENTER();

	WQ_DBG(DM_GENERIC, DL_WRN, "scanu start cfm %p\n",
	       rwnx_hw->scan_request);

	del_timer_sync(&rwnx_hw->scan_timer);

	if (rwnx_hw->scan_request) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
		struct cfg80211_scan_info info = {
			.aborted = false,
		};

		cfg80211_scan_done(rwnx_hw->scan_request, &info);
#else
		cfg80211_scan_done(rwnx_hw->scan_request, false);
#endif

		mutex_unlock(&rwnx_hw->mutex);
	}

	rwnx_hw->scan_request = NULL;

	return 0;
}

static inline struct cfg80211_bss *__must_check cfg80211_inform_bss_frame_patch(
	struct wiphy *wiphy, struct ieee80211_channel *rx_channel,
	struct ieee80211_mgmt *mgmt, size_t len, s32 signal, gfp_t gfp)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
	struct cfg80211_inform_bss data = {
		.chan = rx_channel,
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
		.scan_width = NL80211_BSS_CHAN_WIDTH_20,
#endif
		.signal = signal,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
		.boottime_ns = ktime_get_boottime_ns(),
#else
		.boottime_ns = ktime_get_boot_ns(),
#endif
	};

	return cfg80211_inform_bss_frame_data(wiphy, &data, mgmt, len, gfp);
#else
	return cfg80211_inform_bss_frame(wiphy, rx_channel, mgmt, len, signal,
					 gfp);
#endif
}

static inline int rwnx_rx_scanu_result_ind(struct rwnx_hw *rwnx_hw,
					   struct rwnx_cmd *cmd,
					   struct ipc_e2a_msg *msg)
{
	struct cfg80211_bss *bss = NULL;
	struct ieee80211_channel *chan;
	struct ieee80211_mgmt *mgmt;
	struct timespec64 tv;
	struct scanu_result_ind *ind = (struct scanu_result_ind *)msg->param;
	char ssid[33];
	int ssid_len;
	int i;
	unsigned char *frame;
	u8 cap_info;

	//ENTER();

	chan = ieee80211_get_channel(rwnx_hw->wiphy, ind->center_freq);

	frame = (unsigned char *)&ind->payload[0];
	cap_info = frame[35];
	ssid_len = frame[37];
	if (ssid_len <= 32) {
		for (i = 0; i < ssid_len; i++) {
			if ((frame[38 + i] < 32) || (frame[38 + i] > 126))
				ssid[i] = '*';
			else
				ssid[i] = frame[38 + i];
		}
		ssid[i] = 0;
	} else {
		ssid_len = 0;
		strcpy(ssid, "err : ssid len > 32");
	}

	WQ_DBG(DM_GENERIC, DL_WRN,
	       "scanu_result_ind: \"%s\" %d %d cap_info = %d\n", ssid,
	       ind->center_freq, ind->rssi, cap_info);

	mgmt = (struct ieee80211_mgmt *)ind->payload;
	if (!mgmt->u.probe_resp.timestamp) {
		ktime_get_real_ts64(&tv);
		mgmt->u.probe_resp.timestamp =
			(WQ_GET_TIME_SEC(tv) * 1000000) + WQ_GET_TIME_MSEC(tv);
	}

	if (chan != NULL)
		bss = cfg80211_inform_bss_frame_patch(
			rwnx_hw->wiphy, chan,
			(struct ieee80211_mgmt *)ind->payload, ind->length,
			ind->rssi * 100, GFP_ATOMIC);

	if (bss != NULL)
		cfg80211_put_bss(rwnx_hw->wiphy, bss);

	return 0;
}

/***************************************************************************
 * Messages from ME task
 **************************************************************************/
static inline int rwnx_rx_me_tkip_mic_failure_ind(struct rwnx_hw *rwnx_hw,
						  struct rwnx_cmd *cmd,
						  struct ipc_e2a_msg *msg)
{
	struct me_tkip_mic_failure_ind *ind =
		(struct me_tkip_mic_failure_ind *)msg->param;
	struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[ind->vif_idx];
	struct net_device *dev = rwnx_vif->ndev;

	ENTER();

	rwnx_vif->tkip_mic_failure_count++;

	cfg80211_michael_mic_failure(dev, (u8 *)&ind->addr,
				     (ind->ga ? NL80211_KEYTYPE_GROUP :
						      NL80211_KEYTYPE_PAIRWISE),
				     ind->keyid, (u8 *)&ind->tsc, GFP_ATOMIC);

	return 0;
}

static inline int rwnx_rx_me_tx_credits_update_ind(struct rwnx_hw *rwnx_hw,
						   struct rwnx_cmd *cmd,
						   struct ipc_e2a_msg *msg)
{
	struct me_tx_credits_update_ind *ind =
		(struct me_tx_credits_update_ind *)msg->param;

	ENTER();

	rwnx_txq_credit_update(rwnx_hw, ind->sta_idx, ind->tid, ind->credits);

	return 0;
}

static inline int rwnx_rx_me_wow_resume_ind(struct rwnx_hw *rwnx_hw,
					    struct rwnx_cmd *cmd,
					    struct ipc_e2a_msg *msg)
{
	struct me_wow_resume_ind *ind = (struct me_wow_resume_ind *)msg->param;
	struct sk_buff *skb = NULL;
	struct rwnx_vif *rwnx_vif;
	ENTER();

	dump_bytes(DL_WRN, "wakeup frame:", ind->frame, ind->frame_len);

	skb = dev_alloc_skb(ind->frame_len);
	BUG_ON(!skb);
	rwnx_vif = rwnx_hw->vif_table[ind->vif_idx];

	skb->dev = rwnx_vif->ndev;
	memcpy(skb_put(skb, ind->frame_len), ind->frame, ind->frame_len);
	skb->protocol = eth_type_trans(skb, skb->dev);

	if (irq_count())
		netif_receive_skb(skb);
	else
		netif_rx(skb);

	return 0;
}

static inline int rwnx_rx_me_extend_free_host_data_ring_now_ind(
	struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct me_extend_free_host_data_ring_now_ind *ind =
		(struct me_extend_free_host_data_ring_now_ind *)msg->param;
	struct rwnx_rx_ll *rx_ll;
	struct rx_free_msg_info *free_msg;
	u8 mac_id;
	bool is_backup_ring_ind = ind->is_backup_ring;

	rx_ll = &rwnx_hw->rx_ll;

	spin_lock(&rx_ll->rx_free_msg_lock);
	for (mac_id = 0; mac_id < 2; mac_id++) {
		free_msg = &rx_ll->rx_free_msg_env[mac_id];
		if (ind) {
			free_msg->last_read_offset = ind->last_buf_idx[mac_id];
		}
		WQ_DBG(DM_RX, DL_WRN,
		       "mac_id:%d/%d received free host data ring now indication, "
		       "last/cur:0x%x/0x%x, free:[0x%x,0x%x], flag:[%d,%d]\n",
		       mac_id, is_backup_ring_ind, free_msg->last_read_offset,
		       free_msg->read_offset, free_msg->rx_free_idx[0],
		       free_msg->rx_free_idx[1], free_msg->send_flag[0],
		       free_msg->send_flag[1]);
		if (free_msg->last_read_offset == free_msg->read_offset) {
			if (!free_msg->send_flag[is_backup_ring_ind]) {
				free_msg->send_flag[is_backup_ring_ind] = true;
				free_msg->rx_free_idx[is_backup_ring_ind] =
					free_msg->read_offset &
					HOST_DATA_RING_WRAP_FLAG_CLEAR_MASK;
				del_timer(&free_msg->rx_free_msg_timer);
				WQ_DBG(DM_RX, DL_WRN,
				       "free host data ring(ind):mac%d:0x%x",
				       free_msg->macid, free_msg->read_offset);
				rwnx_send_free_host_ring_req(
					rwnx_hw, free_msg->macid,
					free_msg->read_offset,
					is_backup_ring_ind);
			}
		} else {
			free_msg->fw_recoverying = true;
		}
	}
	spin_unlock(&rx_ll->rx_free_msg_lock);
	return 0;
}

void rwnx_set_sta_amsdu_len_from_htcap(struct rwnx_sta *sta, u16 htcap_info)
{
	switch (htcap_info & IEEE80211_HT_CAP_MAX_AMSDU) {
	case 0:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
		sta->max_amsdu_len = IEEE80211_MAX_MPDU_LEN_HT_3839;
#endif
		break;
	case IEEE80211_HT_CAP_MAX_AMSDU:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
		sta->max_amsdu_len = IEEE80211_MAX_MPDU_LEN_HT_7935;
#endif
		break;
	}
	WQ_DBG(DM_GENERIC, DL_ERR,
	       "%s ta_idx=%d, cap:%d, sta->max_amsdu_len =%d\n", __func__,
	       sta->sta_idx, htcap_info, sta->max_amsdu_len);
}

void rwnx_set_sta_amsdu_len_from_vhtcap(struct rwnx_sta *sta, u16 vhtcap_info)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
	switch (vhtcap_info & IEEE80211_VHT_CAP_MAX_MPDU_MASK) {
	case IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991:
		sta->max_amsdu_len = IEEE80211_MAX_MPDU_LEN_VHT_7991;
		break;
	case IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454:
		sta->max_amsdu_len = IEEE80211_MAX_MPDU_LEN_VHT_11454;
		break;
	case IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_3895:
	default:
		sta->max_amsdu_len = IEEE80211_MAX_MPDU_LEN_VHT_3895;
		break;
	}
#endif
	WQ_DBG(DM_GENERIC, DL_ERR,
	       "%s ta_idx=%d, cap:%d, sta->max_amsdu_len =%d\n", __func__,
	       sta->sta_idx, vhtcap_info, sta->max_amsdu_len);
}

static void rwnx_get_amsdu_num(struct rwnx_sta *sta, const u8 *rsp_ie,
			       u16 assoc_rsp_ie_len)
{
	const struct ieee80211_ht_cap *ht_cap;
	const u8 *vht_capa_ie;
	const u8 *ht_cap_ie;
	const struct ieee80211_vht_cap *vht_cap;
	u32 cap_info = 0;

	WQ_DBG(DM_GENERIC, DL_ERR, "%s:assoc succ sta_idx=%d\n", __func__,
	       sta->sta_idx);
	sta->max_amsdu_len = 0;
	ht_cap_ie = cfg80211_find_ie(WLAN_EID_HT_CAPABILITY, rsp_ie,
				     assoc_rsp_ie_len);
	if (ht_cap_ie) {
		ht_cap = (const struct ieee80211_ht_cap *)(ht_cap_ie + 2);
		rwnx_set_sta_amsdu_len_from_htcap(sta, ht_cap->cap_info);
		cap_info = ht_cap->cap_info;
	}

	/* Look for VHT Capability Information Element */
	vht_capa_ie = cfg80211_find_ie(WLAN_EID_VHT_CAPABILITY, rsp_ie,
				       assoc_rsp_ie_len);
	if (vht_capa_ie) {
		vht_cap = (const struct ieee80211_vht_cap *)(vht_capa_ie + 2);
		rwnx_set_sta_amsdu_len_from_vhtcap(sta, vht_cap->vht_cap_info);
		cap_info = vht_cap->vht_cap_info;
	}

	WQ_DBG(DM_GENERIC, DL_ERR,
	       "%s assoc succ sta_idx=%d, hecap:%p, vhtcap:%p, "
	       "capinfo:%d, last sta->max_amsdu_len =%d\n",
	       __func__, sta->sta_idx, ht_cap_ie, vht_capa_ie, cap_info,
	       sta->max_amsdu_len);
}

u8 vht_mcs_map_to_mcs_val(u8 vht_mcs_map)
{
	u8 mcs_val = 0;

	switch (vht_mcs_map) {
	case IEEE80211_VHT_MCS_NOT_SUPPORTED:
		break;
	case IEEE80211_VHT_MCS_SUPPORT_0_7:
		mcs_val = 7;
		break;
	case IEEE80211_VHT_MCS_SUPPORT_0_8:
		mcs_val = 8;
		break;
	case IEEE80211_VHT_MCS_SUPPORT_0_9:
		mcs_val = 9;
		break;
	default:
		break;
	}

	return mcs_val;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
u8 he_mcs_map_to_mcs_max(u8 he_mcs_map)
{
	switch (he_mcs_map) {
	case IEEE80211_HE_MCS_NOT_SUPPORTED:
		return 0;
	case IEEE80211_HE_MCS_SUPPORT_0_7:
		return 7;
	case IEEE80211_HE_MCS_SUPPORT_0_9:
		return 9;
	case IEEE80211_HE_MCS_SUPPORT_0_11:
		return 11;
	default:
		break;
	}
	return 0;
}
#endif

void rwnx_bcn_change_task(struct work_struct *w)
{
	struct rwnx_hw *rwnx_hw =
		container_of(w, struct rwnx_hw, bcn_change_task);
	struct rwnx_vif *vifs = NULL;

	list_for_each_entry (vifs, &rwnx_hw->vifs, list) {
		if ((RWNX_VIF_TYPE(vifs) == NL80211_IFTYPE_AP) &&
		    (vifs->ap.flags & RWNX_AP_STARTED)) {
			WQ_DBG(DM_IPC, DL_WRN, "Send bcn change to FW\n");
			/* SAP follow STA channel to send BCN change */
			rwnx_send_ch_switch(rwnx_hw, vifs, vifs->ap.chandef);
		}
	}
}

void write_filesystem(struct file *file, loff_t *offset,
	char* data, unsigned int size)
{
	int ret = 0;

#if (KERNEL_VERSION(4, 1, 0) > LINUX_VERSION_CODE)
	if (file->f_op->write)
		ret  = file->f_op->write(file, data, size, offset);
	else
		WQ_DBG(DM_GENERIC, DL_WRN, "no file write method\n");
#elif (KERNEL_VERSION(4, 10, 0) <= LINUX_VERSION_CODE)
	ret  = kernel_write(file, data, size, offset);
#else
	ret  = __vfs_write(file, data, size, offset);
#endif

	if (ret < 0)
		WQ_DBG(DM_GENERIC, DL_WRN, " Failed to write into File\n");
	else
		WQ_DBG(DM_GENERIC, DL_WRN, " Successfuled to write into File\n");
}

void rwnx_tracer_dump_task(struct work_struct *w)
{
	struct rwnx_hw *rwnx_hw =
		container_of(w, struct rwnx_hw, tracer_dump_task);

	char fileName_reg[64];
	char fileName_bin[64];
	struct file *file_w;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	mm_segment_t orig_fs;
#endif
	u32 tracer_reg;
	char s_tracer_reg[11] = {0};
	int i;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	orig_fs = get_fs();
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0))
	set_fs(KERNEL_DS);
#else
	set_fs(get_ds());
#endif
#endif

	if (rwnx_hw->mod_params.android_platform) {
		snprintf(fileName_reg, sizeof(fileName_reg), TRACER_REG_ANDROID);
		snprintf(fileName_bin, sizeof(fileName_bin), TRACER_INFO_ANDROID);
	} else {
		snprintf(fileName_reg, sizeof(fileName_reg), TRACER_REG_LINUX);
		snprintf(fileName_bin, sizeof(fileName_bin), TRACER_INFO_LINUX);
	}
	//1:tracer reg
	file_w = filp_open(fileName_reg, O_WRONLY | O_CREAT, 0664);

	if (IS_ERR(file_w)) {
		WQ_DBG(DM_GENERIC, DL_WRN, "-->2) %s: Error %ld opening %s\n",
			__func__, -PTR_ERR(file_w), fileName_reg);
	} else {
		if (file_w->f_op)
			file_w->f_pos = 0;
		else {
			filp_close(file_w, NULL);
			goto done;
		}

		WQ_DBG(DM_GENERIC, DL_WRN, "%s open success\n", fileName_reg);
		WQ_DBG(DM_GENERIC, DL_WRN, "TRACER_TRIGGER_REG: val 0x%08x\n",
			rwnx_read_reg32(rwnx_hw, TRACER_TRIGGER_REG));
		tracer_reg = rwnx_read_reg32(rwnx_hw, TRACER_TRIGGER_REG);
		snprintf(s_tracer_reg, 11, "0x%08x", tracer_reg);
		write_filesystem(file_w, &file_w->f_pos, s_tracer_reg, 10);

		filp_close(file_w, NULL);
		WQ_DBG(DM_GENERIC, DL_WRN, "%s write done\n",fileName_reg);
	}

	//2:tracer bin
	file_w = filp_open(fileName_bin, O_WRONLY | O_CREAT, 0664);

	if (IS_ERR(file_w)) {
		WQ_DBG(DM_GENERIC, DL_WRN, "-->2) %s: Error %ld opening %s\n",
			__func__, -PTR_ERR(file_w), fileName_bin);
	} else {
		if (file_w->f_op)
			file_w->f_pos = 0;
		else {
			filp_close(file_w, NULL);
			goto done;
		}

		WQ_DBG(DM_GENERIC, DL_WRN, "%s open success\n",fileName_bin);

		for (i = 0; i < NUM_EVENT_OF_TRACER_DUMP; i++) {
			write_filesystem(file_w, &file_w->f_pos,
				(char*)rwnx_hw->tracer.payload[i], 1024);
		}
		filp_close(file_w, NULL);
		WQ_DBG(DM_GENERIC, DL_WRN, "%s write done\n",fileName_bin);
	}

done:
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	set_fs(orig_fs);
#endif
	return;
}

/***************************************************************************
 * Messages from SM task
 **************************************************************************/
static inline int rwnx_rx_sm_connect_ind(struct rwnx_hw *rwnx_hw,
					 struct rwnx_cmd *cmd,
					 struct ipc_e2a_msg *msg)
{
	struct sm_connect_ind *ind = (struct sm_connect_ind *)msg->param;
	struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[ind->vif_idx];
	struct net_device *dev = rwnx_vif->ndev;
	const u8 *req_ie, *rsp_ie;
	const u8 *extcap_ie;
	const struct ieee_types_extcap *extcap;
	struct rwnx_sta *sta = NULL;
#ifdef DEBUG_WQ_DFX
	const struct ieee80211_ht_cap *ht_cap;
	const struct ieee80211_vht_cap *vht_cap;
#endif

	ENTER();

	/* Retrieve IE addresses and lengths */
	req_ie = (const u8 *)ind->assoc_ie_buf;
	rsp_ie = req_ie + ind->assoc_req_ie_len;

	// Fill-in the AP information
	if (ind->status_code == 0) {
		u8 txq_status;
		struct ieee80211_channel *chan;
		struct cfg80211_chan_def chandef;

		sta = &rwnx_hw->sta_table[ind->ap_idx];
		sta->valid = true;
		sta->sta_idx = ind->ap_idx;
		sta->ch_idx = ind->ch_idx;
		sta->vif_idx = ind->vif_idx;
		sta->vlan_idx = sta->vif_idx;
		sta->qos = ind->qos;
		sta->acm = ind->acm;
		sta->ps.active = false;
		sta->aid = ind->aid;
		sta->band = ind->chan.band;
		sta->width = ind->chan.type;
		sta->center_freq = ind->chan.prim20_freq;
		sta->center_freq1 = ind->chan.center1_freq;
		sta->center_freq2 = ind->chan.center2_freq;
		rwnx_vif->sta.ap = sta;
		rwnx_vif->generation++;
		chan = ieee80211_get_channel(rwnx_hw->wiphy,
					     ind->chan.prim20_freq);
		cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_NO_HT);
		if (!rwnx_hw->mod_params.ht_on)
			chandef.width = NL80211_CHAN_WIDTH_20_NOHT;
		else
			chandef.width = chnl2bw[ind->chan.type];
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0) //TODO_need recheck
		dbg_connect_time.connect_time[sta->band][sta->width] =
			get_seconds();
#else
		dbg_connect_time.connect_time[sta->band][sta->width] =
			ktime_get_seconds();
#endif
		chandef.center_freq1 = ind->chan.center1_freq;
		chandef.center_freq2 = ind->chan.center2_freq;
		rwnx_chanctx_link(rwnx_vif, ind->ch_idx, &chandef);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
		ether_addr_copy(sta->mac_addr, (const u8 *)&ind->bssid);
#else
		(void)memcpy(sta->mac_addr, (const u8 *)&ind->bssid, ETH_ALEN);
#endif
		if (ind->ch_idx == rwnx_hw->cur_chanctx) {
			txq_status = 0;
		} else {
			txq_status = RWNX_TXQ_STOP_CHAN;
		}
		memcpy(sta->ac_param, ind->ac_param, sizeof(sta->ac_param));
		rwnx_renew_dev_credit_mapping(
			rwnx_hw, sta->qos, rwnx_vif->crdt_gid, sta->ac_param);
		rwnx_reset_sta_stats(sta);
		rwnx_txq_sta_init(rwnx_hw, sta, txq_status);
		rwnx_dbgfs_register_sta(rwnx_hw, sta);
		rwnx_txq_tdls_vif_init(rwnx_vif);
		rwnx_mu_group_sta_init(sta, NULL);
		/* Look for TDLS Channel Switch Prohibited flag in the Extended Capability
         * Information Element*/
		extcap_ie = cfg80211_find_ie(WLAN_EID_EXT_CAPABILITY, rsp_ie,
					     ind->assoc_rsp_ie_len);
		if (extcap_ie && extcap_ie[1] >= 5) {
			extcap = (void *)(extcap_ie);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
			rwnx_vif->tdls_chsw_prohibited =
				extcap->ext_capab[4] &
				WLAN_EXT_CAPA5_TDLS_CH_SW_PROHIBITED;
#endif
		}

		rwnx_get_amsdu_num(sta, rsp_ie, ind->assoc_rsp_ie_len);
		sta->rx_nss = 0;
		sta->format_mod = 0;
#ifdef DEBUG_WQ_DFX
#define WQ_EID_EXT_HE_CAPA 35
		extcap_ie = cfg80211_find_ie(WLAN_EID_HT_CAPABILITY, rsp_ie,
					     ind->assoc_rsp_ie_len);
		if (extcap_ie && extcap_ie[1] >= sizeof(*ht_cap)) {
			ht_cap = (void *)(extcap_ie + 2);

			sta->ht_cap_info = ht_cap->cap_info;
			// Set HT MCS and NSS info
			if (sta->ht_cap_info) {
				sta->format_mod = FORMATMOD_HT_MF;
				if (ht_cap->mcs.rx_mask[0] & 0xFF)
					sta->rx_mcs_idx = 7;
				if (ht_cap->mcs.rx_mask[0])
					sta->rx_nss++;
				if (ht_cap->mcs.rx_mask[1])
					sta->rx_nss++;
				if (ht_cap->mcs.rx_mask[2])
					sta->rx_nss++;
				if (ht_cap->mcs.rx_mask[3])
					sta->rx_nss++;
				sta->rx_nss = min_t(int, sta->rx_nss,
						    rwnx_hw->mod_params.nss);
				sta->rx_mcs_idx |= (sta->rx_nss - 1) << 3;
			}
		}
		extcap_ie = cfg80211_find_ie(WLAN_EID_VHT_CAPABILITY, rsp_ie,
					     ind->assoc_rsp_ie_len);
		if (extcap_ie && extcap_ie[1] >= sizeof(*vht_cap)) {
			int i;
			u16 rx_mcs_map;
			u8 rx_mcs_mask;

			vht_cap = (void *)(extcap_ie + 2);
			sta->vht_cap_info = vht_cap->vht_cap_info;
			sta->format_mod = FORMATMOD_VHT;
			rx_mcs_map = le16_to_cpu(vht_cap->supp_mcs.rx_mcs_map);

			for (i = 7; i >= 0; i--) {
				rx_mcs_mask = (rx_mcs_map >> (2 * i)) & 3;

				if (rx_mcs_mask !=
				    IEEE80211_VHT_MCS_NOT_SUPPORTED) {
					sta->rx_mcs_idx =
						vht_mcs_map_to_mcs_val(
							rx_mcs_mask);
					sta->rx_nss = i + 1;
					break;
				}
			}
			sta->rx_nss = min_t(int, sta->rx_nss,
					    rwnx_hw->mod_params.nss);
		}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
		extcap_ie = cfg80211_find_ext_ie(WQ_EID_EXT_HE_CAPA, rsp_ie,
						 ind->assoc_rsp_ie_len);
		if (extcap_ie && extcap_ie[1] >= 17) {
			struct ieee80211_he_mcs_nss_supp *sta_mcs_nss_supp;
			u8 rx_mcs_map;
			u16 mcs_map_rx_80;
			u8 nss;

			memcpy(sta->he_mac_cap_info, extcap_ie + 3, 6);
			memcpy(sta->he_phy_cap_info, extcap_ie + 9, 11);
			// Get mcs and nss from HE cap info
			sta_mcs_nss_supp =
				(struct ieee80211_he_mcs_nss_supp *)(extcap_ie +
								     20);
			sta->format_mod = FORMATMOD_HE_SU;
			/* Need to go over for 80MHz, 160MHz and for 80+80 */
			//for (i = 0; i < 3; i++) {
			mcs_map_rx_80 =
				le16_to_cpu(((__le16 *)sta_mcs_nss_supp)[0]);
			for (nss = 7; nss >= 0; nss--) {
				rx_mcs_map = (mcs_map_rx_80 >> (2 * nss)) & 3;
				if (rx_mcs_map !=
				    IEEE80211_HE_MCS_NOT_SUPPORTED) {
					rx_mcs_map = min_t(
						int,
						rwnx_hw->mod_params.he_mcs_map,
						rx_mcs_map);
					sta->rx_mcs_idx = he_mcs_map_to_mcs_max(
						rx_mcs_map);
					sta->rx_nss = nss + 1;
					break;
				}
			}
			sta->rx_nss = min_t(int, sta->rx_nss,
					    rwnx_hw->mod_params.nss);
			//}
		}
#endif
#endif /* DEBUG_WQ_DFX */
#ifdef CONFIG_RWNX_BFMER
		/* If Beamformer feature is activated, check if features can be used
         * with the new peer device
         */
		if (rwnx_hw->mod_params.bfmer) {
			const u8 *vht_capa_ie;
			const struct ieee80211_vht_cap *vht_cap;

			do {
				/* Look for VHT Capability Information Element */
				vht_capa_ie = cfg80211_find_ie(
					WLAN_EID_VHT_CAPABILITY, rsp_ie,
					ind->assoc_rsp_ie_len);

				/* Stop here if peer device does not support VHT */
				if (!vht_capa_ie) {
					break;
				}

				vht_cap = (const struct ieee80211_vht_cap
						   *)(vht_capa_ie + 2);

				/* Send MM_BFMER_ENABLE_REQ message if needed */
				rwnx_send_bfmer_enable(rwnx_hw, sta, vht_cap);
			} while (0);
		}
#endif //(CONFIG_RWNX_BFMER)

#ifdef CONFIG_RWNX_MON_DATA
		// If there are 1 sta and 1 monitor interface active at the same time then
		// monitor interface channel context is always the same as the STA interface.
		// This doesn't work with 2 STA interfaces but we don't want to support it.
		if (rwnx_hw->monitor_vif != RWNX_INVALID_VIF) {
			struct rwnx_vif *rwnx_mon_vif =
				rwnx_hw->vif_table[rwnx_hw->monitor_vif];
			rwnx_chanctx_unlink(rwnx_mon_vif);
			rwnx_chanctx_link(rwnx_mon_vif, ind->ch_idx, NULL);
		}
#endif
	}

	if (gv_get_pwr_from_bin_flag) {
		u8 connect_chan_pwr[PWR_TAB_LEN];
		int con_ret = 0;
		u8 band = ind->chan.band;
		u32 freq = ind->chan.prim20_freq;
		rwnx_store_chan_pwr_tab(rwnx_vif, band, freq, connect_chan_pwr);
		con_ret = rwnx_send_chan_pwr_info_req(
			rwnx_hw, rwnx_vif, connect_chan_pwr, band, freq);
		if (con_ret) {
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "%s, rwnx_send_chan_pwr_info_req faild\n",
			       __func__);
		}
	}

	if (ind->roamed) {
		struct cfg80211_roam_info info;
		memset(&info, 0, sizeof(info));

		if (rwnx_vif->ch_index < NX_CHAN_CTXT_CNT)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
			info.links[0].channel =
				rwnx_hw->chanctx_table[rwnx_vif->ch_index]
					.chan_def.chan;
#else
			info.channel =
				rwnx_hw->chanctx_table[rwnx_vif->ch_index]
					.chan_def.chan;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
		info.links[0].bssid = (const u8 *)&ind->bssid;
#else
		info.bssid = (const u8 *)&ind->bssid;
#endif
		info.req_ie = req_ie;
		info.req_ie_len = ind->assoc_req_ie_len;
		info.resp_ie = rsp_ie;
		info.resp_ie_len = ind->assoc_rsp_ie_len;
		cfg80211_roamed(dev, &info, GFP_ATOMIC);
	} else {
		struct wireless_dev *wdev = &rwnx_vif->wdev;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "[auto]cfg80211_connect_result status_code=%d ssid_len=%d BSSID=%pM",
		       ind->status_code, wdev->u.client.ssid_len,
		       (const u8 *)&ind->bssid);
#else
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "[auto]cfg80211_connect_result status_code=%d ssid_len=%d BSSID=%pM",
		       ind->status_code, wdev->ssid_len,
		       (const u8 *)&ind->bssid);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
		if (wdev->u.client.ssid_len == 0)
#else
		if (wdev->ssid_len == 0)
#endif
			ind->status_code = WLAN_STATUS_UNSPECIFIED_FAILURE;

		if (ind->status_code == 0) {
			struct ieee80211_channel *chan;
			struct cfg80211_bss *bss;

			WQ_DBG(DM_GENERIC, DL_ERR, "connect freq: %d\n",
			       ind->chan.prim20_freq);
			chan = ieee80211_get_channel(rwnx_hw->wiphy,
						     ind->chan.prim20_freq);
			bss = cfg80211_get_bss(rwnx_hw->wiphy, chan,
					       (const u8 *)ind->bssid.array,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 19, 2)
					       wdev->u.client.ssid,
					       wdev->u.client.ssid_len,
#else
					       wdev->ssid, wdev->ssid_len,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
					       IEEE80211_BSS_TYPE_ESS,
#else
					       /* TODO: */ 0,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
					       IEEE80211_PRIVACY_ANY);
#else
					       /* TODO: */ 0);
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0) ||                         \
     defined(CONFIG_CFG80211_CONNECT_BSS_ANDROID))
			cfg80211_connect_bss(dev, (const u8 *)ind->bssid.array,
					     bss, req_ie, ind->assoc_req_ie_len,
					     rsp_ie, ind->assoc_rsp_ie_len,
					     ind->status_code, GFP_ATOMIC,
					     NL80211_TIMEOUT_UNSPECIFIED);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0)
			cfg80211_connect_bss(dev, (const u8 *)ind->bssid.array,
					     bss, req_ie, ind->assoc_req_ie_len,
					     rsp_ie, ind->assoc_rsp_ie_len,
					     ind->status_code, GFP_ATOMIC);
#endif
		} else {
			cfg80211_connect_result(dev,
						(const u8 *)ind->bssid.array,
						req_ie, ind->assoc_req_ie_len,
						rsp_ie, ind->assoc_rsp_ie_len,
						ind->status_code, GFP_ATOMIC);
		}
		rwnx_hw->connect_req_ts = 0;
	}

	mutex_unlock(&rwnx_hw->mutex);

	if (ind->status_code == 0) {
		struct rwnx_vif *vifs = NULL;
		list_for_each_entry (vifs, &rwnx_hw->vifs, list) {
			if ((RWNX_VIF_TYPE(vifs) == NL80211_IFTYPE_AP) &&
			    (vifs->ap.flags & RWNX_AP_STARTED)) {
				if ((vifs->ap.chandef.chan->band ==
				     sta->band) &&
				    (vifs->ap.chandef.chan->center_freq !=
				     sta->center_freq) &&
				    sta->center_freq) {
					WQ_DBG(DM_GENERIC, DL_WRN,
					       "SAP follow STA ch,ch=%u\n",
					       sta->center_freq);
					vifs->ap.chandef.chan->center_freq =
						(u32)sta->center_freq;
					vifs->ap.chandef.center_freq1 =
						sta->center_freq1;
					vifs->ap.chandef.center_freq2 =
						sta->center_freq2;
					vifs->ap.chandef.chan->hw_value =
						(u32)sta->center_freq;
					/* channel 14 is only for IEEE 802.11b */
					if (vifs->ap.chandef.center_freq1 ==
					    2484)
						vifs->ap.chandef.width =
							sta->width;
					else
						vifs->ap.chandef.width =
							sta->width + 1;
					schedule_work(
						&rwnx_hw->bcn_change_task);
				}
			}
		}

		netif_tx_start_all_queues(dev);
		netif_carrier_on(dev);
	}

	return 0;
}

/***************************************************************************
 * Messages from SM task
 **************************************************************************/
static inline int rwnx_rx_sm_connect_ext_ind(struct rwnx_hw *rwnx_hw,
					     struct rwnx_cmd *cmd,
					     struct ipc_e2a_msg *msg)
{
	struct sm_connect_ext_ind *ind =
		(struct sm_connect_ext_ind *)msg->param;

	ENTER();

	// Fill-in the AP information
	if (ind->dbdc_chan_enabled) {
		struct ieee80211_channel *dbdc_chan;
		struct cfg80211_chan_def dbdc_chandef;
		struct ieee80211_channel *chan;
		struct cfg80211_chan_def chandef;
		struct rwnx_vif *rwnx_vif_dbdc =
			rwnx_hw->vif_table[ind->dbdc_vif_idx];
		struct rwnx_vif *rwnx_vif_mac0 =
			rwnx_hw->vif_table[ind->mac0_vif_idx];

		//DBDC role
		dbdc_chan = ieee80211_get_channel(rwnx_hw->wiphy,
						  ind->dbdc_chan.prim20_freq);
		cfg80211_chandef_create(&chandef, dbdc_chan,
					NL80211_CHAN_NO_HT);
		if (!rwnx_hw->mod_params.ht_on)
			dbdc_chandef.width = NL80211_CHAN_WIDTH_20_NOHT;
		else
			dbdc_chandef.width = chnl2bw[ind->dbdc_chan.type];

		dbdc_chandef.center_freq1 = ind->dbdc_chan.center1_freq;
		dbdc_chandef.center_freq2 = ind->dbdc_chan.center2_freq;
		rwnx_chanctx_link(rwnx_vif_dbdc, ind->dbdc_vif_idx,
				  &dbdc_chandef);

		//Other role
		chan = ieee80211_get_channel(rwnx_hw->wiphy,
					     ind->mac0_chan.prim20_freq);
		cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_NO_HT);
		if (!rwnx_hw->mod_params.ht_on)
			chandef.width = NL80211_CHAN_WIDTH_20_NOHT;
		else
			chandef.width = chnl2bw[ind->mac0_chan.type];

		chandef.center_freq1 = ind->mac0_chan.center1_freq;
		chandef.center_freq2 = ind->mac0_chan.center2_freq;
		rwnx_chanctx_link(rwnx_vif_mac0, ind->mac0_vif_idx, &chandef);
	}

	return 0;
}

static inline int rwnx_rx_sm_disconnect_ind(struct rwnx_hw *rwnx_hw,
					    struct rwnx_cmd *cmd,
					    struct ipc_e2a_msg *msg)
{
	struct sm_disconnect_ind *ind = (struct sm_disconnect_ind *)msg->param;
	struct rwnx_vif *rwnx_vif;
	struct net_device *dev;
	struct rwnx_sta *sta_table;
	extern int rwnx_rx_sm_connect_time_show(struct rwnx_sta * sta_table);

	ENTER();

	rwnx_vif = rwnx_hw->vif_table[ind->vif_idx];
	if (rwnx_vif == NULL) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "%s:: rwnx_vif==NULL, return mutex_unlock()\n",
		       __func__);
		mutex_unlock(&rwnx_hw->mutex);
		return 0;
	}

	dev = rwnx_vif->ndev;
	sta_table = (struct rwnx_sta *)rwnx_vif->sta.ap;

	/* if vif is not up, rwnx_close has already been called */
	if (rwnx_vif->up) {
		if (!ind->reassoc) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)
			cfg80211_disconnected(dev, ind->reason_code, NULL, 0,
					      (ind->reason_code <= 1),
					      GFP_ATOMIC);
#else
			cfg80211_disconnected(dev, ind->reason_code, NULL, 0,
					      GFP_ATOMIC);
#endif

			if (rwnx_vif->sta.ft_assoc_ies) {
				kfree(rwnx_vif->sta.ft_assoc_ies);
				rwnx_vif->sta.ft_assoc_ies = NULL;
				rwnx_vif->sta.ft_assoc_ies_len = 0;
			}
		}
		netif_tx_stop_all_queues(dev);
		netif_carrier_off(dev);
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 11, 0) //TODO_need recheck
	dbg_connect_time.disconnect_time[sta_table->band][sta_table->width] =
		get_seconds();
#else
	dbg_connect_time.disconnect_time[sta_table->band][sta_table->width] =
		ktime_get_seconds();
#endif
	rwnx_rx_sm_connect_time_show(sta_table);

#ifdef CONFIG_RWNX_BFMER
	/* Disable Beamformer if supported */
	rwnx_bfmer_report_del(rwnx_hw, rwnx_vif->sta.ap);
#endif //(CONFIG_RWNX_BFMER)

#ifdef DEBUG_WQ_DFX
	/* update debug info, clear cipher and update auth type */
	wq_dbg_update_security_info(&rwnx_vif->security, 0xff, 0,
				    MAC_CIPHER_INVALID);
#endif

	if (rwnx_vif->extAP_supp) {
		WQ_DBG(DM_GENERIC, DL_WRN, "%s del clear extap table\n", __func__);
		extap_tbl_clear();
	}
	rwnx_txq_sta_deinit(rwnx_hw, rwnx_vif->sta.ap);
	rwnx_txq_tdls_vif_deinit(rwnx_vif);
	rwnx_dbgfs_unregister_sta(rwnx_hw, rwnx_vif->sta.ap);
	rwnx_vif->sta.ap->valid = false;
	rwnx_vif->sta.ap = NULL;
	rwnx_vif->generation++;
	rwnx_external_auth_disable(rwnx_vif);
	rwnx_chanctx_unlink(rwnx_vif);

	//del key_add_timer in case of recv disconnect ind
	del_timer(&rwnx_hw->key_add_timer);
	rwnx_vif->b_disconnecting = false;

	WQ_DBG(DM_GENERIC, DL_ERR,
	       "[auto]cfg80211_disconnected reason_code=%d\n",
	       ind->reason_code);

	return 0;
}

int rwnx_rx_sm_connect_time_show(struct rwnx_sta *sta_table)
{
	int band_num = 0, width_num = 0;
	int band = sta_table->band;
	int width = sta_table->width;
	char *band_s[] = { "NL80211_BAND_2GHZ", "NL80211_BAND_5GHZ" };
	char *width_s[] = { "NL80211_CHAN_WIDTH_20", "NL80211_CHAN_WIDTH_40",
			    "NL80211_CHAN_WIDTH_80" };
	struct wq_dbg_connect_time connect_time_stat;

	wq_get_connect_time(sta_table, &connect_time_stat);

	WQ_DBG(DM_GENERIC, DL_WRN, "%s %s this time = %ds", band_s[band],
	       width_s[width], dbg_connect_time.sub_time[band][width]);
	for (band_num = 0; band_num < 2; band_num++) {
		for (width_num = 0; width_num < 3; width_num++) {
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "band %s , width %s all connected time = %ds",
			       band_s[band_num], width_s[width_num],
			       dbg_connect_time.all_time[band_num][width_num]);
		}
	}

	return 0;
}

static inline int rwnx_rx_sm_external_auth_required_ind(struct rwnx_hw *rwnx_hw,
							struct rwnx_cmd *cmd,
							struct ipc_e2a_msg *msg)
{
	struct sm_external_auth_required_ind *ind =
		(struct sm_external_auth_required_ind *)msg->param;
	struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[ind->vif_idx];
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0) ||                          \
	defined(CONFIG_EXTERNAL_AUTH_PATCH)
	struct net_device *dev = rwnx_vif->ndev;
	struct cfg80211_external_auth_params params;

	WQ_DBG(DM_GENERIC, DL_INF,
	       "rwnx_rx_sm_external_auth_required_ind::ssid_len=%d\n",
	       ind->ssid.length);
	ENTER();

	params.action = NL80211_EXTERNAL_AUTH_START;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	ether_addr_copy(params.bssid, (const u8 *)&ind->bssid);
#else
	(void)memcpy(params.bssid, (const u8 *)&ind->bssid, ETH_ALEN);
#endif
	params.ssid.ssid_len = ind->ssid.length;
	memcpy(params.ssid.ssid, ind->ssid.array,
	       min_t(size_t, ind->ssid.length, sizeof(params.ssid.ssid)));
	params.key_mgmt_suite = ind->akm;

	if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_GO &&
	    ind->bit_hml_external_auth_req) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "rwnx_rx_sm_external_auth_required_ind::to run cfg80211_external_auth_request\n");
		cfg80211_external_auth_request(dev, &params, GFP_ATOMIC);
#ifdef CONFIG_HML
		rwnx_external_auth_enable(rwnx_vif);
#endif
		return 0;
	}

	if ((ind->vif_idx > NX_VIRT_DEV_MAX) || !rwnx_vif->up ||
	    (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION) ||
	    cfg80211_external_auth_request(dev, &params, GFP_ATOMIC)) {
		wiphy_err(rwnx_hw->wiphy,
			  "Failed to start external auth on vif %d",
			  ind->vif_idx);
		rwnx_send_sm_external_auth_required_rsp(
			rwnx_hw, rwnx_vif, WLAN_STATUS_UNSPECIFIED_FAILURE);
		return 0;
	}

	rwnx_external_auth_enable(rwnx_vif);
#else
	rwnx_send_sm_external_auth_required_rsp(
		rwnx_hw, rwnx_vif, WLAN_STATUS_UNSPECIFIED_FAILURE);
#endif
	return 0;
}

static inline int rwnx_rx_sm_ft_auth_ind(struct rwnx_hw *rwnx_hw,
					 struct rwnx_cmd *cmd,
					 struct ipc_e2a_msg *msg)
{
	struct sm_ft_auth_ind *ind = (struct sm_ft_auth_ind *)msg->param;
	struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[ind->vif_idx];
	struct sk_buff *skb;
	size_t data_len = (offsetof(struct ieee80211_mgmt, u.auth.variable) +
			   ind->ft_ie_len);

	skb = dev_alloc_skb(data_len);
	if (skb) {
		struct ieee80211_mgmt *mgmt = (void *)skb_put(skb, data_len);
		mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
						  IEEE80211_STYPE_AUTH);
		memcpy(mgmt->u.auth.variable, ind->ft_ie_buf, ind->ft_ie_len);
		rwnx_rx_defer_skb(rwnx_hw, rwnx_vif, skb);
		dev_kfree_skb(skb);
	} else {
		netdev_warn(rwnx_vif->ndev,
			    "Allocation failed for FT auth ind\n");
	}

	return 0;
}

/***************************************************************************
 * Messages from TWT task
 **************************************************************************/
static inline int rwnx_rx_twt_setup_ind(struct rwnx_hw *rwnx_hw,
					struct rwnx_cmd *cmd,
					struct ipc_e2a_msg *msg)
{
	struct twt_setup_ind *ind = (struct twt_setup_ind *)msg->param;
	struct rwnx_sta *rwnx_sta = &rwnx_hw->sta_table[ind->sta_idx];

	ENTER();

	memcpy(&rwnx_sta->twt_ind, ind, sizeof(struct twt_setup_ind));
	return 0;
}

static inline int rwnx_rx_mesh_path_create_cfm(struct rwnx_hw *rwnx_hw,
					       struct rwnx_cmd *cmd,
					       struct ipc_e2a_msg *msg)
{
	struct mesh_path_create_cfm *cfm =
		(struct mesh_path_create_cfm *)msg->param;
	struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[cfm->vif_idx];

	ENTER();

	/* Check we well have a Mesh Point Interface */
	if (rwnx_vif && (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_MESH_POINT))
		rwnx_vif->ap.flags &= ~RWNX_AP_CREATE_MESH_PATH;

	return 0;
}

static inline int rwnx_rx_mesh_peer_update_ind(struct rwnx_hw *rwnx_hw,
					       struct rwnx_cmd *cmd,
					       struct ipc_e2a_msg *msg)
{
	struct mesh_peer_update_ind *ind =
		(struct mesh_peer_update_ind *)msg->param;
	struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[ind->vif_idx];
	struct rwnx_sta *rwnx_sta = &rwnx_hw->sta_table[ind->sta_idx];

	ENTER();

	if ((ind->vif_idx >= (NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX)) ||
	    (rwnx_vif &&
	     (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)) ||
	    (ind->sta_idx >= NX_REMOTE_STA_MAX))
		return 1;

	if (rwnx_vif->ap.flags & RWNX_AP_USER_MESH_PM) {
		if (!ind->estab && rwnx_sta->valid) {
			/* There is no way to inform upper layer for lost of peer, still
               clean everything in the driver */
			rwnx_sta->ps.active = false;
			rwnx_sta->valid = false;

			/* Remove the station from the list of VIF's station */
			list_del_init(&rwnx_sta->list);

			rwnx_txq_sta_deinit(rwnx_hw, rwnx_sta);
			rwnx_dbgfs_unregister_sta(rwnx_hw, rwnx_sta);
		} else {
			WARN_ON(0);
		}
	} else {
		/* Check if peer link has been established or lost */
		if (ind->estab) {
			if (!rwnx_sta->valid) {
				u8 txq_status;

				rwnx_sta->valid = true;
				rwnx_sta->sta_idx = ind->sta_idx;
				rwnx_sta->ch_idx = rwnx_vif->ch_index;
				rwnx_sta->vif_idx = ind->vif_idx;
				rwnx_sta->vlan_idx = rwnx_sta->vif_idx;
				rwnx_sta->ps.active = false;
				rwnx_sta->qos = true;
				rwnx_sta->aid = ind->sta_idx + 1;
				//rwnx_sta->acm = ind->acm;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
				ether_addr_copy(rwnx_sta->mac_addr,
						(const u8 *)&ind->peer_addr);
#else
				(void)memcpy(rwnx_sta->mac_addr,
					     (const u8 *)&ind->peer_addr,
					     ETH_ALEN);
#endif

				rwnx_chanctx_link(rwnx_vif, rwnx_sta->ch_idx,
						  NULL);

				/* Add the station in the list of VIF's stations */
				INIT_LIST_HEAD(&rwnx_sta->list);
				list_add_tail(&rwnx_sta->list,
					      &rwnx_vif->ap.sta_list);

				/* Initialize the TX queues */
				if (rwnx_sta->ch_idx == rwnx_hw->cur_chanctx) {
					txq_status = 0;
				} else {
					txq_status = RWNX_TXQ_STOP_CHAN;
				}

				rwnx_txq_sta_init(rwnx_hw, rwnx_sta,
						  txq_status);
				rwnx_dbgfs_register_sta(rwnx_hw, rwnx_sta);

#ifdef CONFIG_RWNX_BFMER
				// TODO: update indication to contains vht capabilties
				if (rwnx_hw->mod_params.bfmer)
					rwnx_send_bfmer_enable(rwnx_hw,
							       rwnx_sta, NULL);

				rwnx_mu_group_sta_init(rwnx_sta, NULL);
#endif /* CONFIG_RWNX_BFMER */

			} else {
				WARN_ON(0);
			}
		} else {
			if (rwnx_sta->valid) {
				rwnx_sta->ps.active = false;
				rwnx_sta->valid = false;

				/* Remove the station from the list of VIF's station */
				list_del_init(&rwnx_sta->list);

				rwnx_txq_sta_deinit(rwnx_hw, rwnx_sta);
				rwnx_dbgfs_unregister_sta(rwnx_hw, rwnx_sta);
			} else {
				WARN_ON(0);
			}
		}
	}

	return 0;
}

static inline int rwnx_rx_mesh_path_update_ind(struct rwnx_hw *rwnx_hw,
					       struct rwnx_cmd *cmd,
					       struct ipc_e2a_msg *msg)
{
	struct mesh_path_update_ind *ind =
		(struct mesh_path_update_ind *)msg->param;
	struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[ind->vif_idx];
	struct rwnx_mesh_path *mesh_path;
	bool found = false;

	ENTER();

	if (ind->vif_idx >= (NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX))
		return 1;

	if (!rwnx_vif || (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT))
		return 0;

	/* Look for path with provided target address */
	list_for_each_entry (mesh_path, &rwnx_vif->ap.mpath_list, list) {
		if (mesh_path->path_idx == ind->path_idx) {
			found = true;
			break;
		}
	}

	/* Check if element has been deleted */
	if (ind->delete) {
		if (found) {
			trace_mesh_delete_path(mesh_path);
			/* Remove element from list */
			list_del_init(&mesh_path->list);
			/* Free the element */
			kfree(mesh_path);
		}
	} else {
		if (found) {
			// Update the Next Hop STA
			mesh_path->nhop_sta =
				&rwnx_hw->sta_table[ind->nhop_sta_idx];
			trace_mesh_update_path(mesh_path);
		} else {
			// Allocate a Mesh Path structure
			mesh_path = kmalloc(sizeof(struct rwnx_mesh_path),
					    GFP_ATOMIC);

			if (mesh_path) {
				INIT_LIST_HEAD(&mesh_path->list);

				mesh_path->path_idx = ind->path_idx;
				mesh_path->nhop_sta =
					&rwnx_hw->sta_table[ind->nhop_sta_idx];
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
				ether_addr_copy((u8 *)&mesh_path->tgt_mac_addr,
						(const u8 *)&ind->tgt_mac_addr);
#else
				(void)memcpy((u8 *)&mesh_path->tgt_mac_addr,
					     (const u8 *)&ind->tgt_mac_addr,
					     ETH_ALEN);
#endif

				// Insert the path in the list of path
				list_add_tail(&mesh_path->list,
					      &rwnx_vif->ap.mpath_list);
				trace_mesh_create_path(mesh_path);
			}
		}
	}

	return 0;
}

static inline int rwnx_rx_mesh_proxy_update_ind(struct rwnx_hw *rwnx_hw,
						struct rwnx_cmd *cmd,
						struct ipc_e2a_msg *msg)
{
	struct mesh_proxy_update_ind *ind =
		(struct mesh_proxy_update_ind *)msg->param;
	struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[ind->vif_idx];
	struct rwnx_mesh_proxy *mesh_proxy;
	bool found = false;

	ENTER();

	if (ind->vif_idx >= (NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX))
		return 1;

	if (!rwnx_vif || (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT))
		return 0;

	/* Look for path with provided external STA address */
	list_for_each_entry (mesh_proxy, &rwnx_vif->ap.proxy_list, list) {
		if (ether_addr_equal((const u8 *)&ind->ext_sta_addr,
				     (const u8 *)&mesh_proxy->ext_sta_addr)) {
			found = true;
			break;
		}
	}

	if (ind->delete &&found) {
		/* Delete mesh path */
		list_del_init(&mesh_proxy->list);
		kfree(mesh_proxy);
	} else if (!ind->delete &&!found) {
		/* Allocate a Mesh Path structure */
		mesh_proxy = (struct rwnx_mesh_proxy *)kmalloc(
			sizeof(*mesh_proxy), GFP_ATOMIC);

		if (mesh_proxy) {
			INIT_LIST_HEAD(&mesh_proxy->list);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
			ether_addr_copy((u8 *)&mesh_proxy->ext_sta_addr,
					(const u8 *)&ind->ext_sta_addr);
#else
			(void)memcpy((u8 *)&mesh_proxy->ext_sta_addr,
				     (const u8 *)&ind->ext_sta_addr, ETH_ALEN);
#endif
			mesh_proxy->local = ind->local;

			if (!ind->local) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
				ether_addr_copy(
					(u8 *)&mesh_proxy->proxy_addr,
					(const u8 *)&ind->proxy_mac_addr);
#else
				(void)memcpy((u8 *)&mesh_proxy->proxy_addr,
					     (const u8 *)&ind->proxy_mac_addr,
					     ETH_ALEN);
#endif
			}

			/* Insert the path in the list of path */
			list_add_tail(&mesh_proxy->list,
				      &rwnx_vif->ap.proxy_list);
		}
	}

	return 0;
}

/***************************************************************************
 * Messages from APM task
 **************************************************************************/
static inline int rwnx_rx_apm_probe_client_ind(struct rwnx_hw *rwnx_hw,
					       struct rwnx_cmd *cmd,
					       struct ipc_e2a_msg *msg)
{
	struct apm_probe_client_ind *ind =
		(struct apm_probe_client_ind *)msg->param;
	struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[ind->vif_idx];
	struct rwnx_sta *rwnx_sta = &rwnx_hw->sta_table[ind->sta_idx];

	rwnx_sta->stats.last_act = jiffies;
	cfg80211_probe_status(rwnx_vif->ndev, rwnx_sta->mac_addr,
			      (u64)ind->probe_id, ind->client_present, 0, false,
			      GFP_ATOMIC);

	return 0;
}

static char wq_dbg_rec_type_str[][32] = {
	"MAC_NOT_IDLE",	    "TX_TIMEOUT",
	"IDLE_TIMEOUT",	    "HE_TB_TIMEOUT",
	"MM_TIMEOUT",	    "PHY_ERR",
	"PHY_ERR_TB_BASIC", "PHY_ERR_BSRP",
	"PHYIF_UNDERRUN",   "PHYIF_OVERFLOW",
	"RX_FIFO_OVERFLOW", "PT_ERROR",
	"TX_DMA_DEAD",	    "BEACON_DMA_DEAD",
	"RX_HEADER_DEAD",   "RX_PAYLOAD_DEAD",
	"HW_ERR",	    "RX_DESC_ERR",
	"RX_KEY_IDX_ERR",   "RX_NDP_DESC_ERR",
	"RX_LEN_ERR",	    "TX_DESC_ERR",
	"TX_KEY_IDX_ERR",   "TX_DESC_AMPDU_ERR",
	"TX_MAC_IDLE",	    "TX_BEACON_MAC_IDLE",
};
static int wq_dbg_err_recovery_process(struct rwnx_hw *rwnx_hw,
				       struct dbg_err_ind *ind)
{
	struct wq_dbg_recovery_stats *stats;
	uint8_t i;
	uint16_t *old;
	uint16_t *new;

	if (ind) {
		stats = (struct wq_dbg_recovery_stats *)ind->info_param;
		/* compare new stats with old one, search for last recovery event */
#define MAC_REC_NUM 26
		new = (uint16_t *)stats;
		old = (uint16_t *)&rwnx_hw->rec_stats;
		for (i = 0; i < MAC_REC_NUM; i++) {
			if (*new != *old) {
				break;
			} else {
				old++;
				new ++;
			}
		}
		/* print event type */
		if (i < MAC_REC_NUM) {
			WQ_DBG(DM_GENERIC, DL_ERR,
			       "########## MAC RECOVERRED: %s !!! ##########\n",
			       wq_dbg_rec_type_str[i]);
		}

		memcpy(&rwnx_hw->rec_stats, stats,
		       sizeof(struct wq_dbg_recovery_stats));
		wq_dbg_dump_recovery_stats(&rwnx_hw->rec_stats);
	}
	return 0;
}

/***************************************************************************
 * Messages from DEBUG task
 **************************************************************************/
static inline int rwnx_rx_dbg_error_ind(struct rwnx_hw *rwnx_hw,
					struct rwnx_cmd *cmd,
					struct ipc_e2a_msg *msg)
{
	uint16_t param_len;
	struct dbg_err_ind *ind;
	uint8_t subtype;
	ENTER();

	if (msg == NULL) {
		return 0;
	}

	param_len = msg->param_len;

	if (param_len == 0) {
		/* no msg */
		//rwnx_error_ind(rwnx_hw);
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "warning: rwnx_rx_dbg_error_ind param_len is 0\n");
	} else {
		ind = (struct dbg_err_ind *)msg->param;
		subtype = ind->sub_type;
		switch (subtype) {
		case DBG_ERR_IND_REC:
		case DBG_ERR_IND_WOW:
			wq_dbg_err_recovery_process(rwnx_hw, ind);
			break;
		}
		if (subtype == DBG_ERR_IND_WOW) {
			WQ_DBG(DM_GENERIC, DL_WRN, "%s: DBG_ERR_IND_WOW\n",
			       __func__);
			complete(&rwnx_hw->wow_suspend_wait);
		}
	}

	return 0;
}

static msg_cb_fct mm_hdlrs[MSG_I(MM_EXT_MAX)] = {
	[MSG_I(MM_CHANNEL_SWITCH_IND)] = rwnx_rx_chan_switch_ind,
	[MSG_I(MM_CHANNEL_PRE_SWITCH_IND)] = rwnx_rx_chan_pre_switch_ind,
	[MSG_I(MM_REMAIN_ON_CHANNEL_EXP_IND)] =
		rwnx_rx_remain_on_channel_exp_ind,
	[MSG_I(MM_PS_CHANGE_IND)] = rwnx_rx_ps_change_ind,
	[MSG_I(MM_TRAFFIC_REQ_IND)] = rwnx_rx_traffic_req_ind,
	[MSG_I(MM_P2P_VIF_PS_CHANGE_IND)] = rwnx_rx_p2p_vif_ps_change_ind,
	[MSG_I(MM_CSA_COUNTER_IND)] = rwnx_rx_csa_counter_ind,
	[MSG_I(MM_CSA_FINISH_IND)] = rwnx_rx_csa_finish_ind,
	[MSG_I(MM_CSA_TRAFFIC_IND)] = rwnx_rx_csa_traffic_ind,
	[MSG_I(MM_CHANNEL_SURVEY_IND)] = rwnx_rx_channel_survey_ind,
	[MSG_I(MM_P2P_NOA_UPD_IND)] = rwnx_rx_p2p_noa_upd_ind,
	[MSG_I(MM_RSSI_STATUS_IND)] = rwnx_rx_rssi_status_ind,
	[MSG_I(MM_PKTLOSS_IND)] = rwnx_rx_pktloss_notify_ind,
	[MSG_I(MM_NSS_UPDATE_IND)] = rwnx_rx_nss_update_ind,
	[MSG_I(MM_COEX_INFO_UPDATE_IND)] = rwnx_rx_coex_info_update_ind,
};

static msg_cb_fct scan_hdlrs[MSG_I(SCANU_MAX)] = {
	[MSG_I(SCANU_START_CFM)] = rwnx_rx_scanu_start_cfm,
	[MSG_I(SCANU_RESULT_IND)] = rwnx_rx_scanu_result_ind,
};

static msg_cb_fct me_hdlrs[MSG_I(ME_EXTEND_MAX)] = {
	[MSG_I(ME_TKIP_MIC_FAILURE_IND)] = rwnx_rx_me_tkip_mic_failure_ind,
	[MSG_I(ME_TX_CREDITS_UPDATE_IND)] = rwnx_rx_me_tx_credits_update_ind,
	[MSG_I(ME_WOW_RESUME_IND)] = rwnx_rx_me_wow_resume_ind,
	[MSG_I(ME_EXTEND_FREE_HOST_DATA_RING_NOW_IND)] =
		rwnx_rx_me_extend_free_host_data_ring_now_ind,
};

static msg_cb_fct sm_hdlrs[MSG_I(SM_EXT_MAX)] = {
	[MSG_I(SM_CONNECT_IND)] = rwnx_rx_sm_connect_ind,
	[MSG_I(SM_DISCONNECT_IND)] = rwnx_rx_sm_disconnect_ind,
	[MSG_I(SM_EXTERNAL_AUTH_REQUIRED_IND)] =
		rwnx_rx_sm_external_auth_required_ind,
	[MSG_I(SM_FT_AUTH_IND)] = rwnx_rx_sm_ft_auth_ind,
	[MSG_I(SM_CONNECT_EXT_IND)] = rwnx_rx_sm_connect_ext_ind,
};

static msg_cb_fct apm_hdlrs[MSG_I(APM_MAX)] = {
	[MSG_I(APM_PROBE_CLIENT_IND)] = rwnx_rx_apm_probe_client_ind,
};

static msg_cb_fct twt_hdlrs[MSG_I(TWT_MAX)] = {
	[MSG_I(TWT_SETUP_IND)] = rwnx_rx_twt_setup_ind,
};

static msg_cb_fct mesh_hdlrs[MSG_I(MESH_MAX)] = {
	[MSG_I(MESH_PATH_CREATE_CFM)] = rwnx_rx_mesh_path_create_cfm,
	[MSG_I(MESH_PEER_UPDATE_IND)] = rwnx_rx_mesh_peer_update_ind,
	[MSG_I(MESH_PATH_UPDATE_IND)] = rwnx_rx_mesh_path_update_ind,
	[MSG_I(MESH_PROXY_UPDATE_IND)] = rwnx_rx_mesh_proxy_update_ind,
};

static msg_cb_fct dbg_hdlrs[MSG_I(DBG_MAX)] = {
	[MSG_I(DBG_ERROR_IND)] = rwnx_rx_dbg_error_ind,
};

static msg_cb_fct tdls_hdlrs[MSG_I(TDLS_MAX)] = {
	[MSG_I(TDLS_CHAN_SWITCH_CFM)] = rwnx_rx_tdls_chan_switch_cfm,
	[MSG_I(TDLS_CHAN_SWITCH_IND)] = rwnx_rx_tdls_chan_switch_ind,
	[MSG_I(TDLS_CHAN_SWITCH_BASE_IND)] = rwnx_rx_tdls_chan_switch_base_ind,
	[MSG_I(TDLS_PEER_PS_IND)] = rwnx_rx_tdls_peer_ps_ind,
};

#ifdef CONFIG_HML
typedef struct {
	uint8_t vif_idx;
	uint8_t id;
	uint8_t peer_mac_addr[ETH_ALEN];
	uint16_t status;
} hml_mac_conn_prepare_ind;

typedef struct {
	uint8_t vif_idx;
	uint8_t mac_addr[ETH_ALEN];
} hml_mac_sta_del_ind;

static int rwnx_rx_hml_connect_ind_handler(struct rwnx_hw *rwnx_hw,
					   struct rwnx_cmd *cmd,
					   struct ipc_e2a_msg *msg)
{
	struct sm_connect_ind *ind = (struct sm_connect_ind *)msg->param;
	struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[ind->vif_idx];
	struct net_device *dev = rwnx_vif->ndev;
	const u8 *req_ie, *rsp_ie;
	const u8 *extcap_ie;
	const struct ieee_types_extcap *extcap;
	struct rwnx_sta *sta;
	u8 txq_status;

	/* retrieve IE addresses and lengths */
	req_ie = (const u8 *)ind->assoc_ie_buf;
	rsp_ie = req_ie + ind->assoc_req_ie_len;

	// fill-in the ap information
	if (ind->status_code == 0) {
		sta = &rwnx_hw->sta_table[ind->ap_idx];
		sta->sta_idx = ind->ap_idx;
		sta->ch_idx = ind->ch_idx;
		sta->vif_idx = ind->vif_idx;
		sta->vlan_idx = sta->vif_idx;
		sta->qos = ind->qos;
		sta->acm = ind->acm;
		sta->ps.active = false;
		sta->aid = ind->aid;
		sta->band = ind->chan.band;
		sta->width = ind->chan.type;
		sta->center_freq = ind->chan.prim20_freq;
		sta->center_freq1 = ind->chan.center1_freq;
		sta->center_freq2 = ind->chan.center2_freq;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
		ether_addr_copy(sta->mac_addr, (const u8 *)&ind->bssid);
#else
		(void)memcpy(sta->mac_addr, (const u8 *)&ind->bssid, ETH_ALEN);
#endif
		if (ind->ch_idx == rwnx_hw->cur_chanctx) {
			txq_status = 0;
		} else {
			txq_status = RWNX_TXQ_STOP_CHAN;
		}
		rwnx_txq_sta_init(rwnx_hw, sta,
				  rwnx_txq_vif_get_status(rwnx_vif));
		list_add_tail(&sta->list, &rwnx_vif->ap.sta_list);
		rwnx_vif->generation++;
		sta->valid = true;

		memcpy(sta->ac_param, ind->ac_param, sizeof(sta->ac_param));
		rwnx_dbgfs_register_sta(rwnx_hw, sta);
		rwnx_txq_tdls_vif_init(rwnx_vif);
		rwnx_mu_group_sta_init(sta, NULL);
		extcap_ie = cfg80211_find_ie(WLAN_EID_EXT_CAPABILITY, rsp_ie,
					     ind->assoc_rsp_ie_len);
		if (extcap_ie && extcap_ie[1] >= 5) {
			extcap = (void *)(extcap_ie);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
			rwnx_vif->tdls_chsw_prohibited =
				extcap->ext_capab[4] &
				WLAN_EXT_CAPA5_TDLS_CH_SW_PROHIBITED;
#endif
		}
	}

	WQ_DBG(DM_GENERIC, DL_ERR,
	       "[auto]cfg80211_connect_result status_code = %d BSSID=%pM",
	       ind->status_code, (const u8 *)&ind->bssid);
	cfg80211_connect_result(dev, (const u8 *)&ind->bssid, req_ie,
				ind->assoc_req_ie_len, rsp_ie,
				ind->assoc_rsp_ie_len, ind->status_code,
				GFP_ATOMIC);
	return 0;
}

static inline int rwnx_rx_vendor_conn_prepare_ind(struct rwnx_hw *rwnx_hw,
						  struct rwnx_cmd *cmd,
						  struct ipc_e2a_msg *msg)
{
	ENTER();
	return 0;
}

static inline int rwnx_rx_sta_del_ind_handler(struct rwnx_hw *rwnx_hw,
					      struct rwnx_cmd *cmd,
					      struct ipc_e2a_msg *msg)
{
	hml_mac_sta_del_ind *ind = (hml_mac_sta_del_ind *)msg->param;
	struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[ind->vif_idx];
	struct net_device *dev = rwnx_vif->ndev;
	struct rwnx_sta *cur, *tmp;
	ENTER();
	list_for_each_entry_safe (cur, tmp, &rwnx_vif->ap.sta_list, list) {
		if (ether_addr_equal(cur->mac_addr, ind->mac_addr)) {
			netdev_info(dev, "report del sta %d (%pM)",
				    cur->sta_idx, cur->mac_addr);
			cur->ps.active = false;
			cur->valid = false;
			rwnx_txq_sta_deinit(rwnx_hw, cur);
#ifdef CONFIG_RWNX_BFMER
			rwnx_bfmer_report_del(rwnx_hw, cur);
			rwnx_mu_group_sta_del(rwnx_hw, cur);
#endif
			list_del(&cur->list);
			atomic_dec(&rwnx_vif->ap.sta_num);
			rwnx_vif->generation++;
			rwnx_dbgfs_unregister_sta(rwnx_hw, cur);
			cfg80211_del_sta(dev, ind->mac_addr, GFP_ATOMIC);
			break;
		}
	}
	return 0;
}

static inline int rwnx_rx_vendor_test_msg1_cfm(struct rwnx_hw *rwnx_hw,
					       struct rwnx_cmd *cmd,
					       struct ipc_e2a_msg *msg)
{
	ENTER();
	return 0;
}

static msg_cb_fct vendor_hdlrs[MSG_I(HML_MSG_MAX)] = {
	[MSG_I(HML_CONN_IND)] = rwnx_rx_hml_connect_ind_handler,
	[MSG_I(HML_CONN_PREPAIR_IND)] = rwnx_rx_vendor_conn_prepare_ind,
	[MSG_I(HML_STA_DEL_IND)] = rwnx_rx_sta_del_ind_handler,
	[MSG_I(HML_MSG_TEST_CFM)] = rwnx_rx_vendor_test_msg1_cfm,
};
#endif

static msg_cb_fct *msg_hdlrs[] = {
	[TASK_MM] = mm_hdlrs,
	[TASK_DBG] = dbg_hdlrs,
	[TASK_TDLS] = tdls_hdlrs,
	[TASK_SCANU] = scan_hdlrs,
	[TASK_ME] = me_hdlrs,
	[TASK_SM] = sm_hdlrs,
	[TASK_APM] = apm_hdlrs,
	[TASK_MESH] = mesh_hdlrs,
	[TASK_TWT] = twt_hdlrs,
#ifdef CONFIG_HML
	[TASK_VENDOR_HML] = vendor_hdlrs,
#endif
};

/**
 *
 */
void rwnx_rx_handle_msg(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	cmd_mgr_msg_cfm(&rwnx_hw->cmd_mgr, msg,
			msg_hdlrs[MSG_T(msg->id)][MSG_I(msg->id)]);
}

/**
 * rwnx_msgackind() - IRQ handler callback for %IPC_IRQ_E2A_MSG_ACK
 *
 * @rwnx_hw: Pointer to main driver data
 * @ack: command that should be acknowledged
 */
void rwnx_msgackind(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *ack)
{
	cmd_mgr_msg_ack(rwnx_hw, ack);
}
