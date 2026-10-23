#define WQ_LOG_DM DM_TRBUS

#include <linux/kernel.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>

#include "sdio.h"

#include "wq_log.h"
#include "wq_ipc.h" /* NB: include it only for struct wq_ipc_header */
#include "wq_profiling.h"
#include "fw_log.h"

#ifdef CONFIG_SPACEMIT_K1_SDIO_WAR
#include "plat_spacemit_k1.h"
#endif

#define IS_SKB_DATA_ALIGNED(skb)                                               \
	IS_ALIGNED((unsigned long)(skb)->data, sizeof(u32))

#define SDIO_DEBUG
#ifdef SDIO_DEBUG

#define FUN0_READ_256 0
#define FUN2_READ 1
#define FUN2_WRITE 2

#define RX_PKT_QUEUE_MAX 200

static uint8_t last_sdio_action = 0xff;
static uint8_t last_sdio_action_2 = 0xff;
static uint8_t last_sdio_write_blocks = 0;

extern int sdio_ut_mode;

static inline void wq_sdio_last_action_push(u8 action)
{
	last_sdio_action_2 = last_sdio_action;
	last_sdio_action = action;
}

static void wq_sdio_failure_check(struct wq_func *wq_func, int status)
{
	if (last_sdio_action == FUN2_WRITE)
		WQ_DBG(DM_TRBUS, DL_ERR, "last_sdio_write_blocks=%d\n",
		       last_sdio_write_blocks);

	if (status == -ETIMEDOUT || status == -EILSEQ)
		wq_sdio_cmd52_write(wq_func->func, SDIO_CCCR_SUSPEND_INT_WUQI,
				    BIT(wq_func->func_num - 1));

	hif_dump_info(&wq_func->wq_sdio->core);
}
#endif

#if 0
/* FIXME: copy from mmc/core/sdio_ops.c */
static inline void mmc_pre_req(struct mmc_host *host, struct mmc_request *mrq)
{
	if (host->ops->pre_req)
		host->ops->pre_req(host, mrq);
}

static inline void mmc_post_req(struct mmc_host *host, struct mmc_request *mrq,
				int err)
{
	if (host->ops->post_req)
		host->ops->post_req(host, mrq, err);
}

static inline
int mmc_io_rw_extended(struct mmc_card *card, int write, unsigned fn,
		       unsigned addr, int incr_addr, u8 *buf, unsigned blocks, unsigned blksz)
{
	struct mmc_request mrq = {};
	struct mmc_command cmd = {};
	struct mmc_data data = {};
	struct scatterlist sg, *sg_ptr;
	struct sg_table sgtable;
	unsigned int nents, left_size, i;
	unsigned int seg_size = card->host->max_seg_size;
	int err;

	WARN_ON(blksz == 0);

	/* sanity check */
	if (addr & ~0x1FFFF)
		return -EINVAL;

	mrq.cmd = &cmd;
	mrq.data = &data;

	cmd.opcode = SD_IO_RW_EXTENDED;
	cmd.arg = write ? 0x80000000 : 0x00000000;
	cmd.arg |= fn << 28;
	cmd.arg |= incr_addr ? 0x04000000 : 0x00000000;
	cmd.arg |= addr << 9;
	if (blocks == 0)
		cmd.arg |= (blksz == 512) ? 0 : blksz;	/* byte mode */
	else
		cmd.arg |= 0x08000000 | blocks;		/* block mode */
	cmd.flags = MMC_RSP_SPI_R5 | MMC_RSP_R5 | MMC_CMD_ADTC;

	data.blksz = blksz;
	/* Code in host drivers/fwk assumes that "blocks" always is >=1 */
	data.blocks = blocks ? blocks : 1;
	data.flags = write ? MMC_DATA_WRITE : MMC_DATA_READ;

	left_size = data.blksz * data.blocks;
	nents = DIV_ROUND_UP(left_size, seg_size);
	if (nents > 1) {
		if (sg_alloc_table(&sgtable, nents, GFP_KERNEL))
			return -ENOMEM;

		data.sg = sgtable.sgl;
		data.sg_len = nents;

		for_each_sg(data.sg, sg_ptr, data.sg_len, i) {
			sg_set_buf(sg_ptr, buf + i * seg_size,
				   min(seg_size, left_size));
			left_size -= seg_size;
		}
	} else {
		data.sg = &sg;
		data.sg_len = 1;

		sg_init_one(&sg, buf, left_size);
	}

	mmc_set_data_timeout(&data, card);

	mmc_pre_req(card->host, &mrq);

	mmc_wait_for_req(card->host, &mrq);

	if (cmd.error)
		err = cmd.error;
	else if (data.error)
		err = data.error;
	else if (mmc_host_is_spi(card->host))
		/* host driver already reported errors */
		err = 0;
	else if (cmd.resp[0] & R5_ERROR)
		err = -EIO;
	else if (cmd.resp[0] & R5_FUNCTION_NUMBER)
		err = -EINVAL;
	else if (cmd.resp[0] & R5_OUT_OF_RANGE)
		err = -ERANGE;
	else
		err = 0;

	mmc_post_req(card->host, &mrq, err);

	if (nents > 1)
		sg_free_table(&sgtable);

	return err;
}

static inline
int __wq_sdio_cmd53_readsb0(struct sdio_func *func, u8 *buf, unsigned addr, unsigned blksz)
{
	return mmc_io_rw_extended(func->card, 0, SDIO_FUNC_0, addr, 0, buf, 0, blksz);
}

#else

static inline int __wq_sdio_cmd53_readsb0(struct sdio_func *func, u8 *buf,
					  unsigned addr, unsigned blksz)
{
	u8 fn = func->num;
	int ret;

	/* HW limitation: must read it from func0 */
	func->num = SDIO_FUNC_0;
	ret = sdio_readsb(func, buf, addr, blksz);
	func->num = fn;

	return ret;
}

#endif

static void wq_sdio_adma_info_reset(struct wq_func *wq_func)
{
	wq_func->adma.info.rx_accu_len = 0;
	wq_func->adma.info.rx_bus_len = 0;
	wq_func->adma.info.rx_len = 0;

	wq_func->adma.info.tx_need_sync = true;
	wq_func->adma.info.tx_accu_mode = false;
	wq_func->adma.info.tx_buffer_avail = 0;
	wq_func->adma.info.tx_buffer_avail_max = 0;
	wq_func->adma.info.tx_accu_cnt = 0;
	wq_func->adma.info.tx_bus_cnt = 0;
}

static int wq_sdio_adma_info_read(struct wq_func *wq_func, u8 *buf)
{
	struct sdio_func *func = wq_func->func;
	struct wq_sdio *wq_sdio = wq_func->wq_sdio;
	unsigned addr, func_num;
	u64 time_start_us = 0, time_end_us = 0;
	int ret;

#ifdef CONFIG_SPACEMIT_K1_SDIO_WAR
	wq_sdio_config_k1_autoclock(false);
#endif

	if (wq_func == &wq_sdio->wlan) {
		atomic_inc(&wq_sdio->wlan_stats.rx_adma_total_cnt);
	}

	PROFILING_SET(SW_PROF_SDIO_MAIN_RX_INFO);

	time_start_us = (u64)ktime_to_us(ktime_get());

	sdio_claim_host(func);
	if (wq_sdio->fw_cfg_mode & SDIO_FW_CFG_ADMA_FUNX_MODE) {
		addr = SDIO_ADMA_FUNX_INFO_REG;
		func_num = wq_func->func_num;
		ret = sdio_readsb(func, buf, addr, SDIO_ADMA_INFO_LEN);
	} else {
		addr = SDIO_ADMA_FUN0_INFO_REG;
		func_num = SDIO_FUNC_0;
		ret = __wq_sdio_cmd53_readsb0(func, buf, addr, SDIO_ADMA_INFO_LEN);
	}
	sdio_release_host(func);

	time_end_us = (u64)ktime_to_us(ktime_get());

	if (wq_func == &wq_sdio->wlan) {
		atomic_add((u32)(time_end_us - time_start_us), &wq_sdio->wlan_stats.main_rx_adma_time);
	}

	PROFILING_CLR(SW_PROF_SDIO_MAIN_RX_INFO);

	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "[auto]%s: func%d cmd53 read:0x%x failed(%d),last_sdio_action=%d->%d\n",
		       __func__, func_num, addr, ret, last_sdio_action_2,
		       last_sdio_action);
		wq_sdio_failure_check(wq_func, ret);
		wq_sdio_adma_info_reset(wq_func);
	}
	wq_sdio_last_action_push(FUN0_READ_256);

#ifdef CONFIG_SPACEMIT_K1_SDIO_WAR
	wq_sdio_config_k1_autoclock(true);
#endif
	return ret;
}

/* read aggregated message(s) and/or data packet(s) from a FIFO on a SDIO function */
static int wq_sdio_adma_read(struct wq_func *wq_func, void *dst, int len,
			     u32 align_len)
{
	struct sdio_func *func = wq_func->func;
	struct wq_sdio *wq_sdio = wq_func->wq_sdio;
	u64 time_start_us = 0, time_end_us = 0;
	int status;

#ifdef CONFIG_SPACEMIT_K1_SDIO_WAR
	wq_sdio_config_k1_autoclock(false);
#endif

	if (wq_func == &wq_sdio->wlan) {
		atomic_inc(&wq_sdio->wlan_stats.rx_total_cnt);
		atomic_add(len, &wq_sdio->wlan_stats.rx_pkt_total_bytes);

		if (sdio_ut_mode) {
			wq_sdio->ut_stat.rx_pkt_total_bytes += len;
		}
	}

	PROFILING_SET(SW_PROF_SDIO_MAIN_RX);

	time_start_us = (u64)ktime_to_us(ktime_get());

	sdio_claim_host(func);
	status = sdio_readsb(func, dst, len, align_len);
	sdio_release_host(func);

	time_end_us = (u64)ktime_to_us(ktime_get());

	if (wq_func == &wq_sdio->wlan) {
		atomic_add((u32)(time_end_us - time_start_us), &wq_sdio->wlan_stats.main_rx_time);
	}

	PROFILING_CLR(SW_PROF_SDIO_MAIN_RX);

	if (status) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "%s: rx_accu_len %d, rx_bus_len %d, tx_buffer_avail %d\n",
		       __func__, wq_func->adma.info.rx_accu_len,
		       wq_func->adma.info.rx_bus_len,
		       wq_func->adma.info.tx_buffer_avail);
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "[auto]%s: cmd53 read error=%d, len=%d, align_len=%d,last_sdio_action=%d->%d\n",
		       __func__, status, len, align_len, last_sdio_action_2,
		       last_sdio_action);
		wq_sdio_failure_check(wq_func, status);
		wq_sdio_adma_info_reset(wq_func);
	}

	wq_sdio_last_action_push(FUN2_READ);

