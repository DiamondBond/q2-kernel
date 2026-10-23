#ifndef __SFC_FLASH_H
#define __SFC_FLASH_H
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include "sfc.h"

struct device_node;
struct gpio_desc;
struct mtd_info;

enum {
        PART_RW = 0,
        PART_RO,
};

#ifdef CONFIG_MTD_INGENIC_SFC_CONCAT
#define SFC_NOR_MAX_CHIPS	8
#define SFC_NAND_MAX_CHIPS	4
#define SFC_MAX_CHIPS		SFC_NOR_MAX_CHIPS

enum sfc_flash_cs_type {
	SFC_FLASH_CS_NATIVE = 0,
	SFC_FLASH_CS_GPIO,
};

struct sfc_topology {
	struct sfc *sfc;
	struct sfc_flash *chips[SFC_MAX_CHIPS];
	int nchips;
	struct mtd_info *concat_mtd;
	void (*remove)(struct sfc_topology *topo);
	void *priv;
};
#endif

struct sfc_flash {

	struct sfc *sfc;
	void *flash_info;
	struct device* dev;
#ifdef CONFIG_MTD_INGENIC_SFC_CONCAT
	struct gpio_desc *cs_gpiod;
#endif
	const char *name;
	struct mtd_info mtd;
	struct mtd_part_parser_data ppdata;
	struct mutex	lock;
#ifdef CONFIG_MTD_INGENIC_SFC_CONCAT
	enum sfc_flash_cs_type cs_type;
	uint8_t index;
#endif
	int param_offset;	//param_offset.
	struct nand_chip *chip;
	struct ingenic_sfc_info *pdata_params;
#ifdef CONFIG_MTD_INGENIC_SFC_CONCAT
	int (*prepare_profile_locked)(struct sfc_flash *flash);
#endif
	void (*create_cdt_table)(struct sfc *,void *, uint32_t);
	struct attribute_group *attr_group;
};

struct ingenic_sfc_info {

	uint8_t use_ofpart_info;	/* use device tree partiton flag */
	int param_offset;		/* param_offset */
	struct device_node *part_node;  /* flash node */
};

#ifdef CONFIG_MTD_INGENIC_SFC_CONCAT
int sfc_concat_collect_flash_children(struct device *dev, int max_chips,
		const char *media_name,
		struct device_node **children);
struct sfc_flash *sfc_flash_alloc_child(struct device *dev, struct sfc *sfc,
		struct device_node *np, int index, const char *name_prefix);
void sfc_flash_bus_lock(struct sfc_flash *flash);
int sfc_flash_sync_cdt_once(struct sfc_flash *flash,
		struct sfc_cdt_xfer *xfer);
void sfc_flash_bus_unlock(struct sfc_flash *flash);
void sfc_topology_deselect_all(struct sfc_topology *topo);
int sfc_concat_register(struct device *dev, struct device_node *of_node,
		bool use_ofpart, struct mtd_info **concat_mtd,
		struct mtd_info **subdevs, int nchips, const char *name,
		const char * const *types, struct mtd_partition *parts,
		int nr_parts);
#else
static inline void sfc_flash_bus_lock(struct sfc_flash *flash)
{
	(void)flash;
}

static inline void sfc_flash_bus_unlock(struct sfc_flash *flash)
{
	(void)flash;
}
#endif

#endif
