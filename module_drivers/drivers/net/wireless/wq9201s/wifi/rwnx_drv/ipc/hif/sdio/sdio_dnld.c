/** @file sdio_dnld.c
 *
 *  @brief This file contains SDIO MMC IF (interface) module
 *  related functions.
 *
 * Copyright (C) 2016-2022, WuQi Ltd.
 *
 * This software file (the "File") is distributed by WuQi Ltd.
 * Under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 *
 */

#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>

#include "sdio.h"
#include "wq_log.h"

#include "hif_api.h"
#include "wq_fw.h"
#include "bmi_core.h"
#include "bmi_cmd.h"

struct vector {
	void *data;
	u32 len;
};

#define WQ_SDIO_BUS_FW_DL_MTU 1648

struct wq_bus_fw_dl_tag {
	__be16 id; /* packet index */
	__be16 checksum; /* checksum */
} __attribute__((__packed__));

void wq_sdio_rx_bmi_msg(struct wq_sdio *wq_sdio, struct sk_buff *skb)
{
	spin_lock_bh(&wq_sdio->bmi.lock);
	if (wq_sdio->bmi.reply) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "%s: last bmi reply is not consumed: %*ph\n", __func__,
		       wq_sdio->bmi.reply->len, wq_sdio->bmi.reply->data);
		dev_kfree_skb_any(wq_sdio->bmi.reply);
	}
	wq_sdio->bmi.reply = skb;
	WQ_DBG(DM_TRBUS, DL_VRB, "%s: bmi reply %*ph\n", __func__, skb->len,
	       skb->data);
	spin_unlock_bh(&wq_sdio->bmi.lock);
#ifdef USE_COMPLETE
	complete(&wq_sdio->bmi.completion);
#else
	wq_sdio->bmi.woken = true;
	wake_up(&wq_sdio->bmi.wait_q);
#endif
}

static int wq_sdio_bmi_cmd_req(struct wq_func *dtop, u16 msg_id,
			       struct vector *reqs)
{
	struct wq_sdio *wq_sdio;
	struct wq_skbreq *req = NULL;
	struct sk_buff *skb = NULL;
	struct wq_sdio_bmi_msg *bmi_req = NULL;
	struct vector *vec;
	u32 data_len;
	u32 pkt_len;

	if (!dtop || !(wq_sdio = dtop->wq_sdio)) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: dtop %p is NULL!\n", __func__,
		       dtop);
		return -1;
	}

	/** calc pkt size */
	for (vec = reqs, data_len = 0; vec && vec->data; ++vec) {
		data_len += vec->len;
	}

	pkt_len = sizeof(struct wq_sdio_adma_pkt_hdr) +
		  sizeof(struct wq_sdio_bmi_msg) + data_len;

	/** Assemble the pkt */
	skb = dev_alloc_skb(pkt_len);
	if (!skb) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: dev_alloc_skb failed\n",
		       __func__);
		return -1;
	}

	skb_reserve(skb, sizeof(struct wq_sdio_adma_pkt_hdr));
	bmi_req = (void *)skb_put(
		skb, pkt_len - sizeof(struct wq_sdio_adma_pkt_hdr));

	WQ_DBG(DM_TRBUS, DL_VRB, "%s skb %p skb->len %d.\n", __func__, skb,
	       skb->len);

	/** Fulfill the dnld pkt data structure */
	bmi_req->magic = htonl(WQ_SDIO_BMI_MSG_MAGIC);
	bmi_req->id = htons(msg_id);
	bmi_req->length = htons(data_len);

	for (vec = reqs, data_len = 0; vec && vec->data; ++vec) {
		memcpy(bmi_req->data + data_len, vec->data, vec->len);
		data_len += vec->len;
	}

	/* send message by msgout free list wq_skbreq */
	req = wq_skbreq_alloc(&wq_sdio->pools.msgout);
	if (!req) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "%s: req is null, tx_buffer_avail %d\n", __func__,
		       dtop->adma.info.tx_buffer_avail);
		return -1;
	}

	req->skb = skb;
	if (WQ_VREQ_ID_EXE_MP_TEST_CMD == msg_id)
		req->virt_qid = WQ_SDIO_VQID_MSGOUT;
	else
		req->virt_qid = WQ_SDIO_VQID_FW_DL;

	wq_skbreq_enqueue(&dtop->q.msgout, req);

	wq_func_main_trigger(dtop);

	return 0;
}

