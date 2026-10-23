#ifndef _WQ_SDIO_H_
#define _WQ_SDIO_H_

#include <linux/skbuff.h>
#include "hif_api.h"
#include "rwnx_defs.h"
#include "utils.h"

/** sdio throughput config */
#define SDIO_UT_TP_MODE 0
#define SDIO_UT_TP_ADDR 0x0FE00000
#define SDIO_UT_TP_800M_COUNT (1024 * 800 * 4)

/** define wuqi vendor id */
#define WUQI_DTOP_VENDOR_ID 0x0297
#define WUQI_WIFI_VENDOR_ID 0x0298
#define WUQI_WIFI_MSG_VENDOR_ID 0x0299

/** define wuqi device id */
#define WUQI_DTOP_DEVICE_ID 0x5348
#define WUQI_WIFI_DEVICE_ID 0x5349
#define WUQI_WIFI_MSG_DEVICE_ID 0x534a

/** define wuqi class id */
#define WUQI_DTOP_CLASS_ID 0x0
#define WUQI_WIFI_CLASS_ID 0x07
#define WUQI_WIFI_MSG_CLASS_ID 0x02

/* SDIO FUNCs*/
#define SDIO_FUNC_0 0
#define SDIO_FUNC_1 1
#define SDIO_FUNC_2 2
#define SDIO_FUNC_3 3
#define SDIO_FUNC_4 4
#define SDIO_FUNC_5 5
#define SDIO_FUNC_6 6
#define SDIO_FUNC_7 7
#define SDIO_FUNC_MAX 7
#define SDIO_FUNC_SIZE 8

/**func1: dtop, func2:wifi, func3:wifi msg*/
#define SDIO_FUNC_DTOP SDIO_FUNC_1
#define SDIO_FUNC_WIFI SDIO_FUNC_2
#define SDIO_FUNC_WIFI_MSG SDIO_FUNC_3
#define SDIO_FUNC_DTOP_LOG SDIO_FUNC_4

/** Card Common Control Registers (CCCR) */
#define SDIO_CCCR_SDIO_VERSION 0x00
#define SDIO_CCCR_SD_VERSION 0x01
#define SDIO_CCCR_IO_ENABLE 0x02
#define SDIO_CCCR_IO_READY 0x03
#define SDIO_CCCR_INT_ENABLE 0x04
#define SDIO_CCCR_INT_PENDING 0x05
#define SDIO_CCCR_IO_ABORT 0x06
#define SDIO_CCCR_BUS_CONTROL 0x07
#define SDIO_CCCR_CARD_CAP 0x08
#define SDIO_CCCR_CIS_PTR 0x09
#define SDIO_CCCR_BUS_SUSPEND 0x0c
#define SDIO_CCCR_FUNC_SEL 0x0d
#define SDIO_CCCR_EXEC_FLAG 0x0e
#define SDIO_CCCR_READY_FLG 0x0f
#define SDIO_CCCR_BLK_SIZE 0x10
#define SDIO_CCCR_PWR_CONTROL 0x12
#define SDIO_CCCR_BUS_SPEED_SEL 0x13
#define SDIO_CCCR_UHSI_SUPPORT 0x14
#define SDIO_CCCR_DRV_STRENGTH 0x15
#define SDIO_CCCR_INT_EXTERN 0x16
#define SDIO_CCCR_SUSPEND_INT_WUQI 0x17

/* TX buffer available interrupt */
#define SDIO_CCCR_WUQI_BUF_AVAIL_INT 0xEA
#define SDIO_CCCR_WUQI_BUF_AVAIL_INT_SET 0xF7
#define SDIO_CCCR_WUQI_BUF_AVAIL_INT_CLR 0xF9

/* RCV */
#define SDIO_CCCR_WUQI_RCV2 0xE7
#define SDIO_CCCR_WUQI_CFG_MODE 0xD8

#define SDIO_FW_CFG_ADMA_FUNX_MODE BIT(0)
#define SDIO_FW_CFG_TX_ACCU_MODE BIT(1)

