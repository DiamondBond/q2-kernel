#define WQ_LOG_DM DM_TRBUS

/****************************
 * Include
 ****************************/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/firmware.h>
#include <linux/vmalloc.h>
#include <linux/ip.h>

#include "usb.h"
#include "bmi_core.h"
#include "hif_api.h"
#include "wq_wifi_dbg.h"
#include "coex.h"
#include "dtop_main.h"
#include "wq_log.h"
#include "usb_bundle.h"
#include "rwnx_txq.h"
#include "rwnx_defs.h"
#include "fw_log.h"
#include "fw_api/wifi/mac/dp_tx.h"

/****************************
 * USB Configuration
 ****************************/
// EP type
#define WQ_USB_EPTYPE_CTRL 0x00
#define WQ_USB_EPTYPE_ISOC 0x01
#define WQ_USB_EPTYPE_BULK 0x02
#define WQ_USB_EPTYPE_INT 0x03

// EP number
#define WQ_USB_NUM_OF_EP 10

// URB number
#define WQ_MSGOUT_URB_NUM 3
#define WQ_MSGIN_URB_NUM 150
#define WQ_BULKRX_URB_NUM 15
#define WQ_BULKTX_URB_NUM 100

// USB auto pm
#define WQ_USB_AUTOPM_DELAY 2000

#define WIFI_LOG "[WiFi] "

#define WQ_MSG_MAGIC BMI_MSG_MAGIC

char *fw_dtop_usb = NULL;
module_param(fw_dtop_usb, charp, 0);
MODULE_PARM_DESC(fw_dtop_usb, "dtop firmware name for usb, default: null.");

char *fw_wifi_usb = NULL;
module_param(fw_wifi_usb, charp, 0);
MODULE_PARM_DESC(fw_wifi_usb, "wifi firmware name for usb, default: null.");

static struct wq_usb_recovery_ctx {
	bool bus_dead;
	struct completion close_by_user;
} wq_usb_recovery;

#if 0
static int wq_usb_autopm_get(struct wq_core *core)
{
	struct wq_usb *wq_usb = container_of(core, struct wq_usb, core);

	ENTER();

	return usb_autopm_get_interface(wq_usb->intf);
}

static void wq_usb_autopm_put(struct wq_core *core)
{
	struct wq_usb *wq_usb = container_of(core, struct wq_usb, core);

	ENTER();

	usb_autopm_put_interface(wq_usb->intf);
}

static void wq_usb_autopm_enable(struct wq_core *core, int enable)
{
	struct wq_usb *wq_usb = container_of(core, struct wq_usb, core);

	WQ_DBG(DM_TRBUS, DL_WRN, "%s: enable %d\n", __func__, enable);

	if (enable)
		usb_enable_autosuspend(wq_usb->usbdev);
	else
		usb_disable_autosuspend(wq_usb->usbdev);
}
#endif

#ifdef FIXME
static void wq_usb_autopm_resume(struct wq_core *core)
{
#ifdef CONFIG_PM
	struct wq_usb *wq_usb = container_of(core, struct wq_usb, core);

	while (wait_for_completion_timeout(&wq_usb->resume_wait,
					   msecs_to_jiffies(1000))) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
		reinit_completion(&wq_usb->resume_wait);
#else
		wq_usb->resume_wait.done = 0;
#endif
		WQ_DBG(DM_TRBUS, DL_WRN, "%s", __func__);
	}
#endif

	wq_core_suspend_set(core, false);
}
#endif

static void wq_usb_dump_info(struct wq_core *core)
{
	struct wq_usb *wq_usb = container_of(core, struct wq_usb, core);

	WQ_DBG(DM_TRBUS, DL_ERR,
	       "%s: free in pool: pktin=%d, pktout=%d, msgin=%d, msgout=%d\n",
	       __func__, wq_usb->pools.pktin.list.num,
	       wq_usb->pools.pktout.list.num, wq_usb->pools.msgin.list.num,
	       wq_usb->pools.msgout.list.num);

	spin_lock_bh(&wq_usb->usb_out_info_lock);
	if (wq_usb->usb_out_times) {
		WQ_DBG(DM_TRBUS, DL_ERR, "usb out bundle avg: %d.%d\n",
		       (wq_usb->usb_out_pkt_count * 10 /
			wq_usb->usb_out_times) /
			       10,
		       (wq_usb->usb_out_pkt_count * 10 /
			wq_usb->usb_out_times) %
			       10);
		wq_usb->usb_out_pkt_count = 0;
		wq_usb->usb_out_times = 0;
	}
	spin_unlock_bh(&wq_usb->usb_out_info_lock);
}

static void __wq_usb_send_trigger(struct wq_core *core,
				  enum wq_usb_trigger_type type, u16 info)
{
	u32 param;
	void *data;

	WQ_DBG(DM_TRBUS, DL_ERR, "%s: type=%d, info=%d\n", __func__, type,
	       info);

	// dump usb state
	wq_usb_dump_info(core);

	// USB spy monitors vendor request 0x41, and save latest USB packets.
	param = 0xAAAABBBB;
	data = kzalloc(max((u16)type, info), GFP_KERNEL);

	bmi_cmd(core, 0x41, &param, sizeof(param), type ? &data : NULL, type,
		HZ);

	bmi_cmd(core, 0x41, &param, sizeof(param), info ? &data : NULL, info,
		HZ);
	bmi_cmd(core, 0x41, &param, sizeof(param), info ? &data : NULL, info,
		HZ);
	bmi_cmd(core, 0x41, &param, sizeof(param), info ? &data : NULL, info,
		HZ);
	bmi_cmd(core, 0x41, &param, sizeof(param), info ? &data : NULL, info,
		HZ);

	kfree(data);
}

static void wq_usb_trigger_work(struct work_struct *work)
{
	struct wq_usb *wq_usb = container_of(work, struct wq_usb, trigger.work);
	struct wq_usb_trigger *trigger = &wq_usb->trigger;

	WQ_DBG(DM_TRBUS, DL_WRN, "%s: cnt=%d, type=%d, info=0x%x\n", __func__,
	       trigger->cnt, trigger->type, trigger->info);

	__wq_usb_send_trigger(&wq_usb->core, trigger->type, trigger->info);
}

static void wq_usb_send_trigger(struct wq_core *core,
				enum wq_usb_trigger_type type, u16 info)
{
	struct wq_usb *wq_usb = container_of(core, struct wq_usb, core);
	struct wq_usb_trigger *trigger = &wq_usb->trigger;

	trigger->cnt++;
	WQ_DBG(DM_TRBUS, DL_WRN, "%s: cnt=%d, type=%d, info=0x%x\n", __func__,
	       trigger->cnt, type, info);
	if (!in_interrupt()) {
		__wq_usb_send_trigger(core, type, info);
	} else {
		trigger->type = type;
		trigger->info = info;
		schedule_work(&trigger->work);
	}
}

