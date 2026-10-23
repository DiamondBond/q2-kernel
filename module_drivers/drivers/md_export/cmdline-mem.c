#define pr_fmt(fmt)	"cmdline-mem: " fmt

#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/mm.h>
#include <linux/sizes.h>
#include <linux/dma-map-ops.h>
#include <linux/mutex.h>

#define MAX_CMDLINE_MEMS	16
#define CMDLINE_MEM_NAME_LEN	32

struct cmdline_mem {
	char name[CMDLINE_MEM_NAME_LEN];
	phys_addr_t base;
	phys_addr_t size;
	struct dma_coherent_mem	*dma_mem;
};

static struct cmdline_mem cmdline_mem[MAX_CMDLINE_MEMS];
static int cmdline_mem_count;
static DEFINE_MUTEX(cmdline_mem_lock);

static int __init cmdline_mem_store(const char *name, char *p)
{
	phys_addr_t start = 0, size = 0;
	struct cmdline_mem *mem = NULL;

	if (!name || !*name) {
		pr_err("memory region name is missing\n");
		return 0;
	}

	if (strnlen(name, CMDLINE_MEM_NAME_LEN) >= CMDLINE_MEM_NAME_LEN) {
		pr_err("%s: memory region name is too long (maximum %d characters)\n",
			name, CMDLINE_MEM_NAME_LEN - 1);
		return 0;
	}

	if (!p || !*p) {
		pr_err("%s: empty parameter (expected <size>@<base>)\n", name);
		return 0;
	}

	size = memparse(p, &p);
	if (*p == '@')
		start = memparse(p + 1, &p);

	if (!start || !size) {
		pr_err("%s: invalid region, base=%pa, size=%pa\n", name, &start, &size);
		return 0;
	}

	if (cmdline_mem_count >= MAX_CMDLINE_MEMS) {
		pr_err("%s: too many regions (maximum %d)\n", name, MAX_CMDLINE_MEMS);
		return 0;
	}

	mem = &cmdline_mem[cmdline_mem_count++];
	strscpy(mem->name, name, CMDLINE_MEM_NAME_LEN);
	mem->base = start;
	mem->size = size;

	return 0;
}

