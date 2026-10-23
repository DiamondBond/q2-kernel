/* data path RX APIs */
#ifndef WQ_FW_WIFI_MAC_DP_RX_API_H_
#define WQ_FW_WIFI_MAC_DP_RX_API_H_

#include "fw_api/wifi/mac/api.h"

/* Decryption status subfields. (old NX hardware) */
#define RWNX_RX_HD_NX_DECR_UNENC	0 // Frame unencrypted
#define RWNX_RX_HD_NX_DECR_ICVFAIL	1 // WEP/TKIP ICV failure
#define RWNX_RX_HD_NX_DECR_CCMPFAIL	2 // CCMP failure
#define RWNX_RX_HD_NX_DECR_AMSDUDISCARD	3 // A-MSDU discarded at HW
#define RWNX_RX_HD_NX_DECR_NULLKEY	4 // NULL key found
#define RWNX_RX_HD_NX_DECR_WEPSUCCESS	5 // Security type WEP
#define RWNX_RX_HD_NX_DECR_TKIPSUCCESS	6 // Security type TKIP
#define RWNX_RX_HD_NX_DECR_CCMPSUCCESS	7 // Security type CCMP (or WPI)

/* Decryption status subfields. (HE hardware) */
#define RWNX_RX_HD_DECR_UNENC		0 // Frame unencrypted
#define RWNX_RX_HD_DECR_WEP		1 // Security type WEP
#define RWNX_RX_HD_DECR_TKIP		2 // Security type TKIP
#define RWNX_RX_HD_DECR_CCMP128		3 // Security type CCMP (128 bits)
#define RWNX_RX_HD_DECR_CCMP256		4 // Security type CCMP (256 bits)
#define RWNX_RX_HD_DECR_GCMP128		5 // Security type GCMP (128 bits)
#define RWNX_RX_HD_DECR_GCMP256		6 // Security type GCMP (256 bits)
#define RWNX_RX_HD_DECR_WAPI		7 // Security type WAPI
#define RWNX_RX_HD_DECR_NULLKEY		15 // NULL key found

// bandwidth
// 20MHz bandwidth
#define BW_20MHZ		0
// 40MHz bandwidth
#define BW_40MHZ		1
// 80MHz bandwidth
#define BW_80MHZ		2
// 160MHz or 80+80 MHzbandwidth
#define BW_160MHZ		3

// RSSI_LEGACY
// Received Signal Strength in dBm measured during Legacy Preamble(from -128 to 0)
// -128dBm
#define RSSI_LEGACY_128		0x80
// -1dBm
#define RSSI_LEGACY_1		0xFF
// 0dBm
#define RSSI_LEGACY_0		0x00

//DEFINE RX_VECTOR OFFSET
#define RX_VEC_COMMON_FORMAT_MASK		0x0F
#define RX_VEC_COMMON_FORMAT_OFFSET		0
#define RX_VEC_COMMON_CH_BANDWIDTH_MASK		0x70
#define RX_VEC_COMMON_CH_BANDWIDTH_OFFSET	4
#define RX_VEC_COMMON_PREAMBLE_TYPE_MASK	0x80
#define RX_VEC_COMMON_PREAMBLE_TYPE_OFFSET	7
#define RX_VEC_COMMON_ANTENNA_SET_MASK		0xFF00
#define RX_VEC_COMMON_ANTENNA_SET_OFFSET	8
#define RX_VEC_COMMON_RSSI_LEGACY_MASK		0xFF0000
#define RX_VEC_COMMON_RSSI_LEGACY_OFFSET	16
#define RX_VEC_COMMON_LEGACY_LENGTH_LOW_MASK	0xFF000000
#define RX_VEC_COMMON_LEGACY_LENGTH_LOW_OFFSET	24
#define RX_VEC_COMMON_LEGACY_LENGTH_HIGH_MASK	0x0F
#define RX_VEC_COMMON_LEGACY_LENGTH_HIGH_OFFSET	0
#define RX_VEC_COMMON_LEGACY_RATE_MASK		0xF0
#define RX_VEC_COMMON_LEGACY_RATE_OFFSET	4
#define RX_VEC_COMMON_RSSI_MASK			0xFF00
#define RX_VEC_COMMON_RSSI_OFFSET		8

