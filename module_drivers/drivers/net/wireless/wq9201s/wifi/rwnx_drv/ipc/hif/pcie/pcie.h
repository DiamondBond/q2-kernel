/** @file pcie.h
 *
 *  @brief This file contains definitions for PCIE interface.
 *  driver.
 *
 * Copyright (C) 2016-2023, WuQi Ltd.
 *
 *  This software file (the "File") is distributed by Marvell International
 *  Ltd. under the terms of the GNU General Public License Version 2, June 1991
 *  (the "License").  You may use, redistribute and/or modify this File in
 *  accordance with the terms and conditions of the License, a copy of which
 *  is available by writing to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 *  worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 *  THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 *  ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 *  this warranty disclaimer.
 */

/********************************************************
Change log:
	02/09/2023: Initial creation
********************************************************/

#ifndef _WQ_PCIE_H
#define _WQ_PCIE_H

#include <linux/interrupt.h>

#include "fw_api/non_wifi/hif/pcie/api.h"

#include "hif_api.h"
#include "ce.h"

#ifdef CONFIG_PCIE_UNIT_TEST
#define TX_TEST_LEN 2048
#define TX_TEST_MAX_SKB_NUM 32
#endif

#undef RAE_TEST

#define PCIE_NUM_MSIX_VECTORS 0 /* unsupported */

#define __WQ_PCIE_CE_INITED(wq_pcie, chn, dir)                                 \
	(!!((wq_pcie)->ce_init_bitmap[dir] & (1u << (chn))))

#define WQ_PCIE_CE_SRC_INITED(wq_pcie, chn)                                    \
	__WQ_PCIE_CE_INITED((wq_pcie), (chn), WQ_CE_CHN_SRC)

#define WQ_PCIE_CE_DST_INITED(wq_pcie, chn)                                    \
	__WQ_PCIE_CE_INITED((wq_pcie), (chn), WQ_CE_CHN_DST)

#define WQ_PCIE_CE_INITED(wq_pcie, chn)                                        \
	(WQ_PCIE_CE_SRC_INITED((wq_pcie), chn) ||                              \
	 WQ_PCIE_CE_DST_INITED((wq_pcie), chn))

/** PCIe RC <-> EP FW download MSG Sequence */
enum pcie_fw_dnld_msg {
	PCIE_MSG_R2E_BIN_READY = 0x00000010,
	PCIE_MSG_R2E_START_XSYS = 0x00000011,
	PCIE_MSG_R2E_GET_ROM_VER = 0x00000012,

	PCIE_MSG_E2R_BIN_INFO_INVALID = 0x00000018,
	PCIE_MSG_E2R_BIN_INFO_OK,
	PCIE_MSG_E2R_BIN_INFO_ERR,
	PCIE_MSG_E2R_BIN_INFO_CRC_ERR,
	PCIE_MSG_E2R_BIN_WILL_JUMP,
	PCIE_MSG_E2R_REQ_WIFI_BIN,

	PCIE_MSG_E2R_ROM_VER_IS_RDY = 0x0000001E,

	PCIE_MSG_MAX,
};

enum pcie_mbox_id {
	PCIE_MBOX_0,
	PCIE_MBOX_1,
	PCIE_MBOX_PM_CTRL,
	PCIE_MBOX_3,
};

#if PCIE_NUM_MSIX_VECTORS
struct msix_context {
	/** pci_dev structure pointer */
	struct pci_dev *dev;
	/** message id related to msix vector */
	u16 msg_id;
};
#endif

enum pcie_intr_mode {
	PCIE_INTR_MODE_LEGACY = 0,
	PCIE_INTR_MODE_MSI,
	PCIE_INTR_MODE_MSIX,
	PCIE_INTR_MODE_MAX,
};

enum wifi_ahb_scratch1 {
	SCRATCH_PCIE_ACK_BIT,
	SCRATCH_PCIE_READY_BIT,
	SCRATCH_PCIE_IN_RESET_BIT,
	SCRATCH_PCIE_NOT_REBOOT_BIT,
	SCRATCH_PCIE_TEST_RUN_DIR_BIT,
};

enum pcie_pm_status {
	PCIE_ACTIVE,
	PCIE_RPM_SUSPENDING,
	PCIE_RPM_SUSPENDED,
	PCIE_SYS_SUSPENDING,
	PCIE_SYS_SUSPENDED,
	PCIE_RESUMING,
};

