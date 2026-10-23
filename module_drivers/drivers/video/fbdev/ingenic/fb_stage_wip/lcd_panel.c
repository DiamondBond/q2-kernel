// SPDX-License-Identifier: GPL-2.0
/*
 * fb_stage_wip 公共 lcd_panel 注册表。
 *
 * panel 驱动按自身 device_node 注册 lcd_panel，DPU/LCDC 等控制器通过
 * DTS port/endpoint 找到远端 panel 节点后，再从这里取回 lcd_panel。
 * 这样不会改变 panel 驱动原有的 drvdata 私有数据语义。
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/gfp.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/slab.h>

#include "lcd_panel.h"

struct ingenic_lcd_panel_entry {
	struct list_head list;
	struct device *dev;
	struct device_node *of_node;	/* panel 设备节点，作为公共查找键 */
	struct lcd_panel *panel;
};

struct ingenic_lcd_panel_candidate {
	unsigned int endpoint;
	struct lcd_panel *panel;
};

/* panel probe 和控制器 probe 可能并发，注册表需要统一加锁保护。 */
static LIST_HEAD(ingenic_lcd_panel_list);
static DEFINE_MUTEX(ingenic_lcd_panel_lock);

/*
 * 注册 panel 到公共表。
 * dev->of_node 是 DTS graph 最终匹配到的 panel 节点，registry 只保存
 * lcd_panel 指针，不接管 lcd_panel 的生命周期。
 */
