/**
 ******************************************************************************
 *
 * @file rwnx_compat.h
 *
 * Ensure driver compilation for linux 4.4 to 5.9
 *
 * To avoid too many #if LINUX_VERSION_CODE if the code, when prototype change
 * between different kernel version:
 * - For external function, define a macro whose name is the function name with
 *   _compat suffix and prototype (actually the number of parameter) of the
 *   latest version. Then latest version this macro simply call the function
 *   and for older kernel version it call the function adapting the api.
 * - For internal function (e.g. cfg80211_ops) do the same but the macro name
 *   doesn't need to have the _compat suffix when the function is not used
 *   directly by the driver
 *
 * Copyright (C) RivieraWaves 2020
 *
 ******************************************************************************
 */
#ifndef _RWNX_COMPAT_H_
#define _RWNX_COMPAT_H_
#include <linux/version.h>
#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include "wq_compat.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 14)
#error "Minimum kernel version supported is 3.10.14"
#endif

/******************************************************************************
 * Generic
 *****************************************************************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
#define __bf_shf(x) (__builtin_ffsll(x) - 1)
#define FIELD_PREP(_mask, _val)                                                \
	(((typeof(_mask))(_val) << __bf_shf(_mask)) & (_mask))
#else
#include <linux/bitfield.h>
#endif // 4.9

/******************************************************************************
 * CFG80211
 *****************************************************************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 13, 0)
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_0		0x00
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_1		0x08
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_2		0x10
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_3		0x18
#endif // 5.13

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 1, 0)
#define WLAN_EXT_CAPA3_MULTI_BSSID_SUPPORT 0

#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_242 0x00
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_484 0x40
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_996 0x80
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_2x996 0xc0
#define IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_MASK 0xc0

#define IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_0US 0x00
#define IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_8US 0x40
#define IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_16US 0x80
#define IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_RESERVED 0xc0
#define IEEE80211_HE_PHY_CAP9_NOMIMAL_PKT_PADDING_MASK 0xc0

#ifndef for_each_element
/* for some reason, NXP platform removed this part from its <net/cfg80211.h> */
struct element {
	u8 id;
	u8 datalen;
	u8 data[];
} __packed;

/* element iteration helpers */
#define for_each_element(_elem, _data, _datalen)                               \
	for (_elem = (const struct element *)(_data);                          \
	     (const u8 *)(_data) + (_datalen) - (const u8 *)_elem >=           \
		     (int)sizeof(*_elem) &&                                    \
	     (const u8 *)(_data) + (_datalen) - (const u8 *)_elem >=           \
		     (int)sizeof(*_elem) + _elem->datalen;                     \
	     _elem = (const struct element *)(_elem->data + _elem->datalen))
#endif

static inline const struct element *cfg80211_find_elem(u8 eid, const u8 *ies,
						       int len)
{
	const struct element *elem;
	for_each_element (elem, ies, len) {
		if (elem->id == (eid))
			return elem;
	}
	return NULL;
}

#endif // 5.1

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 0, 0)
#define cfg80211_notify_new_peer_candidate(dev, addr, ie, ie_len, sig_dbm,     \
					   gfp)                                \
	cfg80211_notify_new_peer_candidate(dev, addr, ie, ie_len, gfp)

#define WLAN_EXT_CAPA10_TWT_REQUESTER_SUPPORT BIT(5)
#define WLAN_EXT_CAPA10_TWT_RESPONDER_SUPPORT BIT(6)

#endif // 5.0

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0)
#define IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4 0x1c
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 20, 0)
#define IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_MASK                           \
	IEEE80211_HE_MAC_CAP3_MAX_A_AMPDU_LEN_EXP_MASK

#ifdef WQ_HE_STA
#define IEEE80211_HE_MAC_CAP0_TWT_REQ 0x02
#define IEEE80211_HE_MAC_CAP1_MAC_PAD 0x08 //16us
#define IEEE80211_HE_MAC_CAP2_ALL_ACK 0x02
#define IEEE80211_HE_MAC_CAP5_RX_TRIG 0x80

#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G 0x02
#define IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G 0x04

#define IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A 0x10
#define IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD 0x20
#define IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US 0x40
#define IEEE80211_HE_PHY_CAP1_MIDAMBLE_RX_TX_MAX_NSTS 0x80