enum pcie_pm_mbox_msg_id {
	PCIE_PM_MSG_REQ_RPM_SUSPEND,
	PCIE_PM_MSG_REQ_SYS_SUSPEND,
	PCIE_PM_MSG_REQ_RESUME,
};

struct wq_pcie_ce_attr {
	u32 flags;
	u32 src_sz_max : 20;
	u32 src_depth : 14;
	u32 dst_depth : 14;
	u32 high_watermark : 13;
	u32 low_watermark : 13;
	u32 intr_ena : 22;
	/*
	 * memory alloc with dma map
	 */
	int (*buf_alloc)(struct wq_pcie *wq_pcie, u32 nbytes,
			 enum dma_data_direction dir, void **context,
			 dma_addr_t *phys_addr);
	/*
	 * memory free with dma unmap
	 */
	void (*buf_free)(struct wq_pcie *wq_pcie, dma_addr_t phys_addr,
			 u32 nbytes, enum dma_data_direction dir,
			 void *context);
	/*
	 * callback for curr_desc or watermark to call completed_next
	 */
	void (*desc_completed)(struct wq_pcie *wq_pcie, CE_CHN_UUID chn);
};

struct wq_pcie {
	struct wq_core core;

	/* HW resources */
	void __iomem *mem;
	size_t mem_len;

	spinlock_t intc_lock;
	enum pcie_intr_mode intr_mode;
	enum pcie_pm_status pm_status;

#if PCIE_NUM_MSIX_VECTORS
	struct {
		struct msix_entry entries[PCIE_NUM_MSIX_VECTORS];
		struct msix_context contexts[PCIE_NUM_MSIX_VECTORS];
	} msi;
#endif

	const struct wq_pcie_ce_attr *ce_attr_table;
	unsigned int ce_init_bitmap[2];

	wq_ce_attr_t ce_attrs[WQ_PCIE_CE_CH_LAST];
	struct ce_state ce_states[WQ_PCIE_CE_CH_LAST];

	struct completion bmi_recv_done;

#ifdef CONFIG_PCIE_UNIT_TEST
	struct completion test_ce_desc_done;
#endif

	//firmware resource
	const u8 *fw;
	u32 fw_len;
	u8 *fw_buf;
	dma_addr_t fw_buf_pa;
	bool wakeup_target_timeout;

#ifdef CONFIG_PCIE_UNIT_TEST
	u8 tx_cmd_done;

	struct sk_buff *tx_entry_skb;
	dma_addr_t tx_entry_skb_data_pa;

	struct sk_buff *tx_skb[TX_TEST_MAX_SKB_NUM];
	dma_addr_t tx_skb_data_pa[TX_TEST_MAX_SKB_NUM];

	struct sk_buff *tx_pld_skb[TX_TEST_MAX_SKB_NUM];
	dma_addr_t tx_pld_skb_data_pa[TX_TEST_MAX_SKB_NUM];
#endif

	/** ce tx/rx Task */
	struct tasklet_struct ce_txrx_tasklet;

	/* ce tx/rx timer to wait for short time(defalt: 1ms) to decrease ce interrupt count for performance improvement */
	struct hrtimer ce_tx_timer;
	struct hrtimer ce_rx_timer;

	/* txq ring tasklet and timer */
	struct tasklet_struct txqring_tasklet;
	struct hrtimer txqring_timer;

	/* ce dma tx/rx done status */
	volatile bool ce_dma_tx_done[WQ_PCIE_CE_CH_LAST];
	volatile bool ce_dma_rx_done[WQ_PCIE_CE_CH_LAST];

	/* self dev */
	struct pci_dev *pdev;
};

/** Function to read/write register */
static inline u32 wq_pcie_read32(struct wq_pcie *wq_pcie, u32 reg)
{
	BUG_ON(reg >= wq_pcie->mem_len);
	return ioread32(wq_pcie->mem + reg);
}

static inline void wq_pcie_write32(struct wq_pcie *wq_pcie, u32 reg, u32 data)
{
	BUG_ON(reg >= wq_pcie->mem_len);
	iowrite32(data, wq_pcie->mem + reg);
}

/* Functions in interface module */
int wq_map_memory(struct wq_pcie *wq_pcie, u8 *pbuf, dma_addr_t *pbuf_pa,
		  u32 size, u32 flag);
void wq_sync_memory_for_device(struct wq_pcie *wq_pcie, dma_addr_t pbuf_pa,
			       u32 size, u32 flag);
void wq_sync_memory_for_cpu(struct wq_pcie *wq_pcie, dma_addr_t pbuf_pa,
			    u32 size, u32 flag);
