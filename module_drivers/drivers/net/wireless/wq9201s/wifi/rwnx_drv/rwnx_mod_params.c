/**
******************************************************************************
*
* @file rwnx_mod_params.c
*
* @brief Set configuration according to modules parameters
*
* Copyright (C) RivieraWaves 2012-2020
*
******************************************************************************
*/
#include <linux/rtnetlink.h>

#include "rwnx_defs.h"
#include "rwnx_tx.h"
#include "hal_desc.h"
#include "rwnx_compat.h"

#include "wq_log.h"
#include "wq_wifi_dbg.h"
#include "hif_api.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
/* Regulatory rules */
static struct ieee80211_regdomain
	rwnx_regdom = { .n_reg_rules = 2,
			.alpha2 = "99",
			.reg_rules = {
				REG_RULE(2390 - 10, 2510 + 10, 40, 0, 1000, 0),
				REG_RULE(5150 - 10, 5970 + 10, 80, 0, 1000, 0),
			} };
#endif

static const int mcs_map_to_rate[4][3] = {
	[PHY_CHNL_BW_20][IEEE80211_VHT_MCS_SUPPORT_0_7] = 65,
	[PHY_CHNL_BW_20][IEEE80211_VHT_MCS_SUPPORT_0_8] = 78,
	[PHY_CHNL_BW_20][IEEE80211_VHT_MCS_SUPPORT_0_9] = 78,
	[PHY_CHNL_BW_40][IEEE80211_VHT_MCS_SUPPORT_0_7] = 135,
	[PHY_CHNL_BW_40][IEEE80211_VHT_MCS_SUPPORT_0_8] = 162,
	[PHY_CHNL_BW_40][IEEE80211_VHT_MCS_SUPPORT_0_9] = 180,
	[PHY_CHNL_BW_80][IEEE80211_VHT_MCS_SUPPORT_0_7] = 292,
	[PHY_CHNL_BW_80][IEEE80211_VHT_MCS_SUPPORT_0_8] = 351,
	[PHY_CHNL_BW_80][IEEE80211_VHT_MCS_SUPPORT_0_9] = 390,
	[PHY_CHNL_BW_160][IEEE80211_VHT_MCS_SUPPORT_0_7] = 585,
	[PHY_CHNL_BW_160][IEEE80211_VHT_MCS_SUPPORT_0_8] = 702,
	[PHY_CHNL_BW_160][IEEE80211_VHT_MCS_SUPPORT_0_9] = 780,
};

#define MAX_VHT_RATE(map, nss, bw) (mcs_map_to_rate[bw][map] * (nss))

/**
 * Do some sanity check
 *
 */
static int rwnx_check_fw_hw_feature(struct rwnx_hw *rwnx_hw,
				    struct wiphy *wiphy)
{
	u32 sys_feat = rwnx_hw->version_cfm.features;
	u32 mac_feat = rwnx_hw->version_cfm.version_machw_1;
	u32 phy_feat = rwnx_hw->version_cfm.version_phy_1;
	u32 phy_vers = rwnx_hw->version_cfm.version_phy_2;
	u16 fw_max_sta_nb = rwnx_hw->version_cfm.max_sta_nb;
	u8 fw_max_vif_nb = rwnx_hw->version_cfm.max_vif_nb;
	int bw, res = 0;
	int amsdu_rx;
	int i = 0;

	WQ_DBG(DM_GENERIC, DL_WRN,
	       "%s: mac1=0x%x, mac2=0x%x, phy1=0x%x, phy2=0x%x, feat=0x%x, sta=%u, "
	       "vif=%u he_mcs_map=%u max_mcs=%u phy_band_support=%u\n",
	       __func__, rwnx_hw->version_cfm.version_machw_1,
	       rwnx_hw->version_cfm.version_machw_2,
	       rwnx_hw->version_cfm.version_phy_1,
	       rwnx_hw->version_cfm.version_phy_2,
	       rwnx_hw->version_cfm.features, rwnx_hw->version_cfm.max_sta_nb,
	       rwnx_hw->version_cfm.max_vif_nb, rwnx_hw->mod_params.he_mcs_map,
	       rwnx_hw->version_cfm.max_mcs,
	       rwnx_hw->version_cfm.phy_band_support);

	if (rwnx_hw->version_cfm.max_mcs == 7) {
		rwnx_hw->mod_params.he_mcs_map = IEEE80211_HE_MCS_SUPPORT_0_7;
		rwnx_hw->mod_params.mcs_map = IEEE80211_VHT_MCS_SUPPORT_0_7;
	} else if (rwnx_hw->version_cfm.max_mcs == 9) {
		rwnx_hw->mod_params.he_mcs_map = IEEE80211_HE_MCS_SUPPORT_0_9;
		rwnx_hw->mod_params.mcs_map = IEEE80211_VHT_MCS_SUPPORT_0_9;
	} else if (rwnx_hw->version_cfm.max_mcs == 11) {
		rwnx_hw->mod_params.he_mcs_map = IEEE80211_HE_MCS_SUPPORT_0_11;
	}

	if ((rwnx_hw->version_cfm.phy_band_support & (0x1 << PHY_BAND_2G4)) ==
	    0) { // does not support 2.4G band
		for (i = 0;
		     i < rwnx_hw->wiphy->bands[NL80211_BAND_2GHZ]->n_channels;
		     i++) {
			rwnx_hw->wiphy->bands[NL80211_BAND_2GHZ]
				->channels[i]
				.flags |= IEEE80211_CHAN_DISABLED;
		}
	} else if ((rwnx_hw->version_cfm.phy_band_support &
		    (0x1 << PHY_BAND_5G)) == 0) { // does not support 5G band
		for (i = 0;
		     i < rwnx_hw->wiphy->bands[NL80211_BAND_5GHZ]->n_channels;
		     i++) {
			rwnx_hw->wiphy->bands[NL80211_BAND_5GHZ]
				->channels[i]
				.flags |= IEEE80211_CHAN_DISABLED;
		}
	}

	if (!rwnx_hw->mod_params.custregd)
		rwnx_hw->mod_params.custchan = false;

	if (rwnx_hw->mod_params.custchan) {
		rwnx_hw->mod_params.mesh = false;
		rwnx_hw->mod_params.tdls = false;
	}

	if (!(sys_feat & BIT(MM_FEAT_UMAC_BIT))) {
		wiphy_err(wiphy,
			  "Loading softmac firmware with fullmac driver\n");
		res = -1;
	}

	if (!(sys_feat & BIT(MM_FEAT_ANT_DIV_BIT))) {
		rwnx_hw->mod_params.ant_div = false;
	}

	if (!(sys_feat & BIT(MM_FEAT_VHT_BIT))) {
		rwnx_hw->mod_params.vht_on = false;
	}

	// Check if HE is supported
	if (!(sys_feat & BIT(MM_FEAT_HE_BIT))) {
		rwnx_hw->mod_params.he_on = false;
		rwnx_hw->mod_params.he_ul_on = false;
	}

	if (!(sys_feat & BIT(MM_FEAT_PS_BIT))) {
		rwnx_hw->mod_params.ps_on = false;
	}

	/* AMSDU (non)support implies different shared structure definition
       so insure that fw and drv have consistent compilation option */
	if (sys_feat & BIT(MM_FEAT_AMSDU_BIT)) {
#ifndef CONFIG_RWNX_SPLIT_TX_BUF
		wiphy_err(
			wiphy,
			"AMSDU enabled in firmware but support not compiled in driver\n");
		res = -1;
#else
		/* Adjust amsdu_maxnb so that it stays in allowed bounds */
		rwnx_adjust_amsdu_maxnb(rwnx_hw);
#endif /* CONFIG_RWNX_SPLIT_TX_BUF */
	} else {
#ifdef CONFIG_RWNX_SPLIT_TX_BUF
		wiphy_err(
			wiphy,
			"AMSDU disabled in firmware but support compiled in driver\n");
		res = -1;
#endif /* CONFIG_RWNX_SPLIT_TX_BUF */
	}