static int wq_usb_rx_urb_check(struct wq_usb *wq_usb, struct urb *urb)
{
#define WQ_USB_URB_DUMP(LVL, urb)                                              \
	do {                                                                   \
		WQ_DBG(DM_TRBUS, LVL, "%s: status=%d, actual_length=%d\n",     \
		       __func__, urb->status, urb->actual_length);             \
	} while (0)

	int status;

	if (!wq_core_is_hif_ready(&wq_usb->core))
		return -EPROTO;

	switch ((status = urb->status)) {
	case 0:
		if (!urb->actual_length)
			status = -ENODATA;
		WQ_USB_URB_DUMP(DL_VRB, urb);
		break;
	case -EPROTO:
		/* device is reset, crash or unplugged */
		//wq_core_state_set(&wq_usb->core, WQ_CORE_STATE_HIF_NREADY);
		fallthrough;
	default:
		WQ_USB_URB_DUMP(DL_ERR, urb);
		break;
	}
	return status;

#undef WQ_USB_URB_DUMP
}

static int wq_usb_pkt_in(struct wq_usb *wq_usb, struct wq_urb_ctx *ctx);

static void wq_usb_pkt_in_cb(struct urb *urb)
{
	struct wq_urb_ctx *ctx = (struct wq_urb_ctx *)urb->context;
	struct wq_usb *wq_usb = ctx->wq_usb;
	int pkt_num = 0;

	if (wq_usb == NULL)
		return;

	if (wq_usb_rx_urb_check(wq_usb, urb) == 0) {
		struct sk_buff *skb;

		skb = ctx->skb;

		//check usb transfer length
		if (WQ_USB_MTU_PKT_BUNDLE_I < urb->actual_length) {
			WQ_DBG(DM_TRBUS, DL_WRN,
			       "[auto]msg:%s: pktin_mtu=%u, actual_length=%u\n",
			       __func__, WQ_USB_MTU_PKT_BUNDLE_I,
			       urb->actual_length);
		}

		pkt_num = wq_usb_rx_get_pktnum(skb, urb->actual_length);
		if (pkt_num > 0) {
			wq_usb_rx_debundle(&wq_usb->core, WQ_QID_AC_BK, skb,
					   pkt_num);
		} else {
			//dump byte
			dump_bytes(DL_WRN, "USB IN data corrupt",
				   (uint8_t *)skb->data,
				   (urb->actual_length >= 128) ?
					   128 :
					   urb->actual_length);
			wq_usb_send_trigger(&wq_usb->core, WQ_USB_TRI_EVENT,
					    0x100);
		}

		if (wq_usb_pkt_in(wq_usb, ctx) == 0)
			usb_mark_last_busy(urb->dev);
	} else {
		wq_urb_ctx_enqueue(&wq_usb->pools.pktin.list, ctx);
	}

	return;
}

static int wq_usb_pkt_in(struct wq_usb *wq_usb, struct wq_urb_ctx *ctx)
{
	struct sk_buff *skb;
	int ret;
	struct urb *urb;

	WARN_ON(!ctx || !wq_usb);

	skb = ctx->skb;

	if (!skb) {
		WQ_DBG(DM_TRBUS, DL_ERR, "[auto]msg:%s: skb is null\n",
		       __func__);
		BUG_ON(!skb);
	}

	// check skb length
	if ((skb->len != 0) || (skb_tailroom(skb) < WQ_USB_MTU_PKT_BUNDLE_I)) {
		WQ_DBG(DM_TRBUS, DL_WRN,
		       "%s: ctx=0x%p, skb=0x%p(data=0x%p, len=%u, tailroom=%u), pktin_mtu=%u\n",
		       __func__, ctx, skb, skb->data, skb->len,
		       skb_tailroom(skb), WQ_USB_MTU_PKT_BUNDLE_I);
	} else {
		WQ_DBG(DM_TRBUS, DL_VRB,
		       "%s: ctx=0x%p, skb=0x%p(data=0x%p, len=%u, tailroom=%u)\n",
		       __func__, ctx, skb, skb->data, skb->len,
		       skb_tailroom(skb));
	}

	urb = ctx->urb;
	usb_fill_bulk_urb(urb, wq_usb->usbdev,
			  usb_rcvbulkpipe(wq_usb->usbdev, WQ_USB_EP_RX_DATA),
			  skb->data, skb_tailroom(skb), wq_usb_pkt_in_cb, ctx);

	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_WRN, "%s: Error! submit urb failed (%d)!\n",
		       __func__, ret);
		wq_urb_ctx_enqueue(&wq_usb->pools.pktin.list, ctx);
	}

	return ret;
}

static int wq_usb_msg_in(struct wq_usb *wq_usb, struct wq_urb_ctx *ctx);

static void wq_usb_msg_in_cb(struct urb *urb)
{
	struct wq_urb_ctx *ctx = (struct wq_urb_ctx *)urb->context;
	struct wq_usb *wq_usb = ctx->wq_usb;

	if (wq_usb == NULL)
		return;

	if (wq_usb_rx_urb_check(wq_usb, urb) == 0) {
		struct sk_buff *skb;

		skb = ctx->skb;
		ctx->skb = NULL;

		skb_put(skb, urb->actual_length);
		htc_rx(&wq_usb->core, WQ_QID_MSG, skb);

		if (wq_usb_msg_in(wq_usb, ctx) == 0)
			usb_mark_last_busy(urb->dev);
	} else {
		wq_urb_ctx_free(&wq_usb->pools.msgin, ctx);
	}

	return;
}

static int wq_usb_msg_in(struct wq_usb *wq_usb, struct wq_urb_ctx *ctx)
{
	struct sk_buff *skb;
	int ret;
	struct urb *urb;

	WARN_ON(!ctx || !wq_usb);

	//+1: avoid receive zero length pkt
	skb = dev_alloc_skb(WQ_USB_MTU_RX_MSG + 1);
	if (!skb) {
		WQ_DBG(DM_TRBUS, DL_WRN, "%s: Error! allocate skb failed!\n",
		       __func__);
		wq_urb_ctx_free(&wq_usb->pools.msgin, ctx);
		return -ENOMEM;
	}

	ctx->skb = skb;
	urb = ctx->urb;

#if WQ_MSG_EP_TYPE == WQ_USB_EPTYPE_BULK
	usb_fill_bulk_urb(urb, wq_usb->usbdev,
			  usb_rcvbulkpipe(wq_usb->usbdev, WQ_USB_EP_BI_MSG),
			  skb->data, skb_tailroom(skb), wq_usb_msg_in_cb, ctx);
#else
	usb_fill_int_urb(urb, wq_usb->usbdev,
			 usb_rcvintpipe(wq_usb->usbdev, WQ_USB_EP_BI_MSG),
			 skb->data, skb_tailroom(skb), wq_usb_msg_in_cb, ctx,
			 1);
#endif

	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_WRN, "%s: Error! submit urb failed (%d)!\n",
		       __func__, ret);
		wq_urb_ctx_free(&wq_usb->pools.msgin, ctx);
	}
	return ret;
}

