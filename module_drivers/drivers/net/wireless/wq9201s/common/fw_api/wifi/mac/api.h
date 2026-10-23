#ifndef WQ_FW_WIFI_MAC_API_H_
#define WQ_FW_WIFI_MAC_API_H_

#include "fw_api/wifi/api.h"
#include "wq_compat.h"

/* WiFi MAC definitions */

/// SSID maximum length.
#define MAC_SSID_LEN			32

#define MAC_ADDR_LEN			6

/// max number of channels in the 2.4 GHZ band
#define MAC_DOMAINCHANNEL_24G_MAX	14

/// max number of channels in the 5 GHZ band
#define MAC_DOMAINCHANNEL_5G_MAX	28

/// MAC rateset maximum length
#define MAC_RATESET_LEN			12

/// MAC Security Key maximum length
// TKIP keys 256 bits (max length) with MIC keys
#define MAC_SEC_KEY_LEN			32

/// MCS bitfield maximum size (in bytes)
#define MAX_MCS_LEN			16 // 16 * 8 = 128

/// Length (in bytes) of the MAC HE capability field
#define MAC_HE_MAC_CAPA_LEN		6
/// Length (in bytes) of the PHY HE capability field
#define MAC_HE_PHY_CAPA_LEN		11
/// Maximum length (in bytes) of the PPE threshold data
#define MAC_HE_PPE_THRES_MAX_LEN	25

/// Status/error codes used in the MAC software.
enum
{
	CO_OK,
	CO_FAIL,
	CO_EMPTY,
	CO_FULL,
	CO_BAD_PARAM,
	CO_NOT_FOUND,
	CO_NO_MORE_ELT_AVAILABLE,
	CO_NO_ELT_IN_USE,
	CO_BUSY,
	CO_OP_IN_PROGRESS,
};

/// Channel Band
enum mac_chan_band
{
	/// 2.4GHz Band
	PHY_BAND_2G4,
	/// 5GHz band
	PHY_BAND_5G,
	/// Number of bands
	PHY_BAND_MAX,
};

/// Operating Channel Bandwidth
enum mac_chan_bandwidth
{
	/// 20MHz BW
	PHY_CHNL_BW_20 = 0,
	/// 40MHz BW
	PHY_CHNL_BW_40,
	/// 80MHz BW
	PHY_CHNL_BW_80,
	/// 160MHz BW
	PHY_CHNL_BW_160,
	/// 80+80MHz BW
	PHY_CHNL_BW_80P80,
	/// Reserved BW
	PHY_CHNL_BW_OTHER,
};

/// Channel Flag
enum mac_chan_flags
{
	/// Cannot initiate radiation on this channel
	CHAN_NO_IR = BIT(0),
	/// Channel is not allowed
	CHAN_DISABLED = BIT(1),
	/// Radar detection required on this channel
	CHAN_RADAR = BIT(2),
	///phy need dcalibration
	CHAN_PHY_CALI = BIT(3),
	// 10MHz narrow bandwidth
	CHAN_NBW_10MHz = BIT(4),
	// 5MHz narrow bandwidth
	CHAN_NBW_5MHz = BIT(5),
	// 2.5MHz narrow bandwidth
	CHAN_NBW_2_5MHz = BIT(6),
	// down clock narrow bandwidth
	CHAN_NBW_DOWN_CLK = BIT(7),
};

/// Cipher suites
enum mac_cipher_suite
{
	/// 00-0F-AC 1
	MAC_CIPHER_WEP40 = 0,
	/// 00-0F-AC 2
	MAC_CIPHER_TKIP,
	/// 00-0F-AC 4 (aka CCMP-128)
	MAC_CIPHER_CCMP,
	/// 00-0F-AC 5
	MAC_CIPHER_WEP104,
	/// 00-14-72 1
	MAC_CIPHER_WPI_SMS4,
	/// 00-0F-AC 6 (aka AES_CMAC)
	MAC_CIPHER_BIP_CMAC_128,
	/// 00-0F-AC 08
	MAC_CIPHER_GCMP_128,
	/// 00-0F-AC 09
	MAC_CIPHER_GCMP_256,
	/// 00-0F-AC 10
	MAC_CIPHER_CCMP_256,

	// following cipher are not supported by MACHW
	/// 00-0F-AC 11
	MAC_CIPHER_BIP_GMAC_128,
	/// 00-0F-AC 12
	MAC_CIPHER_BIP_GMAC_256 = 10,
	/// 00-0F-AC 13
	MAC_CIPHER_BIP_CMAC_256,

	MAC_CIPHER_INVALID = 0xFF
};

enum mac_proto
{
	MAC_PROTO_WPA = BIT(0),
	MAC_PROTO_RSN = BIT(1),
};

