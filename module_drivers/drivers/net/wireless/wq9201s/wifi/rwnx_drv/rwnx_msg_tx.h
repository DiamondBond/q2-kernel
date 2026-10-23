/**
 ****************************************************************************************
 *
 * @file rwnx_msg_tx.h
 *
 * @brief TX function declarations
 *
 * Copyright (C) RivieraWaves 2012-2020
 *
 ****************************************************************************************
 */

#ifndef _RWNX_MSG_TX_H_
#define _RWNX_MSG_TX_H_

#include "rwnx_defs.h"
#include "fw_api/wifi/mac/cp_api.h"
#include "fw_api/wifi/mac/wowlan.h"

enum {
	SCHED_SCAN_START = 1,
	SCHED_SCAN_STOP,
	SCHED_SCAN_IND = 0xFE,
	SCHED_SCAN_MAX,
};

struct supp_chan_pwr_str {
	u8 channel;
	u8 pwr_data;
};

#define FREQ_TO_CHAN_24G(freq) ((freq - 2407) / 5)
#define FREQ_TO_CHAN_5G(freq) ((freq - 5000) / 5)

int rwnx_send_reset(struct rwnx_hw *rwnx_hw);
int rwnx_set_slottime(struct rwnx_hw *rwnx_hw, u8 slot_time);
int rwnx_send_start(struct rwnx_hw *rwnx_hw);
int rwnx_send_version_req(struct rwnx_hw *rwnx_hw, struct mm_version_cfm *cfm);
int rwnx_send_version_ext_req(struct rwnx_hw *rwnx_hw,
			      struct mm_version_ext_cfm *cfm);
int rwnx_send_add_if(struct rwnx_hw *rwnx_hw, const unsigned char *mac,
		     enum nl80211_iftype iftype, bool p2p, bool hml_flag,
		     struct mm_add_if_cfm *cfm);
int rwnx_send_remove_if(struct rwnx_hw *rwnx_hw, u8 vif_index);
int rwnx_send_set_channel(struct rwnx_hw *rwnx_hw, int phy_idx,
			  struct mm_set_channel_cfm *cfm);
int rwnx_send_key_add(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 sta_idx,
		      bool pairwise, u8 *key, u8 key_len, u8 key_idx,
		      u8 cipher_suite, struct mm_key_add_cfm *cfm);
void rwnx_send_ptk_key_add(struct work_struct *w);
void rwnx_ptk_add_timeout(struct timer_list *key_add_timer);
int rwnx_send_key_del(struct rwnx_hw *rwnx_hw, uint8_t hw_key_idx);
int rwnx_send_bcn_change(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 *bcn_addr,
			 u16 bcn_len, u16 tim_oft, u16 tim_len, u16 *csa_oft);
int rwnx_send_tim_update(struct rwnx_hw *rwnx_hw, u8 vif_idx, u16 aid,
			 u8 tx_status);
int rwnx_send_roc(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
		  struct ieee80211_channel *chan, unsigned int duration);
int rwnx_send_cancel_roc(struct rwnx_hw *rwnx_hw);
int rwnx_send_set_power(struct rwnx_hw *rwnx_hw, u8 vif_idx, s8 pwr,
			struct mm_set_power_cfm *cfm);
int rwnx_send_set_edca(struct rwnx_hw *rwnx_hw, u8 hw_queue, u32 param,
		       bool uapsd, u8 inst_nbr);
int rwnx_send_tdls_chan_switch_req(struct rwnx_hw *rwnx_hw,
				   struct rwnx_vif *rwnx_vif,
				   struct rwnx_sta *rwnx_sta,
				   bool sta_initiator, u8 oper_class,
				   struct cfg80211_chan_def *chandef,
				   struct tdls_chan_switch_cfm *cfm);
int rwnx_send_tdls_cancel_chan_switch_req(
	struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
	struct rwnx_sta *rwnx_sta, struct tdls_cancel_chan_switch_cfm *cfm);

#ifdef CONFIG_RWNX_P2P_DEBUGFS
int rwnx_send_p2p_oppps_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
			    u8 ctw, struct mm_set_p2p_oppps_cfm *cfm);
int rwnx_send_p2p_noa_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
			  int count, int interval, int duration, bool dyn_noa,
			  struct mm_set_p2p_noa_cfm *cfm);
#endif /* CONFIG_RWNX_P2P_DEBUGFS */

void wq_nss_update_task_hdl(struct work_struct *w);
int rwnx_send_me_config_req(struct rwnx_hw *rwnx_hw);
int rwnx_send_me_chan_config_req(struct rwnx_hw *rwnx_hw);
int rwnx_send_me_set_control_port_req(struct rwnx_hw *rwnx_hw, bool opened,
				      u8 sta_idx);
