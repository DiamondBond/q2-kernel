#ifndef _WQ_WIFI_DBG_H
#define _WQ_WIFI_DBG_H

#include <net/cfg80211.h>
#include "rwnx_defs.h"
#include "fw_api/wifi/mac/cp_api.h"
#include "rwnx_msg_tx.h"
#include "fw_api/wifi/mac/wq_pktdump.h"

typedef enum { WIFI_DBG_PKT_TX = 0, WIFI_DBG_PKT_RX = 1 } WIFI_DBG_PKT_DIR;
//extern int (*wifi_dbg_insert_func_ptr)(WIFI_DBG_PKT_DIR , void *, struct mbuf *, uint8_t cindex, uint8_t is_ampdu);

#define WQ_ERROR(error_code)                                                    \
	{                                                                       \
		printk("##################################################\n"); \
		printk("##### CRITICAL ERROR : %s #####\n", #error_code);       \
		printk("##################################################\n"); \
	}

#define WQ_ERROR_CMD_TIMEOUT(cmd_id)                                            \
	{                                                                       \
		printk("##################################################\n"); \
		printk("##### CRITICAL ERROR : CMD %s TIMEOUT #####\n",         \
		       RWNX_ID2STR(cmd_id));                                    \
		printk("##################################################\n"); \
	}

#ifndef KIWI_RELEASE_BUILD

//  Usage Note:
//  1. GDB
//  > dump binary memory dump.bin  start_addr end_addr
//  EX (128K) : dump binary memory dump.bin 0x100E0000 0x10100000
//  EX (32K) : dump binary memory  dump_32K_2.bin 0x10078000 0x10080000
//
//  2. OPENOCD
//  >halt
//  >dump_image  dump.bin 0x100E0000 0x20000
//  >exit
//
//  3. Gen pcapng
//  >python3 wifi/core/utils/gen_pcapng.py inputfile output.pcapng
//  EX:python3 wifi_core/utils/gen_pcapng.py  dump.bin dump.pcapng
//
//############################ format ###############################
//#  0                   1                   2                   3
//#  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//#  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//#  |                     PKT_START_PATTERN(4B)                     |
//#  +-------------------------------+-------------------------------+
//#  |                        MAC Time Stamp (4B)                    |
//#  +-------------------------------+-------------------------------+
//#  |     pkt_len(2B)               |   rate8b      |a|d|frame_cnt6b|
//#  +-------------------------------+-------------------------------+
//#  | cs4b  |  cindex6b |    rxrssi 8b   |   reserved 14 bits       |
//#  +-------------------------------+-------------------------------+
//#  |                      PROTO Time Stamp (4B)                    |
//#  +-------------------------------+-------------------------------+
//#  |                     DRIVER Time Stamp (4B)                    |
//#  +-------------------------------+-------------------------------+
//#  |                        HAL Time Stamp (4B)                    |
//#  +-------------------------------+-------------------------------+
//#  |                           pkt_data(32B)                       |
//#  |                           ............                        |
//#  |                           ............                        |
//#  +-------------------------------+-------------------------------+
//#
//#   rate8b: used rate
//#   frame_cnt6b: data send total cnt (TX only) (6bits)
//#   d     : direction (1bit), 0(tx),1(rx)
//#   a     : is ampdu (1bit)
//#   cs4b: complete status (4bits)
//#   cindex : 6bits (0~63)
//#
//############################ format ###############################

#define PKT_START_PATTERN 0x12344321
#define HIST_CNT 50

void wifi_dbg_enable(int enable);
void wq_packet_dump_evt_handler(WIFI_DBG_PKTDUMP *dpkt);
void print_mgmt_frame_info(char *note, struct ieee80211_mgmt *mgmt,
			   u16 mgmt_tx_len);

#else ////HAWK_RELEASE_BUILD

#define wifi_dbg_enable(enable)
#endif

struct dbg_err_ind {
	uint8_t sub_type;
	uint8_t reserved;
	uint16_t msg_len;
	uint8_t info_param[0];
};

enum dbg_err_ind_subtype {
	DBG_ERR_IND_REC,
	DBG_ERR_IND_WOW,
	DBG_ERR_IND_MAX,
};

typedef struct _wifi_mgmt_pkt {
	__kernel_long_t ts;
	uint8_t sn;
	uint8_t dir : 1;
	uint8_t mac_id : 2;
	uint8_t ack : 1;
	uint8_t reserved : 4;
	uint16_t frame_ctrl;
	uint8_t category;
	uint8_t action_type;
	uint8_t p2p;
	uint8_t da[ETH_ALEN];
	uint8_t sa[ETH_ALEN];
	uint32_t tx_status;
} __packed WIFI_MGMT_PKT;

struct hml_mgmt_to_host {
	uint8_t vif_index;
	uint8_t freq;
	uint8_t rssi;
	uint16_t buf_len;
	uint8_t frame_buf[0];
};

struct wq_dbg_security {
	enum nl80211_auth_type auth_type;
	uint8_t pairwise_cipher;
	uint8_t group_cipher;
	uint8_t mgmt_cipher;
	bool pmf_enable;
};

enum wq_wlan_version {
	WLAN_VER_LEGACY,
	WLAN_VER_11N,
	WLAN_VER_11AC,
	WLAN_VER_11AX,
};

struct wq_dbg_vif {
	char name[IFNAMSIZ];
	enum nl80211_iftype iftype;
	uint8_t mac_addr[ETH_ALEN];
	uint8_t bssid[ETH_ALEN];
	enum nl80211_band band;
	enum nl80211_chan_width width;
	enum nl80211_channel_type chan_type;
	uint16_t center_freq;
	uint32_t center_freq1;
	uint32_t center_freq2;
	enum wq_wlan_version wlan_version;
	u32 tkip_mic_failure_count;
	uint8_t credit_total[WQ_CREDIT_TYPE_NUM]; //total credit size
	uint8_t credit_avail[WQ_CREDIT_TYPE_NUM]; //available credit
	uint8_t credit_lend[WQ_CREDIT_TYPE_NUM]; //lend credit
};

struct wq_dbg_fw_sta_tx_stats {
	uint32_t single_success_cnt;
	uint32_t single_retry_cnt;
	uint32_t single_fail_cnt;
	uint32_t ampdu_success_cnt;
	uint32_t ampdu_retry_cnt;
	uint32_t ampdu_fail_cnt;
	uint32_t mac_total_tx_cnt;
	uint64_t mac_total_tx_len;
};

struct wq_dbg_fw_sta_rx_stats {
	uint8_t mac_id;
	uint32_t single_success_cnt;
	uint32_t ampdu_success_cnt;
	uint64_t mac_total_rx_len;
	uint32_t mac_mgmt_frame_cnt;
	uint32_t mac_ctrl_frame_cnt;
	uint32_t mac_data_frame_cnt;
	uint32_t mac_other_frame_cnt;
};

struct wq_dbg_fw_sta_trx_stats {
	struct wq_dbg_fw_sta_tx_stats tx_stats;
	struct wq_dbg_fw_sta_rx_stats rx_stats;
};

struct pkt_rate_cnt {
	uint32_t rate_1M;
	uint32_t rate_2M;
	uint32_t rate_5_5M;
	uint32_t rate_11M;

	uint32_t rate_6M;
	uint32_t rate_9M;
	uint32_t rate_12M;
	uint32_t rate_18M;
	uint32_t rate_24M;
	uint32_t rate_36M;
	uint32_t rate_48M;
	uint32_t rate_54M;

	uint32_t ht_mcs[8];
	uint32_t ht_mcs32;
	uint32_t ht_mcs_unknown;

	uint32_t vht_mcs[10];
	uint32_t vht_mcs_unknown;

	uint32_t he_su_mcs[12];
	uint32_t he_mu_mcs[12];
	uint32_t he_er_mcs[12];
	uint32_t he_mcs_unknown;

	uint32_t ampdu_cnt;
	uint32_t stbc_cnt;
};

/* FIXME: move it into emu_common */
struct wq_dbg_vif_ext_trx_stats {
	struct wq_dbg_fw_sta_rx_stats unkonwn_rx_stats;
	uint32_t bcn_succ_cnt;
	uint32_t bcn_fail_cnt;
	uint32_t mib_qos_fail_cnt[8];

	uint32_t arp_req_drop;
	uint32_t eth_802_3_drop;
	uint32_t arp_rsp_drop;
	uint32_t eth_802_1q_drop;
	uint32_t ipv4_192_1_681_255_drop;
	uint32_t ipv4_224_239_drop;
	uint32_t dhcp_from_client_drop;
	uint32_t dhcp_for_other_drop;
	uint32_t ipv6_rs_drop;
	uint32_t ipv6_na_drop;
	uint32_t ipv6_multicast_drop;

	struct pkt_rate_cnt tx_rate_cnt;
	struct pkt_rate_cnt rx_rate_cnt;

	uint16_t ampdu_size_record[32];
	uint8_t latest_ampdu_size;
};

struct wq_dbg_chan_noise_info {
	uint32_t duration;

	uint32_t wifi_busy_time;
	int16_t rssi_nonwifi;
	uint32_t nonwifi_busy_time;

	int32_t groud_noise_pri20;
	int32_t groud_noise_pri40;
	int32_t groud_noise_pri80;
};

struct wq_dbg_chan_util_info {
	uint32_t duration;

	uint32_t total_busy_time;

	uint32_t tx_time_total;
	uint32_t rx_time_self;
	uint32_t rx_time_other;

	uint32_t cca_idle_pri_20;
	uint32_t cca_idle_pri_40;
	uint32_t cca_idle_pri_80;
};

struct wq_dbg_dfx_ac_delay {
	uint32_t bk_delay_time;
	uint32_t be_delay_time;
	uint32_t vi_delay_time;
	uint32_t vo_delay_time;
};

struct wq_dbg_dfx_pkt_info {
	int32_t snr;
	int32_t rssi;
	uint8_t mcs_tx[2];
	uint8_t mcs_rx[2];
	uint8_t bw_tx[2];
	uint8_t bw_rx[2];
};

typedef enum {
	MPIF_TX_IDLE,
	MPIF_TX_RECEIVE_VECTOR,
	MPIF_TX_RECEIVE_DATA,
	MPIF_TX_END,
	MPIF_TX_CLOSE
} mpif_tx_st;

typedef enum {
	MPIF_RX_IDLE,
	MPIF_RX_WAIT_VECTOR,
	MPIF_RX_SEND_VECTOR,
	MPIF_RX_SEND_DATA,
	MPIF_RX_TIMING_BARRIER,
	MPIF_RX_SEND_VECTOR2,
	MPIF_RX_END,
	MPIF_RX_ERROR,
	MPIF_RX_ABORT,
	MPIF_RX_CLOSE
} mpif_rx_st;

typedef enum {
	MFSM_IDLE,
	MFSM_TX_WAIT_SIFS,
	MFSM_TX_CHECK_TXACK,
	MFSM_TX_ONGOING,
	MFSM_TX_END_DELAY,
	MFSM_TX_CLOSE,
	MFSM_RX_LISTEN,
	MFSM_RX_DEMOD,
	MFSM_RX_END,
	MFSM_RX_CLOSE
} mfsm_trx_st;

typedef enum { RF_IDLE, RF_ABORTED, RF_PENDING, RF_POWERON } rf_trx_st;

typedef struct _dfx_phy_trx_state {
	mpif_tx_st mpif_tx_state;
	mpif_rx_st mpif_rx_state;
	mfsm_trx_st mfsm_tx_state;
	mfsm_trx_st mfsm_rx_state;
} dfx_phy_trx_state_t;

struct dbg_dfx_edca_param {
	u32 ac_param[4];
	u8 qos_info;
	u8 acm;
	u8 param_set_cnt;
};

typedef struct _dfx_rf_trx_state {
	rf_trx_st rf_tx_fsm;
	rf_trx_st rf_rx_fsm;
} dfx_rf_trx_state_t;

struct wq_dbg_phy_rf_trx_state {
	dfx_phy_trx_state_t phy_trx_state;
	dfx_rf_trx_state_t rf_trx_state;
};
struct wq_dbg_crc_stats {
	uint16_t crc_pass_stat_dsss;
	uint16_t crc_pass_stat_nonht;
	uint16_t crc_pass_stat_nonht_dup;
	uint16_t crc_pass_stat_ht_mm;
	uint16_t crc_pass_stat_ht_gf;
	uint16_t crc_pass_stat_vht;
	uint16_t crc_pass_stat_he_su;
	uint16_t crc_pass_stat_he_mu;
	uint16_t crc_pass_stat_he_ext_su;
	uint16_t crc_pass_stat_he_tb;

	uint16_t crc_fail_stat_dsss;
	uint16_t crc_fail_stat_nonht;
	uint16_t crc_fail_stat_nonht_dup;
	uint16_t crc_fail_stat_ht_mm;
	uint16_t crc_fail_stat_ht_gf;
	uint16_t crc_fail_stat_vht;
	uint16_t crc_fail_stat_he_su;
	uint16_t crc_fail_stat_he_mu;
	uint16_t crc_fail_stat_he_ext_su;
	uint16_t crc_fail_stat_he_tb;

	uint32_t rx_overrun;
};

struct phy_sync_stats {
	uint16_t coarse_sync_cnt;
	uint16_t fine_sync_cnt;
};
struct phy_sig_crc_stats {
	uint16_t lsig_fail_cnt;
	uint16_t lsig_ok_cnt;
	uint16_t herlsig_fail_cnt;
	uint16_t herlsig_ok_cnt;
	uint16_t htsig_fail_cnt;
	uint16_t htsig_ok_cnt;
	uint16_t vhtsiga_fail_cnt;
	uint16_t vhtsiga_ok_cnt;
	uint16_t vhtsigb_fail_cnt;
	uint16_t vhtsigb_ok_cnt;
	uint16_t hesiga_fail_cnt;
	uint16_t hesiga_ok_cnt;
	uint16_t hesigb_fail_cnt;
	uint16_t hesigb_ok_cnt;
	uint16_t data_crc_fail_cnt;
	uint16_t data_crc_ok_cnt;
};
struct wq_dbg_phy_signal_stats {
	struct phy_sync_stats sync_stats;
	struct phy_sig_crc_stats sig_crc_stats;
};

struct wq_dbg_agc_lock_stats {
	uint32_t agc_lock_time_cnt;
	uint16_t agc_lock_timeout_thr;
	uint16_t agc_lock_cnt;
	uint16_t agc_lock_timeout_cnt;
};

typedef struct dfx_freq_offset {
	int32_t freq_offset_pha;
	int32_t freq_offset_hz;
} dfx_freq_offset_t;

typedef struct dfx_rx_dc {
	int16_t dc_i_af_comp;
	int16_t dc_q_af_comp;
} dfx_rx_dc_t;

struct dbg_dfx_agc_code_param {
	u8 dig_gain80m;
	u8 dig_gain40m;
	u8 dig_gain20m;
	u8 rf_rx_gain_db;

	u32 rf_rx_gain_code;
	u32 rf_tx_gain_code;
};

struct dbg_freq_dc_state {
	dfx_freq_offset_t freq_offset;
	dfx_rx_dc_t rx_dc_af;
};

enum wq_dbg_fw_custom_cmd {
	DBG_CMD_PRINT_ERR,
	DBG_CMD_SEND_NULL_PKT,
	DBG_CMD_MAX,
};

extern struct wq_dbg_dfx_pkt_info dfx_pkt_info;
extern WIFI_MGMT_PKT mgmt_hist[HIST_CNT];
extern WIFI_MGMT_PKT pktdump_hist[HIST_CNT];
extern const char mgmt_subtype_to_str[16][10];
extern uint8_t mgmt_idx;
extern uint8_t dump_idx;
inline static char *wq_nl80211_iftype_str(enum nl80211_iftype type)
{
	switch (type) {
	case NL80211_IFTYPE_MESH_POINT:
		return "MESH";
	case NL80211_IFTYPE_ADHOC:
		return "ADHOC";
	case NL80211_IFTYPE_STATION:
		return "STATION";
	case NL80211_IFTYPE_AP:
		return "AP";
	case NL80211_IFTYPE_AP_VLAN:
		return "AP-VLAN";
	case NL80211_IFTYPE_MONITOR:
		return "MONITOR";
	case NL80211_IFTYPE_P2P_CLIENT:
		return "P2P_CLIENT";
	case NL80211_IFTYPE_P2P_GO:
		return "P2P_GO";
	case NL80211_IFTYPE_P2P_DEVICE:
		return "P2P_DEVICE";
	default:
		return "INVALID";
	}
}
inline static char *wq_nl80211_chan_type_str(enum nl80211_channel_type type)
{
	switch (type) {
	case NL80211_CHAN_NO_HT:
		return "NO_HT";
	case NL80211_CHAN_HT20:
		return "CHAN_HT20";
	case NL80211_CHAN_HT40MINUS:
		return "CHAN_HT40-";
	case NL80211_CHAN_HT40PLUS:
		return "CHAN_HT40+";
	default:
		return "INVALID";
	}
}
inline static char *wq_nl80211_chan_width_str(enum nl80211_chan_width width)
{
	switch (width) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
	case NL80211_CHAN_WIDTH_5:
		return "5MHz";
	case NL80211_CHAN_WIDTH_10:
		return "10MHz";
#endif
	case NL80211_CHAN_WIDTH_20_NOHT:
		return "20MHz_NOHT";
	case NL80211_CHAN_WIDTH_20:
		return "20MHz";
	case NL80211_CHAN_WIDTH_40:
		return "40MHz";
	case NL80211_CHAN_WIDTH_80:
		return "80MHz";
	case NL80211_CHAN_WIDTH_80P80:
		return "80+80MHz";
	case NL80211_CHAN_WIDTH_160:
		return "160MHz";
	default:
		return "INVALID";
	}
}

