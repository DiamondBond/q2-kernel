/**
 ******************************************************************************
 *
 * @file rwnx_rx.c
 *
 * Copyright (C) WUQi-Tech 2016-2024
 *
 ******************************************************************************
 */
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/ieee80211.h>
#include <linux/time.h>
#include <net/ieee80211_radiotap.h>

#include "fw_api/wifi/mac/dp_rx.h"
#include "ieee80211_ht.h"
#include "rwnx_compat.h"
#include "rwnx_defs.h"
#include "rwnx_events.h"
#include "rwnx_rx.h"
#include "rwnx_rx_ll.h"
#include "rwnx_tx.h"
#include "wq_ipc.h"
#include "wq_profiling.h"
#include "wq_log.h"
#include "wq_wifi_dbg.h"

// LL TODO: header file for these common stuff?
struct vendor_radiotap_hdr {
	u8 oui[3];
	u8 subns;
	u16 len;
	u8 data[];
};

extern u8 rwnx_rx_rtap_hdrlen(struct rx_vec_detail_1 *rxvect,
			      bool has_vend_rtap);

int rwnx_rx_monitor(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
		    struct sk_buff *skb, struct hw_rxhdr *hw_rxhdr_ptr,
		    u8 rtap_len);

void rwnx_rx_statistic(struct rwnx_hw *rwnx_hw, struct hw_rxhdr *hw_rxhdr,
		       struct rwnx_sta *sta);

void rwnx_rx_mgmt_any(struct rwnx_hw *rwnx_hw, struct sk_buff *skb,
		      struct hw_rxhdr *hw_rxhdr);

void rx_ll_free_host_data_ring(struct rwnx_hw *rwnx_hw, u32 mac_id,
			       u32 buf_rd_idx, bool use_backup_ring);

struct sk_buff *rx_ll_copy_per_payload(struct rwnx_hw *rwnx_hw,
				       struct sk_buff *skb,
				       struct rx_ll_ind_param *rx_ll_sub_ind);

uint8_t gv_cksum_offload;
inline unsigned short from32to16(unsigned int x)
{
	/* add up 16-bit and 16-bit for 16+c bit */
	x = (x & 0xffff) + (x >> 16);
	/* add up carry.. */
	x = (x & 0xffff) + (x >> 16);
	return x;
}

unsigned int memcpy_cksum(unsigned char *dst, const unsigned char *buff,
			  int len)
{
	int odd;
	unsigned int result = 0;

	if (len <= 0)
		goto out;
	odd = 1 & (unsigned long)buff;
	if (odd) {
#ifdef __LITTLE_ENDIAN
		result += (*buff << 8);
#else
		result = *buff;
#endif
		//copy payload
		*dst = *buff;
		dst++;

		len--;
		buff++;
	}

	if (len >= 2) {
		if (2 & (unsigned long)buff) {
			result += *(unsigned short *)buff;

			//copy payload
			*dst = *buff;
			*(dst + 1) = *(buff + 1);

			len -= 2;
			buff += 2;
			dst += 2;
		}
		if (len >= 4) {
			const unsigned char *end = buff + ((unsigned)len & ~3);
			unsigned int carry = 0;
			do {
				unsigned int w = *(unsigned int *)buff;

				//copy payload
				*dst = *buff;
				*(dst + 1) = *(buff + 1);
				*(dst + 2) = *(buff + 2);
				*(dst + 3) = *(buff + 3);

				buff += 4;
				dst += 4;
				result += carry;
				result += w;
				carry = (w > result);
			} while (buff < end);
			result += carry;
			result = (result & 0xffff) + (result >> 16);
		}
		if (len & 2) {
			result += *(unsigned short *)buff;

			//copy payload
			*dst = *buff;
			*(dst + 1) = *(buff + 1);
			dst += 2;

			buff += 2;
		}
	}
	if (len & 1)
#ifdef __LITTLE_ENDIAN
		result += *buff;
#else
		result += (*buff << 8);
#endif
	//copy payload
	*dst = *buff;
	result = from32to16(result);
	if (odd)
		result = ((result >> 8) & 0xff) | ((result & 0xff) << 8);
out:
	return result;
}

static void __frame_add_frag_ll(struct sk_buff *skb, struct page *page,
				void *ptr, int len, int size)
{
	struct skb_shared_info *sh = skb_shinfo(skb);
	int page_offset;

	get_page(page);
	page_offset = ptr - page_address(page);
	skb_add_rx_frag(skb, sh->nr_frags, page, page_offset, len, size);
}

static void __ieee80211_amsdu_copy_frag_ll(struct sk_buff *skb,
					   struct sk_buff *frame, int offset,
					   int len)
{
	struct skb_shared_info *sh = skb_shinfo(skb);
	const skb_frag_t *frag = &sh->frags[0];
	struct page *frag_page;
	void *frag_ptr;
	int frag_len, frag_size;
	int head_size = skb->len - skb->data_len;
	int cur_len;

	frag_page = virt_to_head_page(skb->head);
	frag_ptr = skb->data;
	frag_size = head_size;

	while (offset >= frag_size) {
		offset -= frag_size;
		frag_page = skb_frag_page(frag);
		frag_ptr = skb_frag_address(frag);
		frag_size = skb_frag_size(frag);
		frag++;
	}

	frag_ptr += offset;
	frag_len = frag_size - offset;

	cur_len = min(len, frag_len);

	__frame_add_frag_ll(frame, frag_page, frag_ptr, cur_len, frag_size);
	len -= cur_len;

	while (len > 0) {
		frag_len = skb_frag_size(frag);
		cur_len = min(len, frag_len);
		__frame_add_frag_ll(frame, skb_frag_page(frag),
				    skb_frag_address(frag), cur_len, frag_len);
		len -= cur_len;
		frag++;
	}
}

