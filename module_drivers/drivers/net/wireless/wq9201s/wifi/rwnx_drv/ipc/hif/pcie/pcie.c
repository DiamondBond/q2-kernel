/** @file woal_pcie.c
 *
 *  @brief This file contains PCIE IF (interface) module
 *  related functions.
 *
 * Copyright (C) 2016-2023, WuQi Ltd.
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

#define WQ_LOG_DM DM_TRBUS

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/timex.h>
#include <linux/kthread.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>

#include "pcie.h"
#include "bmi_core.h"

#ifdef CONFIG_RK3588_ENABLE_WAKEUP_OOB
#include "plat_rk3588.h"
#endif

#include "host_reg_base.h"
#include "wifi_ahb_reg.h"
#include "mail_box_reg.h"
#include "intc_reg.h"
#include "pcie_ext_ctrl_reg.h"
#include "rwnx_defs.h"

#include "wq_log.h"
#include "fw_log.h"

/** define WUQI vendor id */
#define WUQI_PCIE_VENDOR_ID 0x1fdd
#define FPGA_PCIE_VENDOR_ID 0x16c3

/** define WUQI device id */
#define WUQI_PCIE_DEVICE_ID_QFN 0x0001 /* WQ_WPHY_PF_QFN_PCIE */
#define WUQI_PCIE_DEVICE_ID_BGA 0x0002 /* WQ_WPHY_PF_BGA_PCIE */
#define FPGA_PCIE_DEVICE_ID 0xabcd /* FPGA device id*/

#define BAR_NUM 0

#define WUQI_DRV_NAME "WuQi Wlan PCIe"

/* bar0 multiple atu region target address */
#define PCIE_BAR0_ATU_REGION_NUM 7
#define PCIE_REGION0_TARGET_ADDR 0x0FE00000
#define PCIE_REGION1_TARGET_ADDR 0x08000000
#define PCIE_REGION2_TARGET_ADDR 0x08002000
#define PCIE_REGION3_TARGET_ADDR 0x08008000
#define PCIE_REGION4_TARGET_ADDR 0x0810E000
#define PCIE_REGION5_TARGET_ADDR 0x08010000
#define PCIE_REGION6_TARGET_ADDR 0x08019000

/* bar region config */
#define PCIE_ATU_VIEWPORT 0x900
#define PCIE_ATU_REGION_INBOUND BIT(31)
#define PCIE_ATU_REGION_OUTBOUND 0
#define PCIE_ATU_CR1 0x904
#define PCIE_ATU_CR2 0x908
#define PCIE_ATU_ENABLE BIT(31)
#define PCIE_ATU_BAR_MODE_ENABLE BIT(30)
#define PCIE_ATU_FUNC_NUM_MATCH_EN BIT(19)
#define PCIE_ATU_LOWER_BASE 0x90C
#define PCIE_ATU_UPPER_BASE 0x910
#define PCIE_ATU_LIMIT 0x914
#define PCIE_ATU_LOWER_TARGET 0x918
#define PCIE_ATU_UPPER_TARGET 0x91C

#define MAILBOX_INT1_NUM 147
#define MAILBOX_TX_DONE_INT 0x02

#define MAILBOX1_BASEADDR (HOST_W_MAILBOX_BASEADDR + 0x20)

#define MAILBOX2_BASEADDR (HOST_W_MAILBOX_BASEADDR + 0x40)

#define WIFI_SCRATCH1_ADDR (HOST_W_AHB_REG_BASEADDR + 0x34)

#define MAILBOX_N_BASEADDR(x) (HOST_W_MAILBOX_BASEADDR + 0x20 * x)

typedef struct wq_pcie_region_addr {
	u32 base_addr;
	u32 limit_addr;
	u32 target_addr;
} wq_pcie_region_addr_t;

static wq_pcie_region_addr_t wq_pcie_bar0_region[PCIE_BAR0_ATU_REGION_NUM] = {
	{ HOST_W_IRAM_BASEADDR, HOST_W_IRAM_ENDADDR, PCIE_REGION0_TARGET_ADDR },
	{ HOST_W_AHB_REG_BASEADDR, HOST_W_AHB_REG_ENDADDR,
	  PCIE_REGION1_TARGET_ADDR },
	{ HOST_PCIE_INTC_REG_BASEADDR, HOST_PCIE_INTC_REG_ENDADDR,
	  PCIE_REGION2_TARGET_ADDR },
	{ HOST_W_CE_BASEADDR, HOST_W_CE_ENDADDR, PCIE_REGION3_TARGET_ADDR },
	{ HOST_PCIE_EXT_CTRL_BASEADDR, HOST_PCIE_EXT_CTRL_ENDADDR,
	  PCIE_REGION4_TARGET_ADDR },
	{ HOST_W_APB_REG_BASEADDR, HOST_W_APB_REG_ENDADDR,
	  PCIE_REGION5_TARGET_ADDR },
	{ HOST_W_MAILBOX_BASEADDR, HOST_W_MAILBOX_ENDADDR,
	  PCIE_REGION6_TARGET_ADDR },
};

#define WQ_PCIE_CHN_DUMP_MASK                                                  \
	((wq_conf.stats_dump_mask & WQ_STATS_CE_DUMP_BITS) >>                  \
	 (WQ_STATS_HIF_START_BIT - WQ_PCIE_CE_CH_WIFI_BASE))

#define PCIE_CE_DEPTH_MSG_TX 128
#define PCIE_CE_DEPTH_MSG_RX 2048

#define PCIE_CE_DEPTH_PKT_TX 4096
#define PCIE_CE_DEPTH_PKT_RX 8192

#define PCIE_CE_DATA_SIZE_MAX (1024 * 2)
#ifndef PHY_ADC_DUMP
#define PCIE_CE_EVT_SIZE_MAX (1024 * 12)
#else
#define PCIE_CE_EVT_SIZE_MAX (1024 * 64)
#endif

#define PCIE_CE_DEPTH_FW_LOG 256
#define PCIE_CE_FW_LOG_SIZE_MAX 2048

#define CE_RAW_PACKET_TX_TIMEOUT_NS 1000000UL /* 1ms */
#define CE_RAW_PACKET_RX_TIMEOUT_NS 1000000UL /* 1ms */

#define FLOW_CTRL_THRESHOLD_STOP 800
#define FLOW_CTRL_THRESHOLD_RESTART 600

#define WQ_PCIE_AUTUSUSPEND_DELAY_MS 500

static void wq_pcie_ce_rx_bmi(struct wq_pcie *wq_pcie, CE_CHN_UUID chn);
static void wq_pcie_ce_tx_wifi(struct wq_pcie *wq_pcie, CE_CHN_UUID chn);
static void wq_pcie_ce_rx_wifi(struct wq_pcie *wq_pcie, CE_CHN_UUID chn);
static void wq_pcie_ce_rx_fwlog(struct wq_pcie *wq_pcie, CE_CHN_UUID chn);
static void wq_pcie_prog_bar0_inbond_atu(struct pci_dev *pdev, int index,
					 u64 base);
static void wq_pcie_read_bar0_inbond_atu(struct pci_dev *pdev, int index);

static const struct wq_pcie_ce_attr pcie_attr_table[CE_CHN_MAX] = {
	[WQ_PCIE_CE_CH_BMI_TX] = {
		.flags = 0,
		.src_sz_max = BMI_MSG_LEN_MAX,
		.src_depth = 2,
		.dst_depth = 0,
		.high_watermark = 1,
		.low_watermark = 0,
		.intr_ena = 0,
		.buf_alloc = NULL,
		.buf_free = NULL,
		.desc_completed = NULL,
	},
	[WQ_PCIE_CE_CH_BMI_RX] = {
		.flags = 0,
		.src_sz_max = BMI_MSG_LEN_MAX,
		.src_depth = 0,
		.dst_depth = 2,
		.high_watermark = 1,
		.low_watermark = 0,
		.intr_ena = (1u << CE_DST_INT_CURR_DESC),
		.buf_alloc = NULL,
		.buf_free = NULL,
		.desc_completed = wq_pcie_ce_rx_bmi,
	},
#ifndef CONFIG_PCIE_UNIT_TEST
	[WQ_PCIE_CE_CH_CMD_TX] = {
		.flags = 0,
		.src_sz_max = PCIE_CE_DATA_SIZE_MAX,
		.src_depth = PCIE_CE_DEPTH_MSG_TX,
		.dst_depth = 0,
		.high_watermark = PCIE_CE_DEPTH_MSG_TX - 1,
		.low_watermark = 0,
		.intr_ena = (1u << CE_SRC_INT_CURR_DESC),
		.buf_alloc = NULL,
		.buf_free = wq_pcie_ce_skb_free,
		.desc_completed = wq_pcie_ce_tx_wifi,
	},
	[WQ_PCIE_CE_CH_EVT_RX] = {
		.flags = 0,
		.src_sz_max = PCIE_CE_EVT_SIZE_MAX,
		.src_depth = 0,
		.dst_depth = PCIE_CE_DEPTH_MSG_RX,
		.high_watermark = PCIE_CE_DEPTH_MSG_RX - 1,
		.low_watermark = 0,
		.intr_ena = (1u << CE_DST_INT_CURR_DESC),
		.buf_alloc = wq_pcie_ce_skb_alloc,
		.buf_free = wq_pcie_ce_skb_free,
		.desc_completed = wq_pcie_ce_rx_wifi,
	},
	[WQ_PCIE_CE_CH_PKT_TX] = {
		.flags = 0,
		.src_sz_max = PCIE_CE_DATA_SIZE_MAX,
		.src_depth = PCIE_CE_DEPTH_PKT_TX,
		.dst_depth = 0,
		.high_watermark = PCIE_CE_DEPTH_PKT_TX - 1,
		.low_watermark = 0,
		.intr_ena = (1u << CE_SRC_INT_CURR_DESC),
		.buf_alloc = NULL,
		.buf_free = wq_pcie_ce_skb_free,
		.desc_completed = wq_pcie_ce_tx_wifi,
	},
	[WQ_PCIE_CE_CH_RAW_TX] = {
		.flags = 0,
		.src_sz_max = PCIE_CE_DATA_SIZE_MAX,
		.src_depth = PCIE_CE_DEPTH_PKT_TX,
		.dst_depth = 0,
		.high_watermark = PCIE_CE_DEPTH_PKT_TX - 1,
		.low_watermark = 0,
		.intr_ena = (1u << CE_SRC_INT_CURR_DESC),
		.buf_alloc = NULL,
		.buf_free = wq_pcie_ce_skb_free,
		.desc_completed = wq_pcie_ce_tx_wifi,
	},
	[WQ_PCIE_CE_CH_RAW_RX] = {
		.flags = 0,
		.src_sz_max = PCIE_CE_DATA_SIZE_MAX,
		.src_depth = 0,
		.dst_depth = PCIE_CE_DEPTH_PKT_RX,
		.high_watermark = PCIE_CE_DEPTH_PKT_RX - 1,
		.low_watermark = 0,
		.intr_ena = (1u << CE_DST_INT_CURR_DESC),
		.buf_alloc = wq_pcie_ce_skb_alloc,
		.buf_free = wq_pcie_ce_skb_free,
		.desc_completed = wq_pcie_ce_rx_wifi,
	},
	[WQ_PCIE_CE_CH_FW_LOG] = {
		.flags = 0,
		.src_sz_max = PCIE_CE_FW_LOG_SIZE_MAX,
		.src_depth = 0,
		.dst_depth = PCIE_CE_DEPTH_FW_LOG,
		.high_watermark = PCIE_CE_DEPTH_FW_LOG - 1,
		.low_watermark = 0,
		.intr_ena = (1u << CE_DST_INT_CURR_DESC),
		.buf_alloc = wq_pcie_ce_skb_alloc,
		.buf_free = wq_pcie_ce_skb_free,
		.desc_completed = wq_pcie_ce_rx_fwlog,
	},
#endif
};

char *fw_dtop_pcie = NULL;
module_param(fw_dtop_pcie, charp, 0);
MODULE_PARM_DESC(fw_dtop_pcie, "dtop firmware name for pcie, default: null.");

char *fw_wifi_pcie = NULL;
module_param(fw_wifi_pcie, charp, 0);
MODULE_PARM_DESC(fw_wifi_pcie, "wifi firmware name for pcie, default: null.");

static int wq_pcie_autopm_get_async(struct wq_core *core);
static void wq_pcie_autopm_put_async(struct wq_core *core);
static bool wq_pcie_is_bus_active(struct wq_core *core);

static void wq_mailbox_irq_handle(struct wq_pcie *wq_pcie, u32 msg_type)
{
	switch (msg_type) {
#ifdef CONFIG_PCIE_UNIT_TEST
	case MAILBOX_TX_DONE_INT:
		wq_pcie->tx_cmd_done = true;
		break;
#ifdef RAE_TEST
#define RAE_PROG_DONE_INT (0x30)
	case RAE_PROG_DONE_INT:
		void woal_rae_remote_buf_prog_done(void);

		woal_rae_remote_buf_prog_done();
#endif
#endif
		break;
#define WQ_PCIE_SPEC_WIFI_TX_DONE 2
	case WQ_PCIE_SPEC_WIFI_TX_DONE:
		tasklet_schedule(&wq_pcie->txqring_tasklet);
		break;
	default:
		WQ_DBG(DM_TRBUS, DL_INF, "%s:msg type = 0x%x\n", __func__,
		       msg_type);
	}
	return;
}

