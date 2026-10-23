#ifndef _HAL_DESC_H_
#define _HAL_DESC_H_

typedef enum { WIFI_DBG_PKT_TX = 0, WIFI_DBG_PKT_RX = 1 } WIFI_DBG_PKT_DIR;

// HE Hardware
struct __attribute__((packed)) rx_leg_vect {
	uint8_t dyn_bw_in_non_ht : 1;
	uint8_t chn_bw_in_non_ht : 2;
	uint8_t rsvd_nht : 4;
	uint8_t lsig_valid : 1;
};

struct __attribute__((packed)) rx_ht_vect {
	uint16_t sounding : 1;
	uint16_t smoothing : 1;
	uint16_t short_gi : 1;
	uint16_t aggregation : 1;
	uint16_t stbc : 1;
	uint16_t num_extn_ss : 2;
	uint16_t lsig_valid : 1;
	uint16_t mcs : 7;
	uint16_t fec : 1;

	uint16_t length : 16;
};

struct __attribute__((packed)) rx_vht_vect {
	uint8_t sounding : 1;
	uint8_t beamformed : 1;
	uint8_t short_gi : 1;
	uint8_t rsvd_vht1 : 1;
	uint8_t stbc : 1;
	uint8_t doze_not_allowed : 1;
	uint8_t first_user : 1;
	uint8_t rsvd_vht2 : 1;

	uint16_t partial_aid : 9;
	uint16_t group_id : 6;
	uint16_t rsvd_vht3 : 1;

	uint32_t mcs : 4;
	uint32_t nss : 3;
	uint32_t fec : 1;
	uint32_t length : 20;
	uint32_t rsvd_vht4 : 4;
};

struct __attribute__((packed)) rx_he_vect {
	uint8_t sounding : 1;
	uint8_t beamformed : 1;
	uint8_t gi_type : 2;
	uint8_t stbc : 1;
	uint8_t rsvd_he1 : 3;

	uint8_t uplink_flag : 1;
	uint8_t beam_change : 1;
	uint8_t dcm : 1;
	uint8_t he_ltf_type : 2;
	uint8_t doppler : 1;
	uint8_t rsvd_he2 : 2;

	uint8_t bss_color : 6;
	uint8_t rsvd_he3 : 2;

	uint8_t txop_duration : 7;
	uint8_t rsvd_he4 : 1;

	uint8_t pe_duration : 4;
	uint8_t spatial_reuse : 4;

	uint8_t sig_b_comp_mode : 1;
	uint8_t dcm_sig_b : 1;
	uint8_t mcs_sig_b : 3;
	uint8_t ru_size : 3;

	uint32_t mcs : 4;
	uint32_t nss : 3;
	uint32_t fec : 1;
	uint32_t length : 20;
	uint32_t rsvd_he6 : 4;
};

struct __attribute__((packed)) rx_vector_1 {
	uint8_t format_mod : 4;
	uint8_t ch_bw : 3;
	uint8_t pre_type : 1;

	uint8_t antenna_set : 8;

	int32_t rssi_leg : 8; //RSSI(dBm)
	uint32_t leg_length : 12;
	uint32_t leg_rate : 4;
	int32_t snr : 8; //changed to SNR(dB) by phy RTL

	union {
		struct rx_leg_vect leg;
		struct rx_ht_vect ht;
		struct rx_vht_vect vht;
		struct rx_he_vect he;
	};
};

#define PKT_COPY_LEN 64
#define PKT_RXVEC_LEN 4

typedef struct __attribute__((packed)) wifi_dbg_pkt {
	uint32_t start;
	uint32_t mac_ts;
	uint16_t len;

	uint16_t rate : 8;
	uint16_t is_ampdu : 1;
	uint16_t dir : 1;
	uint16_t frame_cnt : 6;

	uint32_t cs : 4;
	uint32_t cindex : 6;
	uint32_t rssi : 8;
	uint32_t mac_id : 2;
	uint32_t reserved2 : 4;
	uint32_t sn : 8;

	uint32_t proto_ts;
	uint32_t driver_ts;
	uint32_t hal_ts;
	uint32_t rx_vec[PKT_RXVEC_LEN];
	uint32_t tx_status;
	uint8_t pkt_data[PKT_COPY_LEN];
} WIFI_DBG_PKT;

