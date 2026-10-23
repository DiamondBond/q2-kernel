/**
 ******************************************************************************
 *
 * @file rwnx_main.c
 *
 * @brief Entry point of the RWNX driver
 *
 * Copyright (C) RivieraWaves 2012-2020
 *
 ******************************************************************************
 */

#define WQ_LOG_DM DM_IEEE80211

#include <linux/version.h>
#include <linux/module.h>
#include <linux/inetdevice.h>
#include <net/cfg80211.h>
#include <net/ieee80211_radiotap.h>
#include <net/ip.h>
#include <linux/etherdevice.h>
#include <linux/wireless.h>

#include "rwnx_defs.h"
#include "rwnx_msg_tx.h"
#include "rwnx_tx.h"
#include "hal_desc.h"
#include "rwnx_debugfs.h"
#include "rwnx_radar.h"
#ifdef CONFIG_RWNX_BFMER
#include "rwnx_bfmer.h"
#endif //(CONFIG_RWNX_BFMER)
#include "rwnx_tdls.h"
#include "rwnx_events.h"
#include "rwnx_compat.h"
#include "wq_log.h"
#include "wq_ipc.h"
#include "hif_api.h"
#include "ieee80211_ht.h"
#include "wq_wifi_dbg.h"
#include <net/addrconf.h>
#include "wq_pktlog.h"
#include "wq_rx_defrag.h"
#include "rwnx_rx_ll.h"
#include "wlan_ioctl.h"
#include "rwnx_msg_rx.h"
#include "country.h"
#include "fw_api/non_wifi/hif/usb/api.h"
#include "wq_profiling.h"
#include "rwnx_main.h"
#include "ieee80211_extap.h"

/* nbw_type default: 0
    0 - disable
    1 - 10M
    2 - 5M
    3 - 2.5M
*/
int nbw_type = 0;
module_param(nbw_type, int, 0);
MODULE_PARM_DESC(nbw_type, "Narrow BandWidth type, default: 0");

char *reg_data_file = NULL;
module_param(reg_data_file, charp, 0);
MODULE_PARM_DESC(reg_data_file, "Bin file path");
bool gv_get_pwr_from_bin_flag = false;

/* The upper limit of the number of data packets that the netdev can process at poll function */
#define NAPI_RX_WEIGHT 64

/* enable monitor mode minimal interframe space by set aifs = 1, and cwmin = cwmax = 0 */
#define MONITOR_TX_MIN_IFS_ENABLE 0

#define RWNX_PRINT_CFM_ERR(req)                                                \
	WQ_DBG(DM_GENERIC, DL_ERR, "%s: Status Error(%d)\n", #req,             \
	       (&req##_cfm)->status)

#define RWNX_HT_CAPABILITIES                                                   \
	{                                                                      \
		.ht_supported   = true,                                     \
    .cap            = 0,                                        \
    .ampdu_factor   = IEEE80211_HT_MAX_AMPDU_64K,               \
    .ampdu_density  = IEEE80211_HT_MPDU_DENSITY_16,             \
    .mcs        = {                                             \
        .rx_mask = { 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0, },        \
        .rx_highest = cpu_to_le16(65),                          \
        .tx_params = IEEE80211_HT_MCS_TX_DEFINED,               \
    },  \
	}

#define RWNX_VHT_CAPABILITIES                                                   \
	{                                                                       \
		.vht_supported = false,                                         \
		.cap = (7                                                       \
			<< IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT), \
		.vht_mcs = {                                                    \
			.rx_mcs_map = cpu_to_le16(                              \
				IEEE80211_VHT_MCS_SUPPORT_0_9 << 0 |            \
				IEEE80211_VHT_MCS_NOT_SUPPORTED << 2 |          \
				IEEE80211_VHT_MCS_NOT_SUPPORTED << 4 |          \
				IEEE80211_VHT_MCS_NOT_SUPPORTED << 6 |          \
				IEEE80211_VHT_MCS_NOT_SUPPORTED << 8 |          \
				IEEE80211_VHT_MCS_NOT_SUPPORTED << 10 |         \
				IEEE80211_VHT_MCS_NOT_SUPPORTED << 12 |         \
				IEEE80211_VHT_MCS_NOT_SUPPORTED << 14),         \
			.tx_mcs_map = cpu_to_le16(                              \
				IEEE80211_VHT_MCS_SUPPORT_0_9 << 0 |            \
				IEEE80211_VHT_MCS_NOT_SUPPORTED << 2 |          \
				IEEE80211_VHT_MCS_NOT_SUPPORTED << 4 |          \
				IEEE80211_VHT_MCS_NOT_SUPPORTED << 6 |          \
				IEEE80211_VHT_MCS_NOT_SUPPORTED << 8 |          \
				IEEE80211_VHT_MCS_NOT_SUPPORTED << 10 |         \
				IEEE80211_VHT_MCS_NOT_SUPPORTED << 12 |         \
				IEEE80211_VHT_MCS_NOT_SUPPORTED << 14),         \
		}                                                               \
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
#define RWNX_HE_CAPABILITIES                                                   \
	{                                                                      \
		.has_he = false,                                            \
    .he_cap_elem = {                                            \
        .mac_cap_info[0] = 0,                                   \
        .mac_cap_info[1] = 0,                                   \
        .mac_cap_info[2] = 0,                                   \
        .mac_cap_info[3] = 0,                                   \
        .mac_cap_info[4] = 0,                                   \
        .mac_cap_info[5] = 0,                                   \
        .phy_cap_info[0] = 0,                                   \
        .phy_cap_info[1] = 0,                                   \
        .phy_cap_info[2] = 0,                                   \
        .phy_cap_info[3] = 0,                                   \
        .phy_cap_info[4] = 0,                                   \
        .phy_cap_info[5] = 0,                                   \
        .phy_cap_info[6] = 0,                                   \
        .phy_cap_info[7] = 0,                                   \
        .phy_cap_info[8] = 0,                                   \
        .phy_cap_info[9] = 0,                                   \
        .phy_cap_info[10] = 0,                                  \
    },                                                          \
    .he_mcs_nss_supp = {                                        \
        .rx_mcs_80 = cpu_to_le16(0xfffa),                       \
        .tx_mcs_80 = cpu_to_le16(0xfffa),                       \
        .rx_mcs_160 = cpu_to_le16(0xffff),                      \
        .tx_mcs_160 = cpu_to_le16(0xffff),                      \
        .rx_mcs_80p80 = cpu_to_le16(0xffff),                    \
        .tx_mcs_80p80 = cpu_to_le16(0xffff),                    \
    },                                                          \
    .ppe_thres = {0x00},  \
	}
#endif

#define RATE(_bitrate, _hw_rate, _flags)                                       \
	{                                                                      \
		.bitrate = (_bitrate), .flags = (_flags),                      \
		.hw_value = (_hw_rate),                                        \
	}

#define CHAN(_freq)                                                            \
	{                                                                      \
		.center_freq = (_freq), .max_power = 30, /* FIXME */           \
			.hw_value = (_freq),                                   \
	}

static struct ieee80211_rate rwnx_ratetable[] = {
	RATE(10, 0x00, 0),
	RATE(20, 0x01, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(55, 0x02, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(110, 0x03, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(60, 0x04, 0),
	RATE(90, 0x05, 0),
	RATE(120, 0x06, 0),
	RATE(180, 0x07, 0),
	RATE(240, 0x08, 0),
	RATE(360, 0x09, 0),
	RATE(480, 0x0A, 0),
	RATE(540, 0x0B, 0),
};

/* The channels indexes here are not used anymore */
static struct ieee80211_channel rwnx_2ghz_channels[] = {
	CHAN(2412),
	CHAN(2417),
	CHAN(2422),
	CHAN(2427),
	CHAN(2432),
	CHAN(2437),
	CHAN(2442),
	CHAN(2447),
	CHAN(2452),
	CHAN(2457),
	CHAN(2462),
	CHAN(2467),
	CHAN(2472),
	CHAN(2484),
	// Extra channels defined only to be used for PHY measures.
	// Enabled only if custregd and custchan parameters are set
	CHAN(2390),
	CHAN(2400),
	CHAN(2410),
	CHAN(2420),
	CHAN(2430),
	CHAN(2440),
	CHAN(2450),
	CHAN(2460),
	CHAN(2470),
	CHAN(2480),
	CHAN(2490),
	CHAN(2500),
	CHAN(2510),
};

static struct ieee80211_channel rwnx_5ghz_channels[] = {
	CHAN(5180), // 36 -   20MHz
	CHAN(5200), // 40 -   20MHz
	CHAN(5220), // 44 -   20MHz
	CHAN(5240), // 48 -   20MHz
	CHAN(5260), // 52 -   20MHz
	CHAN(5280), // 56 -   20MHz
	CHAN(5300), // 60 -   20MHz
	CHAN(5320), // 64 -   20MHz
	CHAN(5500), // 100 -  20MHz
	CHAN(5520), // 104 -  20MHz
	CHAN(5540), // 108 -  20MHz
	CHAN(5560), // 112 -  20MHz
	CHAN(5580), // 116 -  20MHz
	CHAN(5600), // 120 -  20MHz
	CHAN(5620), // 124 -  20MHz
	CHAN(5640), // 128 -  20MHz
	CHAN(5660), // 132 -  20MHz
	CHAN(5680), // 136 -  20MHz
	CHAN(5700), // 140 -  20MHz
	CHAN(5720), // 144 -  20MHz
	CHAN(5745), // 149 -  20MHz
	CHAN(5765), // 153 -  20MHz
	CHAN(5785), // 157 -  20MHz
	CHAN(5805), // 161 -  20MHz
	CHAN(5825), // 165 -  20MHz
	// Extra channels defined only to be used for PHY measures.
	// Enabled only if custregd and custchan parameters are set
	CHAN(5190),
	CHAN(5210),
	CHAN(5230),
	CHAN(5250),
	CHAN(5270),
	CHAN(5290),
	CHAN(5310),
	CHAN(5330),
	CHAN(5340),
	CHAN(5350),
	CHAN(5360),
	CHAN(5370),
	CHAN(5380),
	CHAN(5390),
	CHAN(5400),
	CHAN(5410),
	CHAN(5420),
	CHAN(5430),
	CHAN(5440),
	CHAN(5450),
	CHAN(5460),
	CHAN(5470),
	CHAN(5480),
	CHAN(5490),
	CHAN(5510),
	CHAN(5530),
	CHAN(5550),
	CHAN(5570),
	CHAN(5590),
	CHAN(5610),
	CHAN(5630),
	CHAN(5650),
	CHAN(5670),
	CHAN(5690),
	CHAN(5710),
	CHAN(5730),
	CHAN(5750),
	CHAN(5760),
	CHAN(5770),
	CHAN(5780),
	CHAN(5790),
	CHAN(5800),
	CHAN(5810),
	CHAN(5820),
	CHAN(5830),
	CHAN(5840),
	CHAN(5850),
	CHAN(5860),
	CHAN(5870),
	CHAN(5880),
	CHAN(5890),
	CHAN(5900),
	CHAN(5910),
	CHAN(5920),
	CHAN(5930),
	CHAN(5940),
	CHAN(5950),
	CHAN(5960),
	CHAN(5970),
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
static struct ieee80211_sband_iftype_data rwnx_he_capa = {
	.types_mask = BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_AP),
	.he_cap = RWNX_HE_CAPABILITIES,
};
#endif

static struct ieee80211_supported_band rwnx_band_2GHz = {
	.channels = rwnx_2ghz_channels,
	.n_channels = ARRAY_SIZE(rwnx_2ghz_channels) -
		      13, // -13 to exclude extra channels
	.bitrates = rwnx_ratetable,
	.n_bitrates = ARRAY_SIZE(rwnx_ratetable),
	.ht_cap = RWNX_HT_CAPABILITIES,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
	.iftype_data = &rwnx_he_capa,
	.n_iftype_data = 1,
#endif
};

static struct ieee80211_supported_band rwnx_band_5GHz = {
	.channels = rwnx_5ghz_channels,
	.n_channels = ARRAY_SIZE(rwnx_5ghz_channels) -
		      59, // -59 to exclude extra channels
	.bitrates = &rwnx_ratetable[4],
	.n_bitrates = ARRAY_SIZE(rwnx_ratetable) - 4,
	.ht_cap = RWNX_HT_CAPABILITIES,
	.vht_cap = RWNX_VHT_CAPABILITIES,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
	.iftype_data = &rwnx_he_capa,
	.n_iftype_data = 1,
#endif
};

static struct ieee80211_iface_limit rwnx_limits[] = {
	{ .max = 4,
	  .types = BIT(NL80211_IFTYPE_AP) | BIT(NL80211_IFTYPE_STATION) |
		   BIT(NL80211_IFTYPE_P2P_GO) |
		   BIT(NL80211_IFTYPE_P2P_CLIENT) },
};

static struct ieee80211_iface_limit rwnx_limits_dfs[] = {
	{ .max = 4, .types = BIT(NL80211_IFTYPE_AP) }
};

static const struct ieee80211_iface_combination rwnx_combinations[] = {
	{
		.limits = rwnx_limits,
		.n_limits = ARRAY_SIZE(rwnx_limits),
		.num_different_channels = 2,
		.max_interfaces = 4,
	},
	/* Keep this combination as the last one */
	{
		.limits = rwnx_limits_dfs,
		.n_limits = ARRAY_SIZE(rwnx_limits_dfs),
		.num_different_channels = 1,
		.max_interfaces = 4,
		.radar_detect_widths = (BIT(NL80211_CHAN_WIDTH_20_NOHT) |
					BIT(NL80211_CHAN_WIDTH_20) |
					BIT(NL80211_CHAN_WIDTH_40) |
					BIT(NL80211_CHAN_WIDTH_80)),
	}
};

/* There isn't a lot of sense in it, but you can transmit anything you like */
static struct ieee80211_txrx_stypes
rwnx_default_mgmt_stypes[NUM_NL80211_IFTYPES] = {
    [NL80211_IFTYPE_STATION] = {
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ACTION >> 4) |
               BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
               BIT(IEEE80211_STYPE_AUTH >> 4)),
    },
    [NL80211_IFTYPE_AP] = {
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
               BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
               BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
               BIT(IEEE80211_STYPE_DISASSOC >> 4) |
               BIT(IEEE80211_STYPE_AUTH >> 4) |
               BIT(IEEE80211_STYPE_DEAUTH >> 4) |
               BIT(IEEE80211_STYPE_ACTION >> 4)),
    },
    [NL80211_IFTYPE_AP_VLAN] = {
        /* copy AP */
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
               BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
               BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
               BIT(IEEE80211_STYPE_DISASSOC >> 4) |
               BIT(IEEE80211_STYPE_AUTH >> 4) |
               BIT(IEEE80211_STYPE_DEAUTH >> 4) |
               BIT(IEEE80211_STYPE_ACTION >> 4)),
    },
    [NL80211_IFTYPE_P2P_CLIENT] = {
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ACTION >> 4) |
               BIT(IEEE80211_STYPE_PROBE_REQ >> 4)),
    },
    [NL80211_IFTYPE_P2P_GO] = {
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
               BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
               BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
               BIT(IEEE80211_STYPE_DISASSOC >> 4) |
               BIT(IEEE80211_STYPE_AUTH >> 4) |
               BIT(IEEE80211_STYPE_DEAUTH >> 4) |
               BIT(IEEE80211_STYPE_ACTION >> 4)),
    },
    [NL80211_IFTYPE_P2P_DEVICE] = {
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ACTION >> 4) |
               BIT(IEEE80211_STYPE_PROBE_REQ >> 4)),
    },
    [NL80211_IFTYPE_MESH_POINT] = {
        .tx = 0xffff,
        .rx = (BIT(IEEE80211_STYPE_ACTION >> 4) |
               BIT(IEEE80211_STYPE_AUTH >> 4) |
               BIT(IEEE80211_STYPE_DEAUTH >> 4)),
    },
};

static u32 cipher_suites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
	0, // reserved entries to enable AES-CMAC, GCMP-128/256, CCMP-256, SMS4
	0,
	0,
	0,
	0,
};

#define NB_RESERVED_CIPHER 5;

static const int rwnx_ac2hwq[1][NL80211_NUM_ACS] = {
	{ [NL80211_TXQ_Q_VO] = RWNX_HWQ_VO,
	  [NL80211_TXQ_Q_VI] = RWNX_HWQ_VI,
	  [NL80211_TXQ_Q_BE] = RWNX_HWQ_BE,
	  [NL80211_TXQ_Q_BK] = RWNX_HWQ_BK }
};

const int rwnx_tid2hwq[IEEE80211_NUM_TIDS] = {
	RWNX_HWQ_BE,
	RWNX_HWQ_BK,
	RWNX_HWQ_BK,
	RWNX_HWQ_BE,
	RWNX_HWQ_VI,
	RWNX_HWQ_VI,
	RWNX_HWQ_VO,
	RWNX_HWQ_VO,
	/* TID_8 is used for management frames */
	RWNX_HWQ_VO,
	/* At the moment, all others TID are mapped to BE */
	RWNX_HWQ_BE,
	RWNX_HWQ_BE,
	RWNX_HWQ_BE,
	RWNX_HWQ_BE,
	RWNX_HWQ_BE,
	RWNX_HWQ_BE,
	RWNX_HWQ_BE,
};

static const int rwnx_hwq2uapsd[NL80211_NUM_ACS] = {
	[RWNX_HWQ_VO] = IEEE80211_WMM_IE_STA_QOSINFO_AC_VO,
	[RWNX_HWQ_VI] = IEEE80211_WMM_IE_STA_QOSINFO_AC_VI,
	[RWNX_HWQ_BE] = IEEE80211_WMM_IE_STA_QOSINFO_AC_BE,
	[RWNX_HWQ_BK] = IEEE80211_WMM_IE_STA_QOSINFO_AC_BK,
};

#define RWNX_INFOELT_ID_OFT 0
#define RWNX_INFOELT_LEN_OFT 1
#define RWNX_INFOELT_INFO_OFT 2

#define RWNX_80211_SCAN_TIMEOUT_MS (10000)

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 17, 0)
#define dev_addr_set(dev, addr) memcpy((dev)->dev_addr, addr, ETH_ALEN)
#endif

extern int rwnx_send_abort_scan_req(struct rwnx_hw *rwnx_hw,
				    struct rwnx_vif *rwnx_vif);

extern int rwnx_send_ini_conf_req(struct rwnx_hw *rwnx_hw);

static int rwnx_fill_station_info(struct rwnx_sta *sta, struct rwnx_vif *vif,
				  struct station_info *sinfo);

extern int rwnx_send_chan_pwr_info_req(struct rwnx_hw *rwnx_hw,
				       struct rwnx_vif *rwnx_vif, u8 *pwr,
				       u8 band, u32 freq);
extern void rwnx_store_chan_pwr_tab(struct rwnx_vif *vif, u8 band, u32 freq,
				    u8 *pwr_tab);
extern void store_supp_chan_pwr(struct rwnx_hw *rwnx_hw,
				struct supp_chan_pwr_str *supp_pwr);
struct supp_chan_pwr_str wq_supp_pwr[MAC_DOMAINCHANNEL_24G_MAX +
				     MAC_DOMAINCHANNEL_5G_MAX] = { { 0 } };

/*********************************************************************
 * helper
 *********************************************************************/
struct rwnx_sta *rwnx_get_sta(struct rwnx_hw *rwnx_hw, const u8 *mac_addr)
{
	int i;

	for (i = 0; i < NX_REMOTE_STA_MAX; i++) {
		struct rwnx_sta *sta = &rwnx_hw->sta_table[i];
		if (sta->valid && (memcmp(mac_addr, &sta->mac_addr, 6) == 0))
			return sta;
	}

	return NULL;
}

void rwnx_enable_wapi(struct rwnx_hw *rwnx_hw)
{
	cipher_suites[rwnx_hw->wiphy->n_cipher_suites] = WLAN_CIPHER_SUITE_SMS4;
	rwnx_hw->wiphy->n_cipher_suites++;
	rwnx_hw->wiphy->flags |= WIPHY_FLAG_CONTROL_PORT_PROTOCOL;
}

void rwnx_enable_mfp(struct rwnx_hw *rwnx_hw)
{
	cipher_suites[rwnx_hw->wiphy->n_cipher_suites] =
		WLAN_CIPHER_SUITE_AES_CMAC;
	rwnx_hw->wiphy->n_cipher_suites++;
}

void rwnx_enable_gcmp(struct rwnx_hw *rwnx_hw)
{
	// Assume that HW supports CCMP-256 if it supports GCMP
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
	cipher_suites[rwnx_hw->wiphy->n_cipher_suites++] =
		WLAN_CIPHER_SUITE_CCMP_256;
#endif
	cipher_suites[rwnx_hw->wiphy->n_cipher_suites++] =
		WLAN_CIPHER_SUITE_GCMP;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
	cipher_suites[rwnx_hw->wiphy->n_cipher_suites++] =
		WLAN_CIPHER_SUITE_GCMP_256;
#endif
}

#define WLAN_HT_CAPA_LEN 28
#define WLAN_VHT_CAPA_LEN 14

#define WLAN_CAPA_LEN_OFT 1
#define WLAN_CAPA_INFO_OFT 2
#define WLAN_VHT_MCS_MAP_NONE 0x03
#define WLAN_HE_MCS_MAP_NONE 0x03

#define WLAN_HTCAPA_TX_STBC BIT(7)
#define WLAN_VHTCAPA_TX_STBC BIT(7)

u8 *rwnx_mac_ie_find(u8 *addr, uint16_t buflen, uint8_t ie_id, u8 *len)
{
	u8 *start = addr;
	u8 *end = addr + buflen;

	// loop as long as we do not go beyond the frame size
	while ((start + RWNX_INFOELT_LEN_OFT) < end) {
		u8 ie_len = start[1] + RWNX_INFOELT_INFO_OFT;
		u8 *ie_end = start + ie_len;

		// Check if the current IE is the one we look for
		if (ie_id == start[0]) {
			// Check if the IE length complies with the remaining length in the buffer
			if (ie_end > end) {
				WQ_DBG(DM_GENERIC, DL_WRN,
				       "rwnx_ie_find, exit\n");
				return NULL;
			}

			*len = ie_len;
			// The IE is valid
			return start;
		}
		// move on to the next IE
		start = ie_end;
	}

	return NULL;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
u8 *rwnx_ext_mac_ie_find(u8 *addr, uint16_t buflen, uint8_t ext_ie_id,
			 uint8_t *len)
{
	u8 *start = addr;
	u8 *end = addr + buflen;

	// loop as long as we do not go beyond the frame size
	while (start < end) {
		// First of all we need to find the extension element ID
		start = rwnx_mac_ie_find(addr, buflen, WLAN_EID_EXTENSION, len);

		// Check if we found the extension ID, and that we have at least one byte
		// available after the length for the extension field
		if ((start == 0) || ((start + 3) > end))
			return 0;

		// Check if the extension field is the one we look for
		WQ_DBG(DM_GENERIC, DL_WRN, "ext_ie_id=%d, start[2]=%d\n",
		       ext_ie_id, start[2]);
		if (ext_ie_id == start[RWNX_INFOELT_INFO_OFT]) {
			// the extension field matches, return the pointer to this IE
			return start;
		}
		// move on to the next extended IE
		start += *len;
		buflen -= *len;
	}

	return 0;
}
#endif

void rwnx_eid_update_nss_param(struct rwnx_hw *rwnx_hw, u8 *buf, u16 buf_len)
{
	u8 *ht_addr = NULL;
	u8 *vht_addr;
	u8 htcap_len = 0;
	u8 vhtcap_len = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
	u8 *he_addr;
	u8 hecap_len = 0;
#endif

	if (rwnx_hw->mod_params.ht_on) {
		ht_addr = rwnx_mac_ie_find(buf, buf_len, WLAN_EID_HT_CAPABILITY,
					   &htcap_len);

		if (ht_addr && htcap_len == WLAN_HT_CAPA_LEN) {
			struct ieee80211_ht_cap *ht_cap =
				(struct ieee80211_ht_cap *)(ht_addr +
							    WLAN_CAPA_INFO_OFT);
			ht_cap->cap_info &= ~WLAN_HTCAPA_TX_STBC;
			ht_cap->mcs.rx_mask[1] = 0;
		}
	}

	if (rwnx_hw->mod_params.vht_on) {
		vht_addr = rwnx_mac_ie_find(
			buf, buf_len, WLAN_EID_VHT_CAPABILITY, &vhtcap_len);
		WQ_DBG(DM_GENERIC, DL_WRN, "vhtcap_len=%d", vhtcap_len);
		if (vht_addr && vhtcap_len == WLAN_VHT_CAPA_LEN) {
			struct ieee80211_vht_cap *vht_cap =
				(struct ieee80211_vht_cap *)(vht_addr +
							     WLAN_CAPA_INFO_OFT);
			vht_cap->vht_cap_info &= ~WLAN_VHTCAPA_TX_STBC;
			// 2-bit for each nss
			vht_cap->supp_mcs.rx_mcs_map |=
				(WLAN_VHT_MCS_MAP_NONE << 2);
			vht_cap->supp_mcs.tx_mcs_map |=
				(WLAN_VHT_MCS_MAP_NONE << 2);
		}
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
	if (rwnx_hw->mod_params.he_on) {
		struct ieee80211_he_cap_elem *he_cap_elem = NULL;
		struct ieee80211_he_mcs_nss_supp *he_mcs_nss_supp = NULL;

		he_addr = rwnx_ext_mac_ie_find(
			buf, buf_len, WLAN_EID_EXT_HE_CAPABILITY, &hecap_len);
		WQ_DBG(DM_GENERIC, DL_WRN, "hecap_len=%d", hecap_len);
		if (he_addr && he_addr[1] >= sizeof(he_cap_elem) + 1) {
			he_cap_elem =
				(struct ieee80211_he_cap_elem *)(he_addr + 3);
			he_mcs_nss_supp =
				(struct ieee80211_he_mcs_nss_supp
					 *)(he_addr + 3 + MAC_HE_MAC_CAPA_LEN +
					    MAC_HE_PHY_CAPA_LEN);

			he_cap_elem->phy_cap_info[3] &=
				~IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_2;
			he_mcs_nss_supp->rx_mcs_80 |=
				(WLAN_HE_MCS_MAP_NONE << 2);
			he_mcs_nss_supp->tx_mcs_80 |=
				(WLAN_HE_MCS_MAP_NONE << 2);
		}
	}
#endif
}

u8 *rwnx_build_bcn(struct rwnx_bcn *bcn, struct cfg80211_beacon_data *new)
{
	u8 *buf, *pos;

	if (new->head) {
		u8 *head = kmalloc(new->head_len, GFP_KERNEL);

		if (!head)
			return NULL;

		if (bcn->head)
			kfree(bcn->head);

		bcn->head = head;
		bcn->head_len = new->head_len;
		memcpy(bcn->head, new->head, new->head_len);
	}
	if (new->tail) {
		u8 *tail = kmalloc(new->tail_len, GFP_KERNEL);

		if (!tail)
			return NULL;

		if (bcn->tail)
			kfree(bcn->tail);

		bcn->tail = tail;
		bcn->tail_len = new->tail_len;
		memcpy(bcn->tail, new->tail, new->tail_len);
	}

	if (!bcn->head)
		return NULL;

	bcn->tim_len = 6;
	bcn->len = bcn->head_len + bcn->tail_len + bcn->ies_len + bcn->tim_len;

	buf = kmalloc(bcn->len, GFP_KERNEL);
	if (!buf)
		return NULL;

	// Build the beacon buffer
	pos = buf;
	memcpy(pos, bcn->head, bcn->head_len);
	pos += bcn->head_len;
	*pos++ = WLAN_EID_TIM;
	*pos++ = 4;
	*pos++ = 0;
	*pos++ = bcn->dtim;
	*pos++ = 0;
	*pos++ = 0;
	if (bcn->tail) {
		memcpy(pos, bcn->tail, bcn->tail_len);
		pos += bcn->tail_len;
	}
	if (bcn->ies) {
		memcpy(pos, bcn->ies, bcn->ies_len);
	}

	return buf;
}

static void rwnx_del_bcn(struct rwnx_bcn *bcn)
{
	if (bcn->head) {
		kfree(bcn->head);
		bcn->head = NULL;
	}
	bcn->head_len = 0;

	if (bcn->tail) {
		kfree(bcn->tail);
		bcn->tail = NULL;
	}
	bcn->tail_len = 0;

	if (bcn->ies) {
		kfree(bcn->ies);
		bcn->ies = NULL;
	}
	bcn->ies_len = 0;
	bcn->tim_len = 0;
	bcn->dtim = 0;
	bcn->len = 0;
}

u8 *rwnx_ie_find(u8 *addr, uint16_t buflen, uint8_t ie_id, u8 *len,
		 uint16_t *addr_len)
{
	u8 *start;
	u8 *end = addr + buflen;
	uint16_t former_len;

	// loop as long as we do not go beyond the frame size
	if (buflen > 36) {
		start = addr + 36;
		former_len = 36;
	} else
		start = addr;

	while ((start + RWNX_INFOELT_LEN_OFT) < end) {
		u8 ie_len = start[1] + RWNX_INFOELT_INFO_OFT;
		u8 *ie_end = start + ie_len;

		// Check if the current IE is the one we look for
		if (ie_id == start[0]) {
			// Check if the IE length complies with the remaining length in the buffer
			if (ie_end > end) {
				WQ_DBG(DM_GENERIC, DL_WRN,
				       "rwnx_ie_find, exit\n");
				return NULL;
			}

			*len = ie_len;
			*addr_len = former_len;
			// The IE is valid
			return start;
		}
		// move on to the next IE
		start = ie_end;
		former_len += ie_len;
	}

	return NULL;
}

u8 *rwnx_ie_ds_param_find(u8 *buffer, uint16_t buflen, u8 *ie_len,
			  uint16_t *new_len)
{
	u8 len;
	u8 *addr;

	addr = rwnx_ie_find(buffer, buflen, WLAN_EID_DS_PARAMS, &len, new_len);

	if ((addr == NULL) || (len != 3))
		return NULL;

	*ie_len = len;

	return addr;
}

u16 rwnx_freq_to_channel(u32 center_freq, u8 band)
{
	u16 channel = 0;

	if ((band == NL80211_BAND_2GHZ) && (center_freq >= 2412) &&
	    (center_freq <= 2484)) {
		if (center_freq == 2484)
			channel = 14;
		else
			channel = (center_freq - 2407) / 5;
	} else if ((band == NL80211_BAND_5GHZ) && (center_freq >= 5005) &&
		   (center_freq <= 5825)) {
		channel = (center_freq - 5000) / 5;
	}

	return channel;
}

u8 *rwnx_bcn_chan_change(struct rwnx_bcn *bcn, u32 center_freq, u8 band,
			 bool need_csa)
{
	u8 *buf, *pos;
	u8 *head_tmp;
	u8 ds_ie_len;
	u16 before_ds_len;
	u8 csa_len = 0;
	u8 cur_chan;
	u16 new_chan;

	if (!bcn->head) {
		return NULL;
	}

	new_chan = rwnx_freq_to_channel(center_freq, band);
	if (bcn->head) {
		head_tmp = rwnx_ie_ds_param_find(bcn->head, bcn->head_len,
						 &ds_ie_len, &before_ds_len);
		if (head_tmp && head_tmp[1] > 0) {
			cur_chan = head_tmp[2];
		}

		if ((before_ds_len + ds_ie_len) != bcn->head_len) {
			WQ_DBG(DM_GENERIC, DL_ERR, "parse bcn wrong\n");
			return NULL;
		}
	}

	bcn->tim_len = 6;
	if (need_csa)
		csa_len = 5;
	bcn->len = bcn->head_len + bcn->tail_len + bcn->ies_len + bcn->tim_len +
		   csa_len;

	buf = kmalloc(bcn->len, GFP_KERNEL);
	if (!buf) {
		return NULL;
	}

	// Build the beacon buffer
	pos = buf;
	memcpy(pos, bcn->head, before_ds_len);
	pos += before_ds_len;
	// Update DS Param IE
	*pos++ = WLAN_EID_DS_PARAMS; /* EID */
	*pos++ = 1; /* IE length */
	*pos++ = (u8)new_chan;

	if (bcn->head_len - before_ds_len > ds_ie_len)
		memcpy(pos, head_tmp + ds_ie_len,
		       bcn->head_len - before_ds_len - ds_ie_len);

	// Update TIM
	*pos++ = WLAN_EID_TIM;
	*pos++ = 4;
	*pos++ = 0;
	*pos++ = bcn->dtim;
	*pos++ = 0;
	*pos++ = 0;
	if (bcn->tail) {
		memcpy(pos, bcn->tail, bcn->tail_len);
		pos += bcn->tail_len;
	}

	if (need_csa) {
		// Update CSA IE
		*pos++ = WLAN_EID_CHANNEL_SWITCH; /* EID */
		*pos++ = 3; /* IE length */
		*pos++ = 1; /* CSA mode */
		*pos++ = new_chan; /* channel */
		*pos++ = 2; /* count */
		WQ_DBG(DM_GENERIC, DL_ERR, "update csa ie\n");
	}

	if (bcn->ies) {
		memcpy(pos, bcn->ies, bcn->ies_len);
	}

	return buf;
}

u8 *rwnx_bcn_nss_update(struct rwnx_hw *rwnx_hw, struct rwnx_bcn *bcn)
{
	u8 *buf, *pos;

	if (!bcn->head) {
		return NULL;
	}

	if (bcn->tail) {
		WQ_DBG(DM_GENERIC, DL_ERR, "update nss in bcn cap\n");
		rwnx_eid_update_nss_param(rwnx_hw, (u8 *)bcn->tail,
					  bcn->tail_len);
	}

	bcn->tim_len = 6;
	bcn->len = bcn->head_len + bcn->tail_len + bcn->ies_len + bcn->tim_len;

	buf = kmalloc(bcn->len, GFP_KERNEL);
	if (!buf)
		return NULL;

	// Build the beacon buffer
	pos = buf;
	memcpy(pos, bcn->head, bcn->head_len);
	pos += bcn->head_len;
	*pos++ = WLAN_EID_TIM;
	*pos++ = 4;
	*pos++ = 0;
	*pos++ = bcn->dtim;
	*pos++ = 0;
	*pos++ = 0;
	if (bcn->tail) {
		memcpy(pos, bcn->tail, bcn->tail_len);
		pos += bcn->tail_len;
	}
	if (bcn->ies) {
		memcpy(pos, bcn->ies, bcn->ies_len);
	}

	return buf;
}

/**
 *
 * @rwnx_send_ch_switch: Change bcn element and send bcn change to FW.
*/
int rwnx_send_ch_switch(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
			struct cfg80211_chan_def chandef)
{
	u8 *buf = NULL;
	struct rwnx_bcn *bcn;
	int i, error = 0;
	u16 csa_oft[BCN_MAX_CSA_CPT];

	/* Build the new beacon with CSA IE */
	bcn = &vif->ap.bcn;
	buf = rwnx_bcn_chan_change(bcn, chandef.chan->center_freq,
				   chandef.chan->band, true);
	if (!buf)
		return -ENOMEM;

	memset(csa_oft, 0, sizeof(csa_oft));
	for (i = 0; i < BCN_MAX_CSA_CPT; i++) {
		csa_oft[i] = 2;
	}
	/* Send new Beacon. FW will extract channel and count from the beacon */
	error = rwnx_send_bcn_change(rwnx_hw, vif->vif_index, buf, bcn->len,
				     bcn->head_len, bcn->tim_len, csa_oft);
	if (!error) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
		cfg80211_ch_switch_notify(vif->ndev, &vif->ap.chandef, 0, 0);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
		cfg80211_ch_switch_notify(vif->ndev, &vif->ap.chandef, 0);
#else
		cfg80211_ch_switch_notify(vif->ndev, &vif->ap.chandef);
#endif
	}

	if (buf) {
		kfree(buf);
		buf = NULL;
	}

	return error;
}

/**
 * Link channel ctxt to a vif and thus increments count for this context.
 */
void rwnx_chanctx_link(struct rwnx_vif *vif, u8 ch_idx,
		       struct cfg80211_chan_def *chandef)
{
	struct rwnx_chanctx *ctxt;

	// ignore ch_idx report by FW, always use drv_vif_index as the index
	if (vif->drv_vif_index >= NX_CHAN_CTXT_CNT) {
		WARN(1, "Invalid channel ctxt id %d", vif->drv_vif_index);
		return;
	}

	ch_idx = vif->drv_vif_index;

	vif->ch_index = ch_idx;
	ctxt = &vif->rwnx_hw->chanctx_table[ch_idx];
	ctxt->count++;

	// For now chandef is NULL for STATION interface
	if (chandef) {
		if (!ctxt->chan_def.chan)
			ctxt->chan_def = *chandef;
		else {
			// TODO. check that chandef is the same as the one already
			// set for this ctxt
		}
	}
}

/**
 * Unlink channel ctxt from a vif and thus decrements count for this context
 */
void rwnx_chanctx_unlink(struct rwnx_vif *vif)
{
	struct rwnx_chanctx *ctxt;

	if (vif->ch_index == RWNX_CH_NOT_SET)
		return;

	ctxt = &vif->rwnx_hw->chanctx_table[vif->ch_index];

	if (ctxt->count == 0) {
		WARN(1, "Chan ctxt ref count is already 0");
	} else {
		ctxt->count--;
	}

	if (ctxt->count == 0) {
		if (vif->ch_index == vif->rwnx_hw->cur_chanctx) {
			/* If current chan ctxt is no longer linked to a vif
               disable radar detection (no need to check if it was activated) */
			rwnx_radar_detection_enable(&vif->rwnx_hw->radar,
						    RWNX_RADAR_DETECT_DISABLE,
						    RWNX_RADAR_RIU);
		}
		/* set chan to null, so that if this ctxt is relinked to a vif that
           don't have channel information, don't use wrong information */
		ctxt->chan_def.chan = NULL;
	}
	vif->ch_index = RWNX_CH_NOT_SET;
}

int rwnx_chanctx_valid(struct rwnx_hw *rwnx_hw, u8 ch_idx)
{
	if (ch_idx >= NX_CHAN_CTXT_CNT ||
	    rwnx_hw->chanctx_table[ch_idx].chan_def.chan == NULL) {
		return 0;
	}

	return 1;
}

static void rwnx_del_csa(struct rwnx_vif *vif)
{
	struct rwnx_csa *csa = vif->ap.csa;

	if (!csa)
		return;

	rwnx_del_bcn(&csa->bcn);
	kfree(csa);
	vif->ap.csa = NULL;
}

static void rwnx_del_chan(struct rwnx_vif *vif)
{
	struct ieee80211_channel *chan = vif->ap.chandef.chan;

	if (!chan)
		return;

	kfree(chan);
	vif->ap.chandef.chan = NULL;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
static void rwnx_csa_finish(struct work_struct *ws)
{
	struct rwnx_csa *csa = container_of(ws, struct rwnx_csa, work);
	struct rwnx_vif *vif = csa->vif;
	struct rwnx_hw *rwnx_hw = vif->rwnx_hw;
	int error = csa->status;

	if (!error)
		error = rwnx_send_bcn_change(rwnx_hw, vif->vif_index,
					     csa->bcn_buf, csa->bcn.len,
					     csa->bcn.head_len,
					     csa->bcn.tim_len, NULL);

	if (error) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
		cfg80211_stop_iface(rwnx_hw->wiphy, &vif->wdev, GFP_KERNEL);
#endif
	} else {
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
		mutex_lock(&vif->wdev.mtx);
		__acquire(&vif->wdev.mtx);
#endif
		spin_lock_bh(&rwnx_hw->cb_lock);
		rwnx_chanctx_unlink(vif);
		rwnx_chanctx_link(vif, csa->ch_idx, &csa->chandef);
		if (rwnx_hw->cur_chanctx == csa->ch_idx) {
			rwnx_radar_detection_enable_on_cur_channel(rwnx_hw);
			rwnx_txq_vif_start(vif, RWNX_TXQ_STOP_CHAN, rwnx_hw);
		} else
			rwnx_txq_vif_stop(vif, RWNX_TXQ_STOP_CHAN, rwnx_hw);
		spin_unlock_bh(&rwnx_hw->cb_lock);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
		cfg80211_ch_switch_notify(vif->ndev, &csa->chandef, 0, 0);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
		cfg80211_ch_switch_notify(vif->ndev, &csa->chandef, 0);
#else
		cfg80211_ch_switch_notify(vif->ndev, &csa->chandef);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 8, 0)
		mutex_unlock(&vif->wdev.mtx);
		__release(&vif->wdev.mtx);
#endif
	}
	rwnx_del_csa(vif);
}
#endif

