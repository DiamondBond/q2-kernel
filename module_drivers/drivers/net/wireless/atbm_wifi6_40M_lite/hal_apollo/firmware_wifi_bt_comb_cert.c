#include "fwio.h"


#ifdef ATBM_ADD_CRERT_FIRMWARE


#include "apollo.h"
#include "hwio.h"
#ifdef CONFIG_USE_FW_H

#ifdef USB_BUS
#include "firmware_usb_wifi_bt_comb_cert.h"
#endif
#ifdef SDIO_BUS
#include "firmware_sdio_wifi_bt_comb_cert.h"
#endif
#ifdef SPI_BUS
#include "firmware_spi_wifi_bt_comb_cert.h"
#endif

//define this fw_sram is weak,if fw_sram define in firmware**.h £¬ this define will be ignore

__attribute__((weak)) u8 fw_sram[0];

//TODO: Currently no requirement for cert, keep as-is at present
int atbm_load_wifi_bt_cert_firmware(struct firmware_altobeam *fw_altobeam)
{
	struct firmware_headr* hdr = (struct firmware_headr*)firmware_headr;
	fw_altobeam->hdr.iccm_len = sizeof(fw_code);
	fw_altobeam->hdr.dccm_len = sizeof(fw_data);
//#ifdef CONFIG_ATBM_BLE_CODE_SRAM
	fw_altobeam->hdr.sram_len = sizeof(fw_sram);
	if (hdr->flags == ALTOBEAM_WIFI_HDR_FLAG_V2)
		fw_altobeam->hdr.sram_addr = hdr->sram_addr;
	else
		fw_altobeam->hdr.sram_addr = DOWNLOAD_BLE_SRAM_ADDR;
	printk("fwhdr->flags %x sram_addr %x\n", hdr->flags, fw_altobeam->hdr.sram_addr);
	fw_altobeam->fw_sram = (u8 *)&fw_sram[0];
//#endif
	fw_altobeam->fw_iccm = (u8 *)&fw_code[0];
	fw_altobeam->fw_dccm = (u8 *)&fw_data[0];

	return 0;
}
#endif
#else

int atbm_load_wifi_bt_cert_firmware(struct firmware_altobeam *fw_altobeam)
{
	return 0;
}


#endif //defined ATBM_ADD_CRERT_FIRMWARE