void wq_unmap_memory(struct wq_pcie *wq_pcie, dma_addr_t *buf_pa, u32 size,
		     u32 flag);
int wq_map_consistent(struct wq_pcie *wq_pcie, u8 **ppbuf, dma_addr_t *pbuf_pa,
		      u32 size);
void wq_unmap_consistent(struct wq_pcie *wq_pcie, u8 **pbuf, dma_addr_t *buf_pa,
			 u32 size);

int wq_pcie_notify_fw_dnld(struct wq_pcie *wq_pcie, dma_addr_t addr_pa,
			   u32 len);
int wq_pcie_notify_fw_use_msg(struct wq_pcie *wq_pcie,
			      enum pcie_fw_dnld_msg msg);

int wq_pcie_show_bus_info(struct wq_pcie *wq_pcie);
void wq_pcie_irq_mask_set(struct wq_pcie *wq_pcie, u8 group, u8 bit, int set);
bool wq_pcie_irq_mask_get(struct wq_pcie *wq_pcie, u8 group, u8 bit);

static inline void wq_pcie_irq_unmask(struct wq_pcie *wq_pcie, uint32_t vector)
{
	wq_pcie_irq_mask_set(wq_pcie, vector >> 5, vector & (BIT(5) - 1), 1);
}
static inline void wq_pcie_irq_mask(struct wq_pcie *wq_pcie, uint32_t vector)
{
	wq_pcie_irq_mask_set(wq_pcie, vector >> 5, vector & (BIT(5) - 1), 0);
}
static inline bool wq_pcie_irq_status_get(struct wq_pcie *wq_pcie, uint32_t vector)
{
	return wq_pcie_irq_mask_get(wq_pcie, vector >> 5, vector & (BIT(5) - 1));
}

#ifdef CONFIG_PCIE_UNIT_TEST
int wq_pcie_test_start(struct wq_pcie *wq_pcie);
void wq_pcie_test_stop(struct wq_pcie *wq_pcie);
int wq_ce_test_start(struct wq_pcie *wq_pcie);
void wq_ce_test_stop(struct wq_pcie *wq_pcie);
#else
static inline int wq_pcie_test_start(struct wq_pcie *wq_pcie)
{
	return 0;
}
static inline void wq_pcie_test_stop(struct wq_pcie *wq_pcie)
{
}
static inline int wq_ce_test_start(struct wq_pcie *wq_pcie)
{
	return 0;
}
static inline void wq_ce_test_stop(struct wq_pcie *wq_pcie)
{
}
#endif

int wq_dnld_msg_handle(struct wq_pcie *wq_pcie, u32 msg_type);

int wq_pcie_bmi_cmd_init(struct wq_pcie *wq_pcie);

int wq_pcie_bmi_cmd(struct wq_core *core, u8 cmd, const void *param, u16 p_size,
		    void *resp, u16 r_size, int timeout);

int wq_pcie_bmi_xfer(struct wq_core *core, enum wq_bmi_xfer_type type,
		     const u8 *data, int len, int timeout);

int wq_pci_bmi_exchange(struct wq_core *core, void *req, u32 req_len, void *rsp,
			u32 rsp_len_max, int timeout);

int wq_pcie_mailbox_send_msg(struct wq_pcie *wq_pcie, u32 mbox_id, u32 msg);

#ifdef CONFIG_WQ_DTOP
int wq_pcie_app_init(struct wq_pcie *wq_pcie);
void wq_pcie_app_deinit(struct wq_pcie *wq_pcie);
int wq_pci_dtop_send(struct wq_core *core, u8 *data, u32 size, u32 timeout);

#endif

int wq_pcie_ce_init(struct wq_pcie *wq_pcie,
		    const struct wq_pcie_ce_attr attr_table[CE_CHN_MAX]);

void wq_pcie_ce_stop(struct wq_pcie *wq_pcie);

void wq_pcie_ce_cleanup(struct wq_pcie *wq_pcie);

void wq_pcie_ce_deinit(struct wq_pcie *wq_pcie);

int wq_pcie_ce_skb_alloc(struct wq_pcie *wq_pcie, u32 nbytes,
			 enum dma_data_direction dir, void **context,
			 dma_addr_t *phys_addr);

void wq_pcie_ce_skb_free(struct wq_pcie *wq_pcie, dma_addr_t phys_addr,
			 u32 nbytes, enum dma_data_direction dir,
			 void *context);

u8 wq_pcie_get_link_speed(struct pci_dev *pdev);

#endif /* _WQ_PCIE_H_ */
