#ifndef WQ_PACKET_TYPE_H
#define WQ_PACKET_TYPE_H

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68

#define DHCP_CHADDR_LENGTH 16
#define DHCP_SNAME_LENGTH 64
#define DHCP_FILE_LENGTH 128
#define DHCP_OPTIONS_LENGTH 312
#define DHCP_MAGIC_LENGTH 4

#define DHCPDISCOVER 1
#define DHCPOFFER 2
#define DHCPREQUEST 3
#define DHCPDECLINE 4
#define DHCPACK 5
#define DHCPNACK 6
#define DHCPRELEASE 7

#define DHCP_OPTION_MESSAGE_TYPE 53
#define DHCP_OPTION_HOST_NAME 12
#define DHCP_OPTION_BROADCAST_ADDRESS 28
#define DHCP_OPTION_REQUESTED_ADDRESS 50
#define DHCP_OPTION_LEASE_TIME 51
#define DHCP_OPTION_RENEWAL_TIME 58
#define DHCP_OPTION_REBINDING_TIME 59

typedef struct dhcp_packet {
	u_int8_t op; /* packet type */
	u_int8_t htype; /* type of hardware address for this machine (Ethernet, etc) */
	u_int8_t hlen; /* length of hardware address (of this machine) */
	u_int8_t hops; /* hops */
	u_int32_t xid; /* random transaction id number - chosen by this machine */
	u_int16_t secs; /* seconds used in timing */
	u_int16_t flags; /* flags */
	struct in_addr
		ciaddr; /* IP address of this machine (if we already have one) */
	struct in_addr
		yiaddr; /* IP address of this machine (offered by the DHCP server) */
	struct in_addr siaddr; /* IP address of DHCP server */
	struct in_addr giaddr; /* IP address of DHCP relay */
	unsigned char
		chaddr[DHCP_CHADDR_LENGTH]; /* hardware address of this machine */
	char sname[DHCP_SNAME_LENGTH]; /* name of DHCP server */
	char file[DHCP_FILE_LENGTH]; /* boot file name (used for diskless booting?) */
	char magic_cookie[DHCP_MAGIC_LENGTH];
	char options[0];
} dhcp_packet_t;

#define WPA_REPLAY_COUNTER_LEN 8
#define WPA_NONCE_LEN 32
#define WPA_KEY_RSC_LEN 8

enum { IEEE802_1X_TYPE_EAP_PACKET = 0,
       IEEE802_1X_TYPE_EAPOL_START = 1,
       IEEE802_1X_TYPE_EAPOL_LOGOFF = 2,
       IEEE802_1X_TYPE_EAPOL_KEY = 3,
       IEEE802_1X_TYPE_EAPOL_ENCAPSULATED_ASF_ALERT = 4,
       IEEE802_1X_TYPE_EAPOL_MKA = 5,
};

enum { EAPOL_KEY_TYPE_RC4 = 1,
       EAPOL_KEY_TYPE_RSN = 2,
       EAPOL_KEY_TYPE_WPA = 254 };

#define WPA_KEY_INFO_TYPE_MASK ((u16)(BIT(0) | BIT(1) | BIT(2)))
#define WPA_KEY_INFO_TYPE_AKM_DEFINED 0
#define WPA_KEY_INFO_TYPE_HMAC_MD5_RC4 BIT(0)
#define WPA_KEY_INFO_TYPE_HMAC_SHA1_AES BIT(1)
#define WPA_KEY_INFO_TYPE_AES_128_CMAC 3
#define WPA_KEY_INFO_KEY_TYPE BIT(3) /* 1 = Pairwise, 0 = Group key */
#define WPA_KEY_INFO_KEY_INDEX_MASK (BIT(4) | BIT(5))
#define WPA_KEY_INFO_KEY_INDEX_SHIFT 4
#define WPA_KEY_INFO_INSTALL BIT(6)
#define WPA_KEY_INFO_ACK BIT(7)
#define WPA_KEY_INFO_MIC BIT(8)
#define WPA_KEY_INFO_SECURE BIT(9)
#define WPA_KEY_INFO_ERROR BIT(10)
#define WPA_KEY_INFO_REQUEST BIT(11)
#define WPA_KEY_INFO_ENCR_KEY_DATA BIT(12)
#define WPA_KEY_INFO_SMK_MESSAGE BIT(13)

