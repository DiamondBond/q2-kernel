/*
 * SFC flash/topology context helpers.
 */

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/mtd/concat.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <asm/io.h>

#include "ingenic_sfc_common.h"
#include "spinor.h"

int sfc_sync_cdt_raw(struct sfc *sfc, struct sfc_cdt_xfer *xfer);

static void sfc_set_glb1_chip_sel(struct sfc *sfc, uint32_t chip_sel)
{
	uint32_t tmp;

	tmp = readl(sfc->iomem + SFC_GLB1);
	tmp &= ~GLB1_CHIP_SEL_MSK;
	tmp |= chip_sel << GLB1_CHIP_SEL_OFFSET;
	writel(tmp, sfc->iomem + SFC_GLB1);
}

int sfc_concat_register(struct device *dev, struct device_node *of_node,
		bool use_ofpart, struct mtd_info **concat_mtd,
		struct mtd_info **subdevs, int nchips, const char *name,
		const char * const *types, struct mtd_partition *parts,
		int nr_parts)
{
	struct device_node *partitions;
	bool fixed_partitions;
	int ret;

	if (!dev || !concat_mtd || !subdevs || nchips <= 0 || !name)
		return -EINVAL;

	*concat_mtd = mtd_concat_create(subdevs, nchips, name);
	if (!*concat_mtd)
		return -ENOMEM;

	if (use_ofpart) {
		if (!of_node) {
			dev_warn(dev, "ignore concat ofpart: missing SFC OF node\n");
		} else {
			partitions = of_get_child_by_name(of_node, "partitions");
			if (!partitions) {
				dev_warn(dev,
						"ignore concat ofpart: missing fixed-partitions child\n");
			} else {
				fixed_partitions = of_device_is_compatible(partitions,
						"fixed-partitions");
				of_node_put(partitions);
				if (fixed_partitions)
					mtd_set_of_node(*concat_mtd, of_node);
				else
					dev_warn(dev,
							"ignore concat ofpart: partitions child is not fixed-partitions\n");
			}
		}
	}

	ret = mtd_device_parse_register(*concat_mtd, types, NULL, parts,
			nr_parts);
	if (ret) {
		dev_err(dev, "Failed to register concat mtd: %d\n", ret);
		mtd_concat_destroy(*concat_mtd);
		*concat_mtd = NULL;
	}

	return ret;
}

int sfc_concat_collect_flash_children(struct device *dev, int max_chips,
		const char *media_name,
		struct device_node **children)
{
	struct device_node *child;
	int found_chips = 0;
	int max_index = -1;
	int index;
	u32 reg;
	int ret;

	if (!dev || !dev->of_node || !media_name || !children || max_chips <= 0)
		return -EINVAL;

	for_each_available_child_of_node(dev->of_node, child) {
		if (!of_node_name_eq(child, "flash"))
			continue;

		ret = of_property_read_u32(child, "reg", &reg);
		if (ret) {
			dev_err(dev, "%s missing concat flash reg: %d\n",
					child->full_name, ret);
			of_node_put(child);
			return ret;
		}
		if (reg >= max_chips) {
			dev_err(dev, "%s concat flash reg %u exceeds max %d\n",
					child->full_name, reg, max_chips);
			of_node_put(child);
			return -EINVAL;
		}

		index = reg;
		if (children[index]) {
			dev_err(dev, "duplicate %s concat flash reg %d at %s\n",
					media_name, index, child->full_name);
			of_node_put(child);
			return -EINVAL;
		}

		children[index] = child;
		found_chips++;
		max_index = max(max_index, index);
	}

	if (found_chips < 2) {
		dev_err(dev, "%s concat requires at least 2 chips, probed %d\n",
				media_name, found_chips);
		return -EINVAL;
	}

	if (max_index + 1 != found_chips) {
		dev_err(dev,
				"%s concat flash reg values must be contiguous from 0, probed %d chips over span %d\n",
				media_name, found_chips, max_index + 1);
		return -EINVAL;
	}

	return max_index + 1;
}

void sfc_topology_deselect_all(struct sfc_topology *topo)
{
	struct sfc *sfc;
	int i;

	if (!topo || !topo->sfc)
		return;

	sfc = topo->sfc;

	for (i = 0; i < topo->nchips; i++) {
		struct sfc_flash *flash = topo->chips[i];

		if (flash && flash->cs_type == SFC_FLASH_CS_GPIO &&
		    flash->cs_gpiod)
			gpiod_set_value_cansleep(flash->cs_gpiod, 0);
	}

	sfc_set_glb1_chip_sel(sfc, 0);

	if (sfc->pins_gpio_cs)
		sfc_select_pinctrl_state(sfc, SFC_PINCTRL_STATE_GPIO_CS);
	else
		sfc_select_pinctrl_state(sfc, SFC_PINCTRL_STATE_DEFAULT);
}