static int __wq_sdio_bmi_cmd(struct wq_func *dtop, u16 msg_id,
			     struct vector *req, u8 *reply, u16 reply_len,
			     u32 timeout)
{
	struct wq_sdio *wq_sdio = dtop->wq_sdio;
	int ret;
	unsigned long tout = msecs_to_jiffies(timeout);
	int timeout_left;

#ifdef USE_COMPLETE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
	reinit_completion(&wq_sdio->bmi.completion);
#else
	wq_sdio->bmi.completion.done = 0;
#endif
#else
	wq_sdio->bmi.woken = false;
#endif

	ret = wq_sdio_bmi_cmd_req(dtop, msg_id, req);
	if (ret)
		return ret;

		/* wait for reply */
#ifdef USE_COMPLETE
	timeout_left = wait_for_completion_killable_timeout(
		&wq_sdio->bmi.completion, tout);
#else
	timeout_left = wait_event_timeout(wq_sdio->bmi.wait_q,
					  wq_sdio->bmi.woken, tout);
#endif
	if (!timeout_left || timeout_left < 0) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "[auto]ASSERT : dnld cmd ERR %d id: 0x%x\n",
		       timeout_left, msg_id);
		hif_dump_info(&wq_sdio->core);
		return -ETIMEDOUT;
	}

	spin_lock_bh(&wq_sdio->bmi.lock);
	if (wq_sdio->bmi.reply) {
		if (reply) {
			struct wq_sdio_bmi_msg *bmi_reply =
				(void *)wq_sdio->bmi.reply->data;
			u32 actual = ntohs(bmi_reply->length);

			WQ_DBG(DM_TRBUS, DL_VRB,
			       "%s, got reply, reply_len %d, actual %d\n",
			       __func__, reply_len, actual);
			if (reply_len > actual)
				reply_len = actual;
			memcpy(reply, bmi_reply->data, reply_len);
		}
		dev_kfree_skb_any(wq_sdio->bmi.reply);
		wq_sdio->bmi.reply = NULL;
	}
	spin_unlock_bh(&wq_sdio->bmi.lock);

	return 0;
}

int wq_sdio_bmi_cmd(struct wq_core *core, u8 cmd, const void *param, u16 p_size,
		    void *resp, u16 r_size, int timeout)
{
	u8 boot_cccr;
	int ret;
	struct wq_dev_rom_ver *rom_ver;
	struct wq_sdio *wq_sdio = container_of(core, struct wq_sdio, core);
	struct vector reqs[] = {
		{ .data = (void *)param, .len = p_size },
		{ /* terminator */ },
	};

	switch (cmd) {
	case WQ_BMI_CMD_GET_ROM_VER:
		if (resp) {
			rom_ver = (struct wq_dev_rom_ver *)resp;
			ret = wq_sdio_cmd52_read(wq_sdio->dtop.func,
						 SDIO_CCCR_RCV_REG0,
						 &boot_cccr);
			if (ret)
				return ret;
			WQ_DBG(DM_TRBUS, DL_WRN, "rom version boot_cccr=0x%x\n",
			       boot_cccr);
			if (boot_cccr) {
				rom_ver->major = (boot_cccr & 0xF0) >> 4;
				rom_ver->minor = boot_cccr & 0x0F;
			} else {
				rom_ver->major = 1;
				rom_ver->minor = 0;
			}
			rom_ver->build_hr = 0xff;
			rom_ver->build_min = 0xff;
		} else {
			return -EINVAL;
		}
		return 0;

	case WQ_BMI_CMD_GET_SYS_STATE:
		ret = wq_sdio_cmd52_read(wq_sdio->dtop.func, SDIO_CCCR_WUQI_FW,
					 &boot_cccr);
		if (ret)
			return ret;
		WQ_DBG(DM_TRBUS, DL_WRN, "fw_state WUQI_FW=0x%x\n", boot_cccr);
		if (boot_cccr & SDIO_BIT_DTOP_FW_STATUS)
			*(u8 *)resp |= BIT(WQ_FW_DTOP);
		else
			*(u8 *)resp = BIT(WQ_FW_BOOTROM);

		if (boot_cccr & SDIO_BIT_WIFI_FW_STATUS)
			*(u8 *)resp |= BIT(WQ_FW_WIFI);
		return ret;

	case WQ_BMI_CMD_SET_FW_INFO:
	case WQ_BMI_CMD_VERIFY_FW:
	case WQ_BMI_CMD_UNLOAD_DTOP:
		return 0;

	default:
		WQ_DBG(DM_TRBUS, DL_ERR, "unknown cmd: %x!\n", cmd);
		return -EINVAL;
	}

	return __wq_sdio_bmi_cmd(&wq_sdio->dtop, cmd, reqs, resp, r_size,
				 timeout);
}