/*
* tx pkt format: hif + htc + txdesc_host + (eth header + ip header + payload  [+ padding(for ALIGN)])
* standard:      4B    20B     96B           14B         20B         ~1480B
* compressed:    4B    12B     24B           14B/0B      20B/10B     ~1480B
* it will compress 80B(do not compress eth/ip hdr) or 104B(compress eth/ip hdr) tx desc
*/
struct eth_ip_compress_record gv_cached_eth_ip_info = {
	.ethhdr = { { 0 } },
	.ip_version_hdrlen = 0,
	.ip_dscp = 0,
	.saddr = 0,
	.daddr = 0,
};

// compress txdesc_host for imporve usb bus utilization
void compress_txdesc(struct rwnx_hw *hw, struct sk_buff *skb)
{
	struct ethhdr *ethhdr;
	struct iphdr *ip_hdr;
	struct compressed_eth_ip_hdr compressed_eth_ip_hdr;
	struct compressed_htc_v0 compressed_htc_v0;
	struct compressed_hostdesc compressed_hostdesc;
	struct wq_htc_v0 *wq_htc_v0 =
		(void *)skb->data + sizeof(struct wq_hif_hdr);
	struct txdesc_host *txdesc_host = (void *)skb->data +
					  sizeof(struct wq_hif_hdr) +
					  sizeof(struct wq_htc_v0);
	struct hostdesc *hostdesc = &txdesc_host->api.host;
	int compressd_len =
		(sizeof(struct wq_htc_v0) - sizeof(struct compressed_htc_v0)) +
		(sizeof(struct txdesc_host) -
		 sizeof(struct compressed_hostdesc));

	ethhdr = (struct ethhdr *)(txdesc_host + 1);
	ip_hdr = (struct iphdr *)(ethhdr + 1);

	//dump_bytes(DL_WRN, "1 compress skb->data", skb->data, skb->len);
	atomic_inc(&hw->usb_send_cnt);
	memset(&compressed_hostdesc, 0, sizeof(struct compressed_hostdesc));
	if ((memcmp((u8 *)&gv_cached_eth_ip_info, (u8 *)ethhdr,
		    sizeof(struct ethhdr) + 2) == 0) &&
	    gv_cached_eth_ip_info.saddr == ip_hdr->saddr &&
	    gv_cached_eth_ip_info.daddr == ip_hdr->daddr
	    //in order to prevent and restore abnormal conditions, only compress eth/iphdr if throughput > 200Mbps
	    && hw->tx_throughput > 200) {
		//compress ethhdr/iphdr if these bytes in gv_cached_eth_ip_info are the same with last time
		compressed_hostdesc.compress_eth_ip_flag = 1;
		atomic_inc(&hw->usb_com_ethip);
		compressd_len += sizeof(struct eth_ip_compress_record);
		compressed_eth_ip_hdr.tot_len = ip_hdr->tot_len;
		compressed_eth_ip_hdr.id = ip_hdr->id;
		compressed_eth_ip_hdr.frag_off = ip_hdr->frag_off;
		compressed_eth_ip_hdr.ttl = ip_hdr->ttl;
		compressed_eth_ip_hdr.protocol = ip_hdr->protocol;
		compressed_eth_ip_hdr.check = ip_hdr->check;
		//dump_bytes(DL_WRN, "compress_eth_ip", (u8 *)&compressed_eth_ip_hdr, sizeof(compressed_eth_ip_hdr));
	} else {
		// record ethhdr/iphdr if these bytes in eth_ip_com_g are not the same with last time
		// copy ethhdr, ip->ihl, ip->version, ip->tos
		memcpy(&gv_cached_eth_ip_info, (u8 *)ethhdr,
		       sizeof(struct ethhdr) + 2);
		gv_cached_eth_ip_info.saddr = ip_hdr->saddr;
		gv_cached_eth_ip_info.daddr = ip_hdr->daddr;
		//dump_bytes(DL_WRN, "not compress record gv_cached_eth_ip", (u8 *)&gv_cached_eth_ip_info, sizeof(gv_cached_eth_ip_info));
	}

	//copy wq_htc_v0 to compressed_htc_v0
	memcpy(&compressed_htc_v0, wq_htc_v0,
	       sizeof(wq_htc_v0->flags) + sizeof(wq_htc_v0->u));
	compressed_htc_v0.buf_len = wq_htc_v0->buf_len;

	// copy hostdesc to compressed_hostdesc
	// (some member variables are not processed ​​because drv is not used for them, and they are just retained for ALIGN)
	memcpy(&compressed_hostdesc.packet_len, &hostdesc->packet_len,
	       sizeof(compressed_hostdesc.packet_len));
	compressed_hostdesc.packet_cnt = hostdesc->packet_cnt;
	compressed_hostdesc.tid = hostdesc->tid;
	compressed_hostdesc.vif_idx = hostdesc->vif_idx;
	compressed_hostdesc.staid = hostdesc->staid;
	compressed_hostdesc.flags = hostdesc->flags;
	compressed_hostdesc.mgmt_frame_nb = hostdesc->mgmt_frame_nb;
	compressed_hostdesc.via_grp_id = hostdesc->via_grp_id;
	compressed_hostdesc.via_type_id = hostdesc->via_type_id;
	compressed_hostdesc.is_hml = hostdesc->is_hml;
	compressed_hostdesc.encap_type = hostdesc->encap_type;
	//compressed_hostdesc.tae_mode = hostdesc->tae_mode;
	//compressed_hostdesc.need_info_host_when_tx_done = hostdesc->need_info_host_when_tx_done;
	compressed_hostdesc.dhcp_flag = hostdesc->dhcp_flag;
	//compressed_hostdesc.txdesc_host_freed = hostdesc->txdesc_host_freed;
	compressed_hostdesc.ext_flags = hostdesc->ext_flags;
	//compressed_hostdesc.ds_probe_mac1 = hostdesc->ds_probe_mac1;
	//compressed_hostdesc.send_limit = hostdesc->send_limit;

	// write hif-header into skb->data+compressd_len
	memmove((u8 *)skb->data + compressd_len, (u8 *)skb->data,
		sizeof(struct wq_hif_hdr));
	// write compressed_htc_v0 into skb->data+compressd_len+hif_len
	memcpy((u8 *)skb->data + compressd_len + sizeof(struct wq_hif_hdr),
	       (u8 *)&compressed_htc_v0, sizeof(struct compressed_htc_v0));
	// write compressed_hostdesc into skb->data+compressd_len+hif_len+htc_len
	memcpy((u8 *)skb->data + compressd_len + sizeof(struct wq_hif_hdr) +
		       sizeof(struct compressed_htc_v0),
	       (u8 *)&compressed_hostdesc, sizeof(struct compressed_hostdesc));

	// write compressed_eth_ip_hdr if ethhdr and iphdr are compressed
	if (compressed_hostdesc.compress_eth_ip_flag) {
		memcpy((u8 *)skb->data + compressd_len +
			       sizeof(struct wq_hif_hdr) +
			       sizeof(struct compressed_htc_v0) +
			       sizeof(struct compressed_hostdesc),
		       (u8 *)&compressed_eth_ip_hdr,
		       sizeof(struct compressed_eth_ip_hdr));
	}
	skb_pull(skb, compressd_len);

	//dump_bytes(DL_WRN, "2 compress skb->data", skb->data, skb->len);
}

