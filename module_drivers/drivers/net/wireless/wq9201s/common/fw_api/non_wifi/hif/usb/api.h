#ifndef WQ_FW_HIF_USB_API_H_
#define WQ_FW_HIF_USB_API_H_

#include "fw_api/non_wifi/hif/api.h"

/*
 * NB: the following TX/RX is in host's view
 */

/* Data Path */
#define WQ_USB_EP_RX_DATA	1	/* device -> host */
#define WQ_USB_EP_TX_BE		1	/* host -> device */
#define WQ_USB_EP_TX_BK		3	/* host -> device */
#define WQ_USB_EP_TX_VI		4	/* host -> device */
#define WQ_USB_EP_TX_VO		5	/* host -> device */

#define WQ_USB_MTU_PKT		1664

/* Maximum number of subpkt in bundle supported in reception */
#define WQ_USB_MAX_BUNDLE_I			8
/* Maximum size of A-MSDU supported in reception */
#define WQ_USB_MAX_AMSDU_RX			8192
/* Maximum size of bundle supported in reception */
 #define WQ_USB_MTU_PKT_BUNDLE_I	WQ_USB_MAX_AMSDU_RX * WQ_USB_MAX_BUNDLE_I

/* Control Path (message) */
#define WQ_USB_EP_BI_MSG	2	/* bi-direction */

#define WQ_USB_MTU_RX_MSG	1087
#define WQ_USB_MTU_TX_MSG	1024

/* BMI v0 */
#define WQ_USB_EP_FW_DL		7	/* host -> device */
#define WQ_USB_EP_BT_DL		8	/* host -> device */
#define WQ_USB_EP_DTOP_DL	9	/* host -> device */
#define WQ_USB_EP_DBG_LOG	13	/* device -> host */

#define WQ_USB_MTU_FW_DL	10236	/* USB DMA requires 4 bytes alignment */

/* BMI v1 */
#define WQ_USB_EP_BI_BMI	9	/* bi-direction */

/* BMI v2 */
#define WQ_USB3_EP_WIFI_BMI	3
#define WQ_USB_EP_WIFI_BMI	6	/* WIFI-direction */
#define WQ_USB_EP_BT_BMI	9	/* BT-direction */

#endif /* WQ_FW_HIF_USB_API_H_ */