	//DISABLE USPSD in driver
	//if (!(sys_feat & BIT(MM_FEAT_UAPSD_BIT))) {
	rwnx_hw->mod_params.uapsd_timeout = 0;
	//}

	if (!(sys_feat & BIT(MM_FEAT_BFMEE_BIT))) {
		rwnx_hw->mod_params.bfmee = false;
	}

	if ((sys_feat & BIT(MM_FEAT_BFMER_BIT))) {
#ifndef CONFIG_RWNX_BFMER
		wiphy_err(
			wiphy,
			"BFMER enabled in firmware but support not compiled in driver\n");
		res = -1;
#endif /* CONFIG_RWNX_BFMER */
		// Check PHY and MAC HW BFMER support and update parameter accordingly
		if (!(phy_feat & MDM_BFMER_BIT) ||
		    !(mac_feat & BIT(MAC_FEAT1_BFMER_BIT))) {
			rwnx_hw->mod_params.bfmer = false;
			// Disable the feature in the bitfield so that it won't be displayed
			sys_feat &= ~BIT(MM_FEAT_BFMER_BIT);
		}
	} else {
#ifdef CONFIG_RWNX_BFMER
		wiphy_err(
			wiphy,
			"BFMER disabled in firmware but support compiled in driver\n");
		res = -1;
#else
		rwnx_hw->mod_params.bfmer = false;
#endif /* CONFIG_RWNX_BFMER */
	}

	// When we enable bundle mode in USB, we only use one USB endpoint, thus
	// we set remap_to_be flag to tell the HTC/HIF layer to map all the traffic
	// to BE but due to the tid information will be still kept in the host
	// descriptor, we can still have the WMM behavior
	rwnx_hw->core->config.remap_to_be = false;
	if ((sys_feat & BIT(MM_FEAT_INTF_SINGLE_CHAN_BIT))) {
		if (rwnx_hw->core->hif_ops->hif == WQ_HIF_USB)
			rwnx_hw->core->config.remap_to_be = true;
	}

	if (!(sys_feat & BIT(MM_FEAT_TDLS_BIT))) {
		rwnx_hw->mod_params.tdls = false;
	}

	if (!(sys_feat & BIT(MM_FEAT_UF_BIT))) {
		rwnx_hw->mod_params.uf = false;
	}

	if ((sys_feat & BIT(MM_FEAT_MON_DATA_BIT))) {
#ifndef CONFIG_RWNX_MON_DATA
		wiphy_err(
			wiphy,
			"Monitor+Data interface support (MON_DATA) is enabled in firmware but support not compiled in driver\n");
		res = -1;
#endif /* CONFIG_RWNX_MON_DATA */
	} else {
#ifdef CONFIG_RWNX_MON_DATA
		wiphy_err(
			wiphy,
			"Monitor+Data interface support (MON_DATA) disabled in firmware but support compiled in driver\n");
		res = -1;
#endif /* CONFIG_RWNX_MON_DATA */
	}

	//load MAX Rx AMSDU parameter from sys_feat
	amsdu_rx = (sys_feat >> MM_AMSDU_MAX_SIZE_BIT0) & 0x03;

	//Default MAX Rx AMSDU is changed to 11.8K, no-PCIe(SDIO/USB2) interfaces are restricted to 7.9K.
	//This restriction could be removed later once 11.8K is support and tested on SDIO and USB2 mode,
	if (rwnx_hw->core->hif_ops->hif != WQ_HIF_PCIE) {
		//no-PCIe(SDIO/USB2) interfaces are restricted to 7.9K
		amsdu_rx = min(amsdu_rx, 1);
	}

	rwnx_hw->mod_params.amsdu_rx_max = amsdu_rx;
	//amsdu_rx_max: 0--4k, 1--8k, 2--12k
	WQ_DBG(DM_IPC, DL_ERR, "amsdu_rx_max:%d\n",
	       rwnx_hw->mod_params.amsdu_rx_max);

	// Check supported BW
	bw = (phy_feat & MDM_CHBW_MASK) >> MDM_CHBW_LSB;
	// Check if 80MHz BW is supported
	if (bw < 2) {
		rwnx_hw->mod_params.use_80 = false;
	}
	// Check if 40MHz BW is supported
	if (bw < 1)
		rwnx_hw->mod_params.use_2040 = false;

	// 80MHz BW shall be disabled if 40MHz is not enabled
	if (!rwnx_hw->mod_params.use_2040)
		rwnx_hw->mod_params.use_80 = false;

	// Check if HT is supposed to be supported. If not, disable VHT/HE too
	if (!rwnx_hw->mod_params.ht_on) {
		rwnx_hw->mod_params.vht_on = false;
		rwnx_hw->mod_params.he_on = false;
		rwnx_hw->mod_params.he_ul_on = false;
		rwnx_hw->mod_params.use_80 = false;
		rwnx_hw->mod_params.use_2040 = false;
	}

	// LDPC is mandatory for HE40 and above, so if LDPC is not supported, then disable
	// support for 40 and 80MHz
	if (rwnx_hw->mod_params.he_on && !rwnx_hw->mod_params.ldpc_on) {
		rwnx_hw->mod_params.use_80 = false;
		rwnx_hw->mod_params.use_2040 = false;
	}

	// HT greenfield is not supported in modem >= 3.0
	if (__MDM_MAJOR_VERSION(phy_vers) > 0) {
		rwnx_hw->mod_params.gf_rx_on = false;
	}

	if (!(sys_feat & BIT(MM_FEAT_MU_MIMO_RX_BIT)) ||
	    !rwnx_hw->mod_params.bfmee) {
		rwnx_hw->mod_params.murx = false;
	}

	if ((sys_feat & BIT(MM_FEAT_MU_MIMO_TX_BIT))) {
#ifndef CONFIG_RWNX_MUMIMO_TX
		wiphy_err(
			wiphy,
			"MU-MIMO TX enabled in firmware but support not compiled in driver\n");
		res = -1;
#endif /* CONFIG_RWNX_MUMIMO_TX */
		if (!rwnx_hw->mod_params.bfmer)
			rwnx_hw->mod_params.mutx = false;
		// Check PHY and MAC HW MU-MIMO TX support and update parameter accordingly
		else if (!(phy_feat & MDM_MUMIMOTX_BIT) ||
			 !(mac_feat & BIT(MAC_FEAT1_MU_MIMO_TX_BIT))) {
			rwnx_hw->mod_params.mutx = false;
			// Disable the feature in the bitfield so that it won't be displayed
			sys_feat &= ~BIT(MM_FEAT_MU_MIMO_TX_BIT);
		}
	} else {
#ifdef CONFIG_RWNX_MUMIMO_TX
		wiphy_err(
			wiphy,
			"MU-MIMO TX disabled in firmware but support compiled in driver\n");
		res = -1;
#else
		rwnx_hw->mod_params.mutx = false;
#endif /* CONFIG_RWNX_MUMIMO_TX */
	}

	if (sys_feat & BIT(MM_FEAT_WAPI_BIT)) {
		rwnx_enable_wapi(rwnx_hw);
	}

	if (sys_feat & BIT(MM_FEAT_MFP_BIT)) {
		rwnx_enable_mfp(rwnx_hw);
	}

