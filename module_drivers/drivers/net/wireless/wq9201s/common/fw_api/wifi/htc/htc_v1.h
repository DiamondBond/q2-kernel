#ifndef WQ_FW_WIFI_HTC_V1_API_H_
#define WQ_FW_WIFI_HTC_V1_API_H_

#include "fw_api/non_wifi/hif/api.h"
#include "fw_api/wifi/htc/htc_v0.h"

/************************************
 * htc header v1 definition
 ************************************/
// struct wq_htc_v1->flags
#define WQ_LL_IF_HEADER_FLAG_TX         0x1 // 0 means RX.
#define WQ_LL_IF_HEADER_FLAG_IPCINFO    0x2 // means need to include wq_htc_v1 and trailer to tx.
#define WQ_LL_IF_HEADER_FLAG_CREDIT     0x4 // means credit report is included.

// wq_htc_v1 will be used for low latency firstly.
struct wq_htc_v1 {
	//flags to indicate HTC info, refer to WQ_LL_IF_HEADER_FLAG_CREDIT_xxx
	//bit[0]: 0->rx, 1->rx. Set to 1 to tell ipc layer is a tx, 0 is rx.
	//              use direction and channel id to know transfer type/usage.
	//bit[1]: 0->transmit data don't need to include struct wq_htc_v1 and trailer.
	//        1->transmit data include struct wq_htc_v1.
	//bit[2]: 0-> no credit report included.
	//        1-> credit report included.
	//bit[3-15]: reserved(TBD), fill 0 now.
	uint16_t flags;
	//meta info(TBD), fill 0 now.
	uint8_t meta_info;
	// channel id for this IPC transfer. Virtual channel
	// channel id is created to map to interface endpoint or DMA channel.
	uint8_t channel;
	// point to buffer start related with this IPC transfer data payload. Not the header start.
	// could be used to indicate DMA gather buffer in tx or scatter buffer in rx.
	addr32 buf;
	// buffer length in bytes. Starting from transferred data.
	// If flags indicates additional info, then start from the additional info.
	uint16_t buf_len;
	// reserved offset in bytes before the transferred data payload from ipc caller.
	// when there is additional info need to be transferred along data payload, 'buf_off' is used
	// to indicate from start of above 'buf' to the transferred data payload from ipc caller.
	uint16_t buf_off;
	// reserved
	uint32_t reserved[2];
} __packed;

union wq_htc_hdr {
	struct wq_htc_v0 v0;
	struct wq_htc_v1 v1;
};

#endif /* WQ_FW_WIFI_HTC_V1_API_H_ */
