/**
 ******************************************************************************
 *
 * @file usb_bundle.h
 *
 * Copyright (C) wuqi
 *
 ******************************************************************************
 */
#ifndef _WQ_USB_BUNDLE_H
#define _WQ_USB_BUNDLE_H

#include <linux/skbuff.h>
#include "core.h"

int wq_usb_rx_get_pktnum(struct sk_buff *skb, u32 utf_len);
void wq_usb_rx_debundle(struct wq_core *core, enum wq_hif_qid qid,
			struct sk_buff *skb, u8 pkt_num);

#endif /* _WQ_USB_BUNDLE_H */
