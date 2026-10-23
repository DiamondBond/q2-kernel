#ifndef WQ_FW_WIFI_MAC_WOWLAN_API_H_
#define WQ_FW_WIFI_MAC_WOWLAN_API_H_

#include "fw_api/wifi/mac/api.h"


#define WOW_GLOBAL_ENABLE_BIT          0x1
#define SUSPEND_GLOBAL_ENABLE_BIT      WOW_GLOBAL_ENABLE_BIT
#define WOW_GTK_OFFLOAD_ENABLE_BIT              0x10
#define WOW_ARP_OFFLOAD_ENABLE_BIT              0x20
#define WOW_NS_OFFLOAD_ENABLE_BIT               0x40

#define WOW_WAKEUP_4WAY_HANDSHAKE_BIT           0x100
#define WOW_WAKEUP_802_1X_BIT                   0x200
#define WOW_WAKEUP_PATTERN_BIT                  0x400
#define WOW_WAKEUP_CONNECTION_LOST_BIT          0x800
#define WOW_WAKEUP_GTK_REKEY_FAILURE            0x1000

enum wow_req_type {
    WOW_SUSPEND,
    WOW_RESUME,
};

/// Structure containing the parameters of each pattern.
struct me_wow_pattern
{
	u32 length;
	u32 offset;
	u32 id;
	/* u8 *mask; The size of the mask is determined by the length */
	/* u8 *pattern; The size of the pattern is determined by the length */
};


/// Structure containing the parameters of the @ref ME_SET_WOWLAN_REQ message.
struct me_set_wowlan_req_v1
{
	u32 wakeup_type;
	struct me_wow_pattern wow_pattern[0];
};

/// Structure containing the parameters of the @ref ME_SET_WOWLAN_CFM message.
struct me_set_wowlan_cfm
{
    /// Status of the operation (different from 0 if unsuccessful)
    u8 status;
    /// the following parameters used by host resume cfm
    u32 wakeup_reason;
};

/// Structure containing the parameters of the @ref ME_WOW_RESUME_IND message.
struct me_wow_resume_ind
{
    u8 vif_idx;
    u16 frame_len;
    u8 frame[0];
};

#endif /* WQ_FW_WIFI_MAC_WOWLAN_API_H_ */
