/* data path TX APIs */
#ifndef WQ_FW_WIFI_MAC_DP_TX_API_H_
#define WQ_FW_WIFI_MAC_DP_TX_API_H_

#include "fw_api/wifi/mac/api.h"

/* define enable txq ring function or not */
#define TXQ_RING_FUNCTION_ENABLE 0

/* define softap max for txq ring record */
#define TXQ_RING_RCD_SOFTAP_MAX 32

/*
 * Maximum number of payload addresses and lengths present in the descriptor
 */
#define NX_TX_PAYLOAD_MAX	6

#define AMSDU_MAX_PAD 4
#define AMSDU_MAX_MSDU_LEN 100
#define AMSDU_MAX_SIZE 1400
#define AMSDU_MAX_PTK_NUM 30
#define AMSDU_TIMEOUT 10
#define AMPDU_MAX_PTK_NUM (0xff)

/* define the amsdu num one pcie */
#define AMSDU_MAX_PTK_NUM_PCIE 20

/* define the amsdu len for TCPACK on pcie. same as sdio */
#define RWNX_TX_TCPACK_AMSDU_LEN_MAX AMSDU_MAX_SIZE //(1664 - HEADROOM_HIF_HTC - TAILROOM_HIF)

#if AMSDU_MAX_PTK_NUM > AMSDU_MAX_PTK_NUM_PCIE
#define HL_BUNDLE_MAX_NUM AMSDU_MAX_PTK_NUM
#else
#define HL_BUNDLE_MAX_NUM AMSDU_MAX_PTK_NUM_PCIE
#endif

/*
 * Pattern value for 'end_marker' in "struct hostdesc".
 */
#define HOST_DESC_END_MARKER	0xA5A5A5A5UL

/* magic num of TCPACK bundle marker */
#define TCPACK_BUNDLE_MARKER	0xFAFA

enum msdu_encp {
	MSDU_ENCAP_RAW = 0,	/* for MGMT, it's the only encapsulation method */
	MSDU_ENCAP_ETH_V2 = 1,	/* most method used by DATA */
	MSDU_ENCAP_SNAP = 2,
	MSDU_ENCAP_ETH_V2_VLAN = 3,
	MSDU_ENCAP_ETH_V2_QINQ = 4,
	MSDU_ENCAP_ETH_V2_VLAN_TUNNEL = 5,

	MSDU_ENCAP_MAX,
};

#define TXU_CNTRL_RETRY		BIT(0)	/* f/w: a retry frame */
#define TXU_CNTRL_UNDER_BA	BIT(1)	/* f/w: the frame is sent under a BlockAck agreement */
#define TXU_CNTRL_MORE_DATA	BIT(2)	/* more data are buffered on host side for this STA */
#define TXU_CNTRL_MGMT		BIT(3)	/* a management frame */
#define TXU_CNTRL_MGMT_NO_CCK	BIT(4)	/* no CCK rate can be used for current management frame */
#define TXU_CNTRL_MGMT_PM_MON	BIT(5)	/* f/w: the PM monitoring has been started for this frame */
#define TXU_CNTRL_AMSDU		BIT(6)	/* f/w: an A-MSDU frame */
#define TXU_CNTRL_MGMT_ROBUST	BIT(7)	/* a robust management frame */
#define TXU_CNTRL_USE_4ADDR	BIT(8)	/* using 4-address MAC header */
#define TXU_CNTRL_EOSP		BIT(9)	/* the frame is the last of the UAPSD service period */
#define TXU_CNTRL_MESH_FWD	BIT(10)	/* forward to another MESH point */
#define TXU_CNTRL_TDLS		BIT(11)	/* send to a TDLS peer */
#define TXU_CNTRL_POSTPONE_PS	BIT(12)	/* f/w: (AP only) postpone because of PS */
#define TXU_CNTRL_RC_TRIAL	BIT(13)	/* f/w: use the trial rate as 1st/2nd rate */
#define TXU_CNTRL_UAPSD_TRIGGER	BIT(14)	/* f/w: a UAPSD trigger frame */
#define TXU_CNTRL_AMSDU_PRESENT	BIT(15)	/* an A-MSDU encapsulated by host */