static struct sk_buff *__ieee80211_amsdu_copy_ll(struct sk_buff *skb,
						 unsigned int hlen, int offset,
						 int len, bool reuse_frag)
{
	struct sk_buff *frame;
	int cur_len = len;

	if (skb->len - offset < len)
		return NULL;

	/*
	 * When reusing framents, copy some data to the head to simplify
	 * ethernet header handling and speed up protocol header processing
	 * in the stack later.
	 */
	if (reuse_frag)
		cur_len = min_t(int, len, 32);

	/*
	 * Allocate and reserve two bytes more for payload
	 * alignment since sizeof(struct ethhdr) is 14.
	 */
	frame = dev_alloc_skb(hlen + sizeof(struct ethhdr) + 2 + cur_len);
	if (!frame)
		return NULL;

	skb_reserve(frame, hlen + sizeof(struct ethhdr) + 2);
	if (gv_cksum_offload && !reuse_frag) {
		uint8_t *new_data = skb_put(frame, cur_len);
		uint8_t *ptr =
			skb->data + offset; //ptr = payload from llc/snap/type
		uint8_t ipproto = *(ptr + 17);

		if (*(ptr + 8) == 0x45 &&
		    (ipproto == IPPROTO_UDP ||
		     ipproto == IPPROTO_TCP)) { //ckeck IP & proto
			uint16_t cksum, allcksum;
			uint32_t cpy_len =
				cur_len -
				RFC1042_HDR_LEN; //IP HDR + TCP/UDP = (FULL-RFC1042_IPHDR)

			//rfc1042_header+IPHDR with NOIP copy
			memcpy(new_data, ptr,
			       RFC1042_IPHDR_NOIP_LEN); //ptr+20 => LLC/SNAP + IP_Hdr(with no IP)

			//compute sum from src/dst ip to end of payload
			cksum = memcpy_cksum(
				new_data + RFC1042_IPHDR_NOIP_LEN,
				ptr + RFC1042_IPHDR_NOIP_LEN,
				cpy_len - IPHDR_NOIP_LEN); // IP+UDP/TCP
			allcksum =
				ntohs(cksum) + ipproto + (cpy_len - IPHDR_LEN);

			//WQ_DBG(DM_RX, DL_ERR, "%s:(%x)cksum=%x | %x\n", __func__,  ipproto, ntohs(cksum), allcksum);
			//dump_bytes(DL_ERR, "new_data", new_data, cpy_len-FCS_LEN);
			if (allcksum != 0xFFFF) {
				WQ_DBG(DM_RX, DL_ERR,
				       "%s:(%x)cksum=%x | %x len=%x ERROR CKSUM\n",
				       __func__, ipproto, ntohs(cksum),
				       allcksum, (cpy_len - IPHDR_LEN));
				dump_bytes(DL_ERR, "(amsdu)new_data", new_data,
					   cur_len);
				//todo : drop pkt
			} else {
				frame->ip_summed = CHECKSUM_UNNECESSARY;
			}
		} else {
			// copy the payload
			skb_copy_bits(skb, offset, new_data, cur_len);
		}
	} else {
		skb_copy_bits(skb, offset, skb_put(frame, cur_len), cur_len);
	}

	len -= cur_len;
	if (!len)
		return frame;

	offset += cur_len;
	__ieee80211_amsdu_copy_frag_ll(skb, frame, offset, len);

	return frame;
}