struct ieee802_1x_hdr {
	uint8_t version;
	uint8_t type;
	uint16_t length;
} __packed;

struct wpa_eapol_key {
	uint8_t type;
	uint8_t key_info[2]; /* big endian */
	uint8_t key_length[2]; /* big endian */
	uint8_t replay_counter[WPA_REPLAY_COUNTER_LEN];
	uint8_t key_nonce[WPA_NONCE_LEN];
	uint8_t key_iv[16];
	uint8_t key_rsc[WPA_KEY_RSC_LEN];
	uint8_t key_id[8];
} __packed;

enum { EAP_CODE_REQUEST = 1,
       EAP_CODE_RESPONSE = 2,
       EAP_CODE_SUCCESS = 3,
       EAP_CODE_FAILURE = 4,
       EAP_CODE_INITIATE = 5,
       EAP_CODE_FINISH = 6 };

typedef enum {
	EAP_TYPE_NONE = 0,
	EAP_TYPE_IDENTITY = 1,
	EAP_TYPE_NOTIFICATION = 2,
	EAP_TYPE_NAK = 3,
	EAP_TYPE_MD5 = 4,
	EAP_TYPE_OTP = 5,
	EAP_TYPE_GTC = 6,
	EAP_TYPE_TLS = 13,
	EAP_TYPE_LEAP = 17,
	EAP_TYPE_SIM = 18,
	EAP_TYPE_TTLS = 21,
	EAP_TYPE_AKA = 23,
	EAP_TYPE_PEAP = 25,
	EAP_TYPE_MSCHAPV2 = 26,
	EAP_TYPE_TLV = 33,
	EAP_TYPE_TNC = 38,
	EAP_TYPE_FAST = 43,
	EAP_TYPE_PAX = 46,
	EAP_TYPE_PSK = 47,
	EAP_TYPE_SAKE = 48,
	EAP_TYPE_IKEV2 = 49,
	EAP_TYPE_AKA_PRIME = 50,
	EAP_TYPE_GPSK = 51,
	EAP_TYPE_PWD = 52,
	EAP_TYPE_EKE = 53,
	EAP_TYPE_EXPANDED = 254
} EapType;

struct eap_hdr {
	uint8_t code;
	uint8_t identifier;
	uint16_t length;
} __packed;

enum wsc_op_code {
	WSC_UPnP = 0,
	WSC_Start = 0x01,
	WSC_ACK = 0x02,
	WSC_NACK = 0x03,
	WSC_MSG = 0x04,
	WSC_Done = 0x05,
	WSC_FRAG_ACK = 0x06
};

enum wps_msg_type {
	WPS_Beacon = 0x01,
	WPS_ProbeRequest = 0x02,
	WPS_ProbeResponse = 0x03,
	WPS_M1 = 0x04,
	WPS_M2 = 0x05,
	WPS_M2D = 0x06,
	WPS_M3 = 0x07,
	WPS_M4 = 0x08,
	WPS_M5 = 0x09,
	WPS_M6 = 0x0a,
	WPS_M7 = 0x0b,
	WPS_M8 = 0x0c,
	WPS_WSC_ACK = 0x0d,
	WPS_WSC_NACK = 0x0e,
	WPS_WSC_DONE = 0x0f
};

enum { EAP_VENDOR_IETF = 0,
       EAP_VENDOR_MICROSOFT = 0x000137,
       EAP_VENDOR_WFA = 0x00372A,
       EAP_VENDOR_HOSTAP = 39068,
       EAP_VENDOR_WFA_NEW = 40808 };

#define ATTR_MSG_TYPE 0x1022

static inline uint16_t WQ_GET_BE16(uint8_t *a)
{
	return (a[0] << 8) | a[1];
}
static inline uint32_t WQ_GET_BE24(uint8_t *a)
{
	return (a[0] << 16) | (a[1] << 8) | a[2];
}

static inline uint32_t WQ_GET_BE32(uint8_t *a)
{
	return ((uint32_t)a[0] << 24) | (a[1] << 16) | (a[2] << 8) | a[3];
}

#endif
