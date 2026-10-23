#ifndef WQ_FW_HIF_PCIE_API_H_
#define WQ_FW_HIF_PCIE_API_H_

#include "fw_api/non_wifi/hif/api.h"

enum wq_pcie_ce_ch {
	/*
	 * used by BMI
	 */
	WQ_PCIE_CE_CH_BMI_BASE = 0,

	WQ_PCIE_CE_CH_BMI_TX = WQ_PCIE_CE_CH_BMI_BASE,
	WQ_PCIE_CE_CH_BMI_RX,

	WQ_PCIE_CE_CH_BMI_LAST,
	WQ_PCIE_CE_CH_BMI_NUM = WQ_PCIE_CE_CH_BMI_LAST - WQ_PCIE_CE_CH_BMI_BASE,

	/*
	 * used by Wi-Fi
	 */
	WQ_PCIE_CE_CH_WIFI_BASE = WQ_PCIE_CE_CH_BMI_LAST,

	WQ_PCIE_CE_CH_CMD_TX = WQ_PCIE_CE_CH_WIFI_BASE, /* +0 host->target command */
	WQ_PCIE_CE_CH_EVT_RX, /* +1 target->host event */
	WQ_PCIE_CE_CH_PKT_TX, /* +2 host->target low latency tx packet(desc + address) */
	WQ_PCIE_CE_CH_TXD_RX, /* +3 target->host low latency txdone pkt DMA addr */
	WQ_PCIE_CE_CH_PKT_RX, /* +4 target->host rx packet or txdone report(desc + address) */
	WQ_PCIE_CE_CH_RAW_TX, /* +5 host->target raw tx packet (desc + packet payload) */
	WQ_PCIE_CE_CH_RAW_RX, /* +6 target->host raw rx packet (desc + packet payload) */
	WQ_PCIE_CE_CH_LOG_RX, /* +7 target->host log, pktlog(TBD, at raw rx or this channel) */
	WQ_PCIE_CE_CH_MEMCPY, /* +8 consider reserved for memcpy usage. */

	WQ_PCIE_CE_CH_WIFI_LAST,
	WQ_PCIE_CE_CH_WIFI_NUM = WQ_PCIE_CE_CH_WIFI_LAST - WQ_PCIE_CE_CH_WIFI_BASE,

	/*
	 * used by Diagnose
	 */
	WQ_PCIE_CE_CH_DIAG_BASE = WQ_PCIE_CE_CH_WIFI_LAST,

	WQ_PCIE_CE_CH_FW_LOG = WQ_PCIE_CE_CH_DIAG_BASE, /* FW log */
	WQ_PCIE_CE_CH_DIAG_LAST,
	WQ_PCIE_CE_CH_DIAG_NUM = WQ_PCIE_CE_CH_DIAG_LAST - WQ_PCIE_CE_CH_DIAG_BASE,

	WQ_PCIE_CE_CH_LAST = WQ_PCIE_CE_CH_DIAG_LAST,
};

#endif /* WQ_FW_HIF_PCIE_API_H_ */
