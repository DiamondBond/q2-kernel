/** @file woal_dnld.c
  *
  * @brief This file contains the download functions for wifi driver.
  *
  *  Copyright (C) 2022 - 2023, WuQi Technologies. ALL RIGHTS RESERVED.
  *
  *  This Information is proprietary to WuQi Technologies and MAY NOT
  *  be copied by any method or incorporated into another program without
  *  the express written consent of WuQi. This Information or any portion
  *  thereof remains the property of WuQi. The Information contained herein
  *  is believed to be accurate and WuQi assumes no responsibility or
  *  liability for its use in any way and conveys no license or title under
  *  any patent or copyright and makes no representation or warranty that this
  *  Information is free from patent or copyright infringement.
  *
  */

#define WQ_LOG_DM DM_TRBUS

#include "pcie.h"

#include "host_reg_base.h"
#include "wifi_ahb_reg.h"

#include "wq_fw.h"
#include "wq_log.h"
#include "bmi_cmd.h"

/* ahb boot state */
#define FW_STATE_REG_CORE0 BIT(0)
#define FW_STATE_REG_CORE1 BIT(1)
#define FW_STATE_REG_CORE3 BIT(2)
#define FW_STATE_REG_BOOT_CE BIT(3)

enum wq_target_sys {
	DNLD_TARGET_SYS_WIFI = 0x00,
	DNLD_TARGET_SYS_BT = 0x01,
	DNLD_TARGET_SYS_DTOP = 0x10
};

#define DNLD_FW_MAGIC "wqfw"

struct wq_fw_info_tag {
	u8 magic[4];
	u32 len;
	u32 addr;
	u32 start_pc;
	u16 target_sys;
	u16 crc16;
} __attribute__((__packed__));

#define DNLD_CE_SRC_DATA_MAX (1024 * 1024 - 1)
#define DNLD_CE_SRC_RING_SIZE 4
#define DNLD_CE_SRC_RING_DEPTH 4
#define DNLD_CE_DST_RING_DEPTH 8

static const wq_ce_attr_t dnld_attr_table[] = {
	[WQ_PCIE_CE_CH_BMI_TX] = {
		.src_sz_max = DNLD_CE_SRC_DATA_MAX,
		.src_depth = DNLD_CE_SRC_RING_DEPTH,
		.dst_depth = 0,
	},
	[WQ_PCIE_CE_CH_BMI_RX] = {
		.src_sz_max = DNLD_CE_SRC_RING_SIZE,
		.src_depth = 0,
		.dst_depth = DNLD_CE_DST_RING_DEPTH,
	},
};

/*
 * This mysterious table is just the CRC of each possible byte. It can be
 * computed using the standard bit-at-a-time methods. The polynomial can
 * be seen in entry 128, 0x8408. This corresponds to x^0 + x^5 + x^12.
 * Add the implicit x^16, and you have the standard CRC-CCITT.
 */
static u16 const crc_ccitt_table[256] = {
	0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf, 0x8c48,
	0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7, 0x1081, 0x0108,
	0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e, 0x9cc9, 0x8d40, 0xbfdb,
	0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876, 0x2102, 0x308b, 0x0210, 0x1399,
	0x6726, 0x76af, 0x4434, 0x55bd, 0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e,
	0xfae7, 0xc87c, 0xd9f5, 0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e,
	0x54b5, 0x453c, 0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd,
	0xc974, 0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
	0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3, 0x5285,
	0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a, 0xdecd, 0xcf44,
	0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72, 0x6306, 0x728f, 0x4014,
	0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9, 0xef4e, 0xfec7, 0xcc5c, 0xddd5,
	0xa96a, 0xb8e3, 0x8a78, 0x9bf1, 0x7387, 0x620e, 0x5095, 0x411c, 0x35a3,
	0x242a, 0x16b1, 0x0738, 0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862,
	0x9af9, 0x8b70, 0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e,
	0xf0b7, 0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
	0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036, 0x18c1,
	0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e, 0xa50a, 0xb483,
	0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5, 0x2942, 0x38cb, 0x0a50,
	0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd, 0xb58b, 0xa402, 0x9699, 0x8710,
	0xf3af, 0xe226, 0xd0bd, 0xc134, 0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7,
	0x6e6e, 0x5cf5, 0x4d7c, 0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1,
	0xa33a, 0xb2b3, 0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72,
	0x3efb, 0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
	0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a, 0xe70e,
	0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1, 0x6b46, 0x7acf,
	0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9, 0xf78f, 0xe606, 0xd49d,
	0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330, 0x7bc7, 0x6a4e, 0x58d5, 0x495c,
	0x3de3, 0x2c6a, 0x1ef1, 0x0f78
};

