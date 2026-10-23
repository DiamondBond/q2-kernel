#ifndef _WQ_FW_H
#define _WQ_FW_H

/* firmware header: */
#define WQ_FW_HEADER_IMG_GUARD 0xA4E49A17
#define WQ_FW_HEADER_LEN 64

#define WQ_VREQ_ID_GET_FW_DTOP_FLASH_INFO 0xCE //get dtop fw info
#define WQ_VREQ_ID_CONFIG_BOOT_TYPE 0xDD //boot type config
#define WQ_VREQ_ID_EXE_MP_TEST_CMD 0xF0 //mp test command

#define FW_DL_TIMEOUT 2000 /* msec */

typedef enum wq_fw_type_e {
	WQ_FW_BOOTROM = 0,
	WQ_FW_DTOP,
	WQ_FW_WIFI,
	WQ_FW_BT,
	WQ_FW_BOOT_CFG,
	WQ_FW_WIFI_PHY,
	WQ_FW_MAX
} WQ_FW_TYPE;

enum wq_fw_dl_reply_e {
	WQ_FW_DL_READY = 0,
	WQ_FW_DL_ING,
	WQ_FW_DL_REFUSE,
};

typedef struct fw_bin_header {
	u32 fw_image_guard;
	u8 fw_file_type;
	u8 reserved0[3];
	u32 vma;
	u32 lma;
	u32 version;
	u32 fw_bin_length;
	u32 fw_bin_crc;
	u8 reserved1[36];
} __attribute__((__packed__)) fw_bin_header_t;

typedef struct fw_bin_header_ver_detail {
	u32 major : 7;
	u32 minor : 5;
	u32 rever : 8;
	u32 build : 12;
} fw_bin_ver_t;
static void inline wq_fw_get_ver_detail(u32 version, fw_bin_ver_t *p_dver)
{
	p_dver->major = (version >> 25) & 0x7F;
	p_dver->minor = (version >> 20) & 0x1F;
	p_dver->rever = (version >> 12) & 0xFF;
	p_dver->build = (version >> 0) & 0xFFF;
}

struct wq_fw_dl_tag {
	u16 id; //index
	u16 checksum; //checksum
} __attribute__((__packed__));

struct wq_dev_rom_ver {
	u8 major;
	u8 minor;
	u8 build_hr;
	u8 build_min;
} __attribute__((__packed__));

struct fw_dnld {
	const u8 *fw;
	uint32_t fw_len;
	uint32_t dl_addr;
	uint32_t start_pc;
	uint32_t fw_crc;
};

int wq_dnld_init(struct wq_core *core);
void wq_dnld_deinit(struct wq_core *core);
#endif /* _WQ_FW_H */