/** BootROM firmware download */
#define WQ_SDIO_BOOTROM_FW_BULK_MAGIC 0x57514657 /* "WQFW" */
#define WQ_SDIO_BOOTROM_FW_BULK_MAX (4U << 10) /* 4K */

struct wq_sdio_bootrom_fw_bulk {
	__be32 magic; /* FIXED: WQ_SDIO_BOOTROM_FW_BULK_MAGIC */
	__le32 fw_len; /* total firmware length */
	__le32 start_pc; /* start address */
	__le32 length; /* data length */
	u8 data[0];
};

int wq_sdio_read_data_sync_rom(struct wq_sdio *wq_sdio, u8 *pmbuf, u32 count,
			       u32 addr, u32 timeout)
{
	int status = 0;
	struct sdio_func *func = wq_sdio->dtop.func;

	sdio_claim_host(func);
	status = sdio_readsb(func, pmbuf, addr, count);
	sdio_release_host(func);

	if (!status)
		WQ_DBG(DM_TRBUS, DL_VRB, "[GOOD]cmd53 read OK\n");
	else
		WQ_DBG(DM_TRBUS, DL_ERR, "[ERROR]cmd53 read error=%d\n",
		       status);

	return status;
}

static int wq_sdio_write_data_sync_rom(struct wq_sdio *wq_sdio, u8 *pmbuf,
				       u32 addr, u32 count)
{
	int status;
	struct wq_func *dtop_func = &wq_sdio->dtop;
	struct sdio_func *func = wq_sdio->dtop.func;

	WQ_DBG(DM_TRBUS, DL_VRB,
	       "cmd53 write: func num=%d, cur_blksize = %d, max_blk_count = %d, count = %d\n",
	       dtop_func->func_num, func->cur_blksize, func->card->host->max_blk_count,
	       count);
	sdio_claim_host(func);
	status = sdio_writesb(func, addr, pmbuf, count);
	sdio_release_host(func);
	if (!status)
		WQ_DBG(DM_TRBUS, DL_VRB, "[GOOD]cmd53 write OK\n");
	else
		WQ_DBG(DM_TRBUS, DL_ERR, "[ERROR]cmd53 write error=%d\n",
		       status);

	return status;
}

/**
 * @brief Request firmware DPC
 *
 * @param handle    A pointer to driver_handle structure
 * @param firmware  A pointer to firmware image
 *
 * @return        0: Success or error code
 */
