#ifndef WQ_FW_WIFI_MAC_PKTDUMP_H_
#define WQ_FW_WIFI_MAC_PKTDUMP_H_

#define PKT_COPY_LEN 64
#define PKT_RXVEC_LEN 4
#define PKTDUMP_COUNT 10
#define PHY_ADC_DUMP_LEN 1024 * 32 + 4

struct time_stamp {
	int tm_sec;
	/* the number of minutes after the hour, in the range 0 to 59*/
	int tm_min;
	/* the number of hours past midnight, in the range 0 to 23 */
	int tm_hour;
};

typedef struct _wifi_dbg_pktdump {
	uint32_t start;
	uint32_t mac_ts;
	uint16_t len;

	uint8_t rate : 8;
	uint8_t is_ampdu : 1;
	uint8_t dir : 1;
	uint8_t frame_cnt : 6;

	uint8_t cs : 4;
	uint16_t cindex : 6;
	uint32_t rssi : 8;
	uint32_t mac_id : 2;
	uint32_t reserved2 : 4;
	uint32_t sn : 8;

	uint32_t proto_ts;
	uint32_t driver_ts_sec : 8;
	uint32_t driver_ts_min : 8;
	uint32_t driver_ts_hour : 16;
	uint32_t hal_ts;
	uint32_t rx_vec[PKT_RXVEC_LEN];
	uint32_t tx_status;
	uint8_t pkt_data[PKT_COPY_LEN];
	struct time_stamp tm_stamp;
	uint32_t reserved;
} __packed WIFI_DBG_PKTDUMP;

#endif