/**
 *  @brief This function handles the interrupt.
 *
 *  @param irq	    The irq no. of PCIE device
 *  @param dev_id   A pointer to the pci_dev structure
 *
 *  @return         IRQ_HANDLED
 */
static irqreturn_t wq_pcie_interrupt(int irq, void *dev_id)
{
	struct pci_dev *pdev = dev_id;
	struct wq_pcie *wq_pcie;
	u32 reg_data = 0;
	CE_CHN_UUID chn;
	u8 intr_id;
	u8 intr_valid;
	int times = 0;

	if (!pdev) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: pdev is NULL\n", (u8 *)pdev);
		goto exit;
	}

	wq_pcie = (struct wq_pcie *)pci_get_drvdata(pdev);
	if (!wq_pcie) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: wq_pcie=%p\n", __func__, wq_pcie);
		goto exit;
	}

	while (++times < 128) {
		// WQ_DBG(DM_TRBUS, DL_INF, "*** IN PCIE IRQ ***\n");
		reg_data =
			wq_pcie_read32(wq_pcie, HOST_PCIE_INTC_REG_BASEADDR +
							CFG_INT_PRI_STS_ADDR);
		intr_valid = (reg_data & INT_VLD_MASK) >> INT_VLD_OFFSET;

		if (!intr_valid)
			break;

		intr_id = (reg_data & INT_ID_MASK) >> INT_ID_OFFSET;

		WQ_DBG(DM_TRBUS, DL_VRB, "%s: intr_id  = 0x%x\n", __func__,
		       intr_id);

		if (intr_id == MAILBOX_INT1_NUM) {
			wq_pcie_write32(
				wq_pcie,
				MAILBOX1_BASEADDR + CFG_MSG_INT_CLR_ADDR, 0x0F);

			reg_data = wq_pcie_read32(
				wq_pcie, MAILBOX1_BASEADDR + CFG_MSG_STS_ADDR);
			if ((reg_data & MAILBOX_FIFO_DATA_NUM_MASK) != 0x01) {
				//MAILBOX_FIFO_DATA_NUM = 1 means there is one msg need to read
				WQ_DBG(DM_TRBUS, DL_INF,
				       "Read msg sts reg  = 0x%08x\n",
				       reg_data);
			}

			//write CFG_MSG_RCTRL_ADDR as 0x01 before read CFG_MSG_RDATA_ADDR
			wq_pcie_write32(wq_pcie,
					MAILBOX1_BASEADDR + CFG_MSG_RCTRL_ADDR,
					0x01);
			reg_data = wq_pcie_read32(wq_pcie,
						  MAILBOX1_BASEADDR +
							  CFG_MSG_RDATA_ADDR);

			wq_mailbox_irq_handle(wq_pcie, reg_data);
			wq_dnld_msg_handle(wq_pcie, reg_data);
		} else if ((chn = wq_ce_get_chn_from_irq_vec(intr_id)) <
			   WQ_PCIE_CE_CH_LAST) {
			// CE interrupt wq_pcie
			wq_ce_isr(wq_pcie, chn);
		}
	}
	if (times > 64)
		WQ_DBG(DM_TRBUS, DL_WRN, "%s: times %d\n", __func__, times);

	/* ACK (clear pending) */
	switch (wq_pcie->intr_mode) {
	case PCIE_INTR_MODE_MSI:
		wq_pcie_write32(wq_pcie,
				HOST_PCIE_EXT_CTRL_BASEADDR +
					CFG_PCIE_EXT_MSI_3_ADDR,
				0x01);
		break;
	case PCIE_INTR_MODE_LEGACY:
		wq_pcie_write32(wq_pcie,
				HOST_PCIE_EXT_CTRL_BASEADDR +
					CFG_PCIE_EXT_LEGACY_INT_CTRL_ADDR,
				0x01);
		break;
	default:
		BUG();
		break;
	}

exit:
	return IRQ_HANDLED;
}

#if PCIE_NUM_MSIX_VECTORS
static irqreturn_t wq_pcie_interrupt_msix(int irq, void *dev_id)
{
	struct msix_context *ctx = dev_id;
	struct pci_dev *pdev = ctx->dev;

	BUG_ON(!ctx);
	/* FIXME */

	return IRQ_HANDLED;
}
#endif

/********************************************************
			Global Functions
********************************************************/

/**
 *  @brief Map a block of memory to device
 *
 *  @param wq_pcie       Pointer to the struct wq_pcie
 *  @param pbuf         Pointer to the buffer to be mapped
 *  @param pbuf_pa      Pointer to store the physical address of buffer
 *  @param size         Size of the buffer to be mapped
 *  @param flag         Flags for mapping IO
 *
 *  @return             WOAL_STATUS_SUCCESS or WOAL_STATUS_FAILURE
 */
int wq_map_memory(struct wq_pcie *wq_pcie, u8 *pbuf, dma_addr_t *pbuf_pa,
		  u32 size, u32 flag)
{
	dma_addr_t dma;

	/* Init memory to device */
	dma = dma_map_single(wq_pcie->core.dev, pbuf, size, flag);
	if (dma_mapping_error(wq_pcie->core.dev, dma)) {
		WQ_DBG(DM_TRBUS, DL_ERR, "Failed to pci_map_single\n");
		return -ENOMEM;
	}
	*pbuf_pa = dma;
	// WQ_DBG(DM_TRBUS, DL_INF,
	// 	       "%s: allocate map DMA memory(%d bytes)- vbase: %p, pbase: 0x%08x:0x%08x\n",
	// 	       __func__, (int)size,pbuf,(u32)(*pbuf_pa >> 32),(u32)(*pbuf_pa));
	return 0;
}

/**
 * @brief
 *
 * @param wq_pcie
 * @param pbuf_pa
 * @param size
 * @param flag
 * @return int
 */
void wq_sync_memory_for_device(struct wq_pcie *wq_pcie, dma_addr_t pbuf_pa,
			       u32 size, u32 flag)
{
	dma_sync_single_for_device(wq_pcie->core.dev, pbuf_pa, size, flag);
}

/**
 * @brief
 *
 * @param wq_pcie
 * @param pbuf_pa
 * @param size
 * @param flag
 * @return int
 */
void wq_sync_memory_for_cpu(struct wq_pcie *wq_pcie, dma_addr_t pbuf_pa,
			    u32 size, u32 flag)
{
	dma_sync_single_for_cpu(wq_pcie->core.dev, pbuf_pa, size, flag);
}

/**
 *  @brief unMap a block of memory to device
 *
 *  @param wq_pcie       Pointer to the struct wq_pcie
 *  @param pbuf         Pointer to the buffer to be mapped
 *  @param pbuf_pa      Pointer to store the physical address of buffer
 *  @param size         Size of the buffer to be mapped
 *  @param flag         Flags for mapping IO
 *
 *  @return             NA
 */
void wq_unmap_memory(struct wq_pcie *wq_pcie, dma_addr_t *buf_pa, u32 size,
		     u32 flag)
{
	if (!buf_pa)
		return;
	// WQ_DBG(DM_TRBUS, DL_INF,
	// 	       "%s: unmap DMA memory(%d bytes)- pbase: 0x%08x:0x%08x\n",
	// 	       __func__, (int)size, (u32)(*buf_pa >> 32),(u32)(*buf_pa));
	dma_unmap_single(wq_pcie->core.dev, *buf_pa, size, flag);
	*buf_pa = 0;
}

/**
 *  @brief Map a consistent block of memory to device
 *
 *  @param wq_pcie       Pointer to the struct wq_pcie
 *  @param pbuf         Pointer to the buffer to be mapped
 *  @param pbuf_pa      Pointer to store the physical address of buffer
 *  @param size         Size of the buffer to be mapped
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
int wq_map_consistent(struct wq_pcie *wq_pcie, u8 **ppbuf, dma_addr_t *pbuf_pa,
		      u32 size)
{
	*pbuf_pa = 0;
	//*ppbuf = (u8 *)pci_alloc_consistent(wq_pcie->core.dev, size,pbuf_pa);
	*ppbuf = dma_alloc_coherent(wq_pcie->core.dev, size, pbuf_pa,
				    GFP_KERNEL);
	if (*ppbuf == NULL) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "%s: allocate consistent DMA memory (%d bytes) failed!\n",
		       __func__, (int)size);
		return -1;
	}
#if 0
	WQ_DBG(DM_TRBUS, DL_INF,
		       "%s: allocate consistent DMA memory(%d bytes)- vbase: %p, pbase: 0x%llx\n",
		       __func__, (int)size,*ppbuf, (u64)(*pbuf_pa));
#endif
	return 0;
}

/**
 *  @brief unMap a consistent block of memory to device
 *
 *  @param wq_pcie       Pointer to the struct wq_pcie
 *  @param pbuf         Pointer to the buffer to be mapped
 *  @param pbuf_pa      Pointer to store the physical address of buffer
 *  @param size         Size of the buffer to be mapped
 *
 *  @return             NA
 */
void wq_unmap_consistent(struct wq_pcie *wq_pcie, u8 **pbuf, dma_addr_t *buf_pa,
			 u32 size)
{
	if (!buf_pa)
		return;
		//pci_free_consistent(wq_pcie->core.dev, size, pbuf, buf_pa);
#if 0
	WQ_DBG(DM_TRBUS, DL_INF,
		       "%s: unmap DMA consistent memory(%d bytes)- pbase: 0x%llxx\n",
		       __func__, (int)size, (u64)(*buf_pa));
#endif
	dma_free_coherent(wq_pcie->core.dev, size, *pbuf, *buf_pa);
	*pbuf = NULL;
	*buf_pa = 0;
}

int wq_pcie_notify_fw_dnld(struct wq_pcie *wq_pcie, dma_addr_t addr_pa, u32 len)
{
	/* Write the lower 32bits of the physical address to REG_CMD_ADDR_LO */
	wq_pcie_write32(wq_pcie,
			HOST_W_AHB_REG_BASEADDR + CFG_WIFI_AHB_BAK_REG0_ADDR,
			(u32)addr_pa);

	/* Write the command length to REG_CMD_SIZE */
	wq_pcie_write32(wq_pcie,
			HOST_W_AHB_REG_BASEADDR + CFG_WIFI_AHB_BAK_REG1_ADDR,
			len);

	return wq_pcie_notify_fw_use_msg(wq_pcie, PCIE_MSG_R2E_BIN_READY);
}

int wq_pcie_notify_fw_use_msg(struct wq_pcie *wq_pcie,
			      enum pcie_fw_dnld_msg msg)
{
	u32 reg_data;

	reg_data = wq_pcie_read32(wq_pcie,
				  HOST_W_MAILBOX_BASEADDR + CFG_MSG_STS_ADDR);
	if (!((reg_data >> MAILBOX_AUTO_FILL_ENA_OFFSET) & 0x01)) {
		wq_pcie_write32(wq_pcie,
				HOST_W_MAILBOX_BASEADDR + CFG_MSG_WDATA_ADDR,
				msg);
	} else {
		WQ_DBG(DM_TRBUS, DL_INF, "mailbox status=0x%x, bit12=%d\n",
		       reg_data,
		       (reg_data >> MAILBOX_AUTO_FILL_ENA_OFFSET) & 0x01);
	}
	return 0;
}

int wq_pcie_ce_skb_alloc(struct wq_pcie *wq_pcie, u32 nbytes,
			 enum dma_data_direction dir, void **context,
			 dma_addr_t *phys_addr)
{
	int ret;
	struct sk_buff *skb;
	dma_addr_t _phys_addr;

	skb = dev_alloc_skb(nbytes);
	BUG_ON(!skb);

	_phys_addr = dma_map_single(wq_pcie->core.dev, skb->data, nbytes, dir);
	ret = dma_mapping_error(wq_pcie->core.dev, _phys_addr);
	BUG_ON(ret);

	if (context)
		*context = skb;

	if (phys_addr)
		*phys_addr = _phys_addr;

	return 0;
}

void wq_pcie_ce_skb_free(struct wq_pcie *wq_pcie, dma_addr_t phys_addr,
			 u32 nbytes, enum dma_data_direction dir, void *context)
{
	struct sk_buff *skb = context;
	dma_unmap_single(wq_pcie->core.dev, phys_addr, nbytes, dir);
	dev_kfree_skb_any(skb);
}

/**
 *  @brief: show pcie state on host platform
 *  @param wq_pcie Pointer to the struct wq_pcie
 *  @return: status
 */
int wq_pcie_show_bus_info(struct wq_pcie *wq_pcie)
{
	u8 index;
	u32 bar0, bar1;
	u16 vid, did;
	u32 tmp;
	WQ_DBG(DM_TRBUS, DL_ERR,
	       " ======== WQ PCIe DEBUG INFO START ========\n");

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)
	/* show pcie link speed */
	pcie_print_link_status(wq_pcie->pdev);