	if (mac_feat & BIT(MAC_FEAT1_GCMP_BIT)) {
		rwnx_enable_gcmp(rwnx_hw);
	}

	if (mac_feat & BIT(MAC_FEAT1_COMPRESS_TXDESC_BIT)) {
		rwnx_hw->mod_params.compress_txdesc = true;
	}

#define QUEUE_NAME "Broadcast/Multicast queue "

	if (sys_feat & BIT(MM_FEAT_BCN_BIT)) {
#if NX_TXQ_CNT == 4
		wiphy_err(
			wiphy, QUEUE_NAME
			"enabled in firmware but support not compiled in driver\n");
		res = -1;
#endif /* NX_TXQ_CNT == 4 */
	} else {
#if NX_TXQ_CNT == 5
		wiphy_err(
			wiphy, QUEUE_NAME
			"disabled in firmware but support compiled in driver\n");
		res = -1;
#endif /* NX_TXQ_CNT == 5 */
	}
#undef QUEUE_NAME

#ifdef CONFIG_RWNX_RADAR
	if (sys_feat & BIT(MM_FEAT_RADAR_BIT)) {
		/* Enable combination with radar detection */
		wiphy->n_iface_combinations++;
	}
#endif /* CONFIG_RWNX_RADAR */

	WQ_DBG(DM_GENERIC, DL_ERR, "%s(%d) phy_feat = %d\n", __func__, __LINE__,
	       phy_feat);
	if (rwnx_hw->version_cfm.nss == 2 &&
	    (!rwnx_hw->mod_params.nss1_force)) {
		phy_feat |= MDM_NSS2_MASK;
	}

#ifndef CONFIG_RWNX_SDM
	switch (__MDM_PHYCFG_FROM_VERS(phy_feat)) {
	case MDM_PHY_CONFIG_TRIDENT:
		rwnx_hw->mod_params.nss = 1;
		if ((rwnx_hw->mod_params.phy_cfg < 0) ||
		    (rwnx_hw->mod_params.phy_cfg > 2))
			rwnx_hw->mod_params.phy_cfg = 2;
		break;
	case MDM_PHY_CONFIG_KARST:
	case MDM_PHY_CONFIG_CATAXIA: {
		int nss_supp = (phy_feat & MDM_NSS_MASK) >> MDM_NSS_LSB;
		if (rwnx_hw->mod_params.nss > nss_supp)
			rwnx_hw->mod_params.nss = nss_supp;
		if ((rwnx_hw->mod_params.phy_cfg < 0) ||
		    (rwnx_hw->mod_params.phy_cfg > 1))
			rwnx_hw->mod_params.phy_cfg = 0;
	} break;
	default:
		WARN_ON(1);
		break;
	}
#endif /* CONFIG_RWNX_SDM */

	if ((rwnx_hw->mod_params.nss < 1) || (rwnx_hw->mod_params.nss > 2))
		rwnx_hw->mod_params.nss = 1;