/**
 * rwnx_external_auth_enable - Enable external authentication on a vif
 *
 * @vif: VIF on which external authentication must be enabled
 *
 * External authentication requires to start TXQ for unknown STA in
 * order to send auth frame pusehd by user space.
 * Note: It is assumed that fw is on the correct channel.
 */
void rwnx_external_auth_enable(struct rwnx_vif *vif)
{
	vif->sta.flags |= RWNX_STA_EXT_AUTH;
	rwnx_txq_unk_vif_init(vif);
	rwnx_txq_start(rwnx_txq_vif_get(vif, NX_UNK_TXQ_TYPE), 0);
}

/**
 * rwnx_external_auth_disable - Disable external authentication on a vif
 *
 * @vif: VIF on which external authentication must be disabled
 */
void rwnx_external_auth_disable(struct rwnx_vif *vif)
{
	if (!(vif->sta.flags & RWNX_STA_EXT_AUTH)) {
		return;
	}
	vif->sta.flags &= ~RWNX_STA_EXT_AUTH;
	rwnx_txq_unk_vif_deinit(vif);
}

/**
 * rwnx_update_mesh_power_mode -
 *
 * @vif: mesh VIF  for which power mode is updated
 *
 * Does nothing if vif is not a mesh point interface.
 * Since firmware doesn't support one power save mode per link select the
 * most "active" power mode among all mesh links.
 * Indeed as soon as we have to be active on one link we might as well be
 * active on all links.
 *
 * If there is no link then the power mode for next peer is used;
 */
void rwnx_update_mesh_power_mode(struct rwnx_vif *vif)
{
	enum nl80211_mesh_power_mode mesh_pm;
	struct rwnx_sta *sta;
	struct mesh_config mesh_conf;
	struct mesh_update_cfm cfm;
	u32 mask;

	if (RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_MESH_POINT)
		return;

	if (list_empty(&vif->ap.sta_list)) {
		mesh_pm = vif->ap.next_mesh_pm;
	} else {
		mesh_pm = NL80211_MESH_POWER_DEEP_SLEEP;
		list_for_each_entry (sta, &vif->ap.sta_list, list) {
			if (sta->valid && (sta->mesh_pm < mesh_pm)) {
				mesh_pm = sta->mesh_pm;
			}
		}
	}

	if (mesh_pm == vif->ap.mesh_pm)
		return;

	mask = BIT(NL80211_MESHCONF_POWER_MODE - 1);
	mesh_conf.power_mode = mesh_pm;
	if (rwnx_send_mesh_update_req(vif->rwnx_hw, vif, mask, &mesh_conf,
				      &cfm) ||
	    cfm.status)
		return;

	vif->ap.mesh_pm = mesh_pm;
}

/**
 * rwnx_save_assoc_ie_for_ft - Save association request elements if Fast
 * Transition has been configured.
 *
 * @vif: VIF that just connected
 * @sme: Connection info
 */
void rwnx_save_assoc_info_for_ft(struct rwnx_vif *vif,
				 struct cfg80211_connect_params *sme)
{
	int ies_len = sme->ie_len + sme->ssid_len + 2;
	u8 *pos;

	if (!vif->sta.ft_assoc_ies) {
		if (!cfg80211_find_ie(WLAN_EID_MOBILITY_DOMAIN, sme->ie,
				      sme->ie_len))
			return;

		vif->sta.ft_assoc_ies_len = ies_len;
		vif->sta.ft_assoc_ies = kmalloc(ies_len, GFP_KERNEL);
	} else if (vif->sta.ft_assoc_ies_len < ies_len) {
		kfree(vif->sta.ft_assoc_ies);
		vif->sta.ft_assoc_ies = kmalloc(ies_len, GFP_KERNEL);
	}

	if (!vif->sta.ft_assoc_ies)
		return;

	// Also save SSID (as an element) in the buffer
	pos = vif->sta.ft_assoc_ies;
	*pos++ = WLAN_EID_SSID;
	*pos++ = sme->ssid_len;
	memcpy(pos, sme->ssid, sme->ssid_len);
	pos += sme->ssid_len;
	memcpy(pos, sme->ie, sme->ie_len);
	vif->sta.ft_assoc_ies_len = ies_len;
}

/**
 * rwnx_rsne_to_connect_params - Initialise cfg80211_connect_params from
 * RSN element.
 *
 * @rsne: RSN element
 * @sme: Structure cfg80211_connect_params to initialize
 *
 * The goal is only to initialize enough for rwnx_send_sm_connect_req
 */
int rwnx_rsne_to_connect_params(const struct element *rsne,
				struct cfg80211_connect_params *sme)
{
	int len = rsne->datalen;
	int clen;
	const u8 *pos = rsne->data;

	if (len < 8)
		return 1;

	sme->crypto.control_port_no_encrypt = false;
	sme->crypto.control_port = true;
	sme->crypto.control_port_ethertype = cpu_to_be16(ETH_P_PAE);

	pos += 2;
	sme->crypto.cipher_group = ntohl(*((u32 *)pos));
	pos += 4;
	clen = le16_to_cpu(*((u16 *)pos)) * 4;
	pos += 2;
	len -= 8;
	if (len < clen + 2)
		return 1;
	// only need one cipher suite
	sme->crypto.n_ciphers_pairwise = 1;
	sme->crypto.ciphers_pairwise[0] = ntohl(*((u32 *)pos));
	pos += clen;
	len -= clen;

	// no need for AKM
	clen = le16_to_cpu(*((u16 *)pos)) * 4;
	pos += 2;
	len -= 2;
	if (len < clen)
		return 1;
	pos += clen;
	len -= clen;

	if (len < 4)
		return 0;

	pos += 2;
	clen = le16_to_cpu(*((u16 *)pos)) * 16;
	len -= 4;
	if (len > clen)
		sme->mfp = NL80211_MFP_REQUIRED;

	return 0;
}

static struct rwnx_vif *rwnx_get_vif_by_ndev(struct rwnx_hw *rwnx_hw,
					     struct net_device *ndev)
{
	struct rwnx_vif *vif = NULL;

	spin_lock_bh(&rwnx_hw->cb_lock);
	list_for_each_entry (vif, &rwnx_hw->vifs, list) {
		if (vif && vif->ndev == ndev) {
			spin_unlock_bh(&rwnx_hw->cb_lock);
			WQ_DBG(DM_GENERIC, DL_INF,
			       "IP address of %s is changed.\n", ndev->name);
			return vif;
		}
	}
	spin_unlock_bh(&rwnx_hw->cb_lock);

	WQ_DBG(DM_GENERIC, DL_VRB, "can not found the dev: %s\n", ndev->name);
	return NULL;
}

int fib_netdev_event(struct notifier_block *nb, unsigned long action,
		     void *data)
{
	/*process the ip change*/
	struct rwnx_hw *rwnx_hw =
		container_of(nb, struct rwnx_hw, fib_netdev_notifier);
	struct in_ifaddr *ifa = data;
	struct net_device *ndev = ifa->ifa_dev->dev;
	struct rwnx_vif *vif;
	u8 ip_address[IPV4_IP_LEN];
	int error;

	if (!rwnx_hw || !(vif = rwnx_get_vif_by_ndev(rwnx_hw, ndev)))
		return NOTIFY_DONE;

	WQ_DBG(DM_GENERIC, DL_WRN, "comm: %s, pid: %d, action: %ld\n",
	       current->comm, current->pid, action);

	switch (action) {
	case NETDEV_UP:
		if (ndev) {
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "[auto]dev[%s] is up, ip_address:%pI4\n",
			       ndev->name, &(ifa->ifa_address));
			if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_STATION ||
			    RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_CLIENT) {
				rwnx_hw->connect_req_ts = 0;
			}

			memcpy(ip_address, ((u8 *)(&ifa->ifa_address)),
			       sizeof(ip_address));
			/*send the ip to fw*/
			if ((error = rwnx_send_ip_req(rwnx_hw, ip_address,
						      vif->drv_vif_index))) {
				WQ_DBG(DM_GENERIC, DL_ERR,
				       "send ip req fail\n");
			}
		}
		break;
	case NETDEV_DOWN:
		if (ndev) {
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "[auto]dev[%s] is down, ip_address:%pI4\n",
			       ndev->name, &(ifa->ifa_address));
		}
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static void rwnx_ipv6_set_task(struct work_struct *w)
{
	struct rwnx_hw *rwnx_hw =
		container_of(w, struct rwnx_hw, ipv6_set_task);

	RWNX_INFO_NOTIFY_SET_VIF(rwnx_hw, MSG_TYPE_TX_IPV6_ADDR,
				 rwnx_hw->ipv6_notify.vif_index,
				 rwnx_hw->ipv6_notify.ipv6);
}

int inet6dev_event(struct notifier_block *nb, unsigned long action, void *data)
{
	struct rwnx_hw *rwnx_hw =
		container_of(nb, struct rwnx_hw, inet6addr_notifier);
	struct inet6_ifaddr *ifa = data;
	struct net_device *ndev = ifa->idev->dev;
	struct rwnx_vif *vif;
	uint32_t st;

	if (!rwnx_hw || !(vif = rwnx_get_vif_by_ndev(rwnx_hw, ndev)))
		return NOTIFY_DONE;

	WQ_DBG(DM_GENERIC, DL_WRN, "comm: %s, pid: %d, action: %ld\n",
	       current->comm, current->pid, action);

	switch (action) {
	case NETDEV_UP:
		if (ndev) {
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "[auto]dev[%s] is up, ip_address:%pI6\n",
			       ndev->name, &(ifa->addr));
			rwnx_hw->ipv6_notify.vif_index = vif->drv_vif_index;
			memcpy(rwnx_hw->ipv6_notify.ipv6.ipv6_address,
			       ((u8 *)(&ifa->addr)), 16);
			st = ifa->addr.s6_addr32[0];

			if ((st & htonl(0xFFC00000)) == htonl(0XFE800000)) {
				WQ_DBG(DM_GENERIC, DL_WRN,
				       "link local address\n");
				rwnx_hw->ipv6_notify.ipv6.ipv6_type =
					IPV6_LINKLOCAL_ADDRESS;
			} else {
				WQ_DBG(DM_GENERIC, DL_WRN, "global address\n");
				rwnx_hw->ipv6_notify.ipv6.ipv6_type =
					IPV6_GLOBAL_ADDRESS;
			}

			/*send the ipv6 to fw*/
			schedule_work(&rwnx_hw->ipv6_set_task);
		}
		break;
	case NETDEV_DOWN:
		if (ndev) {
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "[auto]dev[%s] is down, ip_address:%pI6\n",
			       ndev->name, &(ifa->addr));
		}
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

void rwnx_cfg80211_timer_shutdown(struct rwnx_hw *rwnx_hw)
{
	u8 mac_id;

	WQ_DBG(DM_GENERIC, DL_WRN, "rwnx timer shutdonw...\n");

	/* shutdonw repeat timer. */
	del_timer_sync(&rwnx_hw->stat_timer);
	del_timer_sync(&rwnx_hw->tx_monitor_timer);
	del_timer_sync(&rwnx_hw->roc_timer);
	del_timer_sync(&rwnx_hw->txq_cleanup);
#ifdef NAPI_SUPPORT
	if (rwnx_hw->core->config.napi_enable) {
		hrtimer_try_to_cancel(&rwnx_hw->napi_rx_defer_timer);
	}
#endif
	del_timer_sync(&rwnx_hw->scan_timer);
	if (rwnx_hw->core->config.rx_ll) {
		for (mac_id = 0; mac_id < 2; mac_id++) {
			del_timer_sync(&rwnx_hw->rx_ll.rx_free_msg_env[mac_id]
						.rx_free_msg_timer);
		}
	}
}

void rwnx_cfg80211_timer_setup(struct rwnx_hw *rwnx_hw)
{
	WQ_DBG(DM_GENERIC, DL_WRN, "rwnx timer setup...\n");

	/* setup repeat timer. */
	if (wq_conf.stats_dump_interval) {
		mod_timer(&rwnx_hw->stat_timer,
			  jiffies + (HZ * wq_conf.stats_dump_interval));
	}
	mod_timer(&rwnx_hw->tx_monitor_timer, jiffies + HZ);
}

static int rwnx_monitor_mode_txpath_config(struct net_device *dev)
{
	int error = 0;
#if MONITOR_TX_MIN_IFS_ENABLE
	u32 aifs = 1;
	u32 cwmin = 0;
	u32 cwmax = 0;
	u32 txop = 0;
	u32 param;
	u8 hw_queue = RWNX_HWQ_BE;
#endif

	struct rwnx_sta *rwnx_sta = NULL;
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	u8 sta_idx = rwnx_monitor_me_sta_add(rwnx_hw, rwnx_vif->vif_index);

	rwnx_hw->monitor_vif = rwnx_vif->vif_index;
	if (rwnx_vif->ch_index != RWNX_CH_NOT_SET) {
		//Configure the monitor channel
		error = rwnx_send_config_monitor_req(
			rwnx_hw,
			&rwnx_hw->chanctx_table[rwnx_vif->ch_index].chan_def,
			NULL);
	}

	/* Init TXQ */
	rwnx_sta = &rwnx_hw->sta_table[sta_idx];
	rwnx_sta->sta_idx = sta_idx;
	rwnx_sta->aid = 0;
	rwnx_sta->vif_idx = rwnx_vif->vif_index;
	rwnx_sta->qos = false;
	rwnx_sta->acm = 0;
	rwnx_sta->ps.active = false;
	rwnx_mu_group_sta_init(rwnx_sta, NULL);
	rwnx_sta->valid = true;
	rwnx_vif->sta.ap = rwnx_sta;
	rwnx_txq_sta_init(rwnx_hw, rwnx_sta, 0);

	rwnx_send_me_set_control_port_req(rwnx_hw,
		rwnx_hw->mod_params.tx_ampdu_enable, sta_idx);
	rwnx_send_set_vif_state_req(rwnx_hw, rwnx_vif->vif_index, 0, true);

#if MONITOR_TX_MIN_IFS_ENABLE
	/* Store queue information in general structure */
	param = (u32)(aifs << 0);
	param |= (u32)(cwmin << 4);
	param |= (u32)(cwmax << 8);
	param |= (u32)(txop) << 12;

	/* Send the MM_SET_EDCA_REQ message to the FW */
	for (hw_queue = RWNX_HWQ_BK; hw_queue < RWNX_HWQ_BCMC; hw_queue++)
		rwnx_send_set_edca(rwnx_hw, hw_queue, param, false,
				   rwnx_vif->vif_index);
#endif
	return 0;
}

/*********************************************************************
 * netdev callbacks
 ********************************************************************/
/**
 * int (*ndo_open)(struct net_device *dev);
 *     This function is called when network device transistions to the up
 *     state.
 *
 * - Start FW if this is the first interface opened
 * - Add interface at fw level
 */
static int rwnx_open(struct net_device *dev)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	struct mm_add_if_cfm add_if_cfm;
	int error = 0;
	bool hml_flag = false;

	ENTER();

	// Check if it is the first opened VIF
	if (rwnx_hw->vif_started == 0) {
		// Start the FW
		if ((error = rwnx_send_start(rwnx_hw)))
			return error;

		/* Device is now started */
		set_bit(RWNX_DEV_STARTED, &rwnx_hw->flags);
	}

#ifdef CONFIG_HML
	if (strcmp(HML_IF_NAME, dev->name) == 0) {
		if (rwnx_vif->wdev.iftype != NL80211_IFTYPE_P2P_GO) {
			rwnx_vif->wdev.iftype = NL80211_IFTYPE_AP;
		}
		hml_flag = true;
		rwnx_vif->is_hml = true;
	}
#endif

	if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_AP_VLAN) {
		/* For AP_vlan use same fw and drv indexes. We ensure that this index
           will not be used by fw for another vif by taking index >= NX_VIRT_DEV_MAX */
		add_if_cfm.inst_nbr = rwnx_vif->drv_vif_index;
		netif_tx_stop_all_queues(dev);
	} else {
		/* Forward the information to the LMAC,
         *     p2p value not used in FMAC configuration, iftype is sufficient */
		if ((error = rwnx_send_add_if(rwnx_hw, dev->dev_addr,
					      RWNX_VIF_TYPE(rwnx_vif), false,
					      hml_flag, &add_if_cfm)))
			return error;

		if (add_if_cfm.status != 0) {
			RWNX_PRINT_CFM_ERR(add_if);
			return -EIO;
		}
	}

	/* Save the index retrieved from LMAC */
	spin_lock_bh(&rwnx_hw->cb_lock);
	rwnx_vif->vif_index = add_if_cfm.inst_nbr;
	rwnx_vif->up = true;
	rwnx_hw->vif_started++;
	rwnx_hw->vif_table[add_if_cfm.inst_nbr] = rwnx_vif;
	spin_unlock_bh(&rwnx_hw->cb_lock);

	if (rwnx_hw->vif_started > 1) {
		rwnx_send_me_set_ps_mode(rwnx_hw, PS_MODE_OFF);
	}
	if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_MONITOR) {
		rwnx_monitor_mode_txpath_config(dev);
	}

	/* TODO: enable this feature later */

	rwnx_cfg80211_timer_setup(rwnx_hw);

	netif_carrier_off(dev);

	if (rwnx_hw->core->hif_ops->hif == WQ_HIF_PCIE) {
		/* disable isr usage */
		struct rwnx_vif *vif = rwnx_get_vif_by_ndev(rwnx_hw, dev);
		rwnx_send_set_isr_usage_req(rwnx_hw, vif, 0);
	}

	return error;
}

/**
 * int (*ndo_stop)(struct net_device *dev);
 *     This function is called when network device transistions to the down
 *     state.
 *
 * - Remove interface at fw level
 * - Reset FW if this is the last interface opened
 */
static int rwnx_close(struct net_device *dev)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;

	ENTER();

	if (rwnx_hw->scan_request) {
		rwnx_send_abort_scan_req(rwnx_hw, rwnx_vif);
	}

	mutex_lock(&rwnx_hw->mutex);

	netdev_info(dev, "CLOSE");

	rwnx_radar_cancel_cac(&rwnx_hw->radar);

// serialize cfg80211 req, not abort cmd
#if 0
    /* Abort scan request on the vif */
    if (rwnx_hw->scan_request &&
        rwnx_hw->scan_request->wdev == &rwnx_vif->wdev) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
        struct cfg80211_scan_info info = {
            .aborted = true,
        };

        cfg80211_scan_done(rwnx_hw->scan_request, &info);
#else
        cfg80211_scan_done(rwnx_hw->scan_request, true);
#endif
        rwnx_hw->scan_request = NULL;

        mutex_unlock(&rwnx_hw->mutex);
    }
#endif

	rwnx_send_remove_if(rwnx_hw, rwnx_vif->vif_index);

	mutex_unlock(&rwnx_hw->mutex);

#if 0
    if (rwnx_hw->roc && (rwnx_hw->roc->vif == rwnx_vif)) {
        mutex_unlock(&rwnx_hw->mutex);
        kfree(rwnx_hw->roc);
        rwnx_hw->roc = NULL;
    }
#endif

	/* Ensure that we won't process disconnect ind */
	spin_lock_bh(&rwnx_hw->cb_lock);

	rwnx_vif->up = false;
	if (netif_carrier_ok(dev)) {
		if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION ||
		    RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_CLIENT) {
			if (rwnx_vif->sta.ft_assoc_ies) {
				kfree(rwnx_vif->sta.ft_assoc_ies);
				rwnx_vif->sta.ft_assoc_ies = NULL;
				rwnx_vif->sta.ft_assoc_ies_len = 0;
			}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)
			cfg80211_disconnected(dev, WLAN_REASON_DEAUTH_LEAVING,
					      NULL, 0, true, GFP_ATOMIC);
#else
			cfg80211_disconnected(dev, WLAN_REASON_DEAUTH_LEAVING,
					      NULL, 0, GFP_ATOMIC);
#endif
			if (rwnx_vif->sta.ap) {
				rwnx_txq_sta_deinit(rwnx_hw, rwnx_vif->sta.ap);
				rwnx_txq_tdls_vif_deinit(rwnx_vif);
			}
			netif_tx_stop_all_queues(dev);
			netif_carrier_off(dev);
		} else if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_AP_VLAN) {
			netif_carrier_off(dev);
		} else if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_MONITOR) {
			netif_tx_stop_all_queues(dev);
			netif_carrier_off(dev);
		} else {
			netdev_warn(dev,
				    "AP not stopped when disabling interface");
		}
	}
	spin_unlock_bh(&rwnx_hw->cb_lock);

	// TODO : Workaround for BUG13882. Wait sta_work done before set vif_table as NULL.
	rwnx_dbgfs_flush_sta_work(rwnx_hw);
	spin_lock_bh(&rwnx_hw->cb_lock);
	rwnx_hw->vif_table[rwnx_vif->vif_index] = NULL;
	spin_unlock_bh(&rwnx_hw->cb_lock);

	rwnx_chanctx_unlink(rwnx_vif);

	if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_MONITOR)
		rwnx_hw->monitor_vif = RWNX_INVALID_VIF;

	rwnx_hw->vif_started--;
	if (rwnx_hw->vif_started == 0) {
		rwnx_send_reset(rwnx_hw);

		// Set parameters to firmware
		rwnx_send_me_config_req(rwnx_hw);

		// Set channel parameters to firmware
		rwnx_send_me_chan_config_req(rwnx_hw);

		clear_bit(RWNX_DEV_STARTED, &rwnx_hw->flags);
	}

	if (rwnx_hw->vif_started == 1) {
		if (rwnx_hw->feature.ps_disable) {
			rwnx_send_me_set_ps_mode(rwnx_hw, PS_MODE_OFF);
		} else {
			rwnx_send_me_set_ps_mode(rwnx_hw, PS_MODE_ON_DYN);
		}
	}

	rwnx_cfg80211_timer_shutdown(rwnx_hw);

	return 0;
}

/**
 * struct net_device_stats* (*ndo_get_stats)(struct net_device *dev);
 *	Called when a user wants to get the network device usage
 *	statistics. Drivers must do one of the following:
 *	1. Define @ndo_get_stats64 to fill in a zero-initialised
 *	   rtnl_link_stats64 structure passed by the caller.
 *	2. Define @ndo_get_stats to update a net_device_stats structure
 *	   (which should normally be dev->stats) and return a pointer to
 *	   it. The structure may be changed asynchronously only if each
 *	   field is written atomically.
 *	3. Update dev->stats asynchronously and atomically, and define
 *	   neither operation.
 */
static struct net_device_stats *rwnx_get_stats(struct net_device *dev)
{
	struct rwnx_vif *vif = netdev_priv(dev);

	return &vif->net_stats;
}

/**
 * u16 (*ndo_select_queue)(struct net_device *dev, struct sk_buff *skb,
 *                         struct net_device *sb_dev);
 *	Called to decide which queue to when device supports multiple
 *	transmit queues.
 */
u16 rwnx_select_queue(struct net_device *dev, struct sk_buff *skb,
		      struct net_device *sb_dev)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	return rwnx_select_txq(rwnx_vif, skb);
}

/**
 * int (*ndo_set_mac_address)(struct net_device *dev, void *addr);
 *	This function  is called when the Media Access Control address
 *	needs to be changed. If this interface is not defined, the
 *	mac address can not be changed.
 */
static int rwnx_set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr *sa = addr;
	int ret;

	ret = eth_mac_addr(dev, sa);

	return ret;
}

int rwnx_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	int ret = 0;

	if ((cmd >= SIOCIWFIRST) && (cmd < SIOCIWFIRSTPRIV)) {
		ret = wq_wext_support_ioctl(dev, ifr, cmd);
	} else if ((cmd >= SIOCIWFIRSTPRIV) && (cmd < SIOCIWLASTPRIV)) {
		wq_priv_support_ioctl(dev, ifr, cmd);
	} else if (cmd == (SIOCDEVPRIVATE + 1)) {
		ret = wq_android_priv_cmd(dev, ifr, cmd);
	} else {
		WQ_DBG(DM_GENERIC, DL_WRN, "%s: [0x%x] Not Support %s\n",
		       __func__, cmd, ifr->ifr_name);
		ret = -EOPNOTSUPP;
	}
	return ret;
}

static void rwnx_set_he_su_mcs_index(struct rwnx_hw *rwnx_hw, uint32_t sta_idx,
				     uint8_t mcs_index, uint8_t bw_idx)
{
	uint16_t rate_cfg = 0;
	uint16_t nss = rwnx_hw->mod_params.nss - 1;
	uint16_t giAndPreTypeTx = 2;

	if (mcs_index > 11) {
		mcs_index = 11;
	}

	rate_cfg |= (FORMATMOD_HE_SU << 11);
	rate_cfg |= (giAndPreTypeTx << 9);
	rate_cfg |= (bw_idx << 7);
	rate_cfg |= (nss << 4);
	rate_cfg |= (mcs_index << 0);

	rwnx_send_me_rc_set_rate(rwnx_hw, sta_idx, rate_cfg);
}

