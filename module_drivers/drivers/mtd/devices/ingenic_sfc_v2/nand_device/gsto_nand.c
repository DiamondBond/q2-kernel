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
#define TSHSL_R	20
#define TSHSL_W	20

#define TRD		450
#define TPP		800
#define TBE		10

static struct ingenic_sfcnand_device *gsto_nand;
static struct ingenic_sfcnand_base_param gsto_param[] = {
	[0] = {
		/*GSS01GSAX1*/
		.pagesize  = 2048,
		.oobsize   = 64,
		.blocksize = 2048 * 64,
		.flashsize = 2048 * 64 * 1024,

		.tSETUP  = TSETUP,
		.tHOLD   = THOLD,
		.tSHSL_R = TSHSL_R,
		.tSHSL_W = TSHSL_W,

		.tRD = TRD,
		.tPP = TPP,
		.tBE = TBE,

		.plane_select = 0,
		.ecc_max      = 8,
		.need_quad    = 1,
	},
	[1] = {
		/*GSS02GSAX1*/
		.pagesize  = 2048,
		.oobsize   = 64,
		.blocksize = 2048 * 64,
		.flashsize = 2048 * 64 * 2048,

		.tSETUP    = TSETUP,
		.tHOLD     = THOLD,
		.tSHSL_R   = TSHSL_R,
		.tSHSL_W   = TSHSL_W,

		.tRD = TRD,
		.tPP = TPP,
		.tBE = TBE,

		.plane_select = 0,
		.ecc_max      = 8,
		.need_quad    = 1,
	},
	[2] = {
		/*GSS01GSAK1*/
		.pagesize  = 2048,
		.oobsize   = 64,
		.blocksize = 2048 * 64,
		.flashsize = 2048 * 64 * 1024,

		.tSETUP  = TSETUP,
		.tHOLD   = THOLD,
		.tSHSL_R = TSHSL_R,
		.tSHSL_W = TSHSL_W,

		.tRD = TRD,
		.tPP = TPP,
		.tBE = TBE,

		.plane_select = 0,
		.ecc_max      = 4,
		.need_quad    = 1,
	},
	[3] = {
		/*GSS02GSAK1*/
		.pagesize  = 2048,
		.oobsize   = 64,
		.blocksize = 2048 * 64,
		.flashsize = 2048 * 64 * 2048,

		.tSETUP    = TSETUP,
		.tHOLD     = THOLD,
		.tSHSL_R   = TSHSL_R,
		.tSHSL_W   = TSHSL_W,

		.tRD = TRD,
		.tPP = TPP,
		.tBE = TBE,

		.plane_select = 0,
		.ecc_max      = 4,
		.need_quad    = 1,
	},
};

static struct device_id_struct device_id[] = {
	DEVICE_ID_STRUCT(0xCA13, "GSS01GSAX1", &gsto_param[0]),
	DEVICE_ID_STRUCT(0xCA23, "GSS02GSAX1", &gsto_param[1]),
	DEVICE_ID_STRUCT(0xBA13, "GSS01GSAK1", &gsto_param[2]),
	DEVICE_ID_STRUCT(0xBA23, "GSS02GSAK1", &gsto_param[3]),
};


static cdt_params_t *gsto_get_cdt_params(struct sfc_flash *flash, uint16_t device_id)
{
	CDT_PARAMS_INIT(gsto_nand->cdt_params);
	switch(device_id) {
		case 0xCA13:
		case 0xCA23:
		case 0xBA13:
		case 0xBA23:
			break;
		default:
			dev_err(flash->dev,"device_id err, please check your  device id: device_id = 0x%02x\n", device_id);
			return NULL;
	}
	return &gsto_nand->cdt_params;
}

static inline int deal_ecc_status(struct sfc_flash *flash, uint16_t device_id, uint8_t ecc_status)
{
	switch(device_id) {
		case 0xCA13:
		case 0xCA23:
			switch((ecc_status >> 4) & 0x3) {
				case 0x0:
					return 0;
				case 0x1:
					return 8;
				case 0x2:
					return -EBADMSG;
				default:
					break;
			}
			break;
		case 0xBA13:
		case 0xBA23:
			switch((ecc_status >> 4) & 0x3) {
				case 0x0:
					return 0;
				case 0x1:
					return 4;
				case 0x2:
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

static int __init gsto_nand_init(void) {

	gsto_nand = kzalloc(sizeof(*gsto_nand), GFP_KERNEL);
	if(!gsto_nand) {
		pr_err("alloc gsto_nand struct fail\n");
		return -ENOMEM;
	}

	gsto_nand->id_manufactory = 0x52;
	gsto_nand->id_device_list = device_id;
	gsto_nand->id_device_count = ARRAY_SIZE(gsto_param);

	gsto_nand->ops.get_cdt_params = gsto_get_cdt_params;
	gsto_nand->ops.deal_ecc_status = deal_ecc_status;
	/* use private get feature interface, please define it in this document */
	gsto_nand->ops.get_feature = NULL;

	return ingenic_sfcnand_register(gsto_nand);
}

fs_initcall(gsto_nand_init);
