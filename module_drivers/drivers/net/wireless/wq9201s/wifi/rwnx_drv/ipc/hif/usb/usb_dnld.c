/** @file usb_dnld.c
 *
 *  @brief This file contains FW download functions.
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
#include "usb.h"
#include "bmi_core.h"
#include "bmi_cmd.h"
#include "wq_wifi_dbg.h"
#include "wq_log.h"

#define BMI_MSG_MAGIC 0x474d5157 /* little endian "WQMG" */

static void wq_usb_bmi_snd_msg_cb(struct urb *urb)
{
	struct wq_usb *wq_usb = (struct wq_usb *)urb->context;

	if (urb->status) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: urb status error %d\n", __func__,
		       urb->status);
		return;
	}

	WARN_ON(urb->transfer_buffer_length != urb->actual_length);
	WQ_DBG(DM_TRBUS, DL_VRB, "%s: should send len:%d ,sucess len:%d \n",
	       __func__, urb->transfer_buffer_length, urb->actual_length);

	wq_usb->bmi.woken = true;
	wake_up(&wq_usb->bmi.wait_q);
}

static u8 wq_usb_bmi_get_wifi_ep(struct wq_usb *wq_usb)
{
	if (wq_usb->usbdev->speed == USB_SPEED_SUPER) {
		return WQ_USB3_EP_WIFI_BMI;
	} else {
		return WQ_USB_EP_WIFI_BMI;
	}
}

static int wq_usb_bmi_snd_msg(struct wq_usb *wq_usb, u8 ep_id, u8 *data,
			      u32 size, int timeout)
{
	int ret = 0;
	int timeout_left;
	struct urb *urb;
	unsigned long tout = msecs_to_jiffies(timeout);
	u8 wifi_bmi_ep = wq_usb_bmi_get_wifi_ep(wq_usb);

	wq_usb->bmi.woken = false;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		ret = -ENOMEM;
		goto out;
	}

	/*bootrom dnld dtop need bulk msg, other need to send int msg */
	if (WQ_USB_EP_DTOP_DL == ep_id) {
		usb_fill_bulk_urb(urb, wq_usb->usbdev,
				  usb_sndbulkpipe(wq_usb->usbdev, ep_id), data,
				  size, wq_usb_bmi_snd_msg_cb, wq_usb);
	} else {
		if (wifi_bmi_ep == WQ_USB3_EP_WIFI_BMI)
			usb_fill_bulk_urb(urb, wq_usb->usbdev,
					  usb_sndbulkpipe(wq_usb->usbdev, wifi_bmi_ep), data,
					  size, wq_usb_bmi_snd_msg_cb, wq_usb);
		else
			usb_fill_int_urb(urb, wq_usb->usbdev,
					  usb_sndintpipe(wq_usb->usbdev, wifi_bmi_ep), data,
					  size, wq_usb_bmi_snd_msg_cb, wq_usb, 1);
	}

	/* auto append zlp when size is multiple of bulk mps */
	urb->transfer_flags |= URB_ZERO_PACKET;

	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: usb_submit_urb FAILED\n",
		       __func__);
		goto free_urb;
	}

	timeout_left =
		wait_event_timeout(wq_usb->bmi.wait_q, wq_usb->bmi.woken, tout);
	if (!timeout_left || timeout_left < 0) {
		WQ_DBG(DM_TRBUS, DL_ERR, "bmi usb bulk out timeout\n");
		ret = -ETIMEDOUT;
		goto free_urb;
	}

free_urb:
	if (urb)
		usb_free_urb(urb);
out:
	return ret;
}