	if ((rwnx_hw->mod_params.mcs_map < 0) ||
	    (rwnx_hw->mod_params.mcs_map > 2))
		rwnx_hw->mod_params.mcs_map = 0;

#define PRINT_RWNX_PHY_FEAT(feat)                                              \
	(phy_feat & MDM_##feat##_BIT ? "[" #feat "]" : "")

	wiphy_info(wiphy, "PHY features: [NSS=%d][CHBW=%d]%s%s%s%s%s%s%s\n",
		   (phy_feat & MDM_NSS_MASK) >> MDM_NSS_LSB,
		   20 * (1 << ((phy_feat & MDM_CHBW_MASK) >> MDM_CHBW_LSB)),
		   (phy_feat & (MDM_LDPCDEC_BIT | MDM_LDPCENC_BIT)) ==
				   (MDM_LDPCDEC_BIT | MDM_LDPCENC_BIT) ?
			   "[LDPC]" :
			   "",
		   PRINT_RWNX_PHY_FEAT(VHT), PRINT_RWNX_PHY_FEAT(HE),
		   PRINT_RWNX_PHY_FEAT(BFMER), PRINT_RWNX_PHY_FEAT(BFMEE),
		   PRINT_RWNX_PHY_FEAT(MUMIMOTX),
		   PRINT_RWNX_PHY_FEAT(MUMIMORX));

#define PRINT_RWNX_FEAT(feat)                                                  \
	(sys_feat & BIT(MM_FEAT_##feat##_BIT) ? "[" #feat "]" : "")

	wiphy_info(
		wiphy,
		"FW features: %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
		PRINT_RWNX_FEAT(BCN), PRINT_RWNX_FEAT(AUTOBCN),
		PRINT_RWNX_FEAT(HWSCAN), PRINT_RWNX_FEAT(CMON),
		PRINT_RWNX_FEAT(MROLE), PRINT_RWNX_FEAT(RADAR),
		PRINT_RWNX_FEAT(PS), PRINT_RWNX_FEAT(UAPSD),
		PRINT_RWNX_FEAT(DPSM), PRINT_RWNX_FEAT(AMPDU),
		PRINT_RWNX_FEAT(AMSDU), PRINT_RWNX_FEAT(CHNL_CTXT),
		PRINT_RWNX_FEAT(REORD), PRINT_RWNX_FEAT(P2P),
		PRINT_RWNX_FEAT(P2P_GO), PRINT_RWNX_FEAT(UMAC),
		PRINT_RWNX_FEAT(VHT), PRINT_RWNX_FEAT(HE),
		PRINT_RWNX_FEAT(BFMEE), PRINT_RWNX_FEAT(BFMER),
		PRINT_RWNX_FEAT(WAPI), PRINT_RWNX_FEAT(MFP),
		PRINT_RWNX_FEAT(MU_MIMO_RX), PRINT_RWNX_FEAT(MU_MIMO_TX),
		PRINT_RWNX_FEAT(MESH), PRINT_RWNX_FEAT(TDLS),
		PRINT_RWNX_FEAT(ANT_DIV), PRINT_RWNX_FEAT(UF),
		PRINT_RWNX_FEAT(TWT));
#undef PRINT_RWNX_FEAT

	if (fw_max_sta_nb > NX_REMOTE_STA_MAX) {
		wiphy_err(
			wiphy,
			"Different number of supported stations between FW and driver (%d > %d)\n",
			fw_max_sta_nb, NX_REMOTE_STA_MAX);
		res = -1;
	}

	if (fw_max_vif_nb > NX_VIRT_DEV_MAX) {
		wiphy_err(
			wiphy,
			"Different number of supported virtual interfaces between FW and driver(%d > %d)\n",
			fw_max_vif_nb, NX_VIRT_DEV_MAX);
		res = -1;
	}

	return res;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
static void rwnx_set_ppe_threshold(struct rwnx_hw *rwnx_hw,
				   struct ieee80211_sta_he_cap *he_cap)
{
	const u8 PPE_THRES_INFO_OFT = 7;
	const u8 PPE_THRES_INFO_BIT_LEN = 6;
	struct ppe_thres_info_tag {
		u8 ppet16 : 3;
		u8 ppet8 : 3;
	} __packed;

	struct ppe_thres_field_tag {
		u8 nsts : 3;
		u8 ru_idx_bmp : 4;
	};
	int nss = rwnx_hw->mod_params.nss;
	struct ppe_thres_field_tag *ppe_thres_field =
		(struct ppe_thres_field_tag *)he_cap->ppe_thres;
	struct ppe_thres_info_tag ppe_thres_info = {
		.ppet16 = 0, //BSPK
		.ppet8 = 7 //None
	};
	u8 *ppe_thres_info_ptr = (u8 *)&ppe_thres_info;
	u16 *ppe_thres_ptr = (u16 *)he_cap->ppe_thres;
	u8 i, j, cnt, offset;

	if (rwnx_hw->mod_params.use_80) {
		ppe_thres_field->ru_idx_bmp = 7;
		cnt = 3;
	} else if (rwnx_hw->mod_params.use_2040) {
		ppe_thres_field->ru_idx_bmp = 3;
		cnt = 2;
	} else {
		ppe_thres_field->ru_idx_bmp = 1;
		cnt = 1;
	}
	ppe_thres_field->nsts = nss - 1;
	for (i = 0; i < nss; i++) {
		for (j = 0; j < cnt; j++) {
			offset = (i * cnt + j) * PPE_THRES_INFO_BIT_LEN +
				 PPE_THRES_INFO_OFT;
			ppe_thres_ptr = (u16 *)&he_cap->ppe_thres[offset / 8];
			*ppe_thres_ptr |= *ppe_thres_info_ptr << (offset % 8);
		}
	}
}
#else
#ifdef WQ_HE_STA
static void rwnx_set_ppe_threshold(struct rwnx_hw *rwnx_hw)
{
	const u8 PPE_THRES_INFO_OFT = 7;
	const u8 PPE_THRES_INFO_BIT_LEN = 6;
	struct ppe_thres_info_tag {
		u8 ppet16 : 3;
		u8 ppet8 : 3;
	} __packed;

	struct ppe_thres_field_tag {
		u8 nsts : 3;
		u8 ru_idx_bmp : 4;
	};
	int nss = rwnx_hw->mod_params.nss;
	struct ppe_thres_field_tag *ppe_thres_field =
		(struct ppe_thres_field_tag *)rwnx_hw->he_cap.ppe_thres;
	struct ppe_thres_info_tag ppe_thres_info = {
		.ppet16 = 0, //BSPK
		.ppet8 = 7 //None
	};
	u8 *ppe_thres_info_ptr = (u8 *)&ppe_thres_info;
	u16 *ppe_thres_ptr = (u16 *)rwnx_hw->he_cap.ppe_thres;
	u8 i, j, cnt, offset;

	if (rwnx_hw->mod_params.use_80) {
		ppe_thres_field->ru_idx_bmp = 7;
		cnt = 3;
	} else if (rwnx_hw->mod_params.use_2040) {
		ppe_thres_field->ru_idx_bmp = 3;
		cnt = 2;
	} else {
		ppe_thres_field->ru_idx_bmp = 1;
		cnt = 1;
	}
	ppe_thres_field->nsts = nss - 1;
	for (i = 0; i < nss; i++) {
		for (j = 0; j < cnt; j++) {
			offset = (i * cnt + j) * PPE_THRES_INFO_BIT_LEN +
				 PPE_THRES_INFO_OFT;
			ppe_thres_ptr =
				(u16 *)&rwnx_hw->he_cap.ppe_thres[offset / 8];
			*ppe_thres_ptr |= *ppe_thres_info_ptr << (offset % 8);
		}
	}
}
#endif // WQ_HE_STA
#endif // LINUX_VERSION_CODE >= 4.20

static void rwnx_set_vht_capa(struct rwnx_hw *rwnx_hw, struct wiphy *wiphy)
{
	struct ieee80211_supported_band *band_5GHz =
		wiphy->bands[NL80211_BAND_5GHZ];
	int i;
	int nss = rwnx_hw->mod_params.nss;
	int mcs_map;
	int mcs_map_max;
	int mcs_map_max_2ss_rx = IEEE80211_VHT_MCS_SUPPORT_0_9;
	int mcs_map_max_2ss_tx = IEEE80211_VHT_MCS_SUPPORT_0_9;
	int bw_max;

	if (!band_5GHz) {
		WQ_DBG(DM_GENERIC, DL_INF, "%s(%d) not support 5G band\n",
		       __func__, __LINE__);
		return;
	}
	if (!rwnx_hw->mod_params.vht_on)
		return;

	band_5GHz->vht_cap.vht_supported = true;
	if (rwnx_hw->mod_params.sgi80)
		band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_SHORT_GI_80;
	if (rwnx_hw->mod_params.stbc_on)
		band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_RXSTBC_1;
	if (rwnx_hw->mod_params.ldpc_on)
		band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_RXLDPC;
	if (rwnx_hw->mod_params.bfmee) {
		band_5GHz->vht_cap.cap |=
			IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
		band_5GHz->vht_cap.cap |=
			3 << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
		band_5GHz->vht_cap.cap |=
			~(3 << __builtin_ctz(IEEE80211_VHT_CAP_BEAMFORMEE_STS_MAX));
#endif
	}
	if (nss > 1)
		band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_TXSTBC;

	// Update the AMSDU max RX size (not shifted as located at offset 0 of the VHT cap)
	band_5GHz->vht_cap.cap |= rwnx_hw->mod_params.amsdu_rx_max;

	if (rwnx_hw->mod_params.bfmer) {
		band_5GHz->vht_cap.cap |=
			IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE;
		/* Set number of sounding dimensions */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
		band_5GHz->vht_cap.cap |=
			(nss - 1)
			<< IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_SHIFT;
#else
		band_5GHz->vht_cap.cap |=
			(nss - 1)
			<< __builtin_ctz(IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MAX);
#endif
	}
	if (rwnx_hw->mod_params.murx)
		band_5GHz->vht_cap.cap |=
			IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE;
	if (rwnx_hw->mod_params.mutx)
		band_5GHz->vht_cap.cap |=
			IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE;

	/*
     * MCS map:
     * This capabilities are filled according to the mcs_map module parameter.
     * However currently we have some limitations due to FPGA clock constraints
     * that prevent always using the range of MCS that is defined by the
     * parameter:
     *   - in RX, 2SS, we support up to MCS7
     *   - in TX, 2SS, we support up to MCS8
     *   - max_mcs = 7, fpga
     *   - max_mcs != 7, chip
     */
	// Get max supported BW
	if (rwnx_hw->mod_params.use_80) {
		bw_max = PHY_CHNL_BW_80;
		mcs_map_max_2ss_rx = IEEE80211_VHT_MCS_SUPPORT_0_9;
		mcs_map_max_2ss_tx = IEEE80211_VHT_MCS_SUPPORT_0_9;
	} else if (rwnx_hw->mod_params.use_2040)
		bw_max = PHY_CHNL_BW_40;
	else
		bw_max = PHY_CHNL_BW_20;

	//fpga mode
	if (rwnx_hw->version_cfm.max_mcs == 7) {
		bw_max = PHY_CHNL_BW_20;
		mcs_map_max_2ss_rx = IEEE80211_VHT_MCS_SUPPORT_0_7;
		mcs_map_max_2ss_tx = IEEE80211_VHT_MCS_SUPPORT_0_7;
	}

	// Check if MCS map should be limited to MCS0_8 due to the standard. Indeed in BW20,
	// MCS9 is not supported in 1 and 2 SS
	if (rwnx_hw->mod_params.use_2040)
		mcs_map_max = IEEE80211_VHT_MCS_SUPPORT_0_9;
	else
		mcs_map_max = IEEE80211_VHT_MCS_SUPPORT_0_9;

	mcs_map = min_t(int, rwnx_hw->mod_params.mcs_map, mcs_map_max);
	band_5GHz->vht_cap.vht_mcs.rx_mcs_map = cpu_to_le16(0);
	for (i = 0; i < nss; i++) {
		band_5GHz->vht_cap.vht_mcs.rx_mcs_map |=
			cpu_to_le16(mcs_map << (i * 2));
		band_5GHz->vht_cap.vht_mcs.rx_highest =
			MAX_VHT_RATE(mcs_map, nss, bw_max);
		mcs_map = min_t(int, mcs_map, mcs_map_max_2ss_rx);
	}
	for (; i < 8; i++) {
		band_5GHz->vht_cap.vht_mcs.rx_mcs_map |=
			cpu_to_le16(IEEE80211_VHT_MCS_NOT_SUPPORTED << (i * 2));
	}

	mcs_map = min_t(int, rwnx_hw->mod_params.mcs_map, mcs_map_max);
	band_5GHz->vht_cap.vht_mcs.tx_mcs_map = cpu_to_le16(0);
	for (i = 0; i < nss; i++) {
		band_5GHz->vht_cap.vht_mcs.tx_mcs_map |=
			cpu_to_le16(mcs_map << (i * 2));
		band_5GHz->vht_cap.vht_mcs.tx_highest =
			MAX_VHT_RATE(mcs_map, nss, bw_max);
		mcs_map = min_t(int, mcs_map, mcs_map_max_2ss_tx);
	}
	for (; i < 8; i++) {
		band_5GHz->vht_cap.vht_mcs.tx_mcs_map |=
			cpu_to_le16(IEEE80211_VHT_MCS_NOT_SUPPORTED << (i * 2));
	}

	if (!rwnx_hw->mod_params.use_80) {
#ifdef CONFIG_VENDOR_RWNX_VHT_NO80
		band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_NOT_SUP_WIDTH_80;
#endif
		band_5GHz->vht_cap.cap &= ~IEEE80211_VHT_CAP_SHORT_GI_80;
	}

	band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_TX_ANTENNA_PATTERN;
	band_5GHz->vht_cap.cap |= IEEE80211_VHT_CAP_RX_ANTENNA_PATTERN;
}

static void rwnx_set_ht_capa(struct rwnx_hw *rwnx_hw, struct wiphy *wiphy)
{
	struct ieee80211_supported_band *band_5GHz =
		wiphy->bands[NL80211_BAND_5GHZ];
	struct ieee80211_supported_band *band_2GHz =
		wiphy->bands[NL80211_BAND_2GHZ];
	int i;
	int nss = rwnx_hw->mod_params.nss;

	if (!rwnx_hw->mod_params.ht_on) {
		band_2GHz->ht_cap.ht_supported = false;
		if (band_5GHz != NULL)
			band_5GHz->ht_cap.ht_supported = false;
		return;
	}

	if (rwnx_hw->mod_params.stbc_on)
		band_2GHz->ht_cap.cap |= 1 << IEEE80211_HT_CAP_RX_STBC_SHIFT;
	if (rwnx_hw->mod_params.ldpc_on)
		band_2GHz->ht_cap.cap |= IEEE80211_HT_CAP_LDPC_CODING;
	if (rwnx_hw->mod_params.use_2040) {
		band_2GHz->ht_cap.mcs.rx_mask[4] = 0x1; /* MCS32 */
		band_2GHz->ht_cap.cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;
		band_2GHz->ht_cap.mcs.rx_highest = cpu_to_le16(135 * nss);
	} else {
		band_2GHz->ht_cap.mcs.rx_highest = cpu_to_le16(65 * nss);
	}
	if (nss > 1)
		band_2GHz->ht_cap.cap |= IEEE80211_HT_CAP_TX_STBC;

	// Update the AMSDU max RX size
	if (rwnx_hw->mod_params.amsdu_rx_max)
		band_2GHz->ht_cap.cap |= IEEE80211_HT_CAP_MAX_AMSDU;

	if (rwnx_hw->mod_params.sgi) {
		band_2GHz->ht_cap.cap |= IEEE80211_HT_CAP_SGI_20;
		if (rwnx_hw->mod_params.use_2040) {
			band_2GHz->ht_cap.cap |= IEEE80211_HT_CAP_SGI_40;
			band_2GHz->ht_cap.mcs.rx_highest =
				cpu_to_le16(150 * nss);
		} else
			band_2GHz->ht_cap.mcs.rx_highest =
				cpu_to_le16(72 * nss);
	}
	if (rwnx_hw->mod_params.gf_rx_on)
		band_2GHz->ht_cap.cap |= IEEE80211_HT_CAP_GRN_FLD;

	memset(band_2GHz->ht_cap.mcs.rx_mask, 0,
	       sizeof(band_2GHz->ht_cap.mcs.rx_mask));
	for (i = 0; i < nss; i++) {
		band_2GHz->ht_cap.mcs.rx_mask[i] = 0xFF;
	}

	if (band_5GHz != NULL)
		band_5GHz->ht_cap = band_2GHz->ht_cap;
}

static void rwnx_set_he_capa(struct rwnx_hw *rwnx_hw, struct wiphy *wiphy)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
	struct ieee80211_supported_band *band_5GHz =
		wiphy->bands[NL80211_BAND_5GHZ];
	struct ieee80211_supported_band *band_2GHz =
		wiphy->bands[NL80211_BAND_2GHZ];
	int i;
	int nss = rwnx_hw->mod_params.nss;
	struct ieee80211_sta_he_cap *he_cap;
	int mcs_map, mcs_map_max_2ss = IEEE80211_HE_MCS_SUPPORT_0_11;
	u8 dcm_max_ru = IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_484;
	u32 phy_vers = rwnx_hw->version_cfm.version_phy_2;

	if (!rwnx_hw->mod_params.he_on) {
		band_2GHz->iftype_data = NULL;
		band_2GHz->n_iftype_data = 0;
		if (band_5GHz != NULL) {
			band_5GHz->iftype_data = NULL;
			band_5GHz->n_iftype_data = 0;
		}
		return;
	}

	he_cap = (struct ieee80211_sta_he_cap *)&band_2GHz->iftype_data->he_cap;
	he_cap->has_he = true;

	if (rwnx_hw->version_cfm.features & BIT(MM_FEAT_TWT_BIT)) {
		rwnx_hw->ext_capa[9] = WLAN_EXT_CAPA10_TWT_REQUESTER_SUPPORT;
		he_cap->he_cap_elem.mac_cap_info[0] |=
			IEEE80211_HE_MAC_CAP0_TWT_REQ;
	}

	he_cap->he_cap_elem.mac_cap_info[2] |= IEEE80211_HE_MAC_CAP2_ALL_ACK;
	rwnx_set_ppe_threshold(rwnx_hw, he_cap);
	if (rwnx_hw->mod_params.use_2040) {
		he_cap->he_cap_elem.phy_cap_info[0] |=
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G;
		dcm_max_ru = IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_484;
	}
	if (rwnx_hw->mod_params.use_80) {
		he_cap->he_cap_elem.phy_cap_info[0] |=
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G;
		// Limit mcs to 7 for USB, or chip + usb will get ip failed sometimes
		// or iperf uplink dtop to 0 sometimes. PCIE must remove limit, or max
		// tput can not reach highest value
		if (rwnx_hw->core->config.up_to_mcs7) {
			mcs_map_max_2ss = IEEE80211_HE_MCS_SUPPORT_0_7;
		}

		dcm_max_ru = IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_996;
	}
	if (rwnx_hw->mod_params.ldpc_on) {
		he_cap->he_cap_elem.phy_cap_info[1] |=
			IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD;
	} else {
		// If no LDPC is supported, we have to limit to MCS0_9, as LDPC is mandatory
		// for MCS 10 and 11
		rwnx_hw->mod_params.he_mcs_map =
			min_t(int, rwnx_hw->mod_params.he_mcs_map,
			      IEEE80211_HE_MCS_SUPPORT_0_9);
	}
	he_cap->he_cap_elem.phy_cap_info[1] |=
		IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
		IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US;
	he_cap->he_cap_elem.phy_cap_info[2] |=
		IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
		IEEE80211_HE_PHY_CAP2_DOPPLER_TX |
		IEEE80211_HE_PHY_CAP2_DOPPLER_RX |
		IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO |
		IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO;
	if (rwnx_hw->mod_params.stbc_on)
		he_cap->he_cap_elem.phy_cap_info[2] |=
			IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ;
	he_cap->he_cap_elem.phy_cap_info[3] |=
		IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_16_QAM |
		IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_16_QAM |
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 13, 0)
		IEEE80211_HE_PHY_CAP3_RX_PARTIAL_BW_SU_IN_20MHZ_MU;
#else
		IEEE80211_HE_PHY_CAP3_RX_HE_MU_PPDU_FROM_NON_AP_STA;
#endif
	if (nss > 1) {
		he_cap->he_cap_elem.phy_cap_info[3] |=
			IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_2;
	} else {
		he_cap->he_cap_elem.phy_cap_info[3] |=
			IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_1;
	}

	if (rwnx_hw->mod_params.bfmee) {
		he_cap->he_cap_elem.phy_cap_info[4] |=
			IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE;
		he_cap->he_cap_elem.phy_cap_info[4] |=
			IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4;
	}
	he_cap->he_cap_elem.phy_cap_info[5] |=
		IEEE80211_HE_PHY_CAP5_NG16_SU_FEEDBACK |
		IEEE80211_HE_PHY_CAP5_NG16_MU_FEEDBACK;
	he_cap->he_cap_elem.phy_cap_info[6] |=
		IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU |
		IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU |
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 13, 0)
		IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB |
		IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB |
#else
		IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMER_FB |
		IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMER_FB |
#endif
		IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT |
		IEEE80211_HE_PHY_CAP6_PARTIAL_BANDWIDTH_DL_MUMIMO;
	he_cap->he_cap_elem.phy_cap_info[7] |=
		IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI;
	he_cap->he_cap_elem.phy_cap_info[8] |=
		IEEE80211_HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G | dcm_max_ru;
	he_cap->he_cap_elem.phy_cap_info[9] |=
		IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB |
		IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB |
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 17, 0)
		IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_16US;
#else
		IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_16US;
#endif