#define RX_VEC_NON_HT_DYN_BANDWIDTH_MASK	0x10000
#define RX_VEC_NON_HT_DYN_BANDWIDTH_OFFSET	16
#define RX_VEC_NON_HT_CH_BANDWIDTH_MASK		0x30000
#define RX_VEC_NON_HT_CH_BANDWIDTH_OFFSET	17
#define RX_VEC_NON_HT_L_SIG_VALID_MASK		0x800000
#define RX_VEC_NON_HT_l_SIG_VALID_OFFSET	23

#define RX_VEC_HT_MF_SOUNDING_MASK		0x10000
#define RX_VEC_HT_MF_SOUNDING_OFFSET		16
#define RX_VEC_HT_MF_SMOOTHING_MASK		0x20000
#define RX_VEC_HT_MF_SMOOTHING_OFFSET		17
#define RX_VEC_HT_MF_GI_TYPE_MASK		0x40000
#define RX_VEC_HT_MF_GI_TYPE_OFFSET		18
#define RX_VEC_HT_MF_AGGREGATION_MASK		0x80000
#define RX_VEC_HT_MF_AGGREGATION_OFFSET		19
#define RX_VEC_HT_MF_STBC_MASK			0x100000
#define RX_VEC_HT_MF_STBC_OFFSET		20
#define RX_VEC_HT_MF_NUM_EXT_SS_MASK		0x600000
#define RX_VEC_HT_MF_NUM_EXT_SS_OFFSET		21
#define RX_VEC_HT_MF_L_SIG_VALID_MASK		0x800000
#define RX_VEC_HT_MF_l_SIG_VALID_OFFSET		23
#define RX_VEC_HT_MF_MCS_MASK			0x7F000000
#define RX_VEC_HT_MF_MCS_OFFSET			24
#define RX_VEC_HT_MF_FEC_MASK			0x80000000
#define RX_VEC_HT_MF_FEC_OFFSET			31
#define RX_VEC_HT_MF_LENGTH_MASK		0xFFFF
#define RX_VEC_HT_MF_LENGTH_OFFSET		0

//DEFINE TXRATECONTROL OFFSET
#define TX_MCSINDEX_MASK			0x7F
#define TX_MCSINDEX_OFFSET			0
#define TX_BWTX_MASK				0x180
#define TX_BWTX_OFFSET				7
#define TX_GIANDPRETYPE_MASK			0x600
#define TX_GIANDPRETYPE_OFFSET			9
#define TX_FORMATMOD_MASK			0x3800
#define TX_FORMATMOD_OFFSET			11
#define TX_DCM_MASK				0x8000
#define TX_DCM_OFFSET				14
#define TX_NAVPROFRMEX_MASK			0x1C000
#define TX_NAVPROFRMEX_OFFSET			14
#define TX_MCSINDEXPRO_MASK			0xFE0000
#define TX_MCSINDEXPRO_OFFSET			17
#define TX_BWPRO_MASK				0x3000000
#define TX_BWPRO_OFFSET				24
#define TX_FORMATMODPRO_MASK			0x1C000000
#define TX_FORMATMODPRO_OFFSET			26
#define TX_NRETRY_MASK				0xE0000000
#define TX_NRETRY_OFFSET			29

