#ifndef _CONFIG_H_
#define _CONFIG_H_
#include "rwnx_compat.h"

#define WQ_WLAN_CONFIG_FILE_NAME "wq_wlan_settings.ini"

#ifndef byte
typedef unsigned char byte;
#endif

#define WQ_MAC_DEF                                                             \
	{                                                                      \
	} /*default: 00:00:00:00:00:00*/

/*default: CN */
#define WQ_COUNTRY_CODE_DEF "CN"
#define WQ_COUNTRY_CODE_LEN 3

#define WQ_FW_LEVEL_DEF                                                        \
	{                                                                      \
	}
#define WQ_FW_LEVEL_LEN 4

#define WQ_FW_LOG_PATH_DEF "/var/log/"
#define WQ_FW_LOG_PATH_LEN 64

#define WQ_CONF_ITEM(_name, _type, _default, desc) _type _name;
#define WQ_CONF_ARRAY_ITEM(_name, _type, _array_size, _default, desc)          \
	_type _name[_array_size];

#ifdef CONFIG_ANDROID
#define WQ_DRIVER_REG_ENABLE true
#else
#define WQ_DRIVER_REG_ENABLE false
#endif

#define WQ_STATS_HIF_START_BIT (8)
#define WQ_STATS_CE_DUMP_BITS (0xFF << WQ_STATS_HIF_START_BIT)

enum wq_stats_dump_mask {
	WQ_STATS_DUMP_MAC_TXQ = BIT(0), /* rwnx_txq_stats_dump() */
	WQ_STATS_DUMP_REORDER =
		BIT(1), /* ieee80211_ampdu_reorder_dump_info() */
	WQ_STATS_DUMP_TX_CREDIT = BIT(2), /* rwnx_credit_dump_info() */
	WQ_STATS_DUMP_HIF_INFO = BIT(3), /* hif_dump_info */

	WQ_STATS_DUMP_CE_CMD_TX = BIT(
		WQ_STATS_HIF_START_BIT), /* the following mask is related to hif_dump_info() */
	WQ_STATS_DUMP_CE_EVT_RX = BIT((WQ_STATS_HIF_START_BIT + 1)),
	WQ_STATS_DUMP_CE_PKT_TX = BIT((WQ_STATS_HIF_START_BIT + 2)),
	WQ_STATS_DUMP_CE_TXD_RX = BIT((WQ_STATS_HIF_START_BIT + 3)),
	WQ_STATS_DUMP_CE_PKT_RX = BIT((WQ_STATS_HIF_START_BIT + 4)),
	WQ_STATS_DUMP_CE_RAW_TX = BIT((WQ_STATS_HIF_START_BIT + 5)),
	WQ_STATS_DUMP_CE_RAW_RX = BIT((WQ_STATS_HIF_START_BIT + 6)),
	WQ_STATS_DUMP_CE_LOG_RX = BIT((WQ_STATS_HIF_START_BIT + 7)),
};

#define WQ_STATS_DUMP_MASK                                                     \
	(WQ_STATS_DUMP_MAC_TXQ /*| WQ_STATS_DUMP_REORDER*/                     \
	 | WQ_STATS_DUMP_TX_CREDIT | WQ_STATS_DUMP_HIF_INFO |                  \
	 WQ_STATS_DUMP_CE_CMD_TX | WQ_STATS_DUMP_CE_RAW_TX |                   \
	 WQ_STATS_DUMP_CE_RAW_RX | WQ_STATS_DUMP_CE_PKT_TX |                   \
	 WQ_STATS_DUMP_CE_EVT_RX)