#ifdef CONFIG_SPACEMIT_K1_SDIO_WAR
	wq_sdio_config_k1_autoclock(true);
#endif

	return status;
}

static int wq_sdio_adma_write(struct wq_func *wq_func, int len, void *src,
			      u32 align_len)
{
	struct sdio_func *func = wq_func->func;
	struct wq_sdio *wq_sdio = wq_func->wq_sdio;
	u64 time_start_us = 0, time_end_us = 0;
	int status;

#ifdef CONFIG_SPACEMIT_K1_SDIO_WAR
	wq_sdio_config_k1_autoclock(false);
#endif

	if (wq_func == &wq_sdio->wlan) {
		atomic_inc(&wq_sdio->wlan_stats.tx_buf_avail_wr_cnt);
		atomic_inc(&wq_sdio->wlan_stats.tx_total_cnt);
		atomic_add(len, &wq_sdio->wlan_stats.tx_pkt_total_bytes);

		if (sdio_ut_mode) {
			wq_sdio->ut_stat.tx_pkt_total_bytes += len;
		}
	}

	PROFILING_SET(SW_PROF_SDIO_MAIN_TX);

	time_start_us = (u64)ktime_to_us(ktime_get());

	sdio_claim_host(func);
	status = sdio_writesb(func, len, src, align_len);
	sdio_release_host(func);

	time_end_us = (u64)ktime_to_us(ktime_get());

	if (wq_func == &wq_sdio->wlan) {
		atomic_add((u32)(time_end_us - time_start_us), &wq_sdio->wlan_stats.main_tx_time);
	}

	PROFILING_CLR(SW_PROF_SDIO_MAIN_TX);

	if (status) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "[auto]cmd53 write error=%d, len=%d, last_sdio_action=%d->%d\n",
		       status, len, last_sdio_action_2, last_sdio_action);
		wq_sdio_failure_check(wq_func, status);
		wq_sdio_adma_info_reset(wq_func);
	}

	wq_sdio_last_action_push(FUN2_WRITE);
	last_sdio_write_blocks = align_len / WQ_SDIO_BLOCK_SIZE;

#ifdef CONFIG_SPACEMIT_K1_SDIO_WAR
	wq_sdio_config_k1_autoclock(true);
#endif
	return status;
}

static int wq_sdio_adma_info_update(struct wq_func *wq_func,
				    struct wq_sdio_adma_info_all *all)
{
	struct wq_sdio *wq_sdio = wq_func->wq_sdio;
	struct wq_sdio_adma_info adma_info;
	u8 func_num = wq_func->func_num;
	int index = func_num - 1;
	u32 tx_buffer_avail;
	wq_sdio_tx_buf_cnt_u tx_buffer_cnt_u;

	memset(&adma_info, 0, sizeof(struct wq_sdio_adma_info));

	if (!all) {
		int ret = wq_sdio_adma_info_read(wq_func,
						 (u8 *)wq_func->adma.all_info);
		if (ret) {
			WQ_DBG(DM_TRBUS, DL_ERR, "%s: get adma info failed\n",
			       __func__);
			return ret;
		}
		all = wq_func->adma.all_info;

		if (!(wq_sdio->fw_cfg_mode & SDIO_FW_CFG_ADMA_FUNX_MODE)) {
			func_num = SDIO_FUNC_0;
		}
	}

	adma_info.rx_accu_len = __le32_to_cpu(all->rx_accu_len[index]);
	adma_info.rx_bus_len = __le32_to_cpu(all->rx_bus_len[index]);
	wq_sdio_adma_rx_len(&adma_info);

	tx_buffer_avail = __le32_to_cpu(all->tx_buffer_avail[index]);

	if (wq_func->adma.info.tx_need_sync) {
		if (wq_sdio->fw_cfg_mode & SDIO_FW_CFG_TX_ACCU_MODE) {
			adma_info.tx_accu_mode = true;
			adma_info.tx_accu_cnt = (tx_buffer_avail & SDIO_ADMA_TX_ACCU_LEN_MASK);

			switch (wq_func->func_num)
			{
				case SDIO_FUNC_1:
					tx_buffer_cnt_u.buf_cnt_32 = __le32_to_cpu(all->fn1_tx_buf_cnt.buf_cnt_32);
					break;
				case SDIO_FUNC_2:
					tx_buffer_cnt_u.buf_cnt_32 = __le32_to_cpu(all->fn2_tx_buf_cnt.buf_cnt_32);
					break;
				case SDIO_FUNC_3:
					tx_buffer_cnt_u.buf_cnt_32 = __le32_to_cpu(all->fn3_tx_buf_cnt.buf_cnt_32);
					break;
				default:
					tx_buffer_cnt_u.buf_cnt_32 = 0;
					break;
			}

			adma_info.tx_bus_cnt = tx_buffer_cnt_u.buf_cnt_s.used_buf_cnt;
			adma_info.tx_buffer_avail = wq_sdio_adma_tx_len(adma_info.tx_accu_cnt, adma_info.tx_bus_cnt);
			adma_info.tx_need_sync = false;
		} else {
			if (tx_buffer_avail & SDIO_ADMA_TX_ACCU_SOFT_MODE) {
				adma_info.tx_accu_mode = true;
				if (tx_buffer_avail & SDIO_ADMA_TX_ACCU_PKT_MAX_MASK) {
					adma_info.tx_accu_cnt = ((tx_buffer_avail >> 16) & 0xFF);
					adma_info.tx_bus_cnt = ((tx_buffer_avail >> 8) & 0xFF);
					adma_info.tx_buffer_avail = wq_sdio_adma_tx_len(adma_info.tx_accu_cnt, adma_info.tx_bus_cnt);
					adma_info.tx_need_sync = false;
				} else {
					adma_info.tx_need_sync = true;
				}
			} else {
				adma_info.tx_accu_mode = false;
				adma_info.tx_buffer_avail = tx_buffer_avail;
				adma_info.tx_need_sync = false;
			}
		}

		WQ_DBG(DM_TRBUS, DL_ERR, "%s[func%d/%d]: tx_need_sync %d, tx_accu_mode %d, tx_buffer_avail %d, host: tx_accu_cnt 0x%x, tx_bus_cnt 0x%x, FW: tx_accu_cnt 0x%x, tx_bus_cnt 0x%x, tx_buffer_avail 0x%x\n",
		       __func__, func_num, wq_func->func_num, adma_info.tx_need_sync, adma_info.tx_accu_mode, adma_info.tx_buffer_avail,
		       wq_func->adma.info.tx_accu_cnt, wq_func->adma.info.tx_bus_cnt, adma_info.tx_accu_cnt, adma_info.tx_bus_cnt, tx_buffer_avail);
	} else {
		if (wq_sdio->fw_cfg_mode & SDIO_FW_CFG_TX_ACCU_MODE) {
			adma_info.tx_accu_mode = true;
			adma_info.tx_accu_cnt = (tx_buffer_avail & SDIO_ADMA_TX_ACCU_LEN_MASK);
			adma_info.tx_bus_cnt = wq_func->adma.info.tx_bus_cnt;
			adma_info.tx_buffer_avail = wq_sdio_adma_tx_len(adma_info.tx_accu_cnt, adma_info.tx_bus_cnt);
		} else {
			if (tx_buffer_avail & SDIO_ADMA_TX_ACCU_SOFT_MODE) {
				adma_info.tx_accu_mode = true;
				adma_info.tx_accu_cnt = ((tx_buffer_avail >> 16) & 0xFF);
				adma_info.tx_bus_cnt = wq_func->adma.info.tx_bus_cnt;
				adma_info.tx_buffer_avail = wq_sdio_adma_tx_len(adma_info.tx_accu_cnt, adma_info.tx_bus_cnt);
			} else {
				adma_info.tx_accu_mode = false;
				adma_info.tx_buffer_avail = tx_buffer_avail;
			}
		}
	}

	if (adma_info.tx_buffer_avail > wq_func->adma.info.tx_buffer_avail_max) {
		adma_info.tx_buffer_avail_max = adma_info.tx_buffer_avail;
	} else {
		adma_info.tx_buffer_avail_max = wq_func->adma.info.tx_buffer_avail_max;
	}

	if ((adma_info.tx_buffer_avail <= SDIO_ADMA_TX_AVAIL_BUF_MAX) &&
			(adma_info.rx_len <= SDIO_ADMA_RX_LEN_MAX)) {
		WQ_DBG(DM_TRBUS, DL_VRB,
			"%s[func%d/%d]: TX: 0x%x - 0x%x = %d pkt, RX: 0x%x - 0x%x = 0x%x bytes\n",
			__func__, func_num, wq_func->func_num,
			adma_info.tx_accu_cnt, adma_info.tx_bus_cnt, adma_info.tx_buffer_avail,
			adma_info.rx_accu_len, adma_info.rx_bus_len, adma_info.rx_len);
		wq_func->adma.info = adma_info;
		return 0;
	}