static inline u16 crc_ccitt_byte(u16 crc, const u8 c)
{
	return (crc >> 8) ^ crc_ccitt_table[(crc ^ c) & 0xff];
}

static u16 wq_dnld_crc_ccitt(u16 crc, u8 const *buffer, size_t len)
{
	while (len--)
		crc = crc_ccitt_byte(crc, *buffer++);

	return crc;
}

static inline u32 wq_dnld_get_boot_state(struct wq_pcie *wq_pcie)
{
	return wq_pcie_read32(wq_pcie, HOST_W_AHB_REG_BASEADDR +
					       CFG_PCIE_BOOT_FW_STATE_ADDR);
}

static inline u32 wq_dnld_get_rom_version(struct wq_pcie *wq_pcie)
{
	return wq_pcie_read32(wq_pcie, HOST_W_AHB_REG_BASEADDR +
					       CFG_WIFI_AHB_BAK_REG0_ADDR);
}

static int woal_dnld_ce_fw_send(struct wq_pcie *wq_pcie)
{
	int ret;
	u32 boot_state = wq_dnld_get_boot_state(wq_pcie);

	if ((boot_state & FW_STATE_REG_BOOT_CE) == 0) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "not ce dnld, do nothing state:0x%x\n", boot_state);
		return 0;
	}

	if (!wq_pcie->fw_buf) {
		WQ_DBG(DM_GENERIC, DL_ERR, "fw buf is NULL\n");
		return -EINVAL;
	}

	ret = wq_map_memory(wq_pcie, wq_pcie->fw_buf, &wq_pcie->fw_buf_pa,
			    wq_pcie->fw_len, DMA_TO_DEVICE);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "%s: memory map failed(0x%p) to buf dma low addr(0x%x)\n",
		       __func__, wq_pcie->fw_buf, (u32)wq_pcie->fw_buf_pa);
		return ret;
	}
	wq_sync_memory_for_device(wq_pcie, wq_pcie->fw_buf_pa, wq_pcie->fw_len,
				  DMA_TO_DEVICE);
	ret = wq_ce_send(wq_pcie, 0, wq_pcie->fw_buf, wq_pcie->fw_buf_pa,
			 wq_pcie->fw_len, 0);
	BUG_ON(ret);

	return ret;
}

int wq_dnld_msg_handle(struct wq_pcie *wq_pcie, u32 msg_type)
{
	switch (msg_type) {
	case PCIE_MSG_E2R_BIN_INFO_INVALID:
		WQ_DBG(DM_TRBUS, DL_INF, "%s: bin info invalid\n", __func__);
		break;
	case PCIE_MSG_E2R_BIN_INFO_OK:
		WQ_DBG(DM_TRBUS, DL_INF, "%s: bin info ok\n", __func__);
		mdelay(100);
		woal_dnld_ce_fw_send(wq_pcie);
		break;
	case PCIE_MSG_E2R_BIN_INFO_ERR:
		WQ_DBG(DM_TRBUS, DL_INF, "%s: bin info error\n", __func__);
		break;
	case PCIE_MSG_E2R_BIN_INFO_CRC_ERR:
		WQ_DBG(DM_TRBUS, DL_INF, "%s: bin crc error\n", __func__);
		break;
	case PCIE_MSG_E2R_BIN_WILL_JUMP:
		WQ_DBG(DM_TRBUS, DL_INF, "%s: dnld dtop ok, dtop will jump\n",
		       __func__);
		break;
	case PCIE_MSG_E2R_ROM_VER_IS_RDY:
		WQ_DBG(DM_TRBUS, DL_INF, "%s: bootrom version ready\n",
		       __func__);
		complete(&wq_pcie->bmi_recv_done);
		break;
	default:
		WQ_DBG(DM_TRBUS, DL_INF, "%s: unknown msg type=0x%x\n",
		       __func__, msg_type);
		break;
	}

	return 0;
}

static int wq_dnld_info_forge(struct wq_pcie *wq_pcie, u8 *buf, u32 addr,
			      enum wq_bmi_xfer_type type)
{
	int ret = 0;
	struct wq_fw_info_tag *fw_info;
	struct wq_core *core = &wq_pcie->core;