#endif

	/* show pcie bar address */
	pci_read_config_dword(wq_pcie->pdev, PCI_BASE_ADDRESS_0, &bar0);
	pci_read_config_dword(wq_pcie->pdev, PCI_BASE_ADDRESS_1, &bar1);
	WQ_DBG(DM_TRBUS, DL_ERR, " PCIe BAR0: 0x%x, BAR1: 0x%x\n", bar0, bar1);

	/* check device id and vendor id */
	pci_read_config_word(wq_pcie->pdev, PCI_VENDOR_ID, &vid);
	pci_read_config_word(wq_pcie->pdev, PCI_DEVICE_ID, &did);
	WQ_DBG(DM_TRBUS, DL_ERR, " PCIe vendor id: 0x%x, device id: 0x%x\n",
	       vid, did);

	/* check inbound */
	tmp = wq_pcie_read32(wq_pcie, HOST_W_AHB_REG_BASEADDR +
					      CFG_WIFI_AHB_BAK_REG0_ADDR);
	WQ_DBG(DM_TRBUS, DL_ERR, " PCIe AHB BAK REG: 0x%x\n", tmp);

	/* show inbound config */
	for (index = 0; index < PCIE_BAR0_ATU_REGION_NUM; index++) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       " ======== WQ PCIe BAR0 REGION %d INFO ========\n",
		       index);
		wq_pcie_read_bar0_inbond_atu(wq_pcie->pdev, index);
	};

	WQ_DBG(DM_TRBUS, DL_ERR, " ======== WQ PCIe DEBUG INFO END ========\n");
	return 0;
}

static void wq_pcie_ce_ring_fill(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
				 u16 nentries)
{
	u32 nbytes;
	dma_addr_t phys_addr;
	struct sk_buff *skb;
	int status;

	if (0 == nentries)
		return;

	nbytes = pcie_attr_table[chn].src_sz_max;

	while (nentries-- > 0) {
		skb = dev_alloc_skb(nbytes);
		BUG_ON(!skb);

		status = wq_map_memory(wq_pcie, skb->data, &phys_addr, nbytes,
				       DMA_FROM_DEVICE);
		BUG_ON(status);

		status = wq_ce_recv(wq_pcie, chn, skb, phys_addr, nbytes, 0);
		BUG_ON(status);
	}
}

static __attribute__((unused)) void wq_pcie_ce_tx_wifi(struct wq_pcie *wq_pcie,
						       CE_CHN_UUID chn)
{
	ENTER();
#if 1
	if (wq_pcie == NULL)
		return;
	wq_ce_irq_mask(wq_pcie, chn, WQ_CE_CHN_SRC);
	wq_pcie->ce_dma_tx_done[chn] = true;

	if (chn == WQ_PCIE_CE_CH_RAW_TX || chn == WQ_PCIE_CE_CH_PKT_TX)
		hrtimer_start(&wq_pcie->ce_tx_timer,
			      ktime_set(0, CE_RAW_PACKET_TX_TIMEOUT_NS),
			      HRTIMER_MODE_REL);
	else
		tasklet_schedule(&wq_pcie->ce_txrx_tasklet);
#else
	u16 nentries = 0;
	struct sk_buff *skb = NULL;
	dma_addr_t phys_addr = 0;
	u32 nbytes = 0;

	ENTER();
	// wq_ce_irq_mask(wq_pcie, chn, WQ_CE_CHN_SRC);

	if (wq_pcie == NULL || chn >= WQ_PCIE_CE_CH_LAST)
		goto send_cb_exit;

	if (pcie_attr_table[chn].src_depth == 0 ||
	    pcie_attr_table[chn].src_sz_max == 0) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: Wrong chan<%d> depth or sz\n",
		       __func__, chn);
		goto send_cb_exit;
	}

	while ((wq_ce_send_completed_next(wq_pcie, chn, (void **)&skb,
					  &phys_addr, &nbytes, NULL)) == 0) {
		if (chn == WQ_PCIE_CE_CH_CMD_TX || chn == WQ_PCIE_CE_CH_RAW_TX)
			htc_tx_done(&wq_pcie->core, skb, 0);
		else {
			WQ_DBG(DM_TRBUS, DL_ERR, "%s: ignore chan<%d> data\n",
			       __func__, chn);
			dev_kfree_skb_any(skb);
		}
		nentries++;
	}

	if (chn == WQ_PCIE_CE_CH_RAW_TX || chn == WQ_PCIE_CE_CH_PKT_TX)
		WQ_DBG(DM_TRBUS, DL_VRB, "ce chn[%d] tx done, nentries:%d\n",
		       chn, nentries);

send_cb_exit:
	LEAVE();
	return;
#endif
	LEAVE();
}

static __attribute__((unused)) void wq_pcie_ce_rx_wifi(struct wq_pcie *wq_pcie,
						       CE_CHN_UUID chn)
{
	bool have_got;
	ENTER();
#if 1
	if (wq_pcie == NULL)
		return;
	have_got = false;
	if (!wq_pcie_is_bus_active((struct wq_core *)wq_pcie)) {
		wq_pcie_autopm_get_async((struct wq_core *)wq_pcie);
		have_got = true;
	}
	wq_ce_irq_mask(wq_pcie, chn, WQ_CE_CHN_DST);
	wq_pcie->ce_dma_rx_done[chn] = true;

	if (chn == WQ_PCIE_CE_CH_RAW_RX)
		hrtimer_start(&wq_pcie->ce_rx_timer,
			      ktime_set(0, CE_RAW_PACKET_RX_TIMEOUT_NS),
			      HRTIMER_MODE_REL);
	else
		tasklet_schedule(&wq_pcie->ce_txrx_tasklet);
	if (have_got) {
		wq_pcie_autopm_put_async((struct wq_core *)wq_pcie);
	}
#else
	u16 nentries = 0;
	struct sk_buff *skb = NULL;
	dma_addr_t phys_addr = 0;
	u32 nbytes = 0;

	ENTER();
	// wq_ce_irq_mask(wq_pcie, chn, WQ_CE_CHN_DST);

	if (wq_pcie == NULL || chn >= WQ_PCIE_CE_CH_LAST)
		goto recv_cb_exit;

	if (pcie_attr_table[chn].dst_depth == 0 ||
	    pcie_attr_table[chn].src_sz_max == 0) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: Wrong chan<%d> depth or sz\n",
		       __func__, chn);
		goto recv_cb_exit;
	}

	while ((wq_ce_recv_completed_next(wq_pcie, chn, (void **)&skb,
					  &phys_addr, &nbytes, NULL)) == 0) {
		wq_sync_memory_for_device(wq_pcie, phys_addr, nbytes,
					  DMA_FROM_DEVICE);
		wq_unmap_memory(wq_pcie, &phys_addr, nbytes, DMA_FROM_DEVICE);

		skb_put(skb, nbytes);
		if (chn == WQ_PCIE_CE_CH_EVT_RX) {
			WQ_DBG(DM_TRBUS, DL_VRB, "%s: chan<%d> data(len:%d):\n",
			       __func__, chn, skb->len);
			dump_bytes(DL_VRB, "EVT_RX:", skb->data,
				   (skb->len > 80) ? 80 : skb->len);
			htc_rx(&wq_pcie->core, WQ_QID_MSG, skb);
		} else if (chn == WQ_PCIE_CE_CH_RAW_RX) {
			WQ_DBG(DM_TRBUS, DL_VRB, "%s: chan<%d> data(len:%d):\n",
			       __func__, chn, skb->len);
			dump_bytes(DL_VRB, "RAW_RX:", skb->data,
				   (skb->len > 80) ? 80 : skb->len);
			htc_rx(&wq_pcie->core, WQ_QID_AC_BK, skb);
		} else {
			WQ_DBG(DM_TRBUS, DL_VRB, "%s: ignore chan<%d> data\n",
			       __func__, chn);
			dev_kfree_skb_any(skb);
		}
		nentries++;
	}

	if (chn == WQ_PCIE_CE_CH_RAW_RX || chn == WQ_PCIE_CE_CH_PKT_RX)
		WQ_DBG(DM_TRBUS, DL_VRB, "ce chn[%d] Rx data, nentries:%d\n",
		       chn, nentries);

	if (nentries)
		wq_pcie_ce_ring_fill(wq_pcie, chn, nentries);

recv_cb_exit:
	// (void)wq_ce_irq_unmask(wq_pcie, chn, WQ_CE_CHN_DST);
	LEAVE();
	return;
#endif
	LEAVE();
}

static void wq_pcie_ce_polling_handle(struct wq_pcie *wq_pcie)
{
	struct sk_buff *skb = NULL;
	dma_addr_t phys_addr = 0;
	u8 chn;
	u32 nbytes = 0;
	int ret = 0;

	for (chn = WQ_PCIE_CE_CH_WIFI_BASE; chn < WQ_PCIE_CE_CH_LAST; chn++) {
		u16 nentries = 0;

		if (pcie_attr_table[chn].src_depth > 0 &&
		    wq_pcie->ce_dma_tx_done[chn]) {
			//check tx_done
			while ((ret = wq_ce_send_completed_next(
				       wq_pcie, chn, (void **)&skb, &phys_addr,
				       &nbytes, NULL)) >= 0) {
				WQ_DBG(DM_TRBUS, DL_VRB, "ce chn[%d] tx done\n",
				       chn);

				if (chn == WQ_PCIE_CE_CH_CMD_TX ||
				    chn == WQ_PCIE_CE_CH_RAW_TX)
					htc_tx_done(&wq_pcie->core, skb, ret);
				else if (chn == WQ_PCIE_CE_CH_PKT_TX)
					htc_ll_msdu_tx_done(&wq_pcie->core, skb, ret);
				else {
					WQ_DBG(DM_TRBUS, DL_ERR,
					       "%s: ignore chan<%d> data\n",
					       __func__, chn);
					htc_tx_skb_dma_unmap(&wq_pcie->core,
							     skb);
					dev_kfree_skb_any(skb);
				}
			}

			wq_pcie->ce_dma_tx_done[chn] = false;
			wq_ce_irq_unmask(wq_pcie, chn, WQ_CE_CHN_SRC);
		}

		if (pcie_attr_table[chn].dst_depth > 0 &&
		    wq_pcie->ce_dma_rx_done[chn]) {
			//polling Rx data
			while ((wq_ce_recv_completed_next(
				       wq_pcie, chn, (void **)&skb, &phys_addr,
				       &nbytes, NULL)) == 0) {
				WQ_DBG(DM_TRBUS, DL_VRB, "ce chn[%d] Rx data\n",
				       chn);

				// Sanity check
				if (nbytes >
				    wq_pcie->ce_attr_table[chn].src_sz_max) {
					WQ_DBG(DM_TRBUS, DL_ERR,
					       "%s: CE nbytes(%d) exceeds max size(%d)!\n",
					       __func__, nbytes,
					       wq_pcie->ce_attr_table[chn]
						       .src_sz_max);
					wq_ce_everything_dump(wq_pcie);
					BUG_ON(1);
				}

				wq_sync_memory_for_cpu(wq_pcie, phys_addr,
						       nbytes, DMA_FROM_DEVICE);
				wq_unmap_memory(
					wq_pcie, &phys_addr,
					wq_pcie->ce_attr_table[chn].src_sz_max,
					DMA_FROM_DEVICE);

				skb_put(skb, nbytes);
				if (chn == WQ_PCIE_CE_CH_EVT_RX) {
					htc_rx(&wq_pcie->core, WQ_QID_MSG, skb);
				} else if (chn == WQ_PCIE_CE_CH_RAW_RX) {
					htc_rx(&wq_pcie->core, WQ_QID_AC_BK,
					       skb);
				} else {
					WQ_DBG(DM_TRBUS, DL_ERR,
					       "%s: ignore chan<%d> data\n",
					       __func__, chn);
					dev_kfree_skb_any(skb);
				}
				nentries++;
			}

			if (nentries)
				wq_pcie_ce_ring_fill(wq_pcie, chn, nentries);

			wq_pcie->ce_dma_rx_done[chn] = false;
			wq_ce_irq_unmask(wq_pcie, chn, WQ_CE_CHN_DST);
		}
	}
}

static void wq_pcie_ce_tasklet(unsigned long data)
{
	struct wq_pcie *wq_pcie = (struct wq_pcie *)data;
	ENTER();
	wq_pcie_ce_polling_handle(wq_pcie);
	LEAVE();
}

static enum hrtimer_restart ce_tx_timer_func(struct hrtimer *timer)
{
	struct wq_pcie *wq_pcie =
		container_of(timer, struct wq_pcie, ce_tx_timer);

	tasklet_schedule(&wq_pcie->ce_txrx_tasklet);
	return HRTIMER_NORESTART;
}

static enum hrtimer_restart ce_rx_timer_func(struct hrtimer *timer)
{
	struct wq_pcie *wq_pcie =
		container_of(timer, struct wq_pcie, ce_rx_timer);

	tasklet_schedule(&wq_pcie->ce_txrx_tasklet);
	return HRTIMER_NORESTART;
}