/* c.f RW-WLAN-nX-MAC-HW-UM */
union rwnx_rate_ctrl_info {
	struct {
		uint32_t mcsIndexTx : 7;
		uint32_t bwTx : 2;
		uint32_t giAndPreTypeTx : 2;
		uint32_t formatModTx : 3;
		uint32_t dcmTx : 1;
	};
	uint32_t value;
};

/* c.f RW-WLAN-nX-MAC-HW-UM */
union rwnx_hw_txstatus_um {
	struct {
		uint32_t num_rts_retries : 8;
		uint32_t num_mpdu_retries : 8;
		uint32_t retry_limit_reached : 1;
		uint32_t lifetime_expired : 1;
		uint32_t baFrameReceived : 1;
		uint32_t reserved2 : 4;
		uint32_t frm_successful_tx : 1;
		uint32_t transmission_bw : 2;
		uint32_t which_descriptor_sw : 4;
		uint32_t descriptor_done_swtx : 1;
		uint32_t descriptor_done_hwtx : 1;
	};
	uint32_t value;
};

//DEFINE RX_VECTOR OFFSET
#define RX_VEC_COMMON_FORMAT_MASK 0x0F
#define RX_VEC_COMMON_FORMAT_OFFSET 0
#define RX_VEC_COMMON_CH_BANDWIDTH_MASK 0x70
#define RX_VEC_COMMON_CH_BANDWIDTH_OFFSET 4
#define RX_VEC_COMMON_PREAMBLE_TYPE_MASK 0x80
#define RX_VEC_COMMON_PREAMBLE_TYPE_OFFSET 7
#define RX_VEC_COMMON_ANTENNA_SET_MASK 0xFF00
#define RX_VEC_COMMON_ANTENNA_SET_OFFSET 8
#define RX_VEC_COMMON_RSSI_LEGACY_MASK 0xFF0000
#define RX_VEC_COMMON_RSSI_LEGACY_OFFSET 16
#define RX_VEC_COMMON_LEGACY_LENGTH_LOW_MASK 0xFF000000
#define RX_VEC_COMMON_LEGACY_LENGTH_LOW_OFFSET 24
#define RX_VEC_COMMON_LEGACY_LENGTH_HIGH_MASK 0x0F
#define RX_VEC_COMMON_LEGACY_LENGTH_HIGH_OFFSET 0
#define RX_VEC_COMMON_LEGACY_RATE_MASK 0xF0
#define RX_VEC_COMMON_LEGACY_RATE_OFFSET 4
#define RX_VEC_COMMON_RSSI_MASK 0xFF00
#define RX_VEC_COMMON_RSSI_OFFSET 8

#define RX_VEC_NON_HT_DYN_BANDWIDTH_MASK 0x10000
#define RX_VEC_NON_HT_DYN_BANDWIDTH_OFFSET 16
#define RX_VEC_NON_HT_CH_BANDWIDTH_MASK 0x30000
#define RX_VEC_NON_HT_CH_BANDWIDTH_OFFSET 17
#define RX_VEC_NON_HT_L_SIG_VALID_MASK 0x800000
#define RX_VEC_NON_HT_l_SIG_VALID_OFFSET 23

#define RX_VEC_HT_MF_SOUNDING_MASK 0x10000
#define RX_VEC_HT_MF_SOUNDING_OFFSET 16
#define RX_VEC_HT_MF_SMOOTHING_MASK 0x20000
#define RX_VEC_HT_MF_SMOOTHING_OFFSET 17
#define RX_VEC_HT_MF_GI_TYPE_MASK 0x40000
#define RX_VEC_HT_MF_GI_TYPE_OFFSET 18
#define RX_VEC_HT_MF_AGGREGATION_MASK 0x80000
#define RX_VEC_HT_MF_AGGREGATION_OFFSET 19
#define RX_VEC_HT_MF_STBC_MASK 0x100000
#define RX_VEC_HT_MF_STBC_OFFSET 20
#define RX_VEC_HT_MF_NUM_EXT_SS_MASK 0x600000
#define RX_VEC_HT_MF_NUM_EXT_SS_OFFSET 21
#define RX_VEC_HT_MF_L_SIG_VALID_MASK 0x800000
#define RX_VEC_HT_MF_l_SIG_VALID_OFFSET 23
#define RX_VEC_HT_MF_MCS_MASK 0x7F000000
#define RX_VEC_HT_MF_MCS_OFFSET 24
#define RX_VEC_HT_MF_FEC_MASK 0x80000000
#define RX_VEC_HT_MF_FEC_OFFSET 31
#define RX_VEC_HT_MF_LENGTH_MASK 0xFFFF
#define RX_VEC_HT_MF_LENGTH_OFFSET 0