static void radiotap_hdr_parse(struct rwnx_hw *rwnx_hw, uint32_t sta_idx,
			       struct ieee80211_radiotap_header *radiotap_hdr,
			       uint32_t radiotap_len)
{
	int ret;
	uint8_t known;
	uint8_t flags;
	uint8_t mcs_idx;
	uint8_t bw_idx;
	uint16_t tx_flags;
	struct ieee80211_radiotap_iterator iterator;

	ret = ieee80211_radiotap_iterator_init(&iterator, radiotap_hdr,
					       radiotap_len, NULL);

	while (ret == 0) {
		ret = ieee80211_radiotap_iterator_next(&iterator);
		if (ret)
			continue;

		switch (iterator.this_arg_index) {
		case IEEE80211_RADIOTAP_TX_FLAGS:
			tx_flags = *(uint16_t *)(iterator.this_arg);
			// TBD: need to handle tx flags.
			break;
		case IEEE80211_RADIOTAP_MCS:
			known = *((uint8_t *)(iterator.this_arg) + 0);
			flags = *((uint8_t *)(iterator.this_arg) + 1);
			mcs_idx = *((uint8_t *)(iterator.this_arg) + 2);
			bw_idx = flags & 0x03;
			if ((mcs_idx != rwnx_hw->monitor_param.tmp_mcs_idx) ||
			    (bw_idx != rwnx_hw->monitor_param.tx_bw_idx)) {
				rwnx_set_he_su_mcs_index(rwnx_hw, sta_idx,
							 mcs_idx, bw_idx);
				rwnx_hw->monitor_param.tmp_mcs_idx = mcs_idx;
				rwnx_hw->monitor_param.tx_bw_idx = bw_idx;
				WQ_DBG(DM_GENERIC, DL_INF,
				       "%s: switch mcs to %u, bw to %u MHz\n",
				       __func__, mcs_idx, (1 << bw_idx) * 20);
			}
			break;
		default:
			WQ_DBG(DM_GENERIC, DL_ERR,
			       "%s: can not handle radiotap type %u\n",
			       __func__, iterator.this_arg_index);
			break;
		}
	}
}

static netdev_tx_t rwnx_monitor_start_xmit(struct sk_buff *skb,
					   struct net_device *dev)
{
	netdev_tx_t ret = NETDEV_TX_OK;
	struct ieee80211_radiotap_header *rtap_hdr;
	struct ieee80211_hdr_3addr *dot11_hdr;
	uint8_t *skb_data = skb->data;
	uint32_t skb_len = skb->len;
	uint32_t rtap_len = 0;

	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	uint8_t sta_idx = rwnx_vif->sta.ap->sta_idx;
	struct rwnx_sta *sta = &rwnx_hw->sta_table[sta_idx];

	if (unlikely(skb_len < sizeof(struct ieee80211_radiotap_header)))
		goto fail;

	rtap_hdr = (struct ieee80211_radiotap_header *)skb->data;
	if (unlikely(rtap_hdr->it_version))
		goto fail;

	rtap_len = ieee80211_get_radiotap_len(skb->data);
	if (unlikely(skb->len < rtap_len))
		goto fail;

	radiotap_hdr_parse(rwnx_hw, sta->sta_idx, rtap_hdr, rtap_len);

	/* Skip the ratio tap header */
	skb_pull(skb, rtap_len);

	if (unlikely(skb->len < sizeof(struct ieee80211_hdr_3addr)))
		goto fail;

	dot11_hdr = (struct ieee80211_hdr_3addr *)skb->data;
	if (likely(ieee80211_is_data(dot11_hdr->frame_control))) {
		ret = rwnx_start_xmit(skb, dev);
		WQ_DBG(DM_GENERIC, DL_INF, "%s: inject data frame length %u\n",
		       __func__, skb->len);
		return ret;
	} else {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "%s: only data frame supported to inject in monitor mode now",
		       __func__);
	}

fail:
	WQ_DBG(DM_TX, DL_ERR, "%s: drop packet, len: %d\n", __func__, skb->len);
	dump_bytes(DL_ERR, "frame to inject failed in monitor mode:", skb_data,
		   skb_len);

	dev_kfree_skb_any(skb);
	return ret;
}

u16 rwnx_monitor_select_queue(struct net_device *dev, struct sk_buff *skb,
			      struct net_device *sb_dev)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	uint32_t txq_idx;
	skb->priority = AC_VI;
	netif_tx_start_all_queues(dev);

	txq_idx = rwnx_vif->sta.ap->sta_idx * NX_NB_TID_PER_STA + skb->priority;

	if (!netif_carrier_ok(dev))
		netif_carrier_on(dev);
	netif_wake_subqueue(dev, txq_idx);

	return txq_idx;
}

static const struct net_device_ops rwnx_netdev_ops = {
	.ndo_open = rwnx_open,
	.ndo_stop = rwnx_close,
	.ndo_start_xmit = rwnx_start_xmit,
	.ndo_get_stats = rwnx_get_stats,
	.ndo_select_queue = rwnx_select_queue,
	.ndo_set_mac_address = rwnx_set_mac_address,
	//    .ndo_set_features       = rwnx_set_features,
	//    .ndo_set_rx_mode        = rwnx_set_multicast_list,
	.ndo_do_ioctl = rwnx_ioctl,
};

static const struct net_device_ops rwnx_netdev_monitor_ops = {
	.ndo_open = rwnx_open,
	.ndo_stop = rwnx_close,
	.ndo_start_xmit = rwnx_monitor_start_xmit,
	.ndo_select_queue = rwnx_monitor_select_queue,
	.ndo_get_stats = rwnx_get_stats,
	.ndo_set_mac_address = rwnx_set_mac_address,
};

static void rwnx_netdev_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->priv_flags &= ~IFF_TX_SKB_SHARING;
	dev->netdev_ops = &rwnx_netdev_ops;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0)
	dev->destructor = free_netdev;
#else
	dev->needs_free_netdev = true;
#endif
	dev->watchdog_timeo = RWNX_TX_LIFETIME_MS;

	dev->needed_headroom = IPC_TX_MAX_HEADROOM;
#ifdef CONFIG_RWNX_AMSDUS_TX
	dev->needed_headroom =
		max(dev->needed_headroom,
		    (unsigned short)(sizeof(struct ethhdr) + 4 +
				     sizeof(rfc1042_header) + 2));
#endif /* CONFIG_RWNX_AMSDUS_TX */

	dev->hw_features = 0;
}

#ifdef NAPI_SUPPORT
static int rwnx_napi_poll_rx(struct napi_struct *napi, int budget)
{
	struct rwnx_hw *rwnx_hw = container_of(napi, struct rwnx_hw, napi_rx);
	struct htc_q *rxq = &rwnx_hw->core->htc.rxq.pkt;
	struct sk_buff *skb;
	int work_done = 0;
	u64 time_start_us = 0, time_end_us = 0;

	time_start_us = (u64)ktime_to_us(ktime_get());

	PROFILING_SET(SW_PROF_NAPI_RX_SOFTIRQ);

	if (!rwnx_hw->core->config.ipc_rx_pkt_use_wq) {
		while ((skb = skb_dequeue(&rxq->head)) != NULL) {
			__wq_ipc_rx_pkt(rwnx_hw, skb);

			if (skb_queue_len(&rwnx_hw->napi_rx_pkt_list) >=
			    budget) {
				break;
			}
		}
	}

	while ((skb = skb_dequeue(&rwnx_hw->napi_rx_pkt_list)) != NULL) {
		napi_gro_receive(&rwnx_hw->napi_rx, skb);
		work_done++;

		if (work_done >= budget)
			goto done;
	}

done:
	if (work_done < budget) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
		napi_complete_done(napi, work_done);
#else
		napi_complete(napi);
#endif

		if (rwnx_hw->napi_param.napi_enable &&
		    !skb_queue_empty(&rwnx_hw->napi_rx_pkt_list)) {
			napi_schedule(&rwnx_hw->napi_rx);
		}
	}

	PROFILING_CLR(SW_PROF_NAPI_RX_SOFTIRQ);

	time_end_us = (u64)ktime_to_us(ktime_get());
	atomic_add((u32)(time_end_us - time_start_us), &rwnx_hw->napi_rx_time);

	return work_done;
}

enum hrtimer_restart rwnx_napi_sched_rx_cb(struct hrtimer *t)
{
	struct rwnx_hw *rwnx_hw =
		container_of(t, struct rwnx_hw, napi_rx_defer_timer);

	napi_schedule(&rwnx_hw->napi_rx);
	return HRTIMER_NORESTART;
}
#endif

#ifdef INCLUDE_WQ_IWPRIVE
extern struct iw_handler_def wq_iwpriv_handler_def;
#endif

/*********************************************************************
 * Cfg80211 callbacks (and helper)
 *********************************************************************/
static struct wireless_dev *rwnx_interface_add(struct rwnx_hw *rwnx_hw,
					       const char *name,
					       unsigned char name_assign_type,
					       enum nl80211_iftype type,
					       struct vif_params *params)
{
	struct net_device *ndev;
	struct rwnx_vif *vif;
	int min_idx, max_idx;
	int vif_idx = -1;
	int i;

	// Look for an available VIF
	if (type == NL80211_IFTYPE_AP_VLAN) {
		min_idx = NX_VIRT_DEV_MAX;
		max_idx = NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX;
	} else {
		min_idx = 0;
		max_idx = NX_VIRT_DEV_MAX;
	}

	for (i = min_idx; i < max_idx; i++) {
		if ((rwnx_hw->avail_idx_map) & BIT_ULL(i)) {
			vif_idx = i;
			break;
		}
	}
	if (vif_idx < 0)
		return NULL;

#ifndef CONFIG_RWNX_MON_DATA
	list_for_each_entry (vif, &rwnx_hw->vifs, list) {
		// Check if monitor interface already exists or type is monitor
		if ((RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_MONITOR) ||
		    (type == NL80211_IFTYPE_MONITOR)) {
			wiphy_err(
				rwnx_hw->wiphy,
				"Monitor+Data interface support (MON_DATA) disabled\n");
			return NULL;
		}
	}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
	ndev = alloc_netdev_mqs(sizeof(*vif), name, name_assign_type,
				rwnx_netdev_setup, NX_NB_NDEV_TXQ, 1);
#else
	ndev = alloc_netdev_mqs(sizeof(*vif), name, rwnx_netdev_setup,
				NX_NB_NDEV_TXQ, 1);
#endif
	if (!ndev)
		return NULL;

	vif = netdev_priv(ndev);
	ndev->ieee80211_ptr = &vif->wdev;
	vif->wdev.wiphy = rwnx_hw->wiphy;
	vif->rwnx_hw = rwnx_hw;
	vif->ndev = ndev;
	vif->drv_vif_index = vif_idx;
	SET_NETDEV_DEV(ndev, wiphy_dev(vif->wdev.wiphy));
	vif->wdev.netdev = ndev;
	vif->wdev.iftype = type;
	vif->up = false;
	vif->ch_index = RWNX_CH_NOT_SET;
	vif->generation = 0;
	memset(&vif->net_stats, 0, sizeof(vif->net_stats));

#ifdef NAPI_SUPPORT
	if (rwnx_hw->core->config.napi_enable) {
		ndev->features |= NETIF_F_GRO;
		ndev->features |= NETIF_F_SG;
	}
#endif

#ifdef INCLUDE_WQ_IWPRIVE
#ifdef CONFIG_WIRELESS_EXT
	ndev->wireless_handlers = &wq_iwpriv_handler_def;
#endif
#endif

	switch (type) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		vif->sta.flags = 0;
		vif->sta.ap = NULL;
		vif->sta.tdls_sta = NULL;
		vif->sta.ft_assoc_ies = NULL;
		vif->sta.ft_assoc_ies_len = 0;
		break;
	case NL80211_IFTYPE_MESH_POINT:
		INIT_LIST_HEAD(&vif->ap.mpath_list);
		INIT_LIST_HEAD(&vif->ap.proxy_list);
		vif->ap.mesh_pm = NL80211_MESH_POWER_ACTIVE;
		vif->ap.next_mesh_pm = NL80211_MESH_POWER_ACTIVE;
		fallthrough;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		INIT_LIST_HEAD(&vif->ap.sta_list);
		memset(&vif->ap.bcn, 0, sizeof(vif->ap.bcn));
		memset(&vif->ap.chandef, 0, sizeof(vif->ap.chandef));
		vif->ap.flags = 0;
		break;
	case NL80211_IFTYPE_AP_VLAN: {
		struct rwnx_vif *master_vif;
		bool found = false;
		list_for_each_entry (master_vif, &rwnx_hw->vifs, list) {
			if ((RWNX_VIF_TYPE(master_vif) == NL80211_IFTYPE_AP) &&
			    !(!memcmp(master_vif->ndev->dev_addr,
				      params->macaddr, ETH_ALEN))) {
				found = true;
				break;
			}
		}

		if (!found)
			goto err;

		vif->ap_vlan.master = master_vif;
		vif->ap_vlan.sta_4a = NULL;
		break;
	}
	case NL80211_IFTYPE_MONITOR:
		ndev->type = ARPHRD_IEEE80211_RADIOTAP;
		ndev->netdev_ops = &rwnx_netdev_monitor_ops;
		break;
	default:
		break;
	}

	if (type == NL80211_IFTYPE_AP_VLAN) {
		dev_addr_set(ndev, params->macaddr);
		vif->crdt_gid = WQ_INVALID_CRDT_ID;
	} else {
		u8 mac[ETH_ALEN];

		if (type == NL80211_IFTYPE_STATION) {
			vif->extAP_supp = rwnx_hw->mod_params.extap_support;
			rwnx_hw->amsdu_param.enable = !vif->extAP_supp;
			WQ_DBG(DM_CRDT, DL_WRN, "%s: extAP_supp:%d\n", __func__, vif->extAP_supp);
		}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
		ether_addr_copy(mac, rwnx_hw->wiphy->perm_addr);
#else
		(void)memcpy(mac, rwnx_hw->wiphy->perm_addr, ETH_ALEN);
#endif
		if (vif_idx != 0) {
			mac[0] = (mac[0] ^ BIT(vif_idx)) | 0x2;
		}
		dev_addr_set(ndev, mac);

		if (wq_conf.default_p2p_on_for_usb == true &&
		    !strcmp(name, "p2p%d")) {
			WQ_DBG(DM_CRDT, DL_WRN,
			       "%s: Interface:p2p0 no need to add credit\n",
			       __func__);
		} else if (!rwnx_add_credit_grp(&rwnx_hw->crdt_mgmt,
						&vif->crdt_gid) &&
			   !rwnx_hw->large_ap_mode)
			goto err;
	}

	if (params) {
		vif->use_4addr = params->use_4addr;
		ndev->ieee80211_ptr->use_4addr = params->use_4addr;
	} else
		vif->use_4addr = false;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 12, 0)
	if (register_netdevice(ndev))
#else
	if (cfg80211_register_netdevice(ndev))
#endif
		goto err;

	if (type == NL80211_IFTYPE_STATION) {
		ndev->priv_flags &= ~IFF_DONT_BRIDGE;
	}
	spin_lock_bh(&rwnx_hw->cb_lock);
	list_add_tail(&vif->list, &rwnx_hw->vifs);
	spin_unlock_bh(&rwnx_hw->cb_lock);
	rwnx_hw->avail_idx_map &= ~BIT_ULL(vif_idx);

	return &vif->wdev;

err:
	free_netdev(ndev);
	return NULL;
}

/**
 * @brief Retrieve the rwnx_sta object allocated for a given MAC address
 * and a given role.
 */
static struct rwnx_sta *rwnx_retrieve_sta(struct rwnx_hw *rwnx_hw,
					  struct rwnx_vif *rwnx_vif, u8 *addr,
					  __le16 fc, bool ap)
{
	if (ap) {
		/* only deauth, disassoc and action are bufferable MMPDUs */
		bool bufferable = ieee80211_is_deauth(fc) ||
				  ieee80211_is_disassoc(fc) ||
				  ieee80211_is_action(fc);

		/* Check if the packet is bufferable or not */
		if (bufferable) {
			/* Check if address is a broadcast or a multicast address */
			if (is_broadcast_ether_addr(addr) ||
			    is_multicast_ether_addr(addr)) {
				/* Returned STA pointer */
				struct rwnx_sta *rwnx_sta =
					&rwnx_hw->sta_table[rwnx_vif->ap
								    .bcmc_index];

				if (rwnx_sta->valid)
					return rwnx_sta;
			} else {
				/* Returned STA pointer */
				struct rwnx_sta *rwnx_sta;

				/* Go through list of STAs linked with the provided VIF */
				list_for_each_entry (rwnx_sta,
						     &rwnx_vif->ap.sta_list,
						     list) {
					if (rwnx_sta->valid &&
					    ether_addr_equal(rwnx_sta->mac_addr,
							     addr)) {
						/* Return the found STA */
						return rwnx_sta;
					}
				}
			}
		}
	} else {
		return rwnx_vif->sta.ap;
	}

	return NULL;
}

/**
 * @add_virtual_intf: create a new virtual interface with the given name,
 *	must set the struct wireless_dev's iftype. Beware: You must create
 *	the new netdev in the wiphy's network namespace! Returns the struct
 *	wireless_dev, or an ERR_PTR. For P2P device wdevs, the driver must
 *	also set the address member in the wdev.
 */
static struct wireless_dev *
rwnx_cfg80211_add_iface(struct wiphy *wiphy, const char *name,
			unsigned char name_assign_type,
			enum nl80211_iftype type, struct vif_params *params)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct wireless_dev *wdev;

	ENTER();

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
	wdev = rwnx_interface_add(rwnx_hw, name, name_assign_type, type,
				  params);
#else
	wdev = rwnx_interface_add(rwnx_hw, name, 0, type, params);
#endif

	if (!wdev)
		return ERR_PTR(-EINVAL);

	return wdev;
}

/**
 * @del_virtual_intf: remove the virtual interface
 */
static int rwnx_cfg80211_del_iface(struct wiphy *wiphy,
				   struct wireless_dev *wdev)
{
	struct net_device *dev = wdev->netdev;
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);

	ENTER();

	netdev_info(dev, "Remove Interface");

	if (dev->reg_state == NETREG_REGISTERED) {
		/* Will call rwnx_close if interface is UP */
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 12, 0)
		unregister_netdevice(dev);
#else
		cfg80211_unregister_netdevice(dev);
#endif
	}

	spin_lock_bh(&rwnx_hw->cb_lock);
	list_del(&rwnx_vif->list);
	spin_unlock_bh(&rwnx_hw->cb_lock);
	rwnx_hw->avail_idx_map |= BIT_ULL(rwnx_vif->drv_vif_index);
	rwnx_vif->ndev = NULL;
	rwnx_del_credit_grp(&rwnx_hw->crdt_mgmt, rwnx_vif->crdt_gid);

	/* Clear the priv in adapter */
	dev->ieee80211_ptr = NULL;

	return 0;
}

static void rwnx_switch_vif_mode(struct net_device *dev,
				 enum nl80211_iftype new_type)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	enum nl80211_iftype old_type = RWNX_VIF_TYPE(rwnx_vif);
	struct mm_add_if_cfm add_if_cfm;
	bool hml_flag = false;

	mutex_lock(&rwnx_hw->mutex);
	rwnx_radar_cancel_cac(&rwnx_hw->radar);
	rwnx_send_remove_if(rwnx_hw, rwnx_vif->vif_index);
	mutex_unlock(&rwnx_hw->mutex);

	spin_lock_bh(&rwnx_hw->cb_lock);
	rwnx_vif->up = false;
	if (netif_carrier_ok(dev) && (old_type == NL80211_IFTYPE_STATION)) {
		if (rwnx_vif->sta.ft_assoc_ies) {
			kfree(rwnx_vif->sta.ft_assoc_ies);
			rwnx_vif->sta.ft_assoc_ies = NULL;
			rwnx_vif->sta.ft_assoc_ies_len = 0;
		}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)
		cfg80211_disconnected(dev, WLAN_REASON_DEAUTH_LEAVING, NULL, 0,
				      true, GFP_ATOMIC);
#else
		cfg80211_disconnected(dev, WLAN_REASON_DEAUTH_LEAVING, NULL, 0,
				      GFP_ATOMIC);
#endif
		if (rwnx_vif->sta.ap) {
			rwnx_txq_sta_deinit(rwnx_hw, rwnx_vif->sta.ap);
			rwnx_txq_tdls_vif_deinit(rwnx_vif);
		}
		netif_tx_stop_all_queues(dev);
		netif_carrier_off(dev);
	}
	rwnx_hw->vif_table[rwnx_vif->vif_index] = NULL;
	rwnx_chanctx_unlink(rwnx_vif);
	spin_unlock_bh(&rwnx_hw->cb_lock);

#ifdef CONFIG_HML
	if (strcmp(HML_IF_NAME, dev->name) == 0) {
		hml_flag = true;
	}
#endif

	rwnx_send_add_if(rwnx_hw, dev->dev_addr, new_type, false, hml_flag,
			 &add_if_cfm);
	if (add_if_cfm.status != 0) {
		RWNX_PRINT_CFM_ERR(add_if);
		return;
	}

	spin_lock_bh(&rwnx_hw->cb_lock);
	rwnx_vif->vif_index = add_if_cfm.inst_nbr;
	rwnx_vif->up = true;
	rwnx_hw->vif_table[add_if_cfm.inst_nbr] = rwnx_vif;
	spin_unlock_bh(&rwnx_hw->cb_lock);

	if (rwnx_hw->vif_started > 1) {
		rwnx_send_me_set_ps_mode(rwnx_hw, PS_MODE_OFF);
	}
}

/**
 * @change_virtual_intf: change type/configuration of virtual interface,
 *	keep the struct wireless_dev's iftype updated.
 */
static int rwnx_cfg80211_change_iface(struct wiphy *wiphy,
				      struct net_device *dev,
				      enum nl80211_iftype type,
				      struct vif_params *params)
{
#ifndef CONFIG_RWNX_MON_DATA
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
#endif
	struct rwnx_vif *vif = netdev_priv(dev);

	ENTER();

	WQ_DBG(DM_GENERIC, DL_WRN,
	       "rwnx_cfg80211_change_iface vif_index=%d, drv_vif_index=%d, iftype=%d, up=%d, sta.ap=0x%p | new_type=%d\n",
	       vif->vif_index, vif->drv_vif_index, vif->wdev.iftype, vif->up,
	       vif->sta.ap, type);

	if (vif->up) {
		/** In the case that ap->sta and sta->ap, call rwnx_switch_vif_mode
         * to change the vif mode for BUG 10018(open softapd always fails
         * in goke environment).
         * Otherwise return busy since change type when vif
         * being up is not supported. */
		if ((((RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_STATION) &&
		      (type == NL80211_IFTYPE_AP)) ||
		     ((RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_AP) &&
		      (type == NL80211_IFTYPE_STATION)))) {
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "switch vif(%s) mode from %d to %d", dev->name,
			       RWNX_VIF_TYPE(vif), type);
			rwnx_switch_vif_mode(dev, type);
		} else {
			return (-EBUSY);
		}
	}

#ifndef CONFIG_RWNX_MON_DATA
	if ((type == NL80211_IFTYPE_MONITOR) &&
	    (RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_MONITOR)) {
		struct rwnx_vif *vif_el;
		list_for_each_entry (vif_el, &rwnx_hw->vifs, list) {
			// Check if data interface already exists
			if ((vif_el != vif) &&
			    (RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_MONITOR)) {
				wiphy_err(
					rwnx_hw->wiphy,
					"Monitor+Data interface support (MON_DATA) disabled\n");
				return -EIO;
			}
		}
	}
#endif

	// Reset to default case (i.e. not monitor)
	dev->type = ARPHRD_ETHER;
	dev->netdev_ops = &rwnx_netdev_ops;

	switch (type) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		vif->sta.flags = 0;
		vif->sta.ap = NULL;
		vif->sta.tdls_sta = NULL;
		vif->sta.ft_assoc_ies = NULL;
		vif->sta.ft_assoc_ies_len = 0;
		break;
	case NL80211_IFTYPE_MESH_POINT:
		INIT_LIST_HEAD(&vif->ap.mpath_list);
		INIT_LIST_HEAD(&vif->ap.proxy_list);
		fallthrough;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		INIT_LIST_HEAD(&vif->ap.sta_list);
		memset(&vif->ap.bcn, 0, sizeof(vif->ap.bcn));
		memset(&vif->ap.chandef, 0, sizeof(vif->ap.chandef));
		vif->ap.flags = 0;
		break;
	case NL80211_IFTYPE_AP_VLAN:
		return -EPERM;
	case NL80211_IFTYPE_MONITOR:
		rwnx_hw->monitor_param.tx_mcs_idx = 0xFF;
		rwnx_hw->monitor_param.tmp_mcs_idx = 0xFF;
		dev->type = ARPHRD_IEEE80211_RADIOTAP;
		dev->netdev_ops = &rwnx_netdev_monitor_ops;
		break;
	default:
		break;
	}

	vif->generation = 0;
	vif->wdev.iftype = type;
	if (params->use_4addr != -1)
		vif->use_4addr = params->use_4addr;

	return 0;
}

static void rwnx_cfg80211_abort_scan(struct wiphy *wiphy,
				     struct wireless_dev *wdev)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *vif = container_of(wdev, struct rwnx_vif, wdev);
	int res = 0;

	ENTER();

	if (!rwnx_hw->scan_request) {
		WQ_DBG(DM_IEEE80211, DL_WRN, "%s: no scan task is ongoing!\n",
		       __func__);
		return;
	}

	if (rwnx_hw->scan_request->wdev == wdev) {
		res = rwnx_send_abort_scan_req(rwnx_hw, vif);
		if (res) {
			WQ_DBG(DM_IEEE80211, DL_WRN,
			       "%s:send abort_scan cmd failed!, res = %d\n",
			       __func__, res);
		}
	} else {
		WQ_DBG(DM_IEEE80211, DL_WRN, "%s:wdev mismatch %p, %p!\n",
		       __func__, rwnx_hw->scan_request->wdev, wdev);
	}

	return;
}

/**
 * @scan: Request to do a scan. If returning zero, the scan request is given
 *	the driver, and will be valid until passed to cfg80211_scan_done().
 *	For scan results, call cfg80211_inform_bss(); you can call this outside
 *	the scan/scan_done bracket too.
 */
static int rwnx_cfg80211_scan(struct wiphy *wiphy,
			      struct cfg80211_scan_request *request)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif =
		container_of(request->wdev, struct rwnx_vif, wdev);
	int error;

	ENTER();

	if (rwnx_hw->feature.scan_disable) {
		return -EACCES;
	}

	if (rwnx_hw->connect_req_ts) {
		if (!time_after(jiffies, rwnx_hw->connect_req_ts +
						 msecs_to_jiffies(10000))) {
			WQ_DBG(DM_IEEE80211, DL_ERR,
			       "[auto]msg:ignore scan, %lu < %lu", jiffies,
			       rwnx_hw->connect_req_ts +
				       msecs_to_jiffies(10000));
			return -EACCES;
		} else {
			rwnx_hw->connect_req_ts = 0;
		}
	}

	if (gv_get_pwr_from_bin_flag) {
		// store 1M/6M power all support channels
		store_supp_chan_pwr(rwnx_hw, wq_supp_pwr);
		if (wq_supp_pwr[0].channel == 0) {
			WQ_DBG(DM_IEEE80211, DL_ERR,
			       "scan pwr value store failed\n");
		}
	}
	mutex_lock(&rwnx_hw->mutex);
	rwnx_hw->scan_request = request;
	if ((error = rwnx_send_scanu_req(rwnx_hw, rwnx_vif, request))) {
		rwnx_hw->scan_request = NULL;
		mutex_unlock(&rwnx_hw->mutex);
		return error;
	}
	mod_timer(&rwnx_hw->scan_timer,
		  (jiffies + msecs_to_jiffies(RWNX_80211_SCAN_TIMEOUT_MS)));

	return 0;
}

/**
 * @sched_scan_start: Tell the driver to start a scheduled scan.
 *
 */
static int
rwnx_cfg80211_sched_scan_start(struct wiphy *wiphy, struct net_device *dev,
			       struct cfg80211_sched_scan_request *request)
{
	//struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);

	int error;

	ENTER();

#if LINUX_VERSION_CODE > KERNEL_VERSION(4, 10, 0)
	WQ_DBG(DM_GENERIC, DL_WRN, "%s reqid: %llu", __func__, request->reqid);
#endif
#if 0
    if (wq_misc_ctrl & SCAN_DISABLE) {
        return -EACCES;
    }
#endif
	if (rwnx_hw->vif_started > 1) {
		WQ_DBG(DM_GENERIC, DL_WRN,
		       "Both station and P2P are started, do not sched_scan_start");
		return -EACCES;
	}

	mutex_lock(&rwnx_hw->mutex);
	rwnx_hw->sched_scan_req.sched_scan_request = request;
	if ((error = rwnx_send_sched_scan_start_req(rwnx_hw, rwnx_vif,
						    request))) {
		rwnx_hw->sched_scan_req.sched_scan_request = NULL;
		mutex_unlock(&rwnx_hw->mutex);
		return error;
	}

	mutex_unlock(&rwnx_hw->mutex);
	return 0;
}

/**
 * @sched_scan_stop: Tell the driver to stop an ongoing scheduled scan with
 *  given request id. This call must stop the scheduled scan and be ready
 *  for starting a new one before it returns, i.e. @sched_scan_start may be
 *  called immediately after that again and should not fail in that case.
 *  The driver should not call cfg80211_sched_scan_stopped() for a requested
 *  stop (when this method returns 0).
 *
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
static int rwnx_cfg80211_sched_scan_stop(struct wiphy *wiphy,
					 struct net_device *dev)
#else
static int rwnx_cfg80211_sched_scan_stop(struct wiphy *wiphy,
					 struct net_device *dev, u64 reqid)
#endif
{
	//struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);

	int error;

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
	u64 reqid = 0;
#endif

	ENTER();

	WQ_DBG(DM_GENERIC, DL_WRN, "%s reqid: %llu", __func__, reqid);

	mutex_lock(&rwnx_hw->mutex);
	if ((error = rwnx_send_sched_scan_stop_req(rwnx_hw, rwnx_vif, reqid))) {
		mutex_unlock(&rwnx_hw->mutex);
		WQ_DBG(DM_GENERIC, DL_WRN, "%s, error num: %d, just return 0",
		       __func__, error);
		return 0;
	}

	mutex_unlock(&rwnx_hw->mutex);
	rwnx_hw->sched_scan_req.sched_scan_request = NULL;
	return 0;
}

/**
 * @add_key: add a key with the given parameters. @mac_addr will be %NULL
 *	when adding a group key.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static int rwnx_cfg80211_add_key(struct wiphy *wiphy, struct net_device *netdev,
				 int link_id, u8 key_index, bool pairwise,
				 const u8 *mac_addr, struct key_params *params)
#else
static int rwnx_cfg80211_add_key(struct wiphy *wiphy, struct net_device *netdev,
				 u8 key_index, bool pairwise,
				 const u8 *mac_addr, struct key_params *params)
#endif
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *vif = netdev_priv(netdev);
	int i, error = 0;
	struct mm_key_add_cfm key_add_cfm;
	u8 cipher = 0;
	struct rwnx_sta *sta = NULL;
	struct rwnx_key *rwnx_key;
	ENTER();

	if (mac_addr) {
		sta = rwnx_get_sta(rwnx_hw, mac_addr);
		if (!sta)
			return -EINVAL;
		rwnx_key = &sta->key;
	} else
		rwnx_key = &vif->key[key_index];

	/* Retrieve the cipher suite selector */
	switch (params->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		cipher = MAC_CIPHER_WEP40;
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		cipher = MAC_CIPHER_WEP104;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		cipher = MAC_CIPHER_TKIP;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		cipher = MAC_CIPHER_CCMP;
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		cipher = MAC_CIPHER_BIP_CMAC_128;
		break;
	case WLAN_CIPHER_SUITE_SMS4: {
		// Need to reverse key order
		u8 tmp, *key = (u8 *)params->key;
		cipher = MAC_CIPHER_WPI_SMS4;
		for (i = 0; i < WPI_SUBKEY_LEN / 2; i++) {
			tmp = key[i];
			key[i] = key[WPI_SUBKEY_LEN - 1 - i];
			key[WPI_SUBKEY_LEN - 1 - i] = tmp;
		}
		for (i = 0; i < WPI_SUBKEY_LEN / 2; i++) {
			tmp = key[i + WPI_SUBKEY_LEN];
			key[i + WPI_SUBKEY_LEN] = key[WPI_KEY_LEN - 1 - i];
			key[WPI_KEY_LEN - 1 - i] = tmp;
		}
		break;
	}
	case WLAN_CIPHER_SUITE_GCMP:
		cipher = MAC_CIPHER_GCMP_128;
		break;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
	case WLAN_CIPHER_SUITE_GCMP_256:
		cipher = MAC_CIPHER_GCMP_256;
		break;
	case WLAN_CIPHER_SUITE_CCMP_256:
		cipher = MAC_CIPHER_CCMP_256;
		break;
#endif
	default:
		return -EINVAL;
	}

	spin_lock_bh(&rwnx_hw->delayed_key_lock);
	if (pairwise)
		rwnx_hw->key_add_params.vif = NULL;

	if ((RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_STATION ||
#ifdef CONFIG_HML
	     (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_GO &&
	      rwnx_hw->key_add_params.m4_sended) ||
#endif
	     RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_CLIENT) &&
	    (!rwnx_hw->key_add_params.m4_ack_done) && pairwise &&
	    params->cipher != WLAN_CIPHER_SUITE_WEP40 &&
	    params->cipher != WLAN_CIPHER_SUITE_WEP104) {
		WQ_DBG(DM_GENERIC, DL_WRN, "store the ptk\n");

		memset(&rwnx_hw->key_add_params, 0,
		       sizeof(struct stored_params));
		memcpy(rwnx_hw->key_add_params.key, params->key,
		       params->key_len);
		rwnx_hw->key_add_params.key_len = params->key_len;
		rwnx_hw->key_add_params.pairwise = pairwise;
		rwnx_hw->key_add_params.key_index = key_index;
		rwnx_hw->key_add_params.cipher = cipher;
		rwnx_hw->key_add_params.vif = vif;
		rwnx_hw->key_add_params.sta_index = (sta ? sta->sta_idx : 0xFF);
		spin_unlock_bh(&rwnx_hw->delayed_key_lock);

		//add key add timer in case schedule key add work failed
		mod_timer(&rwnx_hw->key_add_timer, jiffies + 5 * HZ);
		return 0;
	}

	spin_unlock_bh(&rwnx_hw->delayed_key_lock);
	if ((error = rwnx_send_key_add(rwnx_hw, vif->vif_index,
				       (sta ? sta->sta_idx : 0xFF), pairwise,
				       (u8 *)params->key, params->key_len,
				       key_index, cipher, &key_add_cfm)))
		return error;

	if (key_add_cfm.status != 0) {
		RWNX_PRINT_CFM_ERR(key_add);
		return -EIO;
	}

	/* Save the index retrieved from LMAC */
	rwnx_key->hw_idx = key_add_cfm.hw_key_idx;