static void wq_txqring_tasklet(unsigned long data)
{
	struct wq_pcie *wq_pcie = (struct wq_pcie *)data;
	ENTER();
	htc_txq_ring_txdone(&wq_pcie->core);
	LEAVE();
}

static void wq_txqring_2task(struct wq_core *core)
{
	struct wq_pcie *wq_pcie;
	wq_pcie = container_of(core, struct wq_pcie, core);

	tasklet_schedule(&wq_pcie->txqring_tasklet);
}

static void wq_txqring_start_timer(struct wq_core *core)
{
	struct wq_pcie *wq_pcie;
	wq_pcie = container_of(core, struct wq_pcie, core);

	hrtimer_start(&wq_pcie->txqring_timer,
		      ktime_set(0, TXQ_RING_FREE_TIME_NS), HRTIMER_MODE_REL);
}

static enum hrtimer_restart wq_txqring_timer_handle(struct hrtimer *timer)
{
	struct wq_pcie *wq_pcie =
		container_of(timer, struct wq_pcie, txqring_timer);

	tasklet_schedule(&wq_pcie->txqring_tasklet);

	hrtimer_start(&wq_pcie->txqring_timer,
		      ktime_set(0, TXQ_RING_FREE_TIME_NS), HRTIMER_MODE_REL);

	return HRTIMER_NORESTART;
}

int wq_pcie_ce_task_start(struct wq_pcie *wq_pcie)
{
	if (wq_pcie == NULL)
		return -EINVAL;

	tasklet_init(&wq_pcie->ce_txrx_tasklet, wq_pcie_ce_tasklet,
		     (unsigned long)wq_pcie);

	tasklet_init(&wq_pcie->txqring_tasklet, wq_txqring_tasklet,
		     (unsigned long)wq_pcie);

	WQ_DBG(DM_TRBUS, DL_WRN, "%s: ce tx/rx hrtimer init\n", __func__);
	hrtimer_init(&wq_pcie->ce_tx_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	wq_pcie->ce_tx_timer.function = ce_tx_timer_func;
	hrtimer_init(&wq_pcie->ce_rx_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	wq_pcie->ce_rx_timer.function = ce_rx_timer_func;

	hrtimer_init(&wq_pcie->txqring_timer, CLOCK_MONOTONIC,
		     HRTIMER_MODE_REL);
	wq_pcie->txqring_timer.function = wq_txqring_timer_handle;

	return 0;
}

int wq_pcie_ce_task_stop(struct wq_pcie *wq_pcie)
{
	if (wq_pcie == NULL)
		return -EINVAL;

	WQ_DBG(DM_TRBUS, DL_WRN, "%s: stop ce and its tx/rx hrtimer\n",
	       __func__);
	hrtimer_cancel(&wq_pcie->ce_tx_timer);
	hrtimer_cancel(&wq_pcie->ce_rx_timer);
	hrtimer_cancel(&wq_pcie->txqring_timer);
	tasklet_kill(&wq_pcie->ce_txrx_tasklet);
	tasklet_kill(&wq_pcie->txqring_tasklet);

	return 0;
}

/*
 * Support PCI CE BMI interface
 */
static void wq_pcie_ce_rx_bmi(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
	(void)wq_pcie, (void)chn;
	complete(&wq_pcie->bmi_recv_done);
}

int wq_pci_bmi_exchange(struct wq_core *core, void *req, u32 req_len, void *rsp,
			u32 rsp_len_max, int timeout)
{
	int ret;
	struct wq_pcie *wq_pcie;
	void *req_va, *rsp_va;
	dma_addr_t req_pa, rsp_pa;
	u32 rsp_len;

	wq_pcie = container_of(core, struct wq_pcie, core);

	req_va = kmemdup(req, req_len, GFP_KERNEL);
	if (!req_va)
		return -ENOMEM;
	req_pa = dma_map_single(wq_pcie->core.dev, req_va, req_len,
				DMA_TO_DEVICE);
	ret = dma_mapping_error(wq_pcie->core.dev, req_pa);
	if (ret < 0)
		goto free_req_va;

	rsp_va = kzalloc(rsp_len_max, GFP_KERNEL);
	if (!rsp_va)
		goto unmap_req_pa;
	rsp_pa = dma_map_single(wq_pcie->core.dev, rsp_va, rsp_len_max,
				DMA_FROM_DEVICE);
	ret = dma_mapping_error(wq_pcie->core.dev, rsp_pa);
	if (ret)
		goto free_rsp_va;

	ret = wq_ce_recv(wq_pcie, WQ_PCIE_CE_CH_BMI_RX, NULL, rsp_pa,
			 rsp_len_max, CE_DESC_FLAG_INT_EB);
	if (ret < 0)
		goto unmap_rsp_pa;

	dma_sync_single_for_device(wq_pcie->core.dev, req_pa, req_len,
				   DMA_TO_DEVICE);
	ret = wq_ce_send(wq_pcie, WQ_PCIE_CE_CH_BMI_TX, NULL, req_pa, req_len,
			 0);
	if (ret < 0)
		goto unmap_rsp_pa;

	if (!wait_for_completion_timeout(&wq_pcie->bmi_recv_done,
					 HZ * timeout)) {
		ret = -ETIMEDOUT;
		goto unmap_rsp_pa;
	}

	ret = wq_ce_send_completed_next(wq_pcie, WQ_PCIE_CE_CH_BMI_TX, NULL,
					NULL, NULL, NULL);
	if (ret)
		goto unmap_rsp_pa;

	ret = wq_ce_recv_completed_next(wq_pcie, WQ_PCIE_CE_CH_BMI_RX, NULL,
					NULL, &rsp_len, NULL);
	if (ret)
		goto unmap_rsp_pa;

	dma_sync_single_for_cpu(wq_pcie->core.dev, rsp_pa, rsp_len,
				DMA_FROM_DEVICE);
	(void)memcpy(rsp, rsp_va, rsp_len);
	ret = (int)rsp_len;

unmap_rsp_pa:
	dma_unmap_single(wq_pcie->core.dev, rsp_pa, rsp_len_max,
			 DMA_FROM_DEVICE);

free_rsp_va:
	kfree(rsp_va);

unmap_req_pa:
	dma_unmap_single(wq_pcie->core.dev, req_pa, req_len, DMA_TO_DEVICE);

free_req_va:
	kfree(req_va);

	return ret;
}

/*
 * Support PCI CE FW log
 */
static __attribute__((unused)) void wq_pcie_ce_rx_fwlog(struct wq_pcie *wq_pcie,
							CE_CHN_UUID chn)
{
	const wq_ce_attr_t *attr;
	u16 nentries = 0;
	dma_addr_t phys_addr;
	uint32_t size;
	struct sk_buff *skb;

	attr = wq_ce_attr_get(wq_pcie, chn);
	while (!wq_ce_recv_completed_next(wq_pcie, chn, (void **)&skb,
					  &phys_addr, &size, NULL)) {
		dma_sync_single_for_cpu(wq_pcie->core.dev, phys_addr,
					attr->src_sz_max, DMA_FROM_DEVICE);
		dma_unmap_single(wq_pcie->core.dev, phys_addr, attr->src_sz_max,
				 DMA_FROM_DEVICE);
		wq_fw_log_push(&wq_pcie->core, skb, size);
		nentries++;
	}
	if (nentries) {
		wq_pcie_ce_ring_fill(wq_pcie, chn, nentries);
	}
}

static void wq_pcie_ce_chn_init(struct wq_pcie *wq_pcie, CE_CHN_UUID chn,
				const struct wq_pcie_ce_attr *attr)
{
	wq_ce_attr_t attr_basis;
	int ret;
	u16 i;
	void *recv_context;
	dma_addr_t phys_addr;

	BUG_ON(!attr);

	attr_basis.flags = attr->flags;
	attr_basis.src_sz_max = attr->src_sz_max;
	attr_basis.src_depth = attr->src_depth;
	attr_basis.dst_depth = attr->dst_depth;

	ret = wq_ce_chn_init(wq_pcie, chn, &attr_basis);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "wq_ce_chn_init, "
		       "chn = %d, ret = %d\n",
		       (int)chn, ret);
		BUG_ON(1);
	}

	if (attr->src_depth > 0) {
		ret = wq_ce_watermarks_set(wq_pcie, chn, WQ_CE_CHN_SRC,
					   attr->high_watermark,
					   attr->low_watermark);
		BUG_ON(ret);
		(void)wq_ce_int_cb_set(wq_pcie, chn, WQ_CE_CHN_SRC,
				       CE_SRC_INT_CURR_DESC,
				       attr->desc_completed);
		(void)wq_ce_int_cb_set(wq_pcie, chn, WQ_CE_CHN_SRC,
				       CE_SRC_INT_RING_HIGH_WATER,
				       attr->desc_completed);
		(void)wq_ce_int_cb_set(wq_pcie, chn, WQ_CE_CHN_SRC,
				       CE_SRC_INT_RING_LOW_WATER,
				       attr->desc_completed);
		(void)wq_ce_intr_ena_set(wq_pcie, chn, WQ_CE_CHN_SRC,
					 attr->intr_ena);
		wq_pcie->ce_init_bitmap[WQ_CE_CHN_SRC] |= (1u << chn);
		(void)wq_ce_irq_unmask(wq_pcie, chn, WQ_CE_CHN_SRC);
	}

	if (attr->dst_depth > 0) {
		ret = wq_ce_watermarks_set(wq_pcie, chn, WQ_CE_CHN_DST,
					   attr->high_watermark,
					   attr->low_watermark);
		BUG_ON(ret);
		(void)wq_ce_int_cb_set(wq_pcie, chn, WQ_CE_CHN_DST,
				       CE_DST_INT_CURR_DESC,
				       attr->desc_completed);
		(void)wq_ce_int_cb_set(wq_pcie, chn, WQ_CE_CHN_DST,
				       CE_DST_INT_RING_HIGH_WATER,
				       attr->desc_completed);
		(void)wq_ce_int_cb_set(wq_pcie, chn, WQ_CE_CHN_DST,
				       CE_DST_INT_RING_LOW_WATER,
				       attr->desc_completed);
		(void)wq_ce_intr_ena_set(wq_pcie, chn, WQ_CE_CHN_DST,
					 attr->intr_ena);
		if (attr->buf_alloc) {
			/* fill the dst ring full to prepare recv */
			for (i = 1; i < attr->dst_depth; ++i) {
				/* max count: depth - 1 */
				(void)attr->buf_alloc(wq_pcie, attr->src_sz_max,
						      DMA_FROM_DEVICE,
						      &recv_context,
						      &phys_addr);
				(void)wq_ce_recv(wq_pcie, chn, recv_context,
						 phys_addr, attr->src_sz_max,
						 CE_DESC_FLAG_INT_EB);
			}
			/* workaround for low power feature */
			(void)attr->buf_alloc(wq_pcie, attr->src_sz_max,
					      DMA_FROM_DEVICE, &recv_context,
					      &phys_addr);
			wq_ce_recv_dummy_push(wq_pcie, chn, recv_context,
					      phys_addr, attr->src_sz_max,
					      CE_DESC_FLAG_INT_EB);
		}
		wq_pcie->ce_init_bitmap[WQ_CE_CHN_DST] |= (1u << chn);
		(void)wq_ce_irq_unmask(wq_pcie, chn, WQ_CE_CHN_DST);
	}
}

int wq_pcie_ce_init(struct wq_pcie *wq_pcie,
		    const struct wq_pcie_ce_attr attr_table[CE_CHN_MAX])
{
	CE_CHN_UUID chn;

	wq_pcie->ce_attr_table = attr_table;

	for (chn = 0; chn < CE_CHN_MAX; ++chn) {
		if (!attr_table[chn].src_sz_max)
			continue;
		wq_pcie_ce_chn_init(wq_pcie, chn, &attr_table[chn]);
	}

	return 0;
}

void wq_pcie_ce_stop(struct wq_pcie *wq_pcie)
{
	CE_CHN_UUID chn;

	for (chn = 0; chn < CE_CHN_MAX; ++chn) {
		if (!WQ_PCIE_CE_INITED(wq_pcie, chn))
			continue;
		(void)wq_ce_stop(wq_pcie, chn);
	}
}

void wq_pcie_ce_cleanup(struct wq_pcie *wq_pcie)
{
	CE_CHN_UUID chn;
	void *send_context;
	dma_addr_t phys_addr;
	u32 nbytes;

	for (chn = 0; chn < CE_CHN_MAX; ++chn) {
		if (!wq_pcie->ce_states[chn].src ||
		    !wq_pcie->ce_attr_table[chn].buf_free)
			continue;
		while (!wq_ce_send_cancel_next(wq_pcie, chn, &send_context,
					       &phys_addr, &nbytes, NULL)) {
			wq_pcie->ce_attr_table[chn].buf_free(wq_pcie, phys_addr,
							     nbytes,
							     DMA_TO_DEVICE,
							     send_context);
		}
	}
}