//DEFINE TXRATECONTROL OFFSET
#define TX_MCSINDEX_MASK 0X7F
#define TX_MCSINDEX_OFFSET 0
#define TX_BWTX_MASK 0x180
#define TX_BWTX_OFFSET 7
#define TX_GIANDPRETYPE_MASK 0x600
#define TX_GIANDPRETYPE_OFFSET 9
#define TX_FORMATMOD_MASK 0x3800
#define TX_FORMATMOD_OFFSET 11
#define TX_DCM_MASK 0x8000
#define TX_DCM_OFFSET 14
#define TX_NAVPROFRMEX_MASK 0x1C000
#define TX_NAVPROFRMEX_OFFSET 14
#define TX_MCSINDEXPRO_MASK 0xFE0000
#define TX_MCSINDEXPRO_OFFSET 17
#define TX_BWPRO_MASK 0x3000000
#define TX_BWPRO_OFFSET 24
#define TX_FORMATMODPRO_MASK 0x1C000000
#define TX_FORMATMODPRO_OFFSET 26
#define TX_NRETRY_MASK 0xE0000000
#define TX_NRETRY_OFFSET 29

//DEFINE TXSTATUS OFFSET
#define TX_NUMRTSRETRIES_MASK 0XFF
#define TX_NUMRTSRETRIES_OFFSET 0
#define TX_NUMMPDURETRIES_MASK 0XFF00
#define TX_NUMMPDURETRIES_OFFSET 8
#define TX_RETRYLIMITREACHED_MASK 0X10000
#define TX_RETRYLIMITREACHED_OFFSET 16
#define TX_LIFETIMEEXPIRED_MASK 0X20000
#define TX_LIFETIMEEXPIRED_OFFSET 17
#define TX_BAFRAMERECEIVED_MASK 0X40000
#define TX_BAFRAMERECEIVED_OFFSET 18
#define TX_FRMSUCCESSFULTX_MASK 0X800000
#define TX_FRMSUCCESSFULTX_OFFSET 23
#define TX_TRANSMISSIONBW_MASK 0X3000000
#define TX_TRANSMISSIONBW_OFFSET 24
#define TX_WHICHDESCRIPTORSW_MASK 0x3C000000
#define TX_WHICHDESCRIPTORSW_OFFSET 26
#define TX_DESCRIPTORDONESWTX_MASK 0x40000000
#define TX_DESCRIPTORDONESWTX_OFFSET 30
#define TX_DESCRIPTORDONEHWTX_MASK 0x80000000
#define TX_DESCRIPTORDONEHWTX_OFFSET 31

/* Values for formatModTx */
#define FORMATMOD_NON_HT 0
#define FORMATMOD_NON_HT_DUP_OFDM 1
#define FORMATMOD_HT_MF 2
#define FORMATMOD_HT_GF 3
#define FORMATMOD_VHT 4
#define FORMATMOD_HE_SU 5
#define FORMATMOD_HE_MU 6
#define FORMATMOD_HE_ER 7
#define FORMATMOD_HE_TB 8

// bandwidth
// 20MHz bandwidth
#define BW_20MHZ 0
// 40MHz bandwidth
#define BW_40MHZ 1
// 80MHz bandwidth
#define BW_80MHZ 2
// 160MHz or 80+80 MHzbandwidth
#define BW_160MHZ 3

// RSSI_LEGACY
// Received Signal Strength in dBm measured during Legacy Preamble(from -128 to 0)
// -128dBm
#define RSSI_LEGACY_128 0X80
// -1dBm
#define RSSI_LEGACY_1 0XFF
// 0dBm
#define RSSI_LEGACY_0 0X00

void wq_packet_dump_debug(WIFI_DBG_PKT *dpkt);

#endif