int wq_usb_bmi_exchange(struct wq_core *core, void *req, u32 req_len, void *rsp,
			u32 rsp_len, int timeout)
{
	struct wq_usb *wq_usb = container_of(core, struct wq_usb, core);
	u8 *snd_buf = NULL;
	u8 *rcv_buf = NULL;
	int ret = 0;
	int len_xfer = 0;
	int actual_length;
	u8 wifi_bmi_ep = wq_usb_bmi_get_wifi_ep(wq_usb);

	if (!req || !rsp) {
		return -EINVAL;
	}

	snd_buf = kzalloc(rsp_len, GFP_KERNEL);
	if (!snd_buf) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: allocate download buf failed!\n",
		       __func__);
		WQ_ERROR(FW_DOWNLOAD_MEM_ALLOC_FAIL);
		ret = -ENOMEM;
		goto done;
	}

	rcv_buf = kzalloc(rsp_len, GFP_KERNEL);
	if (!rcv_buf) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "%s: allocate download rcv buf failed!\n", __func__);
		WQ_ERROR(FW_DOWNLOAD_MEM_ALLOC_FAIL);
		ret = -ENOMEM;
		goto done;
	}

	len_xfer = rsp_len; /* max length */
	if (req_len < len_xfer)
		len_xfer = ALIGN(req_len,
				 4); /* 4 byte alignment for USB DMA engine */
	memcpy(snd_buf, req, len_xfer);

	ret = wq_usb_bmi_snd_msg(wq_usb, wifi_bmi_ep, snd_buf, len_xfer,
				 timeout);
	if (ret != 0) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s send msg error! ret: %d \n",
		       __func__, ret);
		goto done;
	}

	if (wifi_bmi_ep == WQ_USB3_EP_WIFI_BMI)
		ret = usb_bulk_msg(wq_usb->usbdev,
				usb_rcvbulkpipe(wq_usb->usbdev, wifi_bmi_ep),
				rcv_buf, rsp_len, &actual_length, timeout);
	else
		ret = usb_interrupt_msg(wq_usb->usbdev,
				usb_rcvintpipe(wq_usb->usbdev, wifi_bmi_ep),
				rcv_buf, rsp_len, &actual_length, timeout);

	if (ret < 0) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "%s recv msg from device failed, ret:%d\n", __func__,
		       ret);
	} else {
		ret = actual_length;
		WQ_DBG(DM_TRBUS, DL_VRB,
		       "%s receive success len: %d data: %d %d %d %d %d\n",
		       __func__, actual_length, rcv_buf[0], rcv_buf[1],
		       rcv_buf[2], rcv_buf[3], rcv_buf[4]);

		if (rsp_len > actual_length)
			rsp_len = actual_length;
		memcpy(rsp, rcv_buf, rsp_len);
	}

done:
	if (snd_buf)
		kfree(snd_buf);

	if (rcv_buf)
		kfree(rcv_buf);

	return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
static int usb_control_msg_recv(struct usb_device *dev, __u8 endpoint,
				__u8 request, __u8 requesttype, __u16 value,
				__u16 index, void *driver_data, __u16 size,
				int timeout, gfp_t memflags)
{
	unsigned int pipe = usb_rcvctrlpipe(dev, endpoint);
	int ret;
	u8 *data;

	if (!size || !driver_data /* || usb_pipe_type_check(dev, pipe) */)
		return -EINVAL;

	data = kmalloc(size, memflags);
	if (!data)
		return -ENOMEM;

	ret = usb_control_msg(dev, pipe, request, requesttype, value, index,
			      data, size, timeout);

	if (ret < 0)
		goto exit;

	if (ret == size) {
		memcpy(driver_data, data, size);
		ret = 0;
	} else {
		ret = -EINVAL;
	}

exit:
	kfree(data);
	return ret;
}
#endif

int wq_usb_bmi_xfer(struct wq_core *core, enum wq_bmi_xfer_type type,
		    const u8 *data, int len, int timeout)
{
	struct wq_usb *wq_usb = container_of(core, struct wq_usb, core);
	u8 *buf = NULL;
	const u8 *p = NULL;
	u16 checksum;
	u32 ep_id = 0;
	int ret = 0;
	int i, id;
	int len_xfer = 0;
	struct wq_fw_dl_tag *fwdl_tag = NULL;

	if (type == WQ_FW_DTOP_DL) {
		ep_id = WQ_USB_EP_DTOP_DL;
	} else {
		return -EINVAL;
	}

	buf = kzalloc(WQ_USB_MTU_FW_DL, GFP_KERNEL);
	if (!buf) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: allocate download buf failed!\n",
		       __func__);
		WQ_ERROR(FW_DOWNLOAD_MEM_ALLOC_FAIL);
		ret = -ENOMEM;
		goto done;
	}

	id = 0;
	p = data;
	while (len > 0) {
		//raw data preparation
		len_xfer =
			WQ_USB_MTU_FW_DL - sizeof(*fwdl_tag); /* max length */
		if (len < len_xfer)
			len_xfer = ALIGN(
				len,
				4); /* 4 byte alignment for USB DMA engine */
		// copy and checksum
		checksum = 0;
		for (i = 0; i < len_xfer; i++) {
			buf[i] = p[i];
			checksum += p[i];
		}

		//update fw download tag
		fwdl_tag = (struct wq_fw_dl_tag *)(buf + len_xfer);
		fwdl_tag->id = id;
		fwdl_tag->checksum = checksum;

		ret = wq_usb_bmi_snd_msg(wq_usb, ep_id, buf,
					 (len_xfer + sizeof(*fwdl_tag)),
					 timeout);
		if (ret != 0) {
			goto done;
		}

		//update data
		len -= len_xfer;
		p += len_xfer;
		id++;
	}