void decompress_txdesc(struct sk_buff *skb)
{
	struct wq_htc_v0 wq_htc_v0;
	struct compressed_htc_v0 *compressed_htc_v0 =
		(void *)skb->data + sizeof(struct wq_hif_hdr);
	struct txdesc_host txdesc_host;
	struct hostdesc *hostdesc = &txdesc_host.api.host;
	struct compressed_hostdesc *compressed_hostdesc =
		(void *)skb->data + sizeof(struct wq_hif_hdr) +
		sizeof(struct compressed_htc_v0);
	int decompressd_len =
		(sizeof(struct wq_htc_v0) - sizeof(struct compressed_htc_v0)) +
		(sizeof(struct txdesc_host) -
		 sizeof(struct compressed_hostdesc));

	if (compressed_hostdesc->compress_eth_ip_flag) {
		decompressd_len += sizeof(struct eth_ip_compress_record);
	}

	//dump_bytes(DL_WRN, "1 decompress skb->data", skb->data, skb->len);

	// copy compressed_htc_v0 to wq_htc_v0
	memset(&wq_htc_v0, 0, sizeof(struct wq_htc_v0));
	memcpy(&wq_htc_v0, compressed_htc_v0,
	       sizeof(wq_htc_v0.flags) + sizeof(wq_htc_v0.u));
	wq_htc_v0.buf_len = compressed_htc_v0->buf_len;

	// copy compressed_hostdesc to hostdesc
	// (some member variables are not processed ​​because drv is not used for them, and they are just retained for ALIGN)
	memset(&txdesc_host, 0, sizeof(struct txdesc_host));
	memcpy(&hostdesc->packet_len, &compressed_hostdesc->packet_len,
	       sizeof(hostdesc->packet_len));
	hostdesc->packet_cnt = compressed_hostdesc->packet_cnt;
	hostdesc->tid = compressed_hostdesc->tid;
	hostdesc->vif_idx = compressed_hostdesc->vif_idx;
	hostdesc->staid = compressed_hostdesc->staid;
	hostdesc->flags = compressed_hostdesc->flags;
	hostdesc->mgmt_frame_nb = compressed_hostdesc->mgmt_frame_nb;
	hostdesc->via_grp_id = compressed_hostdesc->via_grp_id;
	hostdesc->via_type_id = compressed_hostdesc->via_type_id;
	hostdesc->is_hml = compressed_hostdesc->is_hml;
	hostdesc->encap_type = compressed_hostdesc->encap_type;
	//hostdesc->tae_mode = compressed_hostdesc->tae_mode;
	//hostdesc->need_info_host_when_tx_done = compressed_hostdesc->need_info_host_when_tx_done;
	hostdesc->dhcp_flag = compressed_hostdesc->dhcp_flag;
	//hostdesc->txdesc_host_freed = compressed_hostdesc->txdesc_host_freed;
	hostdesc->ext_flags = compressed_hostdesc->ext_flags;
	//hostdesc->ds_probe_mac1 = compressed_hostdesc->ds_probe_mac1;
	//hostdesc->send_limit = compressed_hostdesc->send_limit;

	// set end_marker to HOST_DESC_END_MARKER, so it will not be checked as pattern
	// (there will be check wq_hif_hdr->ptn as magic pattern)
	hostdesc->end_marker = HOST_DESC_END_MARKER;

	// write hif-header into skb->data-decompressd_len
	memmove((u8 *)skb->data - decompressd_len, (u8 *)skb->data,
		sizeof(struct wq_hif_hdr));
	// write wq_htc_v0 into skb->data-compressd_len+hif_len
	memcpy((u8 *)skb->data - decompressd_len + sizeof(struct wq_hif_hdr),
	       (u8 *)&wq_htc_v0, sizeof(struct wq_htc_v0));
	// write txdesc_host into skb->data-compressd_len+hif_len+htc_len
	memcpy((u8 *)skb->data - decompressd_len + sizeof(struct wq_hif_hdr) +
		       sizeof(struct wq_htc_v0),
	       (u8 *)&txdesc_host, sizeof(struct txdesc_host));
	skb_push(skb, decompressd_len);

	//dump_bytes(DL_WRN, "2 decompress skb->data", skb->data, skb->len);
}

static void wq_usb_pkt_out_cb(struct urb *urb)
{
	struct wq_urb_ctx *ctx = (struct wq_urb_ctx *)urb->context;
	struct wq_usb *wq_usb = ctx->wq_usb;

	if (wq_usb->core.hw->mod_params.compress_txdesc &&
	    THRESHOLD_USB_OUT_BUNDLE_MAX == 1) {
		decompress_txdesc(ctx->skb);
	}

	htc_tx_done(&wq_usb->core, ctx->skb, urb->status);

	ctx->skb = NULL; /* skb is already taken by htc_tx_done */
	wq_urb_ctx_free(&wq_usb->pools.pktout, ctx);
}

static const u8 ep_map[WQ_QID_MAX] = {
	WQ_USB_EP_TX_BK,  WQ_USB_EP_TX_BE, WQ_USB_EP_TX_VI, WQ_USB_EP_TX_VO,

	WQ_USB_EP_BI_MSG,
};

static int wq_usb_tx_pkt(struct wq_core *core, enum wq_hif_qid qid,
			 struct sk_buff *skb)
{
	struct wq_usb *wq_usb = container_of(core, struct wq_usb, core);
	struct wq_urb_ctx *ctx;
	struct urb *urb;
	int ret;
	u8 ep_id;

	WQ_DBG(DM_TRBUS, DL_VRB, "%s: txq=%d, skb=0x%p, len=%d\n", __func__,
	       qid, skb, skb->len);
	BUG_ON(qid >= WQ_QID_AC_MAX);

	// urb preparation
	ctx = wq_urb_ctx_alloc(&wq_usb->pools.pktout);
	if (!ctx) {
		WQ_DBG(DM_TRBUS, DL_WRN, "%s: no ctx to send\n", __func__);
		return -ENOMEM;
	}

	if (core->config.remap_to_be == true)
		ep_id = WQ_USB_EP_TX_BE;
	else
		ep_id = ep_map[qid];

	// compress struct txdesc_host if fw also supports compression
	if (core->hw->mod_params.compress_txdesc &&
	    THRESHOLD_USB_OUT_BUNDLE_MAX == 1) {
		compress_txdesc(core->hw, skb);
	}

	ctx->skb = skb;
	urb = ctx->urb;

	usb_fill_bulk_urb(urb, wq_usb->usbdev,
			  usb_sndbulkpipe(wq_usb->usbdev, ep_id), skb->data,
			  skb->len, wq_usb_pkt_out_cb, ctx);
	urb->transfer_flags |= URB_ZERO_PACKET;

	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: usb_submit_urb FAILED\n",
		       __func__);

		/* the caller wq_ipc_tx_pkt() will free skb */
		ctx->skb = NULL;
		wq_urb_ctx_free(&wq_usb->pools.pktout, ctx);
	}
	return ret;
}