	if (!core || !buf)
		return -EINVAL;

	fw_info = (struct wq_fw_info_tag *)buf;
	memcpy(fw_info->magic, DNLD_FW_MAGIC, 4);
	fw_info->len = core->wq_dnld->fw_len;
	fw_info->addr = addr;
	fw_info->start_pc = core->wq_dnld->start_pc;
	if (type == WQ_FW_DTOP_DL) {
		fw_info->target_sys = DNLD_TARGET_SYS_DTOP;
	} else {
		return -EINVAL;
	}

	fw_info->crc16 =
		wq_dnld_crc_ccitt(0, core->wq_dnld->fw, core->wq_dnld->fw_len);
	WQ_DBG(DM_GENERIC, DL_INF,
	       "%s: fw magic=%4.4s, len=0x%x, addr=0x%x, pc=0x%x, sys=0x%x, crc16=0x%x\n",
	       __func__, fw_info->magic, fw_info->len, fw_info->addr,
	       fw_info->start_pc, fw_info->target_sys, fw_info->crc16);

	return ret;
}

static void woal_dnld_ce_send_cb(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
	u8 *buf = NULL;
	dma_addr_t phys_addr = 0;
	u32 nbytes = 0;

	while (wq_ce_send_completed_next(wq_pcie, chn, (void **)&buf,
					 &phys_addr, &nbytes, NULL) == 0) {
		wq_unmap_memory(wq_pcie, &phys_addr, nbytes, DMA_TO_DEVICE);
		if (buf)
			kfree(buf);
	}
}

static void woal_dnld_ce_recv_cb(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
	u32 *buf = NULL;
	dma_addr_t phys_addr = 0;
	u32 nbytes = 0, msg_type = 0;

	while (wq_ce_recv_completed_next(wq_pcie, chn, (void **)&buf,
					 &phys_addr, &nbytes, NULL) == 0) {
		wq_sync_memory_for_device(wq_pcie, phys_addr, nbytes,
					  DMA_FROM_DEVICE);
		wq_unmap_memory(wq_pcie, &phys_addr, nbytes, DMA_FROM_DEVICE);
	}

	msg_type = *buf;
	wq_dnld_msg_handle(wq_pcie, msg_type);
	if (buf)
		kfree(buf);
}

static int woal_dnld_ce_init(struct wq_pcie *wq_pcie)
{
	int ret;

	ret = wq_ce_chn_init(wq_pcie, WQ_PCIE_CE_CH_BMI_TX,
			     &(dnld_attr_table[WQ_PCIE_CE_CH_BMI_TX]));
	BUG_ON(ret);

	wq_ce_int_cb_set(wq_pcie, WQ_PCIE_CE_CH_BMI_TX, WQ_CE_CHN_SRC,
			 CE_SRC_INT_CURR_DESC, woal_dnld_ce_send_cb);
	wq_ce_int_ena_set(wq_pcie, WQ_PCIE_CE_CH_BMI_TX, WQ_CE_CHN_SRC,
			  CE_SRC_INT_CURR_DESC, true);
	wq_ce_irq_unmask(wq_pcie, WQ_PCIE_CE_CH_BMI_TX, WQ_CE_CHN_SRC);

	ret = wq_ce_chn_init(wq_pcie, WQ_PCIE_CE_CH_BMI_RX,
			     &(dnld_attr_table[WQ_PCIE_CE_CH_BMI_RX]));
	BUG_ON(ret);

	wq_ce_int_cb_set(wq_pcie, WQ_PCIE_CE_CH_BMI_RX, WQ_CE_CHN_DST,
			 CE_DST_INT_CURR_DESC, woal_dnld_ce_recv_cb);
	wq_ce_int_ena_set(wq_pcie, WQ_PCIE_CE_CH_BMI_RX, WQ_CE_CHN_DST,
			  CE_DST_INT_CURR_DESC, true);
	wq_ce_irq_unmask(wq_pcie, WQ_PCIE_CE_CH_BMI_RX, WQ_CE_CHN_DST);

	return ret;
}

__maybe_unused static int wq_dnld_use_ce(struct wq_pcie *wq_pcie,
					 enum wq_bmi_xfer_type type)
{
	int ret;
	u32 len = sizeof(struct wq_fw_info_tag);
	u8 i = 0, *buf;
	dma_addr_t buf_pa;