//DEFINE TXSTATUS OFFSET
#define TX_NUMRTSRETRIES_MASK			0xFF
#define TX_NUMRTSRETRIES_OFFSET			0
#define TX_NUMMPDURETRIES_MASK			0xFF00
#define TX_NUMMPDURETRIES_OFFSET		8
#define TX_RETRYLIMITREACHED_MASK		0x10000
#define TX_RETRYLIMITREACHED_OFFSET		16
#define TX_LIFETIMEEXPIRED_MASK			0x20000
#define TX_LIFETIMEEXPIRED_OFFSET		17
#define TX_BAFRAMERECEIVED_MASK			0x40000
#define TX_BAFRAMERECEIVED_OFFSET		18
#define TX_FRMSUCCESSFULTX_MASK			0x800000
#define TX_FRMSUCCESSFULTX_OFFSET		23
#define TX_TRANSMISSIONBW_MASK			0x3000000
#define TX_TRANSMISSIONBW_OFFSET		24
#define TX_WHICHDESCRIPTORSW_MASK		0x3C000000
#define TX_WHICHDESCRIPTORSW_OFFSET		26
#define TX_DESCRIPTORDONESWTX_MASK		0x40000000
#define TX_DESCRIPTORDONESWTX_OFFSET		30
#define TX_DESCRIPTORDONEHWTX_MASK		0x80000000
#define TX_DESCRIPTORDONEHWTX_OFFSET		31

#define RX_MAX_AGG_RX_IND_NUM 16

// NX Hardware (old)
struct rx_vector_1_nx {
	/** Receive Vector 1a */
	u32 leg_length		:12;
	u32 leg_rate		: 4;
	u32 ht_length		:16;

	/** Receive Vector 1b */
	u32 _ht_length		: 4; // FIXME
	u32 short_gi		: 1;
	u32 stbc		: 2;
	u32 smoothing		: 1;
	u32 mcs			: 7;
	u32 pre_type		: 1;
	u32 format_mod		: 3;
	u32 ch_bw		: 2;
	u32 n_sts		: 3;
	u32 lsig_valid		: 1;
	u32 sounding		: 1;
	u32 num_extn_ss		: 2;
	u32 aggregation		: 1;
	u32 fec_coding		: 1;
	u32 dyn_bw		: 1;
	u32 doze_not_allowed	: 1;

	/** Receive Vector 1c */
	u32 antenna_set		: 8;
	u32 partial_aid		: 9;
	u32 group_id		: 6;
	u32 first_user		: 1;
	s32 rssi1		: 8;

	/** Receive Vector 1d */
	s32 rssi2		: 8;
	s32 rssi3		: 8;
	s32 rssi4		: 8;
	u32 reserved_1d		: 8;
};

struct rx_vector_2_nx {
	/** Receive Vector 2a */
	u32 rcpi		: 8;
	u32 evm1		: 8;
	u32 evm2		: 8;
	u32 evm3		: 8;

	/** Receive Vector 2b */
	u32 evm4		: 8;
	u32 reserved2b_1	: 8;
	u32 reserved2b_2	: 8;
	u32 reserved2b_3	: 8;

};

struct mpdu_status_nx {
	u32 rx_vect2_valid	: 1;
	u32 resp_frame		: 1;
	u32 decr_status		: 3;
	u32 rx_fifo_oflow	: 1;
	u32 undef_err		: 1;
	u32 phy_err		: 1;
	u32 fcs_err		: 1;
	u32 addr_mismatch	: 1;
	u32 ga_frame		: 1;
	u32 current_ac		: 2;
	u32 frm_successful_rx	: 1;
	u32 desc_done_rx	: 1;
	u32 key_sram_index	:10;
	u32 key_sram_valid	: 1;
	u32 type		: 2;
	u32 subtype		: 4;
};

// HE Hardware
struct rx_leg_vect {
	u8 dyn_bw_in_non_ht	: 1;
	u8 chn_bw_in_non_ht	: 2;
	u8 rsvd_nht		: 4;
	u8 lsig_valid		: 1;
} __packed;

struct rx_ht_vect {
	u16 sounding		: 1;
	u16 smoothing		: 1;
	u16 short_gi		: 1;
	u16 aggregation		: 1;
	u16 stbc		: 1;
	u16 num_extn_ss		: 2;
	u16 lsig_valid		: 1;
	u16 mcs			: 7;
	u16 fec			: 1;

	u16 length		:16;
} __packed;

struct rx_vht_vect {
	u8 sounding		: 1;
	u8 beamformed		: 1;
	u8 short_gi		: 1;
	u8 rsvd_vht1		: 1;
	u8 stbc			: 1;
	u8 doze_not_allowed	: 1;
	u8 first_user		: 1;
	u8 rsvd_vht2		: 1;