#define TXU_EXT_CNTRL_EAPOL_M4	BIT(0)	/* an EAPOL m4 frame */
#define TXU_EXT_CNTRL_HOST_BUNDLE	BIT(1)	/* an tx bundle frame */
#define TXU_EXT_CNTRL_PS_TX	BIT(2)	/* this frame is a pkt to peer in ps mode*/
#define TXU_EXT_CNTRL_AMSDU	BIT(3)	/* f/w an A-MSDU frame */
#define TXU_EXT_CNTRL_HOST_MEM	BIT(4)	/* the frame is stored in host memory (PCIe only) */

/****************************************************************************************
 * c.f LMAC/src/tx/tx_swdesc.h
 *
 * Descriptor filled by the Host
 ****************************************************************************************/
struct hostdesc {
	addr32 buf;			/* update by f/w, it points to struct wq_pkt */

	/// Pointers to packet payloads
	addr32 packet_addr[NX_TX_PAYLOAD_MAX];
	/// Sizes of the MPDU/MSDU payloads
	u16 packet_len[NX_TX_PAYLOAD_MAX];
	/// Number of payloads forming the MPDU
	u8 packet_cnt;

	/// Address of the status descriptor in host memory (used for confirmation upload)
	addr32 status_desc_addr;	/* FIXME: useless */

	/// Ethernet MAC header
	struct ethhdr ethhdr;

	/// Buffer containing the PN to be used for this packet
	u16 pn[4];
	/// Sequence Number used for transmission of this MPDU
	u16 sn;			/* FIXME: useless */
	/// Timestamp of first transmission of this MPDU
	u16 timestamp;
	/// Packet TID (0xFF if not a QoS frame)
	u8 tid;
	/// Interface Id
	u8 vif_idx;
	/// Station Id (0xFF if station is unknown)
	u8 staid;
	/// MU-MIMO information (GroupId and User Position in the group) - The GroupId
	/// is located on bits 0-5 and the User Position on bits 6-7. The GroupId value is set
	/// to 63 if MU-MIMO shall not be used
	u8 mumimo_info;
	/// TX flags
	u16 flags;		/* TXU_CNTRL_xxx */
	u16 mgmt_frame_nb;
	u8 via_grp_id;
	u8 via_type_id;

	u8 is_hml :1;
	u8 encap_type :3;	/* enum msdu_encp */
	u8 tae_mode :1;		/* FIXME: always PPDU mode */
	u8 need_info_host_when_tx_done :1;	/* f/w used */
	u8 dhcp_flag :1;
	u8 txdesc_host_freed :1;  /*indicate current txdesc host is freed */

	u8 ext_flags :5;	/* TXU_EXT_CNTRL_xxx */
	u8 ds_probe_mac1 :1;
	/// send limit used in coex wifi tx can abort bt trx mode.
	/// 00 as soon as possible
	/// 01: wait for bt event end if bt event will end soon
	/// 10: wait for bt event end
	/// 11: reserved
	u8 send_limit :2;

	/// byte offset in each payload, used for byte order reverse
	/// if 0, not used.
	/// TODO: every packet_addr could have different byte offset
	u8 payload_offset;	/* FIXME: useless */
	union {
		/// end marker, used to check if host driver side's hostdesc has same
		/// length with fw side, must be the pattern value 0xA5A5A5A5
		u32 end_marker;		/* fix: HOST_DESC_END_MARKER */
		/* host seq */
		u32 ipc_host_seq: 16,
			resv3 : 16;
    };
};

// compress hostdesc for improve usb bus utilization
struct compressed_hostdesc {
	/// Sizes of the MPDU/MSDU payloads
	u16 packet_len[NX_TX_PAYLOAD_MAX];

	/// Number of payloads forming the MPDU
	u8 packet_cnt;
	/// Packet TID (0xFF if not a QoS frame)
	u8 tid;
	/// Interface Id
	u8 vif_idx;
	/// Station Id (0xFF if station is unknown)
	u8 staid;