void ieee80211_amsdu_to_8023s_ll(struct sk_buff *skb, struct sk_buff_head *list,
				 const u8 *addr, enum nl80211_iftype iftype,
				 const unsigned int extra_headroom,
				 const u8 *check_da, const u8 *check_sa)
{
	unsigned int hlen = ALIGN(extra_headroom, 4);
	struct sk_buff *frame = NULL;
	u16 ethertype;
	u8 *payload;
	int offset = 0, remaining;
	struct ethhdr eth;
	bool reuse_frag = skb->head_frag && !skb_has_frag_list(skb);
	bool reuse_skb = false;
	bool last = false;
	uint8_t dbg = 0;
	unsigned int subframe_len;
	int len;
	u8 padding;

	while (!last) {
		skb_copy_bits(skb, offset, &eth, sizeof(eth));
		len = ntohs(eth.h_proto);
		subframe_len = sizeof(struct ethhdr) + len;
		padding = (4 - subframe_len) & 0x3;

		/* the last MSDU has no padding */
		remaining = skb->len - offset;
		if (subframe_len > remaining) {
			dbg = 1;
			goto purge;
		}
		/* mitigate A-MSDU aggregation injection attacks */
		if (ether_addr_equal(eth.h_dest, rfc1042_header)) {
			dbg = 2;
			goto purge;
		}

		offset += sizeof(struct ethhdr);
		last = remaining <= subframe_len + padding;

		/* FIXME: should we really accept multicast DA? */
		if ((check_da && !is_multicast_ether_addr(eth.h_dest) &&
		     !ether_addr_equal(check_da, eth.h_dest)) ||
		    (check_sa && !ether_addr_equal(check_sa, eth.h_source))) {
			offset += len + padding;
			dbg = 3;
			continue;
		}

		/* reuse skb for the last subframe */
		if (!skb_is_nonlinear(skb) && !reuse_frag && last) {
			skb_pull(skb, offset);
			frame = skb;
			reuse_skb = true;
		} else {
			frame = __ieee80211_amsdu_copy_ll(skb, hlen, offset,
							  len, reuse_frag);
			if (!frame) {
				dbg = 4;
				goto purge;
			}

			offset += len + padding;
		}

		skb_reset_network_header(frame);
		frame->dev = skb->dev;
		frame->priority = skb->priority;

		payload = frame->data;
		ethertype = (payload[6] << 8) | payload[7];
		if (likely((ether_addr_equal(payload, rfc1042_header) &&
			    ethertype != ETH_P_AARP &&
			    ethertype != ETH_P_IPX) ||
			   ether_addr_equal(payload, bridge_tunnel_header))) {
			eth.h_proto = htons(ethertype);
			skb_pull(frame, ETH_ALEN + 2);
		}

		memcpy(skb_push(frame, sizeof(eth)), &eth, sizeof(eth));
		__skb_queue_tail(list, frame);
		WQ_DBG(DM_RX, DL_INF,
		       "%s: dbg=%d, subframe_len=%d, remaining=%d, offset=%d, len=%d, padding=%d",
		       __func__, dbg, subframe_len, remaining, offset, len,
		       padding);
	}

	if (!reuse_skb)
		dev_kfree_skb(skb);

	return;

purge:
	WQ_DBG(DM_RX, DL_WRN,
	       "%s: dbg=%d, subframe_len=%d, remaining=%d, offset=%d, len=%d, padding=%d",
	       __func__, dbg, subframe_len, remaining, offset, len, padding);
	__skb_queue_purge(list);
	dev_kfree_skb(skb);
}
void rwnx_rx_free_msg_cb(struct timer_list *t)
{
	struct rx_free_msg_info *free_msg =
		(struct rx_free_msg_info *)container_of(
			t, struct rx_free_msg_info, rx_free_msg_timer);
	struct rwnx_rx_ll *rx_ll = (struct rwnx_rx_ll *)container_of(
		free_msg, struct rwnx_rx_ll, rx_free_msg_env[free_msg->macid]);
	struct rwnx_hw *rwnx_hw =
		(struct rwnx_hw *)container_of(rx_ll, struct rwnx_hw, rx_ll);

	spin_lock(&rx_ll->rx_free_msg_lock);
	if (!free_msg->send_flag[free_msg->use_backup_ring]) {
		free_msg->send_flag[free_msg->use_backup_ring] = true;
		free_msg->rx_free_idx[free_msg->use_backup_ring] =
			free_msg->read_offset &
			HOST_DATA_RING_WRAP_FLAG_CLEAR_MASK;
		WQ_DBG(DM_RX, DL_WRN, "free host data ring(t):%d/%d:0x%x",
		       free_msg->macid, free_msg->use_backup_ring,
		       free_msg->read_offset);
		rwnx_send_free_host_ring_req(rwnx_hw, free_msg->macid,
					     free_msg->read_offset,
					     free_msg->use_backup_ring);
	}
	spin_unlock(&rx_ll->rx_free_msg_lock);
}

int rx_ll_init(struct rwnx_hw *rwnx_hw)
{
	struct rwnx_rx_ll *rx_ll = &rwnx_hw->rx_ll;
	u32 alloc_size;
	u8 mac_id;
	struct rx_free_msg_info *free_msg;
	struct rx_rae_ll_host_ring_info *rx_ring, *rx_backup_ring;

	for (mac_id = 0; mac_id < 2; mac_id++) {
		rx_ring = &rx_ll->rx_ring[mac_id];
		rx_backup_ring = &rx_ll->rx_backup_ring[mac_id];
		alloc_size = HOST_RX_RING_SIZE;

		// allocate rx ring and rx backup ring separately to avoid duplicate allocate and
		// memory leak issues for abnormal case: one of rings is allocated failed
		do {
			rx_ring->addr =
				dma_alloc_coherent(rwnx_hw->dev, alloc_size,
						   &rx_ring->dma, GFP_KERNEL);
			if (rx_ring->addr != NULL) {
				break;
			}
			if (alloc_size > HOST_RX_RING_SUB_STEP_SIZE_1_M) {
				alloc_size -= HOST_RX_RING_SUB_STEP_SIZE_1_M;
			} else if (alloc_size >
				   HOST_RX_RING_SUB_STEP_SIZE_100_K) {
				alloc_size -= HOST_RX_RING_SUB_STEP_SIZE_100_K;
			} else {
				alloc_size -= alloc_size;
			}
		} while (alloc_size > 0);

		rx_ring->size = alloc_size;
		alloc_size = HOST_RX_RING_SIZE;

		do {
			rx_backup_ring->addr =
				dma_alloc_coherent(rwnx_hw->dev, alloc_size,
						   &rx_backup_ring->dma,
						   GFP_KERNEL);
			if (rx_backup_ring->addr != NULL) {
				break;
			}
			if (alloc_size > HOST_RX_RING_SUB_STEP_SIZE_1_M) {
				alloc_size -= HOST_RX_RING_SUB_STEP_SIZE_1_M;
			} else if (alloc_size >
				   HOST_RX_RING_SUB_STEP_SIZE_100_K) {
				alloc_size -= HOST_RX_RING_SUB_STEP_SIZE_100_K;
			} else {
				alloc_size -= alloc_size;
			}
		} while (alloc_size > 0);

		rx_backup_ring->size = alloc_size;
		// TODO: Fall back to RX HL mode if 100K consistent DMA memory can't be allocated, debug here for now
		BUG_ON(rx_ring->addr == NULL || rx_ring->size == 0 ||
		       rx_backup_ring->addr == NULL ||
		       rx_backup_ring->size == 0);

		free_msg = &rx_ll->rx_free_msg_env[mac_id];
		free_msg->send_flag[0] = true;
		free_msg->send_flag[1] = true;
		free_msg->macid = mac_id;
		free_msg->fw_recoverying = false;
		free_msg->rx_free_thrd[0] = rx_ring->size >> 4;
		free_msg->rx_free_thrd[1] = rx_backup_ring->size >> 4;
		timer_setup(&free_msg->rx_free_msg_timer, rwnx_rx_free_msg_cb,
			    0);
	}

	WQ_DBG(DM_TRBUS, DL_ERR,
	       "[auto]msg: LL RX allocate consistent DMA memory (0x%x/0x%x bytes) and backup ring (0x%x/0x%x bytes)\n",
	       rx_ll->rx_ring[0].size, rx_ll->rx_ring[1].size,
	       rx_ll->rx_backup_ring[0].size, rx_ll->rx_backup_ring[1].size);
	return 0;
}