static void wq_usb_msg_out_cb(struct urb *urb)
{
	struct wq_urb_ctx *ctx = (struct wq_urb_ctx *)urb->context;

	if (ctx) {
		struct wq_usb *wq_usb = ctx->wq_usb;

		htc_tx_done(&wq_usb->core, ctx->skb, urb->status);

		ctx->skb = NULL;
		wq_urb_ctx_free(&wq_usb->pools.msgout, ctx);
	} else {
		WQ_DBG(DM_TRBUS, DL_OOPS,
		       "%s: urb context is null !, status=%d\n", __func__,
		       urb->status);
	}
}

static int wq_usb_tx_msg(struct wq_core *core, enum wq_hif_qid qid,
			 struct sk_buff *skb)
{
	struct wq_usb *wq_usb = container_of(core, struct wq_usb, core);
	struct wq_urb_ctx *ctx;
	struct urb *urb;
	u8 ep_id;
	int ret = 0;

	WQ_DBG(DM_TRBUS, DL_VRB, "%s: skb=0x%p, skb->len=%d\n", __func__, skb,
	       skb->len);
	BUG_ON(qid != WQ_QID_MSG);
	ep_id = ep_map[qid];

	// 1. check input
	if ((skb->len > WQ_USB_MTU_TX_MSG) || (skb->len == 0)) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: Error! len=%d\n", __func__,
		       skb->len);
		return -EINVAL;
	}

	ctx = wq_urb_ctx_alloc(&wq_usb->pools.msgout);
	if (!ctx) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: no ctx for msg out\n", __func__);
		return -ENOSPC;
	}

	ctx->skb = skb;
	urb = usb_alloc_urb(0, GFP_ATOMIC);

	if (!urb) {
		wq_urb_ctx_free(&wq_usb->pools.msgout, ctx);
		return -ENOMEM;
	}

#if WQ_MSG_EP_TYPE == WQ_USB_EPTYPE_BULK
	usb_fill_bulk_urb(urb, wq_usb->usbdev,
			  usb_sndbulkpipe(wq_usb->usbdev, ep_id), skb->data,
			  skb->len, wq_usb_msg_out_cb, ctx);
	urb->transfer_flags |= URB_ZERO_PACKET;
#else
	usb_fill_int_urb(urb, wq_usb->usbdev,
			 usb_sndintpipe(wq_usb->usbdev, ep_id), skb->data,
			 skb->len, wq_usb_msg_out_cb, ctx, 1);
#endif

	wq_usb->cmd_start_time = ktime_get();
	usb_anchor_urb(urb, &wq_usb->anchors.msgout);
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: usb_submit_urb FAILED\n",
		       __func__);
		ctx->skb = NULL;
		usb_unanchor_urb(urb);
		wq_urb_ctx_free(&wq_usb->pools.msgout, ctx);
	}
	usb_free_urb(urb);

	return ret;
}

static int wq_usb_tx(struct wq_core *core, enum wq_hif_qid qid,
		     struct sk_buff_head *skbq)
{
	int skb_count = 0;
	int ret = 0;
	struct sk_buff *skb_bundle, *skb_temp;
	struct txdesc_host *desc;
	struct wq_skb_txcb *txcb, *txcb_skb_bundle;
	// sk_list_free store the skb send fail to release and return_dev_credit_ex
	struct sk_buff_head sk_list_free;
	int i = 0;
	int temp_skbq_len;
	struct wq_usb *wq_usb;

	wq_usb = container_of(core, struct wq_usb, core);
	
	if(wq_usb_recovery.bus_dead) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: fw crashed!\n", __func__);
		return -ENXIO;
	}

	if (qid == WQ_QID_MSG) {
		skb_temp = __skb_dequeue(skbq);

		BUG_ON(!skb_temp);
		BUG_ON(skb_queue_len(skbq));

		ret = wq_usb_tx_msg(core, qid, skb_temp);
		//queue skb if tx fail for free it
		if (ret) {
			__skb_queue_tail(skbq, skb_temp);
		}

	} else {
		__skb_queue_head_init(&sk_list_free);

		// if the pkt is a mgmt, there must be only one pkt in the skbq
		skb_temp = skb_peek(skbq);
		txcb = WQ_SKB_TXCB(skb_temp);
		// do not bundle if the pkt is mgmt
		if (txcb->pkt_cls & BIT(WQ_PKT_CLS_80211)) {
			skb_temp = __skb_dequeue(skbq);
			ret = wq_usb_tx_pkt(core, qid, skb_temp);
			if (ret) {
				WQ_DBG(DM_TRBUS, DL_ERR, "%s: skb tx fail\n",
				       __func__);
				__skb_queue_tail(&sk_list_free, skb_temp);
			} else {
				core->hw->ipc_tx_pkt_cnt++;
			}
		}

		do {
			if (skb_queue_len(skbq) <= 0) {
				break;
			}

			// check the len of skb, enqueue skb_temp if the len is invalid for free it
			skb_queue_walk(skbq, skb_temp)
			{
				if (!skb_temp->len ||
				    skb_temp->len > WQ_USB_MTU_PKT) {
					WQ_DBG(DM_TRBUS, DL_ERR,
					       "%s: skb len error=%d (MTU %d)\n",
					       __func__, skb_temp->len,
					       WQ_USB_MTU_PKT);
					__skb_unlink(skb_temp, skbq);
					__skb_queue_tail(&sk_list_free,
							 skb_temp);
				}
			}

			// TO DO: dequeue skb from skb_out_bundle pool(link to urb), and queue skb when tx done/fail
			skb_bundle = dev_alloc_skb(
				THRESHOLD_USB_OUT_BUNDLE_MAX * USB_OUT_PKT_LEN);
			if (!skb_bundle) {
				WQ_DBG(DM_TRBUS, DL_ERR,
				       "%s:%d alloc bundle skb fail\n",
				       __func__, __LINE__);
				while ((skb_temp = __skb_dequeue(skbq)) !=
				       NULL) {
					// enqueue skb_temp if alloc skb_bundle fail for free it
					__skb_queue_tail(&sk_list_free,
							 skb_temp);
				}
				ret = -ENOMEM;
				break;
			}
			/*
            WQ_DBG(DM_TRBUS, DL_ERR, "%s: skb:%p len:%d head:%p data:%p tail:%#lx end:%#lx \n",
                __func__, skb_bundle, skb_bundle->len, skb_bundle->head, skb_bundle->data,
                (unsigned long)skb_bundle->tail, (unsigned long)skb_bundle->end);
            */

			temp_skbq_len = skb_queue_len(skbq);

			while ((skb_temp = __skb_dequeue(skbq)) != NULL) {
				txcb_skb_bundle = WQ_SKB_TXCB(skb_bundle);

				// copy skb_temp to skb_bundle and free skb_temp
				// count the number of skb in skb_bundle
				skb_count++;
				if (skb_count % THRESHOLD_USB_OUT_BUNDLE_MAX ==
					    0 ||
				    !skb_queue_len(skbq)) {
					skb_put(skb_bundle, skb_temp->len);
				} else {
					skb_put(skb_bundle, USB_OUT_PKT_LEN);
				}
				memcpy(skb_bundle->data + (skb_count -
							   1) * USB_OUT_PKT_LEN,
				       skb_temp->data, skb_temp->len);

				// copy txcb of skb_temp to skb_bundle if the skb_temp is the first of bundle
				if (skb_count == 1) {
					txcb = WQ_SKB_TXCB(skb_temp);
					memcpy(txcb_skb_bundle, txcb,
					       sizeof(struct wq_skb_txcb));
				}

				desc = (struct txdesc_host *)(skb_temp->data +
							      HEADROOM_HIF_HTC);
				txcb_skb_bundle->pkt_len +=
					desc->api.host.packet_len[0];

				dev_kfree_skb_any(skb_temp);

				// send skb_bundle if skb_count >= BUNDLE_MAX or skbq is empty
				if (skb_count >= THRESHOLD_USB_OUT_BUNDLE_MAX ||
				    !skb_queue_len(skbq)) {
					txcb_skb_bundle->usb_out_bundle_num =
						skb_count;

					spin_lock_bh(
						&wq_usb->usb_out_info_lock);
					wq_usb->usb_out_times++;
					wq_usb->usb_out_pkt_count += skb_count;
					spin_unlock_bh(
						&wq_usb->usb_out_info_lock);

					skb_count = 0;
					ret = wq_usb_tx_pkt(core, qid,
							    skb_bundle);
					// enqueue skb_bundle if tx fail for free it
					if (ret) {
						WQ_DBG(DM_TRBUS, DL_ERR,
						       "%s: skb tx fail\n",
						       __func__);
						__skb_queue_tail(&sk_list_free,
								 skb_bundle);
						while ((skb_temp = __skb_dequeue(
								skbq)) !=
						       NULL) {
							// enqueue skb_temp if alloc skb_bundle fail for free it
							__skb_queue_tail(
								&sk_list_free,
								skb_temp);
						}
						break;
					} else {
						core->hw->ipc_tx_pkt_cnt +=
							txcb_skb_bundle
								->usb_out_bundle_num;
					}

					// if skbq is not empty, alloc skb_bundle for sending next
					if (skb_queue_len(skbq) > 0) {
						skb_bundle = dev_alloc_skb(
							THRESHOLD_USB_OUT_BUNDLE_MAX *
							USB_OUT_PKT_LEN);
						if (!skb_bundle) {
							WQ_DBG(DM_TRBUS, DL_WRN,
							       "%s:%d alloc bundle skb fail\n",
							       __func__,
							       __LINE__);
							while ((skb_temp = __skb_dequeue(
									skbq)) !=
							       NULL) {
								// enqueue skb_temp if alloc skb_bundle fail for free it
								__skb_queue_tail(
									&sk_list_free,
									skb_temp);
							}
							ret = -ENOMEM;
							break;
						}
					}
				}
			}
		} while (0);

		while ((skb_temp = __skb_dequeue(&sk_list_free)) != NULL) {
			struct txdesc_host *txdesc_host =
				(struct txdesc_host *)((u8 *)skb_temp->data +
						       HEADROOM_HIF_HTC);
			struct hostdesc *host = &txdesc_host->api.host;

			txcb = WQ_SKB_TXCB(skb_temp);
			WQ_DBG(DM_IPC, DL_ERR,
			       "[auto]msg:drop skb, qid=%d skb=%p, return tx credit gid=%u, tid=%u\n",
			       qid, skb_temp, host->via_grp_id,
			       host->via_type_id);
			// return credit ex usb_out_bundle_num times for bundle_skb
			if (txcb->usb_out_bundle_num) {
				i = txcb->usb_out_bundle_num;
			}
			do {
				rwnx_return_dev_credit_ex(core->hw,
							  host->via_grp_id,
							  host->via_type_id);
				i--;
			} while (i > 0);
			dev_kfree_skb_any(skb_temp);
		}
	}

	return ret;
}

