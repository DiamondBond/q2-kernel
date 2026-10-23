/*
 * Copyright (c) 2023 WuQi Technologies.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _WQ_WLAN_HIF_H_
#define _WQ_WLAN_HIF_H_

#include <linux/skbuff.h>

#include "core.h"
#include "fw_api/non_wifi/hif/api.h"

/*
 * different hif have different hif hdr len
 * people may don't need hif hdr, size = 0 for some interface
 */
#define hif_get_hdr_sz(core) core->hif_ops->hif_get_hdr_sz(core)

enum wq_bmi_xfer_type {
	WQ_FW_DTOP_DL = 0,
	WQ_FW_WIFI_DL,
	WQ_FW_BT_DL,
	WQ_FW_WIFI_PHY_DL
};

enum wq_usb_trigger_type {
	WQ_USB_TRI_FW_DL = 1,
	WQ_USB_TRI_CMD_TIMEOUT,
	WQ_USB_TRI_EVENT,
	WQ_USB_TRI_TXPKT_CHK,
	WQ_USB_TRI_MAX
};

struct wq_hif_ops {
	enum wq_hif_type hif;
	/* pcie speed is 5G or 2.5G */
	bool low_speed_mode;
	/* restart or stop netdev queue when number of queued buffers if greater than this  */
	u16 txq_stop_threshlod;
	u16 txq_restart_threshlod;
	/*
	 * Auto-PM (power management) APIs
	 *
	 * call autopm_get() to force device awake, i.e. wait for response after request sent.
	 * autopm_put() should be called after get response or timeout, it allows device suspend.
	 */
	void (*autopm_enable)(struct wq_core *core, int enable);
	int (*autopm_get)(struct wq_core *core);
	void (*autopm_put)(struct wq_core *core);
	int (*autopm_get_async)(struct wq_core *core);
	void (*autopm_put_async)(struct wq_core *core);
	void (*autopm_allow)(struct wq_core *core);
	bool (*autopm_is_bus_active)(struct wq_core *core);

	/* TX/RX APIs */
	int (*hif_tx)(struct wq_core *core, enum wq_hif_qid qid,
		      struct sk_buff_head *skbq);
	void (*htc_tx_done)(struct wq_core *core, struct sk_buff *skb,
			    int status);
	void (*htc_txq_done)(struct wq_core *core, struct sk_buff_head *skbq,
			     int status);
	void (*htc_ll_msdu_tx_done)(struct wq_core *core, struct sk_buff *skb,
				    int status);
	void (*htc_rx)(struct wq_core *core, enum wq_hif_qid qid,
		       struct sk_buff *skb);
	void (*htc_rxq)(struct wq_core *core, struct sk_buff_head *skbq);
	/* htc call netif_receive_skb directly in workqueue or kthread env */
	void (*htc_rxq_internal)(struct wq_core *core,
				 struct sk_buff_head *skbq);
	bool (*htc_pending_req_empty)(struct wq_core *core);
	void (*htc_retrigger_tx_task)(struct wq_core *core);
	int (*hif_get_hdr_sz)(struct wq_core *core);
	void (*htc_tx_skb_dma_unmap)(struct wq_core *core, struct sk_buff *skb);

	/* debug API */
	void (*dump_info)(struct wq_core *core);
	/* send special data pattern to trigger bus spy */
	void (*send_trigger)(struct wq_core *core,
			     enum wq_usb_trigger_type type, u16 info);

	/* BMI APIs */
	int (*bmi_exchange)(struct wq_core *core, void *req, u32 req_len,
			    void *rsp, u32 rsp_len_max, int timeout);

	int (*bmi_cmd)(struct wq_core *core, u8 cmd, const void *param,
		       u16 p_size, void *resp, u16 r_size, int timeout);
	int (*bmi_xfer)(struct wq_core *core, enum wq_bmi_xfer_type type,
			const u8 *data, int len, int timeout);

	/* DTOP APIs */
	int (*dtop_bulk_send)(struct wq_core *core, u8 *data, u32 size,
			      u32 timeout);
	/* txq ring */
	void (*htc_txq_ring_txdone)(struct wq_core *core);
	void (*hif_txq_ring_2task)(struct wq_core *core);
	void (*hif_txq_ring_timerstart)(struct wq_core *core);
	bool (*hif_bus_dead)(struct wq_core *core);
};