	/// TX flags
	u16 flags;		/* TXU_CNTRL_xxx */
	u16 mgmt_frame_nb;

	u8 via_grp_id;
	u8 via_type_id;
	u8 is_hml :1;
	u8 encap_type :3;	/* enum msdu_encp */
	u8 tae_mode :1;		/* FIXME: always PPDU mode */
	u8 compress_eth_ip_flag :1; /* it is need_info_host_when_tx_done bit in hostdesc */
	u8 dhcp_flag :1;
	u8 txdesc_host_freed :1;  /*indicate current txdesc host is freed */
	u8 ext_flags :5;	/* TXU_EXT_CNTRL_xxx */
	u8 ds_probe_mac1 :1;
	/// send limit used in coex wifi tx can abort bt trx mode.
	/// 00 as soon as possible
	/// 01: wait for bt event end if bt event will end soon
	/// 10: wait for bt event end
	/// 11: reserved
	u8 send_limit :2;
};

struct txdesc_api {
	/// Information provided by Host
	struct hostdesc host;
};

struct txdesc_host {
	u32 ready;

	/// API of the embedded part
	struct txdesc_api api;
};

// record these member may compressed
struct eth_ip_compress_record {
    struct ethhdr ethhdr;
    u8 ip_version_hdrlen;
    u8 ip_dscp;
    u32 saddr;
    u32 daddr;
};

struct compressed_eth_ip_hdr {
	u16	tot_len;
	u16	id;
	u16	frag_off;
	u8	ttl;
	u8	protocol;
	u16	check;
};

/* txq ring free timer, unit us */
#define TXQ_RING_FREE_TIME_NS 200000UL
/* config ring 0 size */
#define CONFIG_RING0_SZ 2500
/* config ring 1 size */
#define CONFIG_RING1_SZ 2500
/* config ring 2 size */
#define CONFIG_RING2_SZ 2500
/* config ring 3 size */
#define CONFIG_RING3_SZ 2500

/* define the range of msdu bitmap. 64 * 2 * 6 = 768bit; 768/32 = 24 */
#define MAC_TXQ_MSDU_SEQ_WINSZ_WD_MAX 24

/* define the range of msdu bitmap. 64 * 2 * 6 = 768bit */
#define MAC_TXQ_MSDU_BITMAP_MAX 768

enum txq_ring_id {
    /* txq ring 0 */
    TXQ_RING_ID_0,
    /* txq ring 1 */
    TXQ_RING_ID_1,
    /* txq ring 2 */
    TXQ_RING_ID_2,
    /* txq ring 3 */
    TXQ_RING_ID_3,

    TXQ_RING_ID_MAX,
};

struct _ring_config {
    u32 ring_confg_size;
    u16 water_mark_num;
};

struct _tx_ring_cfg {
    /* ring size */
    u32 ring_sz;
    /* host read idx */
    u32 ring_host_read_idx;
    /* host write idx */
    u32 ring_host_write_idx;
    /* fw tx done idx */
    u32 ring_fw_done_idx : 16,
    /* host fw sync seq end */
        ring_sync_seq_end : 16;
    /* water mark */
    u32 ring_water_mark_buf_num : 16,
    /* is overflow */
        ring_overflow : 1,
        ring_resv : 15;
    /* host fw sync seq start */
    u32 ring_sync_seq_start : 16,
    /* fw has more msdu num */
        ring_fw_more_msdu_num : 16;
};

struct _tx_ring_msdu_map {
	u32 msdu_bitmap[MAC_TXQ_MSDU_SEQ_WINSZ_WD_MAX];
};

struct _tx_buf_ring {
    struct _tx_ring_cfg txq_ring[TXQ_RING_ID_MAX];
    struct _tx_ring_msdu_map msdu_map[TXQ_RING_ID_MAX];
};

/* txq ring addr */
extern struct _tx_buf_ring *g_txq_ring;

#endif /* WQ_FW_WIFI_MAC_DP_TX_API_H_ */