#define WQ_CONF_ITEMS                                                                       \
	WQ_CONF_ITEM(ht_on, bool, true, "Enable HT (Default: true)")                        \
	WQ_CONF_ITEM(vht_on, bool, true, "Enable VHT (Default: true)")                      \
	WQ_CONF_ITEM(he_on, bool, true, "Enable HE (Default: true)")                        \
	WQ_CONF_ITEM(he_ul_on, bool, false,                                                 \
		     "Enable HE OFDMA UL (Default: false)")                                 \
	WQ_CONF_ITEM(ldpc_on, bool, true, "Enable LDPC (Default: true)")                    \
	WQ_CONF_ITEM(stbc_on, bool, true, "Enable STBC in RX (Default: true)")              \
	WQ_CONF_ITEM(gf_rx_on, bool, true,                                                  \
		     "Enable HT greenfield in reception (Default: true)")                   \
	WQ_CONF_ITEM(ap_uapsd_on, bool, false,                                               \
		     "Enable UAPSD in AP mode (Default: false)")                             \
	WQ_CONF_ITEM(sgi, bool, true,                                                       \
		     "Advertise Short Guard Interval support (Default: true)")              \
	WQ_CONF_ITEM(                                                                       \
		sgi80, bool, true,                                                          \
		"Advertise Short Guard Interval support for 80MHz (Default: true)")         \
	WQ_CONF_ITEM(use_2040, bool, true, "Enable 40MHz (Default: true)")                  \
	WQ_CONF_ITEM(use_80, bool, true, "Enable 80MHz (Default: true)")                    \
	WQ_CONF_ITEM(custregd, bool, false,                                                 \
		     "Use permissive custom regulatory"                                     \
		     "rules (for testing ONLY) (Default: false)")                           \
	WQ_CONF_ITEM(custchan, bool, false,                                                 \
		     "Extend channel set to non-standard"                                   \
		     "channels (for testing ONLY) (Default: false)")                        \
	WQ_CONF_ITEM(driver_reg_enable, bool, WQ_DRIVER_REG_ENABLE,                         \
		     "Use drivers to customize regulatory rules"                            \
		     "(Android Default: true or Other Default: false)")                     \
	WQ_CONF_ITEM(bfmee, bool, true,                                                     \
		     "Enable Beamformee Capability (Default: true)")                        \
	WQ_CONF_ITEM(bfmer, bool, true,                                                     \
		     "Enable Beamformer Capability (Default: true)")                        \
	WQ_CONF_ITEM(mesh, bool, true,                                                      \
		     "Enable Meshing Capability (Default: true)")                           \
	WQ_CONF_ITEM(murx, bool, true,                                                      \
		     "Enable MU-MIMO RX Capability (Default: true)")                        \
	WQ_CONF_ITEM(mutx, bool, true,                                                      \
		     "Enable MU-MIMO TX Capability (Default: true)")                        \
	WQ_CONF_ITEM(mutx_on, bool, true,                                                   \
		     "Enable MU-MIMO transmissions (Default: true)")                        \
	WQ_CONF_ITEM(                                                                       \
		listen_bcmc, bool, true,                                                    \
		"Wait for BC/MC traffic following DTIM beacon (Default: true)")             \
	WQ_CONF_ITEM(ps_on, bool, true, "Enable PowerSaving (Default: true)")               \
	WQ_CONF_ITEM(tdls, bool, true, "Enable TDLS (Default: true)")                       \
	WQ_CONF_ITEM(uf, bool, true,                                                        \
		     "Enable Unsupported HT Frame Logging (Default: true)")                 \
	WQ_CONF_ITEM(dpsm, bool, true,                                                      \
		     "Enable Dynamic PowerSaving (Default: true)")                          \
	WQ_CONF_ITEM(ant_div, bool, false,                                                  \
		     "Enable Antenna Diversity (Default: false)")                           \
	WQ_CONF_ITEM(bfm_enable, bool, true,                                                \
		     "Enable/Disable beamforming (Default: true)")                          \
	WQ_CONF_ITEM(loadfw_only, bool, false,                                              \
		     "load mp f/w instead of Wi-Fi f/w,"                                    \
		     " no MAC instance is created (Default: false)")                        \
	WQ_CONF_ITEM(nss1_force, bool, false, "spatial. (Default: false)")                  \
	WQ_CONF_ITEM(                                                                       \
		compress_txdesc, bool, false,                                               \
		"Enable compress txdesc_host to compressed_hostdesc (Default: false)")      \
	WQ_CONF_ITEM(stats_dump_interval, int, 10, "stats cycle interval \
					(Default: 10s)")                                    \
	WQ_CONF_ITEM(stats_dump_mask, int, WQ_STATS_DUMP_MASK,                              \
		     "\t\t	bit 0: WQ_STATS_DUMP_MAC_TXQ\n"                                  \
		     "\t\t	bit 1: WQ_STATS_DUMP_REORDER\n"                                  \
		     "\t\t	bit 2: WQ_STATS_DUMP_TX_CREDIT\n"                                \
		     "\t\t	bit 3: WQ_STATS_DUMP_HIF_INFO\n"                                 \
		     "\t\t	bit 8-15: pcie ce[2-9] dump \n"                                  \
		     "\t\t	(Default: 0x1D0D)")                                         \
	WQ_CONF_ITEM(                                                                       \
		mcs_map, int, IEEE80211_VHT_MCS_SUPPORT_0_9,                                \
		"VHT MCS map value: 0: MCS0_7, 1: MCS0_8, 2: MCS0_9(Default: 2)")           \
	WQ_CONF_ITEM(                                                                       \
		he_mcs_map, int, IEEE80211_HE_MCS_SUPPORT_0_11,                             \
		"HE MCS map value 0: MCS0_7, 1: MCS0_9, 2: MCS0_11 (Default: 2)")           \
	WQ_CONF_ITEM(phy_cfg, int, 0, "Main RF Path (Default: 0)")                          \
	WQ_CONF_ITEM(phy_calib_mode, int, WQ_TXRX_CALIB_MODE,                               \
		     "PHY TX/RX calibration mode(Default: 0xff)")                           \
	WQ_CONF_ITEM(                                                                       \
		uapsd_timeout, int, 300,                                                    \
		"UAPSD Timer timeout, in ms (Default: 300). If 0, UAPSD is disabled")       \
	WQ_CONF_ITEM(nss, int, 2,                                                           \
		     "1 <= nss <= 2 : "                                                     \
		     "Supported number of Spatial Streams (Default: 2)")                    \
	WQ_CONF_ITEM(roc_dur_max, int, 1200,                                                \
		     "Maximum Remain on Channel duration (Default: 1200)")                  \
	WQ_CONF_ITEM(listen_itv, int, 0,                                                    \
		     "Maximum listen interval (Default: 0)")                                \
	WQ_CONF_ITEM(                                                                       \
		lp_clk_ppm, int, 20,                                                        \
		"Low Power Clock accuracy of the local device (Default: 20)")               \
	WQ_CONF_ITEM(                                                                       \
		amsdu_rx_max, int, 2,                                                       \
		"0 <= amsdu_rx_max <= 2 : "                                                 \
		"Maximum A-MSDU size supported in RX\n"                                     \
		"\t\t	0: 3895 bytes\n"                                                      \
		"\t\t	1: 7991 bytes\n"                                                      \
		"\t\t	2: 11454 bytes\n"                                                     \
		"\t\t	Default: 2. It might be reduced according to the FW capabilities.") \
	WQ_CONF_ITEM(                                                                       \
		amsdu_maxnb, int, NX_TX_PAYLOAD_MAX,                                        \
		"Maximum number of MSDUs inside an A-MSDU in TX: (Default: 6)")             \
	WQ_CONF_ITEM(                                                                       \
		tx_lft, int, RWNX_TX_LIFETIME_MS,                                           \
		"Tx lifetime (ms) - setting it to 0 disables retries. Default: 100")        \
	WQ_CONF_ITEM(                                                                       \
		uapsd_queues, int, IEEE80211_WMM_IE_STA_QOSINFO_AC_VO,                      \
		"UAPSD Queues, integer value, must be seen as a bitfield\n"                 \
		"\t\t	Bit 0 = VO\n"                                                         \
		"\t\t	Bit 1 = VI\n"                                                         \
		"\t\t	Bit 2 = BK\n"                                                         \
		"\t\t	Bit 3 = BE\n"                                                         \
		"\t\t	uapsd_queues=7 will enable uapsd for VO, VI and BK queues")         \
	WQ_CONF_ITEM(                                                                       \
		tx_to_bk, int, 0,                                                           \
		"TX timeout for BK, in ms "                                                 \
		"(Default: 0, Max: 65535). If 0, default value is applied")                 \
	WQ_CONF_ITEM(                                                                       \
		tx_to_be, int, 0,                                                           \
		"TX timeout for BE, in ms "                                                 \
		"(Default: 0, Max: 65535). If 0, default value is applied")                 \
	WQ_CONF_ITEM(                                                                       \
		tx_to_vi, int, 0,                                                           \
		"TX timeout for VI, in ms "                                                 \
		"(Default: 0, Max: 65535). If 0, default value is applied")                 \
	WQ_CONF_ITEM(                                                                       \
		tx_to_vo, int, 0,                                                           \
		"TX timeout for VO, in ms "                                                 \
		"(Default: 0, Max: 65535). If 0, default value is applied")                 \
	WQ_CONF_ITEM(tx_bundle_max, byte, NX_TX_PAYLOAD_MAX,                                \
		     "tx msdu bundle max number "                                           \
		     "(Default: 6. 0 means ll is disabled. 0 - 6)")                         \
	WQ_CONF_ITEM(                                                                       \
		tx_bundle_expire_us, int, 20,                                               \
		"tx msdu bundle expire time in microseconds (Default: 20 us)")              \
	WQ_CONF_ITEM(                                                                       \
		tx_tcpack_expire_us, int, 5000,                                             \
		"tx TCP ACK bundle expire time in microseconds (Default: 5000 us)")         \
	WQ_CONF_ITEM(fw_sys, byte, 1,                                                       \
		     "fw select: \n"                                                        \
		     "\t\t	0:  dtop only\n"                                                 \
		     "\t\t	BIT 0: wifi\n"                                                   \
		     "\t\t	BIT 2: bt\n"                                                     \
		     "\t\t	default: BIT(0).")                                          \
	WQ_CONF_ITEM(wq_dbg_lv, byte, 3,                                                    \
		     "debug log level:\n"                                                   \
		     "\t\t\t0: none,\n"                                                     \
		     "\t\t\t1: oops,\n"                                                     \
		     "\t\t\t2: error,\n"                                                    \
		     "\t\t\t3: warning,\n"                                                  \
		     "\t\t\t4: info,\n"                                                     \
		     "\t\t\t5: verbose\n"                                                   \
		     "\t\t\tdefault = 3.")                                                  \
	WQ_CONF_ITEM(wq_dbg_mod, byte, 0xff,                                                \
		     "debug module bit mask:\n"                                             \
		     "\t\t\t0x01: IPC,\n"                                                   \
		     "\t\t\t0x02: TRBUS,\n"                                                 \
		     "\t\t\t0x04: RX,\n"                                                    \
		     "\t\t\t0x08: TX,\n"                                                    \
		     "\t\t\t0x10: MAC,\n"                                                   \
		     "\t\t\t0x20: generic,\n"                                               \
		     "\t\t\t0x40: credit,\n"                                                \
		     "\t\t\t0x80: pktdump\n"                                                \
		     "\t\t\tdefault = all.")                                                \
	WQ_CONF_ITEM(fw_log_enable, int, 0,                                                 \
		     "fw log enable: 0: disable, 1: enable")                                \
	WQ_CONF_ITEM(coex_ant_mode, int, 0,                                                 \
		     "coex antenna mode:\n"                                                 \
		     "\t\t\t0: COEX_BT_BYPASS,\n"                                           \
		     "\t\t\t1: COEX_BT_SHARE_WITH_SWITCH,\n"                                \
		     "\t\t\t2: COEX_BT_SHARE_NO_SWITCH,\n"                                  \
		     "\t\t\tdefault = 0.")                                                  \
	WQ_CONF_ITEM(coex_abort_prop_lv, int, 0,                                            \
		     "coex abort prop level:\n"                                             \
		     "\t\t\tdefault = 0.")                                                  \
	WQ_CONF_ITEM(fw_log_level, int, 4,                                                  \
		     "fw log level:\n"                                                      \
		     "\t\t\t0: NONE,\n"                                                     \
		     "\t\t\t1: CRIT,\n"                                                     \
		     "\t\t\t2: ERROR,\n"                                                    \
		     "\t\t\t3: WARNING,\n"                                                  \
		     "\t\t\t4: INFO,\n"                                                     \
		     "\t\t\t5: DEBUG\n"                                                     \
		     "\t\t\tdefault = 4.")                                                  \
	WQ_CONF_ITEM(fw_log_file_size, int, 6,                                              \
		     "fw log file max size (MB):\n"                                         \
		     "\t\t\tdefault = 6.")                                                  \
	WQ_CONF_ITEM(ps_mode, byte, 0,                                                      \
		     "power save mode bit mask:\n"                                          \
		     "\t\t\t0x01: wifi ip sleep,\n"                                         \
		     "\t\t\t0x02: wsys deep sleep,\n"                                       \
		     "\t\t\t0x04: bsys deep sleep,\n"                                       \
		     "\t\t\t0x08: dtop deep sleep,\n"                                       \
		     "\t\t\tdefault = 0.")                                                  \
	WQ_CONF_ARRAY_ITEM(drvcc, byte, WQ_COUNTRY_CODE_LEN,                                \
			   WQ_COUNTRY_CODE_DEF,                                             \
			   "Set country code, ex. CN/US/JP...etc")                          \
	WQ_CONF_ARRAY_ITEM(ftl, byte, WQ_FW_LEVEL_LEN, WQ_FW_LEVEL_DEF,                     \
			   "Firmware trace level")                                          \
	WQ_CONF_ARRAY_ITEM(mac_addr, byte, ETH_ALEN, WQ_MAC_DEF, "mac addr")                \
	WQ_CONF_ARRAY_ITEM(fw_log_path, byte, WQ_FW_LOG_PATH_LEN,                           \
			   WQ_FW_LOG_PATH_DEF,                                              \
			   "Path of fw log file, default is /var/log/ ")                    \
	WQ_CONF_ITEM(default_p2p_on_for_usb, bool, false,                                   \
		     "Create P2P interface automatically (Default: false)")                 \
	WQ_CONF_ITEM(tx_pwr_force_ena, int, 0,                                              \
		     "set tx_pwr_force_ena(Default: 0)")                                    \
	WQ_CONF_ITEM(tx_pwr_force_dbm, int, 0,                                              \
		     "set tx_pwr_force_dbm(Default: 0)")                                    \
	WQ_CONF_ITEM(force_pcie_speed, byte, 0,                                             \
		     "Force PCIE link speed:\n"                                             \
		     "\t\t\t0: disable, self-adaptive PCIE link speed\n"                    \
		     "\t\t\t1: force PCIE link speed to 2.5G/s,\n"                          \
		     "\t\t\t2: force PCIE link speed to 5G/s,\n"                            \
		     "\t\t\tdefault = 0.")                                                  \
	WQ_CONF_ITEM(tx_ampdu_disable, int, 0,                                              \
		     "set tx_ampdu_disable(Default: 0)")                                    \
	WQ_CONF_ITEM(force_edca_vo, int, 0,                                                 \
		     "set force edca be params to vo(0: disable, 1: enable)")               \
	WQ_CONF_ITEM(force_ignore_nav, int, 0,                                              \
		     "set ignore nav(0: disable, 1: enable)")                               \
	WQ_CONF_ITEM(max_support_ba_bitmap, int, 64,                                        \
		     "set max support ba bitmap (Default: 64)")                             \
	WQ_CONF_ITEM(dynbw_enable, bool, false,                                       \
		     "set dynbw support (Default: false)")                                \
	WQ_CONF_ITEM(wow_enable, bool, true,                                                \
		     "Enable wowlan support (Default: true)")                               \
	WQ_CONF_ITEM(sap_follow_sta_enable, bool, true,                                     \
		     "Enable SAP follow STA channel (Default: true)")                       \
	WQ_CONF_ITEM(underrun_adapt_tx_rate, bool, false,                                   \
		     "Adapt tx rate by underrun phy error (Default: false)")                \
	WQ_CONF_ITEM(android_platform, bool, false, "android_platform (Default: false)") \
	WQ_CONF_ITEM(tx_ampdu_enable, bool, true, "Enable AMPDU in TX (Default: true)") \
	WQ_CONF_ITEM(mmode, int, 0, "0 <= mmode <= 2 in TX (Default:0)") \
	WQ_CONF_ITEM(sched_scan_enable, bool, false, "Enable Sched_scan (Default: false)") \
	WQ_CONF_ITEM(extap_support, int, 0, "extAP support (Default: 0)") 

struct wq_conf {
	WQ_CONF_ITEMS
};
#undef WQ_CONF_ITEM
#undef WQ_CONF_ARRAY_ITEM

extern struct wq_conf wq_conf;

int wq_config_load(const char *filename);
void wq_config_dump(void);

#endif /* _RWNX_CFGFILE_H_ */