#define IEEE80211_HE_PHY_CAP2_MIDAMBLE_RX_TX_MAX_NSTS 0x01
#define IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US 0x02
#define IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ 0x08
#define IEEE80211_HE_PHY_CAP2_DOPPLER_TX 0x10
#define IEEE80211_HE_PHY_CAP2_DOPPLER_RX 0x20
#define IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO 0x40
#define IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO 0x80

#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_16_QAM 0x03
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_16_QAM 0x18
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_1 0x00
#define IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_2 0x20
#define IEEE80211_HE_PHY_CAP3_RX_HE_MU_PPDU_FROM_NON_AP_STA 0x40

#define IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE 0x01

#define IEEE80211_HE_PHY_CAP5_NG16_SU_FEEDBACK 0x40
#define IEEE80211_HE_PHY_CAP5_NG16_MU_FEEDBACK 0x80

#define IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU 0x01
#define IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU 0x02
#define IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMER_FB 0x04
#define IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMER_FB 0x08
#define IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE 0x20
#define IEEE80211_HE_PHY_CAP6_PARTIAL_BANDWIDTH_DL_MUMIMO 0x40
#define IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT 0x80

#define IEEE80211_HE_PHY_CAP7_POWER_BOOST_FACTOR_AR 0x02
#define IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI 0x04
#define IEEE80211_HE_PHY_CAP7_MAX_NC_1 0x08
#define IEEE80211_HE_PHY_CAP7_MAX_NC_2 0x10

#define IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI 0x01
#define IEEE80211_HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G 0x02
#define IEEE80211_HE_PHY_CAP8_HE_ER_SU_1XLTF_AND_08_US_GI 0x10
#define IEEE80211_HE_PHY_CAP8_MIDAMBLE_RX_TX_2X_AND_1XLTF 0x20

#define IEEE80211_HE_PHY_CAP9_LONGER_THAN_16_SIGB_OFDM_SYM 0x01
#define IEEE80211_HE_PHY_CAP9_NON_TRIGGERED_CQI_FEEDBACK 0x02
#define IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU 0x04
#define IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU 0x08
#define IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB 0x10
#define IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB 0x20

#endif // WQ_HE_STA
#endif //4.20

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 16, 0)
#ifdef WQ_HE_STA
#define IEEE80211_HE_PHY_CAP3_RX_HE_MU_PPDU_FROM_NON_AP_STA 0x40

#define IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMER_FB 0x04
#define IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMER_FB 0x08
#endif // WQ_HE_STA
#endif //5.16

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0)
#define IEEE80211_RADIOTAP_HE 23
#define IEEE80211_RADIOTAP_HE_MU 24

struct ieee80211_radiotap_he {
	__le16 data1, data2, data3, data4, data5, data6;
};

enum ieee80211_radiotap_he_bits {
	IEEE80211_RADIOTAP_HE_DATA1_FORMAT_MASK = 3,
	IEEE80211_RADIOTAP_HE_DATA1_FORMAT_SU = 0,
	IEEE80211_RADIOTAP_HE_DATA1_FORMAT_EXT_SU = 1,
	IEEE80211_RADIOTAP_HE_DATA1_FORMAT_MU = 2,
	IEEE80211_RADIOTAP_HE_DATA1_FORMAT_TRIG = 3,

	IEEE80211_RADIOTAP_HE_DATA1_BSS_COLOR_KNOWN = 0x0004,
	IEEE80211_RADIOTAP_HE_DATA1_BEAM_CHANGE_KNOWN = 0x0008,
	IEEE80211_RADIOTAP_HE_DATA1_UL_DL_KNOWN = 0x0010,
	IEEE80211_RADIOTAP_HE_DATA1_DATA_MCS_KNOWN = 0x0020,
	IEEE80211_RADIOTAP_HE_DATA1_DATA_DCM_KNOWN = 0x0040,
	IEEE80211_RADIOTAP_HE_DATA1_CODING_KNOWN = 0x0080,
	IEEE80211_RADIOTAP_HE_DATA1_LDPC_XSYMSEG_KNOWN = 0x0100,
	IEEE80211_RADIOTAP_HE_DATA1_STBC_KNOWN = 0x0200,
	IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE_KNOWN = 0x0400,
	IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE2_KNOWN = 0x0800,
	IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE3_KNOWN = 0x1000,
	IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE4_KNOWN = 0x2000,
	IEEE80211_RADIOTAP_HE_DATA1_BW_RU_ALLOC_KNOWN = 0x4000,
	IEEE80211_RADIOTAP_HE_DATA1_DOPPLER_KNOWN = 0x8000,

