#ifndef WQ_FW_HIF_API_H_
#define WQ_FW_HIF_API_H_

#include "fw_api/non_wifi/api.h"

/************************************
 * hif header definition
 ************************************/
#define WQ_HIF_HDR_MAGIC			0x33

enum wq_hif_ver {
	WQ_HIF_HDR_VER_0 = 0,		/* followed by wq_htc_v0 */
	WQ_HIF_HDR_VER_1 = 1,		/* followed by wq_htc_v1 */

	WQ_HIF_HDR_VER_RESERVED = 7,	/* reserved (invalid) */
};

enum wq_hif_qid {
	WQ_QID_AC_BK = 0,		/* alias of broadcast/multicast, same as AC_BK */
	WQ_QID_AC_BE,
	WQ_QID_AC_VI,
	WQ_QID_AC_VO,			/* alias of management */

	WQ_QID_AC_MAX,

	WQ_QID_MSG = WQ_QID_AC_MAX,	/* message (command/event) */

	WQ_QID_MAX,
};

struct wq_hif_hdr {
	uint32_t ptn :8,		// = WQ_HIF_HDR_MAGIC
		ver :3,		// enum wq_hif_ver (= WQ_HIF_HDR_VER_XXX)
		qid :3,		// enum wq_hif_qid (= WQ_QID_xxx)
		dw_len :14,	//Length in DWORDs.means total transferred IPC data must be 4-byte alignment,
				//including wq_htc_v0/1, payload and trailer.
				//MAX length: 16384 * DWORD = 64K.
		crc:4;		//CRC4. Software calculated.
} __packed;

/************************************
 * hif trailer definition
 ************************************/
#define WQ_HIF_TRAILER_CRC32_SIZE	sizeof(u32)	// reserved for CRC32
#define WQ_HIF_TRAILER_SPACE_RSVD	WQ_HIF_TRAILER_CRC32_SIZE

#endif /* WQ_FW_HIF_API_H_ */