inline static char *wq_cipher_str(uint8_t cipher)
{
	switch (cipher) {
	case MAC_CIPHER_WEP40:
		return "WEP40";
	case MAC_CIPHER_WEP104:
		return "WEP104";
	case MAC_CIPHER_TKIP:
		return "TKIP";
	case MAC_CIPHER_CCMP:
		return "CCMP128";
	case MAC_CIPHER_CCMP_256:
		return "CCMP256";
	case MAC_CIPHER_GCMP_128:
		return "GCMP128";
	case MAC_CIPHER_GCMP_256:
		return "GCMP256";
	case MAC_CIPHER_BIP_CMAC_128:
	case MAC_CIPHER_BIP_GMAC_128:
		return "BIP128";
	case MAC_CIPHER_BIP_CMAC_256:
	case MAC_CIPHER_BIP_GMAC_256:
		return "BIP256";
	default:
		return "NONE";
	}
}

inline static char *wq_wpa_ver_str(enum wq_wpa_version version)
{
	switch (version) {
	case WPA_VER_OPEN:
		return "OPEN";
	case WPA_VER_WEP:
		return "WEP";
	case WPA_VER_WPA1:
		return "WPA";
	case WPA_VER_WPA2:
		return "WPA2";
	case WPA_VER_WPA3:
		return "WPA3";
	case WPA_VER_WAPI:
		return "WAPI";
	default:
		return "NONE";
	}
}
inline static char *wq_auth_type_str(enum nl80211_auth_type auth)
{
	switch (auth) {
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
		return "OPEN";
	case NL80211_AUTHTYPE_SHARED_KEY:
		return "SHARE KEY";
	case NL80211_AUTHTYPE_SAE:
		return "SAE";
	case NL80211_AUTHTYPE_FT:
		return "FT";
	default:
		return "INVALID";
	}
}