static int hif_get_hdr_sz_usb(struct wq_core *core)
{
	return sizeof(struct wq_hif_hdr);
}

static struct wq_hif_ops wq_usb_ops = {
	.hif = WQ_HIF_USB,
	.txq_stop_threshlod = RWNX_NDEV_FLOW_CTRL_USB_STOP,
	.txq_restart_threshlod = RWNX_NDEV_FLOW_CTRL_USB_RESTART,

	//	.autopm_enable = wq_usb_autopm_enable,
	//	.autopm_get = wq_usb_autopm_get,
	//	.autopm_put = wq_usb_autopm_put,

	.hif_tx = wq_usb_tx,
	.hif_get_hdr_sz = hif_get_hdr_sz_usb,

	.dump_info = wq_usb_dump_info,
	.send_trigger = wq_usb_send_trigger,

	.bmi_cmd = wq_usb_bmi_cmd,
	.bmi_xfer = wq_usb_bmi_xfer,
	.bmi_exchange = wq_usb_bmi_exchange,

#ifdef CONFIG_WQ_DTOP
	.dtop_bulk_send = wq_usb_dtop_send,
#endif
};

static void wq_usb_submit_in_urb(struct wq_usb *wq_usb)
{
	struct wq_urb_ctx *ctx;

	WQ_DBG(DM_TRBUS, DL_VRB,
	       "%s: wq_usb %p, state %s, msgin num %d, pktin num %d\n",
	       __func__, wq_usb, wq_core_state_name(&wq_usb->core),
	       wq_usb->pools.msgin.list.num, wq_usb->pools.pktin.list.num);

	while ((ctx = wq_urb_ctx_alloc(&wq_usb->pools.msgin)) != NULL)
		if (wq_usb_msg_in(wq_usb, ctx))
			break;

	while ((ctx = wq_urb_ctx_alloc(&wq_usb->pools.pktin)) != NULL)
		if (wq_usb_pkt_in(wq_usb, ctx))
			break;
}

static void wq_usb_kill_urbs(struct wq_list_pool *pool)
{
	int i = 0;
	struct wq_urb_ctx *ctx = pool->entries;

	for (i = 0; i < pool->num; i++) {
		if (!ctx->urb) {
			WQ_DBG(DM_TRBUS, DL_ERR, "%s: bad ctx\n", __func__);
			continue;
		}

		usb_kill_urb(ctx->urb);
		ctx++;
	}
}

static void wq_usb_cancel_all_urbs(struct wq_usb *wq_usb)
{
	usb_kill_anchored_urbs(&wq_usb->anchors.msgout);
	wq_usb_kill_urbs(&wq_usb->pools.pktout);
	wq_usb_kill_urbs(&wq_usb->pools.pktin);
	wq_usb_kill_urbs(&wq_usb->pools.msgin);
}

static int wq_usb_nb_notify(struct notifier_block *nb, unsigned long action,
			    void *data)
{
	struct wq_usb *wq_usb = container_of(nb, struct wq_usb, nb);
	struct device *dev = data;
	struct usb_interface *intf =
		container_of(dev, struct usb_interface, dev);

	WQ_DBG(DM_TRBUS, DL_WRN, "%s: %s, action %ld.\n", __func__,
	       dev_name(dev), action);

	if (intf != wq_usb->intf)
		return 0;

	switch (action) {
	case BUS_NOTIFY_UNBIND_DRIVER:
		WQ_DBG(DM_TRBUS, DL_WRN,
		       "usb interface \"%s\" is going to down.\n",
		       dev_name(dev));
		// wq_core_state_set(&wq_usb->core, WQ_CORE_STATE_DETACH);
#ifdef CONFIG_ROCK_CHIP_WLAN_RECOVERY
		wq_usb_recovery.bus_dead = true;
		wq_wlan_handle_bus_recovery(&wq_usb->core);
#endif
		break;
	}

	return 0;
}

