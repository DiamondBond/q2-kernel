#ifndef WQ_FW_WIFI_HTC_V0_API_H_
#define WQ_FW_WIFI_HTC_V0_API_H_

#include "fw_api/wifi/htc/api.h"
#include "fw_api/wifi/htc/tx_credit.h"
#include "fw_api/non_wifi/hif/api.h"

/************************************
 * htc header v0 definition
 ************************************/
//IPC type
enum wq_ipc_types {
	WQ_IPC_TPE_INVALID = 0,
	WQ_IPC_TPE_CMD,
	WQ_IPC_TPE_EVT,
	WQ_IPC_TPE_PKT,

	WQ_IPC_TPE_MAX,
};
#define WQ_IPC_TPE_MASK		0x3
#define WQ_IPC_TPE_SHIFT	0
#define WQ_IPC_TPE(flags)	(((flags) & WQ_IPC_TPE_MASK) >> WQ_IPC_TPE_SHIFT)

//IPC receiver
enum wq_ipc_rcvs {
	WQ_IPC_RCV_DEV = 0,
	WQ_IPC_RCV_INTF1,
	WQ_IPC_RCV_INTF2,
	WQ_IPC_RCV_MAX
};
#define WQ_IPC_RCV_MASK		0xC
#define WQ_IPC_RCV_SHIFT	2
#define WQ_IPC_RCV(flags)	(((flags) & WQ_IPC_RCV_MASK) >> WQ_IPC_RCV_SHIFT)

//IPC get/set status
enum wq_ipc_status {
	WQ_IPC_STS_SUCC = 0,
	WQ_IPC_STS_ERR
};
#define WQ_IPC_STS_MASK		0xF000
#define WQ_IPC_STS_SHIFT	12
#define WQ_IPC_STS(flags)	(((flags) & WQ_IPC_STS_MASK) >> WQ_IPC_STS_SHIFT)

//IPC sequence number
#define WQ_IPC_SEQ_MASK		0xFFFF0000
#define WQ_IPC_SEQ_SHIFT	16
#define WQ_IPC_SEQ(flags)	(((flags) & WQ_IPC_SEQ_MASK) >> WQ_IPC_SEQ_SHIFT)

/* IPC TX pkt flags */
enum wq_ipc_pkt_flags {
	WQ_IPC_FLAGS_TX_NORMAL_QUEUE = 0,
	WQ_IPC_FLAGS_TX_HIGH_QUEUE,
};

/* assemble flags of struct wq_ipc_header */
#define WQ_IPC_FLAGS_MAKE(type, receiver, txq, seq)	\
			(u32)((((type) << WQ_IPC_TPE_SHIFT) & WQ_IPC_TPE_MASK) | \
			      (((receiver) << WQ_IPC_RCV_SHIFT) & WQ_IPC_RCV_MASK) | \
			      (((txq) << WQ_IPC_STS_SHIFT) & WQ_IPC_STS_MASK) | \
			      (((seq) << WQ_IPC_SEQ_SHIFT) & WQ_IPC_SEQ_MASK))

struct wq_htc_v0 {
	uint32_t flags;	//[0:1] refer to WQ_IPC_TPE_XXX
			//[2:3] receiver => 0:device, 1:interface#1, 2:interface#2
			//[12:15] get/set status (returned from the device)
			//[16:31] sequence number
	union {
		uint32_t cmd_type;	//enum wq_ipc_cmd_type (only used by host)
		uint32_t evt_id;		//enum e2a_event_id (MAC_E2A_XXX)
		uint32_t pkt_flags;	//enum wq_ipc_pkt_flags for date only
	} u;
	union {
		uint32_t all;
		uint8_t txq[WQ_CREDIT_TYPE_NUM];
	} credit_grp[WQ_CREDIT_GROUP_NUM];
	uint32_t buf_len; /* followed buffer(payload) length */
} __packed;

struct compressed_htc_v0 {
	uint32_t flags;	//[0:1] refer to WQ_IPC_TPE_XXX
			//[2:3] receiver => 0:device, 1:interface#1, 2:interface#2
			//[12:15] get/set status (returned from the device)
			//[16:31] sequence number
	union {
		uint32_t cmd_type;	//enum wq_ipc_cmd_type (only used by host)
		uint32_t evt_id;		//enum e2a_event_id (MAC_E2A_XXX)
		uint32_t pkt_flags;	//enum wq_ipc_pkt_flags for date only
	} u;
	uint32_t buf_len; /* followed buffer(payload) length */
} __packed;

#endif /* WQ_FW_WIFI_HTC_V0_API_H_ */