#ifdef DEBUG_WQ_DFX
	/* update security information */
	wq_dbg_update_security_info(&vif->security, key_index, pairwise,
				    cipher);
#endif

	return 0;
}

/**
 * @get_key: get information about the key with the given parameters.
 *	@mac_addr will be %NULL when requesting information for a group
 *	key. All pointers given to the @callback function need not be valid
 *	after it returns. This function should return an error if it is
 *	not possible to retrieve the key, -ENOENT if it doesn't exist.
 *
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static int rwnx_cfg80211_get_key(struct wiphy *wiphy, struct net_device *netdev,
				 int link_id, u8 key_index, bool pairwise,
				 const u8 *mac_addr, void *cookie,
				 void (*callback)(void *cookie,
						  struct key_params *))
#else
static int rwnx_cfg80211_get_key(struct wiphy *wiphy, struct net_device *netdev,
				 u8 key_index, bool pairwise,
				 const u8 *mac_addr, void *cookie,
				 void (*callback)(void *cookie,
						  struct key_params *))
#endif
{
	ENTER();

	return -1;
}

/**
 * @del_key: remove a key given the @mac_addr (%NULL for a group key)
 *	and @key_index, return -ENOENT if the key doesn't exist.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static int rwnx_cfg80211_del_key(struct wiphy *wiphy, struct net_device *netdev,
				 int link_id, u8 key_index, bool pairwise,
				 const u8 *mac_addr)
#else
static int rwnx_cfg80211_del_key(struct wiphy *wiphy, struct net_device *netdev,
				 u8 key_index, bool pairwise,
				 const u8 *mac_addr)
#endif
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *vif = netdev_priv(netdev);
	int error;
	struct rwnx_sta *sta = NULL;
	struct rwnx_key *rwnx_key;

	ENTER();

	if (mac_addr) {
		sta = rwnx_get_sta(rwnx_hw, mac_addr);
		if (!sta)
			return -EINVAL;
		rwnx_key = &sta->key;
	} else
		rwnx_key = &vif->key[key_index];

	error = rwnx_send_key_del(rwnx_hw, rwnx_key->hw_idx);

	return error;
}

/**
 * @set_default_key: set the default key on an interface
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static int rwnx_cfg80211_set_default_key(struct wiphy *wiphy,
					 struct net_device *netdev, int link_id,
					 u8 key_index, bool unicast,
					 bool multicast)
#else

static int rwnx_cfg80211_set_default_key(struct wiphy *wiphy,
					 struct net_device *netdev,
					 u8 key_index, bool unicast,
					 bool multicast)
#endif
{
	ENTER();

	return 0;
}

/**
 * @set_default_mgmt_key: set the default management frame key on an interface
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static int rwnx_cfg80211_set_default_mgmt_key(struct wiphy *wiphy,
					      struct net_device *netdev,
					      int link_id, u8 key_index)
#else
static int rwnx_cfg80211_set_default_mgmt_key(struct wiphy *wiphy,
					      struct net_device *netdev,
					      u8 key_index)
#endif
{
	return 0;
}

/**
 * @connect: Connect to the ESS with the specified parameters. When connected,
 *	call cfg80211_connect_result() with status code %WLAN_STATUS_SUCCESS.
 *	If the connection fails for some reason, call cfg80211_connect_result()
 *	with the status from the AP.
 *	(invoked with the wireless_dev mutex held)
 */
static int rwnx_cfg80211_connect(struct wiphy *wiphy, struct net_device *dev,
				 struct cfg80211_connect_params *sme)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct sm_connect_cfm sm_connect_cfm;
	int error = 0;

	if (rwnx_hw->core->hif_ops->hif == WQ_HIF_USB &&
	    rwnx_hw->connect_req_ts &&
	    !time_after(jiffies,
			rwnx_hw->connect_req_ts + msecs_to_jiffies(10000))) {
		WQ_DBG(DM_IEEE80211, DL_ERR,
		       "[auto]msg:ignore connect req, %lu < %lu", jiffies,
		       rwnx_hw->connect_req_ts + msecs_to_jiffies(10000));
		return -EINPROGRESS;
	}

	//ENTER();
	if (sme->bssid != NULL)
		WQ_DBG(DM_GENERIC, DL_INF, "rwnx_cfg80211_connect bssid=%pM\n",
		       sme->bssid);
	else
		WQ_DBG(DM_GENERIC, DL_INF,
		       "rwnx_cfg80211_connect bssid=NULL\n");

	if (sme->channel != NULL)
		WQ_DBG(DM_GENERIC, DL_INF, "rwnx_cfg80211_connect channel=%d\n",
		       sme->channel->center_freq);
	else
		WQ_DBG(DM_GENERIC, DL_INF,
		       "rwnx_cfg80211_connect channel=NULL\n");

	/* For SHARED-KEY authentication, must install key first */
	if (sme->key_len) {
		if (sme->crypto.cipher_group == WLAN_CIPHER_SUITE_WEP40 ||
		    sme->crypto.cipher_group == WLAN_CIPHER_SUITE_WEP104 ||
		    sme->crypto.ciphers_pairwise[0] ==
			    WLAN_CIPHER_SUITE_WEP40 ||
		    sme->crypto.ciphers_pairwise[0] ==
			    WLAN_CIPHER_SUITE_WEP104) {
			struct key_params key_params;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
			key_params.key = sme->key;
#else
			key_params.key = (u8 *)sme->key;
#endif
			key_params.seq = NULL;
			key_params.key_len = sme->key_len;
			key_params.seq_len = 0;
			key_params.cipher = sme->crypto.cipher_group;

#ifdef DEBUG_WQ_DFX
			/* update security info for debug */
			rwnx_vif->security.auth_type = sme->auth_type;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
			rwnx_cfg80211_add_key(wiphy, dev, 0, sme->key_idx,
					      false, NULL, &key_params);
#else
			rwnx_cfg80211_add_key(wiphy, dev, sme->key_idx, false,
					      NULL, &key_params);
#endif
		}
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0) ||                          \
	defined(CONFIG_EXTERNAL_AUTH_PATCH)
	else if ((sme->auth_type == NL80211_AUTHTYPE_SAE) &&
		 !(sme->flags & CONNECT_REQ_EXTERNAL_AUTH_SUPPORT)) {
		netdev_err(
			dev,
			"Doesn't support SAE without external authentication\n");
		return -EINVAL;
	}
#endif

#ifdef CONFIG_PM
	/* Send proto, akm, pairwise cipher and group cipher to fw */
	rwnx_send_secure_param_set(rwnx_hw, rwnx_vif->vif_index, sme);
#endif

	if (gv_get_pwr_from_bin_flag && (sme->channel != NULL)) {
		u8 connect_chan_pwr[PWR_TAB_LEN];
		int con_ret = 0;
		u8 band = sme->channel->band;
		u32 freq = sme->channel->center_freq;

		rwnx_store_chan_pwr_tab(rwnx_vif, band, freq, connect_chan_pwr);
		con_ret = rwnx_send_chan_pwr_info_req(
			rwnx_hw, rwnx_vif, connect_chan_pwr, band, freq);
		if (con_ret) {
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "%s, rwnx_send_chan_pwr_info_req faild\n",
			       __func__);
		}
	}

	/* Forward the information to the LMAC */
	mutex_lock(&rwnx_hw->mutex);
	if ((error = rwnx_send_sm_connect_req(rwnx_hw, rwnx_vif, sme,
					      &sm_connect_cfm))) {
		mutex_unlock(&rwnx_hw->mutex);
		return error;
	}

#ifdef CONFIG_HML
	if (rwnx_vif->is_hml) {
		return 0;
	}
#endif

	if (sm_connect_cfm.status != CO_OK) {
		//clear the ts if send connect cmd fail
		rwnx_hw->connect_req_ts = 0;
	}

	// Check the status
	switch (sm_connect_cfm.status) {
	case CO_OK:
#ifdef DEBUG_WQ_DFX
		/* update debug info, clear cipher and update auth type */
		rwnx_vif->security.auth_type = sme->auth_type;
		wq_dbg_update_security_info(&rwnx_vif->security, 0xff, 0,
					    MAC_CIPHER_INVALID);
#endif
		rwnx_save_assoc_info_for_ft(rwnx_vif, sme);
		error = 0;
		break;
	case CO_BUSY:
		error = -EINPROGRESS;
		break;
	case CO_BAD_PARAM:
		error = -EINVAL;
		break;
	case CO_OP_IN_PROGRESS:
		error = -EALREADY;
		break;
	default:
		error = -EIO;
		break;
	}

	return error;
}

/**
 * @disconnect: Disconnect from the BSS/ESS.
 *	(invoked with the wireless_dev mutex held)
 */
static int rwnx_cfg80211_disconnect(struct wiphy *wiphy, struct net_device *dev,
				    u16 reason_code)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct wireless_dev *wdev = &rwnx_vif->wdev;

	ENTER();
	WQ_DBG(DM_GENERIC, DL_WRN, WQ_FN_ENTRY_STR);

	/*
     * Clear ssid_len unless we actually were fully connected,
     * in which case cfg80211_disconnected() will take care of
     * this later.
     */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
	if (!wdev->u.ibss.current_bss)
		wdev->u.client.ssid_len = 0;
#else
	if (!wdev->current_bss)
		wdev->ssid_len = 0;
#endif

	//del key_add_timer in case of disconnect
	del_timer(&rwnx_hw->key_add_timer);

	return (rwnx_send_sm_disconnect_req(rwnx_hw, rwnx_vif, reason_code));
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0) ||                          \
	defined(CONFIG_EXTERNAL_AUTH_PATCH)
/**
 * @external_auth: indicates result of offloaded authentication processing from
 *     user space
 */
static int
rwnx_cfg80211_external_auth(struct wiphy *wiphy, struct net_device *dev,
			    struct cfg80211_external_auth_params *params)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);

	if (!(rwnx_vif->sta.flags & RWNX_STA_EXT_AUTH))
		return -EINVAL;

	rwnx_external_auth_disable(rwnx_vif);
	return rwnx_send_sm_external_auth_required_rsp(rwnx_hw, rwnx_vif,
						       params->status);
}
#endif

int rwnx_get_sta_bandwidth(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta)
{
	struct rwnx_chanctx *ctxt;
	int sta_bw, local_sup_bw, peer_sup_bw = PHY_CHNL_BW_20;

	BUG_ON(sta->ch_idx == RWNX_CH_NOT_SET);
	ctxt = &rwnx_hw->chanctx_table[sta->ch_idx];
	local_sup_bw = bw2chnl[ctxt->chan_def.width];

	if (sta->vht_cap_info) {
		switch ((sta->vht_cap_info &
			 IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK) >>
			2) {
		case 0:
			peer_sup_bw = PHY_CHNL_BW_80;
			break;
		case 1:
			peer_sup_bw = PHY_CHNL_BW_160;
			break;
		case 2:
			peer_sup_bw = PHY_CHNL_BW_80P80;
			break;
		default:
			/*VHT40 or VHT20*/
			peer_sup_bw = (sta->ht_cap_info &
				       IEEE80211_HT_CAP_SUP_WIDTH_20_40) ?
						    PHY_CHNL_BW_40 :
						    PHY_CHNL_BW_20;
			break;
		}
	} else if (sta->ht_cap_info) {
		if (sta->ht_cap_info & IEEE80211_HT_CAP_SUP_WIDTH_20_40)
			peer_sup_bw = PHY_CHNL_BW_40;
	}

	sta_bw = peer_sup_bw > local_sup_bw ? local_sup_bw : peer_sup_bw;
	return sta_bw;
}

static void rwnx_set_station_capability(struct rwnx_hw *rwnx_hw,
					struct rwnx_sta *sta,
					struct station_parameters *params)
{
	int i;

	sta->format_mod = FORMATMOD_NON_HT;
	sta->legacy_rate_idx = 12;
	sta->rx_nss = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
	if (params->link_sta_params.ht_capa) {
		const struct ieee80211_ht_cap *ht_capa =
			params->link_sta_params.ht_capa;
#else
	if (params->ht_capa) {
		const struct ieee80211_ht_cap *ht_capa = params->ht_capa;
#endif

		sta->format_mod = FORMATMOD_HT_MF;
		if (ht_capa->mcs.rx_mask[0] & 0xFF)
			sta->rx_mcs_idx = 7;
		if (ht_capa->mcs.rx_mask[0])
			sta->rx_nss++;
		if (ht_capa->mcs.rx_mask[1])
			sta->rx_nss++;
		if (ht_capa->mcs.rx_mask[2])
			sta->rx_nss++;
		if (ht_capa->mcs.rx_mask[3])
			sta->rx_nss++;
		sta->rx_mcs_idx |= (sta->rx_nss - 1) << 3;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
	if (params->link_sta_params.vht_capa) {
		const struct ieee80211_vht_cap *vht_capa =
			params->link_sta_params.vht_capa;
#else
	if (params->vht_capa) {
		const struct ieee80211_vht_cap *vht_capa = params->vht_capa;
#endif
		u16 rx_mcs_map;
		u8 rx_mcs_mask;

		sta->format_mod = FORMATMOD_VHT;
		rx_mcs_map = le16_to_cpu(vht_capa->supp_mcs.rx_mcs_map);
		for (i = 7; i >= 0; i--) {
			rx_mcs_mask = (rx_mcs_map >> (2 * i)) & 3;

			if (rx_mcs_mask != IEEE80211_VHT_MCS_NOT_SUPPORTED) {
				sta->rx_mcs_idx =
					vht_mcs_map_to_mcs_val(rx_mcs_mask);
				sta->rx_nss = i + 1;
				break;
			}
		}
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
		u8 rx_mcs_map;
		u16 mcs_map_rx_80;
		u8 nss;

		struct ieee80211_he_mcs_nss_supp *mcs_nss_supp =
			(struct ieee80211_he_mcs_nss_supp *)(he_capa + 1);

		sta->format_mod = FORMATMOD_HE_SU;
		mcs_map_rx_80 = le16_to_cpu(mcs_nss_supp->rx_mcs_80);
		for (nss = 7; nss >= 0; nss--) {
			rx_mcs_map = (mcs_map_rx_80 >> (2 * nss)) & 3;
			if (rx_mcs_map != IEEE80211_HE_MCS_NOT_SUPPORTED) {
				sta->rx_mcs_idx =
					he_mcs_map_to_mcs_max(rx_mcs_map);
				sta->rx_nss = nss + 1;
				break;
			}
		}
	}
#endif

	sta->width = rwnx_get_sta_bandwidth(rwnx_hw, sta);
}

void rwnx_reset_sta_stats(struct rwnx_sta *sta)
{
	sta->stats.rx_bytes = 0;
	sta->stats.tx_bytes = 0;
	sta->stats.tx_pkts = 0;
	sta->stats.rx_pkts = 0;
	memset(&sta->stats.tx_info, 0, sizeof(struct target_tx_info));
}

/**
 * @add_station: Add a new station.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
static int rwnx_cfg80211_add_station(struct wiphy *wiphy,
				     struct net_device *dev, const u8 *mac,
				     struct station_parameters *params)
#else
static int rwnx_cfg80211_add_station(struct wiphy *wiphy,
				     struct net_device *dev, u8 *mac,
				     struct station_parameters *params)
#endif
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct me_sta_add_cfm me_sta_add_cfm;
	struct station_info *sinfo;
	int error = 0;

	ENTER();

	WARN_ON(RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_AP_VLAN);

	/* Do not add TDLS station */
	if (params->sta_flags_set & BIT(NL80211_STA_FLAG_TDLS_PEER))
		return 0;

	/* Indicate we are in a STA addition process - This will allow handling
     * potential PS mode change indications correctly
     */
	set_bit(RWNX_DEV_ADDING_STA, &rwnx_hw->flags);

	/* Forward the information to the LMAC */
	if ((error = rwnx_send_me_sta_add(rwnx_hw, params, mac,
					  rwnx_vif->vif_index,
					  &me_sta_add_cfm)))
		return error;

	// Check the status
	switch (me_sta_add_cfm.status) {
	case CO_OK: {
		struct rwnx_sta *sta =
			&rwnx_hw->sta_table[me_sta_add_cfm.sta_idx];
		int tid;
		sta->aid = params->aid;

		sta->sta_idx = me_sta_add_cfm.sta_idx;
		sta->ch_idx = rwnx_vif->ch_index;
		sta->vif_idx = rwnx_vif->vif_index;
		sta->vlan_idx = sta->vif_idx;
		sta->qos = (params->sta_flags_set &
			    BIT(NL80211_STA_FLAG_WME)) != 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
		sta->ht_cap_info =
			params->link_sta_params.ht_capa ?
				      params->link_sta_params.ht_capa->cap_info :
				      0;
		sta->vht_cap_info =
			params->link_sta_params.vht_capa ?
				      params->link_sta_params.vht_capa->vht_cap_info :
				      0;
#else
		sta->ht_cap_info =
			params->ht_capa ? params->ht_capa->cap_info : 0;
		sta->vht_cap_info =
			params->vht_capa ? params->vht_capa->vht_cap_info : 0;
#endif
		if (sta->ht_cap_info) {
			rwnx_set_sta_amsdu_len_from_htcap(sta,
							  sta->ht_cap_info);
		}

		if (sta->vht_cap_info) {
			rwnx_set_sta_amsdu_len_from_vhtcap(sta,
							   sta->vht_cap_info);
		}

		rwnx_set_station_capability(rwnx_hw, sta, params);

		sta->acm = 0;
		sta->listen_interval = params->listen_interval;

		if (params->local_pm != NL80211_MESH_POWER_UNKNOWN)
			sta->mesh_pm = params->local_pm;
		else
			sta->mesh_pm = rwnx_vif->ap.next_mesh_pm;
		rwnx_update_mesh_power_mode(rwnx_vif);

		for (tid = 0; tid < NX_NB_TXQ_PER_STA; tid++) {
			int uapsd_bit = rwnx_hwq2uapsd[rwnx_tid2hwq[tid]];
			if (params->uapsd_queues & uapsd_bit)
				sta->uapsd_tids |= 1 << tid;
			else
				sta->uapsd_tids &= ~(1 << tid);
		}
		memcpy(sta->mac_addr, mac, ETH_ALEN);
		rwnx_reset_sta_stats(sta);
		rwnx_dbgfs_register_sta(rwnx_hw, sta);

		/* Ensure that we won't process PS change or channel switch ind*/
		spin_lock_bh(&rwnx_hw->cb_lock);
		rwnx_txq_sta_init(rwnx_hw, sta,
				  rwnx_txq_vif_get_status(rwnx_vif));
		list_add_tail(&sta->list, &rwnx_vif->ap.sta_list);
		atomic_inc(&rwnx_vif->ap.sta_num);
		rwnx_vif->generation++;
		sta->valid = true;
		rwnx_ps_bh_enable(rwnx_hw, sta,
				  sta->ps.active || me_sta_add_cfm.pm_state);
		spin_unlock_bh(&rwnx_hw->cb_lock);

		error = 0;

#ifdef CONFIG_RWNX_BFMER
		if (rwnx_hw->mod_params.bfmer)
			rwnx_send_bfmer_enable(rwnx_hw, sta, params->vht_capa);

		rwnx_mu_group_sta_init(sta, params->vht_capa);
#endif /* CONFIG_RWNX_BFMER */

		sinfo = kzalloc(sizeof(*sinfo), GFP_KERNEL);
		if (sinfo) {
			rwnx_fill_station_info(sta, rwnx_vif, sinfo);
			cfg80211_new_sta(dev, mac, sinfo, GFP_KERNEL);
			kfree(sinfo);
		}

#define PRINT_STA_FLAG(f)                                                      \
	(params->sta_flags_set & BIT(NL80211_STA_FLAG_##f) ? "[" #f "]" : "")

		netdev_info(dev, "Add sta %d (%pM) flags=%s%s%s%s%s%s%s",
			    sta->sta_idx, mac, PRINT_STA_FLAG(AUTHORIZED),
			    PRINT_STA_FLAG(SHORT_PREAMBLE), PRINT_STA_FLAG(WME),
			    PRINT_STA_FLAG(MFP), PRINT_STA_FLAG(AUTHENTICATED),
			    PRINT_STA_FLAG(TDLS_PEER),
			    PRINT_STA_FLAG(ASSOCIATED));
#undef PRINT_STA_FLAG
		break;
	}
	default:
		error = -EBUSY;
		break;
	}

	clear_bit(RWNX_DEV_ADDING_STA, &rwnx_hw->flags);

	return error;
}

/**
 * @del_station: Remove a station
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
static int rwnx_cfg80211_del_station(struct wiphy *wiphy,
				     struct net_device *dev,
				     struct station_del_parameters *params)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
static int rwnx_cfg80211_del_station(struct wiphy *wiphy,
				     struct net_device *dev, const u8 *mac)
#else
static int rwnx_cfg80211_del_station(struct wiphy *wiphy,
				     struct net_device *dev, u8 *mac)
#endif
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_sta *cur, *tmp;
	int error = 0, found = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
	const u8 *mac = NULL;
#endif
#ifdef CONFIG_HML
	u16 reason_code __attribute__((unused)) = 0;
#endif

	ENTER();

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
	if (params) {
		mac = params->mac;
#ifdef CONFIG_HML
		reason_code = params->reason_code;
#endif
	}
#endif
	list_for_each_entry_safe (cur, tmp, &rwnx_vif->ap.sta_list, list) {
		if ((!mac) || (!memcmp(cur->mac_addr, mac, ETH_ALEN))) {
			netdev_info(dev, "Del sta %d (%pM)", cur->sta_idx,
				    cur->mac_addr);
			extap_tbl_del(cur->mac_addr);
			/* Ensure that we won't process PS change ind */
			spin_lock_bh(&rwnx_hw->cb_lock);
			cur->ps.active = false;
			cur->valid = false;
			spin_unlock_bh(&rwnx_hw->cb_lock);

			if (cur->vif_idx != cur->vlan_idx) {
				struct rwnx_vif *vlan_vif;
				vlan_vif = rwnx_hw->vif_table[cur->vlan_idx];
				if (vlan_vif->up) {
					if ((RWNX_VIF_TYPE(vlan_vif) ==
					     NL80211_IFTYPE_AP_VLAN) &&
					    (vlan_vif->use_4addr)) {
						vlan_vif->ap_vlan.sta_4a = NULL;
					} else {
						WARN(1,
						     "Deleting sta belonging to VLAN other than AP_VLAN 4A");
					}
				}
			}
			rwnx_txq_sta_deinit(rwnx_hw, cur);
#ifdef CONFIG_HML
#ifdef DEBUG_WQ_PRIV
			if (rwnx_vif->is_hml) {
				error = rwnx_send_vendor_sta_del(rwnx_hw,
								 rwnx_vif,
								 cur->mac_addr,
								 reason_code);
			} else
#endif
#endif
			{
				error = rwnx_send_me_sta_del(
					rwnx_hw, cur->sta_idx, false);
				if ((rwnx_hw->core->hif_ops->hif ==
				     WQ_HIF_USB) &&
				    (error != 0)) {
					WQ_DBG(DM_GENERIC, DL_WRN,
					       "WARN: skip send_me_sta_del error %d\n",
					       error);
					error = 0;
				}
			}
			if ((error != 0) && (error != -EPIPE) &&
			    (error != -ENXIO))
				return error;

#ifdef CONFIG_RWNX_BFMER
			// Disable Beamformer if supported
			rwnx_bfmer_report_del(rwnx_hw, cur);
			rwnx_mu_group_sta_del(rwnx_hw, cur);
#endif /* CONFIG_RWNX_BFMER */

			if (mac) {
				cfg80211_del_sta(dev, mac, GFP_KERNEL);
			}

			list_del(&cur->list);
			atomic_dec(&rwnx_vif->ap.sta_num);
			rwnx_vif->generation++;
			rwnx_dbgfs_unregister_sta(rwnx_hw, cur);
			found++;
			break;
		}
	}

	if (!found)
		return -ENOENT;

	rwnx_update_mesh_power_mode(rwnx_vif);

	return 0;
}

/**
 * @change_station: Modify a given station. Note that flags changes are not much
 *	validated in cfg80211, in particular the auth/assoc/authorized flags
 *	might come to the driver in invalid combinations -- make sure to check
 *	them, also against the existing state! Drivers must call
 *	cfg80211_check_station_change() to validate the information.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
static int rwnx_cfg80211_change_station(struct wiphy *wiphy,
					struct net_device *dev, const u8 *mac,
					struct station_parameters *params)
#else
static int rwnx_cfg80211_change_station(struct wiphy *wiphy,
					struct net_device *dev, u8 *mac,
					struct station_parameters *params)
#endif
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *vif = netdev_priv(dev);
	struct rwnx_sta *sta;

	sta = rwnx_get_sta(rwnx_hw, mac);
	if (!sta) {
		/* Add the TDLS station */
		if (params->sta_flags_set & BIT(NL80211_STA_FLAG_TDLS_PEER)) {
			struct rwnx_vif *rwnx_vif = netdev_priv(dev);
			struct me_sta_add_cfm me_sta_add_cfm;
			int error = 0;

			/* Indicate we are in a STA addition process - This will allow handling
             * potential PS mode change indications correctly
             */
			set_bit(RWNX_DEV_ADDING_STA, &rwnx_hw->flags);

			/* Forward the information to the LMAC */
			if ((error = rwnx_send_me_sta_add(rwnx_hw, params, mac,
							  rwnx_vif->vif_index,
							  &me_sta_add_cfm)))
				return error;

			// Check the status
			switch (me_sta_add_cfm.status) {
			case CO_OK: {
				int tid;
				sta = &rwnx_hw->sta_table[me_sta_add_cfm.sta_idx];
				sta->aid = params->aid;
				sta->sta_idx = me_sta_add_cfm.sta_idx;
				sta->ch_idx = rwnx_vif->ch_index;
				sta->vif_idx = rwnx_vif->vif_index;
				sta->vlan_idx = sta->vif_idx;
				sta->qos = (params->sta_flags_set &
					    BIT(NL80211_STA_FLAG_WME)) != 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
				sta->ht_cap_info =
					params->link_sta_params.ht_capa ?
						      params->link_sta_params.ht_capa
							->cap_info :
						      0;
				sta->vht_cap_info =
					params->link_sta_params.vht_capa ?
						      params->link_sta_params
							.vht_capa->vht_cap_info :
						      0;
#else
				sta->ht_cap_info =
					params->ht_capa ?
						      params->ht_capa->cap_info :
						      0;
				sta->vht_cap_info =
					params->vht_capa ?
						      params->vht_capa->vht_cap_info :
						      0;
#endif

				rwnx_set_station_capability(rwnx_hw, sta,
							    params);

				sta->acm = 0;
				for (tid = 0; tid < NX_NB_TXQ_PER_STA; tid++) {
					int uapsd_bit =
						rwnx_hwq2uapsd[rwnx_tid2hwq[tid]];
					if (params->uapsd_queues & uapsd_bit)
						sta->uapsd_tids |= 1 << tid;
					else
						sta->uapsd_tids &= ~(1 << tid);
				}
				memcpy(sta->mac_addr, mac, ETH_ALEN);
				rwnx_reset_sta_stats(sta);
				rwnx_dbgfs_register_sta(rwnx_hw, sta);

				/* Ensure that we won't process PS change or channel switch ind*/
				spin_lock_bh(&rwnx_hw->cb_lock);
				rwnx_txq_sta_init(
					rwnx_hw, sta,
					rwnx_txq_vif_get_status(rwnx_vif));
				if (rwnx_vif->tdls_status ==
				    TDLS_SETUP_RSP_TX) {
					rwnx_vif->tdls_status =
						TDLS_LINK_ACTIVE;
					sta->tdls.initiator = true;
					sta->tdls.active = true;
				}
				/* Set TDLS channel switch capability */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
				if ((params->ext_capab[3] &
				     WLAN_EXT_CAPA4_TDLS_CHAN_SWITCH) &&
				    !rwnx_vif->tdls_chsw_prohibited)
					sta->tdls.chsw_allowed = true;
#endif
				rwnx_vif->sta.tdls_sta = sta;
				sta->valid = true;
				spin_unlock_bh(&rwnx_hw->cb_lock);
#ifdef CONFIG_RWNX_BFMER
				if (rwnx_hw->mod_params.bfmer)
					rwnx_send_bfmer_enable(
						rwnx_hw, sta, params->vht_capa);

				rwnx_mu_group_sta_init(sta, NULL);
#endif /* CONFIG_RWNX_BFMER */

#define PRINT_STA_FLAG(f)                                                      \
	(params->sta_flags_set & BIT(NL80211_STA_FLAG_##f) ? "[" #f "]" : "")

				netdev_info(
					dev,
					"Add %s TDLS sta %d (%pM) flags=%s%s%s%s%s%s%s",
					sta->tdls.initiator ? "initiator" :
								    "responder",
					sta->sta_idx, mac,
					PRINT_STA_FLAG(AUTHORIZED),
					PRINT_STA_FLAG(SHORT_PREAMBLE),
					PRINT_STA_FLAG(WME),
					PRINT_STA_FLAG(MFP),
					PRINT_STA_FLAG(AUTHENTICATED),
					PRINT_STA_FLAG(TDLS_PEER),
					PRINT_STA_FLAG(ASSOCIATED));
#undef PRINT_STA_FLAG

				break;
			}
			default:
				error = -EBUSY;
				break;
			}

			clear_bit(RWNX_DEV_ADDING_STA, &rwnx_hw->flags);
		} else {
			return -EINVAL;
		}
	}

	if (params->sta_flags_mask & BIT(NL80211_STA_FLAG_AUTHORIZED))
		rwnx_send_me_set_control_port_req(
			rwnx_hw,
			(params->sta_flags_set &
			 BIT(NL80211_STA_FLAG_AUTHORIZED)) != 0,
			sta->sta_idx);

	if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_MESH_POINT) {
		if (params->sta_modify_mask & STATION_PARAM_APPLY_PLINK_STATE) {
			if (params->plink_state < NUM_NL80211_PLINK_STATES) {
				rwnx_send_mesh_peer_update_ntf(
					rwnx_hw, vif, sta->sta_idx,
					params->plink_state);
			}
		}

		if (params->local_pm != NL80211_MESH_POWER_UNKNOWN) {
			sta->mesh_pm = params->local_pm;
			rwnx_update_mesh_power_mode(vif);
		}
	}

