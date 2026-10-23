/**
 ******************************************************************************
 *
 * @file rwnx_msg_tx.c
 *
 * @brief TX function definitions
 *
 * Copyright (C) RivieraWaves 2012-2020
 *
 ******************************************************************************
 */

#define WQ_LOG_DM DM_TX

#include "rwnx_main.h"
#include "rwnx_msg_tx.h"
#ifdef CONFIG_RWNX_BFMER
#include "rwnx_bfmer.h"
#endif //(CONFIG_RWNX_BFMER)
#include "rwnx_compat.h"
#include "wq_log.h"
#ifdef DEBUG_WQ_PRIV
#include "wq_wifi_priv.h"
#endif
#include "wq_wifi_dbg.h"
#include "fw_api/wifi/mac/cp_api.h"
#include <linux/pci.h>

#include "wq_ipc.h"
#include "rwnx_reg_data.h"

extern int nbw_type;
#define DEFAULT_PWR_32_USR_DBM 40
#define NX_REMOTE_STA_32 32

/* Default MAC Rx filters that can be changed by mac80211
 * (via the configure_filter() callback) */
#define RWNX_MAC80211_CHANGEABLE                                               \
	(NXMAC_ACCEPT_BA_BIT | NXMAC_ACCEPT_BAR_BIT |                          \
	 NXMAC_ACCEPT_OTHER_DATA_FRAMES_BIT | NXMAC_ACCEPT_PROBE_REQ_BIT |     \
	 NXMAC_ACCEPT_PS_POLL_BIT)

/* Default MAC Rx filters that cannot be changed by mac80211 */
#define RWNX_MAC80211_NOT_CHANGEABLE                                           \
	(NXMAC_ACCEPT_QO_S_NULL_BIT | NXMAC_ACCEPT_Q_DATA_BIT |                \
	 NXMAC_ACCEPT_DATA_BIT | NXMAC_ACCEPT_OTHER_MGMT_FRAMES_BIT |          \
	 NXMAC_ACCEPT_MY_UNICAST_BIT | NXMAC_ACCEPT_BROADCAST_BIT |            \
	 NXMAC_ACCEPT_BEACON_BIT | NXMAC_ACCEPT_PROBE_RESP_BIT)

/* Default MAC Rx filter */
#define RWNX_DEFAULT_RX_FILTER                                                 \
	(RWNX_MAC80211_CHANGEABLE | RWNX_MAC80211_NOT_CHANGEABLE)

const int bw2chnl[] = {
	[NL80211_CHAN_WIDTH_20_NOHT] = PHY_CHNL_BW_20,
	[NL80211_CHAN_WIDTH_20] = PHY_CHNL_BW_20,
	[NL80211_CHAN_WIDTH_40] = PHY_CHNL_BW_40,
	[NL80211_CHAN_WIDTH_80] = PHY_CHNL_BW_80,
	[NL80211_CHAN_WIDTH_160] = PHY_CHNL_BW_160,
	[NL80211_CHAN_WIDTH_80P80] = PHY_CHNL_BW_80P80,
};

const int chnl2bw[] = {
	[PHY_CHNL_BW_20] = NL80211_CHAN_WIDTH_20,
	[PHY_CHNL_BW_40] = NL80211_CHAN_WIDTH_40,
	[PHY_CHNL_BW_80] = NL80211_CHAN_WIDTH_80,
	[PHY_CHNL_BW_160] = NL80211_CHAN_WIDTH_160,
	[PHY_CHNL_BW_80P80] = NL80211_CHAN_WIDTH_80P80,
};

/*****************************************************************************/
/*
 * Parse the ampdu density to retrieve the value in usec, according to the
 * values defined in ieee80211.h
 */
static inline u8 rwnx_ampdudensity2usec(u8 ampdudensity)
{
	switch (ampdudensity) {
	case IEEE80211_HT_MPDU_DENSITY_NONE:
		return 0;
		/* 1 microsecond is our granularity */
	case IEEE80211_HT_MPDU_DENSITY_0_25:
	case IEEE80211_HT_MPDU_DENSITY_0_5:
	case IEEE80211_HT_MPDU_DENSITY_1:
		return 1;
	case IEEE80211_HT_MPDU_DENSITY_2:
		return 2;
	case IEEE80211_HT_MPDU_DENSITY_4:
		return 4;
	case IEEE80211_HT_MPDU_DENSITY_8:
		return 8;
	case IEEE80211_HT_MPDU_DENSITY_16:
		return 16;
	default:
		return 0;
	}
}

static inline bool use_pairwise_key(struct cfg80211_crypto_settings *crypto)
{
	if ((crypto->cipher_group == WLAN_CIPHER_SUITE_WEP40) ||
	    (crypto->cipher_group == WLAN_CIPHER_SUITE_WEP104))
		return false;

	return true;
}

static inline bool is_pwr_msg(int id)
{
	return (id == ME_SET_BUS_PWR_STATE_REQ);
}

/**
 * copy_connect_ies -- Copy Association Elements in the the request buffer
 * send to the firmware
 *
 * @vif: Vif that received the connection request
 * @req: Connection request to send to the firmware
 * @sme: Connection info
 *
 * For driver that do not use userspace SME (like this one) the host connection
 * request doesn't explicitly mentions that the connection can use FT over the
 * air. if FT is possible, send the FT elements (as received in update_ft_ies callback)
 * to the firmware
 *
 * In all other cases simply copy the list povided by the user space in the
 * request buffer
 */
static void copy_connect_ies(struct rwnx_vif *vif, struct sm_connect_req *req,
			     struct cfg80211_connect_params *sme)
{
	if ((sme->auth_type == NL80211_AUTHTYPE_FT) &&
	    !(vif->sta.flags & RWNX_STA_FT_OVER_DS)) {
		const struct element *rsne, *fte, *mde;
		uint8_t *pos;
		rsne = cfg80211_find_elem(WLAN_EID_RSN, vif->sta.ft_assoc_ies,
					  vif->sta.ft_assoc_ies_len);
		fte = cfg80211_find_elem(WLAN_EID_FAST_BSS_TRANSITION,
					 vif->sta.ft_assoc_ies,
					 vif->sta.ft_assoc_ies_len);
		mde = cfg80211_find_elem(WLAN_EID_MOBILITY_DOMAIN,
					 vif->sta.ft_assoc_ies,
					 vif->sta.ft_assoc_ies_len);
		pos = (uint8_t *)req->ie_buf;

		// We can use FT over the air
		memcpy(&vif->sta.ft_target_ap, sme->bssid, ETH_ALEN);

		if (rsne) {
			memcpy(pos, rsne,
			       sizeof(struct element) + rsne->datalen);
			pos += sizeof(struct element) + rsne->datalen;
		}
		memcpy(pos, mde, sizeof(struct element) + mde->datalen);
		pos += sizeof(struct element) + mde->datalen;
		if (fte) {
			memcpy(pos, fte, sizeof(struct element) + fte->datalen);
			pos += sizeof(struct element) + fte->datalen;
		}

		req->ie_len = pos - (uint8_t *)req->ie_buf;
	} else {
		memcpy(req->ie_buf, sme->ie, sme->ie_len);
		req->ie_len = sme->ie_len;
	}
}

/**
 * update_connect_req -- Return the length of the association request IEs
 *
 * @vif: Vif that received the connection request
 * @sme: Connection info
 *
 * Return the ft_ie_len in case of FT.
 * FT over the air is possible if:
 * - auth_type = AUTOMATIC (if already set to FT then it means FT over DS)
 * - already associated to a FT BSS
 * - Target Mobility domain is the same as the curent one
 *
 * If FT is not possible return ie length of the connection info
 */
static int update_connect_req(struct rwnx_vif *vif,
			      struct cfg80211_connect_params *sme)
{
	if ((vif->sta.ap) && (vif->sta.ft_assoc_ies) &&
	    (sme->auth_type == NL80211_AUTHTYPE_AUTOMATIC)) {
		const struct element *rsne, *fte, *mde, *mde_req;
		int ft_ie_len = 0;

		mde_req = cfg80211_find_elem(WLAN_EID_MOBILITY_DOMAIN, sme->ie,
					     sme->ie_len);
		mde = cfg80211_find_elem(WLAN_EID_MOBILITY_DOMAIN,
					 vif->sta.ft_assoc_ies,
					 vif->sta.ft_assoc_ies_len);
		if (!mde || !mde_req ||
		    memcmp(mde, mde_req,
			   sizeof(struct element) + mde->datalen)) {
			return sme->ie_len;
		}

		ft_ie_len += sizeof(struct element) + mde->datalen;

		rsne = cfg80211_find_elem(WLAN_EID_RSN, vif->sta.ft_assoc_ies,
					  vif->sta.ft_assoc_ies_len);
		fte = cfg80211_find_elem(WLAN_EID_FAST_BSS_TRANSITION,
					 vif->sta.ft_assoc_ies,
					 vif->sta.ft_assoc_ies_len);

		if (rsne && fte) {
			ft_ie_len += 2 * sizeof(struct element) +
				     rsne->datalen + fte->datalen;
			sme->auth_type = NL80211_AUTHTYPE_FT;
			return ft_ie_len;
		} else if (rsne || fte) {
			netdev_warn(
				vif->ndev,
				"Missing RSNE or FTE element, skip FT over air");
		} else {
			sme->auth_type = NL80211_AUTHTYPE_FT;
			return ft_ie_len;
		}
	}
	return sme->ie_len;
}

static inline u8 get_chan_flags(uint32_t flags)
{
	u8 chan_flags = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	if (flags & IEEE80211_CHAN_NO_IR)
		chan_flags |= CHAN_NO_IR;
#endif
	if (flags & IEEE80211_CHAN_RADAR)
		chan_flags |= CHAN_RADAR;
	switch (nbw_type) {
	case 1:
		WQ_DBG(DM_GENERIC, DL_INF,
		       "switching to 10MHz narrow bandwidth\n");
		chan_flags |= CHAN_NBW_10MHz;
		break;
	case 2:
		WQ_DBG(DM_GENERIC, DL_INF,
		       "switching to 5MHz narrow bandwidth\n");
		chan_flags |= CHAN_NBW_5MHz;
		break;
	case 3:
		WQ_DBG(DM_GENERIC, DL_INF,
		       "switching to 2.5MHz narrow bandwidth\n");
		chan_flags |= CHAN_NBW_2_5MHz;
		break;
	case 4:
		WQ_DBG(DM_GENERIC, DL_INF,
		       "switching to 4 down clock bandwidth\n");
		chan_flags |= CHAN_NBW_5MHz;
		break;
	default:
		break;
	}

	return chan_flags;
}

static inline s8 chan_to_fw_pwr(int power)
{
	// nbw mode, min tx power is 25dBm
	if (nbw_type) {
		return power < 25 ? 25 : (s8)power;
	}

	return power > 127 ? 127 : (s8)power;
}

static void cfg80211_to_rwnx_chan(const struct cfg80211_chan_def *chandef,
				  struct mac_chan_op *chan)
{
	chan->band = chandef->chan->band;
	chan->type = bw2chnl[chandef->width];
	chan->prim20_freq = chandef->chan->center_freq;
	chan->center1_freq = chandef->center_freq1;
	chan->center2_freq = chandef->center_freq2;
	chan->flags = get_chan_flags(chandef->chan->flags);
	chan->tx_power = chan_to_fw_pwr(chandef->chan->max_power);
	if (4 == nbw_type) {
		WQ_ASSERT((PHY_CHNL_BW_20 == chan->type),
			"4 down clock mode supports 20MHz Bandwidth only!");
		chan->type = PHY_CHNL_BW_80;
		chan->prim20_freq -= 10; // to satisfy requirement of channel
	}
}

static inline void limit_chan_bw(u8 *bw, u16 primary, u16 *center1)
{
	int oft, new_oft = 10;

	if (*bw <= PHY_CHNL_BW_40)
		return;

	oft = *center1 - primary;
	*bw = PHY_CHNL_BW_40;

	if (oft < 0)
		new_oft = new_oft * -1;
	if (abs(oft) == 10 || abs(oft) == 50)
		new_oft = new_oft * -1;

	*center1 = primary + new_oft;
}

/**
 ******************************************************************************
 * @brief Allocate memory for a message
 *
 * This primitive allocates memory for a message that has to be sent. The memory
 * is allocated dynamically on the heap and the length of the variable parameter
 * structure has to be provided in order to allocate the correct size.
 *
 * Several additional parameters are provided which will be preset in the message
 * and which may be used internally to choose the kind of memory to allocate.
 *
 * The memory allocated will be automatically freed by the kernel, after the
 * pointer has been sent to ke_msg_send(). If the message is not sent, it must
 * be freed explicitly with ke_msg_free().
 *
 * Allocation failure is considered critical and should not happen.
 *
 * @param[in] id        Message identifier
 * @param[in] dest_id   Destination Task Identifier
 * @param[in] src_id    Source Task Identifier
 * @param[in] param_len Size of the message parameters to be allocated
 *
 * @return Pointer to the parameter member of the ke_msg. If the parameter
 *         structure is empty, the pointer will point to the end of the message
 *         and should not be used (except to retrieve the message pointer or to
 *         send the message)
 ******************************************************************************
 */
static inline void *rwnx_msg_zalloc(ke_msg_id_t const id,
				    ke_task_id_t const dest_id,
				    uint16_t const param_len)
{
	struct ipc_a2e_msg *msg =
		kzalloc(IPC_A2E_MSG_HDR_LEN + param_len,
			in_softirq() ? GFP_ATOMIC : GFP_KERNEL);

	if (msg == NULL) {
		WQ_DBG(DM_GENERIC, DL_ERR, "%s: msg allocation failed\n",
		       __func__);
		return NULL;
	}

	msg->id = id;
	msg->dest_id = dest_id;
	msg->src_id = TASK_HOST_API;
	msg->param_len = param_len;

	return msg->param;
}

static void rwnx_msg_free(struct rwnx_hw *rwnx_hw, const void *msg_params)
{
	struct ipc_a2e_msg *msg =
		container_of((void *)msg_params, struct ipc_a2e_msg, param);

	ENTER();

	/* Free the message */
	kfree(msg);
}

static int __rwnx_send_msg(struct rwnx_hw *rwnx_hw, const void *msg_params,
			   ke_msg_id_t cfm_id, void *cfm, int cfm_size)
{
	struct ipc_a2e_msg *msg =
		container_of((void *)msg_params, struct ipc_a2e_msg, param);
	struct rwnx_cmd *cmd;

	ENTER();

	cmd = kzalloc(sizeof(struct rwnx_cmd),
		      in_softirq() ? GFP_ATOMIC : GFP_KERNEL);
	cmd->result = -EINTR;
	cmd->id = msg->id;
	cmd->cfm_id = cfm_id;
	cmd->a2e_msg = msg;
	cmd->cfm = cfm;
	cmd->cfm_len = cfm_size;
	if (is_pwr_msg(msg->id))
		cmd->flags |= RWNX_CMD_FLAG_PWR;
	if (cfm_id)
		cmd->flags |= RWNX_CMD_FLAG_REQ_CFM;

	return cmd_mgr_queue(&rwnx_hw->cmd_mgr, cmd);
}

static inline int rwnx_send_msg_nonblock(struct rwnx_hw *rwnx_hw,
					 const void *msg_params)
{
	return __rwnx_send_msg(rwnx_hw, msg_params, 0, NULL, 0);
}

static inline int rwnx_send_msg(struct rwnx_hw *rwnx_hw, const void *msg_params,
				ke_msg_id_t cfm_id)
{
	BUG_ON(cfm_id == 0);
	return __rwnx_send_msg(rwnx_hw, msg_params, cfm_id, NULL, 0);
}

#define RWNX_SEND_MSG_EX(rwnx_hw, msg_params, cfm_id, cfm)                     \
	rwnx_send_msg_ex(rwnx_hw, msg_params, cfm_id, cfm, sizeof(*(cfm)))

static inline int rwnx_send_msg_ex(struct rwnx_hw *rwnx_hw,
				   const void *msg_params, ke_msg_id_t cfm_id,
				   void *cfm, int cfm_size)
{
	BUG_ON(cfm_id == 0);
	BUG_ON(!cfm);
	BUG_ON(cfm_size == 0);
	return __rwnx_send_msg(rwnx_hw, msg_params, cfm_id, cfm, cfm_size);
}

/******************************************************************************
 *    Control messages handling functions (SOFTMAC and  FULLMAC)
 *****************************************************************************/
int rwnx_send_reset(struct rwnx_hw *rwnx_hw)
{
	void *void_param;

	ENTER();

	/* RESET REQ has no parameter */
	void_param = rwnx_msg_zalloc(MM_RESET_REQ, TASK_MM, 0);
	if (!void_param)
		return -ENOMEM;

	return rwnx_send_msg(rwnx_hw, void_param, MM_RESET_CFM);
}

int rwnx_send_start(struct rwnx_hw *rwnx_hw)
{
	struct mm_start_req *start_req_param;

	ENTER();

	/* Build the START REQ message */
	start_req_param = rwnx_msg_zalloc(MM_START_REQ, TASK_MM,
					  sizeof(struct mm_start_req));
	if (!start_req_param)
		return -ENOMEM;

	/* Set parameters for the START message */
	//memcpy(&start_req_param->phy_cfg, &rwnx_hw->phy.cfg, sizeof(rwnx_hw->phy.cfg));
	//bit-0 : enable Rx DCC calibration
	//bit-1 : enable Tx DCC calibration
	//bit-2 : enable Tx IQMC calibration
	//bit-3 : enable Rx IQMC calibration
	start_req_param->phy_cfg.cali_mode = rwnx_hw->mod_params.phy_calib_mode;
	if (rwnx_hw->version_cfm.nss == 2 && (!rwnx_hw->mod_params.nss1_force))
		start_req_param->phy_cfg.spatial_stream_mode = 1;
	else
		start_req_param->phy_cfg.spatial_stream_mode = 0;
	start_req_param->uapsd_timeout = (u32)rwnx_hw->mod_params.uapsd_timeout;
	start_req_param->lp_clk_accuracy = (u16)rwnx_hw->mod_params.lp_clk_ppm;
	start_req_param->tx_timeout[AC_BK] = (u16)rwnx_hw->mod_params.tx_to_bk;
	start_req_param->tx_timeout[AC_BE] = (u16)rwnx_hw->mod_params.tx_to_be;
	start_req_param->tx_timeout[AC_VI] = (u16)rwnx_hw->mod_params.tx_to_vi;
	start_req_param->tx_timeout[AC_VO] = (u16)rwnx_hw->mod_params.tx_to_vo;

	start_req_param->nbw_type = nbw_type;
	WQ_DBG(DM_GENERIC, DL_WRN, "%s: nbw_type:%d\n", __func__,
	       start_req_param->nbw_type);

	/* Send the START REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, start_req_param, MM_START_CFM);
}

int rwnx_send_version_req(struct rwnx_hw *rwnx_hw, struct mm_version_cfm *cfm)
{
	void *void_param;

	ENTER();

	/* VERSION REQ has no parameter */
	void_param = rwnx_msg_zalloc(MM_VERSION_REQ, TASK_MM, 0);
	if (!void_param)
		return -ENOMEM;

	return RWNX_SEND_MSG_EX(rwnx_hw, void_param, MM_VERSION_CFM, cfm);
}

int rwnx_send_version_ext_req(struct rwnx_hw *rwnx_hw,
			      struct mm_version_ext_cfm *cfm)
{
	void *void_param;

	ENTER();

	/* VERSION REQ has no parameter */
	void_param = rwnx_msg_zalloc(MM_VERSION_EXT_REQ, TASK_MM, 0);
	if (!void_param)
		return -ENOMEM;

	return RWNX_SEND_MSG_EX(rwnx_hw, void_param, MM_VERSION_EXT_CFM, cfm);
}

int rwnx_send_add_if(struct rwnx_hw *rwnx_hw, const unsigned char *mac,
		     enum nl80211_iftype iftype, bool p2p, bool hml_flag,
		     struct mm_add_if_cfm *cfm)
{
	struct mm_add_if_req *add_if_req_param;
	int error;

	ENTER();

	/* Build the ADD_IF_REQ message */
	add_if_req_param = rwnx_msg_zalloc(MM_ADD_IF_REQ, TASK_MM,
					   sizeof(struct mm_add_if_req));
	if (!add_if_req_param)
		return -ENOMEM;