static void wq_usb_disconnect(struct usb_interface *intf);

static int wq_usb_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	struct usb_device *usb = interface_to_usbdev(intf);
	struct usb_interface_descriptor *desc = &intf->altsetting[0].desc;
	struct wq_usb *wq_usb;
	u8 ep;
	int ret = 0;

	WQ_DBG(DM_TRBUS, DL_WRN,
	       WIFI_LOG
	       "%s: VID=0x%04x, PID=0x%04x, dev class=0x%x, cfg num=%d speed=%s\n",
	       __func__, id->idVendor, id->idProduct,
	       usb->descriptor.bDeviceClass, usb->descriptor.bNumConfigurations,
	       usb_speed_string(usb->speed));
	WQ_DBG(DM_TRBUS, DL_WRN,
	       WIFI_LOG
	       "%s: interface num=%d, class=0x%x, subclass=0x%x, protocol=0x%x, ep num=%d\n",
	       __func__, desc->bInterfaceNumber, desc->bInterfaceClass,
	       desc->bInterfaceSubClass, desc->bInterfaceProtocol,
	       desc->bNumEndpoints);

	//endpoint type/dir/addr
	for (ep = 0; ep < desc->bNumEndpoints; ep++) {
		struct usb_endpoint_descriptor *endpoint =
			&intf->altsetting[0].endpoint[ep].desc;

		WQ_DBG(DM_TRBUS, DL_WRN,
		       WIFI_LOG "%s: ep[%d] num=%d, type=%d, dir=%s\n",
		       __func__, ep, usb_endpoint_num(endpoint),
		       usb_endpoint_type(endpoint),
		       usb_endpoint_dir_in(endpoint) ? "IN" : "OUT");
	}

	if (usb->speed == USB_SPEED_SUPER) {
		// check endpoint number, TODO: u3 descriptors have not yet been finalized
	} else {
		// check endpoint number
		if ((desc->bNumEndpoints != WQ_USB_NUM_OF_EP) &&
			(desc->bNumEndpoints != (WQ_USB_NUM_OF_EP - 1)) &&
			(desc->bNumEndpoints != (WQ_USB_NUM_OF_EP - 3))) {
			WQ_DBG(DM_TRBUS, DL_ERR,
				WIFI_LOG "%s: endpoint number(%u) mismatch!\n", __func__,
				desc->bNumEndpoints);
			WQ_ERROR(USB_EP_NUM_ERROR);
			return -ENODEV;
		}
	}

	//1. device info preparation
	wq_usb = (struct wq_usb *)wq_core_create(
		&wq_usb_ops, &usb->dev, WQ_WPHY_PF_QFN_USB, sizeof(*wq_usb));
	if (wq_usb == NULL) {
		WQ_ERROR(UDEV_ALLOC_FAIL);
		return -ENOMEM;
	}

	usb_get_dev(usb);
	usb_set_intfdata(intf, wq_usb);

	wq_usb->usbdev = usb;
	wq_usb->intf = intf;

	init_completion(&wq_usb->resume_wait);

	init_usb_anchor(&wq_usb->anchors.msgout);

	init_waitqueue_head(&wq_usb->bmi.wait_q);

	spin_lock_init(&wq_usb->usb_out_info_lock);

	wq_usb->cmd_start_time = ktime_get();

	intf->needs_remote_wakeup = 1;
	pm_runtime_set_autosuspend_delay(&usb->dev, WQ_USB_AUTOPM_DELAY);
	// disable autopm
	usb_disable_autosuspend(usb);

	wq_usb->nb.notifier_call = wq_usb_nb_notify;
	bus_register_notifier(usb->dev.bus, &wq_usb->nb);

	INIT_WORK(&wq_usb->trigger.work, wq_usb_trigger_work);

	WQ_DBG(DM_TRBUS, DL_WRN, "%s: pm=%u | msg ep type=%d\n", __func__,
	       IS_ENABLED(CONFIG_PM), WQ_MSG_EP_TYPE);

	ret = wq_usb_pools_init(wq_usb);
	if (ret) {
		WQ_ERROR(URB_INIT_FAIL);
		WQ_DBG(DM_TRBUS, DL_ERR, WIFI_LOG "%s: pool init failed (%d)\n",
		       __func__, ret);
		goto fail;
	}

	ret = wq_fw_name_update(&wq_usb->core, fw_dtop_usb, fw_wifi_usb);
	if (ret)
		goto fail;

	/* start to download firmware */
	ret = wq_fw_dtop_init(&wq_usb->core);
	if (ret)
		goto fail;

	ret = wq_fw_init(&wq_usb->core);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR, "Firmware Init Failed\n");
		goto fail;
	}

	wq_usb_submit_in_urb(wq_usb);

	// wait device ready
	ret = wq_wlan_fw_ready(&wq_usb->core, 2000);
	if (ret)
		goto fail;

	ret = wq_wlan_create(&wq_usb->core, 1);
	if (ret)
		goto fail;

#ifdef CONFIG_WQ_DTOP
	ret = wq_usb_app_init(wq_usb);
	if (ret)
		goto fail;

	wq_usb->dtop_inited = 1;
#endif
	wq_fw_log_proc_init(&wq_usb->core);
	wq_usb_recovery.bus_dead = false;

	/* device initialize success */
	LEAVE();

	return 0;

fail:
	WQ_DBG(DM_TRBUS, DL_ERR,
	       WIFI_LOG "%s: establish communication failed (dev=%s, err=%d)\n",
	       __func__, dev_name(wq_usb->core.dev), ret);
	wq_usb_disconnect(intf);

	LEAVE();

	return ret;
}

static void wq_usb_wifi_shutdown(struct wq_usb *wq_usb)
{
	int ret = 0;

	WQ_DBG(DM_TRBUS, DL_WRN, "wifi shutdown! \n");
	ret = usb_control_msg(wq_usb->usbdev,
			      usb_sndctrlpipe(wq_usb->usbdev, 0),
			      WQ_VREQ_ID_FW_WIFI_REMOVE,
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      ((WQ_MSG_MAGIC >> 16) & 0xffff),
			      (WQ_MSG_MAGIC & 0xffff), NULL, 0, 1500);
	if (ret < 0)
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: failed - err=%d\n", __func__,
		       ret);

	return;
}