int ingenic_lcd_panel_register(struct device *dev, struct lcd_panel *panel)
{
	struct ingenic_lcd_panel_entry *entry;
	struct ingenic_lcd_panel_entry *cur;

	if (!dev || !panel)
		return -EINVAL;

	if (!dev->of_node)
		return -ENODEV;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->dev = dev;
	entry->of_node = of_node_get(dev->of_node);
	entry->panel = panel;

	mutex_lock(&ingenic_lcd_panel_lock);
	list_for_each_entry(cur, &ingenic_lcd_panel_list, list) {
		/* 同一个设备重复注册时更新 panel 指针，避免重复挂链表。 */
		if (cur->dev == dev) {
			cur->panel = panel;
			mutex_unlock(&ingenic_lcd_panel_lock);
			of_node_put(entry->of_node);
			kfree(entry);
			return 0;
		}

		/* 同一个 DTS 节点不允许被两个 panel provider 同时占用。 */
		if (cur->of_node == dev->of_node) {
			mutex_unlock(&ingenic_lcd_panel_lock);
			of_node_put(entry->of_node);
			kfree(entry);
			return -EBUSY;
		}
	}
	list_add_tail(&entry->list, &ingenic_lcd_panel_list);
	mutex_unlock(&ingenic_lcd_panel_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(ingenic_lcd_panel_register);

/* panel remove 或错误回滚时注销公共表项，释放注册时持有的 of_node 引用。 */
void ingenic_lcd_panel_unregister(struct device *dev)
{
	struct ingenic_lcd_panel_entry *entry, *tmp;

	if (!dev)
		return;

	mutex_lock(&ingenic_lcd_panel_lock);
	list_for_each_entry_safe(entry, tmp, &ingenic_lcd_panel_list, list) {
		if (entry->dev != dev)
			continue;

		list_del(&entry->list);
		mutex_unlock(&ingenic_lcd_panel_lock);

		of_node_put(entry->of_node);
		kfree(entry);
		return;
	}
	mutex_unlock(&ingenic_lcd_panel_lock);
}
EXPORT_SYMBOL_GPL(ingenic_lcd_panel_unregister);

/* 控制器已经拿到 panel 节点时，按 device_node 精确查找 lcd_panel。 */
struct lcd_panel *ingenic_lcd_panel_find_by_node(struct device_node *np)
{
	struct ingenic_lcd_panel_entry *entry;
	struct lcd_panel *panel = NULL;

	if (!np)
		return NULL;

	mutex_lock(&ingenic_lcd_panel_lock);
	list_for_each_entry(entry, &ingenic_lcd_panel_list, list) {
		if (entry->of_node == np) {
			panel = entry->panel;
			break;
		}
	}
	mutex_unlock(&ingenic_lcd_panel_lock);

	return panel;
}
EXPORT_SYMBOL_GPL(ingenic_lcd_panel_find_by_node);

static void ingenic_lcd_panel_insert_candidate(struct ingenic_lcd_panel_candidate *candidates,
					       int *count, unsigned int endpoint,
					       struct lcd_panel *panel)
{
	int pos = *count;

	/* endpoint reg 决定读 ID 尝试顺序，reg 小的候选先尝试。 */
	while (pos > 0 && candidates[pos - 1].endpoint > endpoint) {
		candidates[pos] = candidates[pos - 1];
		pos--;
	}

	candidates[pos].endpoint = endpoint;
	candidates[pos].panel = panel;
	(*count)++;
}

/*
 * 控制器侧候选入口：遍历 controller 指定 port 下的全部 endpoint，
 * 将每个 remote panel 转成 lcd_panel 候选列表。返回值大于 0 表示候选
 * 数量；返回 0 表示没有 graph，可继续旧注册方式；返回 -EPROBE_DEFER
 * 表示 DTS 已声明候选关系，但 panel provider 还没全部注册完成。
 *
 * controller_np: DPU/LCDC 等控制器的 device_node。
 * port: 控制器输出口编号，对应 DTS 里的 port@N/reg = <N>。
 * panels: 输出数组，保存匹配到的 lcd_panel 指针。
 * max_panels: 输出数组容量，防止候选数量超过调用者缓冲区。
 *
 * 典型 DTS 结构：
 * &dpu/ports/port@0/endpoint@0 remote 到 panel_fw050_ep；
 * &dpu/ports/port@0/endpoint@1 remote 到 panel_zc50289hshd02_ep。
 * 本接口会把这两个 endpoint 转成两个候选 panel，后续由 fbdev 读 ID
 * 决定真正使用哪一个。
 */
int ingenic_lcd_panel_find_candidates_by_graph(struct device_node *controller_np,
					       int port, struct lcd_panel **panels,
					       int max_panels)
{
	struct ingenic_lcd_panel_candidate candidates[INGENIC_LCD_PANEL_MAX_CANDIDATES];
	struct device_node *ep = NULL;
	struct device_node *panel_np;
	struct lcd_panel *panel;
	struct of_endpoint endpoint;
	int count = 0;
	int found = 0;
	int missing = 0;
	int i;
	int ret;

	if (!controller_np || !of_graph_is_present(controller_np))
		return 0;

	if (!panels || max_panels <= 0)
		return -EINVAL;

	if (max_panels > INGENIC_LCD_PANEL_MAX_CANDIDATES)
		max_panels = INGENIC_LCD_PANEL_MAX_CANDIDATES;

	/*
	 * 遍历控制器节点下所有 endpoint。这里不直接假设 endpoint@0，
	 * 因为同一个 port 可以挂多个候选 panel，需要全部收集后逐个尝试。
	 */
	while ((ep = of_graph_get_next_endpoint(controller_np, ep))) {
		ret = of_graph_parse_endpoint(ep, &endpoint);
		/* 只处理调用者指定的 port，其他输出口留给对应控制器逻辑处理。 */
		if (ret || endpoint.port != port)
			continue;

		found++;
		if (count >= max_panels) {
			of_node_put(ep);
			return -ENOSPC;
		}

		/*
		 * remote-endpoint 指向 panel 端 endpoint，这里取它的父节点，
		 * 得到真正注册 ingenic_lcd_panel_register() 的 panel 节点。
		 */
		panel_np = of_graph_get_remote_port_parent(ep);
		if (!panel_np) {
			missing++;
			continue;
		}

		/* status = "disabled" 的候选 panel 不参与本次选择。 */
		if (!of_device_is_available(panel_np)) {
			of_node_put(panel_np);
			continue;
		}

		/*
		 * panel 驱动 probe 后会按自己的 device_node 注册到公共表。
		 * 如果 DTS 已声明 remote panel，但这里还查不到，说明 panel
		 * provider 尚未 probe 完成，调用者需要 -EPROBE_DEFER。
		 */
		panel = ingenic_lcd_panel_find_by_node(panel_np);
		of_node_put(panel_np);
		if (!panel) {
			missing++;
			continue;
		}

		/* 按 endpoint reg 排序，reg 小的候选会被 fbdev 优先读 ID。 */
		ingenic_lcd_panel_insert_candidate(candidates, &count,
						   endpoint.id, panel);
	}

	/* controller 有 graph，但指定 port 下没有 endpoint，视为 graph 配置无效。 */
	if (!found)
		return -EINVAL;

	/* DTS 有可用候选但尚未全部注册，延后控制器 probe 等 panel provider。 */
	if (missing)
		return -EPROBE_DEFER;

	/* 将局部排序结果复制给调用者，返回候选 panel 数量。 */
	for (i = 0; i < count; i++)
		panels[i] = candidates[i].panel;

	return count;
}
EXPORT_SYMBOL_GPL(ingenic_lcd_panel_find_candidates_by_graph);
