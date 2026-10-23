/**
 ******************************************************************************
 *
 * @file rwnx_rx_ll.h
 *
 * Copyright (C) wuqi
 *
 ******************************************************************************
 */
#ifndef _RWNX_RX_LL_H_
#define _RWNX_RX_LL_H_

#define FREE_MSG_TIMEOUT_INTERVAL msecs_to_jiffies(100) // 100ms

#define HOST_DATA_RING_WRAP_FLAG_CLEAR_MASK 0xFFFFFF

// Host data ring size
#define HOST_RX_RING_SIZE (4 << 20) // 4M Bytes
#define HOST_RX_RING_SUB_STEP_SIZE_1_M (1 << 20) // 1M Bytes
#define HOST_RX_RING_SUB_STEP_SIZE_100_K (102400) // 100K Bytes

//Checksum offload
#define IPHDR_LEN 20
#define IPHDR_NOIP_LEN (IPHDR_LEN - 8) //IPHDR_LEN(20) - SRC/DST_IP_LEN(8)
#define RFC1042_HDR_LEN 8 //LLC(3)+SNAP(3)+TYPE(2)
#define RFC1042_IPHDR_NOIP_LEN (RFC1042_HDR_LEN + IPHDR_NOIP_LEN)

u32 memcpy_cksum(unsigned char *dst, const unsigned char *buff, int len);
void ieee80211_amsdu_to_8023s_ll(struct sk_buff *skb, struct sk_buff_head *list,
				 const u8 *addr, enum nl80211_iftype iftype,
				 const unsigned int extra_headroom,
				 const u8 *check_da, const u8 *check_sa);

extern u8 gv_cksum_offload;

struct rx_free_msg_info {
	struct timer_list rx_free_msg_timer;
	u32 read_offset;
	u32 macid;
	u32 rx_free_idx[2];
	u32 rx_free_thrd[2];
	u32 last_read_offset;
	bool send_flag[2];
	bool fw_recoverying;
	bool use_backup_ring;
	bool use_backup_ring_pre;
};

struct rx_rae_ll_host_ring_info {
	// Host data ring dma address
	dma_addr_t dma;
	// Host data ring start address
	void *addr;
	// Host data ring size
	u32 size;
};

struct rwnx_rx_ll {
	spinlock_t rx_free_msg_lock;
	struct rx_free_msg_info rx_free_msg_env[2];
	struct rx_rae_ll_host_ring_info rx_ring[2];
	struct rx_rae_ll_host_ring_info rx_backup_ring[2];
	bool rx_ll_support;
};

int rx_ll_init(struct rwnx_hw *rwnx_hw);
void rx_ll_deinit(struct rwnx_hw *rwnx_hw);

#endif /* _RWNX_RX_LL_H_ */