inline static char *wq_wlan_ver_str(enum wq_wlan_version version)
{
	switch (version) {
	case WLAN_VER_11N:
		return "Wi-Fi 4";
	case WLAN_VER_11AC:
		return "Wi-Fi 5";
	case WLAN_VER_11AX:
		return "Wi-Fi 6";
	default:
		return "Wi-Fi Legacy";
	}
}

enum WQ_URB_TYPE {
	URB_MSG_OUT,
	URB_MSG_IN,
	URB_PKT_OUT,
	URB_PKT_IN,
};

inline static void
wq_dbg_update_skb_stats_all(void *rwnx_hw, int pkt_out_max, int pkt_out_freecnt,
			    int pkt_in_max, int pkt_in_freecnt, int msg_out_max,
			    int msg_out_freecnt, int msg_in_max,
			    int msg_in_freecnt)
{
	struct rwnx_hw *hw = rwnx_hw;
	struct wq_skb_stats *stats;

	if (rwnx_hw != NULL) {
		stats = &hw->skb_stats;

		stats->pkt_out_max = pkt_out_max;
		stats->pkt_out_freecnt = pkt_out_freecnt;
		stats->pkt_in_max = pkt_in_max;
		stats->pkt_in_freecnt = pkt_in_freecnt;
		stats->msg_out_max = msg_out_max;
		stats->msg_out_freecnt = msg_out_freecnt;
		stats->msg_in_max = msg_in_max;
		stats->msg_in_freecnt = msg_in_freecnt;
	}
}