static void wq_pcie_ce_chn_deinit(struct wq_pcie *wq_pcie, CE_CHN_UUID chn)
{
	void *recv_context;
	dma_addr_t phys_addr;
	u32 nbytes;

	if (wq_pcie->ce_states[chn].dst &&
	    wq_pcie->ce_attr_table[chn].buf_free) {
		while (!wq_ce_recv_revoke_next(wq_pcie, chn, &recv_context,
					       &phys_addr, &nbytes, NULL)) {
			wq_pcie->ce_attr_table[chn].buf_free(wq_pcie, phys_addr,
							     nbytes,
							     DMA_FROM_DEVICE,
							     recv_context);
		}
		/* workaround for low power feature */
		wq_ce_recv_dummy_pop(wq_pcie, chn, &recv_context, &phys_addr,
				     &nbytes, NULL);
		wq_pcie->ce_attr_table[chn].buf_free(wq_pcie, phys_addr, nbytes,
						     DMA_FROM_DEVICE,
						     recv_context);
	}

	wq_pcie->ce_init_bitmap[WQ_CE_CHN_SRC] &= ~(1u << chn);
	wq_pcie->ce_init_bitmap[WQ_CE_CHN_DST] &= ~(1u << chn);

	(void)wq_ce_chn_deinit(wq_pcie, chn);
}

void wq_pcie_ce_deinit(struct wq_pcie *wq_pcie)
{
	CE_CHN_UUID chn;

	for (chn = 0; chn < CE_CHN_MAX; ++chn) {
		if (!WQ_PCIE_CE_INITED(wq_pcie, chn))
			continue;
		wq_pcie_ce_chn_deinit(wq_pcie, chn);
	}

	wq_pcie->ce_attr_table = NULL;
}

/**
 * About PCIe interrupt controller
*/
void wq_pcie_irq_mask_set(struct wq_pcie *wq_pcie, u8 group, u8 bit, int set)
{
	static const u16 cfg_intr_ena_regs[] = {
		CFG_INT_ENA0_ADDR, CFG_INT_ENA1_ADDR, CFG_INT_ENA2_ADDR,
		CFG_INT_ENA3_ADDR, CFG_INT_ENA4_ADDR, CFG_INT_ENA5_ADDR,
	};

	unsigned long flags;

	spin_lock_irqsave(&wq_pcie->intc_lock, flags);

	if (group < ARRAY_SIZE(cfg_intr_ena_regs)) {
		u32 reg =
			HOST_PCIE_INTC_REG_BASEADDR + cfg_intr_ena_regs[group];
		u32 int_ena = wq_pcie_read32(wq_pcie, reg);
		u32 mask = BIT(bit);

		if (set)
			int_ena |= mask;
		else
			int_ena &= ~mask;
		wq_pcie_write32(wq_pcie, reg, int_ena);
	}
	spin_unlock_irqrestore(&wq_pcie->intc_lock, flags);
}

bool wq_pcie_irq_mask_get(struct wq_pcie *wq_pcie, u8 group, u8 bit)
{
	static const u16 cfg_intr_ena_regs[] = {
		CFG_INT_ENA0_ADDR, CFG_INT_ENA1_ADDR, CFG_INT_ENA2_ADDR,
		CFG_INT_ENA3_ADDR, CFG_INT_ENA4_ADDR, CFG_INT_ENA5_ADDR,
	};

	u32 address, value;

	if (group >= ARRAY_SIZE(cfg_intr_ena_regs))
		return false;

	address = HOST_PCIE_INTC_REG_BASEADDR + cfg_intr_ena_regs[group];
	value = wq_pcie_read32(wq_pcie, address);

	return !!(value & BIT(bit));
}

static int wq_pcie_tx(struct wq_core *core, enum wq_hif_qid qid,
	struct sk_buff_head *skbq)
{
	struct wq_pcie *wq_pcie = container_of(core, struct wq_pcie, core);
	enum wq_pcie_ce_ch ch =
		qid == WQ_QID_MSG ? WQ_PCIE_CE_CH_CMD_TX : WQ_PCIE_CE_CH_RAW_TX;
	struct sk_buff *skb;
	int ret = 0;

	/* FIXME: check CE remainder depth */
	while ((skb = __skb_dequeue(skbq))) {
		struct wq_skb_txcb *txcb = WQ_SKB_TXCB(skb);
		dma_addr_t phys_addr = txcb->phyaddr;
		u16 dma_len = txcb->total_dma_len;
		u32 flag = txcb->more_msdu ? CE_DESC_FLAG_GATHER_EB :
						   CE_DESC_FLAG_INT_EB;

		BUG_ON(!phys_addr);
		WQ_DBG(DM_TRBUS, DL_VRB,
			"%s: qid=%d, skb=%p, data=%p, len=%d/%d,"
			" pkt_cls=%x, in_host=%d, has_hif_htc=%d, more=%d\n",
			__func__, qid, skb, skb->data, skb->len, dma_len,
			txcb->pkt_cls, txcb->msdu_in_host, txcb->has_hif_htc,
			txcb->more_msdu);

		/* dma_map_single is done by core, finally core should unmap it */
		dma_sync_single_for_device(wq_pcie->core.dev, phys_addr,
					   dma_len, DMA_TO_DEVICE);

		if (qid == WQ_QID_MSG)
			BUG_ON(txcb->msdu_in_host);

		if (((txcb->pkt_cls &
			(BIT(WQ_PKT_CLS_EAPOL) | BIT(WQ_PKT_CLS_DHCP) |
			BIT(WQ_PKT_CLS_ARP))) ||
			txcb->is_small_pkt) &&
			txcb->qid != WQ_QID_AC_VO) {
			WQ_DBG(DM_TRBUS, DL_WRN,
				"%s: qid=%d, inhost:%d, pktcls:0x%x, issmallpkt:%d\n",
				__func__, qid, txcb->msdu_in_host, txcb->pkt_cls,
				txcb->is_small_pkt);
		}

		/* actual length should be transfered by CE */
		if (!txcb->msdu_in_host) {
			/* TCP ACK use HL mode */
			if (txcb->pkt_cls & BIT(WQ_PKT_CLS_TCP_ACK)) {
				BUG_ON(ch != WQ_PCIE_CE_CH_RAW_TX);
				if (txcb->has_hif_htc) { /* the first one */
					dma_len =
						ALIGN((skb->len - TAILROOM_HIF),
						      sizeof(u32));
				} else { /* not the first one */
					dma_len = ALIGN(skb->len, sizeof(u32));
					phys_addr += (HEADROOM_HIF_HTC_TXDESC - BUNDLE_HDR_LEN);
				}
				if (!txcb->more_msdu) /* the last one */
					dma_len += TAILROOM_HIF;

				WQ_DBG(DM_TRBUS, DL_VRB,
					"%s: qid=%d, has_hif_htc=%d dma_len %d, skblen:%d, hashifhtc:%d, moremsdu:%d\n",
					__func__, qid, txcb->has_hif_htc,
					dma_len, skb->len, txcb->has_hif_htc,
					txcb->more_msdu);
			} else {
				BUG_ON(txcb->more_msdu);
				if (WARN_ON(skb->len != dma_len)) {
					WQ_DBG(DM_TRBUS, DL_WRN,
					       "%s: qid=%d, has_hif_htc=%d dma_len %d,"
					       " skb [%d]: %*ph\n",
					       __func__, qid, txcb->has_hif_htc,
					       dma_len, skb->len,
					       HEADROOM_HIF_HTC_TXDESC,
					       skb->data);
				}
				BUG_ON(!txcb->has_hif_htc);
			}
		} else {
			ch = WQ_PCIE_CE_CH_PKT_TX;
			if (txcb->has_hif_htc) { /* the first one */
				dma_len = HEADROOM_HIF_HTC_TXDESC;
			} else {
				dma_len = HEADROOM_TXDESC;
				phys_addr += HEADROOM_HIF_HTC;
			}
			if (!txcb->more_msdu) /* the last one */
				dma_len += TAILROOM_HIF;
		}

		/* tx time to hif, unit ms */
		txcb->tx_ms = jiffies_to_msecs(jiffies);

		ret = wq_ce_send(wq_pcie, ch, skb, phys_addr, dma_len, flag);
		if (ret && ret != -ENOBUFS) {
			__skb_queue_head(skbq, skb);
			wq_ce_chn_ring_dump(wq_pcie, ch);
			return ret;
		}
	}

	return ret;
}

static void wq_pcie_dump_info(struct wq_core *core)
{
	struct wq_pcie *wq_pcie = container_of(core, struct wq_pcie, core);
	CE_CHN_UUID chn;

	for (chn = 0; chn < WQ_PCIE_CE_CH_LAST; chn++)
		if (BIT(chn) & WQ_PCIE_CHN_DUMP_MASK)
			wq_ce_chn_ring_dump(wq_pcie, chn);

	for (chn = 0; chn < WQ_PCIE_CE_CH_LAST; chn++)
		if (BIT(chn) & WQ_PCIE_CHN_DUMP_MASK)
			wq_ce_dump_chan_dbg_sts(wq_pcie, chn);
}

static int wq_pcie_autopm_get_async(struct wq_core *core)
{
	struct wq_pcie *wq_pcie = (struct wq_pcie *)core;
	ENTER();
	return pm_runtime_get(wq_pcie->core.dev);
}

static void wq_pcie_autopm_put_async(struct wq_core *core)
{
	struct wq_pcie *wq_pcie = (struct wq_pcie *)core;
	ENTER();
	pm_runtime_mark_last_busy(wq_pcie->core.dev);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0)
	__pm_runtime_put_autosuspend(wq_pcie->core.dev);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0) */
	pm_runtime_put_autosuspend(wq_pcie->core.dev);
#endif
}

static bool wq_pcie_is_bus_active(struct wq_core *core)
{
	struct wq_pcie *wq_pcie = (struct wq_pcie *)core;
	BUG_ON(wq_pcie->pm_status == PCIE_SYS_SUSPENDING ||
	       wq_pcie->pm_status == PCIE_SYS_SUSPENDED);
	return wq_pcie->pm_status == PCIE_ACTIVE;
}

static void wq_pcie_runtime_allow(struct wq_core *core)
{
	struct wq_pcie *wq_pcie = (struct wq_pcie *)core;
	pm_runtime_allow(wq_pcie->core.dev);
}

static int hif_get_hdr_sz_pcie(struct wq_core *core)
{
	return sizeof(struct wq_hif_hdr);
}

static struct wq_hif_ops wq_pcie_ops = {
	.hif = WQ_HIF_PCIE,
	.low_speed_mode = false,
	.txq_stop_threshlod = FLOW_CTRL_THRESHOLD_STOP,
	.txq_restart_threshlod = FLOW_CTRL_THRESHOLD_RESTART,

	.autopm_get_async = wq_pcie_autopm_get_async,
	.autopm_put_async = wq_pcie_autopm_put_async,
	.autopm_allow = wq_pcie_runtime_allow,
	.autopm_is_bus_active = wq_pcie_is_bus_active,

	.hif_tx = wq_pcie_tx,
	.hif_get_hdr_sz = hif_get_hdr_sz_pcie,

	.dump_info = wq_pcie_dump_info,

	.bmi_cmd = wq_pcie_bmi_cmd,
	.bmi_xfer = wq_pcie_bmi_xfer,
	.bmi_exchange = wq_pci_bmi_exchange,

	.hif_txq_ring_2task = wq_txqring_2task,
	.hif_txq_ring_timerstart = wq_txqring_start_timer,

#ifdef CONFIG_WQ_DTOP
	.dtop_bulk_send = wq_pci_dtop_send,
#endif
};

#if PCIE_NUM_MSIX_VECTORS
static int wq_pcie_init_irq_msix(struct pci_dev *pdev, struct wq_pcie *wq_pcie)
{
	int ret = -1;
	unsigned char i, j;

	for (i = 0; i < PCIE_NUM_MSIX_VECTORS; i++)
		wq_pcie->msix.entries[i].entry = i;

		/* Try to enable msix */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	ret = pci_enable_msix_exact(pdev, wq_pcie->msix.entries,
				    PCIE_NUM_MSIX_VECTORS);
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31) */
	ret = pci_enable_msix(pdev, wq_pcie->msix.entries,
			      PCIE_NUM_MSIX_VECTORS);
#endif
	if (ret)
		return ret;

	for (i = 0; i < PCIE_NUM_MSIX_VECTORS; i++) {
		wq_pcie->msix.context[i].dev = pdev;
		wq_pcie->msix.context[i].msg_id = i;
		ret = request_irq(wq_pcie->msix.entries[i].vector,
				  wq_pcie_interrupt_msix, 0, "wq_pcie_msix",
				  &wq_pcie->msix.context[i]);

		if (ret)
			break;
	}
	if (i == PCIE_NUM_MSIX_VECTORS)
		return 0;

	WQ_DBG(DM_TRBUS, DL_ERR, "request_irq failed: ret=%d\n", ret);
	for (j = 0; j < i; j++)
		free_irq(wq_pcie->msix.entries[j].vector,
			 &wq_pcie->msix.context[i]);
	pci_disable_msix(pdev);
	return ret;
}
#endif