	if (params->vlan) {
		uint8_t vlan_idx;

		vif = netdev_priv(params->vlan);
		vlan_idx = vif->vif_index;

		if (sta->vlan_idx != vlan_idx) {
			struct rwnx_vif *old_vif;
			old_vif = rwnx_hw->vif_table[sta->vlan_idx];
			rwnx_txq_sta_switch_vif(sta, old_vif, vif);
			sta->vlan_idx = vlan_idx;

			if ((RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_AP_VLAN) &&
			    (vif->use_4addr)) {
				WARN((vif->ap_vlan.sta_4a),
				     "4A AP_VLAN interface with more than one sta");
				vif->ap_vlan.sta_4a = sta;
			}

			if ((RWNX_VIF_TYPE(old_vif) ==
			     NL80211_IFTYPE_AP_VLAN) &&
			    (old_vif->use_4addr)) {
				old_vif->ap_vlan.sta_4a = NULL;
			}
		}
	}

	return 0;
}

/**
 * @start_ap: Start acting in AP mode defined by the parameters.
 */
static int rwnx_cfg80211_start_ap(struct wiphy *wiphy, struct net_device *dev,
				  struct cfg80211_ap_settings *settings)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct apm_start_cfm apm_start_cfm;
	struct rwnx_ipc_elem_var elem;
	struct rwnx_sta *sta;
	int error = 0;

	WQ_DBG(DM_GENERIC, DL_WRN, "%s\n", __func__);

	/* Forward the information to the LMAC */
	if ((error = rwnx_send_apm_start_req(rwnx_hw, rwnx_vif, settings,
					     &apm_start_cfm, &elem)))
		goto end;

	// Check the status
	switch (apm_start_cfm.status) {
	case CO_OK: {
		u8 txq_status = 0;
		rwnx_vif->ap.bcmc_index = apm_start_cfm.bcmc_idx;
		rwnx_vif->ap.flags |= RWNX_AP_STARTED;
		rwnx_vif->ap.bcn_interval = settings->beacon_interval;
		sta = &rwnx_hw->sta_table[apm_start_cfm.bcmc_idx];
		sta->valid = true;
		sta->aid = 0;
		sta->sta_idx = apm_start_cfm.bcmc_idx;
		sta->ch_idx = apm_start_cfm.ch_idx;
		sta->vif_idx = rwnx_vif->vif_index;
		sta->qos = false;
		sta->acm = 0;
		sta->ps.active = false;
		sta->listen_interval = 5;
		rwnx_mu_group_sta_init(sta, NULL);
		spin_lock_bh(&rwnx_hw->cb_lock);
		rwnx_chanctx_link(rwnx_vif, apm_start_cfm.ch_idx,
				  &rwnx_vif->ap.chandef);
		if (rwnx_hw->cur_chanctx != apm_start_cfm.ch_idx) {
			txq_status = RWNX_TXQ_STOP_CHAN;
		}
		rwnx_txq_vif_init(rwnx_hw, rwnx_vif, txq_status);
		spin_unlock_bh(&rwnx_hw->cb_lock);

		netif_tx_start_all_queues(dev);
		netif_carrier_on(dev);
		error = 0;
		/* If the AP channel is already the active, we probably skip radar
               activation on MM_CHANNEL_SWITCH_IND (unless another vif use this
               ctxt). In anycase retest if radar detection must be activated
             */
		if (txq_status == 0) {
			rwnx_radar_detection_enable_on_cur_channel(rwnx_hw);
		}
#ifdef DEBUG_WQ_DFX
		/* update debug info, clear cipher and update auth type */
		rwnx_vif->security.auth_type = settings->auth_type;
		wq_dbg_update_security_info(&rwnx_vif->security, 0xff, 0,
					    MAC_CIPHER_INVALID);
#endif
		break;
	}
	case CO_BUSY:
		error = -EINPROGRESS;
		break;
	case CO_OP_IN_PROGRESS:
		error = -EALREADY;
		break;
	default:
		error = -EIO;
		break;
	}

	if (error) {
		netdev_info(dev, "Failed to start AP (%d)", error);
	} else {
		netdev_info(dev, "AP started: ch=%d, bcmc_idx=%d",
			    rwnx_vif->ch_index, rwnx_vif->ap.bcmc_index);
	}

end:
	// rwnx_ipc_elem_var_deallocs(rwnx_hw, &elem);
	if (gv_get_pwr_from_bin_flag) {
		u8 pwr_tab[PWR_TAB_LEN];
		u32 sap_freq = rwnx_vif->ap.chandef.chan->center_freq;
		u8 sap_band = rwnx_vif->ap.chandef.chan->band;
		int pwr_ret = 0;

		rwnx_store_chan_pwr_tab(rwnx_vif, sap_band, sap_freq, pwr_tab);
		pwr_ret = rwnx_send_chan_pwr_info_req(
			rwnx_hw, rwnx_vif, pwr_tab, sap_band, sap_freq);
		if (pwr_ret) {
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "%s, rwnx_send_chan_pwr_info_req faild\n",
			       __func__);
		}
	}

	if (error)
		rwnx_del_chan(rwnx_vif);

	return error;
}

/**
 * @change_beacon: Change the beacon parameters for an access point mode
 *	interface. This should reject the call when AP mode wasn't started.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
static int rwnx_cfg80211_change_beacon(struct wiphy *wiphy,
				       struct net_device *dev,
				       struct cfg80211_ap_update *upd)
{
	struct cfg80211_beacon_data *info = &upd->beacon;
#else
static int rwnx_cfg80211_change_beacon(struct wiphy *wiphy,
				       struct net_device *dev,
				       struct cfg80211_beacon_data *info)
{
#endif
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *vif = netdev_priv(dev);
	struct rwnx_bcn *bcn = &vif->ap.bcn;

	// struct rwnx_ipc_elem_var elem;
	u8 *buf;
	int error = 0;

	ENTER();

	// Build the beacon
	buf = rwnx_build_bcn(bcn, info);
	if (!buf)
		return -ENOMEM;

	// Sync buffer for FW
	//if ((error = rwnx_ipc_elem_var_allocs(rwnx_hw, &elem, bcn->len, DMA_TO_DEVICE,
	//                                      buf, NULL, NULL)))
	//    return error;

	// Forward the information to the LMAC
	error = rwnx_send_bcn_change(rwnx_hw, vif->vif_index, buf, bcn->len,
				     bcn->head_len, bcn->tim_len, NULL);

	//rwnx_ipc_elem_var_deallocs(rwnx_hw, &elem);
	if (buf) {
		kfree(buf);
		buf = NULL;
	}
	return error;
}

/**
 * * @stop_ap: Stop being an AP, including stopping beaconing.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 19, 2)
static int rwnx_cfg80211_stop_ap(struct wiphy *wiphy, struct net_device *dev,
				 unsigned int link_id)
#else
static int rwnx_cfg80211_stop_ap(struct wiphy *wiphy, struct net_device *dev)
#endif
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_sta *sta;

	WQ_DBG(DM_GENERIC, DL_WRN, "%s\n", __func__);

	// Wait AP mgmt txdone EVT
	if (rwnx_hw->ap_mgt_tx_ongoing) {
		msleep(200);
	}
	mutex_lock(&rwnx_hw->mutex);
	rwnx_radar_cancel_cac(&rwnx_hw->radar);
	rwnx_send_apm_stop_req(rwnx_hw, rwnx_vif);
	spin_lock_bh(&rwnx_hw->cb_lock);
	rwnx_chanctx_unlink(rwnx_vif);
	spin_unlock_bh(&rwnx_hw->cb_lock);
	mutex_unlock(&rwnx_hw->mutex);

	netif_tx_stop_all_queues(dev);
	netif_carrier_off(dev);

	/* delete any remaining STA*/
	while (!list_empty(&rwnx_vif->ap.sta_list)) {
		rwnx_cfg80211_del_station(wiphy, dev, NULL);
	}

	/* delete BC/MC STA */
	sta = &rwnx_hw->sta_table[rwnx_vif->ap.bcmc_index];
	rwnx_txq_vif_deinit(rwnx_hw, rwnx_vif);
	rwnx_del_bcn(&rwnx_vif->ap.bcn);
	rwnx_del_csa(rwnx_vif);
	rwnx_del_chan(rwnx_vif);
	rwnx_vif->ap.flags &= ~RWNX_AP_STARTED;

	netdev_info(dev, "AP Stopped");

	return 0;
}

/**
 * @set_monitor_channel: Set the monitor mode channel for the device. If other
 *	interfaces are active this callback should reject the configuration.
 *	If no interfaces are active or the device is down, the channel should
 *	be stored for when a monitor interface becomes active.
 *
 * Also called internaly with chandef set to NULL simply to retrieve the channel
 * configured at firmware level.
 */
static int rwnx_cfg80211_set_monitor_channel(struct wiphy *wiphy,
					     struct cfg80211_chan_def *chandef)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif;
	struct me_config_monitor_cfm cfm;
	ENTER();

	if (rwnx_hw->monitor_vif == RWNX_INVALID_VIF)
		return -EINVAL;

	rwnx_vif = rwnx_hw->vif_table[rwnx_hw->monitor_vif];

	// Do nothing if monitor interface is already configured with the requested channel
	if (rwnx_chanctx_valid(rwnx_hw, rwnx_vif->ch_index)) {
		struct rwnx_chanctx *ctxt;
		ctxt = &rwnx_vif->rwnx_hw->chanctx_table[rwnx_vif->ch_index];
		if (chandef &&
		    cfg80211_chandef_identical(&ctxt->chan_def, chandef))
			return 0;
	}

	// Always send command to firmware. It allows to retrieve channel context index
	// and its configuration.
	if (rwnx_send_config_monitor_req(rwnx_hw, chandef, &cfm))
		return -EIO;

	// Always re-set channel context info
	rwnx_chanctx_unlink(rwnx_vif);

	// If there is also a STA interface not yet connected then monitor interface
	// will only have a channel context after the connection of the STA interface.
	if (cfm.chan_index != RWNX_CH_NOT_SET) {
		struct cfg80211_chan_def mon_chandef;

		if (rwnx_hw->vif_started > 1) {
			// In this case we just want to update the channel context index not
			// the channel configuration
			rwnx_chanctx_link(rwnx_vif, cfm.chan_index, NULL);
			return -EBUSY;
		}

		memset(&mon_chandef, 0, sizeof(mon_chandef));
		mon_chandef.chan =
			ieee80211_get_channel(wiphy, cfm.chan.prim20_freq);
		mon_chandef.center_freq1 = cfm.chan.center1_freq;
		mon_chandef.center_freq2 = cfm.chan.center2_freq;
		mon_chandef.width = chnl2bw[cfm.chan.type];
		rwnx_chanctx_link(rwnx_vif, cfm.chan_index, &mon_chandef);
	}

	return 0;
}

/**
 * @probe_client: probe an associated client, must return a cookie that it
 *	later passes to cfg80211_probe_status().
 */
int rwnx_cfg80211_probe_client(struct wiphy *wiphy, struct net_device *dev,
			       const u8 *peer, u64 *cookie)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *vif = netdev_priv(dev);
	struct rwnx_sta *sta = NULL;
	struct apm_probe_client_cfm cfm;

	ENTER();

	if ((RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_AP) &&
	    (RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_AP_VLAN) &&
	    (RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_P2P_GO) &&
	    (RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_MESH_POINT))
		return -EINVAL;

	list_for_each_entry (sta, &vif->ap.sta_list, list) {
		if (sta->valid && ether_addr_equal(sta->mac_addr, peer))
			break;
	}

	if (!sta)
		return -EINVAL;

	rwnx_send_apm_probe_req(rwnx_hw, vif, sta, &cfm);

	if (cfm.status != CO_OK)
		return -EINVAL;

	*cookie = (u64)cfm.probe_id;
	return 0;
}

/**
 * @set_wiphy_params: Notify that wiphy parameters have changed;
 *	@changed bitfield (see &enum wiphy_params_flags) describes which values
 *	have changed. The actual parameter values are available in
 *	struct wiphy. If returning an error, no value should be changed.
 */
static int rwnx_cfg80211_set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
	return 0;
}

/**
 * @set_tx_power: set the transmit power according to the parameters,
 *	the power passed is in mBm, to get dBm use MBM_TO_DBM(). The
 *	wdev may be %NULL if power was set for the wiphy, and will
 *	always be %NULL unless the driver supports per-vif TX power
 *	(as advertised by the nl80211 feature flag.)
 */
static int rwnx_cfg80211_set_tx_power(struct wiphy *wiphy,
				      struct wireless_dev *wdev,
				      enum nl80211_tx_power_setting type,
				      int mbm)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *vif;
	s8 pwr;
	int res = 0;

	if (type == NL80211_TX_POWER_AUTOMATIC) {
		pwr = 0x7f;
	} else {
		pwr = MBM_TO_DBM(mbm);
	}

	if (wdev) {
		vif = container_of(wdev, struct rwnx_vif, wdev);
		res = rwnx_send_set_power(rwnx_hw, vif->vif_index, pwr, NULL);
	} else {
		list_for_each_entry (vif, &rwnx_hw->vifs, list) {
			res = rwnx_send_set_power(rwnx_hw, vif->vif_index, pwr,
						  NULL);
			if (res)
				break;
		}
	}

	return res;
}

/**
 * @set_power_mgmt: set the power save to one of those two modes:
 *  Power-save off
 *  Power-save on - Dynamic mode
 */
static int rwnx_cfg80211_set_power_mgmt(struct wiphy *wiphy,
					struct net_device *dev, bool enabled,
					int timeout)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	u8 ps_mode;
	int ret;

	ENTER();
	if (timeout >= 0)
		netdev_info(dev, "Ignore timeout value %d", timeout);

	if (!(rwnx_hw->version_cfm.features & BIT(MM_FEAT_PS_BIT)))
		enabled = false;

	//disable PS mode is ps_disable:1
	if (rwnx_hw->feature.ps_disable) {
		enabled = false;
	}

	if (enabled) {
		/* Switch to Dynamic Power Save */
		ps_mode = PS_MODE_ON_DYN;
	} else {
		/* Exit Power Save */
		ps_mode = PS_MODE_OFF;
	}

	if (rwnx_hw->vif_started > 1)
		ps_mode = PS_MODE_OFF;

	mutex_lock(&rwnx_hw->mutex);
	ret = rwnx_send_me_set_ps_mode(rwnx_hw, ps_mode);
	mutex_unlock(&rwnx_hw->mutex);

	return ret;
}

/**
 * @set_txq_params: Set TX queue parameters
 */
static int rwnx_cfg80211_set_txq_params(struct wiphy *wiphy,
					struct net_device *dev,
					struct ieee80211_txq_params *params)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	u8 hw_queue, aifs, cwmin, cwmax;
	u32 param;

	ENTER();

	hw_queue = rwnx_ac2hwq[0][params->ac];

	aifs = params->aifs;
	cwmin = fls(params->cwmin);
	cwmax = fls(params->cwmax);

	/* Store queue information in general structure */
	param = (u32)(aifs << 0);
	param |= (u32)(cwmin << 4);
	param |= (u32)(cwmax << 8);
	param |= (u32)(params->txop) << 12;

	/* Send the MM_SET_EDCA_REQ message to the FW */
	return rwnx_send_set_edca(rwnx_hw, hw_queue, param, false,
				  rwnx_vif->vif_index);
}

/**
 * @remain_on_channel: Request the driver to remain awake on the specified
 *	channel for the specified duration to complete an off-channel
 *	operation (e.g., public action frame exchange). When the driver is
 *	ready on the requested channel, it must indicate this with an event
 *	notification by calling cfg80211_ready_on_channel().
 */
static int rwnx_cfg80211_remain_on_channel(struct wiphy *wiphy,
					   struct wireless_dev *wdev,
					   struct ieee80211_channel *chan,
					   unsigned int duration, u64 *cookie)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(wdev->netdev);
	struct rwnx_roc *roc;
	int error;

	//ENTER();

	WQ_DBG(DM_GENERIC, DL_WRN,
	       "--> rwnx_cfg80211_remain_on_channel freq=%d dur=%d\n",
	       chan->center_freq, duration);

	/* For debug purpose (use ftrace kernel option) */
	trace_roc(rwnx_vif->vif_index, chan->center_freq, duration);

	/* For internale ROC, if ROC has been launched, just retrun, otherwise
       wait for the completion of ROC */
	if (rwnx_hw->roc) {
		if (!cookie)
			return -EBUSY;
		else {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
			reinit_completion(&rwnx_hw->roc_wait);
#else
			rwnx_hw->roc_wait.done = 0;
#endif
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "%s: wait 1s for pending ROC", __func__);

			if (!wait_for_completion_killable_timeout(
				    &rwnx_hw->roc_wait, HZ)) {
				WQ_DBG(DM_GENERIC, DL_ERR,
				       "%s: wait for ROC completion timeout\n",
				       __func__);
				return -EBUSY;
			}
		}
	}

	/* Allocate a temporary RoC element */
	roc = kmalloc(sizeof(struct rwnx_roc), GFP_KERNEL);
	if (!roc)
		return -ENOMEM;

	/* Initialize the RoC information element */
	roc->vif = rwnx_vif;
	roc->chan = chan;
	roc->duration = duration;
	roc->internal = false;
	roc->on_chan = false;

	/* Initialize the OFFCHAN TX queue to allow off-channel transmissions */
	rwnx_txq_offchan_init(rwnx_vif);

	/* Forward the information to the FMAC */
	rwnx_hw->roc = roc;
	if (cookie)
		*cookie = (u64)(rwnx_hw->roc_cookie);
	error = rwnx_send_roc(rwnx_hw, rwnx_vif, chan, duration);

	if (error != 0) {
		mutex_unlock(&rwnx_hw->mutex);
		kfree(roc);
		rwnx_hw->roc = NULL;
		rwnx_txq_offchan_deinit(rwnx_vif);
	} else {
		check_roc_ignore_nego_req(NULL, 0, 1, duration);
	}

	return error;
}

/**
 * @cancel_remain_on_channel: Cancel an on-going remain-on-channel operation.
 *	This allows the operation to be terminated prior to timeout based on
 *	the duration value.
 */
static int rwnx_cfg80211_cancel_remain_on_channel(struct wiphy *wiphy,
						  struct wireless_dev *wdev,
						  u64 cookie)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(wdev->netdev);

	ENTER();

	trace_cancel_roc(rwnx_vif->vif_index);

	if (!rwnx_hw->roc)
		return 0;

	/* Forward the information to the FMAC */
	return rwnx_send_cancel_roc(rwnx_hw);
}

/**
 * @dump_survey: get site survey information.
 */
static int rwnx_cfg80211_dump_survey(struct wiphy *wiphy,
				     struct net_device *netdev, int idx,
				     struct survey_info *info)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct ieee80211_supported_band *sband;
	struct rwnx_survey_info *rwnx_survey;

	ENTER();

	if (idx >= ARRAY_SIZE(rwnx_hw->survey))
		return -ENOENT;

	rwnx_survey = &rwnx_hw->survey[idx];

	// Check if provided index matches with a supported 2.4GHz channel
	sband = wiphy->bands[NL80211_BAND_2GHZ];
	if (sband && idx >= sband->n_channels) {
		idx -= sband->n_channels;
		sband = NULL;
	}

	if (!sband) {
		// Check if provided index matches with a supported 5GHz channel
		sband = wiphy->bands[NL80211_BAND_5GHZ];
		if (!sband || idx >= sband->n_channels)
			return -ENOENT;
	}

	// Fill the survey
	info->channel = &sband->channels[idx];
	info->filled = rwnx_survey->filled;

	if (rwnx_survey->filled != 0) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
		info->time = (u64)rwnx_survey->chan_time_ms;
		info->time_busy = (u64)rwnx_survey->chan_time_busy_ms;
#else
		info->channel_time = (u64)rwnx_survey->chan_time_ms;
		info->channel_time_busy = (u64)rwnx_survey->chan_time_busy_ms;
#endif
		info->noise = rwnx_survey->noise_dbm;

		// Set the survey report as not used
		rwnx_survey->filled = 0;
	}

	return 0;
}

/**
 * @get_channel: Get the current operating channel for the virtual interface.
 *	For monitor interfaces, it should return %NULL unless there's a single
 *	current monitoring channel.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static int rwnx_cfg80211_get_channel(struct wiphy *wiphy,
				     struct wireless_dev *wdev,
				     unsigned int link_id,
				     struct cfg80211_chan_def *chandef)
#else
static int rwnx_cfg80211_get_channel(struct wiphy *wiphy,
				     struct wireless_dev *wdev,
				     struct cfg80211_chan_def *chandef)
#endif
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = container_of(wdev, struct rwnx_vif, wdev);
	struct rwnx_chanctx *ctxt;

	if (!rwnx_vif->up) {
		return -ENODATA;
	}

	if (rwnx_vif->vif_index == rwnx_hw->monitor_vif) {
		//retrieve channel from firmware
		rwnx_cfg80211_set_monitor_channel(wiphy, NULL);
	}

	//Check if channel context is valid
	if (!rwnx_chanctx_valid(rwnx_hw, rwnx_vif->ch_index)) {
		return -ENODATA;
	}

	ctxt = &rwnx_hw->chanctx_table[rwnx_vif->ch_index];
	*chandef = ctxt->chan_def;

	if (!cfg80211_chandef_valid(chandef)) {
		WQ_DBG(DM_GENERIC, DL_WRN,
		       "rwnx_cfg80211_get_channel invalid\n");
		return -ENODATA;
	}

	WQ_DBG(DM_GENERIC, DL_WRN, "rwnx_cfg80211_get_channel %d %d %d %d\n",
	       chandef->chan->center_freq, chandef->center_freq1,
	       chandef->center_freq2, chandef->width);

	return 0;
}

/**
 * @mgmt_tx: Transmit a management frame.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
static int rwnx_cfg80211_mgmt_tx(struct wiphy *wiphy, struct wireless_dev *wdev,
				 struct cfg80211_mgmt_tx_params *params,
				 u64 *cookie)
{
#else
static int rwnx_cfg80211_mgmt_tx(struct wiphy *wiphy, struct wireless_dev *wdev,
				 struct ieee80211_channel *chan, bool offchan,
				 unsigned int wait, const u8 *buf, size_t len,
				 bool no_cck, bool dont_wait_for_ack,
				 u64 *cookie)
{
	struct cfg80211_mgmt_tx_params params_inst = {
		.chan = chan,
		.offchan = offchan,
		.wait = wait,
		.buf = buf,
		.len = len,
		.no_cck = no_cck,
		.dont_wait_for_ack = dont_wait_for_ack,
	};
	struct cfg80211_mgmt_tx_params *params = &params_inst;
#endif
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(wdev->netdev);
	struct rwnx_sta *rwnx_sta;
	struct ieee80211_mgmt *mgmt = (void *)params->buf;
	bool ap = false;
	bool _offchan = false;

	print_mgmt_frame_info("rwnx_cfg80211_mgmt_tx", mgmt, params->len);

	/* Check if provided VIF is an AP or a STA one */
	switch (RWNX_VIF_TYPE(rwnx_vif)) {
	case NL80211_IFTYPE_AP_VLAN:
		rwnx_vif = rwnx_vif->ap_vlan.master;
		fallthrough;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
	case NL80211_IFTYPE_MESH_POINT:
		ap = true;
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
	default:
		break;
	}

	/* Get STA on which management frame has to be sent */
	rwnx_sta = rwnx_retrieve_sta(rwnx_hw, rwnx_vif, mgmt->da,
				     mgmt->frame_control, ap);

	trace_mgmt_tx((params->chan) ? params->chan->center_freq : 0,
		      rwnx_vif->vif_index,
		      (rwnx_sta) ? rwnx_sta->sta_idx : 0xFF, mgmt);

	if (ap) {
		switch (mgmt->frame_control & IEEE80211_FCTL_STYPE) {
		case (IEEE80211_STYPE_ASSOC_RESP):
		case (IEEE80211_STYPE_REASSOC_RESP):
			if (rwnx_vif->ap.dbdc_mode == 4) {
				u8 *resp_ie = mgmt->u.assoc_resp.variable;
				uint16_t resp_len =
					params->len -
					offsetof(struct ieee80211_mgmt,
						 u.assoc_resp.variable);

				rwnx_eid_update_nss_param(rwnx_hw, resp_ie,
							  resp_len);
			}
			break;

		case (IEEE80211_STYPE_PROBE_RESP):
			if (rwnx_vif->ap.dbdc_mode == 4) {
				u8 *resp_ie = (u8 *)mgmt->u.probe_resp.variable;
				uint16_t resp_len =
					params->len -
					offsetof(struct ieee80211_mgmt,
						 u.probe_resp.variable);
				rwnx_eid_update_nss_param(rwnx_hw, resp_ie,
							  resp_len);
			}
			break;
		default:
			break;
		}
	}

	if (ap || rwnx_sta)
		goto send_frame;

	/* Not an AP interface sending frame to unknown STA:
     * This is allowed for external authentication */
	if ((rwnx_vif->sta.flags & RWNX_STA_EXT_AUTH) &&
	    ieee80211_is_auth(mgmt->frame_control))
		goto send_frame;

	/* Otherwise ROC is needed */
	if (!params->chan)
		return -EINVAL;

	if (rwnx_hw->roc) {
		/* Check if RoC channel is the same than the required one */
		if ((rwnx_hw->roc->vif != rwnx_vif) ||
		    (rwnx_hw->roc->chan->center_freq !=
		     params->chan->center_freq))
			return -EINVAL;

	} else {
		int error;

		WQ_DBG(DM_GENERIC, DL_WRN, "start internal ROC 30ms\n");

		/* Start a ROC procedure for 30ms */
		error = rwnx_cfg80211_remain_on_channel(wiphy, wdev,
							params->chan, 30, NULL);
		if (error)
			return error;

		/* internal RoC, no need to inform user space about it */
		if (rwnx_hw->roc)
			rwnx_hw->roc->internal = true;
	}

	_offchan = true;

send_frame:
	return rwnx_start_mgmt_xmit(rwnx_vif, rwnx_sta, params, _offchan,
				    cookie);
}

/**
 * @start_radar_detection: Start radar detection in the driver.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
static int
rwnx_cfg80211_start_radar_detection(struct wiphy *wiphy, struct net_device *dev,
				    struct cfg80211_chan_def *chandef,
				    u32 cac_time_ms)
#else
static int
rwnx_cfg80211_start_radar_detection(struct wiphy *wiphy, struct net_device *dev,
				    struct cfg80211_chan_def *chandef)
#endif
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct apm_start_cac_cfm cfm;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
	rwnx_radar_start_cac(&rwnx_hw->radar, cac_time_ms, rwnx_vif);
#else
	rwnx_radar_start_cac(&rwnx_hw->radar, 0, rwnx_vif);
#endif
	rwnx_send_apm_start_cac_req(rwnx_hw, rwnx_vif, chandef, &cfm);

	if (cfm.status == CO_OK) {
		spin_lock_bh(&rwnx_hw->cb_lock);
		rwnx_chanctx_link(rwnx_vif, cfm.ch_idx, chandef);
		if (rwnx_hw->cur_chanctx == rwnx_vif->ch_index)
			rwnx_radar_detection_enable(&rwnx_hw->radar,
						    RWNX_RADAR_DETECT_REPORT,
						    RWNX_RADAR_RIU);
		spin_unlock_bh(&rwnx_hw->cb_lock);
	} else {
		return -EIO;
	}

	return 0;
}

/**
 * @update_ft_ies: Provide updated Fast BSS Transition information to the
 *	driver. If the SME is in the driver/firmware, this information can be
 *	used in building Authentication and Reassociation Request frames.
 */
static int
rwnx_cfg80211_update_ft_ies(struct wiphy *wiphy, struct net_device *dev,
			    struct cfg80211_update_ft_ies_params *ftie)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *vif = netdev_priv(dev);
	const struct element *rsne = NULL, *mde = NULL, *fte = NULL, *elem;
	bool ft_in_non_rsn = false;
	int fties_len = 0;
	u8 *ft_assoc_ies, *pos;

	if ((RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_STATION) ||
	    (vif->sta.ft_assoc_ies == NULL))
		return 0;

	for_each_element (elem, ftie->ie, ftie->ie_len) {
		if (elem->id == WLAN_EID_RSN)
			rsne = elem;
		else if (elem->id == WLAN_EID_MOBILITY_DOMAIN)
			mde = elem;
		else if (elem->id == WLAN_EID_FAST_BSS_TRANSITION)
			fte = elem;
		else
			netdev_warn(dev, "Unexpected FT element %d\n",
				    elem->id);
	}
	if (!mde) {
		// maybe just test MDE for
		netdev_warn(dev, "Didn't find Mobility_Domain Element\n");
		return 0;
	} else if (!rsne && !fte) {
		// not sure this happen in real life ...
		ft_in_non_rsn = true;
	} else if (!rsne || !fte) {
		netdev_warn(dev,
			    "Didn't find RSN or Fast Transition Element\n");
		return 0;
	}

	for_each_element (elem, vif->sta.ft_assoc_ies,
			  vif->sta.ft_assoc_ies_len) {
		if ((elem->id == WLAN_EID_RSN) ||
		    (elem->id == WLAN_EID_MOBILITY_DOMAIN) ||
		    (elem->id == WLAN_EID_FAST_BSS_TRANSITION))
			fties_len += elem->datalen + sizeof(struct element);
	}

	ft_assoc_ies =
		kmalloc(vif->sta.ft_assoc_ies_len - fties_len + ftie->ie_len,
			GFP_KERNEL);
	if (!ft_assoc_ies) {
		netdev_warn(dev,
			    "Fail to allocate buffer for association elements");
	}

	// Recopy current Association Elements one at a time and replace FT
	// element with updated version.
	pos = ft_assoc_ies;
	for_each_element (elem, vif->sta.ft_assoc_ies,
			  vif->sta.ft_assoc_ies_len) {
		if (elem->id == WLAN_EID_RSN) {
			if (ft_in_non_rsn) {
				netdev_warn(dev,
					    "Found RSN element in non RSN FT");
				goto abort;
			} else if (!rsne) {
				netdev_warn(dev, "Found several RSN element");
				goto abort;
			} else {
				memcpy(pos, rsne,
				       sizeof(*rsne) + rsne->datalen);
				pos += sizeof(*rsne) + rsne->datalen;
				rsne = NULL;
			}
		} else if (elem->id == WLAN_EID_MOBILITY_DOMAIN) {
			if (!mde) {
				netdev_warn(
					dev,
					"Found several Mobility Domain element");
				goto abort;
			} else {
				memcpy(pos, mde, sizeof(*mde) + mde->datalen);
				pos += sizeof(*mde) + mde->datalen;
				mde = NULL;
			}
		} else if (elem->id == WLAN_EID_FAST_BSS_TRANSITION) {
			if (ft_in_non_rsn) {
				netdev_warn(
					dev,
					"Found Fast Transition element in non RSN FT");
				goto abort;
			} else if (!fte) {
				netdev_warn(
					dev,
					"found several Fast Transition element");
				goto abort;
			} else {
				memcpy(pos, fte, sizeof(*fte) + fte->datalen);
				pos += sizeof(*fte) + fte->datalen;
				fte = NULL;
			}
		} else {
			// Put FTE after MDE if non present in Association Element
			if (fte && !mde) {
				memcpy(pos, fte, sizeof(*fte) + fte->datalen);
				pos += sizeof(*fte) + fte->datalen;
				fte = NULL;
			}
			memcpy(pos, elem, sizeof(*elem) + elem->datalen);
			pos += sizeof(*elem) + elem->datalen;
		}
	}
	if (fte) {
		memcpy(pos, fte, sizeof(*fte) + fte->datalen);
		pos += sizeof(*fte) + fte->datalen;
		fte = NULL;
	}

	kfree(vif->sta.ft_assoc_ies);
	vif->sta.ft_assoc_ies = ft_assoc_ies;
	vif->sta.ft_assoc_ies_len = pos - ft_assoc_ies;

	if (vif->sta.flags & RWNX_STA_FT_OVER_DS) {
		struct sm_connect_cfm sm_connect_cfm;
		struct cfg80211_connect_params sme;

		memset(&sme, 0, sizeof(sme));
		rsne = cfg80211_find_elem(WLAN_EID_RSN, vif->sta.ft_assoc_ies,
					  vif->sta.ft_assoc_ies_len);
		if (rsne && rwnx_rsne_to_connect_params(rsne, &sme)) {
			netdev_warn(dev, "FT RSN parsing failed\n");
			return 0;
		}

		sme.ssid_len = vif->sta.ft_assoc_ies[1];
		sme.ssid = &vif->sta.ft_assoc_ies[2];
		sme.bssid = vif->sta.ft_target_ap;
		sme.ie = &vif->sta.ft_assoc_ies[2 + sme.ssid_len];
		sme.ie_len = vif->sta.ft_assoc_ies_len - (2 + sme.ssid_len);
		sme.auth_type = NL80211_AUTHTYPE_FT;
		rwnx_send_sm_connect_req(rwnx_hw, vif, &sme, &sm_connect_cfm);
		vif->sta.flags &= ~RWNX_STA_FT_OVER_DS;

	} else if (vif->sta.flags & RWNX_STA_FT_OVER_AIR) {
		uint8_t ssid_len;
		vif->sta.flags &= ~RWNX_STA_FT_OVER_AIR;

		// Skip the first element (SSID)
		ssid_len = vif->sta.ft_assoc_ies[1] + 2;
		if (rwnx_send_sm_ft_auth_rsp(
			    rwnx_hw, vif, &vif->sta.ft_assoc_ies[ssid_len],
			    vif->sta.ft_assoc_ies_len - ssid_len))
			netdev_err(
				dev,
				"FT Over Air: Failed to send updated assoc elem\n");
	}

	return 0;