	WQ_DBG(DM_TRBUS, DL_ERR,
	       "%s[func%d/%d]: pre tx_need_sync %d, tx_accu_mode %d, rx_accu_len: 0x%x, rx_bus_len: 0x%x, rx_len: %d, tx_accu_cnt: 0x%x, tx_bus_cnt: 0x%x, tx_buffer_avail: %d, tx_buffer_avail_max %d\n",
	       __func__, func_num, wq_func->func_num, wq_func->adma.info.tx_need_sync, wq_func->adma.info.tx_accu_mode, wq_func->adma.info.rx_accu_len,
	       wq_func->adma.info.rx_bus_len, wq_func->adma.info.rx_len,
	       wq_func->adma.info.tx_accu_cnt,wq_func->adma.info.tx_bus_cnt, wq_func->adma.info.tx_buffer_avail, wq_func->adma.info.tx_buffer_avail_max);

	WQ_DBG(DM_TRBUS, DL_ERR,
	       "%s[func%d/%d]: new tx_need_sync %d, tx_accu_mode %d, rx_accu_len: 0x%x, rx_bus_len: 0x%x, rx_len: %d, tx_accu_cnt: 0x%x, tx_bus_cnt: 0x%x, tx_buffer_avail: %d, tx_buffer_avail_max %d\n",
	       __func__, func_num, wq_func->func_num, wq_func->adma.info.tx_need_sync, wq_func->adma.info.tx_accu_mode, adma_info.rx_accu_len, adma_info.rx_bus_len, adma_info.rx_len,
	       adma_info.tx_accu_cnt, adma_info.tx_bus_cnt, adma_info.tx_buffer_avail, adma_info.tx_buffer_avail_max);
	return -EINVAL;
}

static struct sk_buff *wq_sdio_skb_extract(struct sk_buff *skb, unsigned len)
{
	struct sk_buff *skb2 = NULL;

	if (!IS_SKB_DATA_ALIGNED(skb)) {
		WQ_DBG(DM_TRBUS, DL_VRB, "%s: data is not aligned (%p).\n",
		       __func__, skb->data);
	} else {
#ifdef SDIO_USE_SKB_CLONE
		BUG_ON(irq_count());
		skb2 = skb_clone(skb, irq_count() ? GFP_ATOMIC : GFP_KERNEL);
		if (skb2) {
			skb_trim(skb2, len);
			__skb_pull(skb, len);
			WQ_DBG(DM_TRBUS, DL_VRB,
			       "%s: clone %d, remainder %d@%p.\n", __func__,
			       len, skb->len, skb->data);
			return skb2;
		}
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: failed to clone skb (%d).\n",
		       __func__, len);
#endif
	}

	skb2 = dev_alloc_skb(len);
	if (!skb2)
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: no skb, but extract %d bytes\n",
		       __func__, len);
	else
		WQ_DBG(DM_TRBUS, DL_VRB, "%s: copy %d bytes to new skb (%p).\n",
		       __func__, len, skb2->data);

	if (skb2) {
		memcpy(skb2->data, skb->data, len);
		skb_put(skb2, len);
		BUG_ON(skb2->len != len);
	}
	__skb_pull(skb, len);

	return skb2;
}

static int wq_sdio_adma_deaggr(struct wq_sdio *wq_sdio, struct sk_buff *skb,
			       struct sk_buff_head *skbq)
{
	union {
		struct wq_sdio_bmi_msg bmi;
		wq_msg_header_t msg_hdr;
		struct wq_htc_v0 htc_v0;
		struct {
			struct wq_hif_hdr hif_hdr;
			struct wq_htc_v0 htc_v0;
		} if_hdr;
		u8 bytes[0];
	} * hdr;

	int aggr_len = 0;

	while (true) {
		struct sk_buff *skb2 = NULL;
		unsigned int pkt_len;
		unsigned int len = skb->len;

		/* nothing to do, if data is too short */
		if (len < sizeof(hdr->bmi.magic))
			break;

		/* prepare headers */
		hdr = (void *)skb->data;

		if (hdr->msg_hdr.magic == htons(WQ_INTERFACE_MSG_MAGIC)) {
			if (len < sizeof(hdr->msg_hdr)) {
				WQ_DBG(DM_TRBUS, DL_ERR,
				       "%s: len %d < hdr len %d! [%*ph]\n",
				       __func__, len,
				       (unsigned)sizeof(hdr->msg_hdr), len,
				       &hdr->msg_hdr);
				break;
			}

			pkt_len = ntohs(hdr->msg_hdr.msg_length);
			if (len < pkt_len) {
				WQ_DBG(DM_TRBUS, DL_ERR,
				       "%s: len %d < pkt len %d! [%*ph ...]\n",
				       __func__, len, pkt_len,
				       (unsigned)sizeof(hdr->msg_hdr), &hdr->msg_hdr);
				break;
			}

			skb2 = wq_sdio_skb_extract(skb, pkt_len);
			if (skb2) {
				if (hdr->msg_hdr.msg_type == htons(MSG_TYPE_LOG)) {
					wq_fw_log_push(&wq_sdio->core, skb2, skb2->len);
				} else if (hdr->msg_hdr.msg_type == htons(MSG_TYPE_UT_TP)) {
					wq_sdio_ut_rx_msg(wq_sdio, skb2);
				}

				aggr_len += pkt_len;
			}
		} else if (hdr->bmi.magic == htonl(WQ_SDIO_BMI_MSG_MAGIC)) {
			if (len < sizeof(hdr->bmi)) {
				WQ_DBG(DM_TRBUS, DL_ERR,
				       "%s: len %d < hdr len %d! [%*ph]\n",
				       __func__, len,
				       (unsigned)sizeof(hdr->bmi), len,
				       &hdr->bmi);
				break;
			}

			WQ_DBG(DM_TRBUS, DL_INF, "%s: bmi msg len %d\n", __func__, ntohs(hdr->bmi.length));

			pkt_len = sizeof(struct wq_sdio_bmi_msg) +
				  ntohs(hdr->bmi.length);
			if (len < pkt_len) {
				WQ_DBG(DM_TRBUS, DL_ERR,
				       "%s: len %d < pkt len %d! [%*ph ...]\n",
				       __func__, len, pkt_len,
				       (unsigned)sizeof(hdr->bmi), &hdr->bmi);
				break;
			}

			skb2 = wq_sdio_skb_extract(skb, pkt_len);
			if (skb2) {
				wq_sdio_rx_bmi_msg(wq_sdio, skb2);
				aggr_len += pkt_len;
			}
		} else if (len < sizeof(hdr->htc_v0)) {
			WQ_DBG(DM_TRBUS, DL_ERR,
			       "%s: len %d < hdr len %d! [%*ph]\n", __func__,
			       len, (unsigned)sizeof(hdr->htc_v0), len,
			       &hdr->htc_v0);
			break;
		} else if ((WQ_HIF_HDR_MAGIC == hdr->if_hdr.hif_hdr.ptn) &&
			   (len < ((hdr->if_hdr.hif_hdr.dw_len) << 2))) {
			WQ_DBG(DM_TRBUS, DL_ERR,
			       "%s: len %d < if hdr len %d! [%*ph]\n", __func__,
			       len, (unsigned)sizeof(hdr->if_hdr), len,
			       &hdr->if_hdr);
			break;
		} else {
			u8 ipc_type;

			if (WQ_HIF_HDR_MAGIC == hdr->if_hdr.hif_hdr.ptn) {
				ipc_type = WQ_IPC_TPE(
					le32_to_cpu(hdr->if_hdr.htc_v0.flags));
				pkt_len = hdr->if_hdr.hif_hdr.dw_len << 2;
			} else {
				ipc_type = WQ_IPC_TPE(
					le32_to_cpu(hdr->htc_v0.flags));
				pkt_len = sizeof(struct wq_htc_v0) +
					  le32_to_cpu(hdr->htc_v0.buf_len);
			}

			if (len < pkt_len) {
				WQ_DBG(DM_TRBUS, DL_ERR,
				       "%s: len %d < pkt len %d! [%*ph ...]\n",
				       __func__, len, pkt_len,
				       (unsigned)sizeof(hdr->htc_v0),
				       &hdr->htc_v0);
				break;
			}
			skb2 = wq_sdio_skb_extract(skb, pkt_len);
			if (skb2) {
				aggr_len += pkt_len;
				atomic_inc(&wq_sdio->wlan_stats.rx_pkt_total_num);

				switch (ipc_type) {
				case WQ_IPC_TPE_EVT:
					htc_rx(&wq_sdio->core, WQ_QID_MSG,
					       skb2);
					break;
				case WQ_IPC_TPE_PKT:
					__skb_queue_tail(skbq, skb2);
					break;
				default:
					WQ_DBG(DM_TRBUS, DL_ERR,
					       "%s: invalid IPC type %d! [%*ph ...]\n",
					       __func__, ipc_type,
					       (unsigned)sizeof(hdr->htc_v0),
					       &hdr->htc_v0);
					dev_kfree_skb_any(skb2);
					break;
				}
			}
		}

		/* no more packet */
		if (!skb->len) {
			skb_push(skb, aggr_len);
			return 0;
		}
	}

	WQ_DBG(DM_TRBUS, DL_ERR, "%s: remainder %d! [%*ph ...]\n", __func__,
	       skb->len, skb->len, skb->data);
	skb_push(skb, aggr_len);
	return skb->len;
}