done:
	if (buf)
		kfree(buf);

	return ret;
}

static int wq_usb_control_msg(struct wq_core *core, u8 cmd, const void *param,
			      u16 p_size, void *resp, u16 r_size, int timeout)
{
	struct wq_usb *wq_usb = container_of(core, struct wq_usb, core);
	u16 value = 0;
	u16 index = 0;

	/* param ==> value/index */
	switch (p_size) {
	case 0:
		break;
	case sizeof(u16):
		value = *(u16 *)param;
		break;
	case sizeof(u32):
		index = *(u32 *)param;
		value = (*(u32 *)param) >> 16;
		break;
	default:
		WARN(1, "%s: invalid param size %d (cmd %d)\n", __func__,
		     p_size, cmd);
		break;
	}

	if (!resp || !r_size) {
		WARN_ON(resp || r_size);
		return usb_control_msg(
			wq_usb->usbdev, usb_sndctrlpipe(wq_usb->usbdev, 0), cmd,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, value,
			index, NULL, 0, timeout);
	}
	return usb_control_msg_recv(
		wq_usb->usbdev, 0, cmd,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE, value, index,
		resp, r_size, timeout, GFP_KERNEL);
}

static int usb_dnld_dtop_fw_info(struct wq_core *core)
{
	u8 dl_reply;
	int ret = 0;

	//set download address
	ret = wq_usb_control_msg(core, WQ_VREQ_ID_FW_DTOP_DL_ADDR,
				 &core->wq_dnld->dl_addr, sizeof(u32), 0, 0,
				 FW_DL_TIMEOUT);
	if (ret < 0) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "%s: set download address failed (%d)!\n", __func__,
		       ret);
		goto done;
	} else {
		WQ_DBG(DM_GENERIC, DL_VRB,
		       "%s: set download address(0x%x) complete\n", __func__,
		       core->wq_dnld->dl_addr);
	}

	//set start pc
	ret = wq_usb_control_msg(core, WQ_VREQ_ID_FW_DTOP_STARTPC,
				 &core->wq_dnld->start_pc, sizeof(u32), 0, 0,
				 FW_DL_TIMEOUT);
	if (ret < 0) {
		WQ_DBG(DM_GENERIC, DL_ERR, "%s: set start pc failed (%d)!\n",
		       __func__, ret);
		goto done;
	} else {
		WQ_DBG(DM_GENERIC, DL_VRB, "%s: set start pc(0x%x) complete\n",
		       __func__, core->wq_dnld->start_pc);
	}

	//set fw len
	ret = wq_usb_control_msg(core, WQ_VREQ_ID_FW_DTOP_DL,
				 &core->wq_dnld->fw_len, sizeof(u32), &dl_reply,
				 sizeof(dl_reply), FW_DL_TIMEOUT);
	if (ret < 0 || dl_reply != WQ_FW_DL_READY) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "%s: start command failed (ret=%d) (dl_reply=%d)\n",
		       __func__, ret, dl_reply);
		goto done;
	} else {
		WQ_DBG(DM_GENERIC, DL_VRB, "%s: start command completed\n",
		       __func__);
	}

done:
	if (ret >= 0)
		ret = 0;
	else
		ret = -ENXIO;

	return ret;
}

int wq_usb_bmi_cmd(struct wq_core *core, u8 cmd, const void *param, u16 p_size,
		   void *resp, u16 r_size, int timeout)
{
	u32 magic_id = BMI_MSG_MAGIC;

	switch (cmd) {
	case WQ_BMI_CMD_SET_FW_INFO:
		return usb_dnld_dtop_fw_info(core);

	case WQ_BMI_CMD_GET_ROM_VER:
		return wq_usb_control_msg(core, WQ_VREQ_ID_GET_ROM_VER, param,
					  p_size, resp, r_size, timeout);

	case WQ_BMI_CMD_VERIFY_FW:
		return wq_usb_control_msg(core, WQ_VREQ_ID_FW_DTOP_DL_COMP,
					  param, p_size, resp, r_size, timeout);

	case WQ_BMI_CMD_GET_SYS_STATE:
		return wq_usb_control_msg(core, WQ_VREQ_ID_GET_RUNSYS, param,
					  p_size, resp, r_size, timeout);

	case WQ_BMI_CMD_UNLOAD_DTOP:
		return wq_usb_control_msg(core, WQ_VREQ_ID_SET_SOC_RESET,
					  &magic_id, sizeof(magic_id), resp,
					  r_size, timeout);

	default:
		WQ_DBG(DM_TRBUS, DL_ERR, "unknown cmd: %x!\n", cmd);
		return -EINVAL; /* not supported */
	}
}