static int wq_pcie_init_irq(struct wq_pcie *wq_pcie)
{
	struct pci_dev *pdev = to_pci_dev(wq_pcie->core.dev);
	int ret;

	ENTER();
	if (pci_msi_enabled()) {
#if PCIE_NUM_MSIX_VECTORS
		/* try MSIX */
		wq_pcie->intr_mode = PCIE_INTR_MODE_MSIX;
		ret = wq_pcie_init_irq_msix(pdev, wq_pcie);
		if (ret == 0)
			return 0;
#endif

		/* try MSI */
		wq_pcie->intr_mode = PCIE_INTR_MODE_MSI;
		ret = pci_enable_msi(pdev);
		if (ret == 0) {
			ret = request_irq(pdev->irq, wq_pcie_interrupt,
					  IRQF_SHARED, "wq_pcie_msi", pdev);
			if (ret == 0)
				return 0;

			pci_disable_msi(pdev);
		}
		WQ_DBG(DM_TRBUS, DL_ERR, "request_irq/MSI failed: ret=%d\n",
		       ret);
	}

	wq_pcie->intr_mode = PCIE_INTR_MODE_LEGACY;
	ret = request_irq(pdev->irq, wq_pcie_interrupt, IRQF_SHARED, "wq_pcie",
			  pdev);
	if (ret)
		WQ_DBG(DM_TRBUS, DL_ERR, "request_irq failed: ret=%d\n", ret);

	return ret;
}

static void wq_pcie_deinit_irq(struct wq_pcie *wq_pcie)
{
	u32 msi_pending;
	u16 control;
	struct pci_dev *pdev = to_pci_dev(wq_pcie->core.dev);

	free_irq(pdev->irq, pdev);
	if (wq_pcie->intr_mode == PCIE_INTR_MODE_MSI) {
		pci_disable_msi(pdev);
	}

	pci_read_config_word(pdev, pdev->msi_cap + PCI_MSI_FLAGS, &control);
	pci_read_config_dword(pdev, pdev->msi_cap + PCI_MSI_PENDING_32,
			      &msi_pending);
	WQ_DBG(DM_TRBUS, DL_ERR, "msi control: 0x%x, msi pending: 0x%x\n",
	       control, msi_pending);
}

static void wq_pcie_prog_bar0_inbond_atu(struct pci_dev *pdev, int index,
					 u64 base)
{
	pci_write_config_dword(pdev, PCIE_ATU_VIEWPORT,
			       PCIE_ATU_REGION_INBOUND | index);

	pci_write_config_dword(
		pdev, PCIE_ATU_LOWER_BASE,
		lower_32_bits(base + wq_pcie_bar0_region[index].base_addr));
	pci_write_config_dword(
		pdev, PCIE_ATU_UPPER_BASE,
		upper_32_bits(base + wq_pcie_bar0_region[index].base_addr));

	pci_write_config_dword(
		pdev, PCIE_ATU_LIMIT,
		lower_32_bits(base + wq_pcie_bar0_region[index].limit_addr));

	pci_write_config_dword(pdev, PCIE_ATU_LOWER_TARGET,
			       wq_pcie_bar0_region[index].target_addr);
	pci_write_config_dword(pdev, PCIE_ATU_UPPER_TARGET, 0);

	pci_write_config_dword(pdev, PCIE_ATU_CR2, PCIE_ATU_ENABLE);
}

static void wq_pcie_read_bar0_inbond_atu(struct pci_dev *pdev, int index)
{
	u32 tmp;
	u32 bar0, bar1;
	pci_read_config_dword(pdev, 0x10, &bar0);
	pci_read_config_dword(pdev, 0x14, &bar1);
	WQ_DBG(DM_TRBUS, DL_ERR, "PCIe BAR0: 0x%x, BAR1: 0x%x\n", bar0, bar1);

	pci_write_config_dword(pdev, PCIE_ATU_VIEWPORT,
			       PCIE_ATU_REGION_INBOUND | index);

	pci_read_config_dword(pdev, PCIE_ATU_VIEWPORT, &tmp);
	WQ_DBG(DM_TRBUS, DL_ERR, "PCIe ATU VIEWPORT: 0x%x\n", tmp);

	pci_read_config_dword(pdev, PCIE_ATU_LOWER_BASE, &tmp);
	WQ_DBG(DM_TRBUS, DL_ERR, "PCIe ATU LOWER BASE: 0x%x\n", tmp);

	pci_read_config_dword(pdev, PCIE_ATU_UPPER_BASE, &tmp);
	WQ_DBG(DM_TRBUS, DL_ERR, "PCIe ATU UPPER BASE: 0x%x\n", tmp);

	pci_read_config_dword(pdev, PCIE_ATU_LIMIT, &tmp);
	WQ_DBG(DM_TRBUS, DL_ERR, "PCIe ATU LIMIT: 0x%x\n", tmp);

	pci_read_config_dword(pdev, PCIE_ATU_LOWER_TARGET, &tmp);
	WQ_DBG(DM_TRBUS, DL_ERR, "PCIe ATU LOWER TARGET: 0x%x\n", tmp);

	pci_read_config_dword(pdev, PCIE_ATU_UPPER_TARGET, &tmp);
	WQ_DBG(DM_TRBUS, DL_ERR, "PCIe ATU UPPER TARGET: 0x%x\n", tmp);

	pci_read_config_dword(pdev, PCIE_ATU_CR2, &tmp);
	WQ_DBG(DM_TRBUS, DL_ERR, "PCIe ATU CR2: 0x%x\n", tmp);
}

static void wq_pcie_cfg_inbond(struct pci_dev *pdev)
{
	u64 base;
	u32 val;
	int index;

	pci_read_config_dword(pdev, 0x14, &val);
	base = val;
	base = base << 32;
	pci_read_config_dword(pdev, 0x10, &val);
	base |= val & 0xfffffff0;

	WQ_DBG(DM_TRBUS, DL_INF, "PCIe bar0 base : %llx\n", base);

	for (index = 0; index < PCIE_BAR0_ATU_REGION_NUM; index++) {
		wq_pcie_prog_bar0_inbond_atu(pdev, index, base);
	}
}

static int wq_pcie_hw_claim(struct pci_dev *pdev, struct wq_pcie *wq_pcie)
{
	int ret;

	pci_set_drvdata(pdev, wq_pcie);

	ret = pci_enable_device(pdev);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR, "failed to enable pci device: %d\n",
		       ret);
		return ret;
	}

	ret = pci_request_region(pdev, BAR_NUM, WUQI_DRV_NAME);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR, "failed to request region BAR%d: %d\n",
		       BAR_NUM, ret);
		goto err_device;
	}

	wq_pcie_cfg_inbond(pdev);

	/* Target expects 32 bit DMA. Enforce it. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 27)
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
#else
	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (0 == ret)
		dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
#endif
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "failed to set dma mask to 32-bit: %d\n", ret);
		goto err_region;
	}

	pci_set_master(pdev);

	/* Arrange for access to Target SoC registers. */
	wq_pcie->mem_len = pci_resource_len(pdev, BAR_NUM);
	wq_pcie->mem = pci_iomap(pdev, BAR_NUM, 0);
	if (!wq_pcie->mem) {
		WQ_DBG(DM_TRBUS, DL_ERR, "failed to iomap BAR%d\n", BAR_NUM);
		ret = -EIO;
		goto err_master;
	}

	WQ_DBG(DM_TRBUS, DL_INF, "PCI memory: %p", wq_pcie->mem);
	wq_pcie->pdev = pdev;
	return 0;

err_master:
	pci_clear_master(pdev);

err_region:
	pci_release_region(pdev, BAR_NUM);

err_device:
	pci_disable_device(pdev);

	return ret;
}

static __maybe_unused void pci_clear_and_set_dword(struct pci_dev *pdev,
						   int pos, u32 clear, u32 set)
{
	u32 val;

	pci_read_config_dword(pdev, pos, &val);
	val &= ~clear;
	val |= set;
	pci_write_config_dword(pdev, pos, val);
}

int wq_pcie_mailbox_send_msg(struct wq_pcie *wq_pcie, u32 mbox_id, u32 msg)
{
	u32 reg_data;
	u32 mbox_base;
	u32 retry_count = 0;

	mbox_base = MAILBOX_N_BASEADDR(mbox_id);
	while (retry_count++ < 10) {
		reg_data =
			wq_pcie_read32(wq_pcie, mbox_base + CFG_MSG_STS_ADDR);
		if (!((reg_data >> MAILBOX_AUTO_FILL_ENA_OFFSET) & 0x01)) {
			wq_pcie_write32(wq_pcie, mbox_base + CFG_MSG_WDATA_ADDR,
					msg);
			break;
		} else {
			WQ_DBG(DM_TRBUS, DL_ERR,
			       "mailbox status=0x%x, bit12=%d\n", reg_data,
			       (reg_data >> MAILBOX_AUTO_FILL_ENA_OFFSET) &
				       0x01);
			udelay(1000);
		}
	}

	BUG_ON(retry_count >= 10);

	return 0;
}

static __maybe_unused void wq_pcie_enable_L1ss(struct pci_dev *pdev, u8 enable)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
	int l1ss;
	u32 l1ss_ctrl;

	l1ss = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_L1SS);
	if (!l1ss) {
		WQ_DBG(DM_GENERIC, DL_INF, "l1ss not find \n");
		return;
	}

	if (enable) {
		// TBD:
	} else {
		pci_clear_and_set_dword(pdev, l1ss + PCI_L1SS_CTL1,
					PCI_L1SS_CTL1_L1SS_MASK, 0);
		WQ_DBG(DM_GENERIC, DL_INF, "disable l1ss \n");
		WQ_DBG(DM_GENERIC, DL_INF, "l1ss ctrl = %x \n",
		       pci_read_config_dword(pdev, l1ss + PCI_L1SS_CTL1,
					     &l1ss_ctrl));
	}
#endif
}

static __maybe_unused void wq_pcie_disable_aspm(struct pci_dev *pdev)
{
	u16 child_lnkctl, parent_lnkctl = 0;
	struct pci_dev *parent = pdev->bus->self;

	ENTER();
	pcie_capability_read_word(pdev, PCI_EXP_LNKCTL, &child_lnkctl);
	child_lnkctl &= PCI_EXP_LNKCTL_ASPMC;
	if (parent) {
		pcie_capability_read_word(parent, PCI_EXP_LNKCTL,
					  &parent_lnkctl);
		parent_lnkctl &= PCI_EXP_LNKCTL_ASPMC;
	}

	if (!child_lnkctl && (!parent || !parent_lnkctl)) {
		/* aspm already disabled */
		WQ_DBG(DM_GENERIC, DL_INF, "aspm already disabled\n");
		return;
	}

	pcie_capability_clear_word(pdev, PCI_EXP_LNKCTL, child_lnkctl);
	// if (parent)
	// 	pcie_capability_clear_word(parent, PCI_EXP_LNKCTL,
	// 				   child_lnkctl);

	LEAVE();
	return;
}

static __maybe_unused void wq_pcie_enable_aspm(struct pci_dev *pdev)
{
	u32 parent_lnkcap = 0, child_lnkcap = 0;
	u16 parent_lnkctl = 0, child_lnkctl = 0;
	u32 link_val;
	struct pci_dev *parent = pdev->bus->self;
	struct pci_dev *child = pdev;

	ENTER();
	pcie_capability_read_dword(child, PCI_EXP_LNKCAP, &child_lnkcap);
	pcie_capability_read_word(child, PCI_EXP_LNKCTL, &child_lnkctl);
	if (parent) {
		pcie_capability_read_dword(parent, PCI_EXP_LNKCAP,
					   &parent_lnkcap);
		pcie_capability_read_word(parent, PCI_EXP_LNKCTL,
					  &parent_lnkctl);
	}
	if (!(parent_lnkcap & child_lnkcap & PCI_EXP_LNKCAP_ASPMS)) {
		WQ_DBG(DM_GENERIC, DL_INF,
		       "aspm not support: parent: 0x%x, child: 0x%x\n",
		       parent_lnkcap, child_lnkcap);
		return;
	}

	if (child_lnkctl & parent_lnkctl & PCI_EXP_LNKCTL_ASPMC) {
		/* aspm already enabled */
		WQ_DBG(DM_GENERIC, DL_INF, "aspm already enabled\n");
		return;
	}

	link_val = parent_lnkcap & child_lnkcap & PCI_EXP_LNKCTL_ASPMC;
	// if (parent) {
	// 	pcie_capability_set_dword(parent, PCI_EXP_LNKCTL, link_val);
	// }
	pcie_capability_set_dword(pdev, PCI_EXP_LNKCTL, link_val);

	LEAVE();
	return;
}

static void wq_pcie_release(struct wq_pcie *wq_pcie)
{
	struct pci_dev *pdev = to_pci_dev(wq_pcie->core.dev);

	pci_iounmap(pdev, wq_pcie->mem);
	pci_release_region(pdev, BAR_NUM);
	pci_clear_master(pdev);
	pci_disable_device(pdev);
}