/* Auto-PM APIs */
static inline void hif_autopm_enable(struct wq_core *core, int enable)
{
	if (core->hif_ops->autopm_enable)
		core->hif_ops->autopm_enable(core, enable);
}

static inline int hif_autopm_get(struct wq_core *core)
{
	/* FIXME: trace the caller for debug purpose */
	if (core->hif_ops->autopm_get)
		return core->hif_ops->autopm_get(core);
	return 0; /* unsupported feature. return 0 to avoid raising any warning */
}

static inline void hif_autopm_put(struct wq_core *core)
{
	if (core->hif_ops->autopm_put)
		core->hif_ops->autopm_put(core);
}

static inline int hif_autopm_get_async(struct wq_core *core)
{
	if (core->hif_ops->autopm_get_async)
		return core->hif_ops->autopm_get_async(core);
	return 0;
}

static inline void hif_autopm_put_async(struct wq_core *core)
{
	if (core->hif_ops->autopm_put_async)
		return core->hif_ops->autopm_put_async(core);
}

static inline void hif_autopm_allow(struct wq_core *core)
{
	if (core->hif_ops->autopm_allow)
		return core->hif_ops->autopm_allow(core);
}

static inline bool hif_autopm_is_bus_active(struct wq_core *core)
{
	if (core->hif_ops->autopm_is_bus_active)
		return core->hif_ops->autopm_is_bus_active(core);
	return true;
}

/* TX/RX APIs */
static inline void htc_tx_done(struct wq_core *core, struct sk_buff *skb,
			       int status)
{
	core->hif_ops->htc_tx_done(core, skb, status);
}

static inline void htc_txq_done(struct wq_core *core, struct sk_buff_head *skbq,
				int status)
{
	core->hif_ops->htc_txq_done(core, skbq, status);
}

static inline void htc_ll_msdu_tx_done(struct wq_core *core,
				       struct sk_buff *skb, int status)
{
	core->hif_ops->htc_ll_msdu_tx_done(core, skb, status);
}

static inline void htc_rx(struct wq_core *core, enum wq_hif_qid qid,
			  struct sk_buff *skb)
{
	core->hif_ops->htc_rx(core, qid, skb);
}

static inline void htc_rxq(struct wq_core *core, struct sk_buff_head *skbq)
{
	core->hif_ops->htc_rxq(core, skbq);
}

static inline void htc_rxq_internal(struct wq_core *core,
				    struct sk_buff_head *skbq)
{
	core->hif_ops->htc_rxq_internal(core, skbq);
}

static inline bool htc_pending_req_empty(struct wq_core *core)
{
	return core->hif_ops->htc_pending_req_empty(core);
}
static inline void htc_retrigger_tx_task(struct wq_core *core)
{
	core->hif_ops->htc_retrigger_tx_task(core);
}

/* debug APIs */
static inline void hif_dump_info(struct wq_core *core)
{
	if (core->hif_ops->dump_info)
		core->hif_ops->dump_info(core);
}

static inline void hif_send_trigger(struct wq_core *core,
				    enum wq_usb_trigger_type type, u16 info)
{
	if (core->hif_ops->send_trigger)
		core->hif_ops->send_trigger(core, type, info);
}

static inline int wq_hif_bmi_exchange(struct wq_core *core, void *req,
				      u32 req_len, void *rsp, u32 rsp_len_max,
				      int timeout)
{
	return core->hif_ops->bmi_exchange(core, req, req_len, rsp, rsp_len_max,
					   timeout);
}

static inline void htc_txq_ring_txdone(struct wq_core *core)
{
	core->hif_ops->htc_txq_ring_txdone(core);
}

static inline void htc_tx_skb_dma_unmap(struct wq_core *core,
					struct sk_buff *skb)
{
	if (core->hif_ops->htc_tx_skb_dma_unmap)
		core->hif_ops->htc_tx_skb_dma_unmap(core, skb);
}

static inline bool hif_bus_dead(struct wq_core *core)
{
	if (core->hif_ops->hif_bus_dead)
		return core->hif_ops->hif_bus_dead(core);
	return false;
}

#endif /* _WQ_WLAN_HIF_H_ */