static void wq_usb_disconnect(struct usb_interface *intf)
{
	struct usb_device *usb = interface_to_usbdev(intf);
	struct wq_usb *wq_usb = (struct wq_usb *)usb_get_intfdata(intf);
	struct rwnx_hw *rwnx_hw = wq_usb->core.hw;
	int32_t leave_time;

	WQ_DBG(DM_GENERIC, DL_WRN, "%s\n", __func__);

	WARN_ON(!wq_usb);
	if (wq_usb_recovery.bus_dead) {
		leave_time = wait_for_completion_interruptible_timeout(
			&wq_usb_recovery.close_by_user, msecs_to_jiffies(1000));
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: fw crashed!, leave_time: %d\n",
		       __func__, leave_time);
	}

	/* abort scan request */
	if (rwnx_hw) {
		if (rwnx_hw->scan_request) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
			struct cfg80211_scan_info info = {
				.aborted = true,
			};
			cfg80211_scan_done(rwnx_hw->scan_request, &info);
#else
			cfg80211_scan_done(rwnx_hw->scan_request, true);
#endif
			WQ_DBG(DM_GENERIC, DL_WRN, "abort scan\n");

			rwnx_hw->scan_request = NULL;

			mutex_unlock(&rwnx_hw->mutex);
		} else if (rwnx_hw->roc) {
			struct rwnx_roc *roc = rwnx_hw->roc;
			struct rwnx_vif *rwnx_vif = roc->vif;

			WQ_DBG(DM_GENERIC, DL_WRN, "abort roc %d %d\n",
			       roc->internal, roc->on_chan);

			if (!roc->internal && roc->on_chan) {
				// If RoC has been started by the user space and hasn't been cancelled,
				// inform it that off-channel period has expired
				cfg80211_remain_on_channel_expired(
					&rwnx_vif->wdev,
					(u64)(rwnx_hw->roc_cookie), roc->chan,
					GFP_ATOMIC);
			}

			kfree(roc);
			rwnx_hw->roc = NULL;
			complete(&rwnx_hw->roc_wait);

			mutex_unlock(&rwnx_hw->mutex);
		}
	}

	cancel_work_sync(&wq_usb->trigger.work);

	if (wq_usb->nb.notifier_call)
		bus_unregister_notifier(usb->dev.bus, &wq_usb->nb);

#ifdef CONFIG_WQ_DTOP
	if (wq_usb->dtop_inited)
		wq_usb_app_deinit(wq_usb);
#endif
	wq_fw_log_proc_deinit(&wq_usb->core);

	wq_usb_wifi_shutdown(wq_usb);
	// Some cmd may be sent during unregister wlan. If cmd_mgr has cmd waiting for cfm/ack,
	// subsequent cmd cannot be pushed and cmd time-out may occur. So drain cmd here.
	wq_wlan_cmd_mgr_drain(&wq_usb->core);

	wq_wlan_unregister(&wq_usb->core);

	wq_wlan_destroy(&wq_usb->core);

	wq_usb_cancel_all_urbs(wq_usb);
	wq_usb_pools_deinit(wq_usb);

	wq_core_destroy(&wq_usb->core);

	usb_set_intfdata(intf, NULL);

	usb_put_dev(usb);

	WQ_DBG(DM_TRBUS, DL_WRN, "%s: exit, wq_usb=0x%p\n", __func__, wq_usb);
}

#ifdef CONFIG_PM
static int wq_usb_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct wq_usb *wq_usb = (struct wq_usb *)usb_get_intfdata(intf);
	int ret = 0;

	ENTER();

	while (((u64)ktime_to_ms(ktime_sub(ktime_get(),
					   wq_usb->cmd_start_time))) < 1000) {
		msleep(1000);
		WQ_DBG(DM_TRBUS, DL_WRN, "%s: wait wifi suspend", __func__);
	}

	// Notify FW PM change
	wq_core_suspend_set(&wq_usb->core, 1);

	// wait it complete
	ret = usb_wait_anchor_empty_timeout(&wq_usb->anchors.msgout, 1000);
	if (ret == 0) {
		WQ_DBG(DM_TRBUS, DL_WRN, "%s: wait timeout!", __func__);
	} else {
		WQ_DBG(DM_TRBUS, DL_WRN, "%s: wait succ!", __func__);
	}

	// recall in urb
	wq_usb_cancel_all_urbs(wq_usb);

	// enable a device to wake up the system
	device_wakeup_enable(wq_usb->core.dev);

	LEAVE();

	return 0;
}

static int wq_usb_resume(struct usb_interface *intf)
{
	struct wq_usb *wq_usb = (struct wq_usb *)usb_get_intfdata(intf);

	ENTER();

	// disable a device to wake up the system
	device_wakeup_disable(wq_usb->core.dev);

	// re-fill urb
	wq_usb_submit_in_urb(wq_usb);

	// Notify FW PM change
	wq_core_suspend_set(&wq_usb->core, 0);

	if (wq_usb->core.flags.suspend)
		complete(&wq_usb->resume_wait);

	LEAVE();

	return 0;
}

static int wq_usb_reset_resume(struct usb_interface *intf)
{
	ENTER();

	return wq_usb_resume(intf);
}
#endif

/* table of devices that work with this driver */
static const struct usb_device_id wq_usb_id_table[] = {
	/* support list (Vendor ID, Product ID) */
	{ USB_DEVICE(0x0FFE, 0x0001) },
	// the VID and PID of current USB device is 0x0FFE and 0x3
	{ USB_DEVICE_INTERFACE_NUMBER(0x0FFE, 0x0003, 1) },
	// the VID and PID of current USB device is 0x0FFE and 0x4, for u3
	{ USB_DEVICE_INTERFACE_NUMBER(0x0FFE, 0x0004, 0) },
	// the VID and PID of current USB device is 0x0FFE and 0x5
	{ USB_DEVICE_INTERFACE_NUMBER(0x0FFE, 0x0005, 1) },
	// the VID and PID of current USB device is 0x1FFE and 0x3
	{ USB_DEVICE_INTERFACE_NUMBER(0x1FFE, 0x0003, 1) },
	{ /* Terminating entry */ },
};

MODULE_DEVICE_TABLE(usb, wq_usb_id_table);

static struct usb_driver wq_usb_driver = {
	.name = "wq_usb",
	.probe = wq_usb_probe,
	.disconnect = wq_usb_disconnect,
	.id_table = wq_usb_id_table,
#ifdef CONFIG_PM
	.suspend = wq_usb_suspend,
	.resume = wq_usb_resume,
	.reset_resume = wq_usb_reset_resume,
	.supports_autosuspend = true,
#endif
	//.soft_unbind = 1,
	.disable_hub_initiated_lpm = 1,
};

int __init wq_usb_init(void)
{
	int ret;

	wq_module_init();

	ret = usb_register(&wq_usb_driver);
	if (ret)
		WQ_DBG(DM_TRBUS, DL_INF, "USB Driver Registration Failed\n");
	init_completion(&wq_usb_recovery.close_by_user);

	return ret;
}

void __exit wq_usb_exit(void)
{
	complete(&wq_usb_recovery.close_by_user);
	usb_deregister(&wq_usb_driver);
	wq_module_exit();
}

#ifndef WQ_WLAN_ALL_IN_ONE
module_init(wq_usb_init);
module_exit(wq_usb_exit);
#endif

MODULE_AUTHOR("WuQi Technologies");
MODULE_DESCRIPTION("Driver support for WuQi 802.11ax WLAN USB devices");
MODULE_LICENSE("Dual BSD/GPL");
#ifdef MODULE_IMPORT_NS
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
#endif