void rx_ll_deinit(struct rwnx_hw *rwnx_hw)
{
	struct rwnx_rx_ll *rx_ll = &rwnx_hw->rx_ll;
	u8 mac_id;
	struct rx_free_msg_info *free_msg;
	struct rx_rae_ll_host_ring_info *rx_ring, *rx_backup_ring;

	for (mac_id = 0; mac_id < 2; mac_id++) {
		rx_ring = &rx_ll->rx_ring[mac_id];
		rx_backup_ring = &rx_ll->rx_backup_ring[mac_id];
		if (rx_ring->addr != NULL) {
			WQ_DBG(DM_TRBUS, DL_ERR,
			       "[auto]msg: LL RX free consistent DMA memory (%d bytes) for MAC %d\n",
			       (int)rx_ring->size, mac_id);
			dma_free_coherent(rwnx_hw->dev, rx_ring->size,
					  rx_ring->addr, rx_ring->dma);
		}
		if (rx_backup_ring->addr != NULL) {
			WQ_DBG(DM_TRBUS, DL_ERR,
			       "[auto]msg: LL RX free consistent DMA memory (%d bytes) for MAC %d\n",
			       (int)rx_backup_ring->size, mac_id);
			dma_free_coherent(rwnx_hw->dev, rx_backup_ring->size,
					  rx_backup_ring->addr,
					  rx_backup_ring->dma);
		}
		free_msg = &rx_ll->rx_free_msg_env[mac_id];
		del_timer(&free_msg->rx_free_msg_timer);
	}
}

void rx_ll_free_host_data_ring(struct rwnx_hw *rwnx_hw, u32 mac_id,
			       u32 buf_rd_idx, bool use_backup_ring)
{
	struct rwnx_rx_ll *rx_ll;
	struct rx_free_msg_info *free_msg;
	rx_ll = &rwnx_hw->rx_ll;
	free_msg = &rx_ll->rx_free_msg_env[mac_id];

	spin_lock(&rx_ll->rx_free_msg_lock);

	free_msg->macid = mac_id;
	free_msg->send_flag[use_backup_ring] = false;
	free_msg->use_backup_ring = use_backup_ring;

	if (use_backup_ring != free_msg->use_backup_ring_pre &&
	    !free_msg->send_flag[free_msg->use_backup_ring_pre]) {
		WQ_DBG(DM_RX, DL_WRN, "free host data ring(switch):%d/%d:0x%x",
		       mac_id, use_backup_ring, free_msg->read_offset);
		free_msg->send_flag[free_msg->use_backup_ring_pre] = true;
		free_msg->rx_free_idx[free_msg->use_backup_ring_pre] =
			free_msg->read_offset &
			HOST_DATA_RING_WRAP_FLAG_CLEAR_MASK;
		rwnx_send_free_host_ring_req(rwnx_hw, mac_id,
					     free_msg->read_offset,
					     free_msg->use_backup_ring_pre);
	}

	if ((((buf_rd_idx & HOST_DATA_RING_WRAP_FLAG_CLEAR_MASK) -
	      free_msg->rx_free_idx[use_backup_ring]) >=
	     free_msg->rx_free_thrd[use_backup_ring]) ||
	    ((buf_rd_idx & HOST_DATA_RING_WRAP_FLAG_CLEAR_MASK) <
	     free_msg->rx_free_idx[use_backup_ring])) {
		del_timer(&free_msg->rx_free_msg_timer);
		free_msg->send_flag[use_backup_ring] = true;
		// WQ_DBG(DM_RX, DL_WRN, "free host data ring(thrd):%d/%d:0x%x",
		//        mac_id, use_backup_ring, buf_rd_idx);
		free_msg->rx_free_idx[use_backup_ring] =
			buf_rd_idx & HOST_DATA_RING_WRAP_FLAG_CLEAR_MASK;
		rwnx_send_free_host_ring_req(rwnx_hw, mac_id, buf_rd_idx,
					     use_backup_ring);
	} else {
		if (free_msg->fw_recoverying &&
		    free_msg->last_read_offset == buf_rd_idx) {
			free_msg->fw_recoverying = false;
			free_msg->send_flag[use_backup_ring] = true;
			free_msg->rx_free_idx[use_backup_ring] =
				buf_rd_idx &
				HOST_DATA_RING_WRAP_FLAG_CLEAR_MASK;
			del_timer(&free_msg->rx_free_msg_timer);
			WQ_DBG(DM_RX, DL_WRN,
			       "free host data ring(last):%d/%d:0x%x", mac_id,
			       use_backup_ring, buf_rd_idx);
			rwnx_send_free_host_ring_req(
				rwnx_hw, mac_id, buf_rd_idx, use_backup_ring);
		} else {
			mod_timer(&free_msg->rx_free_msg_timer,
				  jiffies + FREE_MSG_TIMEOUT_INTERVAL);
		}
	}
	free_msg->use_backup_ring_pre = free_msg->use_backup_ring;
	free_msg->read_offset = buf_rd_idx;

	spin_unlock(&rx_ll->rx_free_msg_lock);
}

