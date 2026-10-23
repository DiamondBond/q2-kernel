#ifndef __INGENIC_SFC_NOR_CONCAT_H
#define __INGENIC_SFC_NOR_CONCAT_H

#include <linux/errno.h>

struct mtd_partition;
struct platform_device;
struct sfc;
struct sfc_flash;

#ifdef CONFIG_MTD_INGENIC_SFC_NOR_CONCAT
int ingenic_sfc_nor_probe_concat(struct platform_device *pdev, struct sfc *sfc);
int ingenic_sfc_nor_try_concat_params(struct sfc_flash *flash,
		unsigned int param_offset);
int ingenic_sfc_nor_try_shared_concat_params(struct sfc_flash *flash);
#else
static inline int ingenic_sfc_nor_probe_concat(struct platform_device *pdev,
		struct sfc *sfc)
{
	return -ENOENT;
}

static inline int ingenic_sfc_nor_try_concat_params(struct sfc_flash *flash,
		unsigned int param_offset)
{
	return -ENOENT;
}

static inline int ingenic_sfc_nor_try_shared_concat_params(
		struct sfc_flash *flash)
{
	return -ENOENT;
}
#endif

int sfc_nor_read_id(struct sfc_flash *flash);

int ingenic_sfc_nor_clk_set_highspeed(struct sfc_flash *flash);
int ingenic_sfc_nor_init_chip(struct sfc_flash *flash, const char *name);
void ingenic_sfc_nor_free_chip(struct sfc_flash *flash);
struct mtd_partition *ingenic_sfc_nor_alloc_burner_parts(
		struct sfc_flash *flash, int *num_partition_info);

#endif