/* RX */
#define SDIO_CCCR_WUQI_ACCU_LEN_INT 0xE9
#define SDIO_CCCR_WUQI_ACCU_LEN_INT_SET 0xFA
#define SDIO_CCCR_WUQI_ACCU_LEN_INT_CLR 0xFF

#define SDIO_CCCR_WUQI_FW 0xF8 /* Firmware Status*/
#define SDIO_BIT_DTOP_FW_STATUS BIT(0)
#define SDIO_BIT_WIFI_FW_STATUS BIT(1)
#define SDIO_BIT_BT_FW_STATUS BIT(2)
#define SDIO_BIT_DTOP_FW_READY BIT(3)
#define SDIO_BIT_WIFI_FW_READY BIT(4)
#define SDIO_BIT_BT_FW_READY BIT(5)

/* low power */
#define SDIO_CCCR_SUSPEND_INT                           0xF0
#define SDIO_CCCR_RESUME_INT                            0xF1
#define SDIO_CCCR_RESUME_ACK_CLR_INT                    0xF3
#define SDIO_CCCR_RESUME_ACK_INT                        0xF5
#define SDIO_CCCR_FNX_LP_EN_SET                         0xF2
/** ADMA INFO*/
#define FUNCX_RESUME_ACK_INT            98

#define SDIO_CCCR_RCV_REG0 0xEC /* Rom Version*/

/*
 * FIXME: move this definition into emu_common
 * SDIO virtual queue id
 */
enum wq_sdio_vqid {
	WQ_SDIO_VQID_INVALID = 0,

	WQ_SDIO_VQID_PKTIN = 1,
	WQ_SDIO_VQID_PKTOUT_BE = 1,
	WQ_SDIO_VQID_PKTOUT_BK = 3,
	WQ_SDIO_VQID_PKTOUT_VI = 4,
	WQ_SDIO_VQID_PKTOUT_VO = 5,

	WQ_SDIO_VQID_MSGIN = 2,
	WQ_SDIO_VQID_MSGOUT = 2,

	WQ_SDIO_VQID_FW_DL = 7,
	WQ_SDIO_VQID_BMI = 7,
	WQ_SDIO_VQID_UT_MSG = 8,
	WQ_SDIO_VQID_UT_PKT = 9,
};

#define WQ_SDIO_BLOCK_SIZE_512 512
#define WQ_SDIO_BLOCK_SIZE_256 256
#define WQ_SDIO_BLOCK_SIZE WQ_SDIO_BLOCK_SIZE_256

#define WQ_SDIO_64K_MAX_SEG_SIZE 65535
#define WQ_SDIO_128K_MAX_SEG_SIZE 4096

#define WQ_SDIO_HOST_RX_MAX_SIZE_OTHER 2
#define WQ_SDIO_HOST_RX_MAX_SIZE_64K 1
#define WQ_SDIO_HOST_RX_MAX_SIZE_128K 0

#define WQ_SDIO_BMI_MSG_MAGIC 0x77715751 /*wqWQ*/
#define WQ_INTERFACE_MSG_MAGIC 0x676D
#define WQ_INTERFACE_MSG_VERSION 0x0001

struct wq_sdio_bmi_msg {
	__be32 magic; /* FIXED: WQ_SDIO_BMI_MSG_MAGIC */
	__be16 id; /* message id */
	__be16 length; /* length of the packet data */
	u8 data[0];
} __attribute__((packed));

struct wq_sdio_msg_head {
	u32 len;
	u8 data[0];
} __attribute__((packed));

/** ADMA Info REG */
#define SDIO_ADMA_FUN0_INFO_REG (0x9000)
#define SDIO_ADMA_FUNX_INFO_REG (0x1F000)
#define SDIO_ADMA_INFO_LEN (WQ_SDIO_BLOCK_SIZE_256)
#define SDIO_ADMA_ACCU_LEN_MAX (0xFFFFFFFF)

#define SDIO_ADMA_DTOP_TX_LEN_MAX (64U << 10) //1664
#define SDIO_ADMA_WLAN_TX_LEN_MAX (200U << 10) /* 200K */
#define SDIO_ADMA_RX_LEN_MAX (128U << 10) /* 128K */
#define SDIO_ADMA_TX_AVAIL_BUF_MAX (0xFF)

