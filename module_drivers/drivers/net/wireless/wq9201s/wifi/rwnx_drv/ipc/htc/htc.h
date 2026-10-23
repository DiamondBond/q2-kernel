#ifndef WQ_WLAN_HTC_H_
#define WQ_WLAN_HTC_H_

#include "fw_api/non_wifi/hif/api.h"
#include "utils.h"

#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>

struct htc_q;
struct wq_core;

typedef int (*htc_fn_t)(struct htc_q *q, struct sk_buff *skb);

/* define the time of htc polling. unit ms */
#define HTC_POLLING_RX_TASK_TIME_MS 10

struct htc_q {
	enum wq_hif_qid qid;

#ifdef CONFIG_RX_THREAD
	bool use_thread;
	struct wq_kthread thread; /* use thread to handle RX data */
#endif
	struct tasklet_struct task;

	htc_fn_t fn;

	struct spinlock lock;
	uint32_t last_seq; /* last sequence for sanity check */
	uint32_t cnt; /* total number */
	struct sk_buff_head head; /* rx or tx done queue */
};

struct htc_fc {
	/* constant HIF queue depths */
	uint16_t dst_depth; /* HIF depth in device side */
	uint16_t src_depth; /* HIF depth in host side */

	/* counters */
	uint32_t seq; /* sequence (increased each time) */
	uint32_t deq; /* counter (dequeued from HIF) */

	/* water level */
	uint16_t low;
	uint16_t high;
};

struct htc_txq {
	struct htc_q up; /* to upper layer */
	struct htc_fc fc; /* to hif */
};

/* just for ll data */
struct htc_ll_data {
	uint16_t ll_data_free_mark; /* notify up layer to free ll data */
	uint16_t ll_data_htc_done_cnt; /* notify up layer to free ll data */
	ktime_t deadline; /* deadline to fire it */
	struct hrtimer timer; /* ll data timer */
	htc_fn_t fn;
	uint32_t ll_hdl_flag;
};

struct htc {
	struct {
		struct htc_q pkt;
		struct htc_q msg;
	} rxq;
	struct htc_txq txq[WQ_QID_MAX];
};

struct htc_tx_req {
	struct list_head list;
	struct sk_buff_head skbq;
	enum wq_hif_qid qid;
};

struct htc_wait_q {
	struct list_head tx_req_list;
	atomic_t pending_req;
	uint32_t tx_wait_cnt;
	struct spinlock lock;
	struct tasklet_struct hif_tx_task;
};

int htc_q_init(struct wq_core *core, struct htc_q *q, enum wq_hif_qid qid,
	       htc_fn_t fn);
#ifdef CONFIG_RX_THREAD
int htc_q_init_thread(struct wq_core *core, struct htc_q *q,
		      enum wq_hif_qid qid, htc_fn_t fn);
#endif

void htc_q_deinit(struct htc_q *q);

int htc_tx(struct wq_core *core, enum wq_hif_qid qid, struct sk_buff_head *skbq,
	   uint32_t u);
void htc_tx_waitq_init(struct wq_core *core);
void htc_txq_ring_2task(struct wq_core *core);
void htc_txq_ring_start_timer(struct wq_core *core);
void htc_tx_flush_waitq(struct wq_core *core);

void hif_htc_rxq_decap(struct wq_core *core, struct sk_buff_head *skbq);

static inline void htc_q_kickoff(struct htc_q *q)
{
#ifdef CONFIG_RX_THREAD
	if (q->use_thread)
		complete(&q->thread.comp);
	else
#endif
		tasklet_schedule(&q->task);
}

#endif /* WQ_WLAN_HTC_H_ */
