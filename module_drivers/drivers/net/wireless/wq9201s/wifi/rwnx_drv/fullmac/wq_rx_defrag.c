#include "rwnx_defs.h"
#include "wq_log.h"
#include "wq_rx_defrag.h"
#include "rwnx_compat.h"

void wq_rxu_cntrl_defrag_timeout_cb(struct timer_list *t)
{
	struct rxu_cntrl_defrag *defrag = from_timer(defrag, t, timer);
	struct rwnx_hw *rwnx_hw = defrag->rwnx_hw;
	unsigned long iflags;
	struct list_head *list;

	spin_lock_irqsave(&rwnx_hw->rx_defrag.defrag_lock, iflags);
	WQ_DBG(DM_RX, DL_INF, "%s:defrag=%p lock %p\n", __func__, defrag,
	       &rwnx_hw->rx_defrag.defrag_lock);

	if (defrag->skb) {
		dev_kfree_skb(defrag->skb);
		defrag->skb = NULL;
	} else {
		WQ_DBG(DM_RX, DL_ERR,
		       "%s:defrag is in use, but has no defrag->skb!\n",
		       __func__);
	}

	// Remove the structure from the list
	list_for_each (list, &rwnx_hw->rx_defrag.rxu_defrag_used) {
		if (list == &defrag->entry) {
			list_del(&defrag->entry);
			rwnx_hw->rx_defrag.used_cnt--;
			list_add_tail(&defrag->entry,
				      &rwnx_hw->rx_defrag.rxu_defrag_free);
			break;
		}
	}

	WQ_DBG(DM_RX, DL_INF, "%s:defrag=%p unolock %p\n", __func__, defrag,
	       &rwnx_hw->rx_defrag.defrag_lock);
	spin_unlock_irqrestore(&rwnx_hw->rx_defrag.defrag_lock, iflags);
}

void wq_rxu_defrag_init(struct rwnx_hw *rwnx_hw)
{
	uint16_t i = 0;
	INIT_LIST_HEAD(&rwnx_hw->rx_defrag.rxu_defrag_free);
	INIT_LIST_HEAD(&rwnx_hw->rx_defrag.rxu_defrag_used);
	rwnx_hw->rx_defrag.used_cnt = 0;

	for (i = 0; i < RX_CNTRL_DEFRAG_POOL_SIZE; i++) {
		struct rxu_cntrl_defrag *defrag =
			&rwnx_hw->rx_defrag.rxu_cntrl_defrag_pool[i];
		defrag->rwnx_hw = rwnx_hw;
		timer_setup(&defrag->timer, wq_rxu_cntrl_defrag_timeout_cb, 0);
		INIT_LIST_HEAD(&defrag->entry);
		list_add_tail(&defrag->entry,
			      &rwnx_hw->rx_defrag.rxu_defrag_free);
	}
}

void wq_rxu_defrag_deinit(struct rwnx_hw *rwnx_hw)
{
	uint16_t i = 0;
	uint8_t used_cnt;
	unsigned long iflags;

	spin_lock_irqsave(&rwnx_hw->rx_defrag.defrag_lock, iflags);
	used_cnt = rwnx_hw->rx_defrag.used_cnt;

	for (i = 0; i < used_cnt; i++) {
		struct rxu_cntrl_defrag *defrag =
			list_first_entry(&rwnx_hw->rx_defrag.rxu_defrag_used,
					 struct rxu_cntrl_defrag, entry);
		list_del(&defrag->entry);
		rwnx_hw->rx_defrag.used_cnt--;

		if (defrag) {
			if (defrag->skb) {
				dev_kfree_skb(defrag->skb);
				defrag->skb = NULL;
			} else {
				WQ_DBG(DM_RX, DL_ERR,
				       "%s:defrag is in use, but has no defrag->skb!\n",
				       __func__);
			}
			list_add_tail(&defrag->entry,
				      &rwnx_hw->rx_defrag.rxu_defrag_free);
		} else {
			WQ_DBG(DM_RX, DL_ERR, "%s:get defrag FAIL(%d/%d)!\n",
			       __func__, i, used_cnt);
		}
	}

	spin_unlock_irqrestore(&rwnx_hw->rx_defrag.defrag_lock, iflags);
}

struct rxu_cntrl_defrag *wq_rxu_cntrl_defrag_get(struct rwnx_hw *rwnx_hw,
						 uint16_t sta_idx, uint16_t sn,
						 uint8_t tid)
{
	struct rxu_cntrl_defrag *defrag;
	list_for_each_entry (defrag, &rwnx_hw->rx_defrag.rxu_defrag_used,
			     entry) {
		// Compare Station Id, TID, Sequence Num.
		if ((defrag->sta_idx == sta_idx) && (defrag->tid == tid) &&
		    (defrag->sn == sn)) {
			// We found a matching structure, escape from the loop
			return defrag;
		}
	}
	return NULL;
}