struct sfc_flash *sfc_flash_alloc_child(struct device *dev, struct sfc *sfc,
		struct device_node *np, int index, const char *name_prefix)
{
	struct ingenic_sfc_info *pdata_params;
	struct ingenic_sfc_info *parent_params;
	struct sfc_flash *flash;
	int ret;

	if (!dev || !sfc || !np)
		return ERR_PTR(-EINVAL);

	flash = devm_kzalloc(dev, sizeof(*flash), GFP_KERNEL);
	if (!flash)
		return ERR_PTR(-ENOMEM);

	pdata_params = devm_kzalloc(dev, sizeof(*pdata_params), GFP_KERNEL);
	if (!pdata_params)
		return ERR_PTR(-ENOMEM);

	parent_params = dev_get_platdata(dev);
	if (parent_params) {
		*pdata_params = *parent_params;
	} else {
		pdata_params->param_offset = -EINVAL;
		pdata_params->use_ofpart_info = 0;
	}

	flash->sfc = sfc;
	flash->dev = dev;
	flash->pdata_params = pdata_params;
	flash->index = index;
	mutex_init(&flash->lock);

	flash->name = devm_kasprintf(dev, GFP_KERNEL, "%s%d",
			name_prefix, index);
	if (!flash->name)
		return ERR_PTR(-ENOMEM);

	flash->cs_type = of_get_property(np, "cs-gpios", NULL) ?
		SFC_FLASH_CS_GPIO : SFC_FLASH_CS_NATIVE;

	if (flash->cs_type == SFC_FLASH_CS_GPIO) {
		if (!flash->sfc->pinctrl || !flash->sfc->pins_gpio_cs) {
			dev_err(flash->dev,
					"%s gpio CS requires controller pinctrl state gpio-cs\n",
					np->full_name);
			return ERR_PTR(-EINVAL);
		}

		flash->cs_gpiod = devm_gpiod_get_from_of_node(flash->dev,
				np, "cs-gpios", 0, GPIOD_OUT_LOW,
				flash->name);
		if (IS_ERR(flash->cs_gpiod)) {
			ret = PTR_ERR(flash->cs_gpiod);
			dev_err(flash->dev, "%s get cs-gpios failed: %d\n",
					np->full_name, ret);
			return ERR_PTR(ret);
		}
	}

	return flash;
}


static int sfc_select_flash(struct sfc_flash *flash)
{
	struct sfc *sfc = flash->sfc;
	int ret;

	sfc_topology_deselect_all(sfc->topology);

	if (flash->cs_type == SFC_FLASH_CS_GPIO) {
		/*
		 * GPIO-CS flashes still use SFC native channel 0 for the bus
		 * transfer. The native CS0 pin is isolated by the gpio-cs
		 * pinctrl state, while the external GPIO provides chip select.
		 */
		ret = sfc_select_pinctrl_state(sfc, SFC_PINCTRL_STATE_GPIO_CS);
		if (ret)
			return ret;

		if (!flash->cs_gpiod)
			return -EINVAL;

		gpiod_set_value_cansleep(flash->cs_gpiod, 1);
		return 0;
	}

	if (flash->index > 1) {
		dev_err(flash->dev, "%s native CS%u is not supported\n",
				flash->name, flash->index);
		return -EINVAL;
	}

	ret = sfc_select_pinctrl_state(sfc, SFC_PINCTRL_STATE_DEFAULT);
	if (ret)
		return ret;

	sfc_set_glb1_chip_sel(sfc, flash->index);
	return 0;
}

static struct sfc_cdt *sfc_cdt_entry(struct sfc *sfc, unsigned short index)
{
	if (index >= INDEX_MAX_NUM)
		return NULL;

	return (struct sfc_cdt *)(sfc->iomem + SFC_CDT) + index;
}


static int sfc_sync_single_cdt_for_flash_locked(struct sfc_flash *flash,
		struct sfc_cdt_xfer *xfer, bool force_single)
{
	struct sfc *sfc = flash->sfc;
	struct sfc_cdt *cdt = NULL;
	uint32_t saved_link = 0;
	int ret;

	if (force_single) {
		cdt = sfc_cdt_entry(sfc, xfer->cmd_index);
		if (!cdt)
			return -EINVAL;

		saved_link = cdt->link;
		cdt->link &= ~BIT(31);
	}

	ret = sfc_select_flash(flash);
	if (ret)
		goto restore_cdt;