int rwnx_send_me_sta_add(struct rwnx_hw *rwnx_hw,
			 struct station_parameters *params, const u8 *mac,
			 u8 inst_nbr, struct me_sta_add_cfm *cfm);
int rwnx_send_me_sta_del(struct rwnx_hw *rwnx_hw, u8 sta_idx, bool tdls_sta);
int rwnx_send_me_traffic_ind(struct rwnx_hw *rwnx_hw, u8 sta_idx, bool uapsd,
			     u8 tx_status);
int rwnx_send_twt_request(struct rwnx_hw *rwnx_hw, u8 setup_type, u8 vif_idx,
			  struct twt_conf_tag *conf, struct twt_setup_cfm *cfm);
int rwnx_send_twt_teardown(struct rwnx_hw *rwnx_hw,
			   struct twt_teardown_req *twt_teardown,
			   struct twt_teardown_cfm *cfm);
int rwnx_send_me_rc_stats(struct rwnx_hw *rwnx_hw, u8 sta_idx,
			  struct me_rc_stats_cfm *cfm);
int rwnx_send_me_rc_set_rate(struct rwnx_hw *rwnx_hw, u8 sta_idx, u16 rate_idx);
int rwnx_send_me_set_ps_mode(struct rwnx_hw *rwnx_hw, u8 ps_mode);
int rwnx_send_me_tx_credit_size_req(struct rwnx_hw *rwnx_hw,
				    struct me_tx_credit_size_cfm *cfm);
int rwnx_send_me_set_bus_pwr_state(struct rwnx_hw *rwnx_hw, u8 pwr_state);
int rwnx_send_me_set_wowlan_req(struct rwnx_hw *rwnx_hw,
				struct cfg80211_wowlan *wowl,
				enum wow_req_type req_type, u32 *wakup_reason);
int rwnx_send_sm_connect_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
			     struct cfg80211_connect_params *sme,
			     struct sm_connect_cfm *cfm);
int rwnx_send_sm_disconnect_req(struct rwnx_hw *rwnx_hw,
				struct rwnx_vif *rwnx_vif, u16 reason);
int rwnx_send_sm_external_auth_required_rsp(struct rwnx_hw *rwnx_hw,
					    struct rwnx_vif *rwnx_vif,
					    u16 status);
int rwnx_send_sm_ft_auth_rsp(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
			     uint8_t *ie, int ie_len);
int rwnx_send_apm_start_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
			    struct cfg80211_ap_settings *settings,
			    struct apm_start_cfm *cfm,
			    struct rwnx_ipc_elem_var *elem);
int rwnx_send_apm_stop_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif);
int rwnx_send_apm_probe_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
			    struct rwnx_sta *sta,
			    struct apm_probe_client_cfm *cfm);
int rwnx_send_scanu_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
			struct cfg80211_scan_request *param);
int rwnx_send_abort_scan_req(struct rwnx_hw *rwnx_hw,
			     struct rwnx_vif *rwnx_vif);
int rwnx_send_apm_start_cac_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
				struct cfg80211_chan_def *chandef,
				struct apm_start_cac_cfm *cfm);
int rwnx_send_apm_stop_cac_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif);
int rwnx_send_tdls_peer_traffic_ind_req(struct rwnx_hw *rwnx_hw,
					struct rwnx_vif *rwnx_vif);
int rwnx_send_config_monitor_req(struct rwnx_hw *rwnx_hw,
				 struct cfg80211_chan_def *chandef,
				 struct me_config_monitor_cfm *cfm);
int rwnx_send_mesh_start_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
			     const struct mesh_config *conf,
			     const struct mesh_setup *setup,
			     struct mesh_start_cfm *cfm);
int rwnx_send_mesh_stop_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
			    struct mesh_stop_cfm *cfm);
int rwnx_send_mesh_update_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
			      u32 mask, const struct mesh_config *p_mconf,
			      struct mesh_update_cfm *cfm);
int rwnx_send_mesh_peer_info_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
				 u8 sta_idx, struct mesh_peer_info_cfm *cfm);
void rwnx_send_mesh_peer_update_ntf(struct rwnx_hw *rwnx_hw,
				    struct rwnx_vif *vif, u8 sta_idx,
				    u8 mlink_state);
void rwnx_send_mesh_path_create_req(struct rwnx_hw *rwnx_hw,
				    struct rwnx_vif *vif, u8 *tgt_addr);
int rwnx_send_mesh_path_update_req(struct rwnx_hw *rwnx_hw,
				   struct rwnx_vif *vif, const u8 *tgt_addr,
				   const u8 *p_nhop_addr,
				   struct mesh_path_update_cfm *cfm);
void rwnx_send_mesh_proxy_add_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
				  u8 *ext_addr);