/// Legacy rate 802.11 definitions
enum mac_legacy_rates
{
	/// DSSS/CCK 1Mbps
	MAC_RATE_1MBPS   =   2,
	/// DSSS/CCK 2Mbps
	MAC_RATE_2MBPS   =   4,
	/// DSSS/CCK 5.5Mbps
	MAC_RATE_5_5MBPS =  11,
	/// OFDM 6Mbps
	MAC_RATE_6MBPS   =  12,
	/// OFDM 9Mbps
	MAC_RATE_9MBPS   =  18,
	/// DSSS/CCK 11Mbps
	MAC_RATE_11MBPS  =  22,
	/// OFDM 12Mbps
	MAC_RATE_12MBPS  =  24,
	/// OFDM 18Mbps
	MAC_RATE_18MBPS  =  36,
	/// OFDM 24Mbps
	MAC_RATE_24MBPS  =  48,
	/// OFDM 36Mbps
	MAC_RATE_36MBPS  =  72,
	/// OFDM 48Mbps
	MAC_RATE_48MBPS  =  96,
	/// OFDM 54Mbps
	MAC_RATE_54MBPS  = 108
};

/// Traffic ID enumeration
enum mac_tid
{
	/// TID_0. Mapped to @ref AC_BE as per 802.11 standard.
	TID_0,
	/// TID_1. Mapped to @ref AC_BK as per 802.11 standard.
	TID_1,
	/// TID_2. Mapped to @ref AC_BK as per 802.11 standard.
	TID_2,
	/// TID_3. Mapped to @ref AC_BE as per 802.11 standard.
	TID_3,
	/// TID_4. Mapped to @ref AC_VI as per 802.11 standard.
	TID_4,
	/// TID_5. Mapped to @ref AC_VI as per 802.11 standard.
	TID_5,
	/// TID_6. Mapped to @ref AC_VO as per 802.11 standard.
	TID_6,
	/// TID_7. Mapped to @ref AC_VO as per 802.11 standard.
	TID_7,
	/// Non standard Management TID used internally
	TID_MGT,
	/// Number of TID supported
	TID_MAX
};

/// Access Category enumeration
enum mac_ac
{
	/// Background
	AC_BK = 0,
	/// Best-effort
	AC_BE,
	/// Video
	AC_VI,
	/// Voice
	AC_VO,
	/// Number of access categories
	AC_MAX
};

enum mac_vif_type
{
	/// ESS STA interface
	VIF_STA = 0,
	/// IBSS STA interface
	VIF_IBSS,
	/// AP interface
	VIF_AP,
	// Mesh Pointinterface
	VIF_MESH_POINT,
	/// Monitor interface
	VIF_MONITOR,
	/// Unknowntype
	VIF_UNKNOWN
};

/// Station flags
enum mac_sta_flags
{
	/// Bit indicating that a STA has QoS (WMM) capability
	STA_QOS_CAPA = BIT(0),
	/// Bit indicating that a STA has HT capability
	STA_HT_CAPA = BIT(1),
	/// Bit indicating that a STA has VHT capability
	STA_VHT_CAPA = BIT(2),
	/// Bit indicating that a STA has MFP capability
	STA_MFP_CAPA = BIT(3),
	/// Bit indicating that the STA included the Operation Notification IE
	STA_OPMOD_NOTIF = BIT(4),
	/// Bit indicating that a STA has HE capability
	STA_HE_CAPA = BIT(5),
};

/// Connection flags
enum mac_connection_flags
{
	/// Flag indicating whether the control port is controlled by host or not
	CONTROL_PORT_HOST = BIT(0),
	/// Flag indicating whether the control port frame shall be sent unencrypted
	CONTROL_PORT_NO_ENC = BIT(1),
	/// Flag indicating whether HT and VHT shall be disabled or not
	DISABLE_HT = BIT(2),
	/// Flag indicating whether WPA or WPA2 authentication is in use
	WPA_WPA2_IN_USE = BIT(3),
	/// Flag indicating whether MFP is in use
	MFP_IN_USE = BIT(4),
	/// Flag indicating whether Reassociation should be used instead of Association
	REASSOCIATION = BIT(5),
	/// Flag indicating Connection request if part of FT over DS
	FT_OVER_DS = BIT(6),
	WEP_CONN_FLG = BIT(7),
};

/// SSID.
struct mac_ssid
{
	/// Actual length of the SSID.
	u8 length;
	/// Array containing the SSID name.
	u8 array[MAC_SSID_LEN];
};

/// Primary Channel definition
struct mac_chan_def
{
	/// Frequency of the channel (in MHz)
	u16 freq;
	/// RF band (@ref mac_chan_band)
	u8 band;
	/// Additional information (@ref mac_chan_flags)
	u8 flags;
	/// Max transmit power allowed on this channel (dBm)
	s8 tx_power;
};

