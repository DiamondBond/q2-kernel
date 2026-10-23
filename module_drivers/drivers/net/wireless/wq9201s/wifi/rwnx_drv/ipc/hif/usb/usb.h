#ifndef _WQ_USB_H
#define _WQ_USB_H

#include <linux/usb.h>
#include <linux/netdevice.h>

#include "fw_api/non_wifi/hif/usb/api.h"

#include "hif_api.h"
#include "wq_fw.h"
#include "utils.h"

#define WQ_MSGIN_URB_NUM 150
#define WQ_MSGOUT_URB_NUM 3
#define WQ_PKTIN_URB_NUM 15
#define WQ_PKTOUT_URB_NUM 100

#define THRESHOLD_USB_OUT_BUNDLE_MAX 1
#define USB_OUT_PKT_LEN 2048

/* stop netdev queue when number of queued buffers if greater than this  */
#define RWNX_NDEV_FLOW_CTRL_USB_STOP 600
/* restart netdev queue when number of queued buffers is lower than this */
#define RWNX_NDEV_FLOW_CTRL_USB_RESTART 200

//if the number of URBs used exceeds the WQ_PKTOUT_CONGEST_THRESHOLD, it means that the USB Bus is busy
#define WQ_PKTOUT_CONGEST_THRESHOLD 80

/* CMDs: Host sync with Controller */
#define WQ_VREQ_ID_FW_DTOP_DL_ADDR 0xA4
#define WQ_VREQ_ID_FW_DTOP_STARTPC 0xA5
#define WQ_VREQ_ID_FW_DTOP_DL 0xAA //DTOP firmware download
#define WQ_VREQ_ID_FW_DTOP_DL_COMP 0xAB //DTOP firmware download complete
#define WQ_VREQ_ID_FW_WIFI_REMOVE   0xB1    //wifi sys shutdown
#define WQ_VREQ_ID_FW_BT_REMOVE     0xB2    //bt sys shutdown
#define WQ_VREQ_ID_GET_RUNSYS 0xBD //device chip sys state
#define WQ_VREQ_ID_GET_ROM_VER 0xCA //get rom version
#define WQ_VREQ_ID_SET_SOC_RESET 0xDA //set chip reset to bootrom

/****************************
 * USB module structure
 ****************************/
struct wq_urb_ctx {
	struct list_head list;
	struct wq_usb *wq_usb;
	struct sk_buff *skb;
	struct urb *urb;
};

struct wq_usb_trigger {
	u8 cnt;

	u8 type; /* trigger type */
	u16 info; /* trigger info (pattern) */

	struct work_struct work;
};

struct wq_usb {
	struct wq_core core; /* NB: keep it at the beginning */

	struct usb_device *usbdev;
	struct usb_interface *intf;

	struct {
		struct wq_list_pool msgin;
		struct wq_list_pool msgout;
		struct wq_list_pool pktin;
		struct wq_list_pool pktout;
	} pools;

	struct {
		struct usb_anchor msgout;
	} anchors;

	struct wq_usb_trigger trigger;

	struct notifier_block nb;

	struct completion resume_wait;
	ktime_t cmd_start_time;

	int dtop_inited;
	struct usb_anchor dtop_rx_anchor;
	struct sk_buff *ep9_skb;
	spinlock_t usb_out_info_lock;
	int usb_out_times;
	int usb_out_pkt_count;

	struct {
		spinlock_t lock;
		bool woken;
		wait_queue_head_t wait_q;
	} bmi;
};

static inline void wq_urb_ctx_enqueue(struct wq_list_head *q,
				      struct wq_urb_ctx *ctx)
{
	/* FIXME: wq_dbg_update_skb_stats_all() */
	WQ_LIST_PUSH(q, struct wq_urb_ctx, list, ctx);
}

static inline struct wq_urb_ctx *wq_urb_ctx_dequeue(struct wq_list_head *q)
{
	return WQ_LIST_POP(q, struct wq_urb_ctx, list);
}

static inline struct wq_urb_ctx *wq_urb_ctx_alloc(struct wq_list_pool *pool)
{
	struct wq_urb_ctx *ctx;

	ctx = wq_urb_ctx_dequeue(&pool->list);
	if (ctx)
		return ctx;

	return NULL;
}

static inline void wq_urb_ctx_free(struct wq_list_pool *pool,
				   struct wq_urb_ctx *ctx)
{
	struct sk_buff *skb = ctx->skb;

	if (skb) {
		ctx->skb = NULL;
		WARN_ON(skb->next);
		dev_kfree_skb_any(skb);
	}
	wq_urb_ctx_enqueue(&pool->list, ctx);
}

int wq_usb_pools_init(struct wq_usb *wq_usb);
void wq_usb_pools_deinit(struct wq_usb *wq_usb);

int wq_usb_bmi_exchange(struct wq_core *core, void *req, u32 req_len, void *rsp,
			u32 rsp_len, int timeout);

int wq_usb_bmi_xfer(struct wq_core *core, enum wq_bmi_xfer_type type,
		    const u8 *data, int len, int timeout);

int wq_usb_bmi_cmd(struct wq_core *core, u8 cmd, const void *param, u16 p_size,
		   void *resp, u16 r_size, int timeout);

#ifdef CONFIG_WQ_DTOP
int wq_usb_app_init(struct wq_usb *wq_usb);
void wq_usb_app_deinit(struct wq_usb *wq_usb);

int wq_usb_dtop_send(struct wq_core *core, u8 *data, u32 size, u32 timeout);
int wq_usb_ep9_msg_in(struct wq_usb *wq_usb, struct urb *urb);
#endif

#endif /* _WQ_USB_H */