		/* Set parameters for the ADD_IF_REQ message */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	ether_addr_copy((u8 *)&add_if_req_param->addr, mac);
#else
	(void)memcpy((u8 *)&add_if_req_param->addr, mac, ETH_ALEN);
#endif
	switch (iftype) {
	case NL80211_IFTYPE_P2P_CLIENT:
		add_if_req_param->p2p = true;
		fallthrough;
	case NL80211_IFTYPE_STATION:
		add_if_req_param->type = VIF_STA;
		break;

	case NL80211_IFTYPE_ADHOC:
		add_if_req_param->type = VIF_IBSS;
		break;

	case NL80211_IFTYPE_P2P_GO:
		add_if_req_param->p2p = true;
#ifdef DEBUG_WQ_PRIV
		if (wq_wifi_priv_hml_flag_test_get()) {
			hml_flag = true;
		}
#endif
		fallthrough;
	case NL80211_IFTYPE_AP:
		add_if_req_param->type = VIF_AP;
		break;
	case NL80211_IFTYPE_MESH_POINT:
		add_if_req_param->type = VIF_MESH_POINT;
		break;
	case NL80211_IFTYPE_AP_VLAN:
		return -1;
	case NL80211_IFTYPE_MONITOR:
		add_if_req_param->type = VIF_MONITOR;
		break;
	default:
		add_if_req_param->type = VIF_STA;
		break;
	}

	add_if_req_param->bit_hml_flag = hml_flag;
	mutex_lock(&rwnx_hw->mutex);
	WQ_DBG(DM_GENERIC, DL_INF, "rwnx_send_add_if::hml_flag=%d, p2p=%d\n",
	       add_if_req_param->bit_hml_flag, add_if_req_param->p2p);

	/* Send the ADD_IF_REQ message to LMAC FW */
	error = RWNX_SEND_MSG_EX(rwnx_hw, add_if_req_param, MM_ADD_IF_CFM, cfm);
	mutex_unlock(&rwnx_hw->mutex);

	return error;
}

int rwnx_send_remove_if(struct rwnx_hw *rwnx_hw, u8 vif_index)
{
	struct mm_remove_if_req *remove_if_req;

	ENTER();

	/* Build the MM_REMOVE_IF_REQ message */
	remove_if_req = rwnx_msg_zalloc(MM_REMOVE_IF_REQ, TASK_MM,
					sizeof(struct mm_remove_if_req));
	if (!remove_if_req)
		return -ENOMEM;

	/* Set parameters for the MM_REMOVE_IF_REQ message */
	remove_if_req->inst_nbr = vif_index;

	/* Send the MM_REMOVE_IF_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, remove_if_req, MM_REMOVE_IF_CFM);
}

int rwnx_send_set_channel(struct rwnx_hw *rwnx_hw, int phy_idx,
			  struct mm_set_channel_cfm *cfm)
{
	struct mm_set_channel_req *req;

	ENTER();

	if (phy_idx >= rwnx_hw->phy.cnt)
		return -ENOTSUPP;

	req = rwnx_msg_zalloc(MM_SET_CHANNEL_REQ, TASK_MM,
			      sizeof(struct mm_set_channel_req));
	if (!req)
		return -ENOMEM;

	if (phy_idx == 0) {
		/* On FULLMAC only setting channel of secondary chain */
		wiphy_err(rwnx_hw->wiphy,
			  "Trying to set channel of primary chain");
		return 0;
	} else {
		req->chan = rwnx_hw->phy.sec_chan;
	}

	req->index = phy_idx;

	if (rwnx_hw->phy.limit_bw)
		limit_chan_bw(&req->chan.type, req->chan.prim20_freq,
			      &req->chan.center1_freq);

	WQ_DBG(DM_GENERIC,
	       DL_WRN, //"mac80211:   freq=%d(c1:%d - c2:%d)/width=%d - band=%d\n"
	       "   hw(%d): prim20=%d(c1:%d - c2:%d)/ type=%d - band=%d\n",
	       //center_freq, center_freq1, center_freq2, width, band,
	       phy_idx, req->chan.prim20_freq, req->chan.center1_freq,
	       req->chan.center2_freq, req->chan.type, req->chan.band);

	/* Send the MM_SET_CHANNEL_REQ REQ message to LMAC FW */
	return RWNX_SEND_MSG_EX(rwnx_hw, req, MM_SET_CHANNEL_CFM, cfm);
}

int rwnx_send_key_add(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 sta_idx,
		      bool pairwise, u8 *key, u8 key_len, u8 key_idx,
		      u8 cipher_suite, struct mm_key_add_cfm *cfm)
{
	struct mm_key_add_req *key_add_req;

	ENTER();

	/* Build the MM_KEY_ADD_REQ message */
	key_add_req = rwnx_msg_zalloc(MM_KEY_ADD_REQ, TASK_MM,
				      sizeof(struct mm_key_add_req));
	if (!key_add_req)
		return -ENOMEM;

	/* Set parameters for the MM_KEY_ADD_REQ message */
	if (sta_idx != 0xFF) {
		/* Pairwise key */
		key_add_req->sta_idx = sta_idx;
	} else {
		/* Default key */
		key_add_req->sta_idx = sta_idx;
		key_add_req->key_idx =
			(u8)key_idx; /* only useful for default keys */
	}
	key_add_req->pairwise = pairwise;
	key_add_req->inst_nbr = vif_idx;
	key_add_req->key.length = key_len;
	memcpy(key_add_req->key.array, key, key_len);

	key_add_req->cipher_suite = cipher_suite;

	WQ_DBG(DM_GENERIC, DL_WRN,
	       "%s: sta_idx:%d key_idx:%d inst_nbr:%d cipher:%d key_len:%d\n",
	       __func__, key_add_req->sta_idx, key_add_req->key_idx,
	       key_add_req->inst_nbr, key_add_req->cipher_suite,
	       key_add_req->key.length);
	print_hex_dump_bytes("key: ", DUMP_PREFIX_OFFSET,
			     key_add_req->key.array, key_add_req->key.length);

	/* Send the MM_KEY_ADD_REQ message to LMAC FW */
	return RWNX_SEND_MSG_EX(rwnx_hw, key_add_req, MM_KEY_ADD_CFM, cfm);
}

void wq_update_mac_capa(struct rwnx_hw *rwnx_hw, struct wiphy *wiphy);
void wq_nss_update_task_hdl(struct work_struct *w)
{
	struct rwnx_hw *rwnx_hw =
		container_of(w, struct rwnx_hw, update_nss_task);
	struct wiphy *wiphy = rwnx_hw->wiphy;
	struct rwnx_vif *vif;
	bool found = false;

	WQ_DBG(DM_GENERIC, DL_WRN, "%s: update nss to %d", __func__,
	       rwnx_hw->mod_params.nss);
	/* Set VHT capabilities */
	wq_update_mac_capa(rwnx_hw, wiphy);

	// Look for VIF entry
	list_for_each_entry (vif, &rwnx_hw->vifs, list) {
		if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_AP ||
		    RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_GO) {
			if ((vif->vif_index == vif->ap.nss_idx) &&
			    (vif->ap.dbdc_mode == 4)) {
				WQ_DBG(DM_GENERIC, DL_WRN, "wq_nss_update_task_hdl: vif:%d", vif->vif_index);
				found = true;
				break;
			}
		}
	}

	if (found) {
		u8 *buf = NULL;
		struct rwnx_bcn *bcn;
		int error = 0;

		/* Build the new beacon with CSA IE */
		bcn = &vif->ap.bcn;
		buf = rwnx_bcn_nss_update(rwnx_hw, bcn);
		if (!buf)
			return;

		/* Send new Beacon. FW will extract channel and count from the beacon */
		error = rwnx_send_bcn_change(rwnx_hw, vif->vif_index, buf, bcn->len,
					bcn->head_len, bcn->tim_len, NULL);

		if (buf) {
			kfree(buf);
			buf = NULL;
		}

		return;
	}
}

void rwnx_send_ptk_key_add(struct work_struct *w)
{
	int error = 0;
	struct mm_key_add_cfm key_add_cfm;
	struct rwnx_hw *rwnx_hw = container_of(w, struct rwnx_hw, add_key_task);
	struct rwnx_vif *tmp_vif;
	u8 vif_index = 0;

	spin_lock_bh(&rwnx_hw->delayed_key_lock);
	if (rwnx_hw->key_add_params.vif == NULL) {
		spin_unlock_bh(&rwnx_hw->delayed_key_lock);
		return;
	}

	tmp_vif = rwnx_hw->key_add_params.vif;
	vif_index = rwnx_hw->key_add_params.vif->vif_index;

	rwnx_hw->key_add_params.vif = NULL;
	spin_unlock_bh(&rwnx_hw->delayed_key_lock);

	error = rwnx_send_key_add(rwnx_hw, vif_index,
				  rwnx_hw->key_add_params.sta_index,
				  rwnx_hw->key_add_params.pairwise,
				  (u8 *)(rwnx_hw->key_add_params.key),
				  rwnx_hw->key_add_params.key_len,
				  rwnx_hw->key_add_params.key_index,
				  rwnx_hw->key_add_params.cipher, &key_add_cfm);

	if (error || key_add_cfm.status) {
		WQ_DBG(DM_IPC, DL_WRN,
		       "[auto]msg:add ptk failed, trigger disconnect %d %u\n",
		       error, key_add_cfm.status);
		rwnx_send_sm_disconnect_req(rwnx_hw, tmp_vif,
					    WLAN_REASON_DEAUTH_LEAVING);
	} else {
#ifdef DEBUG_WQ_DFX
		/* update security information */
		wq_dbg_update_security_info(&tmp_vif->security,
					    rwnx_hw->key_add_params.key_index,
					    rwnx_hw->key_add_params.pairwise,
					    rwnx_hw->key_add_params.cipher);
#endif
	}

	return;
}

void rwnx_ptk_add_timeout(struct timer_list *key_add_timer)
{
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)container_of(
		key_add_timer, struct rwnx_hw, key_add_timer);

	//schedule add_key_task failed, add key direct
	WQ_DBG(DM_IPC, DL_WRN,
	       "schedule add_key_task failed, add key direct\n");
	schedule_work(&rwnx_hw->add_key_task);
}

