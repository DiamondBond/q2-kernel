/*
 * SFC NAND concat orchestration.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/mtd/partitions.h>
#include <linux/slab.h>

#include "ingenic_sfc_common.h"
#include "spinand.h"
#include "ingenic_sfc_nand_concat.h"

struct sfc_nand_host {
	struct device *dev;
	struct sfc *sfc;
	struct sfc_flash *chips[SFC_NAND_MAX_CHIPS];
	struct mtd_info *subdevs[SFC_NAND_MAX_CHIPS];
	int nchips;
};

static int ingenic_sfc_nand_prepare_profile_locked(struct sfc_flash *flash)
{
	struct ingenic_sfcnand_flashinfo *nand_info;
	struct ingenic_sfcnand_base_param *param;
	int ret;

	if (!flash || !flash->sfc)
		return -EINVAL;

	nand_info = flash->flash_info;

	if (!flash->sfc->topology || !flash->sfc->topology->priv)
		return -ENODEV;

	if (!nand_info)
		return -EINVAL;

	param = &nand_info->param;

	if (flash->create_cdt_table)
		flash->create_cdt_table(flash->sfc, flash->flash_info, UPDATE_CDT);

	ret = sfc_clk_set_highspeed(flash->sfc, flash->sfc->max_frequency);
	if (ret)
		return ret;

	set_flash_timing(flash->sfc, param->tHOLD, param->tSETUP,
			param->tSHSL_R, param->tSHSL_W);

	return 0;
}


static bool ingenic_sfc_nand_same_geometry(struct sfc_nand_host *host)
{
	struct mtd_info *base;
	int i;

	if (!host->nchips)
		return false;

	base = host->subdevs[0];
	for (i = 1; i < host->nchips; i++) {
		struct mtd_info *mtd = host->subdevs[i];

		if (mtd->type != base->type ||
		    ((mtd->flags ^ base->flags) & ~MTD_WRITEABLE) ||
		    mtd->writesize != base->writesize ||
		    mtd->erasesize != base->erasesize ||
		    mtd->oobsize != base->oobsize ||
		    mtd->ecc_strength != base->ecc_strength ||
		    mtd->bitflip_threshold != base->bitflip_threshold) {
			dev_info(host->dev,
					"NAND geometry mismatch: %s differs from %s, reject concat\n",
					mtd->name, base->name);
			return false;
		}
	}

	return true;
}

static void ingenic_sfc_nand_free_host_chips(struct sfc_nand_host *host)
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
			ingenic_sfc_nand_free_chip(host->chips[i]);
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

static void ingenic_sfc_nand_concat_remove(struct sfc_topology *topo)
{
	struct sfc_nand_host *host = topo ? topo->priv : NULL;

	if (!host)
		return;

	if (host->sfc && host->sfc->tmp_buffer && host->nchips &&
	    host->chips[0])
		free_sfc_buffer(host->chips[0]);

	ingenic_sfc_nand_free_host_chips(host);
	if (topo)
		topo->priv = NULL;
}

int ingenic_sfc_nand_probe_concat(struct platform_device *pdev,
		struct sfc *sfc)
{
	const char *ingenic_probe_types[] = {"cmdlinepart", "ofpart", NULL};
	struct mtd_partition *parts = NULL;
	int nr_parts = 0;
	struct sfc_nand_host *host;
	struct sfc_topology *topo;
	struct device_node *child;
	struct device_node *children[SFC_NAND_MAX_CHIPS] = { NULL };
	struct ingenic_sfcnand_flashinfo *nand_info;
	size_t buffer_size;
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
	topo->remove = ingenic_sfc_nand_concat_remove;
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
			SFC_NAND_MAX_CHIPS, "NAND", children);
	if (nchips < 0) {
		ret = nchips;
		goto err_no_chip;
	}

	for (index = 0; index < nchips; index++) {
		struct sfc_flash *flash;

		child = children[index];
		flash = sfc_flash_alloc_child(host->dev, host->sfc, child,
				index, "sfc_nand");
		if (IS_ERR(flash)) {
			dev_err(&pdev->dev, "alloc %s failed: %ld\n",
					child->full_name, PTR_ERR(flash));
			ret = PTR_ERR(flash);
			goto err_no_chip;
		}
		sfc->topology->chips[index] = flash;
		sfc->topology->nchips = max(sfc->topology->nchips, index + 1);

		sfc_flash_bus_lock(flash);
		ret = ingenic_sfc_nand_init_chip(flash, flash->name);
		sfc_flash_bus_unlock(flash);
		if (ret) {
			dev_err(&pdev->dev, "init %s failed: %d\n",
					child->full_name, ret);
			ingenic_sfc_nand_free_chip(flash);
			sfc->topology->chips[index] = NULL;
			sfc->topology->nchips = host->nchips;
			goto err_no_chip;
		}
		flash->prepare_profile_locked =
			ingenic_sfc_nand_prepare_profile_locked;

		host->chips[index] = flash;
		host->subdevs[index] = &flash->mtd;
		host->nchips = max(host->nchips, index + 1);
	}

	if (!ingenic_sfc_nand_same_geometry(host)) {
		ret = -EINVAL;
		goto err_no_chip;
	}

	buffer_size = 0;
	for (index = 0; index < host->nchips; index++) {
		struct mtd_info *mtd = host->subdevs[index];

		buffer_size = max_t(size_t, buffer_size,
				mtd->writesize + mtd->oobsize);
	}
	ret = request_sfc_buffer_size(host->chips[0], buffer_size);
	if (ret) {
		dev_err(&pdev->dev, "Failure to request Temporary Buffer space!\n");
		ret = -ENOMEM;
		goto err_no_chip;
	}

	dev_info(&pdev->dev,
			"SPI NAND concat uses mtdconcat same-geometry path\n");

	nand_info = host->chips[0]->flash_info;
	use_ofpart = host->chips[0]->pdata_params &&
		host->chips[0]->pdata_params->use_ofpart_info;
	if (!use_ofpart) {
		ret = ingenic_sfcnand_partition(host->chips[0]);
		if (ret && ret != -EINVAL) {
			dev_warn(&pdev->dev,
					"No valid burner partitions for concat MTD fallback: %d\n",
					ret);
		}

		if (!ret) {
			parts = nand_info->partition.partition;
			nr_parts = nand_info->partition.num_partition;
		}
		ingenic_sfcnand_free_burner_param();
	}

	ret = sfc_concat_register(&pdev->dev, pdev->dev.of_node,
			use_ofpart, &topo->concat_mtd, host->subdevs,
			host->nchips, "sfc_nand", ingenic_probe_types, parts,
			nr_parts);
	if (ret)
		goto err_no_chip;

	dev_info(&pdev->dev, "SPI NAND concat MTD LOAD OK, chips:%d size:%llu\n",
			host->nchips, topo->concat_mtd->size);
	return 0;

err_no_chip:
	if (host->sfc && host->nchips && host->chips[0] &&
	    host->sfc->tmp_buffer)
		free_sfc_buffer(host->chips[0]);
	ingenic_sfc_nand_free_host_chips(host);
	sfc->topology = NULL;
	free_sfc_desc(sfc);
	return ret;
}