struct sk_buff *rx_ll_copy_per_payload(struct rwnx_hw *rwnx_hw,
				       struct sk_buff *skb,
				       struct rx_ll_ind_param *rx_ll_sub_ind)
{
	struct hw_vect *hwvect;
	struct wq_rx_hdr *sw_rxhdr;
	struct desc_buf_info *desc_buf_info;
	struct sk_buff *new_skb;
	struct ethhdr *eth;
	u32 frame_len;
	u8 *dest_addr;
	u8 *src_addr;
	u16 len;
	u16 offset;
	u8 *ptr;
	u8 ipproto;

	sw_rxhdr = (struct wq_rx_hdr *)skb->data;
	hwvect = &rx_ll_sub_ind->hwvect;
	frame_len = hwvect->frmlen;
	dest_addr = rx_ll_sub_ind->dest_addr;
	src_addr = rx_ll_sub_ind->src_addr;
	offset = sizeof(struct wq_rx_hdr) + sizeof(struct hw_rxhdr);
	len = sizeof(struct wq_rx_hdr) + sizeof(struct hw_rxhdr) +
	      sizeof(struct ethhdr) + frame_len - 8;
	desc_buf_info = (struct desc_buf_info *)&hwvect->desc_buf_info;

	if (sw_rxhdr->mac_id < 2) {
		if (unlikely(sw_rxhdr->use_backup_ring)) {
			ptr = (u8 *)rwnx_hw->rx_ll
				      .rx_backup_ring[sw_rxhdr->mac_id]
				      .addr +
			      desc_buf_info->buf_ring_wr_index * 4;
		} else {
			ptr = (u8 *)rwnx_hw->rx_ll.rx_ring[sw_rxhdr->mac_id]
				      .addr +
			      desc_buf_info->buf_ring_wr_index * 4;
		}
	} else {
		new_skb = NULL;
		WQ_DBG(DM_RX, DL_ERR, "%s: error mac_id:%d\n", __func__,
		       sw_rxhdr->mac_id);
		goto END;
	}
	// LL : alloc a new skb(size depends on rx amsdu length, ~8K), copy Rx desc of skb to new skb

	if (frame_len <= 8) {
		new_skb = NULL;
		dump_bytes(DL_ERR, "[auto]msg:frame_len<8,swhdr",
			   (u8 *)sw_rxhdr, sizeof(struct wq_rx_hdr));
		dump_bytes(DL_ERR, "hwhdr", (u8 *)rx_ll_sub_ind,
			   sizeof(struct rx_ll_ind_param));
		dump_bytes(DL_ERR, "data", ptr, frame_len);
		goto END;
	}

	// defrag procedure
	if (rx_ll_sub_ind->more_frag != 0 || rx_ll_sub_ind->frag_num != 0) {
		new_skb = wq_rx_defrag(rwnx_hw, sw_rxhdr, rx_ll_sub_ind, ptr,
				       frame_len);
		if (!new_skb) {
			WQ_DBG(DM_RX, DL_ERR, "%s: get defrag skb fail\n",
			       __func__);
		}
		goto END;
	} else {
		new_skb = dev_alloc_skb(len);
	}

	if (new_skb == NULL) {
		WQ_DBG(DM_RX, DL_ERR, "%s: can't allocate skb\n", __func__);
		rwnx_send_free_host_ring_req(rwnx_hw, sw_rxhdr->mac_id,
					     sw_rxhdr->buf_rd_idx,
					     sw_rxhdr->use_backup_ring);
		BUG();
	}

	// copy SW/HW header */
	sw_rxhdr->status = rx_ll_sub_ind->status;
	sw_rxhdr->is_ampdu = rx_ll_sub_ind->is_ampdu;
	sw_rxhdr->tid = rx_ll_sub_ind->tid;
	sw_rxhdr->sn = rx_ll_sub_ind->sn;
	memcpy(new_skb->data, sw_rxhdr, sizeof(struct wq_rx_hdr));
	memcpy(new_skb->data + sizeof(struct wq_rx_hdr), rx_ll_sub_ind,
	       sizeof(struct hw_rxhdr));
	// LL TODO P1.3: while copying payload, compute tcp/udp checksum in parallel

	// LL : if (amsdu) copy payload from host data ring
	if (rx_ll_sub_ind->flags_is_amsdu) {
		// copy the payload of AMSDU
		memcpy(new_skb->data + offset, ptr, frame_len);
		// correct the new_skb length
		skb_put(new_skb, sizeof(struct wq_rx_hdr) +
					 sizeof(struct hw_rxhdr) + frame_len);
	} else {
		// LL : else(not an amsdu) copy payload from host data ring, strip llc/snap in the payload
		// LL : copy DA/SA/TYPE to sk_buff
		//  copy DA/SA and assign the protocol */
		eth = (struct ethhdr *)(new_skb->data + offset);
		memcpy(eth->h_dest, dest_addr, ETH_ALEN);
		memcpy(eth->h_source, src_addr, ETH_ALEN);
		memcpy(&eth->h_proto, ptr + 6, 2);
		offset += sizeof(struct ethhdr);

		// copy the payload
		ipproto = *(ptr + 17);
		if (gv_cksum_offload && *(ptr + 8) == 0x45 &&
		    (ipproto == IPPROTO_UDP || ipproto == IPPROTO_TCP)) {
			uint8_t *iph = ptr + 8;
			uint8_t *new_data = new_skb->data + offset;
			uint16_t cksum, allcksum,
				total_len = ntohs(*((uint16_t *)(ptr + 10)));
			uint32_t cpy_len = frame_len - 8;

			//IPHDR with NOIP copy
			memcpy(new_data, iph, IPHDR_NOIP_LEN);

			//compute sum from src/dst ip to end of payload
			cksum = memcpy_cksum(new_data + IPHDR_NOIP_LEN,
					     iph + IPHDR_NOIP_LEN,
					     total_len - IPHDR_NOIP_LEN);
			allcksum = ntohs(cksum) + ipproto +
				   (total_len - IPHDR_LEN);
			//WQ_DBG(DM_RX, DL_ERR, "%s:(%x)cksum=%x | %x\n", __func__,  ipproto, ntohs(cksum), allcksum);
			//dump_bytes(DL_ERR, "new_data", new_data, cpy_len-FCS_LEN);
			if (allcksum != 0xFFFF) {
				WQ_DBG(DM_RX, DL_ERR,
				       "%s:(%x)cksum=%x | %x len=%x ERROR CKSUM\n",
				       __func__, ipproto, ntohs(cksum),
				       allcksum, (total_len - IPHDR_LEN));
				dump_bytes(DL_ERR, "new_data", new_data,
					   cpy_len);
				//todo : drop pkt
			} else {
				new_skb->ip_summed = CHECKSUM_UNNECESSARY;
			}
		} else {
			// copy the payload
			memcpy(new_skb->data + offset, ptr + 8, frame_len - 8);
		}
		skb_put(new_skb, len);
	}

END:
	return new_skb;
}