/* 解析 <name>=... 参数（如 vpu_mem），存入 cmdline_mem 描述数组，供 dts 中 "cmdline,<name>" 节点匹配使用 */
#define CMDLINE_MEM_PARSER(name)				\
static int __init early_parse_##name(char *p)	\
{												\
	return cmdline_mem_store(#name, p);			\
}												\
early_param(#name, early_parse_##name);

/* 解析 uboot 通过命令行传入的 mem 参数 */
CMDLINE_MEM_PARSER(vpu_mem)
CMDLINE_MEM_PARSER(share_mem)
CMDLINE_MEM_PARSER(lcd_mem)
CMDLINE_MEM_PARSER(rtos_size)
CMDLINE_MEM_PARSER(rmem)

static int cmdline_mem_extract_name(const char *str, char *out, size_t size)
{
	const char *name_start;
	const char prefix[] = "cmdline,";
	size_t prefix_len = sizeof(prefix) - 1;

	if (!str || !out || !size) {
		pr_err("invalid args: str=%p, out=%p, size=%zu\n", str, out, size);
		return -EINVAL;
	}

	if (strncmp(str, prefix, prefix_len) != 0) {
		pr_err("invalid memory-region string \"%s\", expected prefix \"%s\"\n", str, prefix);
		return -EINVAL;
	}

	name_start = str + prefix_len;
	if (*name_start == '\0') {
		pr_err("empty memory name in memory-region string \"%s\"\n", str);
		return -EINVAL;
	}

	if (strnlen(name_start, size) >= size) {
		pr_err("memory name in memory-region string is too long\n");
		return -ENAMETOOLONG;
	}

	strscpy(out, name_start, size);
	return 0;
}

static struct cmdline_mem *cmdline_mem_find_by_name(const char *name)
{
	unsigned int i;

	if (!name)
		return NULL;

	for (i = 0; i < cmdline_mem_count; i++)
		if (strcmp(cmdline_mem[i].name, name) == 0)
			return &cmdline_mem[i];

	return NULL;
}

bool cmdline_mem_has_name(const char *name)
{
	return cmdline_mem_find_by_name(name) != NULL;
}
EXPORT_SYMBOL_GPL(cmdline_mem_has_name);

static bool cmdline_mem_ranges_overlap(phys_addr_t base1, phys_addr_t size1, phys_addr_t base2, phys_addr_t size2)
{
	if (!size1 || !size2)
		return false;

	/* Use differences instead of base + size, which may overflow. */
	if (base1 <= base2)
		return base2 - base1 < size1;

	return base1 - base2 < size2;
}

bool cmdline_mem_region_overlaps(phys_addr_t base, phys_addr_t size, const char **name)
{
	unsigned int i;
	bool overlap = false;

	if (name)
		*name = NULL;

	if (!size)
		return false;

	for (i = 0; i < cmdline_mem_count; i++) {
		if (!cmdline_mem_ranges_overlap(base, size,
						cmdline_mem[i].base,
						cmdline_mem[i].size))
			continue;

		if (name)
			*name = cmdline_mem[i].name;
		overlap = true;
		break;
	}

	return overlap;
}
EXPORT_SYMBOL_GPL(cmdline_mem_region_overlaps);

int of_cmdline_mem_device_init(struct device *dev)
{
	int ret;
	const char *str;
	char mem_name[CMDLINE_MEM_NAME_LEN];
	struct cmdline_mem *mem = NULL;

	if (!dev || !dev->of_node)
		return -EINVAL;

	if (dev->dma_mem) {
		dev_info(dev, "coherent DMA memory is already assigned\n");
		return 0;
	}

	if (!of_find_property(dev->of_node, "memory-region", NULL))
		return -ENODEV;

	ret = of_property_read_string(dev->of_node, "memory-region", &str);
	if (ret) {
		dev_err(dev, "invalid memory-region property\n");
		return -EINVAL;
	}

	ret = cmdline_mem_extract_name(str, mem_name, sizeof(mem_name));
	if (ret) {
		dev_err(dev, "failed to extract the mem name\n");
		return ret;
	}

	mem = cmdline_mem_find_by_name(mem_name);
	if (!mem) {
		dev_err(dev, "cmdline parameter %s not found\n", mem_name);
		return -EINVAL;
	}

	mutex_lock(&cmdline_mem_lock);
	if (!mem->dma_mem) {
		ret = dma_declare_coherent_memory(dev, mem->base, mem->base, mem->size);
		if (ret) {
			dev_err(dev, "failed to init DMA memory pool at %pa, size %ld MiB\n",
				&mem->base, (unsigned long)mem->size / SZ_1M);
			goto unlock;
		}

		mem->dma_mem = dev->dma_mem;
	} else {
		dev->dma_mem = mem->dma_mem;
	}
	ret = 0;

unlock:
	mutex_unlock(&cmdline_mem_lock);

	if (ret)
		return ret;

	dev_info(dev, "Cmdline memory: using DMA pool at %pa, size %ld MiB, %ld KiB\n",
		&mem->base,
		(unsigned long)mem->size / SZ_1M,
		(unsigned long)mem->size / SZ_1K);

	return 0;
}
EXPORT_SYMBOL_GPL(of_cmdline_mem_device_init);

void of_cmdline_mem_device_release(struct device *dev)
{
	const char *str;
	char mem_name[CMDLINE_MEM_NAME_LEN];
	struct cmdline_mem *mem;

	if (!dev || !dev->of_node)
		return;

	if (of_property_read_string(dev->of_node, "memory-region", &str))
		return;

	if (cmdline_mem_extract_name(str, mem_name, sizeof(mem_name)))
		return;

	mem = cmdline_mem_find_by_name(mem_name);
	if (!mem)
		return;

	mutex_lock(&cmdline_mem_lock);
	if (dev->dma_mem == mem->dma_mem)
		dev->dma_mem = NULL;
	mutex_unlock(&cmdline_mem_lock);
}
EXPORT_SYMBOL_GPL(of_cmdline_mem_device_release);