	IEEE80211_RADIOTAP_HE_DATA2_PRISEC_80_KNOWN = 0x0001,
	IEEE80211_RADIOTAP_HE_DATA2_GI_KNOWN = 0x0002,
	IEEE80211_RADIOTAP_HE_DATA2_NUM_LTF_SYMS_KNOWN = 0x0004,
	IEEE80211_RADIOTAP_HE_DATA2_PRE_FEC_PAD_KNOWN = 0x0008,
	IEEE80211_RADIOTAP_HE_DATA2_TXBF_KNOWN = 0x0010,
	IEEE80211_RADIOTAP_HE_DATA2_PE_DISAMBIG_KNOWN = 0x0020,
	IEEE80211_RADIOTAP_HE_DATA2_TXOP_KNOWN = 0x0040,
	IEEE80211_RADIOTAP_HE_DATA2_MIDAMBLE_KNOWN = 0x0080,
	IEEE80211_RADIOTAP_HE_DATA2_RU_OFFSET = 0x3f00,
	IEEE80211_RADIOTAP_HE_DATA2_RU_OFFSET_KNOWN = 0x4000,
	IEEE80211_RADIOTAP_HE_DATA2_PRISEC_80_SEC = 0x8000,

	IEEE80211_RADIOTAP_HE_DATA3_BSS_COLOR = 0x003f,
	IEEE80211_RADIOTAP_HE_DATA3_BEAM_CHANGE = 0x0040,
	IEEE80211_RADIOTAP_HE_DATA3_UL_DL = 0x0080,
	IEEE80211_RADIOTAP_HE_DATA3_DATA_MCS = 0x0f00,
	IEEE80211_RADIOTAP_HE_DATA3_DATA_DCM = 0x1000,
	IEEE80211_RADIOTAP_HE_DATA3_CODING = 0x2000,
	IEEE80211_RADIOTAP_HE_DATA3_LDPC_XSYMSEG = 0x4000,
	IEEE80211_RADIOTAP_HE_DATA3_STBC = 0x8000,

	IEEE80211_RADIOTAP_HE_DATA4_SU_MU_SPTL_REUSE = 0x000f,
	IEEE80211_RADIOTAP_HE_DATA4_MU_STA_ID = 0x7ff0,
	IEEE80211_RADIOTAP_HE_DATA4_TB_SPTL_REUSE1 = 0x000f,
	IEEE80211_RADIOTAP_HE_DATA4_TB_SPTL_REUSE2 = 0x00f0,
	IEEE80211_RADIOTAP_HE_DATA4_TB_SPTL_REUSE3 = 0x0f00,
	IEEE80211_RADIOTAP_HE_DATA4_TB_SPTL_REUSE4 = 0xf000,

	IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC = 0x000f,
	IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_20MHZ = 0,
	IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_40MHZ = 1,
	IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_80MHZ = 2,
	IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_160MHZ = 3,
	IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_26T = 4,
	IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_52T = 5,
	IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_106T = 6,
	IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_242T = 7,
	IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_484T = 8,
	IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_996T = 9,
	IEEE80211_RADIOTAP_HE_DATA5_DATA_BW_RU_ALLOC_2x996T = 10,

	IEEE80211_RADIOTAP_HE_DATA5_GI = 0x0030,
	IEEE80211_RADIOTAP_HE_DATA5_GI_0_8 = 0,
	IEEE80211_RADIOTAP_HE_DATA5_GI_1_6 = 1,
	IEEE80211_RADIOTAP_HE_DATA5_GI_3_2 = 2,

	IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE = 0x00c0,
	IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_UNKNOWN = 0,
	IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_1X = 1,
	IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_2X = 2,
	IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_4X = 3,
	IEEE80211_RADIOTAP_HE_DATA5_NUM_LTF_SYMS = 0x0700,
	IEEE80211_RADIOTAP_HE_DATA5_PRE_FEC_PAD = 0x3000,
	IEEE80211_RADIOTAP_HE_DATA5_TXBF = 0x4000,
	IEEE80211_RADIOTAP_HE_DATA5_PE_DISAMBIG = 0x8000,

	IEEE80211_RADIOTAP_HE_DATA6_NSTS = 0x000f,
	IEEE80211_RADIOTAP_HE_DATA6_DOPPLER = 0x0010,
	IEEE80211_RADIOTAP_HE_DATA6_TXOP = 0x7f00,
	IEEE80211_RADIOTAP_HE_DATA6_MIDAMBLE_PDCTY = 0x8000,
};

struct ieee80211_radiotap_he_mu {
	__le16 flags1, flags2;
	u8 ru_ch1[4];
	u8 ru_ch2[4];
};

enum ieee80211_radiotap_he_mu_bits {
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_MCS = 0x000f,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_MCS_KNOWN = 0x0010,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_DCM = 0x0020,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_DCM_KNOWN = 0x0040,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH2_CTR_26T_RU_KNOWN = 0x0080,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH1_RU_KNOWN = 0x0100,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH2_RU_KNOWN = 0x0200,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH1_CTR_26T_RU_KNOWN = 0x1000,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH1_CTR_26T_RU = 0x2000,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_COMP_KNOWN = 0x4000,
	IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_SYMS_USERS_KNOWN = 0x8000,

	IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW = 0x0003,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW_20MHZ = 0x0000,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW_40MHZ = 0x0001,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW_80MHZ = 0x0002,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW_160MHZ = 0x0003,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW_KNOWN = 0x0004,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_SIG_B_COMP = 0x0008,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_SIG_B_SYMS_USERS = 0x00f0,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_PUNC_FROM_SIG_A_BW = 0x0300,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_PUNC_FROM_SIG_A_BW_KNOWN = 0x0400,
	IEEE80211_RADIOTAP_HE_MU_FLAGS2_CH2_CTR_26T_RU = 0x0800,
};

enum { IEEE80211_HE_MCS_SUPPORT_0_7 = 0,
       IEEE80211_HE_MCS_SUPPORT_0_9 = 1,
       IEEE80211_HE_MCS_SUPPORT_0_11 = 2,
       IEEE80211_HE_MCS_NOT_SUPPORTED = 3,
};
#endif // 4.19

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 17, 0)
#define cfg80211_probe_status(ndev, addr, cookie, ack, ack_pwr, pwr_valid,     \
			      gfp)                                             \
	cfg80211_probe_status(ndev, addr, cookie, ack, gfp)
#endif // 4.17

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
#define rwnx_cfg80211_add_iface(wiphy, name, name_assign_type, type, params) \
    rwnx_cfg80211_add_iface(wiphy, name, name_assign_type, type, u32 *flags, params)
#else
#define rwnx_cfg80211_add_iface(wiphy, name, name_assign_type, type, params) \
    rwnx_cfg80211_add_iface(wiphy, name, type, u32 *flags, params)
#endif

#define rwnx_cfg80211_change_iface(wiphy, dev, type, params)                   \
	rwnx_cfg80211_change_iface(wiphy, dev, type, u32 *flags, params)

#define CCFS0(vht) vht->center_freq_seg1_idx
#define CCFS1(vht) vht->center_freq_seg2_idx

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
#define nla_parse(tb, maxtype, head, len, policy, extack)                      \
	nla_parse(tb, maxtype, head, len, policy)
#endif

struct cfg80211_roam_info {
	struct ieee80211_channel *channel;
	struct cfg80211_bss *bss;
	const u8 *bssid;
	const u8 *req_ie;
	size_t req_ie_len;
	const u8 *resp_ie;
	size_t resp_ie_len;
};

#define cfg80211_roamed(_dev, _info, _gfp)                                     \
	cfg80211_roamed(_dev, (_info)->channel, (_info)->bssid,                \
			(_info)->req_ie, (_info)->req_ie_len,                  \
			(_info)->resp_ie, (_info)->resp_ie_len, _gfp)

#else // 4.12