inline static void wq_dbg_update_skb_stats(void *rwnx_hw, enum WQ_URB_TYPE type,
					   int max, int freecnt)
{
	struct rwnx_hw *hw = rwnx_hw;
	struct wq_skb_stats *stats;

	if (rwnx_hw == NULL) {
		return;
	}
	stats = &hw->skb_stats;
	switch (type) {
	case URB_PKT_OUT:
		stats->pkt_out_max = max;
		stats->pkt_out_freecnt = freecnt;
		break;
	case URB_PKT_IN:
		stats->pkt_in_max = max;
		stats->pkt_in_freecnt = freecnt;
		break;
	case URB_MSG_OUT:
		stats->msg_out_max = max;
		stats->msg_out_freecnt = freecnt;
		break;
	case URB_MSG_IN:
		stats->msg_in_max = max;
		stats->msg_in_freecnt = freecnt;
		break;
	}
}

void wq_dbg_update_security_info(struct wq_security_info *security,
				 uint8_t key_idx, bool pairwise,
				 uint8_t cipher);

int wq_get_vif_info(struct rwnx_vif *vif, struct wq_dbg_vif *dbg_vif);
int wq_get_skb_stats(struct rwnx_hw *rwnx_hw, struct wq_skb_stats *skb_stats);
int wq_get_edca_param(struct rwnx_hw *rwnx_hw, uint8_t mac_id,
		      struct dbg_dfx_edca_param *dfx_edca);