/**
 * rwnx_rxdataind_ll - Process rx buffer for low latency/cut-through
 *
 * @rwnx_hw: Pointer to the object attached to the IPC structure
 *         (points to struct rwnx_hw is this case)
 * @hostid: Address of the RX descriptor
 *
 * This function is called for each buffer received by the fw
 *
 * +---------------+
 * |sw_rxhdr       |
 * +---------------+
 * |rx_ll_payload  |
 * +---------------+
 */
u8 rwnx_rxdataind_ll(struct rwnx_hw *rwnx_hw, struct sk_buff *skb)
{
	struct wq_rx_hdr *sw_rxhdr;
	struct hw_rxhdr *hw_rxhdr;
	struct rxdesc_tag_wq *rxdesc;
	struct rwnx_vif *rwnx_vif;
	int msdu_offset = sizeof(struct hw_rxhdr);
	u16 status;
	struct rx_ll_payload *rx_ll_pld;
	struct rx_ll_ind_param *rx_ll_sub_ind;
	u8 aggre_ind_num = 0;
	struct sk_buff *skb_agg[RX_MAX_AGG_RX_IND_NUM];
	u8 i = 0;

	atomic_inc(&rwnx_hw->ipc_rx_pkt_cnt);
	if (!skb->len) {
		// LL TODO: does LL firmware report credit via zero-length packet? add debug print here to double check
		/* free zero length rx packet used for tx credit report */
		dev_kfree_skb(skb);
		return 0;
	}

	PROFILING_SET(SW_PROF_RX_DATA_IND);

	// LL TODO P1.3: register Rx tcp/udp checksum offload support to kernel

	// LL TODO P3: pull snap/llc and push DA/DA to payload in host data ring (TBD could hw reserve space between payloads)
	// LL TODO P3: alloc sk_buff descriptor, link to payload in host data ring
	// LL TODO P3: setup sk_buff destructor, send rx free msg to firmware to free the buffer
	// LL TODO P3 optimization: reorder rx free msg, and aggregate them to reduce overhead

	sw_rxhdr = (struct wq_rx_hdr *)skb->data;

	/* allocate a new one and copy the whole data including SW/HW header */
	if (likely(sw_rxhdr->rx_ll_payload_present)) {
		rx_ll_pld = (struct rx_ll_payload *)(skb->data +
						     sizeof(struct wq_rx_hdr));
		aggre_ind_num = rx_ll_pld->aggre_num;

		if (aggre_ind_num > RX_MAX_AGG_RX_IND_NUM) {
			WQ_DBG(DM_RX, DL_WRN, "[auto]WARN: %s skb_agg num %d limited to %d !\n", __func__, aggre_ind_num, RX_MAX_AGG_RX_IND_NUM);
			aggre_ind_num = RX_MAX_AGG_RX_IND_NUM;
		}

		for (i = 0; i < aggre_ind_num; i++) {
			rx_ll_sub_ind =
				(struct rx_ll_ind_param
					 *)((u8 *)rx_ll_pld +
					    sizeof(rx_ll_pld->aggre_num) +
					    sizeof(struct rx_ll_ind_param) * i);
			skb_agg[i] = rx_ll_copy_per_payload(rwnx_hw, skb,
							    rx_ll_sub_ind);

			if (!skb_agg[i]) {
				WQ_DBG(DM_RX, DL_ERR,
				       "%s, skb_agg[%d] is NULL\n", __func__,
				       i);
				continue;
			}
		}
		rx_ll_free_host_data_ring(rwnx_hw, sw_rxhdr->mac_id,
					  sw_rxhdr->buf_rd_idx,
					  sw_rxhdr->use_backup_ring);
		dev_kfree_skb(skb);
	} else {
		skb_agg[0] = skb;
		aggre_ind_num = 1;
	}

	for (i = 0; i < aggre_ind_num; i++) {
		if (!skb_agg[i])
			continue;

		skb = skb_agg[i];
		rxdesc = (struct rxdesc_tag_wq *)skb->data;
		sw_rxhdr = &rxdesc->sw_rxhdr;
		status = sw_rxhdr->status;

		//defrag procedure use RX_STAT_LEN_UPDATE befor rcv last frag
		if (status & RX_STAT_LEN_UPDATE) {
			WQ_DBG(DM_RX, DL_INF,
			       "%s, RX_STAT_LEN_UPDATE, do nothing!!\n",
			       __func__);
			goto end;
		}

		skb_pull(skb, sizeof(struct wq_rx_hdr));
		hw_rxhdr = (struct hw_rxhdr *)skb->data;
		WQ_DBG(DM_RX, DL_INF,
		       "%s: aggre total num: %d, cur num: %d, skb=0x%p, status: 0x%x, is_ampdu: %u, tid: %u, sn:%u, sta_idx: %u, is_80211_mpdu: %u\n",
		       __func__, aggre_ind_num, i, skb, sw_rxhdr->status,
		       sw_rxhdr->is_ampdu, sw_rxhdr->tid, sw_rxhdr->sn,
		       hw_rxhdr->flags_sta_idx, hw_rxhdr->flags_is_80211_mpdu);

// LL: to handle defrag with MIC error.
/* Now firmware processing defrag will not set RX_STAT_DELETE, remove the code below. In the future, defrag may be done in host. */
#if 0
        /* Check if we need to delete the buffer */
        if (status & RX_STAT_DELETE) {
            WQ_DBG(DM_RX, DL_INF, "%s, RX_STAT_DELETE !!\n", __func__);
            /* Free the buffer */
            dev_kfree_skb(skb);
            goto end;
        }
#endif

		// LL TODO optmization: do not support monitor mode for now, remove the code below
		/* Check if we need to forward the buffer coming from a monitor interface */
		if (status & RX_STAT_MONITOR) {
			struct sk_buff *skb_monitor;
			struct hw_rxhdr hw_rxhdr_copy;
			u8 rtap_len;
			u16 frm_len;

			WQ_DBG(DM_RX, DL_INF, "%s, RX_STAT_MONITOR !!\n",
			       __func__);

			// Check if monitor interface exists and is open
			rwnx_vif =
				rwnx_rx_get_vif(rwnx_hw, rwnx_hw->monitor_vif);
			if (!rwnx_vif) {
				dev_err(rwnx_hw->dev,
					"Received monitor frame but there is no monitor interface open\n");
				goto check_len_update;
			}

			hw_rxhdr = (struct hw_rxhdr *)skb->data;
			rwnx_rx_vector_convert(rwnx_hw->machw_type,
					       &hw_rxhdr->hwvect.rx_vec_1,
					       &hw_rxhdr->hwvect.rx_vec_2);
			rtap_len = rwnx_rx_rtap_hdrlen(
				&hw_rxhdr->hwvect.rx_vec_1, false);

			// Move skb->data pointer to MAC Header or Ethernet header
			skb->data += msdu_offset;

			// Save frame length
			frm_len = le32_to_cpu(hw_rxhdr->hwvect.frmlen);

			// Reserve space for frame
			skb->len = frm_len;

			if (status == RX_STAT_MONITOR) {
				// Check if there is enough space to add the radiotap header
				if (skb_headroom(skb) > rtap_len) {
					skb_monitor = skb;

					// Duplicate the HW Rx Header to override with the radiotap header
					memcpy(&hw_rxhdr_copy, hw_rxhdr,
					       sizeof(hw_rxhdr_copy));

					hw_rxhdr = &hw_rxhdr_copy;
				} else {
					// Duplicate the skb and extend the headroom
					skb_monitor = skb_copy_expand(
						skb, rtap_len, 0, GFP_ATOMIC);

					// Reset original skb->data pointer
					skb->data = (void *)hw_rxhdr;
				}
			} else {
#ifdef CONFIG_RWNX_MON_DATA
				// Check if MSDU
				if (!hw_rxhdr->flags_is_80211_mpdu) {
					// MSDU
					// Extract MAC header
					u16 machdr_len =
						hw_rxhdr->mac_hdr_backup.buf_len;
					u8 *machdr_ptr =
						hw_rxhdr->mac_hdr_backup.buffer;

					// Pull Ethernet header from skb
					skb_pull(skb, sizeof(struct ethhdr));

					// Copy skb and extend for adding the radiotap header and the MAC header
					skb_monitor = skb_copy_expand(
						skb, rtap_len + machdr_len, 0,
						GFP_ATOMIC);

					// Reserve space for the MAC Header
					skb_push(skb_monitor, machdr_len);

					// Copy MAC Header
					memcpy(skb_monitor->data, machdr_ptr,
					       machdr_len);

					// Update frame length
					frm_len += machdr_len -
						   sizeof(struct ethhdr);
				} else {
					// MPDU
					skb_monitor = skb_copy_expand(
						skb, rtap_len, 0, GFP_ATOMIC);
				}

				// Reset original skb->data pointer
				skb->data = (void *)hw_rxhdr;
#else
				// Reset original skb->data pointer
				skb->data = (void *)hw_rxhdr;

				wiphy_err(
					rwnx_hw->wiphy,
					"RX status %d is invalid when MON_DATA is disabled\n",
					status);
				goto check_len_update;
#endif
			}

			skb_reset_tail_pointer(skb);
			skb->len = 0;
			skb_reset_tail_pointer(skb_monitor);
			skb_monitor->len = 0;

			skb_put(skb_monitor, frm_len);
			if (rwnx_rx_monitor(rwnx_hw, rwnx_vif, skb_monitor,
					    hw_rxhdr, rtap_len))
				dev_kfree_skb(skb_monitor);

			if (status == RX_STAT_MONITOR) {
				status |= RX_STAT_ALLOC;
				if (skb_monitor != skb) {
					dev_kfree_skb(skb);
				}
			}
		}

	// LL TODO optmization: do not support monitor mode for now, remove the code below
	check_len_update:

// LL: redundant, remove the code below
#if 0
        /* Check if we need to update the length */
        if (status & RX_STAT_LEN_UPDATE) {
            WQ_DBG(DM_RX, DL_INF, "%s, RX_STAT_LEN_UPDATE !!\n", __func__);
            hw_rxhdr = (struct hw_rxhdr *)skb->data;
            hw_rxhdr->hwvect.len = frame_len;

            if (status & RX_STAT_ETH_LEN_UPDATE) {
                /* Update Length Field inside the Ethernet Header */
                struct ethhdr *hdr = (struct ethhdr *)((u8 *)hw_rxhdr + msdu_offset);
                hdr->h_proto = htons(frame_len - sizeof(struct ethhdr));
            }

            dev_kfree_skb(skb);
            goto end;
        }
#endif

		// LL: to handle class2/3 frame from unknown STA(neither authenticated nor associated) in AP mode
		/* Check if it must be discarded after informing upper layer */
		if (status & RX_STAT_SPURIOUS) {
			struct ieee80211_hdr *hdr;
			WQ_DBG(DM_RX, DL_INF, "%s, RX_STAT_SPURIOUS !!\n",
			       __func__);
			hw_rxhdr = (struct hw_rxhdr *)skb->data;
			hdr = (struct ieee80211_hdr *)(skb->data + msdu_offset);
			rwnx_vif = rwnx_rx_get_vif(rwnx_hw,
						   hw_rxhdr->flags_vif_idx);
			if (rwnx_vif) {
				cfg80211_rx_spurious_frame(
					rwnx_vif->ndev, hdr->addr2, GFP_ATOMIC);
			}

			dev_kfree_skb(skb);
			goto end;
		}

		/* Check if we need to forward the buffer */
		if (status & RX_STAT_FORWARD) {
			struct rwnx_sta *sta = NULL;
			hw_rxhdr = (struct hw_rxhdr *)skb->data;
			// machw_type is definitely RWNX_MACHW_HE, skip rx_vector_convert
			// rwnx_rx_vector_convert(rwnx_hw->machw_type, &hw_rxhdr->hwvect.rx_vec_1, &hw_rxhdr->hwvect.rx_vec_2);

			skb_pull(skb, msdu_offset);

			WQ_DBG(DM_RX, DL_INF,
			       "RX_STAT_FORWARD: flags_sta_idx: %d, flags_is_80211_mpdu: %d, sw_rxhdr->is_ampdu=%d, skb=0x%p\n",
			       hw_rxhdr->flags_sta_idx,
			       hw_rxhdr->flags_is_80211_mpdu,
			       sw_rxhdr->is_ampdu, skb);

			if (hw_rxhdr->flags_sta_idx != RWNX_INVALID_STA) {
				if (hw_rxhdr->flags_sta_idx <
				    NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX) {
					sta = &rwnx_hw->sta_table
						       [hw_rxhdr->flags_sta_idx];
					rwnx_rx_statistic(rwnx_hw, hw_rxhdr,
							  sta);
				} else {
					WQ_DBG(DM_RX, DL_ERR,
					       "[auto]msg: invalid Rx flags_sta_idx=%u",
					       hw_rxhdr->flags_sta_idx);
				}
			}

			if (hw_rxhdr->flags_is_80211_mpdu) {
				// LL: mgmt frame will come to here
				struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;

				if(ieee80211_is_ctl(mgmt->frame_control)) {
					rwnx_rx_cntrl_any(rwnx_hw, skb, hw_rxhdr);
				} else {
					rwnx_rx_mgmt_any(rwnx_hw, skb, hw_rxhdr);
				}
			} else {
				rwnx_vif = rwnx_rx_get_vif(
					rwnx_hw, hw_rxhdr->flags_vif_idx);

				if (!rwnx_vif) {
					dev_err(rwnx_hw->dev,
						"Frame received but no active vif (%d)",
						hw_rxhdr->flags_vif_idx);
					dev_kfree_skb(skb);
					goto end;
				}

				if (sta) {
					if (sta->vlan_idx !=
					    rwnx_vif->vif_index) {
						rwnx_vif =
							rwnx_hw->vif_table
								[sta->vlan_idx];
						if (!rwnx_vif) {
							dev_kfree_skb(skb);
							goto end;
						}
					}

					if (hw_rxhdr->flags_is_4addr &&
					    !rwnx_vif->use_4addr) {
						//Simply drops the 4 address Rx frame, cfg80211 does nothing but generates kernel warning
						//cfg80211_rx_unexpected_4addr_frame(rwnx_vif->ndev,
						//                                   sta->mac_addr, GFP_ATOMIC);
						WQ_DBG(DM_RX, DL_ERR,
						       "[auto]msg: unexpected_4addr_frame");
						dev_kfree_skb(skb);
						goto end;
					}
				}

				skb->priority = 256 + hw_rxhdr->flags_user_prio;

				// LL TODO: once reorder is done in firmware, skip Rx reorder
				if (sw_rxhdr->is_ampdu &&
				    ieee80211_ampdu_reorder(
					    rwnx_hw, hw_rxhdr->flags_sta_idx,
					    sw_rxhdr->tid, sw_rxhdr->sn,
					    sw_rxhdr->msdu_seq,
					    sw_rxhdr->msdu_seq_end, skb)) {
					goto end;
				}

				rwnx_rx_data_skb(rwnx_hw, rwnx_vif, skb, hw_rxhdr);
			}
		}

		if (!(status & (RX_STAT_FORWARD | RX_STAT_SPURIOUS | RX_STAT_MONITOR))) {
			WQ_DBG(DM_RX, DL_ERR, "[auto]ERROR: %s Invalid data frame, status 0x%x !\n", __func__, status);
			dev_kfree_skb(skb);
		}

	end:
		PROFILING_CLR(SW_PROF_RX_DATA_IND);
	}

	return 0;
}
