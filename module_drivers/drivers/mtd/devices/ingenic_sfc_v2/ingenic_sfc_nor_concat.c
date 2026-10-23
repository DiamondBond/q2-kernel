/*
 * SFC NOR concat orchestration.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/limits.h>
#include <linux/stddef.h>
#include <linux/err.h>
#include <linux/mtd/partitions.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "ingenic_sfc_common.h"
#include "spinor.h"
#include "ingenic_sfc_nor_concat.h"

struct sfc_nor_concat_params {
	struct spiflash_info *spiflash_info;
	struct norflash_partitions partitions;
	int last_error;
	bool loaded;
	bool valid;
};

struct sfc_nor_host {
	struct device *dev;
	struct sfc *sfc;
	struct sfc_flash *chips[SFC_NOR_MAX_CHIPS];
	struct mtd_info *subdevs[SFC_NOR_MAX_CHIPS];
	int nchips;
	struct sfc_nor_concat_params *concat_params;
};

static struct sfc_nor_host *sfc_nor_host_from_flash(struct sfc_flash *flash)
{
	if (!flash || !flash->sfc || !flash->sfc->topology)
		return NULL;

	return flash->sfc->topology->priv;
}

static bool sfc_nor_concat_spi_info_valid(const struct spi_nor_info *info)
{
	return info && info->id && info->chip_size &&
		info->page_size && info->erase_size;
}

static const struct spi_nor_info *sfc_nor_concat_burner_chip_info(
		const struct spiflash_info *spiflash_info, uint32_t chip_index,
		uint32_t *linear_base)
{
	const struct burner_params *burner;
	const struct nor_concat_extension *concat;

	if (!spiflash_info)
		return NULL;

	burner = &spiflash_info->burner_params;
	concat = &spiflash_info->concat;
	if (chip_index == 0) {
		if (linear_base)
			*linear_base = 0;
		return &burner->spi_nor_info;
	}

	if (chip_index >= concat->chip_count)
		return NULL;

	if (linear_base)
		*linear_base = concat->chip[chip_index - 1].linear_base;

	return &concat->chip[chip_index - 1].spi_nor_info;
}

static bool sfc_nor_concat_burner_params_valid(
		const struct spiflash_info *spiflash_info)
{
	const struct burner_params *burner;
	const struct nor_concat_extension *concat;
	const struct spi_nor_info *info;
	uint32_t total;
	uint32_t i;

	if (!spiflash_info)
		return false;
	burner = &spiflash_info->burner_params;
	concat = &spiflash_info->concat;
	if (burner->magic != NOR_MAGIC ||
	    burner->version != NOR_CONCAT_VERSION)
		return false;
	if (concat->chip_count < 2 ||
	    concat->chip_count > NOR_CONCAT_CHIP_MAX)
		return false;
	if (burner->norflash_partitions.num_partition_info > NOR_PART_NUM)
		return false;

	info = sfc_nor_concat_burner_chip_info(spiflash_info, 0, NULL);
	if (!sfc_nor_concat_spi_info_valid(info))
		return false;

	total = info->chip_size;
	for (i = 1; i < concat->chip_count; i++) {
		uint32_t linear_base;

		info = sfc_nor_concat_burner_chip_info(spiflash_info, i,
						       &linear_base);
		if (!sfc_nor_concat_spi_info_valid(info))
			return false;
		if (linear_base != total)
			return false;
		if (info->chip_size > U32_MAX - total)
			return false;
		total += info->chip_size;
	}

	return total == concat->total_size;
}

#ifndef CONFIG_INGENIC_BUILTIN_PARAMS
static bool sfc_nor_concat_partition_valid(const struct nor_partition *part,
		uint32_t total_size)
{
	uint64_t end;

	if (!part || !part->size)
		return false;
	if (strnlen(part->name, sizeof(part->name)) == sizeof(part->name))
		return false;
	if (part->offset > total_size)
		return false;

	end = (uint64_t)part->offset + part->size;
	return end >= part->offset && end <= total_size;
}

static int sfc_nor_concat_load_partitions(
		struct sfc_nor_concat_params *params)
{
	const struct spiflash_info *spiflash_info = params->spiflash_info;
	const struct burner_params *burner;
	const struct nor_concat_extension *concat;
	uint32_t i;

	if (!spiflash_info ||
	    spiflash_info->burner_params.norflash_partitions.num_partition_info >
	    NOR_PART_NUM)
		return -EINVAL;

	burner = &spiflash_info->burner_params;
	concat = &spiflash_info->concat;
	memset(&params->partitions, 0, sizeof(params->partitions));
	params->partitions.num_partition_info =
		burner->norflash_partitions.num_partition_info;

	for (i = 0; i < params->partitions.num_partition_info; i++) {
		const struct nor_partition *src =
			&burner->norflash_partitions.nor_partition[i];
		struct nor_partition *dst = &params->partitions.nor_partition[i];

		if (!sfc_nor_concat_partition_valid(src, concat->total_size))
			return -EINVAL;

		memcpy(dst->name, src->name, sizeof(dst->name));
		dst->offset = src->offset;
		dst->size = src->size;
		dst->mask_flags = src->mask_flags;
		dst->manager_mode = src->manager_mode;
	}

	return 0;
}

static int sfc_nor_concat_read_params(struct sfc_flash *flash,
		unsigned int param_offset, size_t len, uint8_t *buf)
{
	struct sfc_cdt_xfer xfer;

	memset(&xfer, 0, sizeof(xfer));
	xfer.cmd_index = NOR_READ_STANDARD;
	xfer.columnaddr = 0;
	xfer.rowaddr = param_offset;
	xfer.dataen = ENABLE;
	xfer.config.datalen = len;
	xfer.config.data_dir = GLB0_TRAN_DIR_READ;
	xfer.config.ops_mode = CPU_OPS;
	xfer.config.buf = buf;

	if (sfc_sync_cdt(flash->sfc, &xfer)) {
		dev_err(flash->dev, "sfc_sync_cdt error ! %s %s %d\n",
				__FILE__, __func__, __LINE__);
		return -EIO;
	}

	return 0;
}

static int sfc_nor_concat_load_params(struct sfc_flash *flash,
		unsigned int param_offset)
{
	struct sfc_nor_host *host = sfc_nor_host_from_flash(flash);
	struct spiflash_info *spiflash_info;
	struct burner_params *burner;
	int ret;

	if (!host)
		return -ENOENT;
	if (host->concat_params && host->concat_params->loaded)
		return host->concat_params->valid ? 0 :
			(host->concat_params->last_error ?: -ENOENT);

	host->concat_params = devm_kzalloc(host->dev,
			sizeof(*host->concat_params), GFP_KERNEL);
	if (!host->concat_params)
		return -ENOMEM;

	host->concat_params->loaded = true;

	spiflash_info = devm_kzalloc(host->dev, sizeof(*spiflash_info), GFP_KERNEL);
	if (!spiflash_info) {
		ret = -ENOMEM;
		goto err_load;
	}
	burner = &spiflash_info->burner_params;

	ret = sfc_nor_concat_read_params(flash, param_offset,
			sizeof(*spiflash_info), (uint8_t *)spiflash_info);
	if (ret)
		goto err_load;

	if (burner->magic != NOR_MAGIC ||
	    burner->version != NOR_CONCAT_VERSION) {
		ret = -ENOENT;
		goto err_load;
	}

	if (!sfc_nor_concat_burner_params_valid(spiflash_info)) {
		ret = -EINVAL;
		goto err_load;
	}

	host->concat_params->spiflash_info = spiflash_info;
	ret = sfc_nor_concat_load_partitions(host->concat_params);
	if (ret)
		goto err_load;

	host->concat_params->valid = true;
	host->concat_params->last_error = 0;
	dev_info(flash->dev,
			"loaded NOR concat params chips=%u total=0x%x parts=%u\n",
			spiflash_info->concat.chip_count,
			spiflash_info->concat.total_size,
			burner->norflash_partitions.num_partition_info);

	return 0;

err_load:
	host->concat_params->last_error = ret ?: -EINVAL;
	return host->concat_params->last_error;
}

int ingenic_sfc_nor_try_concat_params(struct sfc_flash *flash,
		unsigned int param_offset)
{
	struct spinor_flashinfo *nor_info = flash->flash_info;
	struct sfc_nor_host *host = sfc_nor_host_from_flash(flash);
	struct sfc_nor_concat_params *params;
	const struct spi_nor_info *info;
	struct spi_nor_info *spi_info;
	struct nor_private_data *pri_data;
	int flash_id;
	int ret;

	ret = sfc_nor_concat_load_params(flash, param_offset);
	if (ret)
		return ret;

	if (!host)
		return -ENOENT;

	params = host->concat_params;
	if (!params || !params->valid || flash->index >=
	    params->spiflash_info->concat.chip_count)
		return -EINVAL;

	info = sfc_nor_concat_burner_chip_info(params->spiflash_info,
					       flash->index, NULL);
	if (!info)
		return -EINVAL;

	flash_id = sfc_nor_read_id(flash);
	if (flash_id < 0)
		return flash_id;
	if (flash_id != info->id) {
		dev_err(flash->dev,
				"%s concat params id mismatch index=%u probe=0x%x params=0x%x\n",
				flash->name, flash->index, flash_id, info->id);
		return -ENODEV;
	}

	spi_info = devm_kzalloc(flash->dev, sizeof(*spi_info), GFP_KERNEL);
	pri_data = devm_kzalloc(flash->dev, sizeof(*pri_data), GFP_KERNEL);
	if (!spi_info || !pri_data)
		return -ENOMEM;

	memcpy(spi_info, info, sizeof(*spi_info));
	if (!sfc_nor_concat_spi_info_valid(spi_info))
		return -EINVAL;

	pri_data->fs_erase_size = params->spiflash_info->burner_params.fs_erase_size ?
		params->spiflash_info->burner_params.fs_erase_size :
		spi_info->erase_size;
	pri_data->uk_quad = params->spiflash_info->burner_params.uk_quad;

	nor_info->nor_flash_info = spi_info;
	nor_info->nor_pri_data = pri_data;
	if (flash->index == 0)
		nor_info->norflash_partitions = &params->partitions;

	dev_info(flash->dev,
			"%s use NOR concat params index=%u id=0x%x size=0x%x\n",
			flash->name, flash->index, flash_id, spi_info->chip_size);

	return 0;
}

int ingenic_sfc_nor_try_shared_concat_params(struct sfc_flash *flash)
{
	struct sfc_nor_host *host;
	struct spinor_flashinfo *nor_info;
	struct spinor_flashinfo *ref_info;
	struct sfc_flash *ref;
	int chip_id;
	int i;

	host = sfc_nor_host_from_flash(flash);
	if (!host || !host->nchips)
		return -ENOENT;

	nor_info = flash->flash_info;
	if (!nor_info)
		return -EINVAL;

	chip_id = sfc_nor_read_id(flash);
	if (chip_id < 0) {
		dev_err(flash->dev, "%s read id failed: %d\n",
				flash->name, chip_id);
		return chip_id;
	}

	for (i = 0; i < host->nchips; i++) {
		ref = host->chips[i];
		if (!ref || !ref->flash_info)
			continue;

		ref_info = ref->flash_info;
		if (!ref_info->nor_flash_info || !ref_info->nor_pri_data)
			continue;

		if (ref_info->nor_flash_info->id != chip_id)
			continue;

		nor_info->nor_flash_info = ref_info->nor_flash_info;
		nor_info->nor_pri_data = ref_info->nor_pri_data;
		return 0;
	}

	return -ENOENT;
}

#endif

static int ingenic_sfc_nor_prepare_profile_locked(struct sfc_flash *flash)
{
	struct sfc_nor_host *host;
	struct spinor_flashinfo *nor_info;
	struct spi_nor_info *params;

	if (!flash || !flash->sfc)
		return -EINVAL;

	nor_info = flash->flash_info;

	host = sfc_nor_host_from_flash(flash);
	if (!host)
		return -ENODEV;

	if (!nor_info || !nor_info->nor_flash_info)
		return -EINVAL;

	params = nor_info->nor_flash_info;
	if (flash->create_cdt_table)
		flash->create_cdt_table(flash->sfc, flash->flash_info, UPDATE_CDT);

	set_flash_timing(flash->sfc, params->tCHSH, params->tSLCH,
			params->tSHSL_RD, params->tSHSL_WR);

	return ingenic_sfc_nor_clk_set_highspeed(flash);
}

static int sfc_nor_concat_validate(struct sfc_nor_host *host)
{
	struct sfc_nor_concat_params *params = host ? host->concat_params : NULL;
	uint64_t total = 0;
	int i;

	if (!params || !params->valid)
		return 0;
	if (!params->spiflash_info)
		return -EINVAL;

	if (host->nchips != params->spiflash_info->concat.chip_count) {
		dev_err(host->dev,
				"NOR concat probed %d chips, concat params require %u\n",
				host->nchips,
				params->spiflash_info->concat.chip_count);
		return -EINVAL;
	}

	for (i = 0; i < host->nchips; i++)
		total += host->subdevs[i]->size;

	if (total != params->spiflash_info->concat.total_size) {
		dev_err(host->dev,
				"NOR concat probed size 0x%llx, concat params require 0x%llx\n",
				total, (uint64_t)params->spiflash_info->concat.total_size);
		return -EINVAL;
	}

	return 0;
}

static void ingenic_sfc_nor_free_host_chips(struct sfc_nor_host *host)
{
	struct sfc *sfc;
	int i;

	if (!host)
		return;

	sfc = host->sfc;

	if (sfc)
		sfc_topology_deselect_all(sfc->topology);

	for (i = 0; i < host->nchips; i++) {
		if (host->chips[i]) {
			ingenic_sfc_nor_free_chip(host->chips[i]);
			host->chips[i] = NULL;
			host->subdevs[i] = NULL;
			if (sfc && sfc->topology)
				sfc->topology->chips[i] = NULL;
		}
	}

	host->nchips = 0;
	if (sfc && sfc->topology)
		sfc->topology->nchips = 0;
}

static void ingenic_sfc_nor_concat_remove(struct sfc_topology *topo)
{
	struct sfc_nor_host *host = topo ? topo->priv : NULL;

	if (!host)
		return;

	ingenic_sfc_nor_free_host_chips(host);
	if (topo)
		topo->priv = NULL;
}

int ingenic_sfc_nor_probe_concat(struct platform_device *pdev, struct sfc *sfc)
{
	const char *ingenic_probe_types[] = {"cmdlinepart", "ofpart", NULL};
	struct mtd_partition *ingenic_mtd_partition = NULL;
	struct sfc_nor_host *host;
	struct sfc_topology *topo;
	struct device_node *child;
	struct device_node *children[SFC_NOR_MAX_CHIPS] = { NULL };
	int num_partition_info = 0;
	bool use_ofpart;
	int nchips;
	int ret;
	int index;

	host = devm_kzalloc(&pdev->dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->dev = &pdev->dev;
	host->sfc = sfc;
	topo = devm_kzalloc(&pdev->dev, sizeof(*topo), GFP_KERNEL);
	if (!topo)
		return -ENOMEM;

	topo->sfc = sfc;
	topo->remove = ingenic_sfc_nor_concat_remove;
	topo->priv = host;
	sfc->topology = topo;

	ret = request_sfc_desc(sfc, DESC_MAX_NUM);
	if (ret) {
		dev_err(&pdev->dev,
				"Failure to request DMA descriptor space: %d\n", ret);
		sfc->topology = NULL;
		return ret;
	}

	nchips = sfc_concat_collect_flash_children(&pdev->dev,
			SFC_NOR_MAX_CHIPS, "NOR", children);
	if (nchips < 0) {
		ret = nchips;
		goto err_no_chip;
	}

	for (index = 0; index < nchips; index++) {
		struct sfc_flash *flash;

		child = children[index];
		flash = sfc_flash_alloc_child(host->dev, host->sfc, child,
				index, "sfc_nor");
		if (IS_ERR(flash)) {
			dev_err(&pdev->dev, "alloc %s failed: %ld\n",
					child->full_name, PTR_ERR(flash));
			ret = PTR_ERR(flash);
			goto err_no_chip;
		}
		sfc->topology->chips[index] = flash;
		sfc->topology->nchips = max(sfc->topology->nchips, index + 1);

		sfc_flash_bus_lock(flash);
		ret = ingenic_sfc_nor_init_chip(flash, flash->name);
		sfc_flash_bus_unlock(flash);
		if (ret) {
			dev_err(&pdev->dev, "init %s failed: %d\n",
					child->full_name, ret);
			ingenic_sfc_nor_free_chip(flash);
			sfc->topology->chips[index] = NULL;
			sfc->topology->nchips = host->nchips;
			goto err_no_chip;
		}
		flash->prepare_profile_locked =
			ingenic_sfc_nor_prepare_profile_locked;

		host->chips[index] = flash;
		host->subdevs[index] = &flash->mtd;
		host->nchips = max(host->nchips, index + 1);
	}

	ret = sfc_nor_concat_validate(host);
	if (ret)
		goto err_no_chip;

#ifndef CONFIG_INGENIC_BUILTIN_PARAMS
	if (host->chips[0]->flash_info) {
		struct spinor_flashinfo *nor_info = host->chips[0]->flash_info;

		if (nor_info->norflash_partitions)
			ingenic_mtd_partition =
				ingenic_sfc_nor_alloc_burner_parts(host->chips[0],
						&num_partition_info);
	}
#endif
	if (!ingenic_mtd_partition)
		dev_warn(&pdev->dev, "No burner partitions for concat MTD fallback\n");

	use_ofpart = host->chips[0]->pdata_params &&
		host->chips[0]->pdata_params->use_ofpart_info;
	ret = sfc_concat_register(&pdev->dev, pdev->dev.of_node,
			use_ofpart, &topo->concat_mtd, host->subdevs,
			host->nchips, "sfc_nor", ingenic_probe_types,
			ingenic_mtd_partition, num_partition_info);
	kfree(ingenic_mtd_partition);
	if (ret)
		goto err_concat_register;

	dev_info(&pdev->dev, "SPI NOR concat MTD LOAD OK, chips:%d size:%llu\n",
			host->nchips, topo->concat_mtd->size);
	return 0;

err_concat_register:
err_no_chip:
	ingenic_sfc_nor_free_host_chips(host);
	sfc->topology = NULL;
	free_sfc_desc(sfc);
	return ret;
}
