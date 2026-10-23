#define WQ_LOG_DM DM_TRBUS

#include <linux/string.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/swab.h>
#include <linux/types.h>

#include "core.h"
#include "fw_log.h"

#include "wq_fw.h"
#include "wq_log.h"

#include "pcie.h"
#include "bmi_core.h"

extern void dtop_deinit(struct wq_core *core);
extern void dtop_init(struct wq_core *core);

static int pci_dtop_send_bulk(struct wq_pcie *wq_pcie, u8 *data, u32 size,
			      u32 timeout)
{
	(void)wq_pcie;
	(void)data;
	(void)size;
	(void)timeout;
	WQ_DBG(DM_TRBUS, DL_INF, "%s: wait to do \n", __func__);

	return 0;
}

int wq_pci_dtop_send(struct wq_core *core, u8 *data, u32 size, u32 timeout)
{
	int ret;
	struct wq_pcie *wq_pcie = container_of(core, struct wq_pcie, core);

	if (data == NULL || wq_pcie == NULL) {
		return -EINVAL;
	}

	ret = pci_dtop_send_bulk(wq_pcie, data, size, timeout);

	return ret;
}

void wq_pcie_app_deinit(struct wq_pcie *wq_pcie)
{
	wq_fw_log_proc_deinit(&wq_pcie->core);
#ifdef CONFIG_WQ_DTOP
	dtop_deinit(&wq_pcie->core);
#endif
}

int wq_pcie_app_init(struct wq_pcie *wq_pcie)
{
	struct wq_core *core = &wq_pcie->core;

	WQ_DBG(DM_TRBUS, DL_INF, "[%s] init\n", __func__);

	wq_fw_log_proc_init(&wq_pcie->core);

	/** Initialize the wait queue */
	init_waitqueue_head(&core->driver.main_waitQ);
	skb_queue_head_init(&core->driver.main_rx_Q);

#ifdef CONFIG_WQ_DTOP
	dtop_init(core);
#endif

	return 0;
}