#ifdef SDIO_RX_AGGR_MODE
void wq_sdio_adma_rx_process_one(struct wq_func *wq_func)
{
	struct wq_sdio *wq_sdio;
	struct wq_skbreq *req;
	struct sk_buff_head sk_list;
	u64 time_start_us = 0, time_end_us = 0;

	WQ_DBG(WQ_LOG_DM, DL_INF, "--> %s(%d)\n", __func__,
	       wq_func->func_num);

	if (!(wq_sdio = wq_func->wq_sdio)) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: wq_sdio %p or func is NULL!\n",
		       __func__, wq_sdio);
		return;
	}

	__skb_queue_head_init(&sk_list);

	req = wq_skbreq_dequeue(&wq_func->q.aggrin);
	if (req) {
		time_start_us = (u64)ktime_to_us(ktime_get());

		wq_sdio_adma_deaggr(wq_sdio, req->skb, &sk_list);

		time_end_us = (u64)ktime_to_us(ktime_get());

		if (wq_func == &wq_sdio->wlan) {
			atomic_add((u32)(time_end_us - time_start_us), &wq_sdio->wlan_stats.rx_deaggr_time);
		}

		if (!skb_queue_empty(&sk_list)) {
			time_start_us = (u64)ktime_to_us(ktime_get());

			htc_rxq(&wq_sdio->core, &sk_list);

			time_end_us = (u64)ktime_to_us(ktime_get());

			if (wq_func == &wq_sdio->wlan) {
				atomic_add((u32)(time_end_us - time_start_us), &wq_sdio->wlan_stats.rx_htc_time);
			}
		}

		wq_skbreq_free(&wq_sdio->pools.aggrin, req);
	}
}

void wq_sdio_adma_rx_process_one_internal(struct wq_func *wq_func)
{
	struct wq_sdio *wq_sdio;
	struct wq_skbreq *req;
	struct sk_buff_head sk_list;

	WQ_DBG(WQ_LOG_DM, DL_INF, "--> %s(%d)\n", __func__,
	       wq_func->func_num);

	if (!(wq_sdio = wq_func->wq_sdio)) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: wq_sdio %p or func is NULL!\n",
		       __func__, wq_sdio);
		return;
	}

	__skb_queue_head_init(&sk_list);

	req = wq_skbreq_dequeue(&wq_func->q.aggrin);
	if (req) {
		wq_sdio_adma_deaggr(wq_sdio, req->skb, &sk_list);

		htc_rxq_internal(&wq_sdio->core, &sk_list);

		wq_skbreq_free(&wq_sdio->pools.aggrin, req);
	}
}
#else
void wq_sdio_adma_rx_process_one(struct wq_func *wq_func, struct sk_buff *skb)
{
	struct wq_sdio *wq_sdio;
	struct sk_buff_head sk_list;

	WQ_DBG(WQ_LOG_DM, WQ_LOG_LEVEL, "--> %s(%d)\n", __func__,
	       wq_func->func_num);

	WQ_DBG(WQ_LOG_DM, DL_VRB, "--> %s(%d)\n", __func__, wq_func->func_num);
	if (!(wq_sdio = wq_func->wq_sdio)) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: wq_sdio %p or func is NULL!\n",
		       __func__, wq_sdio);
		return;
	}

	__skb_queue_head_init(&sk_list);

	if (skb) {
		wq_sdio_adma_deaggr(wq_sdio, skb, &sk_list);

		if (!skb_queue_empty(&sk_list)) {
			htc_rxq(&wq_sdio->core, &sk_list);
		}
	}
}
#endif

/* main adma RX API. return < 0 if ADMA info is out of date */
#ifdef SDIO_RX_AGGR_MODE
static int wq_sdio_adma_rx(struct wq_func *wq_func)
{
	int adma_cnt = 0;
	u32 rx_len = wq_func->adma.info.rx_len;
	int ret = 0;
	struct sk_buff *skb = NULL;
	struct wq_skbreq *req = NULL;
	struct wq_sdio *wq_sdio = wq_func->wq_sdio;
	unsigned long flags;

	/**
	 * fix: SDIO_ADMA_INFO_LEN not aligned to block size
	 *
	 * SDIO_ADMA_INFO_LEN also need to be aligned to block size
	*/
	u32 align_len = ALIGN(rx_len, WQ_SDIO_BLOCK_SIZE) + ALIGN(SDIO_ADMA_INFO_LEN, WQ_SDIO_BLOCK_SIZE);

	BUILD_BUG_ON(ALIGN(SDIO_ADMA_INFO_LEN, WQ_SDIO_BLOCK_SIZE) != WQ_SDIO_BLOCK_SIZE);
	if (align_len > SDIO_ADMA_RX_LEN_MAX) {
		WQ_DBG(DM_TRBUS, DL_WRN, "%s: align_len %d is over %dK.\n",
				__func__, align_len, SDIO_ADMA_RX_LEN_MAX >> 10);
		rx_len = SDIO_ADMA_RX_LEN_MAX - SDIO_ADMA_INFO_LEN;
		align_len = SDIO_ADMA_RX_LEN_MAX;
	}

	spin_lock_irqsave(&(wq_func->q.aggrin.lock), flags);

	req = __wq_skbreq_peek_last(&wq_func->q.aggrin);
	if (req && (skb_tailroom(req->skb) >= align_len)) {
		req = __wq_skbreq_dequeue_last(&wq_func->q.aggrin);
	} else {
		req = NULL;
	}
	spin_unlock_irqrestore(&(wq_func->q.aggrin.lock), flags);

	if (!req) {
		req = wq_skbreq_alloc(&wq_sdio->pools.aggrin);
		if (!req) {
			printk_ratelimited(KERN_INFO "%s: req is NULL!\n",
					   __func__);
			wq_sdio_adma_rx_process_one_internal(wq_func);
			goto exit;
		}
		skb_trim(req->skb, 0);

		WQ_DBG(DM_TRBUS, DL_INF, "%s: new skb tail room %d, rx_len %d, align_len %d\n", __func__, skb_tailroom(req->skb), rx_len, align_len);
	} else {
		WQ_DBG(DM_TRBUS, DL_INF, "%s: old skb tail room %d, rx_len %d, align_len %d\n", __func__, skb_tailroom(req->skb), rx_len, align_len);
	}

	skb = req->skb;

	if (!skb) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: skb is NULL!\n", __func__);
		goto exit;
	}

	if (wq_sdio->wlan_stats.rx_aggr_avg_cnt) {
		wq_sdio->wlan_stats.rx_aggr_avg_cnt = (wq_sdio->wlan_stats.rx_aggr_avg_cnt + wq_sdio->pools.aggrin.num - wq_sdio->pools.aggrin.list.num) / 2;
	} else {
		wq_sdio->wlan_stats.rx_aggr_avg_cnt = (wq_sdio->pools.aggrin.num - wq_sdio->pools.aggrin.list.num);
	}

	WQ_DBG(DM_TRBUS, DL_INF, "%s: addr(len) %d, dma len %d\n",
			__func__, rx_len, align_len);

	ret = wq_sdio_adma_read(wq_func, skb_tail_pointer(skb), rx_len, align_len);
	if (ret) {
		wq_skbreq_free(&wq_sdio->pools.aggrin, req);
		goto exit;
	}

	++adma_cnt;
	ret = wq_sdio_adma_info_update(wq_func,
			(void*) (skb_tail_pointer(skb) + ALIGN(rx_len, sizeof(u32))));

	WQ_DBG(DM_TRBUS, DL_INF, "%s: skb tail room %d, rx_len %d, align_len %d\n", __func__, skb_tailroom(skb), rx_len, align_len);

	if (sdio_ut_mode && (wq_func == &wq_sdio->wlan)) {
		wq_ut_stat_t *ut_stat = &wq_sdio->ut_stat;
		wq_ut_config_t *ut_config = &wq_sdio->ut_config;
		if (ut_stat->running && ut_config->test_rx) {
			wq_skbreq_free(&wq_sdio->pools.aggrin, req);
			goto exit;
		}
	}

	skb_put(skb, rx_len); /* trim ADMA info at the end */
	wq_skbreq_enqueue(&wq_func->q.aggrin, req);
	wq_func_rx_trigger(wq_func);

	WQ_DBG(DM_TRBUS, DL_INF, "%s: adma_cnt %d\n", __func__, adma_cnt);

exit:
	return ret;
}
#else
static int wq_sdio_adma_rx(struct wq_func *wq_func)
{
	int adma_cnt = 0;
	u32 rx_len = wq_func->adma.info.rx_len;
	int ret = 0;
	struct sk_buff *skb = wq_func->adma.aggr.rx;

	/**
	 * fix: SDIO_ADMA_INFO_LEN not aligned to block size
	 *
	 * SDIO_ADMA_INFO_LEN also need to be aligned to block size
	*/
	u32 align_len = ALIGN(rx_len, WQ_SDIO_BLOCK_SIZE) + ALIGN(SDIO_ADMA_INFO_LEN, WQ_SDIO_BLOCK_SIZE);

	BUILD_BUG_ON(ALIGN(SDIO_ADMA_INFO_LEN, WQ_SDIO_BLOCK_SIZE) != WQ_SDIO_BLOCK_SIZE);
	if (align_len > SDIO_ADMA_RX_LEN_MAX) {
		WQ_DBG(DM_TRBUS, DL_WRN, "%s: align_len %d is over %dK.\n",
				__func__, align_len, SDIO_ADMA_RX_LEN_MAX >> 10);
		rx_len = SDIO_ADMA_RX_LEN_MAX - SDIO_ADMA_INFO_LEN;
		align_len = SDIO_ADMA_RX_LEN_MAX;
	}

	if (!skb) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: skb is NULL!\n", __func__);
		goto exit;
	}
	skb_trim(skb, 0);

	WQ_DBG(DM_TRBUS, DL_INF, "%s: addr(len) %d, dma len %d\n",
			__func__, rx_len, align_len);
	ret = wq_sdio_adma_read(wq_func, skb->data, rx_len, align_len);
	if (ret) {
		goto exit;
	}

	++adma_cnt;
	ret = wq_sdio_adma_info_update(wq_func,
			(void*) (skb->data + ALIGN(rx_len, sizeof(u32))));

	skb_put(skb, rx_len); /* trim ADMA info at the end */
	wq_sdio_adma_rx_process_one(wq_func, skb);

	WQ_DBG(DM_TRBUS, DL_INF, "%s: adma_cnt %d\n", __func__, adma_cnt);