#ifdef CONFIG_PM
/**
 * wq_pcie_runtime_init() - Initialize Runtime PM
 * @dev: device structure
 * @delay: delay to be confgured for auto suspend
 *
 * This function will init all the Runtime PM config.
 *
 * Return: void
 */
static __attribute__((unused)) void wq_pcie_runtime_init(struct device *dev,
							 int delay)
{
	struct wq_pcie *wq_pcie = dev_get_drvdata(dev);
	wq_pcie->pm_status = PCIE_ACTIVE;
	if (!pm_runtime_active(dev)) {
		pm_runtime_set_active(dev);
	}
	if (!pm_runtime_enabled(dev)) {
		pm_runtime_enable(dev);
	}
	pm_runtime_set_autosuspend_delay(dev, delay);
	pm_runtime_use_autosuspend(dev);
	// pm_runtime_allow(dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_noidle(dev);
	WQ_DBG(DM_TRBUS, DL_INF, "dev %s pcie runtime pm inited\n",
	       dev_name(dev));
}
#endif

static void wq_pcie_wakeup_target(struct wq_pcie *wq_pcie)
{
	struct pci_dev *pdev = to_pci_dev(wq_pcie->core.dev);
	int pos;
	u16 reg16;
	int count = 0;

	pos = pci_find_capability(pdev, PCI_CAP_ID_EXP);

	if (!pos)
		return;

	pci_read_config_word(pdev, pos + PCI_EXP_DEVCTL2, &reg16);
	reg16 &= ~0x6000;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
	reg16 |= PCI_EXP_DEVCTL2_OBFF_MSGA_EN;
#else
	reg16 |= PCI_EXP_OBFF_MSGA_EN;
#endif
	pci_write_config_word(pdev, pos + PCI_EXP_DEVCTL2, reg16);

	while (1) {
		pci_read_config_word(pdev, pos + PCI_EXP_DEVCTL2, &reg16);

		reg16 = (reg16 >> 13 & 0x3);
		if (reg16 == 0x2) {
			WQ_DBG(DM_TRBUS, DL_WRN, "Wakeup target success.\n");
			wq_pcie->wakeup_target_timeout = false;
			break;
		}
		count++;
		if (count > 10) {
			WQ_DBG(DM_TRBUS, DL_WRN, "Wakeup target timeout.\n");
			wq_pcie->wakeup_target_timeout = true;
			break;
		}
		msleep(50);
	}

	//
	pci_read_config_word(pdev, pos + PCI_EXP_DEVCTL2, &reg16);
	reg16 &= ~0x6000;
	pci_write_config_word(pdev, pos + PCI_EXP_DEVCTL2, reg16);
}

static void wq_pcie_shutdown_target(struct wq_pcie *wq_pcie)
{
	wq_pcie_write32(wq_pcie,
			HOST_W_AHB_REG_BASEADDR + CFG_WIFI_SCRATCH3_ADDR,
			0xA5A5A5A5);

	msleep(10);

	WQ_DBG(DM_TRBUS, DL_WRN, "target shutdown...\n");
}

#ifdef CONFIG_ENABLE_WAKEUP_IB
static void wq_pcie_ib_wakeup_host(static pci_dev *pdev)
{
	/* unsupport */
}
#endif

static int wq_pcie_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct wq_pcie *wq_pcie;
	enum wq_wphy_profile wprofile = id->driver_data;
	int ret;
	u8 force_pcie_speed = wq_pcie_get_link_speed(pdev);

	ENTER();
	WQ_DBG(DM_TRBUS, DL_INF,
	       "vendor=0x%4.04X device=0x%4.04X rev=%d %s, pciespeed:%d\n",
	       pdev->vendor, pdev->device, pdev->revision,
	       wq_wphy_profile_name(wprofile), force_pcie_speed);

	if (force_pcie_speed == PCI_EXP_LNKSTA_CLS_2_5GB) {
		wq_pcie_ops.low_speed_mode = true;
	}

	wq_pcie = (struct wq_pcie *)wq_core_create(
		&wq_pcie_ops, &pdev->dev, wprofile, sizeof(struct wq_pcie));
	if (!wq_pcie)
		return -ENOMEM;

	spin_lock_init(&wq_pcie->intc_lock);
	init_completion(&wq_pcie->bmi_recv_done);

	ret = wq_pcie_hw_claim(pdev, wq_pcie);
	if (ret)
		goto err_free_wq_pcie;

	/* TODO: force target awake */
	wq_pcie_wakeup_target(wq_pcie);

	wq_pcie_show_bus_info(wq_pcie);

	/* TODO: disable interrupts */

	ret = wq_pcie_init_irq(wq_pcie);
	if (ret)
		goto err_pci_release;

	// disable ASPM
	wq_pcie_disable_aspm(pdev);

	ret = wq_fw_name_update(&wq_pcie->core, fw_dtop_pcie, fw_wifi_pcie);
	if (ret)
		goto err_deinit_irq;

	ret = wq_fw_dtop_init(&wq_pcie->core);
	if (ret)
		goto err_deinit_irq;

	/* Workaround: wait dtop running */
	msleep(500);

	ret = wq_pcie_ce_init(wq_pcie, pcie_attr_table);
	if (ret)
		goto err_deinit_irq;

#ifdef CONFIG_WQ_DTOP
	ret = wq_pcie_app_init(wq_pcie);
	if (ret)
		goto err_ce_deinit;
#endif

#ifndef CONFIG_PCIE_UNIT_TEST
	wq_pcie_ce_task_start(wq_pcie);
#endif

#ifdef CONFIG_RK3588_ENABLE_WAKEUP_OOB
	/* register oob wakeup way */
	WQ_DBG(DM_TRBUS, DL_INF, "%s wq core config oob wakeup method...\n",
	       __FUNCTION__);
	ret = wq_core_register_oob_wakeup_host(&wq_pcie->core);
	if (ret < 0) {
		WQ_DBG(DM_TRBUS, DL_ERR,
		       "%s wq core oob wakeup register failed: %d\n",
		       __FUNCTION__, ret);
		goto err_res_release;
	}
#endif

	//1. fw download;
	ret = wq_fw_init(&wq_pcie->core);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_ERR, "Firmware Init Failed\n");
		goto err_res_release;
	}

#ifdef CONFIG_PCIE_UNIT_TEST
	msleep(1000);
	wq_pcie_test_start(wq_pcie);
	wq_ce_test_start(wq_pcie);
#else
	//2. wait device ready
	ret = wq_wlan_fw_ready(&wq_pcie->core, 8000);
	if (ret) {
		goto err_res_release;
	}

	// pcie_runtime shall be inited before creating wlan and after fw_ready
#ifdef CONFIG_PM
#ifdef CONFIG_WQ_WLAN_PM
	wq_pcie_runtime_init(&pdev->dev, WQ_PCIE_AUTUSUSPEND_DELAY_MS);
#endif
#endif

	//3. create wlan interface;
	ret = wq_wlan_create(&wq_pcie->core, 0);
	if (ret)
		goto err_res_release;

	// wq_pcie_enable_L1ss(pdev, 0);
	// enable ASPM after fw download compelte
	wq_pcie_enable_aspm(pdev);

	LEAVE();
#endif /* CONFIG_PCIE_UNIT_TEST */
	return 0;

err_res_release:
#ifdef CONFIG_PCIE_UNIT_TEST
	wq_ce_test_stop(wq_pcie);
	wq_pcie_test_stop(wq_pcie);
#else
	wq_pcie_ce_task_stop(wq_pcie);
#endif

#ifdef CONFIG_WQ_DTOP
	wq_pcie_app_deinit(wq_pcie);
#endif

#ifdef CONFIG_RK3588_ENABLE_WAKEUP_OOB
	wq_core_unregister_oob_wakeup_host(&wq_pcie->core);
#endif
err_ce_deinit:
	wq_pcie_ce_deinit(wq_pcie);
err_deinit_irq:
	wq_pcie_deinit_irq(wq_pcie);
err_pci_release:
	wq_pcie_shutdown_target(wq_pcie);
	wq_pcie_release(wq_pcie);
err_free_wq_pcie:
	pci_set_drvdata(pdev, NULL);

	wq_core_destroy(&wq_pcie->core);

	LEAVE();
	return ret;
}

static void wq_pcie_remove(struct pci_dev *pdev)
{
	struct wq_pcie *wq_pcie = pci_get_drvdata(pdev);

	ENTER();
	if (!wq_pcie) {
		WQ_DBG(DM_TRBUS, DL_INF, "PCIE device removed from slot\n");
		LEAVE();
		return;
	}

#ifdef CONFIG_PCIE_UNIT_TEST
	wq_ce_test_stop(wq_pcie);
	wq_pcie_test_stop(wq_pcie);
#endif

	wq_wlan_unregister(&wq_pcie->core);
	wq_wlan_destroy(&wq_pcie->core);

#ifndef CONFIG_PCIE_UNIT_TEST
	wq_pcie_ce_task_stop(wq_pcie);
#endif

#ifdef CONFIG_WQ_DTOP
	wq_pcie_app_deinit(wq_pcie);
#endif

#ifdef CONFIG_RK3588_ENABLE_WAKEUP_OOB
	wq_core_unregister_oob_wakeup_host(&wq_pcie->core);
#endif

	wq_pcie_ce_stop(wq_pcie);
	wq_pcie_ce_cleanup(wq_pcie);
	wq_pcie_ce_deinit(wq_pcie);

	wq_pcie_deinit_irq(wq_pcie);
	wq_pcie_shutdown_target(wq_pcie);
	wq_pcie_release(wq_pcie);
	pci_set_drvdata(pdev, NULL);

	wq_core_destroy(&wq_pcie->core);

	LEAVE();
	return;
}

static void wq_pcie_shutdown(struct pci_dev *pdev)
{
	struct wq_pcie *wq_pcie = pci_get_drvdata(pdev);

	ENTER();
	if (!wq_pcie) {
		WQ_DBG(DM_TRBUS, DL_INF, "PCIE device removed from slot\n");
		LEAVE();
		return;
	}

	(void)wq_wlan_shutdown(&wq_pcie->core);

	wq_dev_restart(&wq_pcie->core);

	/* Cancel ce hrtimers. */
#ifndef CONFIG_PCIE_UNIT_TEST
	wq_pcie_ce_task_stop(wq_pcie);
#endif

	WQ_DBG(DM_TRBUS, DL_INF, "PCIE device restart...\n");

	LEAVE();
	return;
}

static __maybe_unused int wq_pcie_entry_D3(struct pci_dev *pdev)
{
	int ret;
	ENTER();

	pci_save_state(pdev);
	ret = pci_enable_wake(pdev, PCI_D3hot, 1);
	// ret = pci_enable_wake(pdev, PCI_D0, 1);
	WQ_DBG(DM_GENERIC, DL_INF, "pci_enable_wake ,ret = %d\n", ret);

	// WQ_DBG(DM_GENERIC, DL_INF, "pci_dev->dev->power.can_wakeup = %d\n", pdev->dev.power.can_wakeup);
	// WQ_DBG(DM_GENERIC, DL_INF, "pci_dev->dev->power.wakeup = %d\n", pdev->dev.power.wakeup);

	// /* Disable IO/bus master/irq router */
	pci_disable_device(pdev);

	ret = pci_set_power_state(pdev, PCI_D3hot);
	WQ_DBG(DM_GENERIC, DL_INF, "pci_set_power_state, ret = %d\n", ret);

	return ret;
}

static int wq_pcie_entry_D1(struct pci_dev *pdev)
{
	int ret;
	ENTER();

	pci_save_state(pdev);
	ret = pci_enable_wake(pdev, PCI_D1, 1);
	WQ_DBG(DM_GENERIC, DL_ERR, "pci_enable_wake ,ret = %d\n", ret);

	// /* Disable IO/bus master/irq router */
	pci_disable_device(pdev);

	ret = pci_set_power_state(pdev, PCI_D1);
	WQ_DBG(DM_GENERIC, DL_ERR, "pci_set_power_state, ret = %d\n", ret);

	return ret;
}

static int wq_pcie_entry_D0(struct pci_dev *pdev)
{
	int ret = 0;
	ENTER();

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	if (pci_enable_device(pdev)) {
		WQ_DBG(DM_GENERIC, DL_ERR, "%s: fail \n", __FUNCTION__);
	}

	return ret;
}

#define RUNTIME_PM_SYNC_THRESH 50
static __maybe_unused void
wq_pcie_sync_with_target(struct wq_pcie *wq_pcie,
			 enum wifi_ahb_scratch1 sync_bit)
{
	u32 sync_val, count = 0;
	do {
		BUG_ON(count++ > RUNTIME_PM_SYNC_THRESH);
		sync_val = wq_pcie_read32(wq_pcie, WIFI_SCRATCH1_ADDR);
		udelay(100);
	} while (!(sync_val & (1 << sync_bit)));
	sync_val &= ~(1 << sync_bit);
	wq_pcie_write32(wq_pcie, WIFI_SCRATCH1_ADDR, sync_val);
}