	u16 partial_aid		: 9;
	u16 group_id		: 6;
	u16 rsvd_vht3		: 1;

	u32 mcs			: 4;
	u32 nss			: 3;
	u32 fec			: 1;
	u32 length		:20;
	u32 rsvd_vht4		: 4;
} __packed;

struct rx_he_vect {
	u8 sounding		: 1;
	u8 beamformed		: 1;
	u8 gi_type		: 2;
	u8 stbc			: 1;
	u8 rsvd_he1		: 3;

	u8 uplink_flag		: 1;
	u8 beam_change		: 1;
	u8 dcm			: 1;
	u8 he_ltf_type		: 2;
	u8 doppler		: 1;
	u8 rsvd_he2		: 2;

	u8 bss_color		: 6;
	u8 rsvd_he3		: 2;

	u8 txop_duration	: 7;
	u8 rsvd_he4		: 1;

	u8 pe_duration		: 4;
	u8 spatial_reuse	: 4;

	u8 sig_b_comp_mode	: 1;
	u8 dcm_sig_b		: 1;
	u8 mcs_sig_b		: 3;
	u8 ru_size		: 3;

	u32 mcs			: 4;
	u32 nss			: 3;
	u32 fec			: 1;
	u32 length		:20;
	u32 rsvd_he6		: 4;
} __packed;

struct rx_vec_detail_1 {	/* = struct rx_vector_1 */
	u8 format_mod		: 4;
	u8 ch_bw		: 3;
	u8 pre_type		: 1;

	u8 antenna_set		: 8;

	s32 rssi_leg		: 8; //RSSI(dBm)
	u32 leg_length		:12;
	u32 leg_rate		: 4;
	s32 snr			: 8; //changed to SNR(dB) by phy RTL

	union {
		struct rx_leg_vect	leg;
		struct rx_ht_vect	ht;
		struct rx_vht_vect	vht;
		struct rx_he_vect	he;
		/* alias of he */
		struct rx_he_vect	he_su;
		struct rx_he_vect	he_mu;
		struct rx_he_vect	he_er;
		struct rx_he_vect	he_tb;
	};
} __packed;

struct rx_vec_detail_2 {	/* = struct rx_vector_2 */
	/** Receive Vector 2a */
	u32 rcpi1		: 8;
	u32 rcpi2		: 8;
	u32 rcpi3		: 8;
	u32 rcpi4		: 8;

	/** Receive Vector 2b */
	u32 evm1		: 8;
	u32 evm2		: 8;
	u32 evm3		: 8;
	u32 evm4		: 8;
};

struct mpdu_status {
	u32 rx_vect2_valid	: 1;
	u32 resp_frame		: 1;
	u32 decr_type		: 4;
	u32 decr_err		: 1;
	u32 undef_err		: 1;
	u32 fcs_err		: 1;
	u32 addr_mismatch	: 1;
	u32 ga_frame		: 1;
	u32 current_ac		: 2;
	u32 frm_successful_rx	: 1;
	u32 desc_done_rx	: 1;
	u32 key_sram_index	:10;
	u32 key_sram_v		: 1;
	u32 type		: 2;
	u32 subtype		: 4;
};

/*
 * Used for both Hardware type (but for NX Hardware rx_vectx and status fields
 * must be converted first using rwnx_xxxx_convert function).
 * It is ok to use same structure for both HW type because both version
 * of 'RX vectors' and 'MPDU Status' have the same size.
 */
struct hw_vect {
	/** Total length for the MPDU transfer */
	u16 frmlen;

	/** AMPDU Status Information */
	union {
		u16 ampdu_stat_info;
		u16 reserved		: 8,
			mpdu_cnt	: 6,
			ampdu_cnt	: 2;
	};

	/** TSF Low */
	u32 tsflo;
	/** TSF High */
	u32 tsfhi;

	/** Receive Vector 1 */
	struct rx_vec_detail_1 rx_vec_1;
	/** Receive Vector 2 */
	struct rx_vec_detail_2 rx_vec_2;