exit:
	return ret;
}
#endif

#ifdef SDIO_TX_AGGR_MODE
static int __wq_sdio_adma_aggr(struct sk_buff *aggr, struct wq_list_head *q,
			       struct wq_list_head *done, int max_pkts)
{
	struct wq_skbreq *req;
	int pkts;
	unsigned long flags;

	spin_lock_irqsave(&(q->lock), flags);

	for (pkts = 0; pkts < max_pkts && (req = __wq_skbreq_dequeue(q));
	     ++pkts) {
		struct sk_buff *skb = req->skb;
		struct wq_sdio_adma_pkt_hdr hdr = {
			.len = cpu_to_le32(skb->len + sizeof(hdr.virt_qid)),
			.virt_qid = cpu_to_le32(req->virt_qid),
		};
		u32 len = sizeof(hdr) + skb->len;

		WQ_DBG(DM_TRBUS, DL_VRB, "%s: type %d, len %d\n", __func__,
		       req->virt_qid, skb->len);

		if (skb_tailroom(aggr) < len) {
			WQ_DBG(DM_TRBUS, DL_WRN,
			       "%s: un-pop it (tail room %d < %d)!\n", __func__,
			       skb_tailroom(aggr), len);
			__wq_skbreq_unpop(q, req);
			break;
		}

		/* FIXME: padding for alignment? */
		memcpy(skb_tail_pointer(aggr), &hdr, sizeof(hdr));
		memcpy(skb_tail_pointer(aggr) + sizeof(hdr), skb->data,
		       skb->len);
		skb_put(aggr, len);

		__wq_skbreq_enqueue(done, req);
	}

	spin_unlock_irqrestore(&(q->lock), flags);

	return pkts;
}

static int wq_sdio_adma_msg_aggr(struct wq_func *wq_func,
			     struct sk_buff *msg_aggr,
			     struct wq_list_head *msg_aggr_done,
			     int max_pkts)
{
	int pkts = 0;

	skb_trim(msg_aggr, 0);

	pkts = __wq_sdio_adma_aggr(msg_aggr, &wq_func->q.msgout,
					    msg_aggr_done, max_pkts);
	return pkts;
}

static void wq_sdio_adma_tx_msg_done(struct wq_sdio *wq_sdio,
				 struct wq_func *wq_func,
				 struct wq_list_head *msg_aggr_done,
				 int status)
{
	struct wq_skbreq *req;
	unsigned long flags;
	struct sk_buff_head sk_list_wlan;

	__skb_queue_head_init(&sk_list_wlan);

	spin_lock_irqsave(&(wq_sdio->pools.msgout.list.lock), flags);

	/* msg tx done */
	while ((req = __wq_skbreq_dequeue(msg_aggr_done)) != NULL) {
		if (sdio_ut_mode && (wq_func == &wq_sdio->wlan)) {
			dev_kfree_skb_any(req->skb);
		} else {
			if ((wq_func == &wq_sdio->wlan) || (wq_func == &wq_sdio->wlan_msg))
				__skb_queue_tail(&sk_list_wlan, req->skb);
			else
				dev_kfree_skb_any(req->skb);
		}

		req->skb = NULL;
		__wq_skbreq_free(&wq_sdio->pools.msgout, req);
	}

	spin_unlock_irqrestore(&(wq_sdio->pools.msgout.list.lock), flags);

	if (((wq_func == &wq_sdio->wlan) || (wq_func == &wq_sdio->wlan_msg))
			&& skb_queue_len(&sk_list_wlan)) {
		htc_txq_done(&wq_sdio->core, &sk_list_wlan, status);
	}
}

int wq_sdio_adma_tx_msg_aggr(struct wq_func *wq_func, struct sk_buff *msg_aggr, int max_pkts)
{
	struct wq_sdio *wq_sdio = wq_func->wq_sdio;
	struct wq_list_head msg_aggr_done;
	int pkts;

	PROFILING_SET(SW_PROF_SDIO_TX_AGGR);

	INIT_WQ_LIST_HEAD(&msg_aggr_done);

	if (!wq_list_is_empty(&wq_func->q.msgout) &&
	    ((wq_func == &wq_sdio->wlan) || (wq_func == &wq_sdio->wlan_msg))) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "%s[%s]: rx_accu_len: 0x%x, rx_bus_len: 0x%x, tx_buffer_avail: 0x%x\n",
		       __func__, wq_func->name, wq_func->adma.info.rx_accu_len,
		       wq_func->adma.info.rx_bus_len,
		       wq_func->adma.info.tx_buffer_avail);
	}

	pkts = wq_sdio_adma_msg_aggr(wq_func, msg_aggr, &msg_aggr_done, max_pkts);
	if (!pkts) {
		goto exit;
	}

	wq_sdio_adma_tx_msg_done(wq_sdio, wq_func, &msg_aggr_done, 0);

exit:
	PROFILING_CLR(SW_PROF_SDIO_TX_AGGR);

	return pkts;
}

static int __wq_sdio_adma_pkt_aggr(struct wq_skbreq *req_aggr, struct wq_list_head *q,
			       struct wq_list_head *done, int max_pkts)
{
	struct wq_skbreq *req;
	struct sk_buff *aggr = req_aggr->skb;
	int pkts;
	unsigned long flags;

	spin_lock_irqsave(&(q->lock), flags);

	for (pkts = 0; pkts < max_pkts && (req = __wq_skbreq_dequeue(q)); ++pkts) {
		struct sk_buff *skb = req->skb;
		struct wq_sdio_adma_pkt_hdr hdr = {
			.len = cpu_to_le32(skb->len + sizeof(hdr.virt_qid)),
			.virt_qid = cpu_to_le32(req->virt_qid),
		};
		u32 len = sizeof(hdr) + skb->len;

		WQ_DBG(DM_TRBUS, DL_VRB, "%s: type %d, len %d\n", __func__,
		       req->virt_qid, skb->len);

		if (skb_tailroom(aggr) < len) {
			WQ_DBG(DM_TRBUS, DL_WRN,
			       "%s: un-pop it (tail room %d < %d)!\n", __func__,
			       skb_tailroom(aggr), len);
			__wq_skbreq_unpop(q, req);
			break;
		}

		req_aggr->aggr_addr[req_aggr->aggr_cnt] = skb_tail_pointer(aggr);
		req_aggr->aggr_len[req_aggr->aggr_cnt] = len;
		req_aggr->aggr_cnt++;

		/* FIXME: padding for alignment? */
		memcpy(skb_tail_pointer(aggr), &hdr, sizeof(hdr));
		memcpy(skb_tail_pointer(aggr) + sizeof(hdr), skb->data, skb->len);
		skb_put(aggr, len);

		__wq_skbreq_enqueue(done, req);
	}

	spin_unlock_irqrestore(&(q->lock), flags);

	return pkts;
}

static int wq_sdio_adma_pkt_aggr(struct wq_func *wq_func,
				struct wq_skbreq *req_aggr,
				struct wq_list_head *pktq_done,
				int max_pkts)
{
	int pkts = 0;

	pkts = __wq_sdio_adma_pkt_aggr(req_aggr, &wq_func->q.pktout, pktq_done, max_pkts);

	return pkts;
}

static void wq_sdio_adma_tx_pkt_done(struct wq_sdio *wq_sdio,
				 struct wq_func *wq_func,
				 struct wq_list_head *pktq_done,
				 int status)
{
	struct wq_skbreq *req;
	unsigned long flags;
	struct sk_buff_head sk_list;

	__skb_queue_head_init(&sk_list);

	/* pkt tx done */
	spin_lock_irqsave(&(wq_sdio->pools.pktout.list.lock), flags);

	while ((req = __wq_skbreq_dequeue(pktq_done)) != NULL) {
		WQ_DBG(DM_TRBUS, DL_INF, "%s, type %d, len %d\n", __func__,
		       req->virt_qid, req->skb->len);

		__skb_queue_tail(&sk_list, req->skb);

		req->skb = NULL;
		__wq_skbreq_free(&wq_sdio->pools.pktout, req);
	}

	spin_unlock_irqrestore(&(wq_sdio->pools.pktout.list.lock), flags);

	htc_txq_done(&wq_sdio->core, &sk_list, status);
}

int wq_sdio_adma_tx_pkt_aggr_one(struct wq_func *wq_func, int max_pkts)
{
	struct wq_sdio *wq_sdio = wq_func->wq_sdio;
	struct wq_skbreq *req_aggr = NULL;
	struct wq_list_head pktq_done;
	int pkts = 0, aggr_max_pkts;
	unsigned long flags;

	INIT_WQ_LIST_HEAD(&pktq_done);

	spin_lock_irqsave(&(wq_func->q.aggrout.lock), flags);

	req_aggr = __wq_skbreq_peek_last(&wq_func->q.aggrout);
	if ((req_aggr == NULL) || (req_aggr->aggr_cnt == SDIO_ADMA_TX_MAX_AGGR_SIZE)) {
		req_aggr = wq_skbreq_dequeue(&wq_sdio->pools.aggrout.list);
		if (!req_aggr) {
			printk_ratelimited(KERN_INFO "%s: req_aggr is null, max_pkts %d/%d, func aggrout num %d, func pktout num %d\n", __func__,
				max_pkts, SDIO_ADMA_TX_MAX_AGGR_SIZE, wq_func->q.aggrout.num, wq_func->q.pktout.num);
			goto exit;
		}

		req_aggr->aggr_cnt = 0;
		req_aggr->aggr_consumed_cnt = 0;
		__wq_skbreq_enqueue(&wq_func->q.aggrout, req_aggr);
	}

	if (wq_sdio->wlan_stats.tx_aggr_avg_cnt) {
		wq_sdio->wlan_stats.tx_aggr_avg_cnt = (wq_sdio->wlan_stats.tx_aggr_avg_cnt + wq_sdio->pools.aggrout.num - wq_sdio->pools.aggrout.list.num) / 2;
	} else {
		wq_sdio->wlan_stats.tx_aggr_avg_cnt = (wq_sdio->pools.aggrout.num - wq_sdio->pools.aggrout.list.num);
	}

	BUG_ON(req_aggr->aggr_cnt == SDIO_ADMA_TX_MAX_AGGR_SIZE);

	if ((req_aggr->aggr_cnt + max_pkts) > SDIO_ADMA_TX_MAX_AGGR_SIZE) {
		aggr_max_pkts = SDIO_ADMA_TX_MAX_AGGR_SIZE - req_aggr->aggr_cnt;
	} else {
		aggr_max_pkts = max_pkts;
	}

	pkts = wq_sdio_adma_pkt_aggr(wq_func, req_aggr, &pktq_done, aggr_max_pkts);
	if (!pkts) {
		goto exit;
	}
	BUG_ON(req_aggr->aggr_cnt == 0);

	wq_sdio_adma_tx_pkt_done(wq_sdio, wq_func, &pktq_done, 0);

exit:
	spin_unlock_irqrestore(&(wq_func->q.aggrout.lock), flags);

	return pkts;
}