#define CCFS0(vht) vht->center_freq_seg0_idx
#define CCFS1(vht) vht->center_freq_seg1_idx
#endif // 4.12

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0)
#define cfg80211_cqm_rssi_notify(dev, event, level, gfp)                       \
	cfg80211_cqm_rssi_notify(dev, event, gfp)
#endif // 4.11

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0)
#define ieee80211_amsdu_to_8023s(skb, list, addr, iftype, extra_headroom,      \
				 check_da, check_sa)                           \
	ieee80211_amsdu_to_8023s(skb, list, addr, iftype, extra_headroom, false)
#endif // 4.9

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 7, 0)
#define NUM_NL80211_BANDS IEEE80211_NUM_BANDS
#endif // 4.7

#define SURVEY_TIME(s) s->time
#define SURVEY_TIME_BUSY(s) s->time_busy
#define STA_TDLS_INITIATOR(sta) sta->tdls_initiator

/******************************************************************************
 * MAC80211
 *****************************************************************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 3, 0)
#define rwnx_ops_cancel_remain_on_channel(hw, vif)                             \
	rwnx_ops_cancel_remain_on_channel(hw)
#endif // 5.3

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 18, 0)
#define rwnx_ops_mgd_prepare_tx(hw, vif, duration)                             \
	rwnx_ops_mgd_prepare_tx(hw, vif)
#endif // 4.18

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0)

#define RX_ENC_HT(s) s->flag |= RX_FLAG_HT
#define RX_ENC_HT_GF(s) s->flag |= (RX_FLAG_HT | RX_FLAG_HT_GF)
#define RX_ENC_VHT(s) s->flag |= RX_FLAG_HT
#define RX_ENC_HE(s) s->flag |= RX_FLAG_HT
#define RX_ENC_FLAG_SHORT_GI(s) s->flag |= RX_FLAG_SHORT_GI
#define RX_ENC_FLAG_SHORT_PRE(s) s->flag |= RX_FLAG_SHORTPRE
#define RX_ENC_FLAG_LDPC(s) s->flag |= RX_FLAG_LDPC
#define RX_BW_40MHZ(s) s->flag |= RX_FLAG_40MHZ
#define RX_BW_80MHZ(s) s->vht_flag |= RX_VHT_FLAG_80MHZ
#define RX_BW_160MHZ(s) s->vht_flag |= RX_VHT_FLAG_160MHZ
#define RX_NSS(s) s->vht_nss

#else
#define RX_ENC_HT(s) s->encoding = RX_ENC_HT
#define RX_ENC_HT_GF(s)                                                        \
	{                                                                      \
		s->encoding = RX_ENC_HT;                                       \
		s->enc_flags |= RX_ENC_FLAG_HT_GF;                             \
	}
#define RX_ENC_VHT(s) s->encoding = RX_ENC_VHT
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0)
#define RX_ENC_HE(s) s->encoding = RX_ENC_VHT
#else
#define RX_ENC_HE(s) s->encoding = RX_ENC_HE
#endif
#define RX_ENC_FLAG_SHORT_GI(s) s->enc_flags |= RX_ENC_FLAG_SHORT_GI
#define RX_ENC_FLAG_SHORT_PRE(s) s->enc_flags |= RX_ENC_FLAG_SHORTPRE
#define RX_ENC_FLAG_LDPC(s) s->enc_flags |= RX_ENC_FLAG_LDPC
#define RX_BW_40MHZ(s) s->bw = RATE_INFO_BW_40
#define RX_BW_80MHZ(s) s->bw = RATE_INFO_BW_80
#define RX_BW_160MHZ(s) s->bw = RATE_INFO_BW_160
#define RX_NSS(s) s->nss

#endif // 4.12

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0)
#define ieee80211_cqm_rssi_notify(vif, event, level, gfp)                      \
	ieee80211_cqm_rssi_notify(vif, event, gfp)
#endif // 4.11

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 7, 0)
#define RX_FLAG_MIC_STRIPPED 0
#endif // 4.7

#ifndef CONFIG_VENDOR_RWNX_AMSDUS_TX
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0))
#define rwnx_ops_ampdu_action(hw, vif, params)                                 \
	rwnx_ops_ampdu_action(hw, vif,                                         \
			      enum ieee80211_ampdu_mlme_action action,         \
			      struct ieee80211_sta *sta, u16 tid, u16 *ssn,    \
			      u8 buf_size, bool amsdu)
#endif // 4.6
#endif /* CONFIG_VENDOR_RWNX_AMSDUS_TX */