static int wq_sdio_dtop_fw_dpc(struct wq_sdio *wq_sdio, const u8 *data, int len)
{
	int ret = 0;
	u32 fw_start_addr;
	u8 boot_cccr = 0;
	//u8 ioready_val = 0;
	u8 timeout_try;
	u32 fw_len;
	u32 offset;
	struct wq_sdio_bootrom_fw_bulk *fw_bulk = NULL;
	struct wq_core *core = &wq_sdio->core;

	ENTER();

	fw_len = ALIGN(len, sizeof(u32));
	fw_start_addr = core->wq_dnld->start_pc;
	WQ_DBG(DM_TRBUS, DL_VRB, "%s: start addr: %x + %x!\n", __func__,
	       fw_start_addr, fw_len);

	fw_bulk = kzalloc(sizeof(*fw_bulk) + WQ_SDIO_BOOTROM_FW_BULK_MAX,
			  GFP_KERNEL);
	if (!fw_bulk) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "%s: allocate download buffer failed!\n", __func__);
		ret = -ENOMEM;
		goto failed;
	}

	fw_bulk->magic = cpu_to_be32(WQ_SDIO_BOOTROM_FW_BULK_MAGIC);
	fw_bulk->fw_len = cpu_to_le32(fw_len);
	fw_bulk->start_pc = cpu_to_le32(fw_start_addr);
	fw_bulk->length = cpu_to_le32(WQ_SDIO_BOOTROM_FW_BULK_MAX);

	for (offset = 0; offset < fw_len;
	     offset += WQ_SDIO_BOOTROM_FW_BULK_MAX) {
		u32 len = WQ_SDIO_BOOTROM_FW_BULK_MAX;

		if (offset + len > fw_len) {
			len = fw_len - offset;
			fw_bulk->length = cpu_to_le32(len);
		}
		memcpy(fw_bulk->data, data + offset, len);

		WQ_DBG(DM_TRBUS, DL_VRB,
		       "-------- SDIO DTOP firmware download pkg#%d: %x/%x [%*ph ...] --------\n",
		       offset / WQ_SDIO_BOOTROM_FW_BULK_MAX, offset, fw_len, 32,
		       fw_bulk);

		for (timeout_try = 5; timeout_try; timeout_try--) {
			ret = wq_sdio_cmd52_read(wq_sdio->dtop.func,
						 SDIO_CCCR_WUQI_FW, &boot_cccr);
			if (ret)
				goto failed;
			if (boot_cccr & SDIO_BIT_DTOP_FW_READY)
				break;
			msleep(10);
		}
		if (!timeout_try) {
			WQ_DBG(DM_TRBUS, DL_ERR,
			       "-------- SDIO DTOP firmware download failed (%x/%x) --------\n",
			       offset, fw_len);
			ret = -ETIME;
			goto failed;
		}

		ret = wq_sdio_write_data_sync_rom(wq_sdio, (u8 *)fw_bulk, 0x00,
						  WQ_SDIO_BOOTROM_FW_BULK_MAX +
							  WQ_SDIO_BLOCK_SIZE);
		if (ret)
			goto failed;
	}
	WQ_DBG(DM_TRBUS, DL_VRB,
	       "-------- DTOP SDIO firmware download end --------\n");

failed:
	kfree(fw_bulk);

	LEAVE();
	return ret;
}

int wq_sdio_bmi_xfer(struct wq_core *core, enum wq_bmi_xfer_type type,
		     const u8 *data, int len, int timeout)
{
	int ret = -EINVAL;
	struct wq_sdio *wq_sdio = container_of(core, struct wq_sdio, core);

	WQ_DBG(DM_TRBUS, DL_INF, "%s: fw %d.\n", __func__, type);

	switch (type) {
	case WQ_FW_DTOP_DL:
		ret = wq_sdio_dtop_fw_dpc(wq_sdio, data, len);
		break;
	default:
		break;
	}

	return ret;
}