#define SDIO_ADMA_TX_DTOP_AGGR_SIZE (1)
#define SDIO_ADMA_TX_WIFI_AGGR_SIZE (30)
#define SDIO_ADMA_TX_WIFI_MSG_AGGR_SIZE (1)
#define SDIO_ADMA_TX_MAX_AGGR_SIZE (120)

#define SDIO_ADMA_TX_ACCU_LEN_MASK  (0x000000FF)
#define SDIO_ADMA_TX_ACCU_SOFT_MODE  (0x01000000)
#define SDIO_ADMA_TX_ACCU_PKT_MAX_MASK  (0x02000000)

#define SDIO_TP_THRESHOLD  100 /* unit: Mbps */

#define SDIO_TX_AGGR_MODE
#define SDIO_RX_AGGR_MODE
// #define SDIO_TX_POLLING_MODE
#define SDIO_RX_POLLING_MODE
#define SDIO_WLAN_MSG_MODE
#define SDIO_DTOP_LOG_MODE
// #define SDIO_MAIN_KTHREAD

struct wq_sdio_adma_pkt_hdr {
	__le32 len; /* length of type + data[] */
	__le32 virt_qid; /* WQ_SDIO_VQID_XXX */
	u8 data[0];
} __attribute__((packed));

typedef union wq_sdio_tx_buf_cnt {
	__le32 buf_cnt_32;
	struct {
		__le32 used_buf_cnt: 8;
		__le32 free_buf_cnt: 8;
		__le32 reserved: 16;
	} buf_cnt_s;
} wq_sdio_tx_buf_cnt_u;

struct wq_sdio_adma_info_all {
	__le32 rx_accu_len[SDIO_FUNC_MAX]; /* in bytes */
	__le32 rx_bus_len[SDIO_FUNC_MAX]; /* in bytes */
	__le32 tx_buffer_avail[SDIO_FUNC_MAX]; /* packet number */
	__le32 pad1;
	wq_sdio_tx_buf_cnt_u fn1_tx_buf_cnt;
	wq_sdio_tx_buf_cnt_u fn2_tx_buf_cnt;
	wq_sdio_tx_buf_cnt_u fn3_tx_buf_cnt;

	u32 pads[SDIO_ADMA_INFO_LEN / sizeof(u32) - SDIO_FUNC_MAX * 3 - 4];
};

struct wq_sdio_adma_info {
	u32 rx_accu_len;
	u32 rx_bus_len;
	s32 rx_len; /* distance between rx_accu_len and rx_bus_len */

	u8 tx_buffer_avail;
	u8 tx_buffer_avail_max;
	u8 tx_accu_cnt;
	u8 tx_bus_cnt;

	bool tx_need_sync;
	bool tx_accu_mode;
};

struct wq_sdio_stats {
	atomic_t rx_total_cnt;
	atomic_t rx_pkt_total_num;
	atomic_t rx_pkt_total_bytes;
	atomic_t rx_adma_total_cnt;

	atomic_t tx_total_cnt;
	atomic_t tx_pkt_total_num;
	atomic_t tx_pkt_total_bytes;
	atomic_t tx_buf_avail_valid_cnt;
	atomic_t tx_buf_avail_valid_pkt_cnt;
	atomic_t tx_buf_avail_zero_cnt;
	atomic_t tx_buf_avail_zero_pkt_cnt;
	atomic_t tx_buf_avail_wr_cnt;
	u32 tx_aggr_avg_cnt;

	atomic_t main_kthread_time;
	atomic_t main_tx_time;
	atomic_t main_rx_time;
	atomic_t main_rx_adma_time;
	atomic_t main_tx_aggr_time;
	u32 rx_aggr_avg_cnt;

	atomic_t rx_workq_time;
	atomic_t rx_deaggr_time;
	atomic_t rx_htc_time;
	atomic_t rx_workq_tx_aggr_time;

	atomic_t tx_xmit_aggr_time;
};

