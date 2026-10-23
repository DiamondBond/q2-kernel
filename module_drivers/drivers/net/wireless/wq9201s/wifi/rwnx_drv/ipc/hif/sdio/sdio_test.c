#include <linux/kernel.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <linux/module.h>

#include "sdio.h"
#include "wq_log.h"
#include "fw_log.h"
#include "proc.h"
#include "rwnx_compat.h"

/* For throughput test */
#define SDIO_TX_PER_PACK_SIZE                    1600
#define SDIO_TX_PACK_CNT                         120
#define SDIO_TX_TIME                             (30 * 1000)   // ms

#define SDIO_RX_PER_PACK_SIZE                    1600
#define SDIO_RX_PACK_CNT                         78
#define SDIO_RX_TIME                             (30 * 1000)   // ms

#ifndef PROC_DIR
#define PROC_DIR "driver/wifi_usb"
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0))
#define wq_proc_ops file_operations
#else
#define wq_proc_ops proc_ops
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

int sdio_ut_mode = 0;
module_param(sdio_ut_mode, int, 0);
MODULE_PARM_DESC(sdio_ut_mode, "sdio unit test mode(default 0:Disable)");

int wq_sdio_ut_send_tx_config(struct wq_sdio *wq_sdio);
int wq_sdio_ut_send_rx_config(struct wq_sdio *wq_sdio);

static int unit_test_proc_show(struct seq_file *seq, void *v) {
	unsigned int *ptr_var = seq->private;
	seq_printf(seq, "%u\n", *ptr_var);
	return 0;
}

static int unit_test_proc_open(struct inode *inode, struct file *file) {
	return single_open(file, unit_test_proc_show, PDE_DATA(inode));
}