int wq_get_connect_time(struct rwnx_sta *sta_table,
			struct wq_dbg_connect_time *conn_time_stat);
int wq_get_sta_trx_stats(struct rwnx_hw *rwnx_hw, uint8_t sta_idx,
			 struct wq_dbg_fw_sta_trx_stats *stats);
int wq_get_agc_code_get(struct rwnx_hw *rwnx_hw, uint8_t mac_id,
			struct dbg_dfx_agc_code_param *agc_code);
int wq_get_chan_util_info(struct rwnx_vif *vif, uint32_t time_ms,
			  struct wq_dbg_chan_util_info *busy_info);
int wq_get_chan_noise_info(struct rwnx_vif *vif, uint32_t time_ms,
			   struct wq_dbg_chan_noise_info *busy_info);
int wq_get_ampdu_stats(struct rwnx_hw *rwnx_hw, struct rwnx_stats *stats);
int wq_get_ac_delay_time(struct rwnx_hw *rwnx_hw, uint8_t mac_id,
			 struct wq_dbg_dfx_ac_delay *ac_delay);
int wq_get_trx_pkt_info(struct wq_dbg_dfx_pkt_info *pkt_info);
int wq_set_scan_func_enable(int enable);
int wq_get_phy_rf_trx_state(struct rwnx_hw *rwnx_hw, uint8_t mac_id,
			    struct wq_dbg_phy_rf_trx_state *pht_rf_state);
