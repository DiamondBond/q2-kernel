#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/mtd/partitions.h>
#include "../spinand.h"
#include "../ingenic_sfc_common.h"
#include "nand_common.h"

#define THOLD	    5
#define TSETUP	    5
#define TSHSL_R	    20
#define TSHSL_W	    20

#define TRD	    450
#define TPP	    800
#define TBE	    10

static struct ingenic_sfcnand_device *puya_nand;
static struct ingenic_sfcnand_base_param puya_param[] = {
	[0] = {
		/*P25N01GHA*/
		.pagesize = 2 * 1024,
		.blocksize = 2 * 1024 * 64,
		.oobsize = 64,
		.flashsize = 2 * 1024 * 64 * 1024,

		.tSETUP  = TSETUP,
		.tHOLD   = THOLD,
		.tSHSL_R = TSHSL_R,
		.tSHSL_W = TSHSL_W,

		.tRD = TRD,
		.tPP = TPP,
		.tBE = TBE,

		.plane_select = 0,
		.ecc_max = 13,
		.need_quad = 1,
	},
};

static struct device_id_struct device_id[] = {
	DEVICE_ID_STRUCT(0x2A, "P25N01GHA", &puya_param[0]),
};

static cdt_params_t *puya_get_cdt_params(struct sfc_flash *flash, uint16_t device_id)
{
	CDT_PARAMS_INIT(puya_nand->cdt_params);
	switch(device_id) {
		case 0x2A:
			break;
		default:
			dev_err(flash->dev,"device_id err, please check your  device id: device_id = 0x%02x\n", device_id);
			return NULL;
	}
	return &puya_nand->cdt_params;
}

static inline int deal_ecc_status(struct sfc_flash *flash, uint16_t device_id, uint8_t ecc_status)
{
	switch(device_id) {
		case 0x2A:
			switch((ecc_status >> 4) & 0x7) {
				case 0x0:
					return 0;
				case 0x1:
					return 4;
				case 0x2:
					return -EBADMSG;
				case 0x3:
					return 8;
				case 0x5:
					return 13;
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

static int __init puya_nand_init(void) {

	puya_nand = kzalloc(sizeof(*puya_nand), GFP_KERNEL);
	if(!puya_nand) {
		pr_err("alloc puya_nand struct fail\n");
		return -ENOMEM;
	}

	puya_nand->id_manufactory = 0x85;
	puya_nand->id_device_list = device_id;
	puya_nand->id_device_count = ARRAY_SIZE(puya_param);

	puya_nand->ops.get_cdt_params = puya_get_cdt_params;
	puya_nand->ops.deal_ecc_status = deal_ecc_status;

	/* use private get feature interface, please define it in this document */
	puya_nand->ops.get_feature = NULL;

	return ingenic_sfcnand_register(puya_nand);
}

fs_initcall(puya_nand_init);