static inline int wq_sdio_adma_rx_len(struct wq_sdio_adma_info *adma_info)
{
	/**
	 * fix: rx_accu_len less than rx_bus_len when rx_accu_len length overflow
	 *
	 * add rx len calculate for rx_accu_len length overflow
	*/
	if (adma_info->rx_accu_len >= adma_info->rx_bus_len) {
		adma_info->rx_len =
			adma_info->rx_accu_len - adma_info->rx_bus_len;
	} else {
		adma_info->rx_len =
			(SDIO_ADMA_ACCU_LEN_MAX - adma_info->rx_bus_len +
			 adma_info->rx_accu_len + 1);
	}

	return 0;
}

static inline u8 wq_sdio_adma_tx_len(u8 tx_accu_cnt, u8 tx_bus_cnt)
{
	u8 tx_buf_avail = 0;

	if (tx_accu_cnt >= tx_bus_cnt) {
		tx_buf_avail = tx_accu_cnt - tx_bus_cnt;
	} else {
		tx_buf_avail = (SDIO_ADMA_TX_AVAIL_BUF_MAX - tx_bus_cnt + tx_accu_cnt + 1);
	}

	return tx_buf_avail;
}

#define WQ_MSGOUT_NUM 100
#define WQ_PTKOUT_NUM 600
#define WQ_AGGIN_NUM 30
#define WQ_AGGROUT_NUM 60

struct wq_skbreq {
	struct list_head list;
	struct sk_buff *skb;
	enum wq_sdio_vqid virt_qid;
	u32 aggr_cnt;
	u32 aggr_consumed_cnt;
	u16 aggr_len[SDIO_ADMA_TX_MAX_AGGR_SIZE];
	unsigned char *aggr_addr[SDIO_ADMA_TX_MAX_AGGR_SIZE];
};

struct wq_func {
	const char *name;
	uint8_t func_num;
	struct wq_sdio *wq_sdio;
	struct sdio_func *func;

	struct {
#ifdef SDIO_MAIN_KTHREAD
		struct wq_kthread maink;
#else
		struct wq_workq mainq;
#endif

#ifdef SDIO_RX_AGGR_MODE
		struct wq_workq rxq;
#endif

		struct wq_sdio_adma_info info;
		struct wq_sdio_adma_info_all *all_info;

		struct {
			struct sk_buff *tx;
			struct sk_buff *rx; /* remainder of latest de-aggregation */
		} aggr;
	} adma;

	struct {
		struct wq_list_head pktout;
		struct wq_list_head msgout;

#ifdef SDIO_RX_AGGR_MODE
		struct wq_list_head aggrin;
#endif

#ifdef SDIO_TX_AGGR_MODE
		struct wq_list_head aggrout;
#endif
	} q;
};

enum sdio_pm_status {
	SDIO_ACTIVE,
	SDIO_RPM_SUSPENDING,
	SDIO_RPM_SUSPENDED,
	SDIO_SYS_SUSPENDING,
	SDIO_SYS_SUSPENDED,
	SDIO_RESUMING,
};

typedef struct wq_ut_stat {
	volatile uint8_t running;
	ktime_t  time_start;

	uint64_t tx_pkt_total_bytes;
	uint64_t rx_pkt_total_bytes;
} wq_ut_stat_t;

typedef struct wq_ut_config {
	/* test dir */
	uint8_t test_rx;
	uint8_t test_tx;

	uint32_t test_pkt_len;
	uint32_t test_pkt_cnt;

	/* test time */
	uint32_t test_total_time;
} __attribute__((__packed__)) wq_ut_config_t;

struct wq_sdio {
	struct wq_core core;

	struct wq_func dtop;
	struct wq_func dtop_log;
	struct wq_func wlan;
	struct wq_func wlan_msg;

	/* sdio unit test */
	wq_ut_config_t ut_config;
	wq_ut_stat_t  ut_stat;
	struct timer_list test_timer;

	atomic_t pm_status;
	bool wlan_msg_en;