	// Starting from version v31 more HE_ER_SU modulations is supported
	if (__MDM_VERSION(phy_vers) > 30) {
		he_cap->he_cap_elem.phy_cap_info[6] |=
			IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE;
		he_cap->he_cap_elem.phy_cap_info[8] |=
			IEEE80211_HE_PHY_CAP8_HE_ER_SU_1XLTF_AND_08_US_GI |
			IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI;
	}

	mcs_map = rwnx_hw->mod_params.he_mcs_map;
	memset(&he_cap->he_mcs_nss_supp, 0, sizeof(he_cap->he_mcs_nss_supp));
	for (i = 0; i < nss; i++) {
		__le16 unsup_for_ss =
			cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));
		mcs_map = min_t(int, rwnx_hw->mod_params.he_mcs_map,
				mcs_map_max_2ss);
		he_cap->he_mcs_nss_supp.rx_mcs_80 |=
			cpu_to_le16(mcs_map << (i * 2));
		he_cap->he_mcs_nss_supp.rx_mcs_160 |= unsup_for_ss;
		he_cap->he_mcs_nss_supp.rx_mcs_80p80 |= unsup_for_ss;
	}
	for (; i < 8; i++) {
		__le16 unsup_for_ss =
			cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));
		he_cap->he_mcs_nss_supp.rx_mcs_80 |= unsup_for_ss;
		he_cap->he_mcs_nss_supp.rx_mcs_160 |= unsup_for_ss;
		he_cap->he_mcs_nss_supp.rx_mcs_80p80 |= unsup_for_ss;
	}
	mcs_map = rwnx_hw->mod_params.he_mcs_map;
	for (i = 0; i < nss; i++) {
		__le16 unsup_for_ss =
			cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));
		if (rwnx_hw->core->hif_ops->low_speed_mode) {
			/* if 2.5G pcie speed, limit mcs to 9 */
			mcs_map_max_2ss = IEEE80211_HE_MCS_SUPPORT_0_9;
			mcs_map = min_t(int, rwnx_hw->mod_params.he_mcs_map,
					mcs_map_max_2ss);
		}
		he_cap->he_mcs_nss_supp.tx_mcs_80 |=
			cpu_to_le16(mcs_map << (i * 2));
		he_cap->he_mcs_nss_supp.tx_mcs_160 |= unsup_for_ss;
		he_cap->he_mcs_nss_supp.tx_mcs_80p80 |= unsup_for_ss;
	}
	for (; i < 8; i++) {
		__le16 unsup_for_ss =
			cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));
		he_cap->he_mcs_nss_supp.tx_mcs_80 |= unsup_for_ss;
		he_cap->he_mcs_nss_supp.tx_mcs_160 |= unsup_for_ss;
		he_cap->he_mcs_nss_supp.tx_mcs_80p80 |= unsup_for_ss;
	}