abort:
	kfree(ft_assoc_ies);
	return 0;
}

/**
 * @set_cqm_rssi_config: Configure connection quality monitor RSSI threshold.
 */
static int rwnx_cfg80211_set_cqm_rssi_config(struct wiphy *wiphy,
					     struct net_device *dev,
					     int32_t rssi_thold,
					     uint32_t rssi_hyst)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);

	if (rwnx_vif->up == false)
		return 0;

	return rwnx_send_cfg_rssi_req(rwnx_hw, rwnx_vif->vif_index, rssi_thold,
				      rssi_hyst);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
/**
 *
 * @channel_switch: initiate channel-switch procedure (with CSA). Driver is
 *	responsible for veryfing if the switch is possible. Since this is
 *	inherently tricky driver may decide to disconnect an interface later
 *	with cfg80211_stop_iface(). This doesn't mean driver can accept
 *	everything. It should do it's best to verify requests and reject them
 *	as soon as possible.
 */
static int rwnx_cfg80211_channel_switch(struct wiphy *wiphy,
					struct net_device *dev,
					struct cfg80211_csa_settings *params)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *vif = netdev_priv(dev);
	struct rwnx_bcn *bcn, *bcn_after;
	struct rwnx_csa *csa;
	u16 csa_oft[BCN_MAX_CSA_CPT];
	u8 *buf, *buf_after = NULL;
	int i, error = 0;

	if (vif->ap.csa)
		return -EBUSY;

	if (params->n_counter_offsets_beacon > BCN_MAX_CSA_CPT)
		return -EINVAL;

	/* Build the new beacon with CSA IE */
	bcn = &vif->ap.bcn;
	buf = rwnx_build_bcn(bcn, &params->beacon_csa);
	if (!buf)
		return -ENOMEM;

	memset(csa_oft, 0, sizeof(csa_oft));
	for (i = 0; i < params->n_counter_offsets_beacon; i++) {
		csa_oft[i] = params->counter_offsets_beacon[i] + bcn->head_len +
			     bcn->tim_len;
	}

	/* If count is set to 0 (i.e anytime after this beacon) force it to 2 */
	if (params->count == 0) {
		params->count = 2;
		for (i = 0; i < params->n_counter_offsets_beacon; i++) {
			buf[csa_oft[i]] = 2;
		}
	}

	/* Build the beacon to use after CSA. It will only be sent to fw once
       CSA is over, but do it before sending the beacon as it must be ready
       when CSA is finished. */
	csa = kzalloc(sizeof(struct rwnx_csa), GFP_KERNEL);
	if (!csa) {
		error = -ENOMEM;
		goto end;
	}

	bcn_after = &csa->bcn;
	buf_after = rwnx_build_bcn(bcn_after, &params->beacon_after);
	if (!buf_after) {
		error = -ENOMEM;
		rwnx_del_csa(vif);
		goto end;
	}

	memcpy(csa->bcn_buf, buf_after, bcn_after->len);

	vif->ap.csa = csa;
	csa->vif = vif;
	csa->chandef = params->chandef;

	/* Send new Beacon. FW will extract channel and count from the beacon */
	error = rwnx_send_bcn_change(rwnx_hw, vif->vif_index, buf, bcn->len,
				     bcn->head_len, bcn->tim_len, csa_oft);

	if (error) {
		rwnx_del_csa(vif);
		goto end;
	} else {
		INIT_WORK(&csa->work, rwnx_csa_finish);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
		cfg80211_ch_switch_started_notify(dev, &csa->chandef, 0,
						  params->count, false, 0);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
		cfg80211_ch_switch_started_notify(dev, &csa->chandef, 0,
						  params->count, false);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
		cfg80211_ch_switch_started_notify(dev, &csa->chandef,
						  params->count, false);
#else
		cfg80211_ch_switch_started_notify(dev, &csa->chandef,
						  params->count);
#endif
	}

end:
	if (buf) {
		kfree(buf);
		buf = NULL;
	}
	if (buf_after) {
		kfree(buf_after);
		buf_after = NULL;
	}
	return error;
}
#endif

/**
 * @@tdls_mgmt: Transmit a TDLS management frame.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
static int rwnx_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
				   const u8 *peer, int link_id, u8 action_code,
				   u8 dialog_token, u16 status_code,
				   u32 peer_capability, bool initiator,
				   const u8 *buf, size_t len)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
static int rwnx_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
				   const u8 *peer, u8 action_code,
				   u8 dialog_token, u16 status_code,
				   u32 peer_capability, bool initiator,
				   const u8 *buf, size_t len)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
static int rwnx_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
				   u8 *peer, u8 action_code, u8 dialog_token,
				   u16 status_code, u32 peer_capability,
				   const u8 *buf, size_t len)
#else
static int rwnx_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
				   u8 *peer, u8 action_code, u8 dialog_token,
				   u16 status_code, const u8 *buf, size_t len)
#endif
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	int ret = 0;

	/* make sure we support TDLS */
	if (!(wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS))
		return -ENOTSUPP;

	/* make sure we are in station mode (and connected) */
	if ((RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION) ||
	    (!rwnx_vif->up) || (!rwnx_vif->sta.ap))
		return -ENOTSUPP;

	/* only one TDLS link is supported */
	if ((action_code == WLAN_TDLS_SETUP_REQUEST) &&
	    (rwnx_vif->sta.tdls_sta) &&
	    (rwnx_vif->tdls_status == TDLS_LINK_ACTIVE)) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "%s: only one TDLS link is supported!\n", __func__);
		return -ENOTSUPP;
	}

	if ((action_code == WLAN_TDLS_DISCOVERY_REQUEST) &&
	    (rwnx_hw->mod_params.ps_on)) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "%s: discovery request is not supported when "
		       "power-save is enabled!\n",
		       __func__);
		return -ENOTSUPP;
	}

	switch (action_code) {
	case WLAN_TDLS_SETUP_RESPONSE:
		/* only one TDLS link is supported */
		if ((status_code == 0) && (rwnx_vif->sta.tdls_sta) &&
		    (rwnx_vif->tdls_status == TDLS_LINK_ACTIVE)) {
			WQ_DBG(DM_GENERIC, DL_INF,
			       "%s: only one TDLS link is supported!\n",
			       __func__);
			status_code = WLAN_STATUS_REQUEST_DECLINED;
		}
		fallthrough;
	case WLAN_TDLS_SETUP_REQUEST:
	case WLAN_TDLS_TEARDOWN:
	case WLAN_TDLS_DISCOVERY_REQUEST:
	case WLAN_TDLS_SETUP_CONFIRM:
	case WLAN_PUB_ACTION_TDLS_DISCOVER_RES:
		ret = rwnx_tdls_send_mgmt_packet_data(
			rwnx_hw, rwnx_vif, peer, action_code, dialog_token,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
			status_code, peer_capability, initiator, buf, len, 0,
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
			status_code, peer_capability, 0, buf, len, 0,
#else
			status_code, 0, 0, buf, len, 0,
#endif
			NULL);
		break;

	default:
		WQ_DBG(DM_GENERIC, DL_INF,
		       "%s: Unknown TDLS mgmt/action frame %pM\n", __func__,
		       peer);
		ret = -EOPNOTSUPP;
		break;
	}

	if (action_code == WLAN_TDLS_SETUP_REQUEST) {
		rwnx_vif->tdls_status = TDLS_SETUP_REQ_TX;
	} else if (action_code == WLAN_TDLS_SETUP_RESPONSE) {
		rwnx_vif->tdls_status = TDLS_SETUP_RSP_TX;
	} else if ((action_code == WLAN_TDLS_SETUP_CONFIRM) && (ret == CO_OK)) {
		rwnx_vif->tdls_status = TDLS_LINK_ACTIVE;
		/* Set TDLS active */
		rwnx_vif->sta.tdls_sta->tdls.active = true;
	}

	return ret;
}

/**
 * @tdls_oper: Perform a high-level TDLS operation (e.g. TDLS link setup).
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
static int rwnx_cfg80211_tdls_oper(struct wiphy *wiphy, struct net_device *dev,
				   const u8 *peer,
				   enum nl80211_tdls_operation oper)
#else
static int rwnx_cfg80211_tdls_oper(struct wiphy *wiphy, struct net_device *dev,
				   u8 *peer, enum nl80211_tdls_operation oper)
#endif
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	int error;

	if (oper != NL80211_TDLS_DISABLE_LINK)
		return 0;

	if (!rwnx_vif->sta.tdls_sta) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "%s: TDLS station %pM does not exist\n", __func__, peer);
		return -ENOLINK;
	}

	if (memcmp(rwnx_vif->sta.tdls_sta->mac_addr, peer, ETH_ALEN) == 0) {
		/* Disable Channel Switch */
		if (!rwnx_send_tdls_cancel_chan_switch_req(
			    rwnx_hw, rwnx_vif, rwnx_vif->sta.tdls_sta, NULL))
			rwnx_vif->sta.tdls_sta->tdls.chsw_en = false;

		netdev_info(dev, "Del TDLS sta %d (%pM)",
			    rwnx_vif->sta.tdls_sta->sta_idx,
			    rwnx_vif->sta.tdls_sta->mac_addr);
		/* Ensure that we won't process PS change ind */
		spin_lock_bh(&rwnx_hw->cb_lock);
		rwnx_vif->sta.tdls_sta->ps.active = false;
		rwnx_vif->sta.tdls_sta->valid = false;
		spin_unlock_bh(&rwnx_hw->cb_lock);
		rwnx_txq_sta_deinit(rwnx_hw, rwnx_vif->sta.tdls_sta);
		error = rwnx_send_me_sta_del(
			rwnx_hw, rwnx_vif->sta.tdls_sta->sta_idx, true);
		if ((error != 0) && (error != -EPIPE))
			return error;

#ifdef CONFIG_RWNX_BFMER
		// Disable Beamformer if supported
		rwnx_bfmer_report_del(rwnx_hw, rwnx_vif->sta.tdls_sta);
		rwnx_mu_group_sta_del(rwnx_hw, rwnx_vif->sta.tdls_sta);
#endif /* CONFIG_RWNX_BFMER */

		/* Set TDLS not active */
		rwnx_vif->sta.tdls_sta->tdls.active = false;
		rwnx_dbgfs_unregister_sta(rwnx_hw, rwnx_vif->sta.tdls_sta);
		// Remove TDLS station
		rwnx_vif->tdls_status = TDLS_LINK_IDLE;
		rwnx_vif->sta.tdls_sta = NULL;
	}

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
/**
 *  @tdls_channel_switch: Start channel-switching with a TDLS peer. The driver
 *	is responsible for continually initiating channel-switching operations
 *	and returning to the base channel for communication with the AP.
 */
static int rwnx_cfg80211_tdls_channel_switch(struct wiphy *wiphy,
					     struct net_device *dev,
					     const u8 *addr, u8 oper_class,
					     struct cfg80211_chan_def *chandef)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_sta *rwnx_sta = rwnx_vif->sta.tdls_sta;
	struct tdls_chan_switch_cfm cfm;
	int error;

	if ((!rwnx_sta) || (memcmp(addr, rwnx_sta->mac_addr, ETH_ALEN))) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "%s: TDLS station %pM doesn't exist\n", __func__, addr);
		return -ENOLINK;
	}

	if (!rwnx_sta->tdls.chsw_allowed) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "%s: TDLS station %pM does not support TDLS channel switch\n",
		       __func__, addr);
		return -ENOTSUPP;
	}

	error = rwnx_send_tdls_chan_switch_req(rwnx_hw, rwnx_vif, rwnx_sta,
					       rwnx_sta->tdls.initiator,
					       oper_class, chandef, &cfm);
	if (error)
		return error;

	if (!cfm.status) {
		rwnx_sta->tdls.chsw_en = true;
		return 0;
	} else {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "%s: TDLS channel switch already enabled and only one is supported\n",
		       __func__);
		return -EALREADY;
	}
}

/**
 * @tdls_cancel_channel_switch: Stop channel-switching with a TDLS peer. Both
 *	peers must be on the base channel when the call completes.
 */
static void rwnx_cfg80211_tdls_cancel_channel_switch(struct wiphy *wiphy,
						     struct net_device *dev,
						     const u8 *addr)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_sta *rwnx_sta = rwnx_vif->sta.tdls_sta;
	struct tdls_cancel_chan_switch_cfm cfm;

	if (!rwnx_sta)
		return;

	if (!rwnx_send_tdls_cancel_chan_switch_req(rwnx_hw, rwnx_vif, rwnx_sta,
						   &cfm))
		rwnx_sta->tdls.chsw_en = false;
}
#endif

/**
 * @change_bss: Modify parameters for a given BSS (mainly for AP mode).
 */
static int rwnx_cfg80211_change_bss(struct wiphy *wiphy, struct net_device *dev,
				    struct bss_parameters *params)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	int res = -EOPNOTSUPP;

	if (((RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_AP) ||
	     (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_GO)) &&
	    (params->ap_isolate > -1)) {
		if (params->ap_isolate)
			rwnx_vif->ap.flags |= RWNX_AP_ISOLATE;
		else
			rwnx_vif->ap.flags &= ~RWNX_AP_ISOLATE;

		res = 0;
	}

	return res;
}

//iw dev wlan0 set bitrates legacy-2.4 1.0 (legacy 1Mbps)
//iw dev wlan0 set bitrates ht-mcs-2.4 0 (MCS 0)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static int rwnx_cfg80211_set_bitrate(struct wiphy *wiphy,
				     struct net_device *dev,
				     unsigned int link_id, const u8 *addr,
				     const struct cfg80211_bitrate_mask *mask)
#else
static int rwnx_cfg80211_set_bitrate(struct wiphy *wiphy,
				     struct net_device *dev, const u8 *addr,
				     const struct cfg80211_bitrate_mask *mask)
#endif
{
//base on rwnx fw define
#define LEGACY_MEM_LEN 12
#define BW_20MHZ 0
#define MCS_INDEX_TX_RCX_OFT 0
#define HW_RATE_1MBPS 0
#define PRE_TYPE_TX_RCX_OFT 10
#define PRE_TYPE_TX_RCX_MASK (0x1 << PRE_TYPE_TX_RCX_OFT)
#define FORMAT_MOD_TX_RCX_OFT 11
#define HT_MCS_OFT 0
#define BW_TX_RCX_OFT 7
#define HE_GI_TYPE_TX_RCX_OFT 9
#define HE_GI_TYPE_TX_RCX_MASK (0x3 << HE_GI_TYPE_TX_RCX_OFT)

	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	uint32_t i, j;
	uint32_t format_mod = -1, rate_idx = 0, mcs = 0, gi_pr_premable = 0,
		 rate_cfg_hw = 0;
	uint8_t bw = BW_20MHZ;
	uint32_t legacy_mask = mask->control[NL80211_BAND_2GHZ].legacy;
	struct rwnx_vif *vif = netdev_priv(dev);

	//only support fix rate in STA mode for now, to be fix for P2P mode(causing fw crash)
	if (vif->wdev.iftype != NL80211_IFTYPE_STATION) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "rwnx_cfg80211_set_bitrate only in STA mode, skip\n");
		return -ENOTSUPP;
	}
	if (vif->sta.ap == NULL) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "rwnx_cfg80211_set_bitrate vif->sta.ap is null, skip\n");
		return 0;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
	gi_pr_premable = mask->control[NL80211_BAND_2GHZ].gi;
#endif
	//uint8_t legacy_member[LEGACY_MEM_LEN] = { 1, 2, 5, 11, 6, 9, 12, 18, 24, 36, 48, 54 }; //Mbps

	//legacy
	if (legacy_mask && legacy_mask != 0xFFF) {
		for (i = 0; i < LEGACY_MEM_LEN; i++) {
			if (legacy_mask & (0x1 << i)) {
				format_mod = FORMATMOD_NON_HT;
				//rate = legacy_member[i];
				rate_idx = i;
				break;
			}
		}
	} else { //ht-mcs
		for (i = 0; i < IEEE80211_HT_MCS_MASK_LEN; i++) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
			if (mask->control[NL80211_BAND_2GHZ].ht_mcs[i]) {
#else
			if (mask->control[NL80211_BAND_2GHZ].mcs[i]) {
#endif
				for (j = 0; j < 8; j++) { //8bit
					if (mask->control[NL80211_BAND_2GHZ]
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
						    .ht_mcs[i] &
#else
						    .mcs[i] &
#endif
					    (0x1 << j)) {
						format_mod = FORMATMOD_HT_MF;
						mcs = i * 8 + j;
						break;
					}
				}
				break;
			}
		}
	}

	rate_cfg_hw = format_mod << FORMAT_MOD_TX_RCX_OFT;
	gi_pr_premable = (gi_pr_premable << HE_GI_TYPE_TX_RCX_OFT) &
			 HE_GI_TYPE_TX_RCX_MASK;

	switch (format_mod) {
	case FORMATMOD_NON_HT:
		rate_cfg_hw |= rate_idx << MCS_INDEX_TX_RCX_OFT;
		if (rate_idx == HW_RATE_1MBPS) {
			rate_cfg_hw |=
				PRE_TYPE_TX_RCX_MASK; // rate index 0 (CCK 1Mbps) allows only long preamble
		} else {
			rate_cfg_hw |=
				gi_pr_premable; //other is default(short preamble)
		}
		break;
	case FORMATMOD_HT_MF:
		rate_cfg_hw |= mcs << HT_MCS_OFT;
		rate_cfg_hw |= bw << BW_TX_RCX_OFT;
		rate_cfg_hw |= gi_pr_premable;
		break;
	default:
		break;
	}

	if (format_mod != -1) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "%s: rate_cfg_hw = 0x%x, sta_idx: %d\n", __func__,
		       rate_cfg_hw, vif->sta.ap->sta_idx);
		rwnx_send_me_rc_set_rate(rwnx_hw, vif->sta.ap->sta_idx,
					 rate_cfg_hw);
	} else {
		WQ_DBG(DM_GENERIC, DL_INF, "%s: please set correct mode\n",
		       __func__);
	}

	return 0;
}

static int rwnx_fill_station_info(struct rwnx_sta *sta, struct rwnx_vif *vif,
				  struct station_info *sinfo)
{
	struct rwnx_sta_stats *stats = &sta->stats;
	struct rx_vec_detail_1 *rx_vec_1 = &stats->last_rx.rx_vec_1;
	u8 dummy = 0;

	ENTER();

	// Generic info
	sinfo->generation = vif->generation;

	sinfo->inactive_time = jiffies_to_msecs(jiffies - stats->last_act);
	sinfo->rx_bytes = stats->rx_bytes;
	sinfo->tx_bytes = stats->tx_bytes;
	sinfo->tx_packets = stats->tx_pkts;
	sinfo->rx_packets = stats->rx_pkts;

	// For AP mode, we always report RSSI of the last receieved packet
	if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_AP) {
		sinfo->signal = rx_vec_1->rssi_leg;
	} else {
		if (RWNX_INFO_NOTIFY_GET_VIF_NO_CHK(
			    vif->rwnx_hw, MM_INFO_NOTIFY_GET_RSSI,
			    vif->vif_index, dummy,
			    &sinfo->signal) != sizeof(sinfo->signal)) {
			sinfo->signal = rx_vec_1->rssi_leg;
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "%s rwnx_info_notify result fail\n", __func__);
		}
	}

	WQ_DBG(DM_GENERIC, DL_WRN, "[auto][%s] RSSI : %d, FORMAT: %d\n",
	       __func__, sinfo->signal, sta->format_mod);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
	switch (sta->width) {
	case PHY_CHNL_BW_20:
		sinfo->rxrate.bw = RATE_INFO_BW_20;
		break;
	case PHY_CHNL_BW_40:
		sinfo->rxrate.bw = RATE_INFO_BW_40;
		break;
	case PHY_CHNL_BW_80:
		sinfo->rxrate.bw = RATE_INFO_BW_80;
		break;
	case PHY_CHNL_BW_160:
		sinfo->rxrate.bw = RATE_INFO_BW_160;
		break;
	default:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
		sinfo->rxrate.bw = RATE_INFO_BW_HE_RU;
#else
		sinfo->rxrate.bw = RATE_INFO_BW_160;
#endif
		break;
	}
#endif

	switch (sta->format_mod) {
	case FORMATMOD_NON_HT:
	case FORMATMOD_NON_HT_DUP_OFDM:
		sinfo->rxrate.flags = 0;
		if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_AP)
			sinfo->rxrate.legacy =
				legrates_lut[sta->legacy_rate_idx].rate;
		else
			sinfo->rxrate.legacy =
				legrates_lut[rx_vec_1->leg_rate].rate;
		break;
	case FORMATMOD_HT_MF:
	case FORMATMOD_HT_GF:
		sinfo->rxrate.flags = RATE_INFO_FLAGS_MCS;
		//if (rx_vec_1->ht.short_gi)
		sinfo->rxrate.flags |=
			RATE_INFO_FLAGS_SHORT_GI; //force short gi
		sinfo->rxrate.mcs = sta->rx_mcs_idx;
		break;
	case FORMATMOD_VHT:
		sinfo->rxrate.flags = RATE_INFO_FLAGS_VHT_MCS;
		//if (rx_vec_1->vht.short_gi)
		sinfo->rxrate.flags |=
			RATE_INFO_FLAGS_SHORT_GI; //force short gi
		sinfo->rxrate.mcs = sta->rx_mcs_idx;
		if ((sta->rx_nss < 1) || (sta->rx_nss > 8))
			sta->rx_nss = 1;
		sinfo->rxrate.nss = sta->rx_nss;
		break;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
	case FORMATMOD_HE_MU:
		sinfo->rxrate.he_ru_alloc = rx_vec_1->he.ru_size;
		fallthrough;
	case FORMATMOD_HE_SU:
	case FORMATMOD_HE_ER:
	case FORMATMOD_HE_TB:
		sinfo->rxrate.flags = RATE_INFO_FLAGS_HE_MCS;
		sinfo->rxrate.mcs = sta->rx_mcs_idx;
		if ((sta->rx_nss < 1) || (sta->rx_nss > 8))
			sta->rx_nss = 1;
		sinfo->rxrate.nss = sta->rx_nss;
		sinfo->rxrate.he_gi = 0; //short gi
		sinfo->rxrate.he_dcm = 0;
		break;
#else
	case FORMATMOD_HE_MU: //kernel does not support HE, pretend this is a VHT rate
	case FORMATMOD_HE_SU:
	case FORMATMOD_HE_ER:
	case FORMATMOD_HE_TB:
		sinfo->rxrate.flags = RATE_INFO_FLAGS_VHT_MCS;
		//up to MCS9 for VHT
		if (sta->rx_mcs_idx <= 9)
			sinfo->rxrate.mcs = sta->rx_mcs_idx;
		else
			sinfo->rxrate.mcs = 9;
		if ((sta->rx_nss < 1) || (sta->rx_nss > 8))
			sta->rx_nss = 1;
		sinfo->rxrate.nss = sta->rx_nss;
		break;
#endif
	default:
		WQ_DBG(DM_GENERIC, DL_WRN, "unknown mod=%d\n",
		       rx_vec_1->format_mod);
		return -EINVAL;
	}
	/* FIXME: get tx bitrate and tx failed from f/w. */
	memcpy(&sinfo->txrate, &sinfo->rxrate, sizeof(sinfo->rxrate));

	sinfo->filled = (BIT(NL80211_STA_INFO_INACTIVE_TIME) |
			 BIT(NL80211_STA_INFO_RX_BYTES64) |
			 BIT(NL80211_STA_INFO_TX_BYTES64) |
			 BIT(NL80211_STA_INFO_RX_PACKETS) |
			 BIT(NL80211_STA_INFO_TX_PACKETS) |
			 BIT(NL80211_STA_INFO_SIGNAL) |
			 BIT(NL80211_STA_INFO_RX_BITRATE) |
			 BIT(NL80211_STA_INFO_TX_BITRATE) |
			 BIT(NL80211_STA_INFO_TX_FAILED));

	// Mesh specific info
	if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_MESH_POINT) {
		struct mesh_peer_info_cfm peer_info_cfm;
		if (rwnx_send_mesh_peer_info_req(vif->rwnx_hw, vif,
						 sta->sta_idx, &peer_info_cfm))
			return -ENOMEM;

		peer_info_cfm.last_bcn_age = peer_info_cfm.last_bcn_age / 1000;
		if (peer_info_cfm.last_bcn_age < sinfo->inactive_time)
			sinfo->inactive_time = peer_info_cfm.last_bcn_age;

		sinfo->llid = peer_info_cfm.local_link_id;
		sinfo->plid = peer_info_cfm.peer_link_id;
		sinfo->plink_state = peer_info_cfm.link_state;
		sinfo->local_pm = peer_info_cfm.local_ps_mode;
		sinfo->peer_pm = peer_info_cfm.peer_ps_mode;
		sinfo->nonpeer_pm = peer_info_cfm.non_peer_ps_mode;

		sinfo->filled |= (BIT(NL80211_STA_INFO_LLID) |
				  BIT(NL80211_STA_INFO_PLID) |
				  BIT(NL80211_STA_INFO_PLINK_STATE) |
				  BIT(NL80211_STA_INFO_LOCAL_PM) |
				  BIT(NL80211_STA_INFO_PEER_PM) |
				  BIT(NL80211_STA_INFO_NONPEER_PM));
	}

	return 0;
}

/**
 * @get_station: get station information for the station identified by @mac
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
static int rwnx_cfg80211_get_station(struct wiphy *wiphy,
				     struct net_device *dev, const u8 *mac,
				     struct station_info *sinfo)
#else
static int rwnx_cfg80211_get_station(struct wiphy *wiphy,
				     struct net_device *dev, u8 *mac,
				     struct station_info *sinfo)
#endif
{
	struct rwnx_vif *vif = netdev_priv(dev);
	struct rwnx_sta *sta = NULL;

	if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_MONITOR)
		return -EINVAL;
	else if ((RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_STATION) ||
		 (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_CLIENT)) {
		if (vif->sta.ap && ether_addr_equal(vif->sta.ap->mac_addr, mac))
			sta = vif->sta.ap;
	} else {
		struct rwnx_sta *sta_iter;
		list_for_each_entry (sta_iter, &vif->ap.sta_list, list) {
			if (sta_iter->valid &&
			    ether_addr_equal(sta_iter->mac_addr, mac)) {
				sta = sta_iter;
				break;
			}
		}
	}

	if (sta)
		return rwnx_fill_station_info(sta, vif, sinfo);
	else
		WQ_DBG(DM_GENERIC, DL_WRN, "[auto][%s] sta is NULL\n",
		       __func__);

	return -EINVAL;
}

/**
 * @dump_station: dump station callback -- resume dump at index @idx
 */
static int rwnx_cfg80211_dump_station(struct wiphy *wiphy,
				      struct net_device *dev, int idx, u8 *mac,
				      struct station_info *sinfo)
{
	struct rwnx_vif *vif = netdev_priv(dev);
	struct rwnx_sta *sta = NULL;

	if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_MONITOR)
		return -EINVAL;
	else if ((RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_STATION) ||
		 (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_CLIENT)) {
		if ((idx == 0) && vif->sta.ap && vif->sta.ap->valid)
			sta = vif->sta.ap;
	} else {
		struct rwnx_sta *sta_iter;
		int i = 0;
		list_for_each_entry (sta_iter, &vif->ap.sta_list, list) {
			if (i == idx) {
				sta = sta_iter;
				break;
			}
			i++;
		}
	}

	if (sta == NULL)
		return -ENOENT;

	/* Copy peer MAC address */
	memcpy(mac, &sta->mac_addr, ETH_ALEN);

	return rwnx_fill_station_info(sta, vif, sinfo);
}

/**
 * @add_mpath: add a fixed mesh path
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
static int rwnx_cfg80211_add_mpath(struct wiphy *wiphy, struct net_device *dev,
				   const u8 *dst, const u8 *next_hop)
#else
static int rwnx_cfg80211_add_mpath(struct wiphy *wiphy, struct net_device *dev,
				   u8 *dst, u8 *next_hop)
#endif
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct mesh_path_update_cfm cfm;

	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
		return -ENOTSUPP;

	return rwnx_send_mesh_path_update_req(rwnx_hw, rwnx_vif, dst, next_hop,
					      &cfm);
}

/**
 * @del_mpath: delete a given mesh path
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
static int rwnx_cfg80211_del_mpath(struct wiphy *wiphy, struct net_device *dev,
				   const u8 *dst)
#else
static int rwnx_cfg80211_del_mpath(struct wiphy *wiphy, struct net_device *dev,
				   u8 *dst)
#endif
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct mesh_path_update_cfm cfm;

	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
		return -ENOTSUPP;

	return rwnx_send_mesh_path_update_req(rwnx_hw, rwnx_vif, dst, NULL,
					      &cfm);
}

/**
 * @change_mpath: change a given mesh path
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
static int rwnx_cfg80211_change_mpath(struct wiphy *wiphy,
				      struct net_device *dev, const u8 *dst,
				      const u8 *next_hop)
#else
static int rwnx_cfg80211_change_mpath(struct wiphy *wiphy,
				      struct net_device *dev, u8 *dst,
				      u8 *next_hop)
#endif
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct mesh_path_update_cfm cfm;

	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
		return -ENOTSUPP;

	return rwnx_send_mesh_path_update_req(rwnx_hw, rwnx_vif, dst, next_hop,
					      &cfm);
}

/**
 * @get_mpath: get a mesh path for the given parameters
 */
static int rwnx_cfg80211_get_mpath(struct wiphy *wiphy, struct net_device *dev,
				   u8 *dst, u8 *next_hop,
				   struct mpath_info *pinfo)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_mesh_path *mesh_path = NULL;
	struct rwnx_mesh_path *cur;

	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
		return -ENOTSUPP;

	list_for_each_entry (cur, &rwnx_vif->ap.mpath_list, list) {
		/* Compare the path target address and the provided destination address */
		if (memcmp(dst, &cur->tgt_mac_addr, ETH_ALEN)) {
			continue;
		}

		mesh_path = cur;
		break;
	}

	if (mesh_path == NULL)
		return -ENOENT;

	/* Copy next HOP MAC address */
	if (mesh_path->nhop_sta)
		memcpy(next_hop, &mesh_path->nhop_sta->mac_addr, ETH_ALEN);

	/* Fill path information */
	pinfo->filled = 0;
	pinfo->generation = rwnx_vif->generation;

	return 0;
}

/**
 * @dump_mpath: dump mesh path callback -- resume dump at index @idx
 */
static int rwnx_cfg80211_dump_mpath(struct wiphy *wiphy, struct net_device *dev,
				    int idx, u8 *dst, u8 *next_hop,
				    struct mpath_info *pinfo)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_mesh_path *mesh_path = NULL;
	struct rwnx_mesh_path *cur;
	int i = 0;

	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
		return -ENOTSUPP;

	list_for_each_entry (cur, &rwnx_vif->ap.mpath_list, list) {
		if (i < idx) {
			i++;
			continue;
		}

		mesh_path = cur;
		break;
	}

	if (mesh_path == NULL)
		return -ENOENT;

	/* Copy target and next hop MAC address */
	memcpy(dst, &mesh_path->tgt_mac_addr, ETH_ALEN);
	if (mesh_path->nhop_sta)
		memcpy(next_hop, &mesh_path->nhop_sta->mac_addr, ETH_ALEN);

	/* Fill path information */
	pinfo->filled = 0;
	pinfo->generation = rwnx_vif->generation;

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
/**
 * @get_mpp: get a mesh proxy path for the given parameters
 */
static int rwnx_cfg80211_get_mpp(struct wiphy *wiphy, struct net_device *dev,
				 u8 *dst, u8 *mpp, struct mpath_info *pinfo)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_mesh_proxy *mesh_proxy = NULL;
	struct rwnx_mesh_proxy *cur;

	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
		return -ENOTSUPP;

	list_for_each_entry (cur, &rwnx_vif->ap.proxy_list, list) {
		if (cur->local) {
			continue;
		}

		/* Compare the path target address and the provided destination address */
		if (memcmp(dst, &cur->ext_sta_addr, ETH_ALEN)) {
			continue;
		}

		mesh_proxy = cur;
		break;
	}

	if (mesh_proxy == NULL)
		return -ENOENT;

	memcpy(mpp, &mesh_proxy->proxy_addr, ETH_ALEN);

	/* Fill path information */
	pinfo->filled = 0;
	pinfo->generation = rwnx_vif->generation;

	return 0;
}