struct rxu_cntrl_defrag *wq_rxu_cntrl_defrag_alloc(struct rwnx_hw *rwnx_hw)
{
	struct rxu_cntrl_defrag *defrag;
	// Get the first element of the list of used Reassembly structures
	if (!list_empty(&rwnx_hw->rx_defrag.rxu_defrag_free)) {
		defrag = list_first_entry(&rwnx_hw->rx_defrag.rxu_defrag_free,
					  struct rxu_cntrl_defrag, entry);
	} else {
		// Get the first element of the list of used Reassembly structures
		defrag = list_first_entry(&rwnx_hw->rx_defrag.rxu_defrag_used,
					  struct rxu_cntrl_defrag, entry);
		rwnx_hw->rx_defrag.used_cnt--;
		// Sanity check - There shall be an available structure
		WQ_ASSERT(defrag != NULL, "%s:defrag is NULL", __func__);

		WQ_DBG(DM_RX, DL_INF,
		       "[auto]msg:%s: Get defrag from used list(%p), F/U:%d/%d\n",
		       __func__, defrag,
		       RX_DEFRAG_FREE_CNT(rwnx_hw->rx_defrag.used_cnt),
		       rwnx_hw->rx_defrag.used_cnt);

		if (defrag->skb) {
			dev_kfree_skb(defrag->skb);
			defrag->skb = NULL;
		} else {
			WQ_DBG(DM_RX, DL_ERR,
			       "%s:defrag is in use, but has no defrag->skb!\n",
			       __func__);
		}
		del_timer_sync(&defrag->timer);
	}

	list_del(&defrag->entry);
	// Return the allocated element
	return (defrag);
}