int wq_crc_stats_enable(struct rwnx_vif *vif, u8 enable);
int wq_get_crc_stats(struct rwnx_vif *vif, struct wq_dbg_crc_stats *stats);
int wq_get_phy_signal_stats(struct rwnx_vif *vif,
			    struct wq_dbg_phy_signal_stats *stats);
int wq_phy_signal_stats_enable(struct rwnx_vif *vif, u8 enable);
int wq_get_agc_lock_stats(struct rwnx_vif *vif,
			  struct wq_dbg_agc_lock_stats *stats);
int wq_agc_lock_stats_enable(struct rwnx_vif *vif, u8 enable);
int wq_get_freq_dc_state(struct rwnx_hw *rwnx_hw, uint8_t mac_id,
			 struct dbg_freq_dc_state *freq_dc_state);
int wq_get_vif_ext_trx_stats(struct rwnx_vif *vif,
			     struct wq_dbg_vif_ext_trx_stats *stats);
void wq_dbg_dump_recovery_stats(struct wq_dbg_recovery_stats *stat);
int wq_set_edca_params(struct rwnx_vif *vif, u8 ac, u8 aifs, u16 cwmin,
		       u16 cwmax, u16 txop);
int wq_send_fw_cmd(struct rwnx_vif *vif, u8 cmd, u8 info);

void print_fw_hw_feature(struct wq_conf *mod_params);

#endif //_WQ_WIFI_DBG_H