#else
#ifdef WQ_HE_STA
	int i;
	int nss = rwnx_hw->mod_params.nss;
	int mcs_map, mcs_map_max_2ss = IEEE80211_HE_MCS_SUPPORT_0_11;
	u8 dcm_max_ru = IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_242;
	u32 phy_vers = rwnx_hw->version_cfm.version_phy_2;

	if (!rwnx_hw->mod_params.he_on) {
		return;
	}

	memset(&(rwnx_hw->he_cap), 0, sizeof(rwnx_hw->he_cap));
	rwnx_hw->he_cap.mcs_supp.rx_mcs_80 = cpu_to_le16(0xfffa);
	rwnx_hw->he_cap.mcs_supp.tx_mcs_80 = cpu_to_le16(0xfffa);
	rwnx_hw->he_cap.mcs_supp.rx_mcs_160 = cpu_to_le16(0xffff);
	rwnx_hw->he_cap.mcs_supp.tx_mcs_160 = cpu_to_le16(0xffff);
	rwnx_hw->he_cap.mcs_supp.rx_mcs_80p80 = cpu_to_le16(0xffff);
	rwnx_hw->he_cap.mcs_supp.tx_mcs_80p80 = cpu_to_le16(0xffff);

	if (rwnx_hw->version_cfm.features & BIT(MM_FEAT_TWT_BIT)) {
		rwnx_hw->ext_capa[9] = WLAN_EXT_CAPA10_TWT_REQUESTER_SUPPORT;
		rwnx_hw->he_cap.mac_cap_info[0] |=
			IEEE80211_HE_MAC_CAP0_TWT_REQ;
	}
	rwnx_hw->he_cap.mac_cap_info[1] |= IEEE80211_HE_MAC_CAP1_MAC_PAD;
	rwnx_hw->he_cap.mac_cap_info[2] |= IEEE80211_HE_MAC_CAP2_ALL_ACK;
	rwnx_hw->he_cap.mac_cap_info[5] |= IEEE80211_HE_MAC_CAP5_RX_TRIG;
	rwnx_set_ppe_threshold(rwnx_hw);
	if (rwnx_hw->mod_params.use_2040) {
		rwnx_hw->he_cap.phy_cap_info[0] |=
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G;
		rwnx_hw->he_cap.phy_cap_info[0] |=
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G;
		dcm_max_ru = IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_484;
	}
	if (rwnx_hw->mod_params.use_80) {
		rwnx_hw->he_cap.phy_cap_info[0] |=
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G;
		// Limit mcs to 7 for USB, or chip + usb will get ip failed sometimes
		// or iperf uplink dtop to 0 sometimes. PCIE must remove limit, or max
		// tput can not reach highest value
		if (rwnx_hw->core->config.up_to_mcs7) {
			mcs_map_max_2ss = IEEE80211_HE_MCS_SUPPORT_0_7;
		}
		dcm_max_ru = IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_996;
	}
	if (rwnx_hw->mod_params.ldpc_on) {
		rwnx_hw->he_cap.phy_cap_info[1] |=
			IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD;
	} else {
		// If no LDPC is supported, we have to limit to MCS0_9, as LDPC is mandatory
		// for MCS 10 and 11
		rwnx_hw->mod_params.he_mcs_map =
			min_t(int, rwnx_hw->mod_params.he_mcs_map,
			      IEEE80211_HE_MCS_SUPPORT_0_9);
	}

	rwnx_hw->he_cap.phy_cap_info[1] |=
		IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
		IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US;
	rwnx_hw->he_cap.phy_cap_info[2] |=
		IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
		IEEE80211_HE_PHY_CAP2_DOPPLER_TX |
		IEEE80211_HE_PHY_CAP2_DOPPLER_RX |
		IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO |
		IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO;
	if (rwnx_hw->mod_params.stbc_on)
		rwnx_hw->he_cap.phy_cap_info[2] |=
			IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ;
	rwnx_hw->he_cap.phy_cap_info[3] |=
		IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_16_QAM |
		IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_16_QAM |
		IEEE80211_HE_PHY_CAP3_RX_HE_MU_PPDU_FROM_NON_AP_STA;

	if (nss > 1) {
		rwnx_hw->he_cap.phy_cap_info[3] |=
			IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_2;
	} else {
		rwnx_hw->he_cap.phy_cap_info[3] |=
			IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_1;
	}

	if (rwnx_hw->mod_params.bfmee) {
		rwnx_hw->he_cap.phy_cap_info[4] |=
			IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE;
		rwnx_hw->he_cap.phy_cap_info[4] |=
			IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4;
	}
	rwnx_hw->he_cap.phy_cap_info[5] |=
		IEEE80211_HE_PHY_CAP5_NG16_SU_FEEDBACK |
		IEEE80211_HE_PHY_CAP5_NG16_MU_FEEDBACK;
	rwnx_hw->he_cap.phy_cap_info[6] |=
		IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU |
		IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU |
		IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMER_FB |
		IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMER_FB |
		IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE |
		//IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT |
		IEEE80211_HE_PHY_CAP6_PARTIAL_BANDWIDTH_DL_MUMIMO;
	rwnx_hw->he_cap.phy_cap_info[7] |=
		IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI |
		IEEE80211_HE_PHY_CAP7_POWER_BOOST_FACTOR_AR |
		IEEE80211_HE_PHY_CAP7_MAX_NC_2 | IEEE80211_HE_PHY_CAP7_MAX_NC_1;
	rwnx_hw->he_cap.phy_cap_info[8] |=
		IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI |
		IEEE80211_HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G |
		IEEE80211_HE_PHY_CAP8_MIDAMBLE_RX_TX_2X_AND_1XLTF |
		IEEE80211_HE_PHY_CAP8_HE_ER_SU_1XLTF_AND_08_US_GI | dcm_max_ru;
	rwnx_hw->he_cap.phy_cap_info[9] |=
		IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB |
		IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB |
		IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_16US |
		IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU |
		IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU |
		IEEE80211_HE_PHY_CAP9_LONGER_THAN_16_SIGB_OFDM_SYM;

	// Starting from version v31 more HE_ER_SU modulations is supported
	if (__MDM_VERSION(phy_vers) > 30) {
		rwnx_hw->he_cap.phy_cap_info[6] |=
			IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE;
		rwnx_hw->he_cap.phy_cap_info[8] |=
			IEEE80211_HE_PHY_CAP8_HE_ER_SU_1XLTF_AND_08_US_GI |
			IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI;
	}

	mcs_map = rwnx_hw->mod_params.he_mcs_map;
	memset(&(rwnx_hw->he_cap.mcs_supp), 0,
	       sizeof(rwnx_hw->he_cap.mcs_supp));
	for (i = 0; i < nss; i++) {
		__le16 unsup_for_ss =
			cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));
		mcs_map = min_t(int, rwnx_hw->mod_params.he_mcs_map,
				mcs_map_max_2ss);
		rwnx_hw->he_cap.mcs_supp.rx_mcs_80 |=
			cpu_to_le16(mcs_map << (i * 2));
		rwnx_hw->he_cap.mcs_supp.rx_mcs_160 |= unsup_for_ss;
		rwnx_hw->he_cap.mcs_supp.rx_mcs_80p80 |= unsup_for_ss;
	}
	for (; i < 8; i++) {
		__le16 unsup_for_ss =
			cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));
		rwnx_hw->he_cap.mcs_supp.rx_mcs_80 |= unsup_for_ss;
		rwnx_hw->he_cap.mcs_supp.rx_mcs_160 |= unsup_for_ss;
		rwnx_hw->he_cap.mcs_supp.rx_mcs_80p80 |= unsup_for_ss;
	}
	mcs_map = rwnx_hw->mod_params.he_mcs_map;
	for (i = 0; i < nss; i++) {
		__le16 unsup_for_ss =
			cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));
		rwnx_hw->he_cap.mcs_supp.tx_mcs_80 |=
			cpu_to_le16(mcs_map << (i * 2));
		rwnx_hw->he_cap.mcs_supp.tx_mcs_160 |= unsup_for_ss;
		rwnx_hw->he_cap.mcs_supp.tx_mcs_80p80 |= unsup_for_ss;
	}
	for (; i < 8; i++) {
		__le16 unsup_for_ss =
			cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2));
		rwnx_hw->he_cap.mcs_supp.tx_mcs_80 |= unsup_for_ss;
		rwnx_hw->he_cap.mcs_supp.tx_mcs_160 |= unsup_for_ss;
		rwnx_hw->he_cap.mcs_supp.tx_mcs_80p80 |= unsup_for_ss;
	}