struct sk_buff *wq_rx_defrag(struct rwnx_hw *rwnx_hw,
			     struct wq_rx_hdr *sw_rxhdr,
			     struct rx_ll_ind_param *rx_ll_sub_ind, u8 *ptr,
			     u32 frame_len)
{
#define SKB_FULL_LEN 2346

	struct hw_rxhdr *hw_rxhdr = (struct hw_rxhdr *)rx_ll_sub_ind;
	bool to_unlock = false;
	struct rxu_cntrl_defrag *defrag;
	struct sk_buff *defrag_skb = NULL;
	uint16_t sta_idx = RWNX_INVALID_STA;
	uint16_t mf = rx_ll_sub_ind->more_frag;
	unsigned long iflags;
	struct ethhdr *eth;
	u16 offset = sizeof(struct wq_rx_hdr) + sizeof(struct hw_rxhdr);
	struct list_head *list;

	do {
		spin_lock_irqsave(&rwnx_hw->rx_defrag.defrag_lock, iflags);
		WQ_DBG(DM_RX, DL_INF, "%s:lock %p, F/U:%d/%d\n", __func__,
		       &rwnx_hw->rx_defrag.defrag_lock,
		       RX_DEFRAG_FREE_CNT(rwnx_hw->rx_defrag.used_cnt),
		       rwnx_hw->rx_defrag.used_cnt);

		//to get sta_idx
		sta_idx = hw_rxhdr->flags_sta_idx;
		defrag = wq_rxu_cntrl_defrag_get(rwnx_hw, sta_idx,
						 rx_ll_sub_ind->sn,
						 rx_ll_sub_ind->tid);

		if (!defrag) {
			// If not first fragment, we can reject the packet
			if (rx_ll_sub_ind->frag_num) {
				WQ_DBG(DM_RX, DL_INF,
				       "%s: It's not first fragment, fn=%d",
				       __func__, rx_ll_sub_ind->frag_num);
				to_unlock = true;
				break;
			}

			// Allocate a Reassembly structure
			defrag = wq_rxu_cntrl_defrag_alloc(rwnx_hw);
			if (!defrag) {
				WQ_DBG(DM_RX, DL_ERR, "%s:get defrag fail!\n",
				       __func__);
				to_unlock = true;
				break;
			}

			defrag_skb = dev_alloc_skb(SKB_FULL_LEN +
						   sizeof(struct wq_rx_hdr) +
						   sizeof(struct hw_rxhdr));
			if (!defrag_skb) {
				list_add_tail(
					&defrag->entry,
					&rwnx_hw->rx_defrag.rxu_defrag_free);
				WQ_DBG(DM_RX, DL_ERR, "%s:alloc_skb fail!\n",
				       __func__);
				to_unlock = true;
				break;
			}

			// Fullfil the Reassembly structure
			defrag->sta_idx = sta_idx;
			defrag->tid = rx_ll_sub_ind->tid;
			defrag->sn = rx_ll_sub_ind->sn;
			defrag->next_fn = 1;

			// Defragmentation timeout
			mod_timer(&defrag->timer,
				  jiffies + msecs_to_jiffies(
						    RX_CNTRL_DEFRAG_MAX_WAIT));
			list_add_tail(&defrag->entry,
				      &rwnx_hw->rx_defrag.rxu_defrag_used);
			rwnx_hw->rx_defrag.used_cnt++;

			// copy SW/HW header */
			sw_rxhdr->status =
				rx_ll_sub_ind->status | RX_STAT_LEN_UPDATE;
			sw_rxhdr->is_ampdu = rx_ll_sub_ind->is_ampdu;
			sw_rxhdr->tid = rx_ll_sub_ind->tid;
			sw_rxhdr->sn = rx_ll_sub_ind->sn;
			memcpy(defrag_skb->data, sw_rxhdr,
			       sizeof(struct wq_rx_hdr));
			memcpy(defrag_skb->data + sizeof(struct wq_rx_hdr),
			       rx_ll_sub_ind, sizeof(struct hw_rxhdr));

			// copy ethhdr
			eth = (struct ethhdr *)(defrag_skb->data + offset);
			memcpy(eth->h_dest, rx_ll_sub_ind->dest_addr, ETH_ALEN);
			memcpy(eth->h_source, rx_ll_sub_ind->src_addr,
			       ETH_ALEN);
			memcpy(&eth->h_proto, ptr + 6, 2);
			offset += sizeof(struct ethhdr);

			// copy the payload without llc/snap/type
			memcpy(defrag_skb->data + offset, ptr + 8,
			       frame_len - 8);
			offset += (frame_len - 8);
			skb_put(defrag_skb, offset);

			defrag->skb = defrag_skb;
			//dump_bytes(DL_WRN, "defrag->skb First", defrag->skb->data, defrag->skb->len);
			WQ_DBG(DM_RX, DL_INF, "%s:free:%d, used:%d!\n",
			       __func__,
			       RX_DEFRAG_FREE_CNT(rwnx_hw->rx_defrag.used_cnt),
			       rwnx_hw->rx_defrag.used_cnt);
			WQ_DBG(DM_RX, DL_INF,
			       "%s: defrag=%p defrag->skb->data=%p ,skb->len=%d/%d, frame_len=%d\n",
			       __func__, defrag, defrag->skb->data, offset,
			       defrag->skb->len, frame_len);

			//to_unlock = true;
			//break;
		} else {
			// Check the fragment is the one we are waiting for
			if (defrag->next_fn != rx_ll_sub_ind->frag_num) {
				// Packet has already been received
				to_unlock = true;
				break;
			}

			if (!defrag->skb) {
				WQ_DBG(DM_RX, DL_ERR,
				       "%s:defrag->skb NULL %p\n", __func__,
				       defrag);
				to_unlock = true;
				break;
			}

			//extend defrag timeout
			mod_timer(&defrag->timer,
				  jiffies + msecs_to_jiffies(
						    RX_CNTRL_DEFRAG_MAX_WAIT));

			// Update number of received fragment
			defrag->next_fn++;

			WQ_DBG(DM_RX, DL_INF,
			       "%s:defrag %p defrag->skb->data %p\n", __func__,
			       defrag, defrag->skb->data);

			defrag_skb = defrag->skb;
			memcpy(defrag_skb->data + defrag_skb->len, ptr,
			       frame_len);
			skb_put(defrag_skb, frame_len);

			//if(mf)
			//    dump_bytes(DL_WRN, "defrag->skb mid", defrag->skb->data, defrag->skb->len);

			if (!mf) {
				del_timer_sync(&defrag->timer);

				//dump_bytes(DL_WRN, "defrag->skb end", defrag->skb->data, defrag->skb->len);

				//Update rx status, remove RX_STAT_LEN_UPDATE
				sw_rxhdr =
					(struct wq_rx_hdr *)defrag->skb->data;
				sw_rxhdr->status = rx_ll_sub_ind->status;

				defrag->skb = NULL;
				// Free the Reassembly structure
				list_for_each (
					list,
					&rwnx_hw->rx_defrag.rxu_defrag_used) {
					if (list == &defrag->entry) {
						list_del(&defrag->entry);
						rwnx_hw->rx_defrag.used_cnt--;
						list_add_tail(
							&defrag->entry,
							&rwnx_hw->rx_defrag
								 .rxu_defrag_free);
						break;
					}
				}
			}
		}

		WQ_DBG(DM_RX, DL_INF, "%s:unlock %p F/U:%d/%d\n", __func__,
		       &rwnx_hw->rx_defrag.defrag_lock,
		       RX_DEFRAG_FREE_CNT(rwnx_hw->rx_defrag.used_cnt),
		       rwnx_hw->rx_defrag.used_cnt);
		spin_unlock_irqrestore(&rwnx_hw->rx_defrag.defrag_lock, iflags);
	} while (0);

	if (to_unlock) {
		WQ_DBG(DM_RX, DL_INF, "%s:unlock %p F/U:%d/%d\n", __func__,
		       &rwnx_hw->rx_defrag.defrag_lock,
		       RX_DEFRAG_FREE_CNT(rwnx_hw->rx_defrag.used_cnt),
		       rwnx_hw->rx_defrag.used_cnt);
		spin_unlock_irqrestore(&rwnx_hw->rx_defrag.defrag_lock, iflags);
	}

	WQ_DBG(DM_RX, DL_INF, "%s:(return) defrag_skb %p defrag_skb->data %p\n",
	       __func__, defrag_skb, defrag_skb ? defrag_skb->data : NULL);
	return (defrag_skb);
}