	struct {
		struct wq_list_pool pktout;
		struct wq_list_pool msgout;

#ifdef SDIO_RX_AGGR_MODE
		struct wq_list_pool aggrin;
#endif

#ifdef SDIO_TX_AGGR_MODE
		struct wq_list_pool aggrout;
#endif
	} pools;

	/* snap shot of device interrupt mask */
	u8 intr;
	u8 intr_accu_len; /* RX */
	u8 intr_buf_avail; /* TX */
	u8 fw_cfg_mode;

	struct {
		spinlock_t lock;
		struct sk_buff *reply;

#ifdef USE_COMPLETE
		struct completion completion;
#else
		bool woken;
		wait_queue_head_t wait_q;
#endif
	} bmi;

	struct wq_sdio_stats wlan_stats;
	bool bus_dead;
	struct delayed_work detect_work;
};

int wq_sdio_cmd52_read(struct sdio_func *func, int addr, u8 *val);
int wq_sdio_cmd52_write(struct sdio_func *func, int addr, u8 val);

int wq_sdio_extend_mem_readb(struct sdio_func *func, int addr, u8 *val);

int wq_sdio_intr_en(struct wq_sdio *wq_sdio, struct wq_func *wq_func, bool en);
int wq_sdio_intr_accu_len_en(struct wq_sdio *wq_sdio, struct wq_func *wq_func,
			     bool en);
int wq_sdio_intr_buf_avail_en(struct wq_sdio *wq_sdio, struct wq_func *wq_func,
			      bool en);

void wq_sdio_rx_bmi_msg(struct wq_sdio *wq_sdio, struct sk_buff *skb);

int wq_sdio_bmi_cmd(struct wq_core *core, u8 cmd, const void *param, u16 p_size,
		    void *resp, u16 r_size, int timeout);

int wq_sdio_bmi_xfer(struct wq_core *core, enum wq_bmi_xfer_type type,
		     const u8 *data, int len, int timeout);

int wq_sdio_bmi_exchange(struct wq_core *core, void *req, u32 req_len,
			 void *rsp, u32 rsp_len, int timeout);

/* work queue & interrupt */
#ifdef SDIO_MAIN_KTHREAD
int wq_sdio_adma_process(void *data);
#else
void wq_sdio_adma_process(struct work_struct *work);
#endif
void wq_func_main_trigger(struct wq_func *wq_func);
void wq_sdio_interrupt(struct sdio_func *func);

#ifdef SDIO_RX_AGGR_MODE
void wq_sdio_adma_rx_process(struct work_struct *work);
void wq_func_rx_trigger(struct wq_func *wq_func);
#endif

#ifdef SDIO_TX_AGGR_MODE
int wq_sdio_adma_tx_pkt_aggr(struct wq_func *wq_func, int max_pkts);
#endif

int wq_sdio_pools_init(struct wq_sdio *wq_sdio);
void wq_sdio_pools_deinit(struct wq_sdio *wq_sdio);

/* sdio unit test */
void wq_sdio_ut_init(struct wq_sdio *wq_sdio);
void wq_sdio_ut_deinit(struct wq_sdio *wq_sdio);
int wq_sdio_ut_prepare_tx_pkt(struct wq_sdio *wq_sdio);
int wq_sdio_ut_rx_msg(struct wq_sdio *wq_sdio, struct sk_buff *skb);
int wq_sdio_ut_print_stat(struct wq_sdio *wq_sdio);

static inline void wq_skbreq_enqueue(struct wq_list_head *q,
				     struct wq_skbreq *req)
{
	WQ_LIST_PUSH(q, struct wq_skbreq, list, req);
}

static inline void __wq_skbreq_enqueue(struct wq_list_head *q,
				       struct wq_skbreq *req)
{
	__WQ_LIST_PUSH(q, struct wq_skbreq, list, req);
}

static inline struct wq_skbreq *wq_skbreq_dequeue(struct wq_list_head *q)
{
	return WQ_LIST_POP(q, struct wq_skbreq, list);
}

static inline struct wq_skbreq *__wq_skbreq_dequeue(struct wq_list_head *q)
{
	return __WQ_LIST_POP(q, struct wq_skbreq, list);
}