	/* FIXME: memory leakage here */
	buf = kzalloc(len, GFP_KERNEL);
	if (!buf) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: alloc failed\n", __func__);
		return -ENOMEM;
	}

	wq_pcie->fw_buf = kzalloc(wq_pcie->fw_len, GFP_KERNEL);
	if (!wq_pcie->fw_buf) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: fw buf alloc failed\n", __func__);
		return -ENOMEM;
	}
	memcpy(wq_pcie->fw_buf, wq_pcie->fw, wq_pcie->fw_len);

	ret = wq_dnld_info_forge(wq_pcie, buf, 0, type);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: dnld info forge failed\n",
		       __func__);
		kfree(buf);
		kfree(wq_pcie->fw_buf);
		return ret;
	}

	ret = woal_dnld_ce_init(wq_pcie);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: dnld ce init failed\n", __func__);
		kfree(buf);
		kfree(wq_pcie->fw_buf);
		return ret;
	}

	ret = wq_map_memory(wq_pcie, buf, &buf_pa, len, DMA_TO_DEVICE);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "%s: pci memory map failed(0x%p) to buf dma addr(0x%x)\n",
		       __func__, buf, (u32)buf_pa);
		kfree(buf);
		kfree(wq_pcie->fw_buf);
		return ret;
	}

	wq_sync_memory_for_device(wq_pcie, buf_pa, len, DMA_TO_DEVICE);

	ret = wq_ce_send(wq_pcie, WQ_PCIE_CE_CH_BMI_TX, buf, buf_pa, len, 0);
	BUG_ON(ret);

	for (i = 0; i < DNLD_CE_DST_RING_DEPTH - 1; i++) {
		buf = kzalloc(DNLD_CE_SRC_RING_SIZE, GFP_KERNEL);
		ret = wq_map_memory(wq_pcie, buf, &buf_pa,
				    DNLD_CE_SRC_RING_SIZE, DMA_FROM_DEVICE);
		BUG_ON(ret);

		ret = wq_ce_recv(wq_pcie, WQ_PCIE_CE_CH_BMI_RX, buf, buf_pa,
				 DNLD_CE_SRC_RING_SIZE, CE_DESC_FLAG_INT_EB);
		BUG_ON(ret);
	}

	return ret;
}

static int wq_dnld_use_cpu_copy(struct wq_pcie *wq_pcie, const char *data,
				int size, enum wq_bmi_xfer_type type)
{
	int ret;
	u32 len = sizeof(struct wq_fw_info_tag) + size;

	ret = wq_map_consistent(wq_pcie, (u8 **)&wq_pcie->fw_buf,
				&wq_pcie->fw_buf_pa, len);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "%s: wq_map_consistent failed (len = %d)\n", __func__,
		       len);
		return -ENOMEM;
	}

	ret = wq_dnld_info_forge(
		wq_pcie, wq_pcie->fw_buf,
		wq_pcie->fw_buf_pa + sizeof(struct wq_fw_info_tag), type);
	if (ret != 0) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: dnld info forge failed\n",
		       __func__);
		wq_unmap_consistent(wq_pcie, (u8 **)&wq_pcie->fw_buf,
				    &wq_pcie->fw_buf_pa, len);
		return -EINVAL;
	}
	memcpy(wq_pcie->fw_buf + sizeof(struct wq_fw_info_tag), data, size);

	ret = wq_pcie_notify_fw_dnld(wq_pcie, wq_pcie->fw_buf_pa, len);
	if (ret != 0) {
		WQ_DBG(DM_GENERIC, DL_ERR, "%s: notify fw dnld failed\n",
		       __func__);
		wq_unmap_consistent(wq_pcie, (u8 **)&wq_pcie->fw_buf,
				    &wq_pcie->fw_buf_pa, len);
		return -EIO;
	}

	return 0;
}

static int wq_pcie_fw_dpc(struct wq_pcie *wq_pcie, const char *data, int len,
			  enum wq_bmi_xfer_type type)
{
	int ret;

#define MSLEEP_TIME 100
	u32 i = 0, wait_secs = 1 * (1000 / MSLEEP_TIME);

	if (type == WQ_FW_DTOP_DL) {
		do {
			u32 boot_state = wq_dnld_get_boot_state(wq_pcie);
			if (!(boot_state & FW_STATE_REG_CORE0)) {
				WQ_DBG(DM_GENERIC, DL_WRN,
				       "BootROM is ready, state: 0x%x\n",
				       boot_state);
				break;
			} else {
				WQ_DBG(DM_GENERIC, DL_ERR,
				       "%d: boot state erro: 0x%x\n", i,
				       boot_state);
			}
			msleep(MSLEEP_TIME);
			i++;
		} while (i < wait_secs);

		if (i >= wait_secs) {
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "wait BootROM ready timeout\n");
			return -EPERM;
		}
	}