/**
 * @dump_mpp: dump mesh proxy path callback -- resume dump at index @idx
 */
static int rwnx_cfg80211_dump_mpp(struct wiphy *wiphy, struct net_device *dev,
				  int idx, u8 *dst, u8 *mpp,
				  struct mpath_info *pinfo)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_mesh_proxy *mesh_proxy = NULL;
	struct rwnx_mesh_proxy *cur;
	int i = 0;

	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
		return -ENOTSUPP;

	list_for_each_entry (cur, &rwnx_vif->ap.proxy_list, list) {
		if (cur->local) {
			continue;
		}

		if (i < idx) {
			i++;
			continue;
		}

		mesh_proxy = cur;
		break;
	}

	if (mesh_proxy == NULL)
		return -ENOENT;

	/* Copy target MAC address */
	memcpy(dst, &mesh_proxy->ext_sta_addr, ETH_ALEN);
	memcpy(mpp, &mesh_proxy->proxy_addr, ETH_ALEN);

	/* Fill path information */
	pinfo->filled = 0;
	pinfo->generation = rwnx_vif->generation;

	return 0;
}
#endif

/**
 * @get_mesh_config: Get the current mesh configuration
 */
static int rwnx_cfg80211_get_mesh_config(struct wiphy *wiphy,
					 struct net_device *dev,
					 struct mesh_config *conf)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);

	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
		return -ENOTSUPP;

	return 0;
}

/**
 * @update_mesh_config: Update mesh parameters on a running mesh.
 */
static int rwnx_cfg80211_update_mesh_config(struct wiphy *wiphy,
					    struct net_device *dev, u32 mask,
					    const struct mesh_config *nconf)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct mesh_update_cfm cfm;
	int status;

	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
		return -ENOTSUPP;

	if (mask & BIT(NL80211_MESHCONF_POWER_MODE - 1)) {
		rwnx_vif->ap.next_mesh_pm = nconf->power_mode;

		if (!list_empty(&rwnx_vif->ap.sta_list)) {
			// If there are mesh links we don't want to update the power mode
			// It will be updated with rwnx_update_mesh_power_mode() when the
			// ps mode of a link is updated or when a new link is added/removed
			mask &= ~BIT(NL80211_MESHCONF_POWER_MODE - 1);

			if (!mask)
				return 0;
		}
	}

	status =
		rwnx_send_mesh_update_req(rwnx_hw, rwnx_vif, mask, nconf, &cfm);

	if (!status && (cfm.status != 0))
		status = -EINVAL;

	return status;
}

/**
 * @join_mesh: join the mesh network with the specified parameters
 * (invoked with the wireless_dev mutex held)
 */
static int rwnx_cfg80211_join_mesh(struct wiphy *wiphy, struct net_device *dev,
				   const struct mesh_config *conf,
				   const struct mesh_setup *setup)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct mesh_start_cfm mesh_start_cfm;
	int error = 0;
	u8 txq_status = 0;
	/* STA for BC/MC traffic */
	struct rwnx_sta *sta;

	ENTER();

	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
		return -ENOTSUPP;

	/* Forward the information to the UMAC */
	if ((error = rwnx_send_mesh_start_req(rwnx_hw, rwnx_vif, conf, setup,
					      &mesh_start_cfm))) {
		return error;
	}

	/* Check the status */
	switch (mesh_start_cfm.status) {
	case CO_OK:
		rwnx_vif->ap.bcmc_index = mesh_start_cfm.bcmc_idx;
		rwnx_vif->ap.flags = 0;
		rwnx_vif->ap.bcn_interval = setup->beacon_interval;
		rwnx_vif->use_4addr = true;
		if (setup->user_mpm)
			rwnx_vif->ap.flags |= RWNX_AP_USER_MESH_PM;

		sta = &rwnx_hw->sta_table[mesh_start_cfm.bcmc_idx];
		sta->valid = true;
		sta->aid = 0;
		sta->sta_idx = mesh_start_cfm.bcmc_idx;
		sta->ch_idx = mesh_start_cfm.ch_idx;
		sta->vif_idx = rwnx_vif->vif_index;
		sta->qos = true;
		sta->acm = 0;
		sta->ps.active = false;
		sta->listen_interval = 5;
		rwnx_mu_group_sta_init(sta, NULL);
		spin_lock_bh(&rwnx_hw->cb_lock);
		rwnx_chanctx_link(
			rwnx_vif, mesh_start_cfm.ch_idx,
			(struct cfg80211_chan_def *)(&setup->chandef));
		if (rwnx_hw->cur_chanctx != mesh_start_cfm.ch_idx) {
			txq_status = RWNX_TXQ_STOP_CHAN;
		}
		rwnx_txq_vif_init(rwnx_hw, rwnx_vif, txq_status);
		spin_unlock_bh(&rwnx_hw->cb_lock);

		netif_tx_start_all_queues(dev);
		netif_carrier_on(dev);

		/* If the AP channel is already the active, we probably skip radar
               activation on MM_CHANNEL_SWITCH_IND (unless another vif use this
               ctxt). In anycase retest if radar detection must be activated
             */
		if (rwnx_hw->cur_chanctx == mesh_start_cfm.ch_idx) {
			rwnx_radar_detection_enable_on_cur_channel(rwnx_hw);
		}
		break;

	case CO_BUSY:
		error = -EINPROGRESS;
		break;

	default:
		error = -EIO;
		break;
	}

	/* Print information about the operation */
	if (error) {
		netdev_info(dev, "Failed to start MP (%d)", error);
	} else {
		netdev_info(dev, "MP started: ch=%d, bcmc_idx=%d",
			    rwnx_vif->ch_index, rwnx_vif->ap.bcmc_index);
	}

	return error;
}

/**
 * @leave_mesh: leave the current mesh network
 * (invoked with the wireless_dev mutex held)
 */
static int rwnx_cfg80211_leave_mesh(struct wiphy *wiphy, struct net_device *dev)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct mesh_stop_cfm mesh_stop_cfm;
	int error = 0;

	error = rwnx_send_mesh_stop_req(rwnx_hw, rwnx_vif, &mesh_stop_cfm);

	if (error == 0) {
		/* Check the status */
		switch (mesh_stop_cfm.status) {
		case CO_OK:
			spin_lock_bh(&rwnx_hw->cb_lock);
			rwnx_chanctx_unlink(rwnx_vif);
			rwnx_radar_cancel_cac(&rwnx_hw->radar);
			spin_unlock_bh(&rwnx_hw->cb_lock);
			/* delete BC/MC STA */
			rwnx_txq_vif_deinit(rwnx_hw, rwnx_vif);
			rwnx_del_bcn(&rwnx_vif->ap.bcn);

			netif_tx_stop_all_queues(dev);
			netif_carrier_off(dev);

			break;

		default:
			error = -EIO;
			break;
		}
	}

	if (error) {
		netdev_info(dev, "Failed to stop MP");
	} else {
		netdev_info(dev, "MP Stopped");
	}

	return 0;
}

#ifdef CONFIG_PM

#define RWNX_WOWL_MAXPATTERNS 4 //8
#define RWNX_WOWL_MAXPATTERNSIZE 64 //128
#define RWNX_WOWL_MINPATTERNSIZE 1
#define RWNX_WOWL_MAXPKTOFFSET 128

static const struct wiphy_wowlan_support rwnx_wowlan_support = {
	.flags = WIPHY_WOWLAN_ANY | WIPHY_WOWLAN_DISCONNECT |
		 WIPHY_WOWLAN_GTK_REKEY_FAILURE |
		 WIPHY_WOWLAN_SUPPORTS_GTK_REKEY |
		 WIPHY_WOWLAN_EAP_IDENTITY_REQ | WIPHY_WOWLAN_4WAY_HANDSHAKE,
	.n_patterns = RWNX_WOWL_MAXPATTERNS,
	.pattern_max_len = RWNX_WOWL_MAXPATTERNSIZE,
	.pattern_min_len = RWNX_WOWL_MINPATTERNSIZE,
	.max_pkt_offset = RWNX_WOWL_MAXPKTOFFSET,
};

/**
    We create a pattern to support suspend in this case(WIPHY_WOWLAN_ANY):
    The driver simulates the cfg80211 layer and generates the
    necessary pattern parameters to support wakeup
**/
void rwnx_wowlan_any_flag_makeup(struct wiphy *wiphy,
				 struct cfg80211_wowlan *wowl)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
	struct cfg80211_pkt_pattern *patterns = NULL;
#else
	struct cfg80211_wowlan_trig_pkt_pattern *patterns = NULL;
#endif

	if (wowl && wowl->any) {
		patterns = wowl->patterns =
			kzalloc(sizeof(*patterns),
				in_softirq() ? GFP_ATOMIC : GFP_KERNEL);
		WQ_ASSERT(patterns != NULL, "%s patterns is NULL", __func__);

		wowl->n_patterns = 1;
		patterns->pattern_len = 10; // fc(2B) + duration(2B) + addr1(6B)

		patterns->pattern =
			kzalloc(patterns->pattern_len,
				in_softirq() ? GFP_ATOMIC : GFP_KERNEL);
		WQ_ASSERT(patterns->pattern != NULL,
			  "%s pattern length is NULL", __func__);

		patterns->mask =
			kzalloc(patterns->pattern_len / 8 + 1,
				in_softirq() ? GFP_ATOMIC : GFP_KERNEL);
		WQ_ASSERT(patterns->mask != NULL, "%s mask length is NULL",
			  __func__);

		// Fill the pattern with mac addr data
		memcpy((uint8_t *)patterns->pattern + 4, wiphy->perm_addr,
		       MAC_ADDR_LEN);
		*(uint16_t *)patterns->mask = 0x3f0;
	}
}

void rwnx_wowlan_any_flag_cleanup(struct cfg80211_wowlan *wowl)
{
	if (wowl && wowl->any) {
		kfree(wowl->patterns->pattern);
		kfree(wowl->patterns->mask);
		kfree(wowl->patterns);
		wowl->patterns = NULL;
		wowl->n_patterns = 0;
	}
}

int rwnx_cfg80211_dummy_suspend(struct wiphy *wiphy,
				struct cfg80211_wowlan *wowl)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct cfg80211_scan_request *scan_request = rwnx_hw->scan_request;
	int ret = 0;

	ENTER();
	/* 1. stop scan if exist */
	if (NULL != scan_request) {
		rwnx_cfg80211_abort_scan(wiphy, scan_request->wdev);
	} else {
		WQ_DBG(DM_GENERIC, DL_WRN,
		       "rwnx_cfg80211_dummy_suspend: not scanning.\n");
	}

	/* 2. Trigger pcie suspend continue */
	complete(&rwnx_hw->cfg80211_suspend_wait);
	LEAVE();

	return ret;
}

int rwnx_cfg80211_suspend_wow(struct wiphy *wiphy, struct cfg80211_wowlan *wowl)
{
	unsigned long rem;
	int ret = 0;
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);

	/* 1. Take mutex to wait scan indication completed. */
	mutex_lock(&rwnx_hw->mutex);
	rwnx_wowlan_any_flag_makeup(wiphy, wowl);
	ret = rwnx_send_me_set_wowlan_req(rwnx_hw, wowl, WOW_SUSPEND, NULL);
	rwnx_wowlan_any_flag_cleanup(wowl);
	mutex_unlock(&rwnx_hw->mutex);

	if (ret) {
		return ret;
	}

	// 2. wait until fw ring switch finish for PCIe case, completion directly for SDIO. */
	rem = wait_for_completion_timeout(&rwnx_hw->wow_suspend_wait,
					  msecs_to_jiffies(2000));
	if (!rem) {
		return -ETIMEDOUT;
	}

	return 0;
}

int rwnx_cfg80211_suspend(struct wiphy *wiphy, struct cfg80211_wowlan *wowl)
{
	unsigned long rem;
	int ret = 0;
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct cfg80211_scan_request *scan_request = rwnx_hw->scan_request;

	ENTER();

	/* Flush data in waitQ. */
	htc_tx_flush_waitq(rwnx_hw->core);

	/* 1. stop scan. */
	if (NULL != scan_request) {
		rwnx_cfg80211_abort_scan(wiphy, scan_request->wdev);
	} else {
		WQ_DBG(DM_GENERIC, DL_WRN,
		       "rwnx_cfg80211_suspend: not scanning.\n");
	}

	/* 2. Requst target for WOW. */
	ret = rwnx_cfg80211_suspend_wow(wiphy, wowl);
	if (ret) {
		WQ_DBG(DM_GENERIC, DL_WRN,
		       "rwnx_cfg80211_suspend: WOW suspend timeout.\n");
		return ret;
	}

	// 3. wait until cfg80211 suspend finish.
	rem = wait_for_completion_timeout(&rwnx_hw->cfg80211_suspend_wait,
					  msecs_to_jiffies(5000));
	if (!rem) {
		WQ_DBG(DM_TRBUS, DL_WRN, "failed to suspend wlan(timeout).\n");
		return -ETIMEDOUT;
	}

	rwnx_cfg80211_timer_shutdown(rwnx_hw);

	/* Wait command idle. */
	if (cmd_mgr_suspend(&rwnx_hw->cmd_mgr, 3000)) {
		WQ_DBG(DM_GENERIC, DL_WRN,
		       "rwnx_cfg80211_suspend : Command busy !\n");
		return -ETIMEDOUT;
	}

	if (!wq_core_wait_data_path_idle(rwnx_hw->core)) {
		WQ_DBG(DM_GENERIC, DL_WRN,
		       "rwnx_cfg80211_suspend : Data path not complate !\n");
		/* return -ETIMEDOUT; Not treat as error.*/
	}

	/* FIXME: suspend device */

	WQ_DBG(DM_GENERIC, DL_WRN, "[auto]msg: rwnx_cfg80211_suspend: done.\n");

	LEAVE();

	return 0;
}

int rwnx_cfg80211_dummy_resume(struct wiphy *wiphy)
{
	// do nothing, just for fix kernel WARN_ON
	return 0;
}

int rwnx_cfg80211_resume_wow(struct wiphy *wiphy)
{
	struct rwnx_vif *rwnx_vif;
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);

	struct cfg80211_wowlan_wakeup wakeup_report __attribute__((unused));
	int ret = 0;
	u32 wakeup_reason;
	bool wakeup_reason_report __attribute__((unused)) = true;

	/* FIXME: resume device */
	mutex_lock(&rwnx_hw->mutex);
	ret = rwnx_send_me_set_wowlan_req(rwnx_hw, NULL, WOW_RESUME,
					  &wakeup_reason);
	mutex_unlock(&rwnx_hw->mutex);

	rwnx_vif = rwnx_hw->vif_table[(wakeup_reason >> 24) & 0xff];

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
	if (wiphy->wowlan_config && wakeup_reason) {
		memset(&wakeup_report, 0,
		       sizeof(struct cfg80211_wowlan_wakeup));
		wakeup_report.pattern_idx = -1;

		switch (wakeup_reason & 0xffffff) {
		case WOW_WAKEUP_4WAY_HANDSHAKE_BIT:
			if (wiphy->wowlan_config->four_way_handshake)
				wakeup_report.four_way_handshake = true;
			break;
		case WOW_WAKEUP_802_1X_BIT:
			if (wiphy->wowlan_config->eap_identity_req)
				wakeup_report.eap_identity_req = true;
			break;
		case WOW_WAKEUP_PATTERN_BIT:
			wakeup_report.pattern_idx = 1;
			break;
		case WOW_WAKEUP_CONNECTION_LOST_BIT:
			if (wiphy->wowlan_config->disconnect)
				wakeup_report.disconnect = true;
			break;
		case WOW_WAKEUP_GTK_REKEY_FAILURE:
			if (wiphy->wowlan_config->gtk_rekey_failure)
				wakeup_report.gtk_rekey_failure = true;
			break;
		default:
			wakeup_reason_report = false;
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "unknown wakeup reason:0x%x.\n", wakeup_reason);
			ret = -EINVAL;
		}
		if (wakeup_reason_report) {
			cfg80211_report_wowlan_wakeup(
				&rwnx_vif->wdev, &wakeup_report, GFP_KERNEL);
		}
	}
#endif

	WQ_DBG(DM_GENERIC, DL_WRN,
	       "rwnx_cfg80211_resume: ret = %x, wakeup reason = 0x%x.\n", ret,
	       wakeup_reason & 0xffffff);

	return ret;
}

int rwnx_cfg80211_resume(struct wiphy *wiphy)
{
	int ret = 0;
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);

	ENTER();

	cmd_mgr_resume(&rwnx_hw->cmd_mgr);

	rwnx_cfg80211_timer_setup(rwnx_hw);

	ret = rwnx_cfg80211_resume_wow(wiphy);

	/* start txq */
	rwnx_txq_start_all(rwnx_hw, TXQ_STOP_REASON_SUSPEND);

	LEAVE();

	return ret;
}

static int rwnx_cfg80211_set_rekey_data(struct wiphy *wiphy,
					struct net_device *netdev,
					struct cfg80211_gtk_rekey_data *key)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *vif = netdev_priv(netdev);

	WQ_DBG(DM_GENERIC, DL_WRN, "%s", __func__);

	if (RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_STATION)
		return -EINVAL;

	return rwnx_send_rekey_data_set(rwnx_hw, vif->vif_index, key);
}
#endif

static struct cfg80211_ops rwnx_cfg80211_ops = {
	.add_virtual_intf = rwnx_cfg80211_add_iface,
	.del_virtual_intf = rwnx_cfg80211_del_iface,
	.change_virtual_intf = rwnx_cfg80211_change_iface,
	.scan = rwnx_cfg80211_scan,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
	.abort_scan = rwnx_cfg80211_abort_scan,
#endif
	.connect = rwnx_cfg80211_connect,
	.disconnect = rwnx_cfg80211_disconnect,
	.add_key = rwnx_cfg80211_add_key,
	.get_key = rwnx_cfg80211_get_key,
	.del_key = rwnx_cfg80211_del_key,
	.set_default_key = rwnx_cfg80211_set_default_key,
	.set_default_mgmt_key = rwnx_cfg80211_set_default_mgmt_key,
	.add_station = rwnx_cfg80211_add_station,
	.del_station = rwnx_cfg80211_del_station,
	.change_station = rwnx_cfg80211_change_station,
	.mgmt_tx = rwnx_cfg80211_mgmt_tx,
	.start_ap = rwnx_cfg80211_start_ap,
	.change_beacon = rwnx_cfg80211_change_beacon,
	.stop_ap = rwnx_cfg80211_stop_ap,
	.set_monitor_channel = rwnx_cfg80211_set_monitor_channel,
	.probe_client = rwnx_cfg80211_probe_client,
	.set_wiphy_params = rwnx_cfg80211_set_wiphy_params,
	.set_txq_params = rwnx_cfg80211_set_txq_params,
	.set_tx_power = rwnx_cfg80211_set_tx_power,
	//    .get_tx_power = rwnx_cfg80211_get_tx_power,
	.set_power_mgmt = rwnx_cfg80211_set_power_mgmt,
	.get_station = rwnx_cfg80211_get_station,
	.dump_station = rwnx_cfg80211_dump_station,
	.remain_on_channel = rwnx_cfg80211_remain_on_channel,
	.cancel_remain_on_channel = rwnx_cfg80211_cancel_remain_on_channel,
	.dump_survey = rwnx_cfg80211_dump_survey,
	.get_channel = rwnx_cfg80211_get_channel,
	.start_radar_detection = rwnx_cfg80211_start_radar_detection,
	.update_ft_ies = rwnx_cfg80211_update_ft_ies,
	.set_cqm_rssi_config = rwnx_cfg80211_set_cqm_rssi_config,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
	.channel_switch = rwnx_cfg80211_channel_switch,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
	.tdls_channel_switch = rwnx_cfg80211_tdls_channel_switch,
	.tdls_cancel_channel_switch = rwnx_cfg80211_tdls_cancel_channel_switch,
#endif
	.tdls_mgmt = rwnx_cfg80211_tdls_mgmt,
	.tdls_oper = rwnx_cfg80211_tdls_oper,
	.change_bss = rwnx_cfg80211_change_bss,
	.set_bitrate_mask = rwnx_cfg80211_set_bitrate,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0) ||                          \
	defined(CONFIG_EXTERNAL_AUTH_PATCH)
	.external_auth = rwnx_cfg80211_external_auth,
#endif
#ifdef CONFIG_PM
	/*
      Called by pcie suspend/resume APIs by now, bellow APIs
      just for sync timing.
    */
	.suspend = rwnx_cfg80211_dummy_suspend,
	.resume = rwnx_cfg80211_dummy_resume,
	.set_rekey_data = rwnx_cfg80211_set_rekey_data,
#endif

	.sched_scan_start = rwnx_cfg80211_sched_scan_start,
	.sched_scan_stop = rwnx_cfg80211_sched_scan_stop,
};

/*********************************************************************
 * Init/Exit functions
 *********************************************************************/
void rwnx_wdev_unregister(struct rwnx_hw *rwnx_hw)
{
	struct rwnx_vif *rwnx_vif, *tmp;

	rtnl_lock();
	list_for_each_entry_safe (rwnx_vif, tmp, &rwnx_hw->vifs, list) {
		rwnx_cfg80211_del_iface(rwnx_hw->wiphy, &rwnx_vif->wdev);
	}
	rtnl_unlock();
}

static void rwnx_set_vers(struct rwnx_hw *rwnx_hw)
{
	u32 vers = rwnx_hw->version_cfm.version_lmac;

	ENTER();

	snprintf(rwnx_hw->wiphy->fw_version, sizeof(rwnx_hw->wiphy->fw_version),
		 "%d.%d.%d.%d", (vers & (0xff << 24)) >> 24,
		 (vers & (0xff << 16)) >> 16, (vers & (0xff << 8)) >> 8,
		 (vers & (0xff << 0)) >> 0);

	WQ_DBG(DM_GENERIC, DL_WRN, "[EMU] FW version : %s\n",
	       rwnx_hw->wiphy->fw_version);
	rwnx_hw->machw_type =
		rwnx_machw_type(rwnx_hw->version_cfm.version_machw_2);
}

static void rwnx_enable_mesh(struct rwnx_hw *rwnx_hw)
{
	struct wiphy *wiphy = rwnx_hw->wiphy;

	if (!rwnx_hw->mod_params.mesh)
		return;

	rwnx_cfg80211_ops.add_mpath = rwnx_cfg80211_add_mpath;
	rwnx_cfg80211_ops.del_mpath = rwnx_cfg80211_del_mpath;
	rwnx_cfg80211_ops.change_mpath = rwnx_cfg80211_change_mpath;
	rwnx_cfg80211_ops.get_mpath = rwnx_cfg80211_get_mpath;
	rwnx_cfg80211_ops.dump_mpath = rwnx_cfg80211_dump_mpath;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
	rwnx_cfg80211_ops.get_mpp = rwnx_cfg80211_get_mpp;
	rwnx_cfg80211_ops.dump_mpp = rwnx_cfg80211_dump_mpp;
#endif
	rwnx_cfg80211_ops.get_mesh_config = rwnx_cfg80211_get_mesh_config;
	rwnx_cfg80211_ops.update_mesh_config = rwnx_cfg80211_update_mesh_config;
	rwnx_cfg80211_ops.join_mesh = rwnx_cfg80211_join_mesh;
	rwnx_cfg80211_ops.leave_mesh = rwnx_cfg80211_leave_mesh;

	wiphy->flags |= (WIPHY_FLAG_MESH_AUTH | WIPHY_FLAG_IBSS_RSN);
	wiphy->features |= NL80211_FEATURE_USERSPACE_MPM;
	wiphy->interface_modes |= BIT(NL80211_IFTYPE_MESH_POINT);

	rwnx_limits[0].types |= BIT(NL80211_IFTYPE_MESH_POINT);
	rwnx_limits_dfs[0].types |= BIT(NL80211_IFTYPE_MESH_POINT);
}

static void rwnx_stat_timer_cb(struct timer_list *t)
{
	struct rwnx_hw *rwnx_hw = from_timer(rwnx_hw, t, stat_timer);
	int dump_mask = wq_conf.stats_dump_mask;

	//1. dump credit info
	if (dump_mask & WQ_STATS_DUMP_TX_CREDIT)
		rwnx_credit_dump_info(rwnx_hw);

	//2. dump hif info
	if (dump_mask & WQ_STATS_DUMP_HIF_INFO)
		hif_dump_info(rwnx_hw->core);

	if (dump_mask & WQ_STATS_DUMP_MAC_TXQ)
		rwnx_txq_stats_dump(rwnx_hw);

	//reorder info
	if (dump_mask & WQ_STATS_DUMP_REORDER)
		ieee80211_ampdu_reorder_dump_info(rwnx_hw, NULL, true, false, 0,
						  0);

	if (wq_conf.stats_dump_interval)
		mod_timer(t, jiffies + (HZ * wq_conf.stats_dump_interval));
}

static void dump_sta_txq(struct rwnx_hw *rwnx_hw)
{
	int i;

	for (i = 0; i < NX_VIRT_DEV_MAX; i++) {
		struct rwnx_vif *rwnx_vif;

		rwnx_vif = rwnx_hw->vif_table[i];
		if (rwnx_vif != NULL) {
			if ((RWNX_VIF_TYPE(rwnx_vif) ==
			     NL80211_IFTYPE_STATION) &&
			    rwnx_vif->sta.ap && rwnx_vif->sta.ap->valid) {
				struct rwnx_sta *rwnx_sta = rwnx_vif->sta.ap;
				u32 len[NX_NB_TXQ_PER_STA] = {};
				struct rwnx_txq *txq;
				int tid;

				foreach_sta_txq(rwnx_sta, txq, tid, rwnx_hw)
				{
					if (txq->idx == TXQ_INACTIVE)
						continue;
					len[tid] =
						skb_queue_len(&txq->sk_list) +
						skb_queue_len(
							&txq->sk_ack_list);
				}
				WQ_DBG(DM_GENERIC, DL_WRN,
				       "%pM TXQ length: %-3d %-3d %-3d %-3d, %-3d %-3d %-3d %-3d, %-3d\n",
				       rwnx_sta->mac_addr, len[0], len[1],
				       len[2], len[3], len[4], len[5], len[6],
				       len[7], len[8]);
			}
		}
	}
}

static void rwnx_throughput_calc_cb(struct timer_list *t)
{
	struct rwnx_hw *rwnx_hw = from_timer(rwnx_hw, t, tx_monitor_timer);
	struct credit_mgmt *crdt_mgmt = &rwnx_hw->crdt_mgmt;
	u8 i;
	u32 tx_tp = 0, tx_bytes = 0, tx_packets = 0;
	u32 rx_tp = 0, rx_bytes = 0, rx_packets = 0;
	u32 usb_send_cnt = 0, usb_com_ethip = 0;
	u8 compress_ratio_int = 0, compress_ratio_frac = 0;
	u8 sta_idx = 0;
	struct rwnx_msdu_txdone_status *txqring_sts = NULL;

	// stat compress eth/ip hdr ratio (for usb only)
	usb_send_cnt = atomic_read(&rwnx_hw->usb_send_cnt);
	usb_com_ethip = atomic_read(&rwnx_hw->usb_com_ethip);
	if (usb_send_cnt) {
		compress_ratio_int = (usb_com_ethip * 1000 / usb_send_cnt) / 10;
		compress_ratio_frac =
			(usb_com_ethip * 1000 / usb_send_cnt) % 10;
		atomic_set(&rwnx_hw->usb_send_cnt, 0);
		atomic_set(&rwnx_hw->usb_com_ethip, 0);
	}

	tx_bytes = atomic_read(&rwnx_hw->tx_bytes);
	tx_packets = atomic_read(&rwnx_hw->tx_packets);
	rx_bytes = atomic_read(&rwnx_hw->rx_bytes);
	rx_packets = atomic_read(&rwnx_hw->rx_packets);

	rwnx_hw->tx_throughput = tx_tp = (tx_bytes * 8) / (1 << 20) / 1;
	rwnx_hw->rx_throughput = rx_tp = (rx_bytes * 8) / (1 << 20) / 1;
	WQ_DBG(DM_GENERIC, DL_WRN,
	       "Tx_Tput: %u Mbps, Rx_Tput:%u Mbps, tx_pkt: %u, tx_bytes: %u, rx_pkt: %u, rx_bytes: %u, crdt_num: %u, "
	       "comp_ratio:%d.%d%%, start_xmit_cnt: %u, ipc_tx_cnt: %u, ipc_rx_cnt: %u, txll:%u - %u, "
	       "fwdone:%u - %u, hidone:%u - %u, txtcpack dly:%d - %d, stopcewm:%d - %d, stopsuspend:%d - %d\n",
	       tx_tp, rx_tp, tx_packets, tx_bytes, rx_packets, rx_bytes,
	       crdt_mgmt->drv_crdt_num, compress_ratio_int, compress_ratio_frac,
	       rwnx_hw->hard_start_xmit_cnt, rwnx_hw->ipc_tx_pkt_cnt,
	       atomic_read(&rwnx_hw->ipc_rx_pkt_cnt), rwnx_hw->ll_pkt_cnt,
	       rwnx_hw->free_ll_pkt_cnt, rwnx_hw->ll_fwdone_cnt,
	       rwnx_hw->ll_fwdone_free_cnt, rwnx_hw->ll_hifdone_cnt,
	       rwnx_hw->ll_hifdone_free_cnt,
	       atomic_read(&rwnx_hw->xmit_to_hifdone_ms_max),
	       atomic_read(&rwnx_hw->xmit_to_hif_ms_max),
	       rwnx_hw->ce_sw_watermark_in, rwnx_hw->ce_sw_watermark_out,
	       rwnx_hw->suspend_in, rwnx_hw->suspend_out);

	if (rwnx_hw->txq_ring_sts) {
		txqring_sts = rwnx_hw->txq_ring_sts;
		for (sta_idx = 0; sta_idx < TXQ_RING_RCD_SOFTAP_MAX;
		     sta_idx++) {
			if (txqring_sts->txq_ring_rcd[sta_idx]
				    .push_txqring_cnt[0] ||
			    txqring_sts->txq_ring_rcd[sta_idx]
				    .push_txqring_cnt[1] ||
			    txqring_sts->txq_ring_rcd[sta_idx]
				    .push_txqring_cnt[2] ||
			    txqring_sts->txq_ring_rcd[sta_idx]
				    .push_txqring_cnt[3]) {
				WQ_DBG(DM_GENERIC, DL_WRN,
				       "txq ring dump: sta:%d, push:%d, %d, %d, %d, pop:%d, %d, %d, %d\n",
				       sta_idx,
				       txqring_sts->txq_ring_rcd[sta_idx]
					       .push_txqring_cnt[0],
				       txqring_sts->txq_ring_rcd[sta_idx]
					       .push_txqring_cnt[1],
				       txqring_sts->txq_ring_rcd[sta_idx]
					       .push_txqring_cnt[2],
				       txqring_sts->txq_ring_rcd[sta_idx]
					       .push_txqring_cnt[3],
				       txqring_sts->txq_ring_rcd[sta_idx]
					       .pop_txqring_cnt[0],
				       txqring_sts->txq_ring_rcd[sta_idx]
					       .pop_txqring_cnt[1],
				       txqring_sts->txq_ring_rcd[sta_idx]
					       .pop_txqring_cnt[2],
				       txqring_sts->txq_ring_rcd[sta_idx]
					       .pop_txqring_cnt[3]);
			}
		}
	}

	if (rwnx_hw->core->hif_ops->hif !=
	    WQ_HIF_USB) //this is noisy for USB, so...
	{
		if (tx_packets < 20 || rwnx_hw->ipc_tx_pkt_cnt < 30) {
			dump_sta_txq(rwnx_hw);
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "crdt_num: %u, hard_start_xmit_cnt: %u, ipc_tx_pkt_cnt: %u\n",
			       crdt_mgmt->drv_crdt_num,
			       rwnx_hw->hard_start_xmit_cnt,
			       rwnx_hw->ipc_tx_pkt_cnt);
			for (i = 0; i < 5; i++)
				WQ_DBG(DM_GENERIC, DL_WRN,
				       "[%d]: credit:%d, jiffies: %lu, hard_start_xmit_cnt: %u, ipc_tx_pkt_cnt: %u\n",
				       i, rwnx_hw->record_stats[i].crdt_num,
				       rwnx_hw->record_stats[i].timestamp,
				       rwnx_hw->record_stats[i]
					       .hard_start_xmit_cnt,
				       rwnx_hw->record_stats[i].ipc_tx_pkt_cnt);
		}
	}
	mod_timer(t, jiffies + HZ);

	if (rwnx_hw->tx_mon_idx == 5)
		rwnx_hw->tx_mon_idx = 0;

	rwnx_hw->record_stats[rwnx_hw->tx_mon_idx].hard_start_xmit_cnt =
		rwnx_hw->hard_start_xmit_cnt;
	rwnx_hw->record_stats[rwnx_hw->tx_mon_idx].ipc_tx_pkt_cnt =
		rwnx_hw->ipc_tx_pkt_cnt;
	rwnx_hw->record_stats[rwnx_hw->tx_mon_idx].tx_bytes = tx_bytes;
	rwnx_hw->record_stats[rwnx_hw->tx_mon_idx].tx_packets = tx_packets;
	rwnx_hw->record_stats[rwnx_hw->tx_mon_idx].timestamp = jiffies;
	rwnx_hw->record_stats[rwnx_hw->tx_mon_idx].crdt_num =
		crdt_mgmt->drv_crdt_num;
	atomic_set(&rwnx_hw->tx_bytes, 0);
	atomic_set(&rwnx_hw->tx_packets, 0);
	rwnx_hw->hard_start_xmit_cnt = 0;
	rwnx_hw->ipc_tx_pkt_cnt = 0;
	rwnx_hw->tx_mon_idx++;
	atomic_set(&rwnx_hw->rx_bytes, 0);
	atomic_set(&rwnx_hw->rx_packets, 0);
	atomic_set(&rwnx_hw->ipc_rx_pkt_cnt, 0);
	atomic_set(&rwnx_hw->xmit_to_hifdone_ms_max, 0);
	atomic_set(&rwnx_hw->xmit_to_hif_ms_max, 0);
}