int wq_sdio_adma_tx_pkt_aggr(struct wq_func *wq_func, int max_pkts)
{
	int pkts = 0;

	PROFILING_SET(SW_PROF_SDIO_TX_AGGR);
	if (!wq_list_is_empty(&wq_func->q.pktout)) {
		pkts += wq_sdio_adma_tx_pkt_aggr_one(wq_func, max_pkts);
	}

	if (!wq_list_is_empty(&wq_func->q.pktout)) {
		pkts += wq_sdio_adma_tx_pkt_aggr_one(wq_func, max_pkts);
	}

	PROFILING_CLR(SW_PROF_SDIO_TX_AGGR);

	return pkts;
}

static int wq_sdio_adma_tx_msg(struct wq_func *wq_func)
{
	struct wq_sdio *wq_sdio = wq_func->wq_sdio;
	struct sk_buff *msg_aggr = wq_func->adma.aggr.tx;
	int max_pkts = wq_func->adma.info.tx_buffer_avail;
	int pkts;
	u32 addr;
	u32 align_len;
	int ret = 0;

	if (!max_pkts) {
		printk_ratelimited(KERN_INFO "%s: tx_buffer_avail %d, func msgout num %d\n",
				   __func__, max_pkts, wq_func->q.msgout.num);
		goto exit;
	}

	pkts = wq_sdio_adma_tx_msg_aggr(wq_func, msg_aggr, max_pkts);
	if (!pkts) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: msgout num %d agggr failed \n", __func__, wq_func->q.msgout.num);
		ret = -1;
		goto exit;
	}

	/* exclude length of struct wq_sdio_adma_pkt_hdr's member "u32 len;" */
	addr = msg_aggr->len - (sizeof(u32) * pkts);
	align_len = ALIGN(msg_aggr->len, WQ_SDIO_BLOCK_SIZE);

	WQ_DBG(DM_TRBUS, DL_INF, "%s: packet %d/%d, addr %d, align length %d\n",
	       __func__, pkts, max_pkts, addr, align_len);

	if (wq_func == &wq_sdio->wlan) {
		atomic_add(pkts, &wq_sdio->wlan_stats.tx_pkt_total_num);
	}

	ret = wq_sdio_adma_write(wq_func, addr, msg_aggr->data, align_len);
	if (ret == 0) {
		if (wq_func->adma.info.tx_accu_mode) {
			wq_func->adma.info.tx_buffer_avail -= pkts;
			wq_func->adma.info.tx_bus_cnt += pkts;
		} else {
			wq_func->adma.info.tx_buffer_avail = 0;
		}
	}

	if (wq_conf.wq_dbg_lv >= DL_INF)
		printk_ratelimited(KERN_INFO "%s: tx packet %d/%d, pkt_free_cnt=%d ret=%d\n",
				   __func__, pkts, max_pkts, wq_sdio->pools.pktout.list.num, ret);

exit:
	return ret;
}

static int wq_sdio_adma_tx_pkt(struct wq_func *wq_func)
{
	struct wq_sdio *wq_sdio = wq_func->wq_sdio;
	struct wq_skbreq *req_aggr = NULL;
	struct rwnx_hw *rwnx_hw = wq_sdio->core.hw;
	int max_pkts = wq_func->adma.info.tx_buffer_avail;
	int aggr_size = wq_sdio_adma_aggr_size(wq_func);
	u32 addr, align_len;
	unsigned char *skb_data_ptr;
	u32 skb_aggr_cnt = 0, skb_aggr_len = 0, aggr_left_cnt = 0;
	int i, ret = 0;

	if (max_pkts < aggr_size) {
		printk_ratelimited(KERN_INFO "%s: tx_buffer_avail %d, func aggrout num %d\n",
				   __func__, max_pkts, wq_func->q.aggrout.num);
		goto done;
	}

	if (wq_list_is_empty(&wq_func->q.aggrout) && !wq_list_is_empty(&wq_func->q.pktout)) {
		WQ_DBG(DM_TRBUS, DL_INF,
		       "%s: aggrout list is empty, msgout num %u, pktout num %u\n",
		       __func__, wq_func->q.msgout.num, wq_func->q.pktout.num);

		if (rwnx_hw && (rwnx_hw->tx_throughput > SDIO_TP_THRESHOLD)) {
			u64 time_start_us = 0, time_end_us = 0;

			time_start_us = (u64)ktime_to_us(ktime_get());

			wq_sdio_adma_tx_pkt_aggr(wq_func, aggr_size);

			time_end_us = (u64)ktime_to_us(ktime_get());
			atomic_add((u32)(time_end_us - time_start_us), &wq_sdio->wlan_stats.main_tx_aggr_time);
		}
	}

	req_aggr = wq_skbreq_dequeue(&wq_func->q.aggrout);
	if (!req_aggr) {
		WQ_DBG(DM_TRBUS, DL_INF, "%s: req_aggr is null\n", __func__);
		goto done;
	}

	if (req_aggr->aggr_cnt > SDIO_ADMA_TX_MAX_AGGR_SIZE) {
		ret = -1;
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "%s: req_aggr aggr_cnt %d > aggr_size %d\n", __func__,
		       req_aggr->aggr_cnt, SDIO_ADMA_TX_MAX_AGGR_SIZE);
		wq_skbreq_free(&wq_sdio->pools.aggrout, req_aggr);
		goto done;
	}

	skb_data_ptr = req_aggr->aggr_addr[req_aggr->aggr_consumed_cnt];

	aggr_left_cnt = (req_aggr->aggr_cnt - req_aggr->aggr_consumed_cnt);
	if (aggr_left_cnt < aggr_size) {
		skb_aggr_cnt = aggr_left_cnt;
	} else {
		skb_aggr_cnt = aggr_size;
	}

	for (i = 0; i < skb_aggr_cnt; i++) {
		skb_aggr_len += req_aggr->aggr_len[req_aggr->aggr_consumed_cnt + i];
	}

	WQ_DBG(DM_TRBUS, DL_INF, "%s: aggr_consumed_cnt %d, aggr_cnt %d, aggr_left_cnt %d, aggr_size %d, skb_aggr_cnt %d, skb_aggr_len %d\n",
		__func__, req_aggr->aggr_consumed_cnt, req_aggr->aggr_cnt, aggr_left_cnt, aggr_size, skb_aggr_cnt, skb_aggr_len);

	/* exclude length of struct wq_sdio_adma_pkt_hdr's member "u32 len;" */
	addr = skb_aggr_len - (sizeof(u32) * skb_aggr_cnt);
	align_len = ALIGN(skb_aggr_len, WQ_SDIO_BLOCK_SIZE);

	WQ_DBG(DM_TRBUS, DL_INF, "%s: packet %d/%d, addr %d, align length %d\n",
	       __func__, skb_aggr_cnt, aggr_size, addr, align_len);

	if (wq_func == &wq_sdio->wlan) {
		atomic_add(skb_aggr_cnt, &wq_sdio->wlan_stats.tx_pkt_total_num);
	}

	ret = wq_sdio_adma_write(wq_func, addr, skb_data_ptr, align_len);
	if (ret == 0) {
		if (wq_func->adma.info.tx_accu_mode) {
			wq_func->adma.info.tx_buffer_avail -= skb_aggr_cnt;
			wq_func->adma.info.tx_bus_cnt += skb_aggr_cnt;
		} else {
			wq_func->adma.info.tx_buffer_avail = 0;
		}
	}

	req_aggr->aggr_consumed_cnt += skb_aggr_cnt;

	if (wq_conf.wq_dbg_lv >= DL_INF)
		printk_ratelimited(
			KERN_INFO
			"%s: tx packet %d/%d, pkt_free_cnt=%d ret=%d\n",
			__func__, skb_aggr_cnt, aggr_size,
			wq_sdio->pools.pktout.list.num, ret);

	if (req_aggr->aggr_consumed_cnt == req_aggr->aggr_cnt) {
		wq_skbreq_free(&wq_sdio->pools.aggrout, req_aggr);
	} else {
		wq_skbreq_unpop(&wq_func->q.aggrout, req_aggr);
	}

done:
	return ret;
}

static int wq_sdio_adma_tx(struct wq_func *wq_func)
{
	int ret = 0;

	if (!wq_list_is_empty(&wq_func->q.msgout)) {
		ret = wq_sdio_adma_tx_msg(wq_func);
	} else {
		ret = wq_sdio_adma_tx_pkt(wq_func);
	}

	return ret;
}

static inline int wq_func_is_tx_empty(struct wq_func *wq_func)
{
	if (wq_list_is_empty(&wq_func->q.aggrout)
			&& wq_list_is_empty(&wq_func->q.msgout) && wq_list_is_empty(&wq_func->q.pktout)) {
		return true;
	} else {
		return false;
	}
}