#endif // WQ_HE_STA
#endif
}

static void rwnx_set_wiphy_params(struct rwnx_hw *rwnx_hw, struct wiphy *wiphy)
{
	/* FULLMAC specific parameters */
	wiphy->flags |= WIPHY_FLAG_REPORTS_OBSS;
	wiphy->max_scan_ssids = SCAN_SSID_MAX;
	wiphy->max_scan_ie_len = SCANU_MAX_IE_LEN;

    if(wq_conf.sched_scan_enable) {
        /* Sched Scan */
        wiphy->max_sched_scan_ssids = SCAN_SSID_MAX;
        wiphy->max_match_sets = MATCH_SET_MAX;
        wiphy->max_sched_scan_ie_len =SCANU_MAX_IE_LEN;
#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 10, 0)
        wiphy->max_sched_scan_reqs = 1;
        wiphy->max_sched_scan_plans = SCHED_SCAN_PLAN_MAX;
        wiphy->max_sched_scan_plan_interval = U16_MAX;
        wiphy->max_sched_scan_plan_iterations = 254;
#endif
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
	wiphy->support_mbssid = 1;
#endif

	if (rwnx_hw->mod_params.tdls) {
		/* TDLS support */
		wiphy->flags |= WIPHY_FLAG_SUPPORTS_TDLS;
		/* TDLS external setup support */
		wiphy->flags |= WIPHY_FLAG_TDLS_EXTERNAL_SETUP;
	}

	if (rwnx_hw->mod_params.ap_uapsd_on)
		wiphy->flags |= WIPHY_FLAG_AP_UAPSD;

	if (rwnx_hw->mod_params.ps_on)
		wiphy->flags |= WIPHY_FLAG_PS_ON_BY_DEFAULT;
	else
		wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;

	if (rwnx_hw->mod_params.custregd) {
		// Check if custom channel set shall be enabled. In such case only monitor mode is
		// supported
		if (rwnx_hw->mod_params.custchan) {
			wiphy->interface_modes = BIT(NL80211_IFTYPE_MONITOR);

			// Enable "extra" channels
			wiphy->bands[NL80211_BAND_2GHZ]->n_channels += 13;
			if (wiphy->bands[NL80211_BAND_5GHZ] != NULL)
				wiphy->bands[NL80211_BAND_5GHZ]->n_channels +=
					59;
		}
	}
}

static void rwnx_set_rf_params(struct rwnx_hw *rwnx_hw, struct wiphy *wiphy)
{
#ifndef CONFIG_RWNX_SDM
	struct ieee80211_supported_band *band_5GHz =
		wiphy->bands[NL80211_BAND_5GHZ];
	u32 mdm_phy_cfg =
		__MDM_PHYCFG_FROM_VERS(rwnx_hw->version_cfm.version_phy_1);
#if 0 //TODO  init too much time
    struct rwnx_phy_conf_file phy_conf;

    /*
     * Get configuration file depending on the RF
     */
    if (mdm_phy_cfg == MDM_PHY_CONFIG_TRIDENT) {
        // Retrieve the Trident configuration
        rwnx_parse_phy_configfile(rwnx_hw, RWNX_PHY_CONFIG_TRD_NAME,
                                  &phy_conf, rwnx_hw->mod_params.phy_cfg);
        memcpy(&rwnx_hw->phy.cfg, &phy_conf.trd, sizeof(phy_conf.trd));
    } else if (mdm_phy_cfg == MDM_PHY_CONFIG_CATAXIA) {
        memset(&phy_conf.cataxia, 0, sizeof(phy_conf.cataxia));
        phy_conf.cataxia.path_used = rwnx_hw->mod_params.phy_cfg;
        memcpy(&rwnx_hw->phy.cfg, &phy_conf.cataxia, sizeof(phy_conf.cataxia));
    } else if (mdm_phy_cfg == MDM_PHY_CONFIG_KARST) {
        // We use the NSS parameter as is
        // Retrieve the Karst configuration
        rwnx_parse_phy_configfile(rwnx_hw, RWNX_PHY_CONFIG_KARST_NAME,
                                  &phy_conf, rwnx_hw->mod_params.phy_cfg);

        memcpy(&rwnx_hw->phy.cfg, &phy_conf.karst, sizeof(phy_conf.karst));
    } else {
        WARN_ON(1);
    }
#endif
	/*
     * adjust caps depending on the RF
     */
	switch (mdm_phy_cfg) {
	case MDM_PHY_CONFIG_TRIDENT: {
		if (!rwnx_hw->mod_params.use_80) {
			wiphy_dbg(wiphy,
				  "found Trident PHY .. limit BW to 40MHz\n");
			rwnx_hw->phy.limit_bw = true;
			if (band_5GHz != NULL) {
#ifdef CONFIG_VENDOR_RWNX_VHT_NO80
				band_5GHz->vht_cap.cap |=
					IEEE80211_VHT_CAP_NOT_SUP_WIDTH_80;
#endif
				band_5GHz->vht_cap.cap &=
					~(IEEE80211_VHT_CAP_SHORT_GI_80);
				//| IEEE80211_VHT_CAP_RXSTBC_MASK);
			}
		}

		break;
	}
	case MDM_PHY_CONFIG_CATAXIA: {
		wiphy_dbg(wiphy, "found CATAXIA PHY\n");
		break;
	}
	case MDM_PHY_CONFIG_KARST: {
		wiphy_dbg(wiphy, "found KARST PHY\n");
		break;
	}
	default:
		WARN_ON(1);
		break;
	}
#endif /* CONFIG_RWNX_SDM */
}

int rwnx_handle_dynparams(struct rwnx_hw *rwnx_hw, struct wiphy *wiphy)
{
	int ret;

	/* Check compatibility between requested parameters and HW/SW features */
	ret = rwnx_check_fw_hw_feature(rwnx_hw, wiphy);
	print_fw_hw_feature(&rwnx_hw->mod_params);
	if (ret)
		return ret;
#if 0 //TODO: USB project, mark temporarily, use default config.

    /* Allocate the RX buffers according to the maximum AMSDU RX size */
    ret = rwnx_ipc_rxbuf_init(rwnx_hw,
                              (4 * (rwnx_hw->mod_params.amsdu_rx_max + 1) + 1) * 1024);
    if (ret) {
        wiphy_err(wiphy, "Cannot allocate the RX buffers\n");
        return ret;
    }
#endif

	/* Set wiphy parameters */
	rwnx_set_wiphy_params(rwnx_hw, wiphy);

	/* Set VHT capabilities */
	rwnx_set_vht_capa(rwnx_hw, wiphy);

	/* Set HE capabilities */
	rwnx_set_he_capa(rwnx_hw, wiphy);

	/* Set HT capabilities */
	rwnx_set_ht_capa(rwnx_hw, wiphy);

	/* Set RF specific parameters (shall be done last as it might change some
       capabilities previously set) */
	rwnx_set_rf_params(rwnx_hw, wiphy);

	return 0;
}
void wq_update_mac_capa(struct rwnx_hw *rwnx_hw, struct wiphy *wiphy)
{
	/* Set VHT capabilities */
	rwnx_set_vht_capa(rwnx_hw, wiphy);

	/* Set HE capabilities */
	rwnx_set_he_capa(rwnx_hw, wiphy);

	/* Set HT capabilities */
	rwnx_set_ht_capa(rwnx_hw, wiphy);

	/* Set RF specific parameters (shall be done last as it might change some
       capabilities previously set) */
	rwnx_set_rf_params(rwnx_hw, wiphy);
}

void rwnx_custregd(struct rwnx_hw *rwnx_hw, struct wiphy *wiphy)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
	if (!rwnx_hw->mod_params.custregd)
		return;

	/* REGULATORY_IGNORE_STALE_KICKOFF is removed since Linux 6.1.0-12, and it's useless. */
	/* wiphy->regulatory_flags |= REGULATORY_IGNORE_STALE_KICKOFF; */
	wiphy->regulatory_flags |= REGULATORY_WIPHY_SELF_MANAGED;

	rtnl_lock();
#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 12, 0)
	if (regulatory_set_wiphy_regd_sync(wiphy, &rwnx_regdom))
#else
	if (regulatory_set_wiphy_regd_sync_rtnl(wiphy, &rwnx_regdom))
#endif
		wiphy_err(wiphy, "Failed to set custom regdomain\n");
	else
		wiphy_err(
			wiphy,
			"\n"
			"*******************************************************\n"
			"** CAUTION: USING PERMISSIVE CUSTOM REGULATORY RULES **\n"
			"*******************************************************\n");
	rtnl_unlock();
#endif
}

void rwnx_adjust_amsdu_maxnb(struct rwnx_hw *rwnx_hw)
{
	if (rwnx_hw->mod_params.amsdu_maxnb > NX_TX_PAYLOAD_MAX)
		rwnx_hw->mod_params.amsdu_maxnb = NX_TX_PAYLOAD_MAX;
	else if (rwnx_hw->mod_params.amsdu_maxnb == 0)
		rwnx_hw->mod_params.amsdu_maxnb = 1;
}