	/** MPDU status information */
	struct mpdu_status statinfo;

	u32 rhd_extend_0;
	union {
		u32 desc_buf_info;
		u32 rhd_extend_1;
	};
};

struct phy_channel_info_desc {
	/** PHY channel information 1 */
	u32 phy_band		: 8;
	u32 phy_channel_type	: 8;
	u32 phy_prim20_freq	:16;

	/** PHY channel information 2 */
	u32 phy_center1_freq	:16;
	u32 phy_center2_freq	:16;
};

struct hw_rxhdr {
	/** RX vector */
	struct hw_vect hwvect;

	/** PHY channel information */
	struct phy_channel_info_desc phy_info;

	/** RX flags */
	u32 flags_is_amsdu	: 1;
	u32 flags_is_80211_mpdu	: 1;
	u32 flags_is_4addr	: 1;
	u32 flags_new_peer	: 1;
	u32 flags_user_prio	: 3;
	u32 flags_rsvd0	: 1;
	u32 flags_vif_idx	: 8;	// 0xFF if invalid VIF index
	u32 flags_sta_idx	: 8;	// 0xFF if invalid STA index
	u32 flags_dst_idx	: 8;	// 0xFF if unknown destination STA
#if 0 // CONFIG_RWNX_MON_DATA
	/// MAC header backup descriptor (used only for MSDU when there is a monitor and a data interface)
	struct mon_machdrdesc mac_hdr_backup;
#endif
	/** Pattern indicating if the buffer is available for the driver */
	u32 pattern;
};

struct wq_rx_hdr {
	u16 status;
	u16 is_ampdu			: 1,
		tid			: 3,
		sn			: 12;
	union {
		u32 new_read;	//FW used only
		u32 buf_rd_idx	: 25,
			reserved	: 7;
	};
	u32 mac_id			: 4,	//FW used only
		buf_id			: 4,	//FW used only
		msdu_seq		: 5,	//For msdu subframe duplicate check
		msdu_seq_end		: 1,	//For msdu subframe end check
		desc_rd_idx		: 17,	//FW used only
		wq_pkt_no_need_free	: 1;	//FW used only
	u32 rx_ll_payload_present 	: 1,
		use_backup_ring		: 1,
		reserved_1 		: 30;
} __packed;

struct rxdesc_tag_wq {
	struct wq_rx_hdr sw_rxhdr;
	struct hw_rxhdr hw_rxhdr;
};

struct desc_buf_info {
	u32 is_buf_valid 		: 1,
	    buf_invalid_resean 		: 3,
	    header_in_buf_ring 		: 1,
	    reserverd 			: 2,
	    buf_ring_wr_flag 		: 1,
	    buf_ring_wr_index 		: 24;
};

struct rx_ll_ind_param {
	struct hw_vect hwvect;
	struct phy_channel_info_desc phy_info;
	/** RX flags */
	u32 flags_is_amsdu 		: 1,
	    flags_is_80211_mpdu 	: 1,
	    flags_is_4addr 		: 1,
	    flags_new_peer 		: 1,
	    flags_user_prio 		: 3,
	    flags_rsvd0 		: 1,
	    flags_vif_idx 		: 8, // 0xFF if invalid VIF index
	    flags_sta_idx 		: 8, // 0xFF if invalid STA index
	    flags_dst_idx 		: 8; // 0xFF if unknown destination STA
	u16 status;
	u16 is_ampdu 			: 1,
	    tid 			: 3,
	    sn 				: 12;
	u8 dest_addr[6];
	u8 src_addr[6];
	u8 frag_num 			: 4,
	    more_frag 			: 1,
	    mic_check 			: 1,
	    to_ds			: 1,
	    from_ds			: 1;
} __packed;

struct rx_ll_payload {
	u8 aggre_num;
	struct rx_ll_ind_param params[RX_MAX_AGG_RX_IND_NUM];
} __packed;

#endif /* WQ_FW_WIFI_MAC_DP_RX_API_H_ */