#else
static int __wq_sdio_adma_aggr(struct sk_buff *aggr, struct wq_list_head *q,
			       struct wq_list_head *done, int max_pkts)
{
	struct wq_skbreq *req;
	int pkts;
	unsigned long flags;

	spin_lock_irqsave(&(q->lock), flags);

	for (pkts = 0; pkts < max_pkts && (req = __wq_skbreq_dequeue(q));
	     ++pkts) {
		struct sk_buff *skb = req->skb;
		struct wq_sdio_adma_pkt_hdr hdr = {
			.len = cpu_to_le32(skb->len + sizeof(hdr.virt_qid)),
			.virt_qid = cpu_to_le32(req->virt_qid),
		};
		u32 len = sizeof(hdr) + skb->len;

		WQ_DBG(DM_TRBUS, DL_VRB, "%s: type %d, len %d\n", __func__,
		       req->virt_qid, skb->len);

		if (skb_tailroom(aggr) < len) {
			WQ_DBG(DM_TRBUS, DL_WRN,
			       "%s: un-pop it (tail room %d < %d)!\n", __func__,
			       skb_tailroom(aggr), len);
			__wq_skbreq_unpop(q, req);
			break;
		}

		/* FIXME: padding for alignment? */
		memcpy(skb_tail_pointer(aggr), &hdr, sizeof(hdr));
		memcpy(skb_tail_pointer(aggr) + sizeof(hdr), skb->data,
		       skb->len);
		skb_put(aggr, len);

		__wq_skbreq_enqueue(done, req);
	}

	spin_unlock_irqrestore(&(q->lock), flags);

	return pkts;
}

static int wq_sdio_adma_aggr(struct wq_func *wq_func,
			     struct sk_buff *aggr,
			     struct wq_list_head *msg_aggr_done,
			     struct wq_list_head *pkt_aggr_done, int max_pkts)
{
	int pkts = 0;

	skb_trim(aggr, 0);

	if (pkts < max_pkts)
		pkts += __wq_sdio_adma_aggr(aggr, &wq_func->q.msgout,
					    msg_aggr_done, max_pkts - pkts);
	/* FIXME: should consider QoS? */
	if (pkts < max_pkts)
		pkts += __wq_sdio_adma_aggr(aggr, &wq_func->q.pktout,
					    pkt_aggr_done, max_pkts - pkts);

	return pkts;
}

static void wq_sdio_adma_tx_done(struct wq_sdio *wq_sdio,
				 struct wq_func *wq_func,
				 struct wq_list_head *msg_aggr_done,
				 struct wq_list_head *pkt_aggr_done, int status)
{
	struct wq_skbreq *req;
	unsigned long flags;
	struct sk_buff_head sk_list_wlan;
	struct sk_buff_head sk_list_dtop;

	__skb_queue_head_init(&sk_list_wlan);
	__skb_queue_head_init(&sk_list_dtop);

	spin_lock_irqsave(&(wq_sdio->pools.msgout.list.lock), flags);

	/* msg tx done */
	while ((req = __wq_skbreq_dequeue(msg_aggr_done)) != NULL) {
		if (wq_func == &wq_sdio->wlan)
			__skb_queue_tail(&sk_list_wlan, req->skb);
		else
			dev_kfree_skb_any(req->skb);

		req->skb = NULL;
		__wq_skbreq_free(&wq_sdio->pools.msgout, req);
	}

	spin_unlock_irqrestore(&(wq_sdio->pools.msgout.list.lock), flags);

	/* pkt tx done */
	spin_lock_irqsave(&(wq_sdio->pools.pktout.list.lock), flags);

	while ((req = __wq_skbreq_dequeue(pkt_aggr_done)) != NULL) {
		WQ_DBG(DM_TRBUS, DL_INF, "%s, type %d, len %d\n", __func__,
		       req->virt_qid, req->skb->len);

		if (wq_func == &wq_sdio->wlan)
			__skb_queue_tail(&sk_list_wlan, req->skb);
		else
			__skb_queue_tail(&sk_list_dtop, req->skb);

		req->skb = NULL;
		__wq_skbreq_free(&wq_sdio->pools.pktout, req);
	}

	spin_unlock_irqrestore(&(wq_sdio->pools.pktout.list.lock), flags);

	if ((wq_func == &wq_sdio->wlan) && skb_queue_len(&sk_list_wlan)) {
		htc_txq_done(&wq_sdio->core, &sk_list_wlan, status);
	} else if (skb_queue_len(&sk_list_dtop)) {
		htc_txq_done(&wq_sdio->core, &sk_list_dtop, status);
	}
}

static int wq_sdio_adma_tx(struct wq_func *wq_func)
{
	struct wq_sdio *wq_sdio = wq_func->wq_sdio;
	struct wq_list_head msg_aggr_done, pkt_aggr_done;
	struct sk_buff *aggr = wq_func->adma.aggr.tx;
	int max_pkts = wq_func->adma.info.tx_buffer_avail;
	int pkts;
	u32 addr;
	u32 align_len;
	int ret;

	INIT_WQ_LIST_HEAD(&msg_aggr_done);
	INIT_WQ_LIST_HEAD(&pkt_aggr_done);

	skb_trim(aggr, 0);	/* reset it */

	pkts = wq_sdio_adma_aggr(wq_func, aggr, &msg_aggr_done,
				 &pkt_aggr_done, max_pkts);
	if (!pkts) {
		WQ_DBG(DM_TRBUS, DL_INF, "%s: pkts %d, max_pkts %d\n", __func__,
		       pkts, max_pkts);
		ret = -1;
		goto done;
	}

	/* exclude length of struct wq_sdio_adma_pkt_hdr's member "u32 len;" */
	addr = aggr->len - (sizeof(u32) * pkts);
	align_len = ALIGN(aggr->len, WQ_SDIO_BLOCK_SIZE);
	WQ_DBG(DM_TRBUS, DL_VRB, "%s: packet %d/%d, addr %d, align length %d\n",
	       __func__, pkts, max_pkts, addr, align_len);

	if (wq_func == &wq_sdio->wlan) {
		atomic_add(pkts, &wq_sdio->wlan_stats.tx_pkt_total_num);
	}

	ret = wq_sdio_adma_write(wq_func, addr, aggr->data, align_len);
	if (ret == 0)
		wq_func->adma.info.tx_buffer_avail -= pkts; /* tx buffer is consumed */

	wq_sdio_adma_tx_done(wq_sdio, wq_func, &msg_aggr_done, &pkt_aggr_done, ret);

	if (wq_conf.wq_dbg_lv >= DL_INF)
		printk_ratelimited(
			KERN_INFO
			"%s: tx packet %d/%d, pkt_free_cnt=%d ret=%d\n",
			__func__, pkts, max_pkts,
			wq_sdio->pools.pktout.list.num, ret);

done:
	return ret;
}

static inline int wq_func_is_tx_empty(struct wq_func *wq_func)
{
	return wq_list_is_empty(&wq_func->q.msgout) &&
	       wq_list_is_empty(&wq_func->q.pktout);
}
#endif

static bool wq_sdio_is_remove(struct wq_func *wq_func)
{
	struct wq_sdio *wq_sdio;

	if (!(wq_sdio = wq_func->wq_sdio)) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: wq_sdio %p or func is NULL!\n",
		       __func__, wq_sdio);
		return true;
	}

	if ((wq_func == &wq_sdio->wlan)
			&& (wq_sdio->core.state < WQ_CORE_STATE_HIF_READY)) {
		return true;
	}

	if ((wq_func == &wq_sdio->wlan_msg)
			&& (wq_sdio->core.state < WQ_CORE_STATE_HIF_READY)) {
		return true;
	}

	if ((wq_func == &wq_sdio->dtop)
			&& (wq_sdio->core.state < WQ_CORE_STATE_HIF_NREADY)) {
		return true;
	}

	if ((wq_func == &wq_sdio->dtop_log)
			&& (wq_sdio->core.state < WQ_CORE_STATE_HIF_NREADY)) {
		return true;
	}

	return false;
}