static void wq_pcie_write_and_sync_with_target(struct wq_pcie *wq_pcie,
					       enum pcie_pm_mbox_msg_id msg_id,
					       enum wifi_ahb_scratch1 sync_bit)
{
	u32 sync_val, count = 0;
	do {
		BUG_ON(count++ > RUNTIME_PM_SYNC_THRESH);
		wq_pcie_mailbox_send_msg(wq_pcie, PCIE_MBOX_PM_CTRL, msg_id);
		udelay(1000);
		sync_val = wq_pcie_read32(wq_pcie, WIFI_SCRATCH1_ADDR);
	} while (!(sync_val & (1 << sync_bit)));
	sync_val &= ~(1 << sync_bit);
	wq_pcie_write32(wq_pcie, WIFI_SCRATCH1_ADDR, sync_val);
}

static int wq_pcie_check_communication(struct wq_pcie *wq_pcie)
{
	u32 reg0, run_state;
	int ret = 0;
	ENTER();

	wq_pcie_write32(wq_pcie,
			HOST_W_AHB_REG_BASEADDR + CFG_WIFI_AHB_BAK_REG0_ADDR,
			0x12345678);
	reg0 = wq_pcie_read32(wq_pcie, HOST_W_AHB_REG_BASEADDR +
					       CFG_WIFI_AHB_BAK_REG0_ADDR);
	if (reg0 != 0x12345678) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "PCIe r/w check, should val: %x, read val :%x\n",
		       0x12345678, reg0);
		return -EIO;
	} else {
		wq_pcie_write32(wq_pcie,
				HOST_W_AHB_REG_BASEADDR +
					CFG_WIFI_AHB_BAK_REG0_ADDR,
				0);
	}

	ret = wq_core_get_sys_state(&wq_pcie->core, &run_state);
	if (ret < 0) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "%s get sys state fail!, ret = %d, state: %d\n",
		       __FUNCTION__, ret, run_state);
	}
	WQ_DBG(DM_GENERIC, DL_ERR, "%s get sys state: %d\n", __FUNCTION__,
	       run_state);

	return ret;
}

static __maybe_unused int wq_pcie_pm_suspend(struct device *dev)
{
	struct wq_pcie *wq_pcie = dev_get_drvdata(dev);
	struct pci_dev *pdev = to_pci_dev(wq_pcie->core.dev);
	struct rwnx_hw *rwnx_hw = wiphy_priv(wq_pcie->core.hw->wiphy);
	struct rwnx_vif *vif;
	int ret = 0;
    u32 ack_timeout = 0x00;
	u32 sync_val;
	u32 data;

	ENTER();
	WQ_DBG(DM_TRBUS, DL_ERR, "---->wq_pcie_pm_suspend\n");

	wq_core_deep_suspend_set(&wq_pcie->core, 1);

	// Only sta mode supports suspend
	list_for_each_entry (vif, &rwnx_hw->vifs, list) {
		if (RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_STATION && vif->up) {
			WQ_DBG(DM_TRBUS, DL_WRN,
			       "not support mode(%d), failed to suspend\n",
			       RWNX_VIF_TYPE(vif));
			wq_core_deep_suspend_set(&wq_pcie->core, 0);
			return -ENOTSUPP;
		}
	}

	// If runtime suspend, recovery to active
	if (!pm_runtime_active(dev)) {
		WQ_DBG(DM_TRBUS, DL_WRN,
		       "pcie rpm not active at system suspend\n");
		wq_pcie->pm_status = PCIE_RESUMING;
		wq_pcie_write_and_sync_with_target(
			wq_pcie, PCIE_PM_MSG_REQ_RESUME, SCRATCH_PCIE_ACK_BIT);
		pm_runtime_set_active(dev);
		pm_runtime_mark_last_busy(dev);
		wq_pcie->pm_status = PCIE_ACTIVE;
	}

	ret = wq_wlan_suspend(&wq_pcie->core);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_WRN, "failed to suspend wlan: %d\n", ret);
		wq_core_deep_suspend_set(&wq_pcie->core, 0);
		return ret;
	}

	wq_core_sys_suspend(&wq_pcie->core);

	wq_pcie_write32(wq_pcie, MAILBOX2_BASEADDR + CFG_MSG_WDATA_ADDR,
			PCIE_PM_MSG_REQ_SYS_SUSPEND);

	/* clear wow wakeup flag */
	data = wq_pcie_read32(wq_pcie, HOST_W_AHB_REG_BASEADDR + 0xD8);
	data &= (~0x100); /* 8bit */
	wq_pcie_write32(wq_pcie, HOST_W_AHB_REG_BASEADDR + 0xD8, data);

	// ask fw goto sys suspend
	wq_pcie->pm_status = PCIE_SYS_SUSPENDING;
	do {
		sync_val = wq_pcie_read32(wq_pcie, WIFI_SCRATCH1_ADDR);
		udelay(100);
        ack_timeout++;
        if (ack_timeout > 10)
            break;
	} while (!(sync_val & (1 << SCRATCH_PCIE_ACK_BIT)));
    if (ack_timeout >= 10)
	    WQ_DBG(DM_TRBUS, DL_ERR, "pcie wifi scartch1 sync timeout...\n");
	sync_val &= ~(1 << SCRATCH_PCIE_ACK_BIT);
	sync_val |= (1 << SCRATCH_PCIE_NOT_REBOOT_BIT);
	wq_pcie_write32(wq_pcie, WIFI_SCRATCH1_ADDR, sync_val);
	wq_pcie->pm_status = PCIE_SYS_SUSPENDED;

	wq_pcie_disable_aspm(pdev);
	wq_pcie_deinit_irq(wq_pcie);
	wq_pcie_entry_D1(pdev);
	// wq_pcie_entry_D3(pdev);
	WQ_DBG(DM_TRBUS, DL_ERR, "<----wq_pcie_pm_suspend\n");
	LEAVE();
	return ret;
}

static __maybe_unused int wq_pcie_pm_resume(struct device *dev)
{
	struct wq_pcie *wq_pcie = dev_get_drvdata(dev);
	struct pci_dev *pdev = to_pci_dev(wq_pcie->core.dev);
	int ret = 0;
	u32 sync_val, loop_cnt = 0;
	u32 data;

	ENTER();
	WQ_DBG(DM_GENERIC, DL_WRN, "---->wq_pcie_pm_resume\n");

	wq_pcie_entry_D0(pdev);
	do {
		sync_val = wq_pcie_read32(wq_pcie, WIFI_SCRATCH1_ADDR);
		if ((sync_val != 0xFFFFFFFF) &&
		    (!(sync_val & BIT(SCRATCH_PCIE_IN_RESET_BIT)))) {
			break;
		}
		udelay(100);
	} while (loop_cnt++ < 10000);

	if (loop_cnt == 10001) {
		WQ_DBG(DM_GENERIC, DL_ERR,
		       "wait fw prest restore complete timeout!!!!!! sync_val : %x. \n",
		       sync_val);
		goto failed;
	}

	// TODO: wq_pcie_enable_aspm(dev);
	wq_pcie_cfg_inbond(pdev);
	ret = wq_pcie_init_irq(wq_pcie);
	if (ret) {
		WQ_DBG(DM_GENERIC, DL_ERR, "Reinit pcie failed!\n");
		goto failed;
	}

	wq_pcie->pm_status = PCIE_RESUMING;
	wq_pcie_write_and_sync_with_target(wq_pcie, PCIE_PM_MSG_REQ_RESUME,
					   SCRATCH_PCIE_ACK_BIT);
	wq_pcie->pm_status = PCIE_ACTIVE;

	/* set wow wakeup flag */
	data = wq_pcie_read32(wq_pcie, HOST_W_AHB_REG_BASEADDR + 0xD8);
	data |= 0x100; /* 8bit */
	wq_pcie_write32(wq_pcie, HOST_W_AHB_REG_BASEADDR + 0xD8, data);

	pm_runtime_set_active(dev);

	wq_core_sys_resume(&wq_pcie->core);

	ret = wq_pcie_check_communication(wq_pcie);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_WRN,
		       "PCIe communication check failed!: %d\n", ret);
		goto failed;
	}

	// set pcie resume bit before wlan resume
	wq_core_deep_suspend_set(&wq_pcie->core, 0);

	ret = wq_wlan_resume(&wq_pcie->core);
	if (ret) {
		WQ_DBG(DM_TRBUS, DL_WRN, "failed to resume wlan: %d\n", ret);
		goto failed;
	}

failed:
	LEAVE();
	return ret;
}

#ifdef CONFIG_PM
static __maybe_unused int wq_pcie_runtime_suspend(struct device *dev)
{
	int ret = 0;
	struct wq_pcie *wq_pcie = dev_get_drvdata(dev);
	struct pci_dev *pdev = to_pci_dev(dev);
	WQ_DBG(DM_TRBUS, DL_WRN, "runtime suspend enter\n");

	if (!htc_pending_req_empty(&wq_pcie->core)) {
		WQ_DBG(DM_TRBUS, DL_ERR, "%s: pending req not empty\n",
		       __func__);
		ret = -EBUSY;
	} else {
		wq_pcie->pm_status = PCIE_RPM_SUSPENDING;
		pci_save_state(pdev);
		wq_pcie_mailbox_send_msg(wq_pcie, PCIE_MBOX_PM_CTRL,
					 PCIE_PM_MSG_REQ_RPM_SUSPEND);
		wq_pcie_sync_with_target(wq_pcie, SCRATCH_PCIE_ACK_BIT);
		wq_pcie->pm_status = PCIE_RPM_SUSPENDED;
		WQ_DBG(DM_TRBUS, DL_WRN, "runtime suspend end\n");
	}

	return ret;
}

static __maybe_unused int wq_pcie_runtime_resume(struct device *dev)
{
	int ret = 0;
	struct wq_pcie *wq_pcie = dev_get_drvdata(dev);
	struct pci_dev *pdev = to_pci_dev(dev);
	WQ_DBG(DM_TRBUS, DL_WRN, "runtime resume enter\n");
	wq_pcie->pm_status = PCIE_RESUMING;
	pci_restore_state(pdev);
	wq_pcie_write_and_sync_with_target(wq_pcie, PCIE_PM_MSG_REQ_RESUME,
					   SCRATCH_PCIE_ACK_BIT);
	wq_pcie->pm_status = PCIE_ACTIVE;
	pm_runtime_mark_last_busy(dev);
	htc_retrigger_tx_task(&wq_pcie->core);
	WQ_DBG(DM_TRBUS, DL_WRN, "runtime resume end\n");
	return ret;
}

static const struct dev_pm_ops wq_pcie_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(wq_pcie_pm_suspend, wq_pcie_pm_resume)
#ifdef CONFIG_WQ_WLAN_PM
		SET_RUNTIME_PM_OPS(wq_pcie_runtime_suspend,
				   wq_pcie_runtime_resume, NULL)
#endif
};
#endif

static const struct pci_device_id wq_pcie_id_table[] = {
	{
		WUQI_PCIE_VENDOR_ID,
		WUQI_PCIE_DEVICE_ID_QFN,
		PCI_ANY_ID,
		PCI_ANY_ID,
		0,
		0,
		WQ_WPHY_PF_QFN_PCIE,
	},
	{
		WUQI_PCIE_VENDOR_ID,
		WUQI_PCIE_DEVICE_ID_BGA,
		PCI_ANY_ID,
		PCI_ANY_ID,
		0,
		0,
		WQ_WPHY_PF_BGA_PCIE,
	},
	{},
};

MODULE_DEVICE_TABLE(pci, wq_pcie_id_table);

static struct pci_driver wq_pcie_driver = {
	.name = "wq_pcie",
	.id_table = wq_pcie_id_table,
	.probe = wq_pcie_probe,
	.remove = wq_pcie_remove,
	.shutdown = wq_pcie_shutdown,
#ifdef CONFIG_PM
	.driver.pm = &wq_pcie_pm_ops,
#endif
};

int __init wq_pcie_init(void)
{
	int ret;

	wq_module_init();

	ret = pci_register_driver(&wq_pcie_driver);
	if (ret)
		WQ_DBG(DM_TRBUS, DL_INF, "PCIE Driver Registration Failed\n");

	return ret;
}

void __exit wq_pcie_exit(void)
{
	pci_unregister_driver(&wq_pcie_driver);
	wq_module_exit();
}

#ifndef WQ_WLAN_ALL_IN_ONE
module_init(wq_pcie_init);
module_exit(wq_pcie_exit);
#endif

MODULE_AUTHOR("WuQi Technologies");
MODULE_DESCRIPTION("Driver support for WuQi 802.11ax WLAN PCIe devices");
MODULE_LICENSE("Dual BSD/GPL");
#ifdef MODULE_IMPORT_NS
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
#endif

u8 wq_pcie_get_link_speed(struct pci_dev *pdev)
{
	u16 link_sta;
	int ret;
	u8 speed;

	ret = pcie_capability_read_word(pdev, PCI_EXP_LNKSTA, &link_sta);
	if (ret < 0) {
		printk(KERN_ERR "Failed to read Link Status register\n");
		return 0;
	}

	speed = (u8)(link_sta & PCI_EXP_LNKSTA_CLS);
	printk(KERN_INFO "PCIe device link speed: %d\n", speed);

	return speed;
}