static inline struct wq_skbreq *wq_skbreq_dequeue_last(struct wq_list_head *q)
{
	return WQ_LIST_POP_LAST(q, struct wq_skbreq, list);
}

static inline struct wq_skbreq *__wq_skbreq_dequeue_last(struct wq_list_head *q)
{
	return __WQ_LIST_POP_LAST(q, struct wq_skbreq, list);
}

static inline struct wq_skbreq *wq_skbreq_peek(struct wq_list_head *q)
{
	return WQ_LIST_PEEK(q, struct wq_skbreq, list);
}

static inline struct wq_skbreq *__wq_skbreq_peek(struct wq_list_head *q)
{
	return __WQ_LIST_PEEK(q, struct wq_skbreq, list);
}

static inline struct wq_skbreq *wq_skbreq_peek_last(struct wq_list_head *q)
{
	return WQ_LIST_PEEK_LAST(q, struct wq_skbreq, list);
}

static inline struct wq_skbreq *__wq_skbreq_peek_last(struct wq_list_head *q)
{
	return __WQ_LIST_PEEK_LAST(q, struct wq_skbreq, list);
}

static inline void wq_skbreq_unpop(struct wq_list_head *q,
				   struct wq_skbreq *req)
{
	WQ_LIST_UNPOP(q, struct wq_skbreq, list, req);
}

static inline void __wq_skbreq_unpop(struct wq_list_head *q,
				     struct wq_skbreq *req)
{
	__WQ_LIST_UNPOP(q, struct wq_skbreq, list, req);
}

static inline struct wq_skbreq *wq_skbreq_alloc(struct wq_list_pool *pool)
{
	return wq_skbreq_dequeue(&pool->list);
}

static inline struct wq_skbreq *__wq_skbreq_alloc(struct wq_list_pool *pool)
{
	return __wq_skbreq_dequeue(&pool->list);
}

static inline void wq_skbreq_free(struct wq_list_pool *pool,
				  struct wq_skbreq *req)
{
	if (req->skb) {
		skb_trim(req->skb, 0);
		req->aggr_cnt = 0;
		req->aggr_consumed_cnt = 0;
	}

	wq_skbreq_enqueue(&pool->list, req);
}

static inline void __wq_skbreq_free(struct wq_list_pool *pool,
				    struct wq_skbreq *req)
{
	if (req->skb) {
		skb_trim(req->skb, 0);
		req->aggr_cnt = 0;
		req->aggr_consumed_cnt = 0;
	}

	__wq_skbreq_enqueue(&pool->list, req);
}

static inline int wq_sdio_get_free_pkt_num(struct wq_sdio *wq_sdio)
{
	int tx_pkt_free_num = 0;
	unsigned long flags;

	spin_lock_irqsave(&(wq_sdio->wlan.q.pktout.lock), flags);

	tx_pkt_free_num = (wq_sdio->pools.pktout.num - wq_sdio->wlan.q.pktout.num);

	spin_unlock_irqrestore(&(wq_sdio->wlan.q.pktout.lock), flags);

	return tx_pkt_free_num;
}

static inline int wq_sdio_adma_aggr_size(struct wq_func *wq_func)
{
	struct wq_sdio *wq_sdio = wq_func->wq_sdio;
	int aggr_size = 0;

	if (wq_func->adma.info.tx_accu_mode) {
		if ((wq_func == &wq_sdio->dtop) || (wq_func == &wq_sdio->dtop_log)) {
			aggr_size = SDIO_ADMA_TX_DTOP_AGGR_SIZE;
		} else if (wq_func == &wq_sdio->wlan_msg) {
			aggr_size = SDIO_ADMA_TX_WIFI_MSG_AGGR_SIZE;
		} else {
			aggr_size = SDIO_ADMA_TX_WIFI_AGGR_SIZE;
		}
	} else {
		aggr_size = wq_func->adma.info.tx_buffer_avail_max;
	}

	return aggr_size;
}

void wq_sdio_wifi_fwdl_work(struct work_struct *work);

void wq_sdio_set_host_rx_max_size(struct wq_func *wq_func);

#endif
