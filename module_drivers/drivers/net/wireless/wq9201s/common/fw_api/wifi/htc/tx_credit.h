#ifndef WQ_FW_WIFI_TX_CREDIT_API_H_
#define WQ_FW_WIFI_TX_CREDIT_API_H_

#include "fw_api/non_wifi/hif/api.h"

#include "fw_api/wifi/htc/api.h"

enum tx_credit_group {
	TX_CREDIT_GROUP_0 = 0,
	TX_CREDIT_GROUP_1,

	WQ_CREDIT_GROUP_NUM,			/* max 2 VIFs */

	TX_CREDIT_GROUP_DISABLED = 0xFF,
};

#define WQ_CREDIT_TYPE_NUM	WQ_QID_AC_MAX	/* for BK/BE/VI/VO */

#endif /* WQ_FW_WIFI_TX_CREDIT_API_H_ */