	ret = sfc_sync_cdt_raw(sfc, xfer);
	if (flash->cs_type == SFC_FLASH_CS_GPIO && flash->cs_gpiod)
		gpiod_set_value_cansleep(flash->cs_gpiod, 0);

restore_cdt:
	if (force_single)
		cdt->link = saved_link;

	return ret;
}

void sfc_flash_bus_lock(struct sfc_flash *flash)
{
	struct sfc *sfc;

	if (!flash || !flash->sfc || !flash->sfc->topology)
		return;

	sfc = flash->sfc;
	mutex_lock(&sfc->bus_lock);
	sfc->bus_owner = current;
	sfc->flash = flash;
}

void sfc_flash_bus_unlock(struct sfc_flash *flash)
{
	struct sfc *sfc;

	if (!flash || !flash->sfc || !flash->sfc->topology)
		return;

	sfc = flash->sfc;
	if (WARN_ON(sfc->bus_owner != current))
		return;
	if (sfc->flash == flash)
		sfc->flash = NULL;
	sfc->bus_owner = NULL;
	mutex_unlock(&sfc->bus_lock);
}

static int sfc_sync_cdt_for_flash_locked(struct sfc_flash *flash,
		struct sfc_cdt_xfer *xfer)
{
	struct sfc *sfc = flash->sfc;
	struct sfc_cdt_xfer single_xfer;
	struct sfc_cdt *cdt;
	unsigned short index;
	int ret;

	cdt = sfc_cdt_entry(sfc, xfer->cmd_index);
	if (flash->cs_type == SFC_FLASH_CS_GPIO && cdt &&
	    (cdt->link & BIT(31))) {
		index = xfer->cmd_index;
		do {
			bool linked;

			cdt = sfc_cdt_entry(sfc, index);
			if (!cdt)
				return -EINVAL;
			linked = !!(cdt->link & BIT(31));

			single_xfer = *xfer;
			single_xfer.cmd_index = index;
			if (!(cdt->xfer & TRAN_CONF0_DATEEN)) {
				single_xfer.dataen = DISABLE;
				memset(&single_xfer.config, 0,
						sizeof(single_xfer.config));
			}

			ret = sfc_sync_single_cdt_for_flash_locked(flash,
					&single_xfer, true);
			if (ret)
				return ret;

			index++;
			if (!linked)
				break;
		} while (index < INDEX_MAX_NUM);

		return 0;
	}

	return sfc_sync_single_cdt_for_flash_locked(flash, xfer, false);
}

static int sfc_flash_do_sync_cdt_locked(struct sfc_flash *flash,
		struct sfc_cdt_xfer *xfer, bool single)
{
	int ret;

	if (!flash || !flash->sfc)
		return -EINVAL;

	if (!flash->sfc->topology) {
		if (flash->prepare_profile_locked)
			return -ENODEV;
		return sfc_sync_cdt_raw(flash->sfc, xfer);
	}

	ret = 0;
	if (flash->prepare_profile_locked)
		ret = flash->prepare_profile_locked(flash);

	if (ret)
		return ret;

	if (single)
		return sfc_sync_single_cdt_for_flash_locked(flash, xfer, true);

	return sfc_sync_cdt_for_flash_locked(flash, xfer);
}

int sfc_flash_sync_cdt_once(struct sfc_flash *flash,
		struct sfc_cdt_xfer *xfer)
{
	struct sfc *sfc;
	int ret;

	if (!flash || !flash->sfc || !xfer)
		return -EINVAL;

	sfc = flash->sfc;
	if (sfc->bus_owner == current)
		return sfc_flash_do_sync_cdt_locked(flash, xfer, true);

	sfc_flash_bus_lock(flash);
	ret = sfc_flash_do_sync_cdt_locked(flash, xfer, true);
	sfc_flash_bus_unlock(flash);

	return ret;
}

int sfc_sync_cdt(struct sfc *sfc, struct sfc_cdt_xfer *xfer)
{
	struct sfc_flash *flash;
	int ret;

	if (!sfc || !xfer)
		return -EINVAL;

	flash = sfc->flash;
	if (!flash || !flash->sfc)
		return sfc_sync_cdt_raw(sfc, xfer);
	if (!flash->sfc->topology) {
		if (flash->prepare_profile_locked)
			return -ENODEV;
		return sfc_sync_cdt_raw(sfc, xfer);
	}

	if (sfc->bus_owner == current)
		return sfc_flash_do_sync_cdt_locked(flash, xfer, false);

	sfc_flash_bus_lock(flash);
	ret = sfc_flash_do_sync_cdt_locked(flash, xfer, false);
	sfc_flash_bus_unlock(flash);

	return ret;
}