int rwnx_send_key_del(struct rwnx_hw *rwnx_hw, uint8_t hw_key_idx)
{
	struct mm_key_del_req *key_del_req;

	ENTER();

	/* Build the MM_KEY_DEL_REQ message */
	key_del_req = rwnx_msg_zalloc(MM_KEY_DEL_REQ, TASK_MM,
				      sizeof(struct mm_key_del_req));
	if (!key_del_req)
		return -ENOMEM;

	/* Set parameters for the MM_KEY_DEL_REQ message */
	key_del_req->hw_key_idx = hw_key_idx;

	/* Send the MM_KEY_DEL_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, key_del_req, MM_KEY_DEL_CFM);
}

int rwnx_send_bcn_change(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 *bcn_addr,
			 u16 bcn_len, u16 tim_oft, u16 tim_len, u16 *csa_oft)
{
	struct mm_bcn_change_req *req;

	ENTER();

	/* Build the MM_BCN_CHANGE_REQ message */
	req = rwnx_msg_zalloc(MM_BCN_CHANGE_REQ, TASK_MM,
			      sizeof(struct mm_bcn_change_req));
	if (!req) {
		return -ENOMEM;
	}
	/* Set parameters for the MM_BCN_CHANGE_REQ message */
	//req->bcn_ptr = bcn_addr;
	memcpy(req->bcn_buf, (u8 *)bcn_addr, bcn_len);

	req->bcn_len = bcn_len;
	req->tim_oft = tim_oft;
	req->tim_len = tim_len;
	req->inst_nbr = vif_idx;

	if (csa_oft) {
		int i;
		for (i = 0; i < BCN_MAX_CSA_CPT; i++) {
			req->csa_oft[i] = csa_oft[i];
		}
	}

	/* Send the MM_BCN_CHANGE_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, MM_BCN_CHANGE_CFM);
}

int rwnx_send_roc(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
		  struct ieee80211_channel *chan, unsigned int duration)
{
	struct mm_remain_on_channel_req *req;
	struct cfg80211_chan_def chandef;

	ENTER();

	/* Create channel definition structure */
	cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_NO_HT);

	/* Build the MM_REMAIN_ON_CHANNEL_REQ message */
	req = rwnx_msg_zalloc(MM_REMAIN_ON_CHANNEL_REQ, TASK_MM,
			      sizeof(struct mm_remain_on_channel_req));
	if (!req)
		return -ENOMEM;

	mutex_lock(&rwnx_hw->mutex);
	/* Add RoC Timer to avoid mutex_lock always handled */
	mod_timer(&rwnx_hw->roc_timer, jiffies + duration + 1000);

	/* Set parameters for the MM_REMAIN_ON_CHANNEL_REQ message */
	req->op_code = MM_ROC_OP_START;
	req->vif_index = vif->vif_index;
	req->duration_ms = duration;
	cfg80211_to_rwnx_chan(&chandef, &req->chan);

	/* Send the MM_REMAIN_ON_CHANNEL_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, MM_REMAIN_ON_CHANNEL_CFM);
}

int rwnx_send_cancel_roc(struct rwnx_hw *rwnx_hw)
{
	struct mm_remain_on_channel_req *req;

	ENTER();

	/* Build the MM_REMAIN_ON_CHANNEL_REQ message */
	req = rwnx_msg_zalloc(MM_REMAIN_ON_CHANNEL_REQ, TASK_MM,
			      sizeof(struct mm_remain_on_channel_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the MM_REMAIN_ON_CHANNEL_REQ message */
	req->op_code = MM_ROC_OP_CANCEL;

	/* Send the MM_REMAIN_ON_CHANNEL_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, MM_REMAIN_ON_CHANNEL_CFM);
}

int rwnx_send_set_power(struct rwnx_hw *rwnx_hw, u8 vif_idx, s8 pwr,
			struct mm_set_power_cfm *cfm)
{
	struct mm_set_power_req *req;

	ENTER();

	/* Build the MM_SET_POWER_REQ message */
	req = rwnx_msg_zalloc(MM_SET_POWER_REQ, TASK_MM,
			      sizeof(struct mm_set_power_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the MM_SET_POWER_REQ message */
	req->inst_nbr = vif_idx;
	req->power = pwr;
	if (rwnx_hw->monitor_vif != RWNX_INVALID_VIF) {
		rwnx_hw->monitor_param.tx_power = pwr;
	}

	/* Send the MM_SET_POWER_REQ message to LMAC FW */
	return rwnx_send_msg_nonblock(rwnx_hw, req);
}

int rwnx_send_set_edca(struct rwnx_hw *rwnx_hw, u8 hw_queue, u32 param,
		       bool uapsd, u8 inst_nbr)
{
	struct mm_set_edca_req *set_edca_req;

	ENTER();

	/* Build the MM_SET_EDCA_REQ message */
	set_edca_req = rwnx_msg_zalloc(MM_SET_EDCA_REQ, TASK_MM,
				       sizeof(struct mm_set_edca_req));
	if (!set_edca_req)
		return -ENOMEM;

	/* Set parameters for the MM_SET_EDCA_REQ message */
	set_edca_req->ac_param = param;
	set_edca_req->uapsd = uapsd;
	set_edca_req->hw_queue = hw_queue;
	set_edca_req->inst_nbr = inst_nbr;

	/* Send the MM_SET_EDCA_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, set_edca_req, MM_SET_EDCA_CFM);
}

int rwnx_send_set_vif_state_req(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 aid,
				bool active)
{
	struct mm_set_vif_state_req *set_vif_state_req;
	ENTER();

	set_vif_state_req =
		rwnx_msg_zalloc(MM_SET_VIF_STATE_REQ, TASK_MM,
				sizeof(struct mm_set_vif_state_req));
	if (!set_vif_state_req)
		return -ENOMEM;

	set_vif_state_req->active = active;
	set_vif_state_req->inst_nbr = vif_idx;
	set_vif_state_req->aid = aid;

	LEAVE();

	return rwnx_send_msg(rwnx_hw, set_vif_state_req, MM_SET_VIF_STATE_CFM);
}

#ifdef CONFIG_RWNX_P2P_DEBUGFS
int rwnx_send_p2p_oppps_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
			    u8 ctw, struct mm_set_p2p_oppps_cfm *cfm)
{
	struct mm_set_p2p_oppps_req *p2p_oppps_req;
	int error;

	ENTER();

	/* Build the MM_SET_P2P_OPPPS_REQ message */
	p2p_oppps_req = rwnx_msg_zalloc(MM_SET_P2P_OPPPS_REQ, TASK_MM,
					sizeof(struct mm_set_p2p_oppps_req));

	if (!p2p_oppps_req) {
		return -ENOMEM;
	}

	/* Fill the message parameters */
	p2p_oppps_req->vif_index = rwnx_vif->vif_index;
	p2p_oppps_req->ctwindow = ctw;

	/* Send the MM_P2P_OPPPS_REQ message to LMAC FW */
	error = RWNX_SEND_MSG_EX(rwnx_hw, p2p_oppps_req, MM_SET_P2P_OPPPS_CFM,
				 cfm);

	return (error);
}

int rwnx_send_p2p_noa_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
			  int count, int interval, int duration, bool dyn_noa,
			  struct mm_set_p2p_noa_cfm *cfm)
{
	struct mm_set_p2p_noa_req *p2p_noa_req;
	int error;

	ENTER();

	/* Param check */
	if (count > 255)
		count = 255;

	if (duration >= interval) {
		dev_err(rwnx_hw->dev,
			"Invalid p2p NOA config: interval=%d <= duration=%d\n",
			interval, duration);
		return -EINVAL;
	}

	/* Build the MM_SET_P2P_NOA_REQ message */
	p2p_noa_req = rwnx_msg_zalloc(MM_SET_P2P_NOA_REQ, TASK_MM,
				      sizeof(struct mm_set_p2p_noa_req));

	if (!p2p_noa_req) {
		return -ENOMEM;
	}

	/* Fill the message parameters */
	p2p_noa_req->vif_index = rwnx_vif->vif_index;
	p2p_noa_req->noa_inst_nb = 0;
	p2p_noa_req->count = count;

	if (count) {
		p2p_noa_req->duration_us = duration * 1024;
		p2p_noa_req->interval_us = interval * 1024;
		p2p_noa_req->start_offset = (interval - duration - 10) * 1024;
		p2p_noa_req->dyn_noa = dyn_noa;
	}

	/* Send the MM_SET_2P_NOA_REQ message to LMAC FW */
	error = RWNX_SEND_MSG_EX(rwnx_hw, p2p_noa_req, MM_SET_P2P_NOA_CFM, cfm);

	return (error);
}
#endif /* CONFIG_RWNX_P2P_DEBUGFS */

/******************************************************************************
 *    Control messages handling functions (FULLMAC only)
 *****************************************************************************/
int rwnx_write_reg(struct rwnx_hw *rwnx_hw, u32 addr, u32 val)
{
	struct rwnx_write_reg_cfg *req;
	struct rwnx_write_reg_cfm cfm;
	int ret;

	BUG_ON(in_atomic());

	/* Build the message */
	req = rwnx_msg_zalloc(MM_REG_WRITE_REQ, TASK_MM,
			      sizeof(struct rwnx_write_reg_cfg));
	if (!req)
		return -ENOMEM;

	req->addr = addr;
	req->value = val;

	ret = RWNX_SEND_MSG_EX(rwnx_hw, req, MM_REG_WRITE_CFM, &cfm);

	return cfm.result;
}

int rwnx_read_reg(struct rwnx_hw *rwnx_hw, u32 addr, u8 *buf, int buf_len)
{
	struct rwnx_read_reg_cfg *readreg;
	struct rwnx_read_reg_cfm cfm;
	int ret;

	BUG_ON(in_atomic());

	/* Build the message */
	readreg = rwnx_msg_zalloc(MM_REG_READ_REQ, TASK_MM,
				  sizeof(struct rwnx_read_reg_cfg));
	if (!readreg)
		return -ENOMEM;

	readreg->addr = addr;
	readreg->len = buf_len;

	ret = RWNX_SEND_MSG_EX(rwnx_hw, readreg, MM_REG_READ_CFM, &cfm);

	if (cfm.result == REG_CFM_SUCC)
		memcpy(buf, &cfm.value, sizeof(cfm.value));

	return cfm.result;
}

int rwnx_write_reg32(struct rwnx_hw *rwnx_hw, u32 addr, u32 val)
{
	int result;

	WQ_DBG(DM_GENERIC, DL_INF, "[%s:%d] addr:0x%x, val:0x%x\n", __func__,
	       __LINE__, addr, val);
	result = rwnx_write_reg(rwnx_hw, addr, val);
	if (result != REG_CFM_SUCC)
		WQ_DBG(DM_GENERIC, DL_ERR, "%s failed! result: %d\n", __func__,
		       result);

	return result;
}

u32 rwnx_read_reg32(struct rwnx_hw *rwnx_hw, u32 addr)
{
	u32 val = 0;
	int result;

	result = rwnx_read_reg(rwnx_hw, addr, (u8 *)&val, sizeof(val));
	if (result != REG_CFM_SUCC)
		WQ_DBG(DM_GENERIC, DL_ERR, "[%s:%d] failed! result: %d\n",
		       __func__, __LINE__, result);
	else
		WQ_DBG(DM_GENERIC, DL_INF, "[%s:%d] addr:0x%x, val:0x%x\n",
		       __func__, __LINE__, addr, val);

	return val;
}

int rwnx_send_me_config_req(struct rwnx_hw *rwnx_hw)
{
	struct me_config_req *req;
	struct wiphy *wiphy = rwnx_hw->wiphy;
	struct ieee80211_sta_ht_cap *ht_cap;
	struct ieee80211_sta_vht_cap *vht_cap;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
	struct ieee80211_sta_he_cap const *he_cap;
#endif
	uint8_t *ht_mcs;
	int i;

#ifdef SUPPORT_5G_BAND
	ht_cap = &wiphy->bands[NL80211_BAND_5GHZ]->ht_cap;
	vht_cap = &wiphy->bands[NL80211_BAND_5GHZ]->vht_cap;
#else
	struct ieee80211_sta_vht_cap tmp;
	ht_cap = &wiphy->bands[NL80211_BAND_2GHZ]->ht_cap;
	memset(&tmp, 0, sizeof(struct ieee80211_sta_vht_cap));
	vht_cap = &tmp;
#endif
	ht_mcs = (uint8_t *)&ht_cap->mcs;

	ENTER();

	/* Build the ME_CONFIG_REQ message */
	req = rwnx_msg_zalloc(ME_CONFIG_REQ, TASK_ME,
			      sizeof(struct me_config_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the ME_CONFIG_REQ message */
	req->ht_supp = ht_cap->ht_supported;
	req->vht_supp = vht_cap->vht_supported;
	req->ht_cap.ht_capa_info = cpu_to_le16(ht_cap->cap);
	req->ht_cap.a_mpdu_param = ht_cap->ampdu_factor |
				   (ht_cap->ampdu_density
				    << IEEE80211_HT_AMPDU_PARM_DENSITY_SHIFT);
	for (i = 0; i < sizeof(ht_cap->mcs.rx_mask); i++)
		req->ht_cap.mcs_rate[i] = ht_mcs[i];
	req->ht_cap.ht_extended_capa = 0;
	req->ht_cap.tx_beamforming_capa = 0;
	req->ht_cap.asel_capa = 0;

	req->vht_cap.vht_capa_info = cpu_to_le32(vht_cap->cap);
	req->vht_cap.rx_highest = cpu_to_le16(vht_cap->vht_mcs.rx_highest);
	req->vht_cap.rx_mcs_map = cpu_to_le16(vht_cap->vht_mcs.rx_mcs_map);
	req->vht_cap.tx_highest = cpu_to_le16(vht_cap->vht_mcs.tx_highest);
	req->vht_cap.tx_mcs_map = cpu_to_le16(vht_cap->vht_mcs.tx_mcs_map);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
	if (wiphy->bands[NL80211_BAND_5GHZ] != NULL &&
	    wiphy->bands[NL80211_BAND_5GHZ]->iftype_data != NULL) {
		he_cap = &wiphy->bands[NL80211_BAND_5GHZ]->iftype_data->he_cap;

		req->he_supp = he_cap->has_he;
		for (i = 0; i < ARRAY_SIZE(he_cap->he_cap_elem.mac_cap_info);
		     i++) {
			req->he_cap.mac_cap_info[i] =
				he_cap->he_cap_elem.mac_cap_info[i];
		}
		for (i = 0; i < ARRAY_SIZE(he_cap->he_cap_elem.phy_cap_info);
		     i++) {
			req->he_cap.phy_cap_info[i] =
				he_cap->he_cap_elem.phy_cap_info[i];
		}
		if (!rwnx_hw->mod_params.bfmee) {
			req->he_cap.phy_cap_info[4] &=
				~IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE;
			req->he_cap.phy_cap_info[4] &=
				~IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4;
		}
		req->he_cap.mcs_supp.rx_mcs_80 =
			cpu_to_le16(he_cap->he_mcs_nss_supp.rx_mcs_80);
		req->he_cap.mcs_supp.tx_mcs_80 =
			cpu_to_le16(he_cap->he_mcs_nss_supp.tx_mcs_80);
		req->he_cap.mcs_supp.rx_mcs_160 =
			cpu_to_le16(he_cap->he_mcs_nss_supp.rx_mcs_160);
		req->he_cap.mcs_supp.tx_mcs_160 =
			cpu_to_le16(he_cap->he_mcs_nss_supp.tx_mcs_160);
		req->he_cap.mcs_supp.rx_mcs_80p80 =
			cpu_to_le16(he_cap->he_mcs_nss_supp.rx_mcs_80p80);
		req->he_cap.mcs_supp.tx_mcs_80p80 =
			cpu_to_le16(he_cap->he_mcs_nss_supp.tx_mcs_80p80);
		for (i = 0; i < MAC_HE_PPE_THRES_MAX_LEN; i++) {
			req->he_cap.ppe_thres[i] = he_cap->ppe_thres[i];
		}
		req->he_ul_on = rwnx_hw->mod_params.he_ul_on;
	}
#else
#ifdef WQ_HE_STA
	if (wiphy->bands[NL80211_BAND_5GHZ] != NULL &&
	    rwnx_hw->mod_params.he_on) {
		req->he_supp = true;
		for (i = 0; i < ARRAY_SIZE(rwnx_hw->he_cap.mac_cap_info); i++) {
			req->he_cap.mac_cap_info[i] =
				rwnx_hw->he_cap.mac_cap_info[i];
		}
		for (i = 0; i < ARRAY_SIZE(rwnx_hw->he_cap.phy_cap_info); i++) {
			req->he_cap.phy_cap_info[i] =
				rwnx_hw->he_cap.phy_cap_info[i];
		}
		req->he_cap.mcs_supp.rx_mcs_80 =
			cpu_to_le16(rwnx_hw->he_cap.mcs_supp.rx_mcs_80);
		req->he_cap.mcs_supp.tx_mcs_80 =
			cpu_to_le16(rwnx_hw->he_cap.mcs_supp.tx_mcs_80);
		req->he_cap.mcs_supp.rx_mcs_160 =
			cpu_to_le16(rwnx_hw->he_cap.mcs_supp.rx_mcs_160);
		req->he_cap.mcs_supp.tx_mcs_160 =
			cpu_to_le16(rwnx_hw->he_cap.mcs_supp.tx_mcs_160);
		req->he_cap.mcs_supp.rx_mcs_80p80 =
			cpu_to_le16(rwnx_hw->he_cap.mcs_supp.rx_mcs_80p80);
		req->he_cap.mcs_supp.tx_mcs_80p80 =
			cpu_to_le16(rwnx_hw->he_cap.mcs_supp.tx_mcs_80p80);
		for (i = 0; i < MAC_HE_PPE_THRES_MAX_LEN; i++) {
			req->he_cap.ppe_thres[i] = rwnx_hw->he_cap.ppe_thres[i];
		}
		req->he_ul_on = rwnx_hw->mod_params.he_ul_on;
	}
#else
	req->he_supp = false;
	req->he_ul_on = false;
#endif
#endif
	req->ps_on = rwnx_hw->mod_params.ps_on;
	req->dpsm = rwnx_hw->mod_params.dpsm;
	req->tx_lft = rwnx_hw->mod_params.tx_lft;
	req->ant_div_on = rwnx_hw->mod_params.ant_div;
	if (rwnx_hw->mod_params.use_80)
		req->phy_bw_max = PHY_CHNL_BW_80;
	else if (rwnx_hw->mod_params.use_2040)
		req->phy_bw_max = PHY_CHNL_BW_40;
	else
		req->phy_bw_max = PHY_CHNL_BW_20;

	wiphy_info(wiphy, "HT supp %d, VHT supp %d, HE supp %d\n", req->ht_supp,
		   req->vht_supp, req->he_supp);

	/* Send the ME_CONFIG_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, ME_CONFIG_CFM);
}

int rwnx_send_me_chan_config_req(struct rwnx_hw *rwnx_hw)
{
	struct me_chan_config_req *req;
	struct wiphy *wiphy = rwnx_hw->wiphy;
	int i;

	ENTER();

	/* Build the ME_CHAN_CONFIG_REQ message */
	req = rwnx_msg_zalloc(ME_CHAN_CONFIG_REQ, TASK_ME,
			      sizeof(struct me_chan_config_req));
	if (!req)
		return -ENOMEM;

	req->chan2G4_cnt = 0;
	if (wiphy->bands[NL80211_BAND_2GHZ] != NULL) {
		struct ieee80211_supported_band *b =
			wiphy->bands[NL80211_BAND_2GHZ];
		for (i = 0; i < b->n_channels; i++) {
			req->chan2G4[req->chan2G4_cnt].flags = 0;
			if (b->channels[i].flags & IEEE80211_CHAN_DISABLED)
				req->chan2G4[req->chan2G4_cnt].flags |=
					CHAN_DISABLED;
			req->chan2G4[req->chan2G4_cnt].flags |=
				get_chan_flags(b->channels[i].flags);
			req->chan2G4[req->chan2G4_cnt].band = NL80211_BAND_2GHZ;
			req->chan2G4[req->chan2G4_cnt].freq =
				b->channels[i].center_freq;
			req->chan2G4[req->chan2G4_cnt].tx_power =
				chan_to_fw_pwr(b->channels[i].max_power);
			req->chan2G4_cnt++;
			if (req->chan2G4_cnt == MAC_DOMAINCHANNEL_24G_MAX)
				break;
		}
	}

	req->chan5G_cnt = 0;
	if (wiphy->bands[NL80211_BAND_5GHZ] != NULL) {
		struct ieee80211_supported_band *b =
			wiphy->bands[NL80211_BAND_5GHZ];
		for (i = 0; i < b->n_channels; i++) {
			req->chan5G[req->chan5G_cnt].flags = 0;
			if (b->channels[i].flags & IEEE80211_CHAN_DISABLED)
				req->chan5G[req->chan5G_cnt].flags |=
					CHAN_DISABLED;
			req->chan5G[req->chan5G_cnt].flags |=
				get_chan_flags(b->channels[i].flags);
			req->chan5G[req->chan5G_cnt].band = NL80211_BAND_5GHZ;
			req->chan5G[req->chan5G_cnt].freq =
				b->channels[i].center_freq;
			req->chan5G[req->chan5G_cnt].tx_power =
				chan_to_fw_pwr(b->channels[i].max_power);
			req->chan5G_cnt++;
			if (req->chan5G_cnt == MAC_DOMAINCHANNEL_5G_MAX)
				break;
		}
	}

	/* Send the ME_CHAN_CONFIG_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, ME_CHAN_CONFIG_CFM);
}

int rwnx_send_me_set_control_port_req(struct rwnx_hw *rwnx_hw, bool opened,
				      u8 sta_idx)
{
	struct me_set_control_port_req *req;
	int error;

	ENTER();

	/* Build the ME_SET_CONTROL_PORT_REQ message */
	req = rwnx_msg_zalloc(ME_SET_CONTROL_PORT_REQ, TASK_ME,
			      sizeof(struct me_set_control_port_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the ME_SET_CONTROL_PORT_REQ message */
	req->sta_idx = sta_idx;
	req->control_port_open = opened;

	mutex_lock(&rwnx_hw->mutex);
	/* Send the ME_SET_CONTROL_PORT_REQ message to LMAC FW */
	error = rwnx_send_msg(rwnx_hw, req, ME_SET_CONTROL_PORT_CFM);
	mutex_unlock(&rwnx_hw->mutex);

	return error;
}

int rwnx_monitor_me_sta_add(struct rwnx_hw *rwnx_hw, u8 vif_idx)
{
	struct me_sta_add_req *req;
	struct me_sta_add_cfm cfm;
	int result;

	ENTER();

	/* Build the MM_STA_ADD_REQ message */
	req = rwnx_msg_zalloc(ME_STA_ADD_REQ, TASK_ME,
			      sizeof(struct me_sta_add_req));
	if (!req)
		return -ENOMEM;

	req->flags = STA_HT_CAPA | STA_VHT_CAPA | STA_HE_CAPA | STA_QOS_CAPA;
	req->vif_idx = vif_idx;
	req->he_cap.phy_cap_info[0] =
		IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G;
	if (rwnx_hw->mod_params.ldpc_on)
		req->he_cap.phy_cap_info[1] |=
			IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD;
	// max supported ampdu length set to (1 << (13 + 7 + 0)) - 1 = 1MB - 1
	req->vht_cap.vht_capa_info =
		IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK;
	req->he_cap.mac_cap_info[3] =
		IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_0;

	if (rwnx_hw->mod_params.stbc_on)
		req->ht_cap.ht_capa_info |= IEEE80211_HT_CAP_TX_STBC;

	/* Send the ME_STA_ADD_REQ message to LMAC FW */
	result = RWNX_SEND_MSG_EX(rwnx_hw, req, ME_STA_ADD_CFM, &cfm);

	if (result == 0) {
		result = cfm.sta_idx;
	}

	LEAVE();
	return result;
}

int rwnx_send_me_sta_add(struct rwnx_hw *rwnx_hw,
			 struct station_parameters *params, const u8 *mac,
			 u8 inst_nbr, struct me_sta_add_cfm *cfm)
{
	struct me_sta_add_req *req;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
	u8 *ht_mcs = (u8 *)&params->link_sta_params.ht_capa->mcs;
#else
	u8 *ht_mcs = (u8 *)&params->ht_capa->mcs;
#endif
	int i;

	ENTER();

	/* Build the MM_STA_ADD_REQ message */
	req = rwnx_msg_zalloc(ME_STA_ADD_REQ, TASK_ME,
			      sizeof(struct me_sta_add_req));
	if (!req)
		return -ENOMEM;

		/* Set parameters for the MM_STA_ADD_REQ message */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	ether_addr_copy((u8 *)&req->mac_addr, mac);
#else
	(void)memcpy((u8 *)&req->mac_addr, mac, ETH_ALEN);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
	req->rate_set.length = params->link_sta_params.supported_rates_len;
	for (i = 0; i < params->link_sta_params.supported_rates_len; i++)
		req->rate_set.array[i] =
			params->link_sta_params.supported_rates[i];
#else
	req->rate_set.length = params->supported_rates_len;
	for (i = 0; i < params->supported_rates_len; i++)
		req->rate_set.array[i] = params->supported_rates[i];
#endif

	req->flags = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
	if (params->link_sta_params.ht_capa) {
		const struct ieee80211_ht_cap *ht_capa =
			params->link_sta_params.ht_capa;
#else
	if (params->ht_capa) {
		const struct ieee80211_ht_cap *ht_capa = params->ht_capa;
#endif

		req->flags |= STA_HT_CAPA;
		req->ht_cap.ht_capa_info = cpu_to_le16(ht_capa->cap_info);
		req->ht_cap.a_mpdu_param = ht_capa->ampdu_params_info;
		for (i = 0; i < sizeof(ht_capa->mcs); i++)
			req->ht_cap.mcs_rate[i] = ht_mcs[i];
		req->ht_cap.ht_extended_capa =
			cpu_to_le16(ht_capa->extended_ht_cap_info);
		req->ht_cap.tx_beamforming_capa =
			cpu_to_le32(ht_capa->tx_BF_cap_info);
		req->ht_cap.asel_capa = ht_capa->antenna_selection_info;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
	if (params->link_sta_params.vht_capa) {
		const struct ieee80211_vht_cap *vht_capa =
			params->link_sta_params.vht_capa;
#else
	if (params->vht_capa) {
		const struct ieee80211_vht_cap *vht_capa = params->vht_capa;
#endif

		req->flags |= STA_VHT_CAPA;
		req->vht_cap.vht_capa_info =
			cpu_to_le32(vht_capa->vht_cap_info);
		req->vht_cap.rx_highest =
			cpu_to_le16(vht_capa->supp_mcs.rx_highest);
		req->vht_cap.rx_mcs_map =
			cpu_to_le16(vht_capa->supp_mcs.rx_mcs_map);
		req->vht_cap.tx_highest =
			cpu_to_le16(vht_capa->supp_mcs.tx_highest);
		req->vht_cap.tx_mcs_map =
			cpu_to_le16(vht_capa->supp_mcs.tx_mcs_map);
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
	if (params->link_sta_params.he_capa) {
		const struct ieee80211_he_cap_elem *he_capa =
			params->link_sta_params.he_capa;
#else
	if (params->he_capa) {
		const struct ieee80211_he_cap_elem *he_capa = params->he_capa;
#endif
		struct ieee80211_he_mcs_nss_supp *mcs_nss_supp =
			(struct ieee80211_he_mcs_nss_supp *)(he_capa + 1);

		req->flags |= STA_HE_CAPA;
		for (i = 0; i < ARRAY_SIZE(he_capa->mac_cap_info); i++) {
			req->he_cap.mac_cap_info[i] = he_capa->mac_cap_info[i];
		}
		for (i = 0; i < ARRAY_SIZE(he_capa->phy_cap_info); i++) {
			req->he_cap.phy_cap_info[i] = he_capa->phy_cap_info[i];
		}
		req->he_cap.mcs_supp.rx_mcs_80 = mcs_nss_supp->rx_mcs_80;
		req->he_cap.mcs_supp.tx_mcs_80 = mcs_nss_supp->tx_mcs_80;
		req->he_cap.mcs_supp.rx_mcs_160 = mcs_nss_supp->rx_mcs_160;
		req->he_cap.mcs_supp.tx_mcs_160 = mcs_nss_supp->tx_mcs_160;
		req->he_cap.mcs_supp.rx_mcs_80p80 = mcs_nss_supp->rx_mcs_80p80;
		req->he_cap.mcs_supp.tx_mcs_80p80 = mcs_nss_supp->tx_mcs_80p80;
	}
#endif

	if (params->sta_flags_set & BIT(NL80211_STA_FLAG_WME))
		req->flags |= STA_QOS_CAPA;

	if (params->sta_flags_set & BIT(NL80211_STA_FLAG_MFP))
		req->flags |= STA_MFP_CAPA;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
	if (params->link_sta_params.opmode_notif_used) {
		req->flags |= STA_OPMOD_NOTIF;
		req->opmode = params->link_sta_params.opmode_notif;
	}
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	if (params->opmode_notif_used) {
		req->flags |= STA_OPMOD_NOTIF;
		req->opmode = params->opmode_notif;
	}
#endif

	req->aid = cpu_to_le16(params->aid);
	req->uapsd_queues = params->uapsd_queues;
	req->max_sp_len = params->max_sp * 2;
	req->vif_idx = inst_nbr;

	if (params->sta_flags_set & BIT(NL80211_STA_FLAG_TDLS_PEER)) {
		struct rwnx_vif *rwnx_vif = rwnx_hw->vif_table[inst_nbr];
		req->tdls_sta = true;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
		if ((params->ext_capab[3] & WLAN_EXT_CAPA4_TDLS_CHAN_SWITCH) &&
		    !rwnx_vif->tdls_chsw_prohibited)
			req->tdls_chsw_allowed = true;
#endif
		if (rwnx_vif->tdls_status == TDLS_SETUP_RSP_TX)
			req->tdls_initiator = true;
	}

	/* Send the ME_STA_ADD_REQ message to LMAC FW */
	return RWNX_SEND_MSG_EX(rwnx_hw, req, ME_STA_ADD_CFM, cfm);
}

int rwnx_send_me_sta_del(struct rwnx_hw *rwnx_hw, u8 sta_idx, bool tdls_sta)
{
	struct me_sta_del_req *req;

	ENTER();

	/* Build the MM_STA_DEL_REQ message */
	req = rwnx_msg_zalloc(ME_STA_DEL_REQ, TASK_ME,
			      sizeof(struct me_sta_del_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the MM_STA_DEL_REQ message */
	req->sta_idx = sta_idx;
	req->tdls_sta = tdls_sta;

	/* Send the ME_STA_DEL_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, ME_STA_DEL_CFM);
}

int rwnx_send_me_traffic_ind(struct rwnx_hw *rwnx_hw, u8 sta_idx, bool uapsd,
			     u8 tx_status)
{
	struct me_traffic_ind_req *req;

	ENTER();

	/* Build the ME_UTRAFFIC_IND_REQ message */
	req = rwnx_msg_zalloc(ME_TRAFFIC_IND_REQ, TASK_ME,
			      sizeof(struct me_traffic_ind_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the ME_TRAFFIC_IND_REQ message */
	req->sta_idx = sta_idx;
	req->tx_avail = tx_status;
	req->uapsd = uapsd;

	/* Send the ME_TRAFFIC_IND_REQ to UMAC FW */
	return rwnx_send_msg_nonblock(rwnx_hw, req);
}

int rwnx_send_twt_request(struct rwnx_hw *rwnx_hw, u8 setup_type, u8 vif_idx,
			  struct twt_conf_tag *conf, struct twt_setup_cfm *cfm)
{
	struct twt_setup_req *req;

	ENTER();

	/* Build the TWT_SETUP_REQ message */
	req = rwnx_msg_zalloc(TWT_SETUP_REQ, TASK_TWT,
			      sizeof(struct twt_setup_req));
	if (!req)
		return -ENOMEM;

	memcpy(&req->conf, conf, sizeof(req->conf));
	req->setup_type = setup_type;
	req->vif_idx = vif_idx;

	/* Send the TWT_SETUP_REQ message to LMAC FW */
	return RWNX_SEND_MSG_EX(rwnx_hw, req, TWT_SETUP_CFM, cfm);
}

int rwnx_send_twt_teardown(struct rwnx_hw *rwnx_hw,
			   struct twt_teardown_req *twt_teardown,
			   struct twt_teardown_cfm *cfm)
{
	struct twt_teardown_req *req;

	ENTER();

	/* Build the TWT_TEARDOWN_REQ message */
	req = rwnx_msg_zalloc(TWT_TEARDOWN_REQ, TASK_TWT,
			      sizeof(struct twt_teardown_req));
	if (!req)
		return -ENOMEM;

	memcpy(req, twt_teardown, sizeof(struct twt_teardown_req));

	/* Send the TWT_TEARDOWN_REQ message to LMAC FW */
	return RWNX_SEND_MSG_EX(rwnx_hw, req, TWT_TEARDOWN_CFM, cfm);
}

int rwnx_send_me_rc_stats(struct rwnx_hw *rwnx_hw, u8 sta_idx,
			  struct me_rc_stats_cfm *cfm)
{
	struct me_rc_stats_req *req;

	ENTER();

	/* Build the ME_RC_STATS_REQ message */
	req = rwnx_msg_zalloc(ME_RC_STATS_REQ, TASK_ME,
			      sizeof(struct me_rc_stats_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the ME_RC_STATS_REQ message */
	req->sta_idx = sta_idx;

	/* Send the ME_RC_STATS_REQ message to LMAC FW */
	return RWNX_SEND_MSG_EX(rwnx_hw, req, ME_RC_STATS_CFM, cfm);
}

int rwnx_send_me_rc_set_rate(struct rwnx_hw *rwnx_hw, u8 sta_idx, u16 rate_cfg)
{
	struct me_rc_set_rate_req *req;

	ENTER();

	/* Build the ME_RC_SET_RATE_REQ message */
	req = rwnx_msg_zalloc(ME_RC_SET_RATE_REQ, TASK_ME,
			      sizeof(struct me_rc_set_rate_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the ME_RC_SET_RATE_REQ message */
	req->sta_idx = sta_idx;
	req->fixed_rate_cfg = rate_cfg;

	/* Send the ME_RC_SET_RATE_REQ message to FW */
	return rwnx_send_msg_nonblock(rwnx_hw, req);
}

int rwnx_send_me_set_ps_mode(struct rwnx_hw *rwnx_hw, u8 ps_mode)
{
	struct me_set_ps_mode_req *req;

	ENTER();
	if (!(wq_conf.ps_mode & BIT(0))) {
		ps_mode = PS_MODE_OFF;
	}
	WQ_DBG(DM_GENERIC, DL_ERR, "%s vif_started=%d, PS=%d\n", __func__,
	       rwnx_hw->vif_started, ps_mode);

	/* Build the ME_SET_PS_MODE_REQ message */
	req = rwnx_msg_zalloc(ME_SET_PS_MODE_REQ, TASK_ME,
			      sizeof(struct me_set_ps_mode_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the ME_SET_PS_MODE_REQ message */
	req->ps_state = ps_mode;

	/* Send the ME_SET_PS_MODE_REQ message to FW */
	return rwnx_send_msg(rwnx_hw, req, ME_SET_PS_MODE_CFM);
}

int rwnx_send_me_tx_credit_size_req(struct rwnx_hw *rwnx_hw,
				    struct me_tx_credit_size_cfm *cfm)
{
	void *void_param;

	ENTER();

	/* TX CREDIT SIZE REQ has no parameter */
	void_param = rwnx_msg_zalloc(ME_TX_CREDIT_SIZE_REQ, TASK_ME, 0);
	if (!void_param)
		return -ENOMEM;

	/* Send the ME_TX_CREDIT_SIZE_REQ message to FW */
	return RWNX_SEND_MSG_EX(rwnx_hw, void_param, ME_TX_CREDIT_SIZE_CFM,
				cfm);
}

int rwnx_send_me_set_bus_pwr_state(struct rwnx_hw *rwnx_hw, u8 pwr_state)
{
	struct me_set_bus_pwr_state_req *req;

	ENTER();

	/* Build the ME_SET_BUS_PWR_STATE_REQ message */
	req = rwnx_msg_zalloc(ME_SET_BUS_PWR_STATE_REQ, TASK_ME,
			      sizeof(struct me_set_bus_pwr_state_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the ME_SET_BUS_PWR_STATE_REQ message */
	req->pwr_state = pwr_state;

	/* Send the ME_SET_BUS_PWR_STATE_REQ message to FW */
	return rwnx_send_msg(rwnx_hw, req, ME_SET_BUS_PWR_STATE_CFM);
}

int rwnx_send_me_set_wowlan_req(struct rwnx_hw *rwnx_hw,
				struct cfg80211_wowlan *wowl,
				enum wow_req_type req_type, u32 *wakup_reason)
{
	struct me_set_wowlan_req_v1 *req;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
	struct cfg80211_pkt_pattern *patterns;
#else
	struct cfg80211_wowlan_trig_pkt_pattern *patterns;
#endif
	struct me_wow_pattern *wow_pattern;
	struct me_set_wowlan_cfm cfm;
	u32 wowlan_config = 0;
	u8 i, j;
	int res = 0;

	ENTER();

	if (wowl) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
		WQ_DBG(DM_GENERIC, DL_WRN,
		       "%s: any=%d, disconnect=%d, magic=%d, rekey=%d, eap=%d, 4way=%d, rfkill=%d, n_ptn=%d, ptn=0x%p, tcp=0x%p, nd_cfg=0x%p\n",
		       __func__, wowl->any, wowl->disconnect, wowl->magic_pkt,
		       wowl->gtk_rekey_failure, wowl->eap_identity_req,
		       wowl->four_way_handshake, wowl->rfkill_release,
		       wowl->n_patterns, wowl->patterns, wowl->tcp,
		       wowl->nd_config);
#endif
	} else {
		WQ_DBG(DM_GENERIC, DL_WRN, "%s: wowl is null\n", __func__);
	}

	// host resume req
	if (req_type == WOW_RESUME) {
		/* Build the ME_SET_WOWLAN_REQ message */
		req = rwnx_msg_zalloc(ME_SET_WOWLAN_REQ, TASK_ME, sizeof(*req));
		if (!req)
			return -ENOMEM;

		//if equal to 0, it means resume type
		req->wakeup_type = wowlan_config;

		/* Send the ME_SET_WOWLAN_CFM message to FW */
		if ((res = RWNX_SEND_MSG_EX(rwnx_hw, req, ME_SET_WOWLAN_CFM,
					    &cfm)))
			return res;

		if (cfm.status != 0) {
			WQ_DBG(DM_GENERIC, DL_ERR, "%s: status error(%d)\n",
			       __func__, cfm.status);
			return -EIO;
		}
		*wakup_reason = cfm.wakeup_reason;
		// TODO: cfm process
		return 0;
	}

	// host suspend req
	//	wowlan_config |= WOW_GLOBAL_ENABLE_BIT;
	wowlan_config |= WOW_ARP_OFFLOAD_ENABLE_BIT;
	wowlan_config |= WOW_NS_OFFLOAD_ENABLE_BIT;
	if (wowl) {
		patterns = wowl->patterns;
		if (wowl->disconnect)
			wowlan_config |= WOW_WAKEUP_CONNECTION_LOST_BIT;
		if (wowl->eap_identity_req)
			wowlan_config |= WOW_WAKEUP_802_1X_BIT;
		if (wowl->four_way_handshake)
			wowlan_config |= WOW_WAKEUP_4WAY_HANDSHAKE_BIT;
		if (wowl->gtk_rekey_failure) {
			wowlan_config |= WOW_GTK_OFFLOAD_ENABLE_BIT;
			wowlan_config |= WOW_WAKEUP_GTK_REKEY_FAILURE;
		}
	}
	if (wowl && wowl->n_patterns && patterns) {
		u8 *bytemask;
		wowlan_config |= WOW_WAKEUP_PATTERN_BIT;

		for (i = 0; i < wowl->n_patterns; i++) {
			/* Build the ME_SET_WOWLAN_REQ message */
			req = rwnx_msg_zalloc(
				ME_SET_WOWLAN_REQ, TASK_ME,
				sizeof(*req) + sizeof(*wow_pattern) +
					patterns[i].pattern_len * 2);
			if (!req)
				return -ENOMEM;

			bytemask =
				(u8 *)req->wow_pattern + sizeof(*wow_pattern);

			for (j = 0; j < patterns[i].pattern_len; j++) {
				if (patterns[i].mask[j / 8] & BIT(j % 8)) {
					bytemask[j] = 0xff;
				}
			}

			req->wakeup_type = wowlan_config;
			wow_pattern = req->wow_pattern;
			wow_pattern->length = patterns[i].pattern_len;
			wow_pattern->id = i;
			wow_pattern->offset = patterns[i].pkt_offset;

			memcpy(bytemask + wow_pattern->length,
			       patterns[i].pattern, wow_pattern->length);

			WQ_DBG(DM_GENERIC, DL_WRN,
			       "%s, offset:%d, len:%d, id:%d, wakeup type :0x%x\n",
			       __func__, wow_pattern->offset,
			       wow_pattern->length, i, req->wakeup_type);
			dump_bytes(DL_WRN, "mask:",
				   (u8 *)wow_pattern + sizeof(*wow_pattern),
				   wow_pattern->length);
			dump_bytes(DL_WRN, "pattern:",
				   (u8 *)wow_pattern + sizeof(*wow_pattern) +
					   wow_pattern->length,
				   wow_pattern->length);

			/* Send the ME_SET_WOWLAN_CFM message to FW */
			res = RWNX_SEND_MSG_EX(rwnx_hw, req, ME_SET_WOWLAN_CFM,
					       &cfm);
			if (res || cfm.status) {
				WQ_DBG(DM_GENERIC, DL_ERR,
				       "%s: Failed to send ME_SET_WOWLAN_REQ\n",
				       __func__);
				return res | cfm.status;
			}
		}
	} else {
		/* Build the ME_SET_WOWLAN_REQ message */
		req = rwnx_msg_zalloc(ME_SET_WOWLAN_REQ, TASK_ME, sizeof(*req));
		if (!req)
			return -ENOMEM;

		req->wakeup_type = wowlan_config;
		WQ_DBG(DM_GENERIC, DL_WRN, "%s, wakeup type :0x%x\n", __func__,
		       req->wakeup_type);

		/* Send the ME_SET_WOWLAN_CFM message to FW */
		res = RWNX_SEND_MSG_EX(rwnx_hw, req, ME_SET_WOWLAN_CFM, &cfm);
		if (res || cfm.status) {
			WQ_DBG(DM_GENERIC, DL_ERR,
			       "%s: Failed to send ME_SET_WOWLAN_REQ\n",
			       __func__);
		}
	}

	return res | cfm.status;
}

int rwnx_send_sm_connect_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
			     struct cfg80211_connect_params *sme,
			     struct sm_connect_cfm *cfm)
{
	struct sm_connect_req *req;
	int i, ie_len;
	ke_msg_id_t cmd_id = SM_CONNECT_REQ;
	ke_task_id_t task_id = TASK_SM;
	ke_msg_id_t cfm_id = SM_CONNECT_CFM;
	int is_wep = 0;

	ENTER();

	if (sme->crypto.n_ciphers_pairwise &&
	    ((sme->crypto.ciphers_pairwise[0] == WLAN_CIPHER_SUITE_WEP40) ||
	     (sme->crypto.ciphers_pairwise[0] == WLAN_CIPHER_SUITE_WEP104))) {
		is_wep = 1;
	}
	if ((sme->crypto.cipher_group == WLAN_CIPHER_SUITE_WEP40) ||
	    (sme->crypto.cipher_group == WLAN_CIPHER_SUITE_WEP104)) {
		is_wep = 1;
	}

	rwnx_vif->b_disconnecting = false;

	ie_len = update_connect_req(rwnx_vif, sme);
#ifdef CONFIG_HML
	if (rwnx_vif->is_hml) {
		cmd_id = HML_CONN_START_REQ;
		task_id = TASK_VENDOR_HML;
		cfm_id = HML_CONN_START_CFM;
	}
#endif

	/* Build the SM_CONNECT_REQ message */
	req = rwnx_msg_zalloc(cmd_id, task_id,
			      (sizeof(struct sm_connect_req) + ie_len));

	if (!req)
		return -ENOMEM;

	/* Set parameters for the SM_CONNECT_REQ message */
	if (sme->crypto.n_ciphers_pairwise &&
	    ((sme->crypto.ciphers_pairwise[0] == WLAN_CIPHER_SUITE_WEP40) ||
	     (sme->crypto.ciphers_pairwise[0] == WLAN_CIPHER_SUITE_TKIP) ||
	     (sme->crypto.ciphers_pairwise[0] == WLAN_CIPHER_SUITE_WEP104)))
		req->flags |= DISABLE_HT;

	if (sme->crypto.control_port)
		req->flags |= CONTROL_PORT_HOST;

	if (sme->crypto.control_port_no_encrypt)
		req->flags |= CONTROL_PORT_NO_ENC;

	if (use_pairwise_key(&sme->crypto))
		req->flags |= WPA_WPA2_IN_USE;

	if (sme->mfp == NL80211_MFP_REQUIRED)
		req->flags |= MFP_IN_USE;
	if (is_wep) {
		req->flags |= WEP_CONN_FLG;
	}

	req->ctrl_port_ethertype = sme->crypto.control_port_ethertype;

	if (sme->bssid)
		memcpy(&req->bssid, sme->bssid, ETH_ALEN);
	else
		eth_broadcast_addr((u8 *)&req->bssid);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0)
	if (sme->prev_bssid)
		req->flags |= REASSOCIATION;
#else
	if (rwnx_vif->sta.ap)
		req->flags |= REASSOCIATION;
#endif

	if ((sme->auth_type == NL80211_AUTHTYPE_FT) &&
	    (rwnx_vif->sta.flags & RWNX_STA_FT_OVER_DS))
		req->flags |= (REASSOCIATION | FT_OVER_DS);

	req->vif_idx = rwnx_vif->vif_index;
	if (sme->channel) {
		req->chan.band = sme->channel->band;
		req->chan.freq = sme->channel->center_freq;
		req->chan.flags = get_chan_flags(sme->channel->flags);
	} else {
		req->chan.freq = (u16)-1;
	}
	for (i = 0; i < sme->ssid_len; i++)
		req->ssid.array[i] = sme->ssid[i];
	req->ssid.length = sme->ssid_len;

	req->listen_interval = rwnx_hw->mod_params.listen_itv;
	req->dont_wait_bcmc = !rwnx_hw->mod_params.listen_bcmc;

	/* Set auth_type */
	if ((sme->auth_type == NL80211_AUTHTYPE_OPEN_SYSTEM) ||
	    (sme->auth_type == NL80211_AUTHTYPE_AUTOMATIC && is_wep == 0))
		req->auth_type = WLAN_AUTH_OPEN;
	else if (sme->auth_type == NL80211_AUTHTYPE_SHARED_KEY ||
		 (sme->auth_type == NL80211_AUTHTYPE_AUTOMATIC && is_wep))
		req->auth_type = WLAN_AUTH_SHARED_KEY;
	else if (sme->auth_type == NL80211_AUTHTYPE_FT)
		req->auth_type = WLAN_AUTH_FT;
	else if (sme->auth_type == NL80211_AUTHTYPE_SAE)
		req->auth_type = WLAN_AUTH_SAE;
	else
		goto invalid_param;

	copy_connect_ies(rwnx_vif, req, sme);
	rwnx_hw->connect_req_ts = jiffies;

	/* Set UAPSD queues */
	req->uapsd_queues = rwnx_hw->mod_params.uapsd_queues;
	WQ_DBG(DM_GENERIC, DL_WRN,
	       "Connect to {SSID:%s,BSSID:%pM,Freq:%d} AP\n",
	       &req->ssid.array[0], (uint8_t *)&req->bssid, req->chan.freq);
#ifdef CONFIG_HML
#ifdef CONFIG_DEBUG_WQ_PRIV
	if (cmd_id == HML_CONN_START_REQ) {
		WQ_DBG(DM_GENERIC, DL_WRN,
		       "rwnx_send_sm_connect_req:vendor use ep2\n");
		rwnx_send_dbg_wq_priv_test_req(
			rwnx_hw, rwnx_vif, DBG_WQ_PRIV_TO_HML_TASK,
			HML_TO_VENDOR_MSG_CONN_REQ, (char *)req,
			sizeof(struct sm_connect_req) + ie_len);
		rwnx_msg_free(rwnx_hw, req);
		return 0;
	} else
#endif
#endif
	{
		/* Send the SM_CONNECT_REQ message to LMAC FW */
		return RWNX_SEND_MSG_EX(rwnx_hw, req, cfm_id, cfm);
	}

invalid_param:
	rwnx_msg_free(rwnx_hw, req);
	return -EINVAL;
}

int rwnx_send_sm_disconnect_req(struct rwnx_hw *rwnx_hw,
				struct rwnx_vif *rwnx_vif, u16 reason)
{
	struct sm_disconnect_req *req;
	int error;

	ENTER();
#ifdef CONFIG_HML
	if (rwnx_vif->is_hml) {
		WQ_DBG(DM_GENERIC, DL_WRN, "hml should not enter here");
		return -EINVAL;
	}
#endif
	rwnx_vif->b_disconnecting = true;

	/* Build the SM_DISCONNECT_REQ message */
	req = rwnx_msg_zalloc(SM_DISCONNECT_REQ, TASK_SM,
			      sizeof(struct sm_disconnect_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the SM_DISCONNECT_REQ message */
	req->reason_code = reason;
	req->vif_idx = rwnx_vif->vif_index;

	WQ_DBG(DM_GENERIC, DL_WRN,
	       "vif-%d, send SM_DISCONNECT_REQ to fw, reason %d\n",
	       rwnx_vif->vif_index, reason);
	mutex_lock(&rwnx_hw->mutex);
	/* Send the SM_DISCONNECT_REQ message to LMAC FW */
	error = rwnx_send_msg(rwnx_hw, req, SM_DISCONNECT_CFM);
	mutex_unlock(&rwnx_hw->mutex);

	return error;
}

int rwnx_send_sm_external_auth_required_rsp(struct rwnx_hw *rwnx_hw,
					    struct rwnx_vif *rwnx_vif,
					    u16 status)
{
	struct sm_external_auth_required_rsp *rsp;
	ke_msg_id_t cmd_id = SM_EXTERNAL_AUTH_REQUIRED_RSP;
	ke_task_id_t task_id = TASK_SM;
	ENTER();
#ifdef CONFIG_HML
	if (strcmp(HML_IF_NAME, rwnx_vif->ndev->name) == 0) {
		cmd_id = HML_AUTH_REQUIRED_RSP;
		task_id = TASK_VENDOR;
	}
#endif

	/* Build the SM_EXTERNAL_AUTH_CFM message */
	rsp = rwnx_msg_zalloc(cmd_id, task_id,
			      sizeof(struct sm_external_auth_required_rsp));
	if (!rsp)
		return -ENOMEM;

	rsp->status = status;
	rsp->vif_idx = rwnx_vif->vif_index;
#ifdef CONFIG_HML
#ifdef CONFIG_DEBUG_WQ_PRIV
	if (cmd_id == HML_AUTH_REQUIRED_RSP) {
		WQ_DBG(DM_GENERIC, DL_WRN,
		       "rwnx_send_sm_external_auth_required_rsp: ep2\n");
		rwnx_send_dbg_wq_priv_test_req(
			rwnx_hw, rwnx_vif, DBG_WQ_PRIV_TO_HML_TASK,
			HML_TO_VENDOR_MSG_AUTH_RSP, (char *)rsp,
			sizeof(struct sm_external_auth_required_rsp));
		rwnx_msg_free(rwnx_hw, rsp);
		return 0;
	} else
#endif
#endif
	{
		/* send the SM_EXTERNAL_AUTH_REQUIRED_RSP message UMAC FW */
		return rwnx_send_msg_nonblock(rwnx_hw, rsp);
	}
}

int rwnx_send_sm_ft_auth_rsp(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
			     uint8_t *ie, int ie_len)
{
	struct sm_connect_req *rsp;

	rsp = rwnx_msg_zalloc(SM_FT_AUTH_RSP, TASK_SM,
			      (sizeof(struct sm_connect_req) + ie_len));
	if (!rsp)
		return -ENOMEM;

	rsp->vif_idx = rwnx_vif->vif_index;
	rsp->ie_len = ie_len;
	memcpy(rsp->ie_buf, ie, rsp->ie_len);

	return rwnx_send_msg_nonblock(rwnx_hw, rsp);
}

static int rwnx_ap_set_vif_chandef(struct rwnx_vif *vif,
				   const struct cfg80211_chan_def *chandef)
{
	struct ieee80211_channel *chan;

	chan = kzalloc(sizeof(struct ieee80211_channel), GFP_KERNEL);
	if (!chan) {
		return -ENOMEM;
	}

	chan->band = chandef->chan->band;
	chan->center_freq = chandef->chan->center_freq;

	chan->hw_value = chandef->chan->hw_value;
	chan->flags = chandef->chan->flags;
	chan->max_antenna_gain = chandef->chan->max_antenna_gain;
	chan->max_power = chandef->chan->max_power;
	chan->max_reg_power = chandef->chan->max_reg_power;
	chan->beacon_found = chandef->chan->beacon_found;
	chan->orig_flags = chandef->chan->orig_flags;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
	chan->freq_offset = chandef->chan->freq_offset;
#endif
	vif->ap.chandef.chan = chan;
	vif->ap.chandef.width = chandef->width;
	vif->ap.chandef.center_freq1 = chandef->center_freq1;
	vif->ap.chandef.center_freq2 = chandef->center_freq2;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
	vif->ap.chandef.edmg = chandef->edmg;
	vif->ap.chandef.freq1_offset = chandef->freq1_offset;
#endif

	return 0;
}

// store all pwr data whth sap running channel
void rwnx_store_chan_pwr_tab(struct rwnx_vif *vif, u8 band, u32 freq,
			     u8 *pwr_tab)
{
	int channel = (band == (int)NL80211_BAND_2GHZ) ?
				    FREQ_TO_CHAN_24G(freq) :
				    FREQ_TO_CHAN_5G(freq);
	int CHAN_LIST_LEN = wq_reg_data.length / PER_CHAN_SIZE;
	int ch_5g, ch_2g;

	switch (band) {
	case NL80211_BAND_2GHZ:
		for (ch_2g = 0; ch_2g < MAC_DOMAINCHANNEL_24G_MAX; ch_2g++) {
			if (wq_reg_data.chan_pwr_tab[ch_2g].chan_num ==
			    channel) {
				memcpy(pwr_tab,
				       wq_reg_data.chan_pwr_tab[ch_2g]
					       .rate_pwr_tab,
				       PWR_TAB_LEN);
				break;
			}
		}
		break;
	case NL80211_BAND_5GHZ:
		for (ch_5g = 0; ch_5g < CHAN_LIST_LEN; ch_5g++) {
			if (wq_reg_data.chan_pwr_tab[ch_5g].chan_num ==
			    channel) {
				memcpy(pwr_tab,
				       wq_reg_data.chan_pwr_tab[ch_5g]
					       .rate_pwr_tab,
				       PWR_TAB_LEN);
				break;
			}
		}
		break;
	default:
		break;
	}
}

// 1 store all support channels(24g+5g)
// 2 filter pwr data by channels and rate(scan step)
void store_supp_chan_pwr(struct rwnx_hw *rwnx_hw,
			 struct supp_chan_pwr_str *supp_pwr)
{
#define RATE_1M_POS 0
#define RATE_6M_POS 4
	struct wiphy *wiphy = rwnx_hw->wiphy;
	u8 sup_2gchan[MAC_DOMAINCHANNEL_24G_MAX];
	u8 sup_5gchan[MAC_DOMAINCHANNEL_5G_MAX];
	int chn, sup, i;
	int chan_cnt_2g = 0;

	// 1. recode support channels
	if (wiphy->bands[NL80211_BAND_2GHZ] != NULL) {
		struct ieee80211_supported_band *b =
			wiphy->bands[NL80211_BAND_2GHZ];
		for (i = 0; i < b->n_channels; i++) {
			sup_2gchan[i] =
				FREQ_TO_CHAN_24G(b->channels[i].center_freq);
			chan_cnt_2g++;
			if (i == MAC_DOMAINCHANNEL_24G_MAX)
				break;
		}
	}
	if (wiphy->bands[NL80211_BAND_5GHZ] != NULL) {
		struct ieee80211_supported_band *b =
			wiphy->bands[NL80211_BAND_5GHZ];
		for (i = 0; i < b->n_channels; i++) {
			sup_5gchan[i] =
				FREQ_TO_CHAN_5G(b->channels[i].center_freq);
			if (i == MAC_DOMAINCHANNEL_5G_MAX)
				break;
		}
	}

	//2. filter pwr data by channels and rate(scan step)
	for (sup = 0; sup < chan_cnt_2g; sup++) {
		for (chn = sup; chn < wq_reg_data.length / PER_CHAN_SIZE;
		     chn++) {
			if (sup_2gchan[sup] ==
			    wq_reg_data.chan_pwr_tab[chn].chan_num) {
				supp_pwr[sup].channel = sup_2gchan[sup];
				supp_pwr[sup].pwr_data =
					wq_reg_data.chan_pwr_tab[chn].rate_pwr_tab
						[RATE_1M_POS]; //2g mode peobe req rate 1M
				break;
			}
		}
	}

	for (sup = chan_cnt_2g; sup < MAC_DOMAINCHANNEL_5G_MAX + chan_cnt_2g;
	     sup++) {
		for (chn = sup; chn < wq_reg_data.length / PER_CHAN_SIZE;
		     chn++) {
			if (sup_5gchan[sup] ==
			    wq_reg_data.chan_pwr_tab[chn].chan_num) {
				supp_pwr[sup].channel = sup_5gchan[sup];
				supp_pwr[sup].pwr_data =
					wq_reg_data.chan_pwr_tab[chn].rate_pwr_tab
						[RATE_6M_POS]; //5g mode peobe req rate 6M
				break;
			}
		}
	}
	return;
}

void change_format_pwr_data(u8 *pwr, struct mm_chan_pwr_info_req *req)
{
#define FORMAT_NON_HT 0
#define FORMAT_HT 1
#define FORMAT_VHT 2
#define FORMAT_HE 3
#define BW_20MHZ 0
#define BW_40MHZ 1
#define BW_80MHZ 2
#define CHAN_PWR_TAB_LEN 144 //4*3*12
	memset(req->chan_pwr_tab, 0, CHAN_PWR_TAB_LEN);

	memcpy(&req->chan_pwr_tab[FORMAT_NON_HT][BW_20MHZ], pwr,
	       RATE_MAX); // 1M..54M
	pwr += RATE_MAX;
	memcpy(&req->chan_pwr_tab[FORMAT_HT][BW_20MHZ], pwr,
	       RATE_MAX); // HT 20M MCS0-MCS11
	memcpy(&req->chan_pwr_tab[FORMAT_VHT][BW_20MHZ], pwr,
	       RATE_MAX); // VHT 20M MCS0-MCS11
	pwr += RATE_MAX;
	memcpy(&req->chan_pwr_tab[FORMAT_HT][BW_40MHZ], pwr,
	       RATE_MAX); // HT 40M MCS0-MCS11
	memcpy(&req->chan_pwr_tab[FORMAT_VHT][BW_40MHZ], pwr,
	       RATE_MAX); // VHT 40M MCS0-MCS11
	pwr += RATE_MAX;
	memcpy(&req->chan_pwr_tab[FORMAT_VHT][BW_80MHZ], pwr,
	       RATE_MAX); // VHT 80M MCS0-MCS11
	pwr += 4 * RATE_MAX;
	memcpy(&req->chan_pwr_tab[FORMAT_HE][BW_20MHZ], pwr,
	       RATE_MAX); // HE 20M MCS0-MCS11(RU242)
	pwr += RATE_MAX;
	memcpy(&req->chan_pwr_tab[FORMAT_HE][BW_40MHZ], pwr,
	       RATE_MAX); // HE 40M MCS0-MCS11(RU484)
	pwr += RATE_MAX;
	memcpy(&req->chan_pwr_tab[FORMAT_HE][BW_80MHZ], pwr,
	       RATE_MAX); // HE 80M MCS0-MCS11(RU996)

	return;
}

// send apm chan pwr to fw
int rwnx_send_chan_pwr_info_req(struct rwnx_hw *rwnx_hw,
				struct rwnx_vif *rwnx_vif, u8 *pwr, u8 band,
				u32 freq)
{
	struct mm_chan_pwr_info_req *req;
	ENTER();

	req = rwnx_msg_zalloc(MM_CHAN_PWR_INFO_REQ, TASK_MM,
			      sizeof(struct mm_chan_pwr_info_req));
	if (!req)
		return -ENOMEM;
	req->vif_idx = rwnx_vif->vif_index;
	req->channel = (band == NL80211_BAND_2GHZ) ? FREQ_TO_CHAN_24G(freq) :
							   FREQ_TO_CHAN_5G(freq);
	req->enable = 1;
	change_format_pwr_data(pwr, req);

	WQ_DBG(DM_GENERIC, DL_WRN,
	       "rwnx_send_chan_pwr_info_req:send MM_CHAN_PWR_INFO_REQ, vif-%d\n",
	       rwnx_vif->vif_index);
	/* Send the MM_CHAN_PWR_INFO_REQ message to LMAC FW */
	return rwnx_send_msg_nonblock(rwnx_hw, req);
}

int rwnx_send_apm_start_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
			    struct cfg80211_ap_settings *settings,
			    struct apm_start_cfm *cfm,
			    struct rwnx_ipc_elem_var *elem)
{
	struct apm_start_req *req;
	struct rwnx_bcn *bcn = &vif->ap.bcn;
	u8 *buf;
	u8 *buf_new = NULL;
	u32 flags = 0;
	const u8 *rate_ie;
	u8 rate_len = 0;
	int var_offset = offsetof(struct ieee80211_mgmt, u.beacon.variable);
	const u8 *var_pos;
	int len, i;
	struct rwnx_vif *rwnx_vif = NULL;
	struct rwnx_sta *sta = NULL;
	bool need_swt_ch = false;
	// int error;

	ENTER();

	/* Build the APM_START_REQ message */
	req = rwnx_msg_zalloc(APM_START_REQ, TASK_APM,
			      sizeof(struct apm_start_req));
	if (!req)
		return -ENOMEM;

	if (rwnx_ap_set_vif_chandef(vif, &settings->chandef) < 0)
		vif->ap.chandef = settings->chandef;

	spin_lock_bh(&rwnx_hw->cb_lock);
	list_for_each_entry (rwnx_vif, &rwnx_hw->vifs, list) {
		if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION) {
			if (rwnx_vif->sta.ap != NULL &&
			    rwnx_vif->sta.ap->center_freq) {
				sta = rwnx_vif->sta.ap;
				/* STA + SAP Concurrency mode, change SAP ch with STA connected ch */
				if ((settings->chandef.chan->band ==
				     sta->band) &&
				    (sta->center_freq !=
				     settings->chandef.chan->center_freq) &&
				    wq_conf.sap_follow_sta_enable) {
					WQ_DBG(DM_GENERIC, DL_WRN,
					       "SAP following STA ch, sta->width=%d, ap_width=%d\n",
					       sta->width,
					       settings->chandef.width);
					vif->ap.chandef.chan->center_freq =
						(u32)sta->center_freq;
					vif->ap.chandef.center_freq1 =
						sta->center_freq1;
					vif->ap.chandef.center_freq2 =
						sta->center_freq2;
					vif->ap.chandef.chan->hw_value =
						(u32)sta->center_freq;
					/* channel 14 is only for IEEE 802.11b */
					if (vif->ap.chandef.center_freq1 ==
					    2484)
						vif->ap.chandef.width =
							sta->width;
					else
						vif->ap.chandef.width =
							sta->width + 1;
					need_swt_ch = true;
				}
			}
		}
	}
	spin_unlock_bh(&rwnx_hw->cb_lock);

	// Build the beacon
	bcn->dtim = (u8)settings->dtim_period;
	buf = rwnx_build_bcn(bcn, &settings->beacon);
	if (!buf) {
		rwnx_msg_free(rwnx_hw, req);
		return -ENOMEM;
	}

	if (need_swt_ch) {
		buf_new =
			rwnx_bcn_chan_change(bcn, vif->ap.chandef.chan->center_freq,
					vif->ap.chandef.chan->band, false);
		if (!buf_new) {
			need_swt_ch = false;
		} else {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
			cfg80211_ch_switch_notify(vif->ndev, &vif->ap.chandef,
						  0, 0);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
			cfg80211_ch_switch_notify(vif->ndev, &vif->ap.chandef,
						  0);
#else
			cfg80211_ch_switch_notify(vif->ndev, &vif->ap.chandef);
#endif
		}
	}

	// Retrieve the basic rate set from the beacon buffer
	len = bcn->len - var_offset;
	var_pos = buf + var_offset;

// Assume that rate higher that 54 Mbps are BSS membership
#define IS_BASIC_RATE(r) (r & 0x80) && ((r & ~0x80) <= (54 * 2))

	rate_ie = cfg80211_find_ie(WLAN_EID_SUPP_RATES, var_pos, len);
	if (rate_ie) {
		const u8 *rates = rate_ie + 2;
		for (i = 0; (i < rate_ie[1]) && (rate_len < MAC_RATESET_LEN);
		     i++) {
			if (IS_BASIC_RATE(rates[i]))
				req->basic_rates.array[rate_len++] = rates[i];
		}
	}
	rate_ie = cfg80211_find_ie(WLAN_EID_EXT_SUPP_RATES, var_pos, len);
	if (rate_ie) {
		const u8 *rates = rate_ie + 2;
		for (i = 0; (i < rate_ie[1]) && (rate_len < MAC_RATESET_LEN);
		     i++) {
			if (IS_BASIC_RATE(rates[i]))
				req->basic_rates.array[rate_len++] = rates[i];
		}
	}
	req->basic_rates.length = rate_len;
#undef IS_BASIC_RATE

	// Sync buffer for FW
	//if ((error = rwnx_ipc_elem_var_allocs(rwnx_hw, elem, bcn->len,
	//                                      DMA_TO_DEVICE, buf, NULL, NULL))) {
	//    return error;
	//}
	if (need_swt_ch)
		memcpy(req->bcn_buf, buf_new, bcn->len);
	else
		memcpy(req->bcn_buf, buf, bcn->len);

	if (buf) {
		kfree(buf);
		buf = NULL;
	}
	if (buf_new) {
		kfree(buf_new);
		buf_new = NULL;
	}

	/* Set parameters for the APM_START_REQ message */
	req->vif_idx = vif->vif_index;
	//req->bcn_addr = elem->dma_addr;
	req->bcn_len = bcn->len;
	req->tim_oft = bcn->head_len;
	req->tim_len = bcn->tim_len;
	cfg80211_to_rwnx_chan(&vif->ap.chandef, &req->chan);
	req->bcn_int = settings->beacon_interval;
	if (settings->crypto.control_port)
		flags |= CONTROL_PORT_HOST;

	if (settings->crypto.control_port_no_encrypt)
		flags |= CONTROL_PORT_NO_ENC;

	if (use_pairwise_key(&settings->crypto))
		flags |= WPA_WPA2_IN_USE;

	if (settings->crypto.control_port_ethertype)
		req->ctrl_port_ethertype =
			settings->crypto.control_port_ethertype;
	else
		req->ctrl_port_ethertype = ETH_P_PAE;
	req->flags = flags;
	WQ_DBG(DM_GENERIC, DL_WRN,
	       "AP mode configure info: "
	       "band:%s, BW:%s, center_freq:%d,center_freq1 %d,center_freq2:%d\n",
	       (vif->ap.chandef.chan->band == (int)NL80211_BAND_2GHZ) ? "2G" :
									      "5G",
	       (vif->ap.chandef.width == NL80211_CHAN_WIDTH_20_NOHT) ?
									   "NOHT-20M" :
	       (vif->ap.chandef.width == NL80211_CHAN_WIDTH_20)	   ? "20M" :
	       (vif->ap.chandef.width == NL80211_CHAN_WIDTH_40)	   ? "40M" :
	       (vif->ap.chandef.width == NL80211_CHAN_WIDTH_80)	   ? "80M" :
	       (vif->ap.chandef.width == NL80211_CHAN_WIDTH_80P80) ? "80+80M" :
	       (vif->ap.chandef.width == NL80211_CHAN_WIDTH_160)   ? "160M" :
									   "unknow",
	       vif->ap.chandef.chan->center_freq, vif->ap.chandef.center_freq1,
	       vif->ap.chandef.center_freq2);

	/* Send the APM_START_REQ message to LMAC FW */
	return RWNX_SEND_MSG_EX(rwnx_hw, req, APM_START_CFM, cfm);
}

int rwnx_send_apm_stop_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif)
{
	struct apm_stop_req *req;

	ENTER();

	/* Build the APM_STOP_REQ message */
	req = rwnx_msg_zalloc(APM_STOP_REQ, TASK_APM,
			      sizeof(struct apm_stop_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the APM_STOP_REQ message */
	req->vif_idx = vif->vif_index;

	/* Send the APM_STOP_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, APM_STOP_CFM);
}

int rwnx_send_apm_probe_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
			    struct rwnx_sta *sta,
			    struct apm_probe_client_cfm *cfm)
{
	struct apm_probe_client_req *req;

	ENTER();

	req = rwnx_msg_zalloc(APM_PROBE_CLIENT_REQ, TASK_APM,
			      sizeof(struct apm_probe_client_req));
	if (!req)
		return -ENOMEM;

	req->vif_idx = vif->vif_index;
	req->sta_idx = sta->sta_idx;

	/* Send the APM_PROBE_CLIENT_REQ message to UMAC FW */
	return RWNX_SEND_MSG_EX(rwnx_hw, req, APM_PROBE_CLIENT_CFM, cfm);
}

extern struct supp_chan_pwr_str
	wq_supp_pwr[MAC_DOMAINCHANNEL_24G_MAX + MAC_DOMAINCHANNEL_5G_MAX];
int rwnx_send_scanu_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
			struct cfg80211_scan_request *param)
{
	struct scanu_start_req *req;
	int i;
	uint8_t chan_flags = 0;
	uint16_t req_len = offsetof(struct scanu_start_req, ies);

	ENTER();

	if (param->ie) {
		if (param->ie_len > SCANU_MAX_IE_LEN)
			return -EINVAL;

		req_len += param->ie_len;
	}

	/* Build the SCANU_START_REQ message */
	req = rwnx_msg_zalloc(SCANU_START_REQ, TASK_SCANU, req_len);
	if (!req)
		return -ENOMEM;

	/* Set parameters */
	req->vif_idx = rwnx_vif->vif_index;
	req->chan_cnt = (u8)min_t(int, SCAN_CHANNEL_MAX, param->n_channels);
	req->ssid_cnt = (u8)min_t(int, SCAN_SSID_MAX, param->n_ssids);
	eth_broadcast_addr((u8 *)&req->bssid);
	req->no_cck = param->no_cck;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
	if (param->duration_mandatory)
		req->duration = ieee80211_tu_to_usec(param->duration);
#endif

	if (req->ssid_cnt == 0)
		chan_flags |= CHAN_NO_IR;
	for (i = 0; i < req->ssid_cnt; i++) {
		int j;
		for (j = 0; j < param->ssids[i].ssid_len; j++)
			req->ssid[i].array[j] = param->ssids[i].ssid[j];
		req->ssid[i].length = param->ssids[i].ssid_len;
	}

	if (param->ie) {
		/* copy the additional IE to scan request payload, due to in target
           firmware, address of add_ie will be checked if it is NULL,
           we hard code the value 0x41005678 */
		memcpy(req->ies, param->ie, param->ie_len);
		req->add_ies = 0x41005678;

		req->add_ie_len = param->ie_len;
	} else {
		req->add_ie_len = 0;
		req->add_ies = 0;
	}

	WQ_DBG(DM_GENERIC, DL_WRN, "%s req->chan_cnt %d\n", __func__,
	       req->chan_cnt);
	for (i = 0; i < req->chan_cnt; i++) {
		struct ieee80211_channel *chan = param->channels[i];

		req->chan[i].band = chan->band;
		req->chan[i].freq = chan->center_freq;
		req->chan[i].flags = chan_flags | get_chan_flags(chan->flags);
		req->chan[i].tx_power = chan_to_fw_pwr(chan->max_reg_power);
		if (gv_get_pwr_from_bin_flag) {
			int channel_num =
				(chan->band == NL80211_BAND_2GHZ) ?
					      FREQ_TO_CHAN_24G(chan->center_freq) :
					      FREQ_TO_CHAN_5G(chan->center_freq);
			if (wq_supp_pwr[i].channel == channel_num) {
				req->chan[i].tx_power = min(
					chan_to_fw_pwr(chan->max_reg_power),
					(s8)((wq_supp_pwr[i].pwr_data) / 2));
			}
		}
	}

	/* Send the SCANU_START_REQ message to LMAC FW */
	return rwnx_send_msg_nonblock(rwnx_hw, req);
}

int rwnx_send_sched_scan_start_req(struct rwnx_hw *rwnx_hw,
				   struct rwnx_vif *rwnx_vif,
				   struct cfg80211_sched_scan_request *param)
{
	struct scanu_start_req *req;
	int i, j, ret;
	uint8_t chan_flags = 0;
	uint16_t req_len = offsetof(struct scanu_start_req, sscan_param);
	struct mac_addr mac_addr_bcst = { { 0xFFFF, 0xFFFF, 0xFFFF } };

	ENTER();

	if (param->ie) {
		if (param->ie_len > SCANU_MAX_IE_LEN)
			return -EINVAL;
	}

	/* Build the SCANU_START_REQ message */
	req_len += sizeof(struct sched_scan_parameter);
	req = rwnx_msg_zalloc(SCANU_START_REQ, TASK_SCANU, req_len);

	if (!req)
		return -ENOMEM;

		/* Set parameters */
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 10, 0)
	req->sscan_param.reqid = param->reqid;
#endif
	WQ_DBG(DM_GENERIC, DL_WRN, "%s req->sched_scan_conf.reqid: %u",
	       __func__, req->sscan_param.reqid);

	req->vif_idx = rwnx_vif->vif_index;
	req->chan_cnt = (u8)min_t(int, SCAN_CHANNEL_MAX, param->n_channels);
	req->ssid_cnt = (u8)min_t(int, SCAN_SSID_MAX, param->n_ssids);
	req->sscan_param.match_set_cnt =
		(u8)min_t(int, MATCH_SET_MAX, param->n_match_sets);
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 10, 0)
	req->sscan_param.scan_plan_cnt =
		(u8)min_t(int, SCHED_SCAN_PLAN_MAX, param->n_scan_plans);
#endif
	req->no_cck = 0;
	req->bssid = mac_addr_bcst; //It will be modified later
	req->duration = 100; //It will be modified later

	WQ_DBG(DM_GENERIC, DL_WRN, "sched_scan_plan cnt: %u\n",
	       req->sscan_param.scan_plan_cnt);
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 10, 0)
	for (i = 0; i < req->sscan_param.scan_plan_cnt; i++) {
		req->sscan_param.scan_plan[i].interval =
			param->scan_plans[i].interval;
		req->sscan_param.scan_plan[i].iterations =
			param->scan_plans[i].iterations;
		WQ_DBG(DM_GENERIC, DL_WRN,
		       "[%d] interval = %d, iterations = %d\n", i,
		       param->scan_plans[i].interval,
		       param->scan_plans[i].iterations);
	}
#endif
	WQ_DBG(DM_GENERIC, DL_WRN, "match_set cnt: %u\n",
	       req->sscan_param.match_set_cnt);

	for (i = 0; i < req->sscan_param.match_set_cnt; i++) {
		req->sscan_param.match_set[i].ssid.length =
			param->match_sets[i].ssid.ssid_len;
#if LINUX_VERSION_CODE > KERNEL_VERSION(3, 15, 0)
		req->sscan_param.match_set[i].rssi_thold =
			param->match_sets[i].rssi_thold;
#endif
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 10, 0)
		for (j = 0; j < MAC_ADDR_LEN; j++) {
			req->sscan_param.match_set[i].match_bssid[j] =
				param->match_sets[i].bssid[j];
		}
#endif
		for (j = 0; j < param->match_sets[i].ssid.ssid_len; j++) {
			req->sscan_param.match_set[i].ssid.array[j] =
				param->match_sets[i].ssid.ssid[j];
		}

		WQ_DBG(DM_GENERIC, DL_WRN,
		       "sched_scan target {SSID:%s,BSSID:%pM} AP\n",
		       &req->sscan_param.match_set[i].ssid.array[0],
		       &req->sscan_param.match_set[i].match_bssid[0]);
	}

	WQ_DBG(DM_GENERIC, DL_WRN, "sched_scan_SSID cnt: %u\n", req->ssid_cnt);

	if (req->ssid_cnt == 0)
		chan_flags |= CHAN_NO_IR;
	for (i = 0; i < req->ssid_cnt; i++) {
		for (j = 0; j < param->ssids[i].ssid_len; j++) {
			req->ssid[i].array[j] = param->ssids[i].ssid[j];
		}

		req->ssid[i].length = param->ssids[i].ssid_len;
		WQ_DBG(DM_GENERIC, DL_WRN, "sched_scan for {SSID:%s} AP\n",
		       &param->ssids[i].ssid[0]);
	}

	if (param->ie) {
		/* copy the additional IE to scan request payload, due to in target
           firmware, address of add_ie will be checked if it is NULL,
           we hard code the value 0x41005678 */
		memcpy(req->ies, param->ie, param->ie_len);
		req->add_ies = 0x41005678;

		req->add_ie_len = param->ie_len;
	} else {
		req->add_ie_len = 0;
		req->add_ies = 0;
	}

	req->add_ies = SCHED_SCAN_START;

	WQ_DBG(DM_GENERIC, DL_WRN, "%s req->add_ies %08x\n", __func__,
	       req->add_ies);
	WQ_DBG(DM_GENERIC, DL_WRN, "req->chan_cnt: %d\n", req->chan_cnt);

	for (i = 0; i < req->chan_cnt; i++) {
		struct ieee80211_channel *chan = param->channels[i];

		req->chan[i].band = chan->band;
		req->chan[i].freq = chan->center_freq;
		req->chan[i].flags = chan_flags | get_chan_flags(chan->flags);
		req->chan[i].tx_power = chan_to_fw_pwr(chan->max_reg_power);
		WQ_DBG(DM_GENERIC, DL_WRN, "req->freq[%d] = %d\n", i,
		       req->chan[i].freq);
	}

	/* Send the SCHED_SCAN_START_REQ message to LMAC FW */
	ret = rwnx_send_msg_nonblock(rwnx_hw, req);
	if (ret == 0) {
		rwnx_hw->sched_scan_req.sscan_enabled = 1;
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 10, 0)
		rwnx_hw->sched_scan_req.sscan_reqid = param->reqid;
#endif
	}

	return ret;
}

int rwnx_send_sched_scan_stop_req(struct rwnx_hw *rwnx_hw,
				  struct rwnx_vif *rwnx_vif, u64 reqid)
{
	int ret;
	struct scanu_start_req *req;
	uint16_t req_len = offsetof(struct scanu_start_req, sscan_param) +
			   sizeof(struct sched_scan_parameter);

	ENTER();

	/* Build the SCANU_START_REQ message */
	req = rwnx_msg_zalloc(SCANU_START_REQ, TASK_SCANU, req_len);

	if (!req)
		return -ENOMEM;

	/* Set parameters */
	req->sscan_param.reqid = reqid;
	req->add_ies = SCHED_SCAN_STOP;

	WQ_DBG(DM_GENERIC, DL_WRN, "%s req->sched_scan_conf.reqid %u", __func__,
	       req->sscan_param.reqid);

	/* Send the SCHED_SCAN_STOP_REQ message to LMAC FW */
	ret = rwnx_send_msg_nonblock(rwnx_hw, req);
	if (ret == 0)
		rwnx_hw->sched_scan_req.sscan_enabled = 0;

	return ret;
}

int rwnx_send_abort_scan_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif)
{
	struct abort_scan_req *req;
	ENTER();

	req = rwnx_msg_zalloc(SCANU_ABORT_REQ, TASK_SCANU,
			      sizeof(struct abort_scan_req));
	if (!req)
		return -ENOMEM;

	req->vif_idx = rwnx_vif->vif_index;
	req->host_abort_flag = true;
	WQ_DBG(DM_GENERIC, DL_WRN,
	       "rwnx_send_abort_scan_req:send SCANU_ABORT_REQ\n");
	/* Send the SCANU_ABORT_REQ message to LMAC FW */
	return rwnx_send_msg_nonblock(rwnx_hw, req);
}

int rwnx_send_apm_start_cac_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
				struct cfg80211_chan_def *chandef,
				struct apm_start_cac_cfm *cfm)
{
	struct apm_start_cac_req *req;

	ENTER();

	/* Build the APM_START_CAC_REQ message */
	req = rwnx_msg_zalloc(APM_START_CAC_REQ, TASK_APM,
			      sizeof(struct apm_start_cac_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the APM_START_CAC_REQ message */
	req->vif_idx = vif->vif_index;
	cfg80211_to_rwnx_chan(chandef, &req->chan);

	/* Send the APM_START_CAC_REQ message to LMAC FW */
	return RWNX_SEND_MSG_EX(rwnx_hw, req, APM_START_CAC_CFM, cfm);
}

int rwnx_send_apm_stop_cac_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif)
{
	struct apm_stop_cac_req *req;

	ENTER();

	/* Build the APM_STOP_CAC_REQ message */
	req = rwnx_msg_zalloc(APM_STOP_CAC_REQ, TASK_APM,
			      sizeof(struct apm_stop_cac_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the APM_STOP_CAC_REQ message */
	req->vif_idx = vif->vif_index;

	/* Send the APM_STOP_CAC_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, APM_STOP_CAC_CFM);
}

int rwnx_send_mesh_start_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
			     const struct mesh_config *conf,
			     const struct mesh_setup *setup,
			     struct mesh_start_cfm *cfm)
{
	// Message to send
	struct mesh_start_req *req;
	// Supported basic rates
	struct ieee80211_supported_band *band =
		rwnx_hw->wiphy->bands[setup->chandef.chan->band];
	/* Counter */
	int i;
	/* Return status */
	int status;
	/* DMA Address to be unmapped after confirmation reception */
	u32 dma_addr = 0;

	ENTER();

	/* Build the MESH_START_REQ message */
	req = rwnx_msg_zalloc(MESH_START_REQ, TASK_MESH,
			      sizeof(struct mesh_start_req));
	if (!req) {
		return -ENOMEM;
	}

	req->vif_index = vif->vif_index;
	req->bcn_int = setup->beacon_interval;
	req->dtim_period = setup->dtim_period;
	req->mesh_id_len = setup->mesh_id_len;

	for (i = 0; i < setup->mesh_id_len; i++) {
		req->mesh_id[i] = *(setup->mesh_id + i);
	}

	req->user_mpm = setup->user_mpm;
	req->is_auth = setup->is_authenticated;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
	req->auth_id = setup->auth_id;
#endif
	req->ie_len = setup->ie_len;

	if (setup->ie_len) {
		/*
         * Need to provide a Virtual Address to the MAC so that it can download the
         * additional information elements.
         */
		req->ie_addr = dma_map_single(rwnx_hw->dev, (void *)setup->ie,
					      setup->ie_len, DMA_FROM_DEVICE);

		/* Check DMA mapping result */
		if (dma_mapping_error(rwnx_hw->dev, req->ie_addr)) {
			WQ_DBG(DM_GENERIC, DL_ERR,
			       "%s - DMA Mapping error on additional IEs\n",
			       __func__);

			/* Consider there is no Additional IEs */
			req->ie_len = 0;
		} else {
			/* Store DMA Address so that we can unmap the memory section once MESH_START_CFM is received */
			dma_addr = req->ie_addr;
		}
	}

	/* Provide rate information */
	req->basic_rates.length = 0;
	if (band != NULL) {
		for (i = 0; i < band->n_bitrates; i++) {
			u16 rate = band->bitrates[i].bitrate;

			/* Read value is in in units of 100 Kbps, provided value is in units
             * of 1Mbps, and multiplied by 2 so that 5.5 becomes 11 */
			rate = (rate << 1) / 10;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
			if (setup->basic_rates & BIT(i)) {
				rate |= 0x80;
			}
#endif

			req->basic_rates.array[i] = (u8)rate;
			req->basic_rates.length++;
		}
	}

	/* Provide channel information */
	cfg80211_to_rwnx_chan(&setup->chandef, &req->chan);

	/* Send the MESH_START_REQ message to UMAC FW */
	status = RWNX_SEND_MSG_EX(rwnx_hw, req, MESH_START_CFM, cfm);

	/* Unmap DMA area */
	if (setup->ie_len) {
		dma_unmap_single(rwnx_hw->dev, dma_addr, setup->ie_len,
				 DMA_TO_DEVICE);
	}

	/* Return the status */
	return (status);
}

int rwnx_send_mesh_stop_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
			    struct mesh_stop_cfm *cfm)
{
	// Message to send
	struct mesh_stop_req *req;

	ENTER();

	/* Build the MESH_STOP_REQ message */
	req = rwnx_msg_zalloc(MESH_STOP_REQ, TASK_MESH,
			      sizeof(struct mesh_stop_req));
	if (!req) {
		return -ENOMEM;
	}

	req->vif_idx = vif->vif_index;

	/* Send the MESH_STOP_REQ message to UMAC FW */
	return RWNX_SEND_MSG_EX(rwnx_hw, req, MESH_STOP_CFM, cfm);
}

int rwnx_send_mesh_update_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
			      u32 mask, const struct mesh_config *p_mconf,
			      struct mesh_update_cfm *cfm)
{
	// Message to send
	struct mesh_update_req *req;
	// Keep only bit for fields which can be updated
	u32 supp_mask =
		(mask << 1) & (BIT(NL80211_MESHCONF_GATE_ANNOUNCEMENTS) |
			       BIT(NL80211_MESHCONF_HWMP_ROOTMODE) |
			       BIT(NL80211_MESHCONF_FORWARDING) |
			       BIT(NL80211_MESHCONF_POWER_MODE));

	ENTER();

	if (!supp_mask) {
		return -ENOENT;
	}

	/* Build the MESH_UPDATE_REQ message */
	req = rwnx_msg_zalloc(MESH_UPDATE_REQ, TASK_MESH,
			      sizeof(struct mesh_update_req));

	if (!req) {
		return -ENOMEM;
	}

	req->vif_idx = vif->vif_index;

	if (supp_mask & BIT(NL80211_MESHCONF_GATE_ANNOUNCEMENTS)) {
		req->flags |= BIT(MESH_UPDATE_FLAGS_GATE_MODE_BIT);
		req->gate_announ = p_mconf->dot11MeshGateAnnouncementProtocol;
	}

	if (supp_mask & BIT(NL80211_MESHCONF_HWMP_ROOTMODE)) {
		req->flags |= BIT(MESH_UPDATE_FLAGS_ROOT_MODE_BIT);
		req->root_mode = p_mconf->dot11MeshHWMPRootMode;
	}

	if (supp_mask & BIT(NL80211_MESHCONF_FORWARDING)) {
		req->flags |= BIT(MESH_UPDATE_FLAGS_MESH_FWD_BIT);
		req->mesh_forward = p_mconf->dot11MeshForwarding;
	}

	if (supp_mask & BIT(NL80211_MESHCONF_POWER_MODE)) {
		req->flags |= BIT(MESH_UPDATE_FLAGS_LOCAL_PSM_BIT);
		req->local_ps_mode = p_mconf->power_mode;
	}

	/* Send the MESH_UPDATE_REQ message to UMAC FW */
	return RWNX_SEND_MSG_EX(rwnx_hw, req, MESH_UPDATE_CFM, cfm);
}

int rwnx_send_mesh_peer_info_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
				 u8 sta_idx, struct mesh_peer_info_cfm *cfm)
{
	// Message to send
	struct mesh_peer_info_req *req;

	ENTER();

	/* Build the MESH_PEER_INFO_REQ message */
	req = rwnx_msg_zalloc(MESH_PEER_INFO_REQ, TASK_MESH,
			      sizeof(struct mesh_peer_info_req));
	if (!req) {
		return -ENOMEM;
	}

	req->sta_idx = sta_idx;

	/* Send the MESH_PEER_INFO_REQ message to UMAC FW */
	return RWNX_SEND_MSG_EX(rwnx_hw, req, MESH_PEER_INFO_CFM, cfm);
}

void rwnx_send_mesh_peer_update_ntf(struct rwnx_hw *rwnx_hw,
				    struct rwnx_vif *vif, u8 sta_idx,
				    u8 mlink_state)
{
	// Message to send
	struct mesh_peer_update_ntf *ntf;

	ENTER();

	/* Build the MESH_PEER_UPDATE_NTF message */
	ntf = rwnx_msg_zalloc(MESH_PEER_UPDATE_NTF, TASK_MESH,
			      sizeof(struct mesh_peer_update_ntf));

	if (ntf) {
		ntf->vif_idx = vif->vif_index;
		ntf->sta_idx = sta_idx;
		ntf->state = mlink_state;

		/* Send the MESH_PEER_INFO_REQ message to UMAC FW */
		rwnx_send_msg_nonblock(rwnx_hw, ntf);
	}
}

void rwnx_send_mesh_path_create_req(struct rwnx_hw *rwnx_hw,
				    struct rwnx_vif *vif, u8 *tgt_addr)
{
	struct mesh_path_create_req *req;

	ENTER();

	/* Check if we are already waiting for a confirmation */
	if (vif->ap.flags & RWNX_AP_CREATE_MESH_PATH)
		return;

	/* Build the MESH_PATH_CREATE_REQ message */
	req = rwnx_msg_zalloc(MESH_PATH_CREATE_REQ, TASK_MESH,
			      sizeof(struct mesh_path_create_req));
	if (!req)
		return;

	req->vif_idx = vif->vif_index;
	memcpy(&req->tgt_mac_addr, tgt_addr, ETH_ALEN);

	vif->ap.flags |= RWNX_AP_CREATE_MESH_PATH;

	/* Send the MESH_PATH_CREATE_REQ message to UMAC FW */
	rwnx_send_msg_nonblock(rwnx_hw, req);
}

int rwnx_send_mesh_path_update_req(struct rwnx_hw *rwnx_hw,
				   struct rwnx_vif *vif, const u8 *tgt_addr,
				   const u8 *p_nhop_addr,
				   struct mesh_path_update_cfm *cfm)
{
	// Message to send
	struct mesh_path_update_req *req;

	ENTER();

	/* Build the MESH_PATH_UPDATE_REQ message */
	req = rwnx_msg_zalloc(MESH_PATH_UPDATE_REQ, TASK_MESH,
			      sizeof(struct mesh_path_update_req));
	if (!req) {
		return -ENOMEM;
	}

	req->delete = (p_nhop_addr == NULL);
	req->vif_idx = vif->vif_index;
	memcpy(&req->tgt_mac_addr, tgt_addr, ETH_ALEN);

	if (p_nhop_addr) {
		memcpy(&req->nhop_mac_addr, p_nhop_addr, ETH_ALEN);
	}

	/* Send the MESH_PATH_UPDATE_REQ message to UMAC FW */
	return RWNX_SEND_MSG_EX(rwnx_hw, req, MESH_PATH_UPDATE_CFM, cfm);
}

void rwnx_send_mesh_proxy_add_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
				  u8 *ext_addr)
{
	// Message to send
	struct mesh_proxy_add_req *req;

	ENTER();

	/* Build the MESH_PROXY_ADD_REQ message */
	req = rwnx_msg_zalloc(MESH_PROXY_ADD_REQ, TASK_MESH,
			      sizeof(struct mesh_proxy_add_req));

	if (req) {
		req->vif_idx = vif->vif_index;
		memcpy(&req->ext_sta_addr, ext_addr, ETH_ALEN);

		/* Send the MESH_PROXY_ADD_REQ message to UMAC FW */
		rwnx_send_msg_nonblock(rwnx_hw, req);
	}
}

int rwnx_send_tdls_peer_traffic_ind_req(struct rwnx_hw *rwnx_hw,
					struct rwnx_vif *rwnx_vif)
{
	struct tdls_peer_traffic_ind_req *tdls_peer_traffic_ind_req;

	if (!rwnx_vif->sta.tdls_sta)
		return -ENOLINK;

	/* Build the TDLS_PEER_TRAFFIC_IND_REQ message */
	tdls_peer_traffic_ind_req =
		rwnx_msg_zalloc(TDLS_PEER_TRAFFIC_IND_REQ, TASK_TDLS,
				sizeof(struct tdls_peer_traffic_ind_req));

	if (!tdls_peer_traffic_ind_req)
		return -ENOMEM;

	/* Set parameters for the TDLS_PEER_TRAFFIC_IND_REQ message */
	tdls_peer_traffic_ind_req->vif_index = rwnx_vif->vif_index;
	tdls_peer_traffic_ind_req->sta_idx = rwnx_vif->sta.tdls_sta->sta_idx;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	ether_addr_copy((u8 *)&tdls_peer_traffic_ind_req->peer_mac_addr,
			rwnx_vif->sta.tdls_sta->mac_addr);
#else
	(void)memcpy((u8 *)&tdls_peer_traffic_ind_req->peer_mac_addr,
		     rwnx_vif->sta.tdls_sta->mac_addr, ETH_ALEN);
#endif
	tdls_peer_traffic_ind_req->dialog_token = 0; // check dialog token value
	tdls_peer_traffic_ind_req->last_tid =
		rwnx_vif->sta.tdls_sta->tdls.last_tid;
	tdls_peer_traffic_ind_req->last_sn =
		rwnx_vif->sta.tdls_sta->tdls.last_sn;

	/* Send the TDLS_PEER_TRAFFIC_IND_REQ message to LMAC FW */
	return rwnx_send_msg_nonblock(rwnx_hw, tdls_peer_traffic_ind_req);
}

int rwnx_send_config_monitor_req(struct rwnx_hw *rwnx_hw,
				 struct cfg80211_chan_def *chandef,
				 struct me_config_monitor_cfm *cfm)
{
	struct me_config_monitor_req *req;

	ENTER();

	/* Build the ME_CONFIG_MONITOR_REQ message */
	req = rwnx_msg_zalloc(ME_CONFIG_MONITOR_REQ, TASK_ME,
			      sizeof(struct me_config_monitor_req));
	if (!req)
		return -ENOMEM;

	if (chandef) {
		req->chan_set = true;
		cfg80211_to_rwnx_chan(chandef, &req->chan);
		rwnx_hw->monitor_param.tx_power = req->chan.tx_power;
		rwnx_hw->monitor_param.ch_band = (1 << req->chan.type) * 20;
		rwnx_hw->monitor_param.ch_index =
			ieee80211_frequency_to_channel(req->chan.center1_freq);
		if (rwnx_hw->phy.limit_bw)
			limit_chan_bw(&req->chan.type, req->chan.prim20_freq,
				      &req->chan.center1_freq);
	} else {
		req->chan_set = false;
	}

	req->uf = rwnx_hw->mod_params.uf;

	/* Send the ME_CONFIG_MONITOR_REQ message to FW */
	return RWNX_SEND_MSG_EX(rwnx_hw, req, ME_CONFIG_MONITOR_CFM, cfm);
}
#ifdef CONFIG_HML
#ifdef DEBUG_WQ_PRIV
typedef struct {
	uint8_t mac_addr[ETH_ALEN];
	uint16_t reason_code;
} vendor_hml_mac_cfg_kick_user_param_stru;

int rwnx_send_vendor_sta_del(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
			     const u8 *mac, u16 reason)
{
	vendor_hml_mac_cfg_kick_user_param_stru req;
	ENTER();

	if (mac == NULL) {
		return -EINVAL;
	}

	memcpy(req.mac_addr, mac, ETH_ALEN);
	req.reason_code = reason;
	rwnx_send_dbg_wq_priv_test_req(
		rwnx_hw, rwnx_vif, DBG_WQ_PRIV_TO_HML_TASK,
		HML_TO_VENDOR_MSG_STA_DEL, (char *)&req,
		sizeof(vendor_hml_mac_cfg_kick_user_param_stru));
	return 0;
}

int rwnx_send_vendor_cmd(struct rwnx_hw *rwnx_hw, uint16_t cmd_id,
			 void *write_msg, uint16_t len)
{
	void *cmd_req = NULL;
	cmd_req = rwnx_msg_zalloc(cmd_id, TASK_VENDOR, len);
	if (cmd_req == NULL) {
		return -EINVAL;
	}
	memcpy(cmd_req, write_msg, len);
	return rwnx_send_msg_nonblock(rwnx_hw, cmd_req);
}

int rwnx_send_vendor_msg1_req(struct rwnx_hw *rwnx_hw)
{
	uint32_t *req = NULL;
	uint32_t cfm;
	ENTER();
	req = rwnx_msg_zalloc(HML_MSG_TEST_REQ, TASK_VENDOR, sizeof(uint32_t));
	return RWNX_SEND_MSG_EX(rwnx_hw, req, HML_MSG_TEST_CFM, &cfm);
}
#endif
#endif

int rwnx_send_tdls_chan_switch_req(struct rwnx_hw *rwnx_hw,
				   struct rwnx_vif *rwnx_vif,
				   struct rwnx_sta *rwnx_sta,
				   bool sta_initiator, u8 oper_class,
				   struct cfg80211_chan_def *chandef,
				   struct tdls_chan_switch_cfm *cfm)
{
	struct tdls_chan_switch_req *tdls_chan_switch_req;

	/* Build the TDLS_CHAN_SWITCH_REQ message */
	tdls_chan_switch_req =
		rwnx_msg_zalloc(TDLS_CHAN_SWITCH_REQ, TASK_TDLS,
				sizeof(struct tdls_chan_switch_req));

	if (!tdls_chan_switch_req)
		return -ENOMEM;

	/* Set parameters for the TDLS_CHAN_SWITCH_REQ message */
	tdls_chan_switch_req->vif_index = rwnx_vif->vif_index;
	tdls_chan_switch_req->sta_idx = rwnx_sta->sta_idx;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	ether_addr_copy((u8 *)&tdls_chan_switch_req->peer_mac_addr,
			rwnx_sta_addr(rwnx_sta));
#else
	(void)memcpy((u8 *)&tdls_chan_switch_req->peer_mac_addr,
		     rwnx_sta_addr(rwnx_sta), ETH_ALEN);
#endif
	tdls_chan_switch_req->initiator = sta_initiator;
	cfg80211_to_rwnx_chan(chandef, &tdls_chan_switch_req->chan);
	tdls_chan_switch_req->op_class = oper_class;

	/* Send the TDLS_CHAN_SWITCH_REQ message to LMAC FW */
	return RWNX_SEND_MSG_EX(rwnx_hw, tdls_chan_switch_req,
				TDLS_CHAN_SWITCH_CFM, cfm);
}

int rwnx_send_tdls_cancel_chan_switch_req(
	struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
	struct rwnx_sta *rwnx_sta, struct tdls_cancel_chan_switch_cfm *cfm)
{
	struct tdls_cancel_chan_switch_req *tdls_cancel_chan_switch_req;

	/* Build the TDLS_CHAN_SWITCH_REQ message */
	tdls_cancel_chan_switch_req =
		rwnx_msg_zalloc(TDLS_CANCEL_CHAN_SWITCH_REQ, TASK_TDLS,
				sizeof(struct tdls_cancel_chan_switch_req));
	if (!tdls_cancel_chan_switch_req)
		return -ENOMEM;

	/* Set parameters for the TDLS_CHAN_SWITCH_REQ message */
	tdls_cancel_chan_switch_req->vif_index = rwnx_vif->vif_index;
	tdls_cancel_chan_switch_req->sta_idx = rwnx_sta->sta_idx;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	ether_addr_copy((u8 *)&tdls_cancel_chan_switch_req->peer_mac_addr,
			rwnx_sta_addr(rwnx_sta));
#else
	(void)memcpy((u8 *)&tdls_cancel_chan_switch_req->peer_mac_addr,
		     rwnx_sta_addr(rwnx_sta), ETH_ALEN);
#endif

	/* Send the TDLS_CHAN_SWITCH_REQ message to LMAC FW */
	return RWNX_SEND_MSG_EX(rwnx_hw, tdls_cancel_chan_switch_req,
				TDLS_CANCEL_CHAN_SWITCH_CFM, cfm);
}

#ifdef CONFIG_RWNX_BFMER
void rwnx_send_bfmer_enable(struct rwnx_hw *rwnx_hw, struct rwnx_sta *rwnx_sta,
			    const struct ieee80211_vht_cap *vht_cap)
{
	struct mm_bfmer_enable_req *bfmer_en_req;
	__le32 vht_capability;
	u8 rx_nss = 0;

	ENTER();

	if (!vht_cap) {
		goto end;
	}

	vht_capability = vht_cap->vht_cap_info;
	if (!(vht_capability & IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE)) {
		goto end;
	}

	rx_nss = rwnx_bfmer_get_rx_nss(vht_cap);

	/* Allocate a structure that will contain the beamforming report */
	if (rwnx_bfmer_report_add(rwnx_hw, rwnx_sta,
				  RWNX_BFMER_REPORT_SPACE_SIZE)) {
		goto end;
	}

	/* Build the MM_BFMER_ENABLE_REQ message */
	bfmer_en_req = rwnx_msg_zalloc(MM_BFMER_ENABLE_REQ, TASK_MM,
				       sizeof(struct mm_bfmer_enable_req));

	/* Check message allocation */
	if (!bfmer_en_req) {
		/* Free memory allocated for the report */
		rwnx_bfmer_report_del(rwnx_hw, rwnx_sta);

		/* Do not use beamforming */
		goto end;
	}

	/* Provide DMA address to the MAC */
	bfmer_en_req->host_bfr_addr = rwnx_sta->bfm_report->dma_addr;
	bfmer_en_req->host_bfr_size = RWNX_BFMER_REPORT_SPACE_SIZE;
	bfmer_en_req->sta_idx = rwnx_sta->sta_idx;
	bfmer_en_req->aid = rwnx_sta->aid;
	bfmer_en_req->rx_nss = rx_nss;

	if (vht_capability & IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE) {
		bfmer_en_req->vht_mu_bfmee = true;
	} else {
		bfmer_en_req->vht_mu_bfmee = false;
	}

	/* Send the MM_BFMER_EN_REQ message to LMAC FW */
	rwnx_send_msg_nonblock(rwnx_hw, bfmer_en_req);

end:
	return;
}

#ifdef CONFIG_RWNX_MUMIMO_TX
int rwnx_send_mu_group_update_req(struct rwnx_hw *rwnx_hw,
				  struct rwnx_sta *rwnx_sta)
{
	struct mm_mu_group_update_req *req;
	int group_id, i = 0;
	u64 map;

	ENTER();

	/* Build the MM_MU_GROUP_UPDATE_REQ message */
	req = rwnx_msg_zalloc(MM_MU_GROUP_UPDATE_REQ, TASK_MM,
			      sizeof(struct mm_mu_group_update_req) +
				      rwnx_sta->group_info.cnt *
					      sizeof(req->groups[0]));

	/* Check message allocation */
	if (!req)
		return -ENOMEM;

	/* Go through the groups the STA belongs to */
	group_sta_for_each(rwnx_sta, group_id, map)
	{
		int user_pos =
			rwnx_mu_group_sta_get_pos(rwnx_hw, rwnx_sta, group_id);

		if (WARN((i >= rwnx_sta->group_info.cnt),
			 "STA%d: Too much group (%d)\n", rwnx_sta->sta_idx,
			 i + 1))
			break;

		req->groups[i].group_id = group_id;
		req->groups[i].user_pos = user_pos;

		i++;
	}

	req->group_cnt = rwnx_sta->group_info.cnt;
	req->sta_idx = rwnx_sta->sta_idx;

	/* Send the MM_MU_GROUP_UPDATE_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, MM_MU_GROUP_UPDATE_CFM);
}
#endif /* CONFIG_RWNX_MUMIMO_TX */
#endif /* CONFIG_RWNX_BFMER */

/**********************************************************************
 *    Debug Messages
 *********************************************************************/
int rwnx_send_dbg_trigger_req(struct rwnx_hw *rwnx_hw, char *msg)
{
	struct mm_dbg_trigger_req *req;

	ENTER();

	/* Build the MM_DBG_TRIGGER_REQ message */
	req = rwnx_msg_zalloc(MM_DBG_TRIGGER_REQ, TASK_MM,
			      sizeof(struct mm_dbg_trigger_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the MM_DBG_TRIGGER_REQ message */
	strncpy(req->error, msg, sizeof(req->error));

	/* Send the MM_DBG_TRIGGER_REQ message to LMAC FW */
	return rwnx_send_msg_nonblock(rwnx_hw, req);
}

int rwnx_send_dbg_mem_read_req(struct rwnx_hw *rwnx_hw, u32 mem_addr,
			       struct dbg_mem_read_cfm *cfm)
{
	struct dbg_mem_read_req *mem_read_req;

	ENTER();

	/* Build the DBG_MEM_READ_REQ message */
	mem_read_req = rwnx_msg_zalloc(DBG_MEM_READ_REQ, TASK_DBG,
				       sizeof(struct dbg_mem_read_req));
	if (!mem_read_req)
		return -ENOMEM;

	/* Set parameters for the DBG_MEM_READ_REQ message */
	mem_read_req->memaddr = mem_addr;

	/* Send the DBG_MEM_READ_REQ message to LMAC FW */
	return RWNX_SEND_MSG_EX(rwnx_hw, mem_read_req, DBG_MEM_READ_CFM, cfm);
}

#if 1
/*
send dbg wq_priv cmd
*/
int rwnx_send_dbg_wq_priv_test_req(struct rwnx_hw *rwnx_hw,
				   struct rwnx_vif *rwnx_vif, u8 msg_id,
				   u8 sub_msg_id, char *mgs, int mesg_len)
{
	struct dbg_wq_priv_test_req *wq_priv_req;

	ENTER();

	/* Build the DBG_SET_WQ_PRIV_TEST_REQ message */
	wq_priv_req =
		rwnx_msg_zalloc(DBG_SET_WQ_PRIV_TEST_REQ, TASK_DBG,
				sizeof(struct dbg_wq_priv_test_req) + mesg_len);
	if (!wq_priv_req)
		return -ENOMEM;

	/* Set parameters for the DBG_SET_WQ_PRIV_TEST_REQ message */
	wq_priv_req->vif_id = rwnx_vif->vif_index;
	wq_priv_req->msg_id = msg_id;
	wq_priv_req->sub_msg_id = sub_msg_id;
	wq_priv_req->wq_priv_msg_len = mesg_len;
	WQ_DBG(DM_GENERIC, DL_ERR,
	       "rwnx_send_dbg_wq_priv_test_req::vif_id=%d, msg_id=%d, hml_test_msg_len=%d\n",
	       wq_priv_req->vif_id, wq_priv_req->msg_id,
	       wq_priv_req->wq_priv_msg_len);
	if (wq_priv_req->wq_priv_msg_len > 0) {
		memcpy(wq_priv_req->wq_priv_msg, mgs, mesg_len);
	}

	return rwnx_send_msg_nonblock(rwnx_hw, wq_priv_req);
}
#endif

int rwnx_send_dbg_mem_write_req(struct rwnx_hw *rwnx_hw, u32 mem_addr,
				u32 mem_data)
{
	struct dbg_mem_write_req *mem_write_req;

	ENTER();

	/* Build the DBG_MEM_WRITE_REQ message */
	mem_write_req = rwnx_msg_zalloc(DBG_MEM_WRITE_REQ, TASK_DBG,
					sizeof(struct dbg_mem_write_req));
	if (!mem_write_req)
		return -ENOMEM;

	/* Set parameters for the DBG_MEM_WRITE_REQ message */
	mem_write_req->memaddr = mem_addr;
	mem_write_req->memdata = mem_data;

	/* Send the DBG_MEM_WRITE_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, mem_write_req, DBG_MEM_WRITE_CFM);
}

int rwnx_send_dbg_set_mod_filter_req(struct rwnx_hw *rwnx_hw, u32 filter)
{
	struct dbg_set_mod_filter_req *set_mod_filter_req;

	ENTER();

	/* Build the DBG_SET_MOD_FILTER_REQ message */
	set_mod_filter_req =
		rwnx_msg_zalloc(DBG_SET_MOD_FILTER_REQ, TASK_DBG,
				sizeof(struct dbg_set_mod_filter_req));
	if (!set_mod_filter_req)
		return -ENOMEM;

	/* Set parameters for the DBG_SET_MOD_FILTER_REQ message */
	set_mod_filter_req->mod_filter = filter;

	/* Send the DBG_SET_MOD_FILTER_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, set_mod_filter_req,
			     DBG_SET_MOD_FILTER_CFM);
}

int rwnx_send_dbg_set_sev_filter_req(struct rwnx_hw *rwnx_hw, u32 filter)
{
	struct dbg_set_sev_filter_req *set_sev_filter_req;

	ENTER();

	/* Build the DBG_SET_SEV_FILTER_REQ message */
	set_sev_filter_req =
		rwnx_msg_zalloc(DBG_SET_SEV_FILTER_REQ, TASK_DBG,
				sizeof(struct dbg_set_sev_filter_req));
	if (!set_sev_filter_req)
		return -ENOMEM;

	/* Set parameters for the DBG_SET_SEV_FILTER_REQ message */
	set_sev_filter_req->sev_filter = filter;

	/* Send the DBG_SET_SEV_FILTER_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, set_sev_filter_req,
			     DBG_SET_SEV_FILTER_CFM);
}

int rwnx_send_dbg_get_sys_stat_req(struct rwnx_hw *rwnx_hw,
				   struct dbg_get_sys_stat_cfm *cfm)
{
	void *req;

	ENTER();

	/* Allocate the message */
	req = rwnx_msg_zalloc(DBG_GET_SYS_STAT_REQ, TASK_DBG, 0);
	if (!req)
		return -ENOMEM;

	/* Send the DBG_MEM_READ_REQ message to LMAC FW */
	return RWNX_SEND_MSG_EX(rwnx_hw, req, DBG_GET_SYS_STAT_CFM, cfm);
}

int rwnx_send_dbg_pktlog_cfg_req(struct rwnx_hw *rwnx_hw, u8 flags)
{
	struct dbg_pktdump_en_req *pktlog_cfg_req;

	ENTER();

	pktlog_cfg_req = rwnx_msg_zalloc(DBG_PKTDUMP_EN_REQ, TASK_DBG,
					 sizeof(struct dbg_pktdump_en_req));
	if (!pktlog_cfg_req)
		return -ENOMEM;

	/* Set parameters for the DBG_SET_SEV_FILTER_REQ message */
	pktlog_cfg_req->pktdump_en = flags;

	/* Send the DBG_SET_SEV_FILTER_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, pktlog_cfg_req, DBG_PKTDUMP_EN_CFM);
}

int rwnx_send_cfg_rssi_req(struct rwnx_hw *rwnx_hw, u8 vif_index,
			   int rssi_thold, u32 rssi_hyst)
{
	struct mm_cfg_rssi_req *req;

	ENTER();

	/* Build the MM_CFG_RSSI_REQ message */
	req = rwnx_msg_zalloc(MM_CFG_RSSI_REQ, TASK_MM,
			      sizeof(struct mm_cfg_rssi_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the MM_CFG_RSSI_REQ message */
	req->vif_index = vif_index;
	req->rssi_thold = (s8)rssi_thold;
	req->rssi_hyst = (u8)rssi_hyst;

	/* Send the MM_CFG_RSSI_REQ message to LMAC FW */
	return rwnx_send_msg_nonblock(rwnx_hw, req);
}

int rwnx_send_ip_req(struct rwnx_hw *rwnx_hw, u8 *ip_addr, u8 index)
{
	struct mm_start_ip_req *ip_set_param;

	ENTER();

	/* Build the MM_SET_IP_REQ message */
	ip_set_param = rwnx_msg_zalloc(MM_SET_IP_REQ, TASK_MM,
				       sizeof(struct mm_start_ip_req));
	if (!ip_set_param) {
		return -ENOMEM;
	}
	memcpy(ip_set_param->ip_address, ip_addr,
	       sizeof(ip_set_param->ip_address));
	ip_set_param->vif_index = index;

	return rwnx_send_msg(rwnx_hw, ip_set_param, MM_SET_IP_CFM);
}

int rwnx_info_notify_set(struct rwnx_hw *rwnx_hw, u8 msg_type, u8 vif_index,
			 const void *param, u8 param_len)
{
	struct mm_info_notify_req *req;

	ENTER();

	BUG_ON(param_len == 0);
	BUG_ON(param_len > sizeof(req->info_param));

	req = rwnx_msg_zalloc(MM_INFO_NOTIFY_REQ, TASK_MM,
			      sizeof(struct mm_info_notify_req));
	if (!req)
		return -ENOMEM;

	req->set = 1;
	req->vif_index = vif_index;
	req->msg_type = msg_type;
	memcpy(req->info_param, param, param_len);

	return rwnx_send_msg(rwnx_hw, req, MM_INFO_NOTIFY_CFM);
}

int rwnx_info_notify_get(struct rwnx_hw *rwnx_hw, u8 msg_type, u8 vif_index,
			 const void *param, u8 param_len, void *cfm_result,
			 u16 result_len)
{
	struct mm_info_notify_req *req;
	struct mm_info_notify_cfm *cfm;
	int ret;

	ENTER();

	BUG_ON(param_len > sizeof(req->info_param));
	BUG_ON(!cfm_result);
	BUG_ON(result_len == 0);

	cfm = kzalloc(sizeof(*cfm) + result_len,
		      in_softirq() ? GFP_ATOMIC : GFP_KERNEL);
	if (!cfm)
		return -ENOMEM;

	req = rwnx_msg_zalloc(MM_INFO_NOTIFY_REQ, TASK_MM,
			      sizeof(struct mm_info_notify_req));
	if (!req) {
		kfree(cfm);
		return -ENOMEM;
	}

	req->set = 0;
	req->vif_index = vif_index;
	req->msg_type = msg_type;
	if (param && param_len)
		memcpy(req->info_param, param, param_len);

	ret = rwnx_send_msg_ex(rwnx_hw, req, MM_INFO_NOTIFY_CFM, cfm,
			       sizeof(*cfm) + result_len);
	if (ret == 0) {
		/* WAR: struct mm_info_notify_get_cfm should be replaced with mm_info_notify_cfm in f/w */
		if (msg_type == MM_INFO_NOTIFY_GET_RSSI) {
			struct mm_info_notify_get_cfm get_cfm =
				*(struct mm_info_notify_get_cfm *)cfm;

			cfm->ret_value = get_cfm.result;
			cfm->result_len = get_cfm.result_len;
		}

		if (cfm->ret_value != REG_CFM_SUCC) {
			WQ_DBG(DM_GENERIC, DL_ERR,
			       "%s: req %d is unsuccessful!\n", __func__,
			       msg_type);
			ret = -EINVAL;
		} else {
			if (result_len != cfm->result_len)
				WQ_DBG(DM_GENERIC, DL_WRN,
				       "%s: req %d, confirm length %d != expected %d!\n",
				       __func__, msg_type, cfm->result_len,
				       result_len);
			ret = min(result_len, cfm->result_len);
			if (ret)
				memcpy(cfm_result, cfm->info_param, ret);
		}
	}
	kfree(cfm);
	return ret;
}

/* FIXME: don't directly send MM_INFO_NOTIFY_REQ, call it by RWNX_INFO_NOTIFY_SET/GET */
int rwnx_send_dbg_recover_test_req(struct rwnx_hw *rwnx_hw,
				   uint32_t recover_mode,
				   struct mm_info_notify_cfm *cfm)
{
	struct mm_info_notify_req *info_notify_param;

	WQ_DBG(DM_GENERIC, DL_WRN, WQ_FN_ENTRY_STR);

	/* Build the DBG_RECOVER_TEST_REQ message */
	info_notify_param = rwnx_msg_zalloc(MM_INFO_NOTIFY_REQ, TASK_MM,
					    sizeof(struct mm_info_notify_req));
	if (!info_notify_param)
		return -ENOMEM;

	info_notify_param->set = recover_mode;
	info_notify_param->msg_type = MSG_TYPE_RECOVER_TEST;

	return RWNX_SEND_MSG_EX(rwnx_hw, info_notify_param, MM_INFO_NOTIFY_CFM,
				cfm);
}

int rwnx_send_ant_req(struct rwnx_hw *rwnx_hw, u32 ant)
{
	struct mm_set_ant_req *mm_set_ant_req;

	ENTER();

	/* Build the MM_SET_ANT_REQ message */
	mm_set_ant_req = rwnx_msg_zalloc(MM_SET_ANT_REQ, TASK_MM,
					 sizeof(struct mm_set_ant_req));

	if (!mm_set_ant_req)
		return -ENOMEM;

	/* Set parameters for the MM_SET_ANT_REQ message */
	mm_set_ant_req->ant = ant;

	WQ_DBG(DM_GENERIC, DL_ERR, "%s ant=%d\n", __func__,
	       mm_set_ant_req->ant);

	/* Send the MM_SET_ANT_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, mm_set_ant_req, MM_SET_ANT_CFM);
}

int rwnx_send_free_host_ring_req(struct rwnx_hw *rwnx_hw, u8 mac_id,
				 u32 buf_rd_idx, bool use_backup_ring)
{
	struct host_data_ring_free_req *req;

	ENTER();

	req = rwnx_msg_zalloc(ME_FREE_HOST_DATA_RING_REQ, TASK_ME,
			      sizeof(struct host_data_ring_free_req));
	if (!req)
		return -ENOMEM;

	req->mac_id = mac_id;
	req->buf_rd_idx = buf_rd_idx;
	req->use_backup_ring = use_backup_ring;

	return rwnx_send_msg_nonblock(rwnx_hw, req);
}

int rwnx_send_set_host_ring_req(struct rwnx_hw *rwnx_hw,
				struct rwnx_rx_ll *rx_ll)
{
	struct me_extend_set_host_data_ring_req *req;

	req = rwnx_msg_zalloc(ME_EXTEND_SET_HOST_DATA_RING_REQ, TASK_ME,
			      sizeof(struct me_extend_set_host_data_ring_req));
	if (!req)
		return -ENOMEM;

	req->ver = 0;
	req->host_buf_ring_addr[0] = (u32)rx_ll->rx_ring[0].dma;
	req->host_buf_ring_addr[1] = (u32)rx_ll->rx_ring[1].dma;
	req->host_buf_ring_sz[0] = rx_ll->rx_ring[0].size;
	req->host_buf_ring_sz[1] = rx_ll->rx_ring[1].size;
	req->backup_host_buf_ring_addr[0] = (u32)rx_ll->rx_backup_ring[0].dma;
	req->backup_host_buf_ring_addr[1] = (u32)rx_ll->rx_backup_ring[1].dma;
	req->backup_host_buf_ring_sz[0] = rx_ll->rx_backup_ring[0].size;
	req->backup_host_buf_ring_sz[1] = rx_ll->rx_backup_ring[1].size;

	WQ_DBG(DM_GENERIC, DL_WRN,
	       "send set host data ring req, ring addr[2]=[0x%x/0x%x], ring size[2]=[0x%x/0x%x], "
	       "backup ring addr[2]=[0x%x/0x%x], backup ring size[2]=[0x%x/0x%x]\n",
	       req->host_buf_ring_addr[0], req->host_buf_ring_addr[1],
	       req->host_buf_ring_sz[0], req->host_buf_ring_sz[1],
	       req->backup_host_buf_ring_addr[0],
	       req->backup_host_buf_ring_addr[1],
	       req->backup_host_buf_ring_sz[0],
	       req->backup_host_buf_ring_sz[1]);

	return RWNX_SEND_MSG_EX(rwnx_hw, req, ME_EXTEND_SET_HOST_DATA_RING_CFM,
				&rx_ll->rx_ll_support);
}

int rwnx_send_cca_config_set(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 ena, u16 period)
{
	struct mm_set_cca_req *req;

	req = rwnx_msg_zalloc(MM_SET_CCA_CAP_REQ, TASK_MM, sizeof(*req));

	if (!req) {
		return -ENOMEM;
	}

	req->vif = vif_idx;
	req->ena = ena;
	req->period = period;

	WQ_DBG(DM_GENERIC, DL_WRN,"%s(%u): set cca ena %u, period %u MS.\n",
		__func__, vif_idx, ena, period);

	return rwnx_send_msg(rwnx_hw, req, MM_SET_CCA_CAP_CFM);
}

int rwnx_send_cca_data_get(struct rwnx_hw *rwnx_hw, u8 vif_idx, struct mm_get_cca_cfm *cfm)
{
	struct mm_get_cca_req *req;
	int ret;

	if (!cfm) {
		return -EINVAL;
	}

	req = rwnx_msg_zalloc(MM_GET_CCA_CAP_REQ, TASK_MM, sizeof(*req));

	if (!req) {
		return -ENOMEM;
	}

	req->vif = vif_idx;

	ret = RWNX_SEND_MSG_EX(rwnx_hw, req, MM_GET_CCA_CAP_CFM, cfm);

	WQ_DBG(DM_GENERIC, DL_WRN,"%s(%u): get cca valid %d, vif %u, token %u, frq %u mHz, busy %u US, period %u US.\n",
		__func__, vif_idx, cfm->valid, cfm->vif, cfm->token, cfm->freq, cfm->busy_time, cfm->period);

	return ret;
}

#ifdef CONFIG_PM
int rwnx_send_secure_param_set(struct rwnx_hw *rwnx_hw, u8 vif_idx,
			       struct cfg80211_connect_params *sme)
{
	struct secure_param_set_req *req;

	req = rwnx_msg_zalloc(ME_SET_SECURE_PARAM_REQ, TASK_ME,
			      sizeof(struct secure_param_set_req));

	if (!req)
		return -ENOMEM;

	req->vif_index = vif_idx;
	/// get proto, no OSEN/WAPI
	if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_1)
		req->proto |= MAC_PROTO_WPA;
	if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_2)
		req->proto |= MAC_PROTO_RSN;
	req->akm = sme->crypto.akm_suites[0];
	req->pairwise_cipher = sme->crypto.ciphers_pairwise[0];
	req->group_cipher = sme->crypto.cipher_group;

	WQ_DBG(DM_GENERIC, DL_WRN,
	       "Send secure param set req, akm:0x%04x, proto:%d, pairwise_cipher:0x%04x, group_cipher:0x%04x\n",
	       req->akm, req->proto, req->pairwise_cipher, req->group_cipher);

	return rwnx_send_msg_nonblock(rwnx_hw, req);
}

int rwnx_send_rekey_data_set(struct rwnx_hw *rwnx_hw, u8 vif_idx,
			     struct cfg80211_gtk_rekey_data *key)
{
	struct rekey_data_set_req *req;

	req = rwnx_msg_zalloc(ME_SET_REKEY_DATA_REQ, TASK_ME,
			      sizeof(struct rekey_data_set_req));

	if (!req)
		return -ENOMEM;

	req->vif_index = vif_idx;
	memcpy(req->kck, key->kck, WPA_KCK_LEN);
	memcpy(req->kek, key->kek, WPA_KEK_LEN);
	memcpy(req->replay_counter, key->replay_ctr, WPA_REPLAY_CTR_LEN);

	WQ_DBG(DM_GENERIC, DL_WRN, "Send rekey data set req, vif_idx: %d\n",
	       vif_idx);

	return rwnx_send_msg_nonblock(rwnx_hw, req);
}
#endif

int rwnx_txq_ring_init(struct rwnx_hw *rwnx_hw, u16 sta_cnt)
{
	struct txq_ring_ind_req *req;
	dma_addr_t phys_addr;
	int ret = 0, ring_idx, msdu_idx;
	void *cpu_addr;
	u16 sta_idx;

	/* ring global config */
	struct _ring_config ring_confg[TXQ_RING_ID_MAX] = {
		{ CONFIG_RING0_SZ, 0 },
		{ CONFIG_RING1_SZ, 0 },
		{ CONFIG_RING2_SZ, 0 },
		{ CONFIG_RING3_SZ, 0 },
	};

	rwnx_hw->txq_ring_sts = NULL;

	if (!TXQ_RING_FUNCTION_ENABLE || !rwnx_hw->core->config.tx_bundle_max)
		return 0;

	ENTER();

	/* init txq ring status */
	rwnx_hw->txq_ring_sts = (struct rwnx_msdu_txdone_status *)kmalloc(
		sizeof(struct rwnx_msdu_txdone_status), GFP_KERNEL);
	if (!rwnx_hw->txq_ring_sts) {
		ret = -ENOMEM;
		return ret;
	}
	memset(&rwnx_hw->txq_ring_sts->txq_ring_rcd[0], 0,
	       sizeof(struct rwnx_msdu_txdone_status));

	/* add multi or bcast sta */
	sta_cnt += 1;

	WQ_DBG(DM_GENERIC, DL_ERR, "%s, stacnt:%d\n", __func__, sta_cnt);

	cpu_addr = dma_alloc_coherent(rwnx_hw->dev,
				      sizeof(struct _tx_buf_ring) * sta_cnt,
				      &phys_addr, GFP_KERNEL);
	BUG_ON(!cpu_addr);

	rwnx_hw->txq_ring_vaddr = (struct _tx_buf_ring *)cpu_addr;
	rwnx_hw->txq_ring_paddr = phys_addr;

	for (sta_idx = 0; sta_idx < sta_cnt; sta_idx++) {
		struct _tx_buf_ring *sta_txq_ring =
			rwnx_hw->txq_ring_vaddr + sta_idx;
		for (ring_idx = 0; ring_idx < TXQ_RING_ID_MAX; ring_idx++) {
			sta_txq_ring->txq_ring[ring_idx].ring_sz =
				ring_confg[ring_idx].ring_confg_size;
			sta_txq_ring->txq_ring[ring_idx].ring_host_read_idx = 0;
			sta_txq_ring->txq_ring[ring_idx].ring_host_write_idx =
				0;
			sta_txq_ring->txq_ring[ring_idx].ring_fw_done_idx = 0;
			sta_txq_ring->txq_ring[ring_idx]
				.ring_water_mark_buf_num =
				ring_confg[ring_idx].water_mark_num;
			sta_txq_ring->txq_ring[ring_idx].ring_overflow = 0;
			for (msdu_idx = 0;
			     msdu_idx < MAC_TXQ_MSDU_SEQ_WINSZ_WD_MAX;
			     msdu_idx++) {
				sta_txq_ring->msdu_map[ring_idx]
					.msdu_bitmap[msdu_idx] = 0;
			}
		}
	}

	/* Build the ME_SET_PS_MODE_REQ message */
	req = rwnx_msg_zalloc(ME_SET_TXQ_RING_ADDR_REQ, TASK_ME,
			      sizeof(struct txq_ring_ind_req));
	if (!req) {
		dma_free_coherent(rwnx_hw->dev,
				  sizeof(struct _tx_buf_ring) * sta_cnt,
				  cpu_addr, phys_addr);
		if (rwnx_hw->txq_ring_sts)
			kfree((u8 *)rwnx_hw->txq_ring_sts);
		return -ENOMEM;
	}
	/* Set parameters for the addr message */
	req->txq_ring_addr = phys_addr;

	/* Send the ME_SET_PS_MODE_REQ message to FW */
	ret = rwnx_send_msg(rwnx_hw, req, ME_SET_TXQ_RING_ADDR_CFM);

	WQ_DBG(DM_GENERIC, DL_ERR,
	       "rwnx_send_txq_ring_ind phyaddr:0x%x, ring1sz:%d, ring2sz:%d, ring3sz:%d, ring4sz:%d\n",
	       (u32)phys_addr, rwnx_hw->txq_ring_vaddr->txq_ring[0].ring_sz,
	       rwnx_hw->txq_ring_vaddr->txq_ring[1].ring_sz,
	       rwnx_hw->txq_ring_vaddr->txq_ring[2].ring_sz,
	       rwnx_hw->txq_ring_vaddr->txq_ring[3].ring_sz);

	/* start txq ring timer */
	//wq_ipc_txq_ring_start_timer(rwnx_hw->core);

	return ret;
}

void rwnx_txq_ring_deinit(struct rwnx_hw *rwnx_hw, u16 sta_cnt)
{
	sta_cnt += 1;
	if (rwnx_hw->txq_ring_paddr) {
		dma_free_coherent(rwnx_hw->dev,
				  sizeof(struct _tx_buf_ring) * sta_cnt,
				  rwnx_hw->txq_ring_vaddr,
				  rwnx_hw->txq_ring_paddr);
		rwnx_hw->txq_ring_vaddr = 0;
		rwnx_hw->txq_ring_paddr = 0;
	}
	if (rwnx_hw->txq_ring_sts) {
		kfree((u8 *)rwnx_hw->txq_ring_sts);
	}
}

int rwnx_send_set_usb_param_req(struct rwnx_hw *rwnx_hw, u32 max_bundle_size)
{
	struct me_extend_set_usb_param_req *req;

	req = rwnx_msg_zalloc(ME_EXTEND_SET_USB_PARAM_REQ, TASK_ME,
			      sizeof(struct me_extend_set_usb_param_req));

	if (!req) {
		return -ENOMEM;
	}

	req->max_bundle_size = max_bundle_size;

	WQ_DBG(DM_GENERIC, DL_WRN,
	       "Send set usb param req: max_bundle_size=%d\n", max_bundle_size);

	return rwnx_send_msg(rwnx_hw, req, ME_EXTEND_SET_USB_PARAM_CFM);
}

int rwnx_send_ini_conf_req(struct rwnx_hw *rwnx_hw)
{
	struct mm_ini_conf_req *req;
	ENTER();

	req = rwnx_msg_zalloc(MM_INI_CONF_REQ, TASK_MM,
			      sizeof(struct mm_ini_conf_req));
	if (!req)
		return -ENOMEM;

	// if 32usr and tx_pwr_force_ena= false, set pwr with DEFAULT_PWR_32_USR_DBM
	if ((NX_REMOTE_STA_MAX == NX_REMOTE_STA_32) &&
	    (!wq_conf.tx_pwr_force_ena)) {
		req->tx_pwr_force_ena = 1;
		req->tx_pwr_force_dbm = DEFAULT_PWR_32_USR_DBM;
		WQ_DBG(DM_GENERIC, DL_WRN,
		       "rwnx_send_pwr_info_req: set pwr with no ini and 32 usr\n");
	} else {
		req->tx_pwr_force_ena = wq_conf.tx_pwr_force_ena ? 1 : 0;
		req->tx_pwr_force_dbm =
			wq_conf.tx_pwr_force_dbm ? wq_conf.tx_pwr_force_dbm : 0;
	}

	req->tx_ampdu_disable = wq_conf.tx_ampdu_disable ? 1 : 0;
	req->force_edca_vo = wq_conf.force_edca_vo ? 1 : 0;
	req->force_ignore_nav = wq_conf.force_ignore_nav ? 1 : 0;
	req->dynbw_enable = wq_conf.dynbw_enable ? 1 : 0;
	req->max_support_ba_bitmap = wq_conf.max_support_ba_bitmap;
	req->underrun_adapt_tx_rate = wq_conf.underrun_adapt_tx_rate;
	req->mmode = wq_conf.mmode;

	WQ_DBG(DM_GENERIC, DL_WRN,
	       "rwnx_send_ini_conf_req:send MM_INI_CONF_REQ\n");
	/* Send the MM_INI_CONF_REQ message to LMAC FW */
	return rwnx_send_msg_nonblock(rwnx_hw, req);
}

int rwnx_send_reg_dm_code_req(struct rwnx_hw *rwnx_hw, u8 country_code)
{
	struct mm_reg_dm_code_req *req;
	ENTER();

	req = rwnx_msg_zalloc(MM_REG_DM_CODE_REQ, TASK_MM,
			      sizeof(struct mm_reg_dm_code_req));
	req->reg_dm_code = country_code;
	WQ_DBG(DM_GENERIC, DL_WRN,
	       "rwnx_send_reg_dm_code_req:send MM_REG_DM_CODE_REQ, code %d\n",
	       country_code);
	/* Send the MM_REG_DM_CODE_REQ message to LMAC FW */
	return rwnx_send_msg_nonblock(rwnx_hw, req);
}

extern int wq_wifi_priv_recover_test(struct rwnx_hw *rwnx_hw,
				     struct rwnx_vif *rwnx_vif, void *msgs,
				     int msg_len);
int rwnx_send_set_isr_usage_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
				u32 enable)
{
	int ret;
	u8 set_mode;
	struct mm_info_notify_cfm *cfm;
	cfm = kmalloc(sizeof(*cfm), GFP_KERNEL);

	if (enable) {
		set_mode = 4; //4 is DBG_ISR_USAGE_ENABLE
	} else {
		set_mode = 5; //5 is DBG_ISR_USAGE_DISABLE
	}

	ret = wq_wifi_priv_recover_test(rwnx_hw, vif, &set_mode,
					sizeof(set_mode));
	kfree(cfm);

	return ret;
}

int rwnx_send_force_pcie_link_speed_req(struct rwnx_hw *rwnx_hw, u32 *cfm)
{
	u32 *req;
	ENTER();

	req = rwnx_msg_zalloc(ME_EXTEND_FORCE_PCIE_LINK_SPEED_REQ, TASK_ME,
			      sizeof(u32));

	if (!req)
		return -ENOMEM;

	*req = wq_conf.force_pcie_speed;

	return RWNX_SEND_MSG_EX(rwnx_hw, req,
				ME_EXTEND_FORCE_PCIE_LINK_SPEED_CFM, cfm);
}