/******************************************************************************
 * NET
 *****************************************************************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0)
#define rwnx_select_queue(dev, skb, sb_dev) rwnx_select_queue(dev, skb)
#define rwnx_monitor_select_queue(dev, skb, sb_dev) \
	rwnx_monitor_select_queue(dev, skb)
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0)
#define rwnx_select_queue(dev, skb, sb_dev)                                    \
	rwnx_select_queue(dev, skb, void *accel_priv,                          \
			  select_queue_fallback_t fallback)
#define rwnx_monitor_select_queue(dev, skb, sb_dev)                            \
	rwnx_monitor_select_queue(dev, skb, void *accel_priv,                  \
			  select_queue_fallback_t fallback)
#elif LINUX_VERSION_CODE < KERNEL_VERSION(5, 0, 0)
#define rwnx_select_queue(dev, skb, sb_dev)                                    \
	rwnx_select_queue(dev, skb, sb_dev, select_queue_fallback_t fallback)
#define rwnx_monitor_select_queue(dev, skb, sb_dev)                            \
	rwnx_monitor_select_queue(dev, skb, sb_dev, select_queue_fallback_t fallback)
#endif //4.19

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0)) &&                         \
	!(defined CONFIG_VENDOR_RWNX)
#define sk_pacing_shift_update(sk, shift)
#endif // 4.16

/******************************************************************************
 * TRACE
 *****************************************************************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 2, 0)
#define trace_print_symbols_seq ftrace_print_symbols_seq
#endif // 4.2

/******************************************************************************
 * TIME
 *****************************************************************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 8, 0)
#define time64_to_tm(t, o, tm) time_to_tm((time_t)t, o, tm)
#endif // 4.8

/******************************************************************************
 * timer
 *****************************************************************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
#define from_timer(var, callback_timer, timer_fieldname)                       \
	container_of(callback_timer, typeof(*var), timer_fieldname)

#define timer_setup(timer, callback, flags)                                    \
	__setup_timer(timer, (void (*)(unsigned long))callback,                \
		      (unsigned long)timer, flags)
#endif // 4.14

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
#define WQ_GET_TIME_SEC(_time) _time.tv_sec
#define WQ_GET_TIME_MSEC(_time) (_time.tv_nsec / 1000) /*Get milliseconds*/
#define WQ_GET_TIME_NSEC(_time) _time.tv_nsec
#else
#define timespec64 timeval
#define ktime_get_real_ts64 do_gettimeofday
#define WQ_GET_TIME_SEC(_time) _time.tv_sec
#define WQ_GET_TIME_MSEC(_time) _time.tv_usec
#define WQ_GET_TIME_NSEC(_time) (_time.tv_usec * 1000)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 11, 0)
#define IEEE80211_MAX_CHAINS	4
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0)
/**
 * struct cfg80211_mgmt_tx_params - mgmt tx parameters
 *
 * This structure provides information needed to transmit a mgmt frame
 *
 * @chan: channel to use
 * @offchan: indicates wether off channel operation is required
 * @wait: duration for ROC
 * @buf: buffer to transmit
 * @len: buffer length
 * @no_cck: don't use cck rates for this frame
 * @dont_wait_for_ack: tells the low level not to wait for an ack
 */
struct cfg80211_mgmt_tx_params {
	struct ieee80211_channel *chan;
	bool offchan;
	unsigned int wait;
	const u8 *buf;
	size_t len;
	bool no_cck;
	bool dont_wait_for_ack;
};
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 1, 0)
/**
 * ieee80211_chandef_to_operating_class - convert chandef to operation class
 *
 * @chandef: the chandef to convert
 * @op_class: a pointer to the resulting operating class
 *
 * Returns %true if the conversion was successful, %false otherwise.
 */
bool ieee80211_chandef_to_operating_class(struct cfg80211_chan_def *chandef,
					  u8 *op_class);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 75)
#include <linux/ceph/decode.h>
#endif

#endif /* _RWNX_COMPAT_H_ */