/// Operating Channel
struct mac_chan_op
{
	/// Band (@ref mac_chan_band)
	u8 band;
	/// Channel type (@ref mac_chan_bandwidth)
	u8 type;
	/// Frequency for Primary 20MHz channel (in MHz)
	u16 prim20_freq;
	/// Frequency center of the contiguous channel or center of Primary 80+80 (in MHz)
	u16 center1_freq;
	/// Frequency center of the non-contiguous secondary 80+80 (in MHz)
	u16 center2_freq;
	/// Max transmit power allowed on this channel (dBm)
	s8 tx_power;
	/// Additional information (@ref mac_chan_flags)
	u8 flags;
};

/// Structure containing the legacy rateset of a station
struct mac_rateset
{
	/// Number of legacy rates supported
	u8 length;
	/// Array of legacy rates
	u8 array[MAC_RATESET_LEN];
};

/// Structure defining a security key
struct mac_sec_key
{
	/// Key material length
	u8 length;
	/// Key material
	u32 array[MAC_SEC_KEY_LEN/4];
};

/// MAC HT capability information element
struct mac_htcapability
{
	/// HT capability information
	u16 ht_capa_info;
	/// A-MPDU parameters
	u8 a_mpdu_param;
	/// Supported MCS
	u8 mcs_rate[MAX_MCS_LEN];
	/// HT extended capability information
	u16 ht_extended_capa;
	/// Beamforming capability information
	u32 tx_beamforming_capa;
	/// Antenna selection capability information
	u8 asel_capa;
};

/// MAC VHT capability information element
struct mac_vhtcapability
{
	/// VHT capability information
	u32 vht_capa_info;
	/// RX MCS map
	u16 rx_mcs_map;
	/// RX highest data rate
	u16 rx_highest;
	/// TX MCS map
	u16 tx_mcs_map;
	/// TX highest data rate
	u16 tx_highest;
};

/// Structure listing the per-NSS, per-BW supported MCS combinations
struct mac_he_mcs_nss_supp
{
	/// per-NSS supported MCS in RX, for BW <= 80MHz
	u16 rx_mcs_80;
	/// per-NSS supported MCS in TX, for BW <= 80MHz
	u16 tx_mcs_80;
	/// per-NSS supported MCS in RX, for BW = 160MHz
	u16 rx_mcs_160;
	/// per-NSS supported MCS in TX, for BW = 160MHz
	u16 tx_mcs_160;
	/// per-NSS supported MCS in RX, for BW = 80+80MHz
	u16 rx_mcs_80p80;
	/// per-NSS supported MCS in TX, for BW = 80+80MHz
	u16 tx_mcs_80p80;
};

/// MAC HE capability information element
struct mac_hecapability
{
	/// MAC HE capabilities
	u8 mac_cap_info[MAC_HE_MAC_CAPA_LEN];
	/// PHY HE capabilities
	u8 phy_cap_info[MAC_HE_PHY_CAPA_LEN];
	/// Supported MCS combinations
	struct mac_he_mcs_nss_supp mcs_supp;
	/// PPE Thresholds data
	u8 ppe_thres[MAC_HE_PPE_THRES_MAX_LEN];
};

//note: common struct cannot be modified and added, can only be created a new struct like txl_txstats_external_v1
struct txl_txstats
{
	u32 txl_cntrl_cnt;
	u32 payload_alloc_cnt;
	u32 payload_transfer_cnt;
	u32 buffer_alloc_cnt;
	u32 buffer_alloc_fail_cnt;
	u32 buffer_free_cnt;
	u32 dma_push_cnt;
	u32 frame_queue_cnt;
	u32 frame_direct_xmit_cnt;
	u32 frame_sn_in_baw_cnt;
	u32 frame_sn_out_of_baw_cnt;
	u32 sn_out_of_baw_when_xmit_cnt;
	u32 mpdu_in_aggr_cnt;
	u32 mpdu_in_single_cnt;
	u32 xmit_aggr_cnt;
	u32 pkt_free_cnt;
	u32 mpdu_retry_cnt;
	u32 txu_discard_cnt;
	u32 queue_pause_cnt;
	u32 tid_queue_cnt;
	u32 tid_sched_cnt;
};

struct txl_txstats_external_v1
{
    u32 txl_discard_cnt;
    u32 reserved_0;
    u32 reserved_1;
    u32 reserved_2;
};

struct peer_tx_stats
{
	u8 sta_idx;
	u8 sw_retry_step;
	u16 retry_step_idx[4];
	u16 rate_config[4];
	u32 single_xmit_success;
	u32 single_retry_cnt;
	u32 single_fail_cnt;
	u32 mpdu_xmit_success;
	u32 mpdu_retry_cnt;
	u32 mpdu_fail_cnt;
	u32 mac_total_tx_cnt;
};

struct target_peer_info
{
	u8 peer_num;
	u8 index;
	u16 flags;
	struct peer_tx_stats stats[0];
};

#endif /* WQ_FW_WIFI_MAC_API_H_ */
