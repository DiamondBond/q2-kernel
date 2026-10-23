#define WQ_LOG_DM DM_TRBUS

#include <linux/string.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/swab.h>

#include "wq_log.h"

#include "usb.h"
#include "bmi_core.h"

#include "fw_log.h"

#define USB_MSG_TIMEOUT 200
#define ALLOC_BUF_SIZE 2048

extern void dtop_deinit(struct wq_core *core);
extern void dtop_init(struct wq_core *core);

static int usb_dtop_send_bulk(struct wq_usb *wq_usb, u8 *data, u32 size, u8 ep,
			      u32 timeout)
{
	int ret;
	int actual_length;

	if (data == NULL || wq_usb == NULL) {
		return -EINVAL;
	}

	ret = usb_bulk_msg(wq_usb->usbdev, //dev
			   usb_sndbulkpipe(wq_usb->usbdev, ep), //pipe
			   (u8 *)data, //data
			   size, //len
			   &actual_length, //actual length
			   timeout); //timeout

	return ret;
}

int wq_usb_dtop_send(struct wq_core *core, u8 *data, u32 size, u32 timeout)
{
	int ret;
	struct wq_usb *wq_usb = container_of(core, struct wq_usb, core);

	if (data == NULL || wq_usb == NULL) {
		return -EINVAL;
	}

	ret = usb_dtop_send_bulk(wq_usb, data, size, WQ_USB_EP_DTOP_DL,
				 timeout);

	return ret;
}

/**
 *  @brief   Callback function for Bulk IN URB (DBGLOG Data)
 *
 *  @param urb	 A Pointer to urb structure
 *
 *  @return		0 for success else fail
 */
void wq_usb_ep9_msg_in_cb(struct urb *urb)
{
	struct sk_buff *skb;
	struct wq_usb *wq_usb = (struct wq_usb *)urb->context;

	WQ_DBG(DM_TRBUS, DL_INF,
	       "Bulk RX complete: urb %p status %d count %d.\n", urb,
	       urb->status, urb->actual_length);

	if (urb->status == -ENOENT) {
		/* Avoid suspend failed when usb_kill_urb */
		return;
	}

	if (urb->status == 0 && urb->actual_length) {
		skb = wq_usb->ep9_skb;
		wq_usb->ep9_skb = NULL;

		/* For printk & coredump process */
		skb_put(skb, urb->actual_length);
		skb_queue_tail(&wq_usb->core.driver.main_rx_Q, skb);
		wake_up_interruptible(&wq_usb->core.driver.main_waitQ);

		/* new urb */
		urb = usb_alloc_urb(0, GFP_ATOMIC);
		if (urb == NULL)
			return;

		wq_usb_ep9_msg_in(wq_usb, urb);
	}
}

int wq_usb_ep9_msg_in(struct wq_usb *wq_usb, struct urb *urb)
{
	struct sk_buff *skb;
	int ret;
	struct usb_device *usbdev = wq_usb->usbdev;

	if (usbdev == NULL)
		return -ENODEV;

	skb = dev_alloc_skb(ALLOC_BUF_SIZE);
	if (!skb) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	wq_usb->ep9_skb = skb;
	usb_fill_bulk_urb(urb, wq_usb->usbdev,
			  usb_rcvbulkpipe(wq_usb->usbdev, WQ_USB_EP_DBG_LOG),
			  skb->data, skb_tailroom(skb), wq_usb_ep9_msg_in_cb,
			  wq_usb);

	usb_mark_last_busy(usbdev);
	usb_anchor_urb(urb, &wq_usb->dtop_rx_anchor);

	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret < 0) {
		WQ_DBG(DM_TRBUS, DL_ERR, "bulk urb %p submission failed (%d)",
		       urb, ret);
		usb_unanchor_urb(urb);
	}
	usb_free_urb(urb);

	return ret;
}

void wq_usb_app_deinit(struct wq_usb *wq_usb)
{
	dtop_deinit(&wq_usb->core);

	if (wq_usb->ep9_skb)
		dev_kfree_skb_any(wq_usb->ep9_skb);

	usb_kill_anchored_urbs(&wq_usb->dtop_rx_anchor);
}

int wq_usb_app_init(struct wq_usb *wq_usb)
{
	// struct urb *urb;
	struct wq_core *core = &wq_usb->core;

	WQ_DBG(DM_TRBUS, DL_INF, "[%s] init\n", __func__);

	if (wq_usb == NULL) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s:wq_usb is invaild\n", __func__);
		return (-ENODEV);
	}

	init_usb_anchor(&wq_usb->dtop_rx_anchor);

	/** Initialize the wait queue */
	init_waitqueue_head(&core->driver.main_waitQ);
	skb_queue_head_init(&core->driver.main_rx_Q);

	// urb = usb_alloc_urb(0, GFP_ATOMIC);

	// if (urb == NULL)
	// 	return -ENOMEM;

	///* Register the urb for rx. */
	//if (wq_usb_ep9_msg_in(wq_usb,urb) < 0) {
	//	WQ_DBG(DM_TRBUS, DL_ERR, "%s:Failed to register ep9 urb!\n",__func__);
	//	return -ENOMEM;
	//}
	(void)wq_usb_ep9_msg_in;

	dtop_init(core);

	return 0;
}