#undef MSLEEP_TIME

	ret = wq_dnld_use_cpu_copy(wq_pcie, data, len, type);

	return ret;
}

int wq_pcie_bmi_xfer(struct wq_core *core, enum wq_bmi_xfer_type type,
		     const u8 *data, int len, int timeout)
{
	int ret;
	struct wq_pcie *wq_pcie = container_of(core, struct wq_pcie, core);

	ret = wq_pcie_fw_dpc(wq_pcie, data, len, type);

	return ret;
}

int wq_pcie_bmi_cmd(struct wq_core *core, u8 cmd, const void *param, u16 p_size,
		    void *resp, u16 r_size, int timeout)
{
	struct wq_pcie *wq_pcie = container_of(core, struct wq_pcie, core);
	struct wq_dev_rom_ver *rom_ver;
	u32 reg_value = 0;
	u8 dstate = 0;
	int ret = 0;

	switch (cmd) {
	case WQ_BMI_CMD_GET_ROM_VER:
		if (!resp)
			return -EINVAL;

		rom_ver = (struct wq_dev_rom_ver *)resp;
		if (!wq_pcie->wakeup_target_timeout) {
			/*dtop active, get rom version from pcie_boot_state*/
			u8 rom_ver_value = 0;
			reg_value = wq_dnld_get_boot_state(wq_pcie);
			rom_ver_value = (reg_value & 0xFF000000) >> 24;
			WQ_DBG(DM_GENERIC, DL_WRN,
			       "boot state rom version: 0x%x\n", rom_ver_value);
			if (rom_ver_value) {
				rom_ver->major = (rom_ver_value & 0xF0) >> 4;
				rom_ver->minor = rom_ver_value & 0x0F;
			} else {
				rom_ver->major = 1;
				rom_ver->minor = 0;
			}
		} else {
			if (wq_pcie_notify_fw_use_msg(
				    wq_pcie, PCIE_MSG_R2E_GET_ROM_VER) != 0) {
				WQ_DBG(DM_GENERIC, DL_ERR,
				       "notify fw get rom_ver failed! \n");
				return -EIO;
			}
			if (!wait_for_completion_timeout(
				    &wq_pcie->bmi_recv_done, HZ * timeout)) {
				return -ETIMEDOUT;
			} else {
				reg_value = wq_dnld_get_rom_version(wq_pcie);
				WQ_DBG(DM_GENERIC, DL_WRN,
				       "rom version reg:0x%x\n", reg_value);
				if (reg_value) {
					rom_ver->major =
						(reg_value >> 16) & 0xFF;
					rom_ver->minor = reg_value & 0xFF;
				} else {
					rom_ver->major = 1;
					rom_ver->minor = 0;
				}
			}
		}
		rom_ver->build_hr = 0xff;
		rom_ver->build_min = 0xff;
		break;
	case WQ_BMI_CMD_GET_SYS_STATE:
		reg_value = wq_dnld_get_boot_state(wq_pcie);
		WQ_DBG(DM_GENERIC, DL_WRN, "boot state reg:0x%x\n",
		       reg_value & 0xFFFFFF);
		if (reg_value & FW_STATE_REG_CORE0) {
			dstate = BIT(WQ_FW_DTOP);
			memcpy(resp, &dstate, 1);
		} else {
			dstate = BIT(WQ_FW_BOOTROM);
			memcpy(resp, &dstate, 1);
		}
		/* if need to check wifi state, please reset wifi_state in dtop_fw boot_state when wifi remove*/
		break;
	case WQ_BMI_CMD_VERIFY_FW:
		wq_unmap_consistent(
			wq_pcie, (u8 **)&wq_pcie->fw_buf, &wq_pcie->fw_buf_pa,
			wq_pcie->fw_len + sizeof(struct wq_fw_info_tag));
		break;
	case WQ_BMI_CMD_SET_FW_INFO:
	case WQ_BMI_CMD_UNLOAD_DTOP:
		ret = 0;
		break;

	default:
		WQ_DBG(DM_TRBUS, DL_ERR, "unknown cmd: %x!\n", cmd);
		ret = -EINVAL;
		break;
	}

	return ret;
}
