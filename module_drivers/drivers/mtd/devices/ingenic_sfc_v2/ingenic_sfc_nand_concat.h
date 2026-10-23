#ifndef __INGENIC_SFC_NAND_CONCAT_H
#define __INGENIC_SFC_NAND_CONCAT_H

#include <linux/types.h>

struct platform_device;
struct sfc;
struct sfc_flash;

int ingenic_sfc_nand_probe_concat(struct platform_device *pdev,
		struct sfc *sfc);

int ingenic_sfc_nand_init_chip(struct sfc_flash *flash, const char *name);
void ingenic_sfc_nand_free_chip(struct sfc_flash *flash);
int32_t ingenic_sfcnand_partition(struct sfc_flash *flash);
void ingenic_sfcnand_free_burner_param(void);
void free_sfc_buffer(struct sfc_flash *flash);
int request_sfc_buffer_size(struct sfc_flash *flash, size_t size);

#endif