int wq_sdio_bmi_exchange(struct wq_core *core, void *req, u32 req_len,
			 void *rsp, u32 rsp_len, int timeout)
{
	u32 pkt_len = 0;
	struct vector *vec;
	struct vector reqs[] = {
		{ .data = (void *)req, .len = req_len },
		{ /* terminator */ },
	};

	struct wq_sdio *wq_sdio = container_of(core, struct wq_sdio, core);
	struct wq_func *dtop = &wq_sdio->dtop;
	struct wq_sdio_msg_head *bmi_req = NULL;
	struct sk_buff *skb = NULL;
	struct wq_skbreq *req_skb = NULL;
	int timeout_left;
	u32 data_len;
	unsigned long tout = msecs_to_jiffies(timeout);

#ifdef USE_COMPLETE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
	reinit_completion(&wq_sdio->bmi.completion);
#else
	wq_sdio->bmi.completion.done = 0;
#endif
#else
	wq_sdio->bmi.woken = false;
#endif

	/** calc pkt size */
	for (vec = reqs, data_len = 0; vec && vec->data; ++vec) {
		data_len += vec->len;
	}
	pkt_len = sizeof(struct wq_sdio_adma_pkt_hdr) + sizeof(data_len) +
		  data_len;

	/** Assemble the pkt */
	skb = dev_alloc_skb(pkt_len);
	if (!skb) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: dev_alloc_skb failed\n",
		       __func__);
		return -1;
	}

	skb_reserve(skb, sizeof(struct wq_sdio_adma_pkt_hdr));
	bmi_req = (void *)skb_put(
		skb, pkt_len - sizeof(struct wq_sdio_adma_pkt_hdr));

	bmi_req->len = htonl(data_len);
	WQ_DBG(DM_TRBUS, DL_VRB, "%s skb %p skb->len %d.\n", __func__, skb,
	       skb->len);

	for (vec = reqs, data_len = 0; vec && vec->data; ++vec) {
		memcpy(bmi_req->data + data_len, vec->data, vec->len);
		data_len += vec->len;
	}
	// WQ_DBG(DM_TRBUS, DL_ERR, " %d %d %d %d %d %d \n", bmi_req->data[0], bmi_req->data[1], bmi_req->data[2], bmi_req->data[3], bmi_req->data[4], bmi_req->data[5]);

	/* send message by msgout free list wq_skbreq */
	req_skb = wq_skbreq_alloc(&wq_sdio->pools.msgout);
	if (!req_skb) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "%s: req_skb is null, tx_buffer_avail %d\n", __func__,
		       dtop->adma.info.tx_buffer_avail);
		return -1;
	}

	req_skb->skb = skb;
	req_skb->virt_qid = WQ_SDIO_VQID_FW_DL;

	wq_skbreq_enqueue(&dtop->q.msgout, req_skb);

	wq_func_main_trigger(dtop);

	/* wait for reply */
#ifdef USE_COMPLETE
	timeout_left = wait_for_completion_killable_timeout(
		&wq_sdio->bmi.completion, tout);
#else
	timeout_left = wait_event_timeout(wq_sdio->bmi.wait_q,
					  wq_sdio->bmi.woken, tout);
#endif
	if (!timeout_left || timeout_left < 0) {
		WQ_DBG(DM_TRBUS, DL_ERR, "[auto]ASSERT : dnld cmd ERR %d\n",
		       timeout_left);
		hif_dump_info(&wq_sdio->core);
		return -ETIMEDOUT;
	}

	spin_lock_bh(&wq_sdio->bmi.lock);
	if (wq_sdio->bmi.reply) {
		if (rsp) {
			struct wq_sdio_bmi_msg *bmi_reply =
				(void *)wq_sdio->bmi.reply->data;
			u32 actual = ntohs(bmi_reply->length);

			WQ_DBG(DM_TRBUS, DL_VRB,
			       "%s, got reply, rsp_len %d, actual %d\n",
			       __func__, rsp_len, actual);
			if (rsp_len > actual)
				rsp_len = actual;

			memcpy(rsp, bmi_reply->data, rsp_len);
			// WQ_DBG(DM_TRBUS, DL_ERR, "resp data %d %d %d %d %d\n", bmi_reply->data[0], bmi_reply->data[1], bmi_reply->data[2], bmi_reply->data[3], bmi_reply->data[4]);
		}
		dev_kfree_skb_any(wq_sdio->bmi.reply);
		wq_sdio->bmi.reply = NULL;
	}
	spin_unlock_bh(&wq_sdio->bmi.lock);

	return rsp_len;
}
