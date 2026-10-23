#ifndef WQ_FW_WIFI_PHY_API_H_
#define WQ_FW_WIFI_PHY_API_H_

#include "fw_api/wifi/api.h"

/// Maximum number of words in the configuration buffer
#define PHY_CFG_BUF_SIZE     16

/// Structure containing the parameters of the PHY configuration
struct phy_cfg_tag
{
	/// Buffer containing the parameters specific for the PHY used
	u8 cali_mode;
	u8 quick_st;
	u8 spatial_stream_mode;
	u8 wifi01_switch_en;
	u32 parameters[PHY_CFG_BUF_SIZE - 1];
};

#endif /* WQ_FW_WIFI_PHY_API_H_ */
