#ifndef _WQ_RX_DFRAG_H_
#define _WQ_RX_DFRAG_H_

#include <linux/list.h>
#include "fw_api/wifi/mac/dp_rx.h"

/// Size of the pool containing Reassembly structure
#define RX_CNTRL_DEFRAG_POOL_SIZE (10)
/// Maximum time we can wait for a fragment (in ms)
#define RX_CNTRL_DEFRAG_MAX_WAIT (100)
/// Remain number available of defrag pool
#define RX_DEFRAG_FREE_CNT(used) (RX_CNTRL_DEFRAG_POOL_SIZE - (used))

struct rwnx_hw;

void wq_rxu_defrag_init(struct rwnx_hw *rwnx_hw);
void wq_rxu_defrag_deinit(struct rwnx_hw *rwnx_hw);

struct rxu_cntrl_defrag *wq_rxu_cntrl_defrag_get(struct rwnx_hw *rwnx_hw,
						 uint16_t sta_idx, uint16_t sn,
						 uint8_t tid);
struct rxu_cntrl_defrag *wq_rxu_cntrl_defrag_alloc(struct rwnx_hw *rwnx_hw);
struct sk_buff *wq_rx_defrag(struct rwnx_hw *rwnx_hw,
			     struct wq_rx_hdr *sw_rxhdr,
			     struct rx_ll_ind_param *rx_ll_sub_ind, u8 *ptr,
			     u32 frame_len);

/// Structure used during reassembly
struct rxu_cntrl_defrag {
	/// Pointer to the next element in the queue
	struct list_head entry;

	// Driver info
	struct rwnx_hw *rwnx_hw;
	/// Station Index
	uint16_t sta_idx;
	/// Traffic Index
	uint8_t tid;
	/// Next Expected FN
	uint8_t next_fn;
	/// Sequence Number
	uint16_t sn;
	/// Defrag timeout cb setup
	struct timer_list timer;
	/// Build full pkt to upload
	struct sk_buff *skb;
};

//rx defrag data structure
struct wq_rx_defrag {
	struct rxu_cntrl_defrag rxu_cntrl_defrag_pool[RX_CNTRL_DEFRAG_POOL_SIZE];

	struct list_head rxu_defrag_free;

	struct list_head rxu_defrag_used;
	uint8_t used_cnt;

	spinlock_t defrag_lock;
};

#endif /* _WQ_RX_DFRAG_H_ */
