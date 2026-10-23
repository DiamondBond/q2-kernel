#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/mtd/partitions.h>
#include "../spinand.h"
#include "../ingenic_sfc_common.h"
#include "nand_common.h"

#define TSETUP		5
#define THOLD		5
#define TSHSL_R		20
#define TSHSL_W		20

#define TRD		450
#define TPP		800
#define TBE		10

static struct ingenic_sfcnand_device *wodposit_nand;
static struct ingenic_sfcnand_base_param wodposit_param[] = {
	[0] = {
		/* WPS3NS01W */
		.pagesize = 2048,
		.blocksize = 2048 * 64,
		.oobsize = 64,
		.flashsize = 2048 * 64 * 1024,

		.tSETUP  = TSETUP,
		.tHOLD   = THOLD,
		.tSHSL_R = TSHSL_R,
		.tSHSL_W = TSHSL_W,

		.tRD = TRD,
		.tPP = TPP,
		.tBE = TBE,

		.plane_select = 0,
		.ecc_max = 0x8,
		.need_quad = 1,
	},
	[1] = {
		/* WPS3NS02W */
		.pagesize = 2048,
		.blocksize = 2048 * 64,
		.oobsize = 64,
		.flashsize = 2048 * 64 * 2048,

		.tSETUP  = TSETUP,
		.tHOLD   = THOLD,
		.tSHSL_R = TSHSL_R,
		.tSHSL_W = TSHSL_W,

		.tRD = TRD,
		.tPP = TPP,
		.tBE = TBE,

		.plane_select = 0,
		.ecc_max = 0x8,
		.need_quad = 1,
	},
	[2] = {
		/* WPS3NS04W */
		.pagesize = 4096,
		.blocksize = 4096 * 64,
		.oobsize = 128,
		.flashsize = 4096 * 64 * 2048,

		.tSETUP  = TSETUP,
		.tHOLD   = THOLD,
		.tSHSL_R = TSHSL_R,
		.tSHSL_W = TSHSL_W,

		.tRD = TRD,
		.tPP = TPP,
		.tBE = TBE,

		.plane_select = 0,
		.ecc_max = 0x8,
		.need_quad = 1,
	},
};

static struct device_id_struct device_id[] = {
	DEVICE_ID_STRUCT(0xA081, "WPS3NS01W", &wodposit_param[0]),
	DEVICE_ID_STRUCT(0xA082, "WPS3NS02W", &wodposit_param[1]),
	DEVICE_ID_STRUCT(0xA083, "WPS3NS04W", &wodposit_param[2]),
};
static cdt_params_t *wodposit_get_cdt_params(struct sfc_flash *flash, uint16_t device_id)
{
	CDT_PARAMS_INIT(wodposit_nand->cdt_params);
	switch(device_id) {
		case 0xA081:
		case 0xA082:
		case 0xA083:
			break;
		default:
			dev_err(flash->dev,"device_id err, please check your  device id: device_id = 0x%02x\n", device_id);
			return NULL;
	}
	return &wodposit_nand->cdt_params;
}

static inline int deal_ecc_status(struct sfc_flash *flash, uint16_t device_id, uint8_t ecc_status)
{
	switch(device_id) {
		case 0xA081:
		case 0xA082:
		case 0xA083:
			switch((ecc_status >> 4) & 0x3) {
				case 0x0:
					return 0;
				case 0x1:
					return 4;
				case 0x2:
				case 0x3:
					return -EBADMSG;
				default:
					break;
			}
			break;
		default:
			dev_warn(flash->dev,"device_id err, it maybe don`t support this device, check your device id: device_id = 0x%02x\n", device_id);
			break;
	}
	return -EINVAL;
}

static int __init wodposit_nand_init(void) {

	wodposit_nand = kzalloc(sizeof(*wodposit_nand), GFP_KERNEL);
	if(!wodposit_nand) {
		pr_err("alloc wodposit_nand struct fail\n");
		return -ENOMEM;
	}

	wodposit_nand->id_manufactory = 0xA5;
	wodposit_nand->id_device_list = device_id;
	wodposit_nand->id_device_count = ARRAY_SIZE(wodposit_param);

	wodposit_nand->ops.get_cdt_params = wodposit_get_cdt_params;
	wodposit_nand->ops.deal_ecc_status = deal_ecc_status;
	/* use private get feature interface, please define it in this document */
	wodposit_nand->ops.get_feature = NULL;

	return ingenic_sfcnand_register(wodposit_nand);
}

fs_initcall(wodposit_nand_init);