#ifdef CONFIG_HML
int rwnx_send_vendor_msg1_req(struct rwnx_hw *rwnx_hw);
int rwnx_send_vendor_sta_del(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
			     const u8 *mac, u16 reason);
int rwnx_send_vendor_cmd(struct rwnx_hw *rwnx_hw, uint16_t cmd_id,
			 void *write_msg, uint16_t len);
#endif

#ifdef CONFIG_RWNX_BFMER
#ifdef CONFIG_RWNX_MUMIMO_TX
int rwnx_send_mu_group_update_req(struct rwnx_hw *rwnx_hw,
				  struct rwnx_sta *rwnx_sta);
#endif /* CONFIG_RWNX_MUMIMO_TX */
#endif /* CONFIG_RWNX_BFMER */

/* Debug messages */
int rwnx_send_dbg_trigger_req(struct rwnx_hw *rwnx_hw, char *msg);
int rwnx_send_dbg_mem_read_req(struct rwnx_hw *rwnx_hw, u32 mem_addr,
			       struct dbg_mem_read_cfm *cfm);
int rwnx_send_dbg_mem_write_req(struct rwnx_hw *rwnx_hw, u32 mem_addr,
				u32 mem_data);
int rwnx_send_dbg_set_mod_filter_req(struct rwnx_hw *rwnx_hw, u32 filter);
int rwnx_send_dbg_set_sev_filter_req(struct rwnx_hw *rwnx_hw, u32 filter);
int rwnx_send_dbg_get_sys_stat_req(struct rwnx_hw *rwnx_hw,
				   struct dbg_get_sys_stat_cfm *cfm);
int rwnx_send_dbg_pktlog_cfg_req(struct rwnx_hw *rwnx_hw, u8 flags);
#if 1
int rwnx_send_dbg_wq_priv_test_req(struct rwnx_hw *rwnx_hw,
				   struct rwnx_vif *rwnx_vif, u8 msg_id,
				   u8 sub_msg_id, char *mgs, int mesg_len);
#endif
int rwnx_send_cfg_rssi_req(struct rwnx_hw *rwnx_hw, u8 vif_index,
			   int rssi_thold, u32 rssi_hyst);

int rwnx_write_reg(struct rwnx_hw *rwnx_hw, u32 addr, u32 val);
int rwnx_read_reg(struct rwnx_hw *rwnx_hw, u32 addr, u8 *buf, int buf_len);

int rwnx_write_reg32(struct rwnx_hw *rwnx_hw, u32 addr, u32 val);
u32 rwnx_read_reg32(struct rwnx_hw *rwnx_hw, u32 addr);

int rwnx_send_ip_req(struct rwnx_hw *rwnx_hw, u8 *ip_addr, u8 index);

int rwnx_send_cca_config_set(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 ena, u16 period);
int rwnx_send_cca_data_get(struct rwnx_hw *rwnx_hw, u8 vif_idx, struct mm_get_cca_cfm *cfm);

/*
 * RWNX_INFO_NOTIFY_SET(_VIF)_NO_CHK()
 *  simply wraps rwnx_info_notify_set().
 *  return
 *      = 0: success.
 *      != 0: error.
 *
 * RWNX_INFO_NOTIFY_SET(_VIF)
 *  outputs a log if error returned.
 */
#define RWNX_INFO_NOTIFY_SET_NO_CHK(rwnx_hw, msg_type, param)                  \
	RWNX_INFO_NOTIFY_SET_VIF_NO_CHK(rwnx_hw, msg_type, 0xff, param)
#define RWNX_INFO_NOTIFY_SET_VIF_NO_CHK(rwnx_hw, msg_type, vif_index, param)   \
	rwnx_info_notify_set(rwnx_hw, msg_type, vif_index, &(param),           \
			     sizeof(param))
#define RWNX_INFO_NOTIFY_SET(rwnx_hw, msg_type, param)                         \
	RWNX_INFO_NOTIFY_SET_VIF(rwnx_hw, msg_type, 0xff, param)
#define RWNX_INFO_NOTIFY_SET_VIF(rwnx_hw, msg_type, vif_index, param)          \
	({                                                                     \
		int ret = RWNX_INFO_NOTIFY_SET_VIF_NO_CHK(rwnx_hw, msg_type,   \
							  vif_index, param);   \
                                                                               \
		if (ret)                                                       \
			WQ_DBG(DM_GENERIC, DL_ERR,                             \
			       "%s: SET " #msg_type                            \
			       " error(%d, %x [%*ph])!\n",                     \
			       __func__, ret, vif_index, (int)sizeof(param),   \
			       &(param));                                      \
		ret;                                                           \
	})