void __wq_sdio_adma_main_process(struct wq_func *wq_func)
{
	struct wq_sdio *wq_sdio;
	struct rwnx_hw *rwnx_hw;
	u64 time_start_us = 0, time_end_us = 0;
	int aggr_size = wq_sdio_adma_aggr_size(wq_func);
	int ret = 0;

	WQ_DBG(WQ_LOG_DM, WQ_LOG_LEVEL, "--> %s(%d)\n", __func__,
	       wq_func->func_num);

	WQ_DBG(WQ_LOG_DM, DL_VRB, "--> %s(%d)\n", __func__, wq_func->func_num);
	if (!(wq_sdio = wq_func->wq_sdio)) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: wq_sdio %p or func is NULL!\n",
		       __func__, wq_sdio);
		return;
	}

	PROFILING_SET(SW_PROF_SDIO_MAIN_PROCESS);

	time_start_us = (u64)ktime_to_us(ktime_get());

	wq_sdio_intr_en(wq_sdio, wq_func, false);

	time_end_us = (u64)ktime_to_us(ktime_get());

	if (wq_func == &wq_sdio->wlan) {
		atomic_add((u32)(time_end_us - time_start_us), &wq_sdio->wlan_stats.main_kthread_time);
	}

	while (true)
	{
		time_start_us = (u64)ktime_to_us(ktime_get());

		if (wq_sdio_is_remove(wq_func)) {
			WQ_DBG(DM_TRBUS, DL_ERR, "%s: sdio is not ready.\n", __func__);
			break;
		}

		if (wq_func->adma.info.rx_len <= 0) {
			ret = wq_sdio_adma_info_update(wq_func, NULL);
			if (ret) {
				break;
			}
		}

		while (wq_func->adma.info.rx_len > 0) {
			ret = wq_sdio_adma_rx(wq_func);
			if (ret) {
				break;
			}

			if (wq_func->adma.info.tx_buffer_avail && !wq_func_is_tx_empty(wq_func)) {
				break;
			}
		}

		if (wq_func == &wq_sdio->wlan) {
			if (wq_func->adma.info.tx_buffer_avail >= aggr_size) {
				atomic_inc(&wq_sdio->wlan_stats.tx_buf_avail_valid_cnt);

				if (!wq_list_is_empty(&wq_func->q.aggrout)) {
					atomic_inc(&wq_sdio->wlan_stats.tx_buf_avail_valid_pkt_cnt);
				}
			} else {
				atomic_inc(&wq_sdio->wlan_stats.tx_buf_avail_zero_cnt);

				if (!wq_list_is_empty(&wq_func->q.aggrout)) {
					atomic_inc(&wq_sdio->wlan_stats.tx_buf_avail_zero_pkt_cnt);
				}
			}
		}

		if (sdio_ut_mode && (wq_func == &wq_sdio->wlan)) {
			wq_ut_stat_t *ut_stat = &wq_sdio->ut_stat;
			wq_ut_config_t *ut_config = &wq_sdio->ut_config;
			if (ut_stat->running && ut_config->test_tx) {
				if (wq_list_is_empty(&wq_func->q.aggrout)) {
					wq_sdio_ut_prepare_tx_pkt(wq_sdio);
				}
			}
		}

		while (wq_func->adma.info.tx_buffer_avail >= aggr_size) {
			ret = wq_sdio_adma_tx(wq_func);
			if (ret) {
				break;
			}

			if (wq_func->adma.info.rx_len) {
				break;
			}

			if (wq_func_is_tx_empty(wq_func) || (wq_func->adma.info.tx_buffer_avail < aggr_size)) {
				break;
			}
		}

		time_end_us = (u64)ktime_to_us(ktime_get());

		if (wq_func == &wq_sdio->wlan) {
			atomic_add((u32)(time_end_us - time_start_us), &wq_sdio->wlan_stats.main_kthread_time);
		}

		rwnx_hw = wq_sdio->core.hw;
		if (rwnx_hw && (wq_func == &wq_sdio->wlan)) {
#ifdef SDIO_TX_POLLING_MODE
			if (rwnx_hw->tx_throughput > SDIO_TP_THRESHOLD) {
				continue;
			}
#endif

#ifdef SDIO_RX_POLLING_MODE
			if (rwnx_hw->rx_throughput > SDIO_TP_THRESHOLD) {
				continue;
			}
#endif
		}

		if (sdio_ut_mode && (wq_func == &wq_sdio->wlan)) {
			wq_ut_stat_t *ut_stat = &wq_sdio->ut_stat;
			wq_ut_config_t *ut_config = &wq_sdio->ut_config;
			if (ut_stat->running && ut_config->test_tx) {
				if (wq_list_is_empty(&wq_func->q.aggrout)) {
					wq_sdio_ut_prepare_tx_pkt(wq_sdio);
				}
			}
		}

		if ((wq_func->adma.info.rx_len == 0) &&
				(wq_func_is_tx_empty(wq_func))) {
			break;
		}
	}

	time_start_us = (u64)ktime_to_us(ktime_get());

	/* enable TX interrupt, if more TX is queued. otherwise, disable it */
	wq_sdio_intr_buf_avail_en(wq_sdio, wq_func, !wq_func_is_tx_empty(wq_func));

	wq_sdio_intr_en(wq_sdio, wq_func, true);

	time_end_us = (u64)ktime_to_us(ktime_get());

	if (wq_func == &wq_sdio->wlan) {
		atomic_add((u32)(time_end_us - time_start_us), &wq_sdio->wlan_stats.main_kthread_time);
	}

	PROFILING_CLR(SW_PROF_SDIO_MAIN_PROCESS);

	WQ_DBG(WQ_LOG_DM, WQ_LOG_LEVEL, "<-- %s(%d)\n", __func__, wq_func_is_tx_empty(wq_func));
}

#ifdef SDIO_MAIN_KTHREAD
int wq_sdio_adma_process(void *data)
{
	struct wq_kthread *wq_thread = (struct wq_kthread *)data;
	struct wq_func *wq_func =
		container_of(wq_thread, struct wq_func, adma.maink);

#if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 8, 0)
	const struct sched_param param = { .sched_priority = (MAX_RT_PRIO - 1) };
	sched_setscheduler(current, SCHED_FIFO, &param);
#else
	sched_set_fifo(current);
#endif

	WQ_DBG(DM_TRBUS, DL_ERR, "%s: enter\n", __func__);

	while (true) {
		wait_for_completion_interruptible(&wq_thread->comp);

		if (kthread_should_stop()) {
			WQ_DBG(DM_TRBUS, DL_ERR, "%s: kthread should stop!\n",
			       __func__);
			goto exit;
		}

		if (wq_sdio_is_remove(wq_func)) {
			WQ_DBG(DM_TRBUS, DL_ERR, "%s: sdio is not ready.\n",
			       __func__);
			goto exit;
		}

		__wq_sdio_adma_main_process(wq_func);
	}

exit:
	WQ_DBG(DM_TRBUS, DL_ERR, "%s: leave\n", __func__);
	return 0;
}
#else
void wq_sdio_adma_process(struct work_struct *work)
{
	struct wq_func *wq_func =
		container_of(work, struct wq_func, adma.mainq.work);

	__wq_sdio_adma_main_process(wq_func);
}
#endif

#ifdef SDIO_MAIN_KTHREAD
void wq_func_main_trigger(struct wq_func *wq_func)
{
	struct wq_sdio *wq_sdio = wq_func->wq_sdio;
	struct wq_kthread *wq_thread = &wq_func->adma.maink;

	if (!wq_sdio || !wq_thread->thread) {
		WQ_DBG(DM_TRBUS, DL_VRB, "%s: wq_sdio or thread is NULL\n",
		       __func__);
		return;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
	reinit_completion(&wq_thread->comp);
#else
	wq_thread->comp.done = 0;
#endif

	if (!wq_sdio_is_remove(wq_func))
		complete(&wq_thread->comp);
	else
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: sdio is not ready.\n", __func__);
}
#else
void wq_func_main_trigger(struct wq_func *wq_func)
{
	struct wq_sdio *wq_sdio = wq_func->wq_sdio;
	struct wq_workq *workq = &wq_func->adma.mainq;

	if (!wq_sdio || !workq->workqueue) {
		WQ_DBG(DM_TRBUS, DL_VRB, "%s: wq_sdio or workqueue is NULL\n",
		       __func__);
		return;
	}

	if (!wq_sdio_is_remove(wq_func))
		queue_work(workq->workqueue, &workq->work);
	else
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: sdio is not ready.\n", __func__);
}
#endif


#ifdef SDIO_RX_AGGR_MODE
void wq_sdio_adma_rx_process(struct work_struct *work)
{
	struct wq_func *wq_func =
		container_of(work, struct wq_func, adma.rxq.work);
	struct wq_sdio *wq_sdio;
	struct htc_q *rxq;
	u64 time_start_us = 0, time_end_us = 0;

	WQ_DBG(WQ_LOG_DM, WQ_LOG_LEVEL, "--> %s(%d)\n", __func__,
	       wq_func->func_num);

	WQ_DBG(WQ_LOG_DM, DL_VRB, "--> %s(%d)\n", __func__, wq_func->func_num);
	if (!(wq_sdio = wq_func->wq_sdio)) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: wq_sdio %p or func is NULL!\n",
		       __func__, wq_sdio);
		return;
	}

	while (!wq_list_is_empty(&wq_func->q.aggrin)) {
		time_start_us = (u64)ktime_to_us(ktime_get());

		rxq = &wq_sdio->core.htc.rxq.pkt;
		if (skb_queue_len(&rxq->head) >= RX_PKT_QUEUE_MAX) {
			wq_sdio_adma_rx_process_one_internal(wq_func);
		} else {
			wq_sdio_adma_rx_process_one(wq_func);
		}

		time_end_us = (u64)ktime_to_us(ktime_get());

		if (wq_func == &wq_sdio->wlan) {
			atomic_add((u32)(time_end_us - time_start_us), &wq_sdio->wlan_stats.rx_workq_time);
		}
	}
}

void wq_func_rx_trigger(struct wq_func *wq_func)
{
	struct wq_sdio *wq_sdio = wq_func->wq_sdio;
	struct wq_workq *workq = &wq_func->adma.rxq;

	if (!wq_sdio || !workq->workqueue) {
		WQ_DBG(DM_TRBUS, DL_VRB, "%s: wq_sdio or workqueue is NULL\n",
		       __func__);
		return;
	}

	if (!wq_sdio_is_remove(wq_func))
		queue_work(workq->workqueue, &workq->work);
	else
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: sdio is not ready.\n", __func__);
}
#endif

void wq_sdio_interrupt(struct sdio_func *func)
{
	struct wq_sdio *wq_sdio = sdio_get_drvdata(func);
	struct wq_func *wq_func;

	PROFILING_SET(SW_PROF_SDIO_INTERRUPT);

	if (!wq_sdio) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s(func = %p) wq_sdio is NULL\n",
		       __func__, func);
		LEAVE();
		PROFILING_CLR(SW_PROF_SDIO_INTERRUPT);
		return;
	}

	if (func == wq_sdio->dtop.func) {
		wq_func = &wq_sdio->dtop;
	} else if (func == wq_sdio->dtop_log.func) {
		wq_func = &wq_sdio->dtop_log;
	} else if (func == wq_sdio->wlan_msg.func) {
		wq_func = &wq_sdio->wlan_msg;
	} else {
		wq_func = &wq_sdio->wlan;
	}

	if (wq_func) {
		wq_func_main_trigger(wq_func);
	}

	PROFILING_CLR(SW_PROF_SDIO_INTERRUPT);
}