static void rwnx_ap_txdone_cb(struct timer_list *t)
{
	struct rwnx_hw *rwnx_hw = from_timer(rwnx_hw, t, ap_mgt_txdone_timer);
	rwnx_hw->ap_mgt_tx_ongoing = 0;
	WQ_DBG(DM_GENERIC, DL_WRN, "%s:: ap_mgt_tx_ongoing=%d", __func__,
	       rwnx_hw->ap_mgt_tx_ongoing);
}

static void rwnx_scan_timeout_cb(struct timer_list *t)
{
	struct rwnx_hw *rwnx_hw = from_timer(rwnx_hw, t, scan_timer);

	ENTER();

	/* abort scan request */
	if (rwnx_hw->scan_request) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
		struct cfg80211_scan_info info = {
			.aborted = true,
		};

		cfg80211_scan_done(rwnx_hw->scan_request, &info);
#else
		cfg80211_scan_done(rwnx_hw->scan_request, true);
#endif
		WQ_DBG(DM_GENERIC, DL_WRN, "%s: abort scan %p\n", __func__,
		       rwnx_hw->scan_request);
		mutex_unlock(&rwnx_hw->mutex);
	}
	rwnx_hw->scan_request = NULL;
}

void wq_hwq_process_all(struct rwnx_hw *rwnx_hw)
{
	int id;

	for (id = ARRAY_SIZE(rwnx_hw->hwq) - 1; id >= 0; id--) {
		if (rwnx_hw->hwq[id].need_processing) {
			rwnx_hwq_process(rwnx_hw, &rwnx_hw->hwq[id]);
		}
	}
}

void wq_hwq_task(unsigned long data)
{
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)data;
	u64 time_start_us = 0, time_end_us = 0;

	time_start_us = (u64)ktime_to_us(ktime_get());

	spin_lock_bh(&rwnx_hw->tx_lock);
	wq_hwq_process_all(rwnx_hw);
	spin_unlock_bh(&rwnx_hw->tx_lock);

	time_end_us = (u64)ktime_to_us(ktime_get());
	atomic_add((u32)(time_end_us - time_start_us),
		   &rwnx_hw->wq_hwq_task_time);
}

void wq_proc_init(struct rwnx_hw *rwnx_hw, struct wireless_dev *wdev);
void wq_proc_deinit(void);

static int wq_hw_vif_sta_table_init(struct rwnx_hw *rwnx_hw, u8 max_vif_nb,
				    u16 max_sta_nb)
{
	u16 num = max_vif_nb + max_sta_nb;

	WQ_DBG(DM_GENERIC, DL_WRN, "%s: max_vif_nb:%d, max_sta_nb:%d\n",
	       __func__, max_vif_nb, max_sta_nb);

	rwnx_hw->vif_table = (struct rwnx_vif **)kzalloc(
		sizeof(struct rwnx_vif *) * num, GFP_KERNEL);
	if (rwnx_hw->vif_table == NULL)
		goto err_vif;
	rwnx_hw->sta_table = (struct rwnx_sta *)kzalloc(
		sizeof(struct rwnx_sta) * num, GFP_KERNEL);
	if (rwnx_hw->sta_table == NULL) {
		goto err_sta;
	}

	return 0;
err_sta:
	kfree(rwnx_hw->vif_table);
err_vif:
	return -ENOMEM;
}

static void wq_hw_vif_sta_table_deinit(struct rwnx_hw *rwnx_hw)
{
	if (rwnx_hw->sta_table != NULL) {
		kfree(rwnx_hw->sta_table);
		rwnx_hw->sta_table = NULL;
	}
	if (rwnx_hw->vif_table != NULL) {
		kfree(rwnx_hw->vif_table);
		rwnx_hw->vif_table = NULL;
	}
}

char *cc_user = NULL;
module_param(cc_user, charp, 0);
MODULE_PARM_DESC(cc_user,
		 "Set country code from insmod param, ex. CN/US/JP...etc");

/**
 *
 */
int rwnx_cfg80211_init(struct wq_core *core)
{
	struct rwnx_hw *rwnx_hw;
	int ret = 0;
	struct wiphy *wiphy;
	struct wireless_dev *wdev;
	struct wireless_dev *p2p_dev = NULL;
	int i;
	struct me_tx_credit_size_cfm tx_credit_size_cfm;
	int mac_sum = 0;
	u32 link_speed = 0;

	ENTER();

	/* create a new wiphy for use with cfg80211 */
	wiphy = wiphy_new(&rwnx_cfg80211_ops, sizeof(struct rwnx_hw));

	if (!wiphy) {
		dev_err(core->dev, "Failed to create new wiphy\n");
		ret = -ENOMEM;
		goto err_out;
	}

	/* Set wiphy parent device as bus->dev. */
	set_wiphy_dev(wiphy, core->dev);

	rwnx_hw = wiphy_priv(wiphy);

	core->hw = rwnx_hw;

	rwnx_hw->wiphy = wiphy;
	rwnx_hw->core = core;
	rwnx_hw->dev = core->dev;
	rwnx_hw->mod_params = wq_conf;
	rwnx_hw->tcp_pacing_shift = 7;
	rwnx_hw->feature.autopm_disable = 1;
	rwnx_hw->feature.rx_rate_log_disable = 1;
	rwnx_hw->feature.rx_amsdu_disable = 1;
	/*Enable/Disable beamforming*/
	if (!rwnx_hw->mod_params.bfm_enable) {
		rwnx_hw->feature.bfmee_disable = 1;
		rwnx_hw->feature.murx_disable = 1;
	}

	if (cc_user) {
		for (i = 0; i < WQ_COUNTRY_CODE_LEN; i++) {
			rwnx_hw->mod_params.drvcc[i] = cc_user[i];
		}
	}
	WQ_DBG(DM_GENERIC, DL_WRN, "%s: drvcc:%s*\n", __func__,
	       rwnx_hw->mod_params.drvcc);

	rwnx_hw->he_ltf_gi = 0xffffffff;
	memset(&rwnx_hw->debugfs, 0, sizeof(struct rwnx_debugfs));

	ieee80211_ht_init();

	rwnx_hw->vif_started = 0;
	rwnx_hw->monitor_vif = RWNX_INVALID_VIF;

	rwnx_hw->scan_ie.addr = NULL;
	wq_rxu_defrag_init(rwnx_hw);
	rwnx_hwq_init(rwnx_hw);
	rwnx_mu_group_init(rwnx_hw);

	rwnx_hw->roc = NULL;
	rwnx_hw->roc_cookie = 1;

#if defined(CONFIG_HIGH_RES_TIMERS) && defined(USE_HRT)
	tasklet_init(&rwnx_hw->amsdu_task, amsdu_task, (unsigned long)rwnx_hw);
	INIT_LIST_HEAD(&rwnx_hw->amsdu_list_head);
#endif

	WQ_DBG(DM_GENERIC, DL_WRN, "%s: wiphy=0x%p, rwnx_hw=0x%p\n", __func__,
	       wiphy, rwnx_hw);

	/* set device pointer for wiphy */
	//set_wiphy_dev(wiphy, rwnx_hw->dev); //USB project marked

	if (is_valid_ether_addr(rwnx_hw->mod_params.mac_addr)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
		ether_addr_copy(wiphy->perm_addr, rwnx_hw->mod_params.mac_addr);
#else
		(void)memcpy(wiphy->perm_addr, rwnx_hw->mod_params.mac_addr,
			     ETH_ALEN);
#endif
	} else {
		eth_random_addr(wiphy->perm_addr);
		memcpy(wiphy->perm_addr, "\x00\x03\x7f\x12",
		       4); /* to easily identify it */
		WQ_DBG(DM_GENERIC, DL_WRN, "Use random mac address: %pM\n",
		       wiphy->perm_addr);
	}

	wiphy->mgmt_stypes = rwnx_default_mgmt_stypes;

	wiphy->bands[NL80211_BAND_2GHZ] = &rwnx_band_2GHz;
#ifdef SUPPORT_5G_BAND
	wiphy->bands[NL80211_BAND_5GHZ] = &rwnx_band_5GHz;
#endif
	WQ_DBG(DM_GENERIC, DL_INF,
	       "%s(%d) wiphy->bands[NL80211_BAND_5GHZ] = (0x%p)\n", __func__,
	       __LINE__, wiphy->bands[NL80211_BAND_5GHZ]);
	wiphy->interface_modes =
		BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_AP) |
		BIT(NL80211_IFTYPE_AP_VLAN) | BIT(NL80211_IFTYPE_P2P_CLIENT) |
		BIT(NL80211_IFTYPE_P2P_GO) | BIT(NL80211_IFTYPE_MONITOR);
	wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL |
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
			WIPHY_FLAG_HAS_CHANNEL_SWITCH |
#endif
			WIPHY_FLAG_4ADDR_STATION | WIPHY_FLAG_4ADDR_AP;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
	wiphy->max_num_csa_counters = BCN_MAX_CSA_CPT;
#endif

	wiphy->max_remain_on_channel_duration = rwnx_hw->mod_params.roc_dur_max;

	wiphy->features |=
		NL80211_FEATURE_NEED_OBSS_SCAN | NL80211_FEATURE_SK_TX_STATUS |
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
		NL80211_FEATURE_VIF_TXPOWER | NL80211_FEATURE_ACTIVE_MONITOR |
#else
		NL80211_FEATURE_VIF_TXPOWER |
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
		NL80211_FEATURE_AP_MODE_CHAN_WIDTH_CHANGE;
#else
		0;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0) ||                          \
	defined(CONFIG_EXTERNAL_AUTH_PATCH)
	wiphy->features |= NL80211_FEATURE_SAE;
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
	if (rwnx_hw->mod_params.tdls)
		/* TDLS support */
		wiphy->features |= NL80211_FEATURE_TDLS_CHANNEL_SWITCH;
#endif

	wiphy->iface_combinations = rwnx_combinations;
	/* -1 not to include combination with radar detection, will be re-added in
       rwnx_handle_dynparams if supported */
	wiphy->n_iface_combinations = ARRAY_SIZE(rwnx_combinations) - 1;

	wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;

	wiphy->cipher_suites = cipher_suites;
	wiphy->n_cipher_suites = ARRAY_SIZE(cipher_suites) - NB_RESERVED_CIPHER;

	wq_wiphy_vendor_init(wiphy);

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0)
	wiphy->ap_sme_capa = 1;
	wiphy->flags |= WIPHY_FLAG_HAVE_AP_SME;
#endif

#ifdef CONFIG_PM
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
	wiphy->wowlan = wq_conf.wow_enable ? &rwnx_wowlan_support : NULL;
#endif
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
	rwnx_hw->ext_capa[0] = WLAN_EXT_CAPA1_EXT_CHANNEL_SWITCHING;
#endif
	rwnx_hw->ext_capa[2] = WLAN_EXT_CAPA3_MULTI_BSSID_SUPPORT;
	rwnx_hw->ext_capa[7] = WLAN_EXT_CAPA8_OPMODE_NOTIF;

	wiphy->extended_capabilities = rwnx_hw->ext_capa;
	wiphy->extended_capabilities_mask = rwnx_hw->ext_capa;
	wiphy->extended_capabilities_len = ARRAY_SIZE(rwnx_hw->ext_capa);

	INIT_WORK(&rwnx_hw->add_key_task, rwnx_send_ptk_key_add);
	INIT_WORK(&rwnx_hw->update_nss_task, wq_nss_update_task_hdl);
	INIT_WORK(&rwnx_hw->ipv6_set_task, rwnx_ipv6_set_task);
	INIT_WORK(&rwnx_hw->disconnect_task, rwnx_disconnect_task);
	INIT_WORK(&rwnx_hw->bcn_change_task, rwnx_bcn_change_task);
	INIT_WORK(&rwnx_hw->tracer_dump_task, rwnx_tracer_dump_task);

	INIT_LIST_HEAD(&rwnx_hw->vifs);

	tasklet_init(&rwnx_hw->credit_task, wq_hwq_task,
		     (unsigned long)rwnx_hw);

	mutex_init(&rwnx_hw->dbgdump_elem.mutex);
	mutex_init(&rwnx_hw->mutex);
	spin_lock_init(&rwnx_hw->tx_lock);
	spin_lock_init(&rwnx_hw->cb_lock);
	spin_lock_init(&rwnx_hw->mgmt_hist_lock);
	spin_lock_init(&rwnx_hw->delayed_key_lock);
	spin_lock_init(&rwnx_hw->rx_defrag.defrag_lock);
	spin_lock_init(&rwnx_hw->txq_ring_lock);

	rwnx_hw->amsdu_param.max_len = AMSDU_MAX_SIZE;
	rwnx_hw->amsdu_param.max_packets_num = AMSDU_MAX_PTK_NUM;
	rwnx_hw->amsdu_param.timeout = AMSDU_TIMEOUT;
	rwnx_hw->amsdu_param.enable = true;
	rwnx_hw->ampdu_parm.max_aggr_num = AMPDU_MAX_PTK_NUM;

	/* for usb interface, we change the amsdu parameter to 4ms timeout
           and maximum packet 10 */
	if (core->hif_ops->hif == WQ_HIF_USB) {
		rwnx_hw->amsdu_param.max_packets_num = 10;
#if defined(CONFIG_HIGH_RES_TIMERS) && defined(USE_HRT)
		rwnx_hw->amsdu_param.timeout = 4000000;
#else
		rwnx_hw->amsdu_param.timeout = 4;
#endif
	}

#ifdef NAPI_SUPPORT
	skb_queue_head_init(&rwnx_hw->napi_rx_pkt_list);
	rwnx_hw->napi_param.napi_enable = core->config.napi_enable;
	rwnx_hw->napi_param.packets_num = 30;
	rwnx_hw->napi_param.timeout = 4000000;
	rwnx_hw->napi_param.param_enable = false;
	WQ_DBG(DM_GENERIC, DL_WRN, "%s: napi_enable %d, napi param_enable %d\n",
	       __func__, rwnx_hw->napi_param.napi_enable,
	       rwnx_hw->napi_param.param_enable);
#endif
	init_completion(&rwnx_hw->wow_suspend_wait);
	init_completion(&rwnx_hw->cfg80211_suspend_wait);
	init_completion(&rwnx_hw->roc_wait);
	timer_setup(&rwnx_hw->key_add_timer, rwnx_ptk_add_timeout, 0);
	timer_setup(&rwnx_hw->roc_timer, rwnx_roc_timeout, 0);

	/* After interface created and notification sent to the upper layer,
       rwnx_open may be invoked and use the timer (in mod_timer). So move the
       timer initialization here prior to register_inetaddr_notifier. */
	timer_setup(&rwnx_hw->stat_timer, rwnx_stat_timer_cb, 0);
	timer_setup(&rwnx_hw->tx_monitor_timer, rwnx_throughput_calc_cb, 0);
	timer_setup(&rwnx_hw->ap_mgt_txdone_timer, rwnx_ap_txdone_cb, 0);
	timer_setup(&rwnx_hw->scan_timer, rwnx_scan_timeout_cb, 0);

	atomic_set(&rwnx_hw->xmit_to_hifdone_ms_max, 0);
	atomic_set(&rwnx_hw->xmit_to_hif_ms_max, 0);

	atomic_set(&rwnx_hw->sending, 0);
	q_stats_init(&rwnx_hw->tx_stats, 0);

	rwnx_cmd_mgr_init(&rwnx_hw->cmd_mgr);

#if 0 //USB project marked
    /* Reset FW */
    if ((ret = rwnx_send_reset(rwnx_hw)))
        goto err_lmac_reqs;
#endif
	if ((ret = rwnx_send_version_req(rwnx_hw, &rwnx_hw->version_cfm)))
		goto err_lmac_reqs;
	rwnx_set_vers(rwnx_hw);

	if ((ret = rwnx_send_version_ext_req(rwnx_hw,
					     &rwnx_hw->version_ext_cfm)))
		goto err_lmac_reqs;

	NX_VIRT_DEV_MAX = rwnx_hw->version_cfm.max_vif_nb;
	NX_REMOTE_STA_MAX = rwnx_hw->version_cfm.max_sta_nb;
	global_data_init(NX_VIRT_DEV_MAX, NX_REMOTE_STA_MAX);

	if (NX_REMOTE_STA_MAX > 20)
		rwnx_hw->large_ap_mode = 1;

	for (i = 0; i < NX_VIRT_DEV_MAX + NX_REMOTE_STA_MAX; i++)
		rwnx_hw->avail_idx_map |= BIT_ULL(i);
	if (wq_hw_vif_sta_table_init(rwnx_hw, NX_VIRT_DEV_MAX,
				     NX_REMOTE_STA_MAX)) {
		WQ_DBG(DM_GENERIC, DL_ERR, "malloc table fail\n");
		goto err_malloc;
	}
	NX_NB_TXQ = ((NX_NB_TXQ_PER_STA * NX_REMOTE_STA_MAX) +
		     (NX_NB_TXQ_PER_VIF * NX_VIRT_DEV_MAX) + 1);
	WQ_DBG(DM_GENERIC, DL_ERR, "VIF_MAX:%d,STA_MAX:%d,NX_NB_TXQ:%d\n",
	       NX_VIRT_DEV_MAX, NX_REMOTE_STA_MAX, NX_NB_TXQ);
	if (rwnx_txq_malloc(rwnx_hw, NX_NB_TXQ)) {
		WQ_DBG(DM_GENERIC, DL_ERR, "malloc txq fail\n");
		goto err_malloc;
	}
	rwnx_txq_prepare(rwnx_hw);

	WQ_DBG(DM_GENERIC, DL_ERR, "ver req format=%x",
	       rwnx_hw->version_ext_cfm.format);
	if (rwnx_hw->version_ext_cfm.format ==
	    0x69) //magic code to identify mac address is indeed read from EFUSE
	{
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "EFUSE MAC:%02X:%02X:%02X:%02X:%02X:%02X",
		       rwnx_hw->version_ext_cfm.mac_addr[0],
		       rwnx_hw->version_ext_cfm.mac_addr[1],
		       rwnx_hw->version_ext_cfm.mac_addr[2],
		       rwnx_hw->version_ext_cfm.mac_addr[3],
		       rwnx_hw->version_ext_cfm.mac_addr[4],
		       rwnx_hw->version_ext_cfm.mac_addr[5]);

		mac_sum = 0;

		for (i = 0; i < ETH_ALEN; i++)
			mac_sum += rwnx_hw->version_ext_cfm.mac_addr[i];

		//do not use EFUSE mac address if it is all zero, or it's bit-0 is 1(group address)
		if ((mac_sum != 0) &&
		    ((rwnx_hw->version_ext_cfm.mac_addr[0] & 0x1) == 0)) {
			WQ_DBG(DM_GENERIC, DL_ERR, "use EFUSE mac address");
			memcpy(wiphy->perm_addr,
			       &rwnx_hw->version_ext_cfm.mac_addr[0], ETH_ALEN);
		}
	}

	if (core->hif_ops->hif == WQ_HIF_PCIE) {
		if (wq_conf.force_pcie_speed == FORCE_PCIE_LINK_SPEED_2_5_G ||
		    wq_conf.force_pcie_speed == FORCE_PCIE_LINK_SPEED_5_G) {
			ret = rwnx_send_force_pcie_link_speed_req(rwnx_hw,
								  &link_speed);
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "force PCIE link speed, config:%d, result:%d\n",
			       wq_conf.force_pcie_speed, link_speed);
			if (ret || (link_speed != wq_conf.force_pcie_speed)) {
				WQ_DBG(DM_GENERIC, DL_ERR,
				       "force PCIE link speed failed\n");
				goto err_lmac_reqs;
			}
		} else {
			if (wq_conf.force_pcie_speed !=
			    FORCE_PCIE_LINK_SPEED_ADAPTIVE)
				WQ_DBG(DM_GENERIC, DL_ERR,
				       "force PCIE link speed param (%d) invalid\n",
				       wq_conf.force_pcie_speed);
		}
	}

	if (core->config.rx_ll) {
		rx_ll_init(rwnx_hw);
		if ((ret = rwnx_send_set_host_ring_req(rwnx_hw,
						       &rwnx_hw->rx_ll)))
			goto err_lmac_reqs;
	} else {
		rwnx_hw->rx_ll.rx_ll_support = false;
	}

	WQ_DBG(DM_GENERIC, DL_WRN, "RX LL (cut through) mode: %s\n",
	       rwnx_hw->rx_ll.rx_ll_support ? "SUPPORT" : "NOT SUPPORT");

	/* config txq ring phy addr to fw */
	rwnx_txq_ring_init(rwnx_hw, NX_REMOTE_STA_MAX);

	if ((ret = rwnx_handle_dynparams(rwnx_hw, rwnx_hw->wiphy)))
		goto err_lmac_reqs;

	if ((ret = rwnx_send_me_tx_credit_size_req(rwnx_hw,
						   &tx_credit_size_cfm)))
		goto err_lmac_reqs;
	rwnx_credit_mgmt_init(&rwnx_hw->crdt_mgmt,
			      tx_credit_size_cfm.tx_credit_size);

	rwnx_enable_mesh(rwnx_hw);
	rwnx_radar_detection_init(&rwnx_hw->radar);

	/* Set parameters to firmware */
	rwnx_send_me_config_req(rwnx_hw);

	/* Only monitor mode supported when custom channels are enabled */
	if (rwnx_hw->mod_params.custchan) {
		rwnx_limits[0].types = BIT(NL80211_IFTYPE_MONITOR);
		rwnx_limits[0].max = NX_VIRT_DEV_MAX;
		rwnx_limits_dfs[0].types = BIT(NL80211_IFTYPE_MONITOR);
		rwnx_limits_dfs[0].max = NX_VIRT_DEV_MAX;
	}

	if ((ret = wiphy_register(wiphy))) {
		wiphy_err(wiphy, "Could not register wiphy device\n");
		goto err_register_wiphy;
	}

	/* Work to defer processing of rx buffer */
	INIT_WORK(&rwnx_hw->defer_rx.work, rwnx_rx_deferred);
	skb_queue_head_init(&rwnx_hw->defer_rx.sk_list);

	/* Update regulatory (if needed) and set channel parameters to firmware
		(must be done after WiPHY registration) */
	wq_set_regd_wiphy(rwnx_hw, wiphy);

	rwnx_send_me_chan_config_req(rwnx_hw);

	if ((ret = rwnx_dbgfs_register(rwnx_hw, "rwnx"))) {
		wiphy_err(wiphy, "Failed to register debugfs entries");
		goto err_debugfs;
	}
	//send ini conf info
	rwnx_send_ini_conf_req(rwnx_hw);

	rtnl_lock();

	/* Add an initial interface */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
	wdev = rwnx_interface_add(rwnx_hw, "wlan%d", NET_NAME_UNKNOWN,
#else
	wdev = rwnx_interface_add(rwnx_hw, "wlan%d", 0,
#endif
				  rwnx_hw->mod_params.custchan ?
						NL80211_IFTYPE_MONITOR :
						NL80211_IFTYPE_STATION,
				  NULL);

	if (rwnx_hw->core->hif_ops->hif != WQ_HIF_USB ||
	    wq_conf.default_p2p_on_for_usb == true) {
		/* Add p2p interface */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
		p2p_dev = rwnx_interface_add(rwnx_hw, "p2p%d", NET_NAME_UNKNOWN,
#else
		p2p_dev = rwnx_interface_add(rwnx_hw, "p2p%d", 0,
#endif
					     NL80211_IFTYPE_P2P_CLIENT, NULL);
	}

	rtnl_unlock();

	if (!wdev) {
		wiphy_err(wiphy, "Failed to instantiate a network device\n");
		ret = -ENOMEM;
		goto err_add_interface;
	}

	if (rwnx_hw->core->hif_ops->hif != WQ_HIF_USB) {
		if (!p2p_dev) {
			wiphy_err(wiphy,
				  "Failed to instantiate a p2p device\n");
			ret = -ENOMEM;
			goto err_add_interface;
		}
	}

	if (rwnx_hw->core->hif_ops->hif == WQ_HIF_USB) {
		/* set max upload bundle size */
		rwnx_send_set_usb_param_req(rwnx_hw, WQ_USB_MTU_PKT_BUNDLE_I);
	}

#ifdef NAPI_SUPPORT
	if (rwnx_hw->core->config.napi_enable) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
		netif_napi_add(wdev->netdev, &rwnx_hw->napi_rx,
			       rwnx_napi_poll_rx);
#else
		netif_napi_add(wdev->netdev, &rwnx_hw->napi_rx,
			       rwnx_napi_poll_rx, NAPI_RX_WEIGHT);
#endif
		napi_enable(&rwnx_hw->napi_rx);

		hrtimer_init(&rwnx_hw->napi_rx_defer_timer, CLOCK_MONOTONIC,
			     HRTIMER_MODE_REL);
		rwnx_hw->napi_rx_defer_timer.function = rwnx_napi_sched_rx_cb;
		atomic_set(&rwnx_hw->napi_rx_time, 0);
	}
#endif

	wq_proc_init(rwnx_hw, wdev);

	wq_pktlog_init(&rwnx_hw->pktlog);

	wiphy_info(wiphy, "New interface create %s", wdev->netdev->name);

	rwnx_hw->fib_netdev_notifier.notifier_call = fib_netdev_event;
	ret = register_inetaddr_notifier(&rwnx_hw->fib_netdev_notifier);
	if (ret < 0) {
		printk("failed to register inetaddr notifier\n");
		goto err_add_interface;
	}

	rwnx_hw->inet6addr_notifier.notifier_call = inet6dev_event;
	ret = register_inet6addr_notifier(&rwnx_hw->inet6addr_notifier);
	if (ret < 0) {
		printk("failed to register inet6addr notifier\n");
		goto err_add_interface;
	}

	rwnx_hw->monitor_param.tx_mcs_idx = 0xFF;
	rwnx_hw->monitor_param.tmp_mcs_idx = 0xFF;

	return 0;

err_add_interface:
err_debugfs:
	wiphy_unregister(rwnx_hw->wiphy);
err_register_wiphy:
err_lmac_reqs:
err_malloc:
	rwnx_fw_trace_dump(rwnx_hw);
	//err_platon:
	//err_config:
	wiphy_free(wiphy);
err_out:
	core->hw = NULL;
	return ret;
}

/**
 *
 */
void rwnx_cfg80211_deinit(struct rwnx_hw *rwnx_hw)
{
	struct rwnx_vif *rwnx_vif;
	int i;
#ifdef CONFIG_RWNX_DEBUGFS
	struct rwnx_rc_config_save *cfg, *next;
	struct rwnx_debugfs *rwnx_debugfs = &rwnx_hw->debugfs;
#endif

	ENTER();

	WQ_DBG(DM_GENERIC, DL_INF, "%s: rwnx_hw=0x%p\n", __func__, rwnx_hw);

	if (!rwnx_hw)
		return;

	regulatory_hint(rwnx_hw->wiphy, "00"); //set to default
	//rwnx_dbgfs_unregister(rwnx_hw);
#ifdef CONFIG_RWNX_DEBUGFS
	list_for_each_entry_safe (cfg, next, &rwnx_debugfs->rc_config_save,
				  list) {
		list_del(&cfg->list);
		kfree(cfg);
	}
#endif
	rwnx_debugfs_deinit(&rwnx_hw->debugfs);

	rwnx_cmd_mgr_deinit(&rwnx_hw->cmd_mgr);

	/* abort scan request */
	if (rwnx_hw->scan_request) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
		struct cfg80211_scan_info info = {
			.aborted = true,
		};

		WQ_DBG(DM_GENERIC, DL_WRN, "abort scan request\n");
		cfg80211_scan_done(rwnx_hw->scan_request, &info);
#else
		WQ_DBG(DM_GENERIC, DL_WRN, "abort scan request\n");
		cfg80211_scan_done(rwnx_hw->scan_request, true);
#endif
		rwnx_hw->scan_request = NULL;

		mutex_unlock(&rwnx_hw->mutex);
	} else if (rwnx_hw->roc) {
		struct rwnx_roc *roc = rwnx_hw->roc;
		struct rwnx_vif *rwnx_vif = roc->vif;

		if (!roc->internal && roc->on_chan) {
			// If RoC has been started by the user space and hasn't been cancelled,
			// inform it that off-channel period has expired
			cfg80211_remain_on_channel_expired(
				&rwnx_vif->wdev, (u64)(rwnx_hw->roc_cookie),
				roc->chan, GFP_ATOMIC);
		}

		kfree(roc);
		rwnx_hw->roc = NULL;
		complete(&rwnx_hw->roc_wait);

		mutex_unlock(&rwnx_hw->mutex);
	}

	for (i = 0; i < NX_VIRT_DEV_MAX; i++) {
		rwnx_vif = rwnx_hw->vif_table[i];

		if (rwnx_vif != NULL) {
			if ((RWNX_VIF_TYPE(rwnx_vif) ==
			     NL80211_IFTYPE_STATION) ||
			    (RWNX_VIF_TYPE(rwnx_vif) ==
			     NL80211_IFTYPE_P2P_CLIENT)) {
				if (rwnx_vif->sta.ap != NULL) {
					if (rwnx_vif->sta.ap->valid) {
						//fix dbgfs memory leak in STA mode
						WQ_DBG(DM_GENERIC, DL_WRN,
						       "STA vif[%d]:rwnx_dbgfs_unregister_sta %p",
						       i, rwnx_vif->sta.ap);
						rwnx_dbgfs_unregister_sta(
							rwnx_hw,
							rwnx_vif->sta.ap);
					}
				}
			}
		}
	}

	rwnx_cfg80211_timer_shutdown(rwnx_hw);

	del_timer_sync(&rwnx_hw->txq_cleanup);
	del_timer_sync(&rwnx_hw->ap_mgt_txdone_timer);

#ifdef NAPI_SUPPORT
	if (rwnx_hw->core->config.napi_enable) {
		napi_disable(&rwnx_hw->napi_rx);
		netif_napi_del(&rwnx_hw->napi_rx);
	}
#endif

	wq_hw_vif_sta_table_deinit(rwnx_hw);
	rwnx_txq_free(rwnx_hw);
	rwnx_wdev_unregister(rwnx_hw);
	unregister_inetaddr_notifier(&rwnx_hw->fib_netdev_notifier);
	unregister_inet6addr_notifier(&rwnx_hw->inet6addr_notifier);
	wiphy_unregister(rwnx_hw->wiphy);
	rwnx_radar_detection_deinit(&rwnx_hw->radar);
	kmem_cache_destroy(rwnx_hw->sw_txhdr_cache);
	tasklet_kill(&rwnx_hw->credit_task);

	rwnx_hwq_deinit(rwnx_hw);
	wq_rxu_defrag_deinit(rwnx_hw);
	wq_pktlog_deinit(&rwnx_hw->pktlog);
	wq_proc_deinit();
	rx_ll_deinit(rwnx_hw);
	/* txq ring deinit */
	rwnx_txq_ring_deinit(rwnx_hw, NX_REMOTE_STA_MAX);
	/* free mem which used to store pwr data read from bin file*/
	free_pwr_tab_mem();

	cancel_work_sync(&rwnx_hw->add_key_task);
	cancel_work_sync(&rwnx_hw->update_nss_task);
	cancel_work_sync(&rwnx_hw->ipv6_set_task);
	cancel_work_sync(&rwnx_hw->disconnect_task);
	cancel_work_sync(&rwnx_hw->defer_rx.work);
	cancel_work_sync(&rwnx_hw->bcn_change_task);

	wiphy_free(rwnx_hw->wiphy);
}