/*
 * RWNX_INFO_NOTIFY_GET(_VIF)_NO_CHK
 *  simply wraps rwnx_info_notify_get().
 *  return
 *      > 0: actual confirmation result length (success).
 *      = 0: success, but actual confirmation result length got from f/w is 0.
 *      < 0: error code.
 *
 * RWNX_INFO_NOTIFY_GET(_VIF)
 *  outputs a log if error or actual confirmation length < "sizeof(*(cfm_result))".
 *  return
 *     = 0: success. (actual confirmation length >= expected)
 *     > 0: actual confirmation length (< expected). consider as an error.
 *     < 0: error code
 */
#define RWNX_INFO_NOTIFY_GET_NO_CHK(rwnx_hw, msg_type, param, cfm_result)      \
	RWNX_INFO_NOTIFY_GET_VIF_NO_CHK(rwnx_hw, msg_type, 0xff, param,        \
					cfm_result)
#define RWNX_INFO_NOTIFY_GET_VIF_NO_CHK(rwnx_hw, msg_type, vif_index, param,   \
					cfm_result)                            \
	rwnx_info_notify_get(rwnx_hw, msg_type, vif_index, &(param),           \
			     sizeof(param), (void *)(cfm_result),              \
			     sizeof(*(cfm_result)))
#define RWNX_INFO_NOTIFY_GET(rwnx_hw, msg_type, param, cfm_result)             \
	RWNX_INFO_NOTIFY_GET_VIF(rwnx_hw, msg_type, 0xff, param, cfm_result)
#define RWNX_INFO_NOTIFY_GET_VIF(rwnx_hw, msg_type, vif_index, param,          \
				 cfm_result)                                   \
	({                                                                     \
		int ret = RWNX_INFO_NOTIFY_GET_VIF_NO_CHK(                     \
			rwnx_hw, msg_type, vif_index, param, cfm_result);      \
                                                                               \
		if (ret >= sizeof(*(cfm_result)))                              \
			ret = 0;                                               \
		else                                                           \
			WQ_DBG(DM_GENERIC, DL_ERR,                             \
			       "%s: GET " #msg_type                            \
			       " error(%d, %x [%*ph])!\n",                     \
			       __func__, ret, vif_index, (int)sizeof(param),   \
			       &(param));                                      \
		ret;                                                           \
	})

int rwnx_info_notify_set(struct rwnx_hw *rwnx_hw, u8 msg_type, u8 vif_index,
			 const void *param, u8 param_len);
/* return actual confirmation result length or an error code(< 0) */
int rwnx_info_notify_get(struct rwnx_hw *rwnx_hw, u8 msg_type, u8 vif_index,
			 const void *param, u8 param_len, void *cfm_result,
			 u16 result_len);

int rwnx_send_free_host_ring_req(struct rwnx_hw *rwnx_hw, u8 mac_id,
				 u32 buf_rd_idx, bool use_backup_ring);
int rwnx_send_set_host_ring_req(struct rwnx_hw *rwnx_hw,
				struct rwnx_rx_ll *rx_ll);

#ifdef CONFIG_PM
int rwnx_send_secure_param_set(struct rwnx_hw *rwnx_hw, u8 vif_idx,
			       struct cfg80211_connect_params *sme);
int rwnx_send_rekey_data_set(struct rwnx_hw *rwnx_hw, u8 vif_idx,
			     struct cfg80211_gtk_rekey_data *key);
#endif

int rwnx_send_dbg_recover_test_req(struct rwnx_hw *rwnx_hw,
				   uint32_t recover_mode,
				   struct mm_info_notify_cfm *cfm);
int rwnx_send_sched_scan_start_req(struct rwnx_hw *rwnx_hw,
				   struct rwnx_vif *rwnx_vif,
				   struct cfg80211_sched_scan_request *param);
int rwnx_send_sched_scan_stop_req(struct rwnx_hw *rwnx_hw,
				  struct rwnx_vif *rwnx_vif, u64 reqid);
int rwnx_send_set_usb_param_req(struct rwnx_hw *rwnx_hw, u32 max_upload_size);
int rwnx_txq_ring_init(struct rwnx_hw *rwnx_hw, u16 sta_cnt);
void rwnx_txq_ring_deinit(struct rwnx_hw *rwnx_hw, u16 sta_cnt);
int rwnx_send_set_isr_usage_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
				u32 enable);
int rwnx_monitor_me_sta_add(struct rwnx_hw *rwnx_hw, u8 vif_idx);
int rwnx_send_set_vif_state_req(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 aid,
				bool active);
int rwnx_send_force_pcie_link_speed_req(struct rwnx_hw *rwnx_hw, u32 *cfm);
int rwnx_send_ant_req(struct rwnx_hw *rwnx_hw, u32 ant);

#endif /* _RWNX_MSG_TX_H_ */