static ssize_t unit_test_tx_proc_write(struct file *file, const char __user *buffer, size_t count, loff_t *pos) {
    struct seq_file *seq = file->private_data;
    struct wq_sdio *wq_sdio = seq->private;

    wq_sdio_ut_send_tx_config(wq_sdio);

    return count;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops proc_unit_test_tx_fops = {
	.owner = THIS_MODULE,
	.open = unit_test_proc_open,
	.write  = unit_test_tx_proc_write,
	.llseek = noop_llseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops proc_unit_test_tx_fops = {
	.proc_open = unit_test_proc_open,
	.proc_write = unit_test_tx_proc_write,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};
#endif

static ssize_t unit_test_rx_proc_write(struct file *file, const char __user *buffer, size_t count, loff_t *pos) {
    struct seq_file *seq = file->private_data;
    struct wq_sdio *wq_sdio = seq->private;

    wq_sdio_ut_send_rx_config(wq_sdio);

    return count;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops proc_unit_test_rx_fops = {
	.owner = THIS_MODULE,
	.open = unit_test_proc_open,
	.write  = unit_test_rx_proc_write,
	.llseek = noop_llseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops proc_unit_test_rx_fops = {
	.proc_open = unit_test_proc_open,
	.proc_write = unit_test_rx_proc_write,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
};
#endif

static void wq_sdio_ut_stat_cb(struct timer_list *t)
{
	struct wq_sdio *wq_sdio = container_of(t, struct wq_sdio, test_timer);
	wq_ut_stat_t *ut_stat = &wq_sdio->ut_stat;
	wq_ut_config_t *ut_config = &wq_sdio->ut_config;

	if (ut_stat->running) {
		ktime_t now, diff;
		now = ktime_get();
		diff = ktime_sub(now, wq_sdio->ut_stat.time_start);

		wq_sdio_ut_print_stat(wq_sdio);

		if (ktime_to_ms(diff) >= ut_config->test_total_time) {
			ut_stat->running = false;
		} else {
			mod_timer(t, (jiffies + HZ));
		}
	}
}

void wq_sdio_ut_init(struct wq_sdio *wq_sdio)
{
	wq_sdio->ut_stat.running = 0;
	timer_setup(&wq_sdio->test_timer, wq_sdio_ut_stat_cb, 0);

	proc_mkdir(PROC_DIR, NULL);

	proc_create_data(PROC_DIR "/unit_test_tx", S_IRUGO | S_IWUGO, NULL,
		&proc_unit_test_tx_fops, wq_sdio);
	proc_create_data(PROC_DIR "/unit_test_rx", S_IRUGO | S_IWUGO, NULL,
		&proc_unit_test_rx_fops, wq_sdio);
}

void wq_sdio_ut_deinit(struct wq_sdio *wq_sdio)
{
	remove_proc_entry(PROC_DIR "/unit_test_tx", NULL);
	remove_proc_entry(PROC_DIR "/unit_test_rx", NULL);

	remove_proc_entry(PROC_DIR, NULL);
}

int wq_sdio_ut_tx_msg(struct wq_sdio *wq_sdio, uint8_t* msg_data, uint16_t msg_len)
{
	struct wq_func *wq_func = &wq_sdio->wlan;
	struct wq_skbreq *req;
	struct sk_buff *skb = NULL;
	wq_msg_header_t *msg_hdr;
	int skb_len = sizeof(wq_msg_header_t) + msg_len;

	skb = dev_alloc_skb(skb_len);
	if (!skb) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: skb is NULL!\n", __func__);
		return -1;
	}

	msg_hdr = (wq_msg_header_t *)skb_put(skb, sizeof(wq_msg_header_t));
	msg_hdr->magic = cpu_to_be16(WQ_INTERFACE_MSG_MAGIC);
	msg_hdr->version = cpu_to_be16(WQ_INTERFACE_MSG_VERSION);
	msg_hdr->msg_type = cpu_to_be16(MSG_TYPE_UT_TP);
	msg_hdr->msg_length = cpu_to_be16(skb_len);

	if (msg_data) {
		memcpy(msg_hdr->data, msg_data, msg_len);
		skb_put(skb, msg_len);
	}

	req = wq_skbreq_alloc(&wq_sdio->pools.msgout);
	if (!req) {
		WQ_DBG(DM_TRBUS, DL_ERR,
				"%s: req is null, tx_buffer_avail %d\n",
				__func__,
				wq_func->adma.info.tx_buffer_avail);
		return -1;
	}

	req->skb = skb;
	req->virt_qid = WQ_SDIO_VQID_UT_MSG;
	wq_skbreq_enqueue(&wq_func->q.msgout, req);

	wq_func_main_trigger(wq_func);

	return 0;
}

int wq_sdio_ut_send_config(struct wq_sdio *wq_sdio, wq_ut_config_t *ut_config_ptr)
{
    wq_ut_config_t ut_config;

    ut_config.test_rx = ut_config_ptr->test_rx;
    ut_config.test_tx = ut_config_ptr->test_tx;
    ut_config.test_pkt_len = cpu_to_be32(ut_config_ptr->test_pkt_len);
    ut_config.test_pkt_cnt = cpu_to_be32(ut_config_ptr->test_pkt_cnt);
    ut_config.test_total_time = cpu_to_be32(ut_config_ptr->test_total_time);

    WQ_DBG(DM_GENERIC, DL_ERR, "Send throughput unit test cmd. test_rx 0x%x, test_tx 0x%x, test_pkt_len 0x%x, test_pkt_cnt 0x%x, test_total_time 0x%x\n",
        ut_config.test_rx, ut_config.test_tx, ut_config.test_pkt_len, ut_config.test_pkt_cnt, ut_config.test_total_time);

    return wq_sdio_ut_tx_msg(wq_sdio, (uint8_t*)&ut_config, sizeof(wq_ut_config_t));
}

int wq_sdio_ut_send_tx_config(struct wq_sdio *wq_sdio)
{
	wq_ut_config_t *ut_config = &wq_sdio->ut_config;

    WQ_DBG(DM_GENERIC, DL_ERR, "Send throughput unit test tx cmd.\n");

    ut_config->test_rx = 0;
    ut_config->test_tx = 1;
    ut_config->test_pkt_len = SDIO_TX_PER_PACK_SIZE;
    ut_config->test_pkt_cnt = SDIO_TX_PACK_CNT;
    ut_config->test_total_time = SDIO_TX_TIME;

    return wq_sdio_ut_send_config(wq_sdio, ut_config);
}

int wq_sdio_ut_send_rx_config(struct wq_sdio *wq_sdio)
{
	wq_ut_config_t *ut_config = &wq_sdio->ut_config;

    WQ_DBG(DM_GENERIC, DL_ERR, "Send throughput unit test rx cmd.\n");

    ut_config->test_rx = 1;
    ut_config->test_tx = 0;
    ut_config->test_pkt_len = SDIO_RX_PER_PACK_SIZE;
    ut_config->test_pkt_cnt = SDIO_RX_PACK_CNT;
    ut_config->test_total_time = SDIO_RX_TIME;

    return wq_sdio_ut_send_config(wq_sdio, ut_config);
}

int wq_sdio_ut_prepare_tx_pkt(struct wq_sdio *wq_sdio)
{
	struct wq_func *wq_func = &wq_sdio->wlan;
	wq_ut_config_t *ut_config = &wq_sdio->ut_config;
	struct wq_skbreq *req;
	struct wq_sdio_adma_pkt_hdr *pkt_hdr;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&(wq_func->q.aggrout.lock), flags);

	req = __wq_skbreq_peek_last(&wq_func->q.aggrout);
	if ((req == NULL) || (req->aggr_cnt == ut_config->test_pkt_cnt)) {
		req = wq_skbreq_dequeue(&wq_sdio->pools.aggrout.list);
		if (!req) {
			printk_ratelimited(KERN_INFO "%s: req is null, max_pkts %d, func aggrout num %d, func pktout num %d\n", __func__,
				ut_config->test_pkt_cnt, wq_func->q.aggrout.num, wq_func->q.pktout.num);
			goto exit;
		}

		req->aggr_cnt = 0;
		req->aggr_consumed_cnt = 0;
		__wq_skbreq_enqueue(&wq_func->q.aggrout, req);
	}

	for (i = 0; i < ut_config->test_pkt_cnt; i++) {
		req->aggr_addr[req->aggr_cnt] = skb_tail_pointer(req->skb);
		req->aggr_len[req->aggr_cnt] = (ut_config->test_pkt_len + sizeof(struct wq_sdio_adma_pkt_hdr));

		pkt_hdr = (struct wq_sdio_adma_pkt_hdr *)skb_put(req->skb, sizeof(struct wq_sdio_adma_pkt_hdr));
		pkt_hdr->len = cpu_to_le32(ut_config->test_pkt_len + sizeof(pkt_hdr->virt_qid));
		pkt_hdr->virt_qid = cpu_to_le32(WQ_SDIO_VQID_UT_PKT);

		skb_put(req->skb, ut_config->test_pkt_len);
		req->aggr_cnt++;
	}

exit:
	spin_unlock_irqrestore(&(wq_func->q.aggrout.lock), flags);

	return 0;
}

int wq_sdio_ut_rx_msg(struct wq_sdio *wq_sdio, struct sk_buff *skb)
{
	struct wq_func *wq_func = &wq_sdio->wlan;
	wq_ut_config_t *ut_config = &wq_sdio->ut_config;
	wq_msg_header_t *msg_hdr = (wq_msg_header_t *)skb->data;

	WQ_DBG(DM_TRBUS, DL_ERR, "%s: magic 0x%x, version %d, msg_type %d\n", __FUNCTION__,
		be16_to_cpu(msg_hdr->magic), be16_to_cpu(msg_hdr->version), be16_to_cpu(msg_hdr->msg_type));

	wq_sdio->ut_stat.time_start = ktime_get();
	wq_sdio->ut_stat.running = 1;
	wq_sdio->ut_stat.tx_pkt_total_bytes = 0;
	wq_sdio->ut_stat.rx_pkt_total_bytes = 0;

	if (ut_config->test_tx) {
		WQ_DBG(DM_GENERIC, DL_ERR, "TX speed test START: test_pkt_len %d, test_pkt_cnt %d, test_total_time %d\n",
			ut_config->test_pkt_len, ut_config->test_pkt_cnt, ut_config->test_total_time);

		wq_func_main_trigger(wq_func);
	} else {
		WQ_DBG(DM_GENERIC, DL_ERR, "RX speed test START: test_pkt_len %d, test_pkt_cnt %d, test_total_time %d\n",
			ut_config->test_pkt_len, ut_config->test_pkt_cnt, ut_config->test_total_time);
	}

	mod_timer(&wq_sdio->test_timer, (jiffies + HZ));

	dev_kfree_skb_any(skb);

	return 0;
}

int wq_sdio_ut_print_stat(struct wq_sdio *wq_sdio)
{
	wq_ut_config_t *ut_config = &wq_sdio->ut_config;
	ktime_t now, diff;
	uint32_t time_sec;

	now = ktime_get();
	diff = ktime_sub(now, wq_sdio->ut_stat.time_start);
	time_sec = div_u64(ktime_to_ms(diff), 1000);

	if (ut_config->test_tx) {
		u64 tx_tp = 0;
		u64 tx_pkt_total_bytes = wq_sdio->ut_stat.tx_pkt_total_bytes;

		tx_tp = div_u64(tx_pkt_total_bytes, (1024 * 1024 * time_sec));
		tx_tp *= 8;

		WQ_DBG(DM_TRBUS, DL_ERR, "Tx throughput: %llu Mbps, %d second\n", tx_tp, time_sec);
	}

	if (ut_config->test_rx) {
		u64 rx_tp = 0;
		u64 rx_pkt_total_bytes = wq_sdio->ut_stat.rx_pkt_total_bytes;

		rx_tp = div_u64(rx_pkt_total_bytes, (1024 * 1024 * time_sec));
		rx_tp *= 8;

		WQ_DBG(DM_TRBUS, DL_ERR, "Rx throughput: %llu Mbps, %d second\n", rx_tp, time_sec);
	}

	return 0;
}