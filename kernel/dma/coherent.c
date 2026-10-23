// SPDX-License-Identifier: GPL-2.0
/*
 * Coherent per-device memory handling.
 * Borrowed from i386
 */
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dma-direct.h>
#include <linux/dma-map-ops.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>


struct dma_coherent_mem {
	void		*virt_base;
	dma_addr_t	device_base;
	unsigned long	pfn_base;
	int		size;
	unsigned long	*bitmap;
	spinlock_t	spinlock;
	bool		use_dev_dma_pfn_offset;

	/*For Debug*/
	unsigned long 	allocated_size;
	unsigned long	max_allocated_size;
	struct list_head entry;
	struct list_head node_list;
};

struct alloc_node {
	struct list_head entry;		/*当前节点的list*/
	struct device *dev;		/*当前内存是哪个设备分配的.*/
	unsigned long addr;		/*当前分配的起始地址*/
	unsigned long size;		/*当前分配的大小*/
};


static LIST_HEAD(dma_mem_list);


static int trace_buffer_allocate = 0;
module_param(trace_buffer_allocate, int,  S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(trace_buffer_allocate, "record buffer allocated info for debug");


static int dma_coherent_debug_show(struct seq_file *m, void *unused)
{

	struct alloc_node *n, *t;
	struct dma_coherent_mem *mem, *tmem;

	seq_puts(m, "dma coherent alloc info\n");

	list_for_each_entry_safe(mem, tmem, &dma_mem_list, entry) {
		seq_printf(m, " dma_mm: %lx, <virt:%lx size: %d pages %ldKBytes>\n", (unsigned long)mem, (unsigned long)mem->virt_base, mem->size, mem->size * PAGE_SIZE / 1024);
		seq_printf(m, " max_allocated_size: %ld, current allocated_size: %ld\n", mem->max_allocated_size, mem->allocated_size);
		list_for_each_entry_safe(n, t, &mem->node_list, entry) {
			seq_printf(m, "\tdev:%s, addr: %08lx, size: %ld\n", dev_name(n->dev), n->addr, n->size);

		}

	}

	return 0;
}

static int dma_coherent_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, dma_coherent_debug_show, NULL);
}

static const struct file_operations dma_coherent_debug_fops = {
	.owner = THIS_MODULE,
	.open = dma_coherent_debug_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static struct dentry *dma_coherent_alloc_dentry;

static int __init dma_coherent_debugfs_init(void)
{
	dma_coherent_alloc_dentry = debugfs_create_file("dma_coherent_mm",
							S_IRUGO, NULL, NULL, &dma_coherent_debug_fops);
	return 0;
}

late_initcall(dma_coherent_debugfs_init);

static struct dma_coherent_mem *dma_coherent_default_memory __ro_after_init;

static inline struct dma_coherent_mem *dev_get_coherent_memory(struct device *dev)
{
	if (dev && dev->dma_mem)
		return dev->dma_mem;
	return NULL;
}

static inline dma_addr_t dma_get_device_base(struct device *dev,
					     struct dma_coherent_mem * mem)
{
	if (mem->use_dev_dma_pfn_offset)
		return phys_to_dma(dev, PFN_PHYS(mem->pfn_base));
	return mem->device_base;
}

static int dma_init_coherent_memory(phys_addr_t phys_addr,
		dma_addr_t device_addr, size_t size,
		struct dma_coherent_mem **mem)
{
	struct dma_coherent_mem *dma_mem = NULL;
	void *mem_base = NULL;
	int pages = size >> PAGE_SHIFT;
	int bitmap_size = BITS_TO_LONGS(pages) * sizeof(long);
	int ret;

	if (!size) {
		ret = -EINVAL;
		goto out;
	}

	//mem_base = memremap(phys_addr, size, MEMREMAP_WB);
	mem_base = ioremap(phys_addr, size);
	if (!mem_base) {
		ret = -EINVAL;
		goto out;
	}

	dma_mem = kzalloc(sizeof(struct dma_coherent_mem), GFP_KERNEL);
	if (!dma_mem) {
		ret = -ENOMEM;
		goto out;
	}
	dma_mem->bitmap = kzalloc(bitmap_size, GFP_KERNEL);
	if (!dma_mem->bitmap) {
		ret = -ENOMEM;
		goto out;
	}

	dma_mem->virt_base = mem_base;
	dma_mem->device_base = device_addr;
	dma_mem->pfn_base = PFN_DOWN(phys_addr);
	dma_mem->size = pages;
	spin_lock_init(&dma_mem->spinlock);

	*mem = dma_mem;

	INIT_LIST_HEAD(&dma_mem->entry);
	INIT_LIST_HEAD(&dma_mem->node_list);
	list_add_tail(&dma_mem->entry, &dma_mem_list);

	return 0;

out:
	kfree(dma_mem);
	if (mem_base)
		iounmap(mem_base);
	return ret;
}

static void dma_release_coherent_memory(struct dma_coherent_mem *mem)
{
	if (!mem)
		return;

	iounmap(mem->virt_base);
	kfree(mem->bitmap);
	kfree(mem);
}

static int dma_assign_coherent_memory(struct device *dev,
				      struct dma_coherent_mem *mem)
{
	if (!dev)
		return -ENODEV;

	if (dev->dma_mem)
		return -EBUSY;

	dev->dma_mem = mem;
	return 0;
}

int dma_coherent_mem_size(struct device *dev)
{
	struct dma_coherent_mem *mem = dev_get_coherent_memory(dev);
	if(mem == NULL)
		return 0;
	return mem->size << PAGE_SHIFT;
}

int dma_coherent_mem_available(struct device *dev)
{
	struct dma_coherent_mem *mem = dev_get_coherent_memory(dev);
	unsigned int pos;		/* scans bitmap by regions of size order */
	int bitmap_size;
	unsigned long total = 0;

	if(mem == NULL)//先判空后才可以访问
		return 0;

	bitmap_size = (mem->size + 31) / (sizeof(unsigned long) * 8);

	/*step = 1 << 5 pages. */
	for (pos = 0; pos < bitmap_size; pos++) {
		unsigned long val = mem->bitmap[pos];
		if(val) {
			val = (val & 0x55555555) + ((val >> 1) & 0x55555555);
			val = (val & 0x33333333) + ((val >> 2) & 0x33333333);
			val = (val & 0x0f0f0f0f) + ((val >> 4) & 0x0f0f0f0f);
			val = (val & 0x00ff00ff) + ((val >> 8) & 0x00ff00ff);
			val = (val & 0x0000ffff) + ((val >> 16) & 0x0000ffff);
			total += val;
		}
	}
	total = (mem->size - total) << PAGE_SHIFT;
	return total;
}

/*
 * Declare a region of memory to be handed out by dma_alloc_coherent() when it
 * is asked for coherent memory for this device.  This shall only be used
 * from platform code, usually based on the device tree description.
 *
 * phys_addr is the CPU physical address to which the memory is currently
 * assigned (this will be ioremapped so the CPU can access the region).
 *
 * device_addr is the DMA address the device needs to be programmed with to
 * actually address this memory (this will be handed out as the dma_addr_t in
 * dma_alloc_coherent()).
 *
 * size is the size of the area (must be a multiple of PAGE_SIZE).
 *
 * As a simplification for the platforms, only *one* such region of memory may
 * be declared per device.
 */
int dma_declare_coherent_memory(struct device *dev, phys_addr_t phys_addr,
				dma_addr_t device_addr, size_t size)
{
	struct dma_coherent_mem *mem;
	int ret;

	ret = dma_init_coherent_memory(phys_addr, device_addr, size, &mem);
	if (ret)
		return ret;

	ret = dma_assign_coherent_memory(dev, mem);
	if (ret)
		dma_release_coherent_memory(mem);
	return ret;
}

static int dma_bitmap_find_free_region(unsigned long *bitmap, unsigned int bits, unsigned int pages)
{
	unsigned int pos, end;		/* scans bitmap by regions of size order */
	unsigned long start = 0;

	/*step = 1 << 5 pages. */
	for (pos = 0; (end = pos + (1U << 5)) <= bits; pos = end) {
		start = bitmap_find_next_zero_area(bitmap, bits, pos, pages, 0);
		if( start > bits) {
			return -ENOMEM;
		}

		break;
	}

	//ALLOC buffer.
	bitmap_set(bitmap, start, pages);

	return start;
}

static int dma_bitmap_release_region(unsigned long *bitmap, unsigned int bits, int pages)
{
	bitmap_clear(bitmap, bits, pages);

	return 0;
}


static void *__dma_alloc_from_coherent(struct device *dev,
				       struct dma_coherent_mem *mem,
				       ssize_t size, dma_addr_t *dma_handle)
{
	unsigned int pages = ALIGN(size, PAGE_SIZE) >> PAGE_SHIFT;
	unsigned long flags;
	int pageno;
	void *ret;

	spin_lock_irqsave(&mem->spinlock, flags);
	/* dma_handle need an invalid value if alloc fault */
	*dma_handle = 0;

	if (unlikely(size > ((dma_addr_t)mem->size << PAGE_SHIFT)))
		goto err;

	//pageno = bitmap_find_free_region(mem->bitmap, mem->size, order);
	pageno = dma_bitmap_find_free_region(mem->bitmap, mem->size, pages);
	if (unlikely(pageno < 0))
		goto err;

	/*
	 * Memory was found in the coherent area.
	 */
	*dma_handle = dma_get_device_base(dev, mem) +
			((dma_addr_t)pageno << PAGE_SHIFT);
	ret = mem->virt_base + ((dma_addr_t)pageno << PAGE_SHIFT);

	dev_dbg(dev, "%s, allocated@ 0x%p, start: %d, pages: %d\n", __func__, ret, pageno, pages);

	if(trace_buffer_allocate == 1) {
		struct alloc_node *n = kzalloc(sizeof(struct alloc_node), GFP_KERNEL);
		n->dev = dev;
		n->addr = (unsigned long)ret;
		n->size = pages;
		INIT_LIST_HEAD(&n->entry);
		list_add_tail(&n->entry, &mem->node_list);
	}
	mem->allocated_size += pages;
	/*
	  记录最大分配过的内存.
	*/
	if(mem->max_allocated_size < mem->allocated_size) {
		mem->max_allocated_size = mem->allocated_size;
	}

	spin_unlock_irqrestore(&mem->spinlock, flags);
	//memset(ret, 0, size);
	return ret;
err:
	spin_unlock_irqrestore(&mem->spinlock, flags);
	return NULL;
}

/**
 * dma_alloc_from_dev_coherent() - allocate memory from device coherent pool
 * @dev:	device from which we allocate memory
 * @size:	size of requested memory area
 * @dma_handle:	This will be filled with the correct dma handle
 * @ret:	This pointer will be filled with the virtual address
 *		to allocated area.
 *
 * This function should be only called from per-arch dma_alloc_coherent()
 * to support allocation from per-device coherent memory pools.
 *
 * Returns 0 if dma_alloc_coherent should continue with allocating from
 * generic memory areas, or !0 if dma_alloc_coherent should return @ret.
 */
int dma_alloc_from_dev_coherent(struct device *dev, ssize_t size,
		dma_addr_t *dma_handle, void **ret)
{
	struct dma_coherent_mem *mem = dev_get_coherent_memory(dev);

	if (!mem)
		return 0;

	*ret = __dma_alloc_from_coherent(dev, mem, size, dma_handle);
	return 1;
}

void *dma_alloc_from_global_coherent(struct device *dev, ssize_t size,
				     dma_addr_t *dma_handle)
{
	if (!dma_coherent_default_memory)
		return NULL;

	return __dma_alloc_from_coherent(dev, dma_coherent_default_memory, size,
					 dma_handle);
}

static int __dma_release_from_coherent(struct device *dev, struct dma_coherent_mem *mem,
				       ssize_t size, void *vaddr)
{
	unsigned int pages = ALIGN(size, PAGE_SIZE) >> PAGE_SHIFT;
	if (mem && vaddr >= mem->virt_base && vaddr <
		   (mem->virt_base + ((dma_addr_t)mem->size << PAGE_SHIFT))) {
		int page = (vaddr - mem->virt_base) >> PAGE_SHIFT;
		unsigned long flags;

		spin_lock_irqsave(&mem->spinlock, flags);
		dma_bitmap_release_region(mem->bitmap, page, pages);
		mem->allocated_size -= pages;
		if(trace_buffer_allocate == 1) {
			struct alloc_node *n, *t;
			list_for_each_entry_safe(n, t, &mem->node_list, entry) {
				if(n->dev == dev
				   && n->addr == (unsigned long)vaddr
				   && n->size == pages) {
					/*found node*/
					list_del(&n->entry);
					kfree(n);
				}
			}
		}
		spin_unlock_irqrestore(&mem->spinlock, flags);
		return 1;
	}
	return 0;
}

/**
 * dma_release_from_dev_coherent() - free memory to device coherent memory pool
 * @dev:	device from which the memory was allocated
 * @order:	the order of pages allocated
 * @vaddr:	virtual address of allocated pages
 *
 * This checks whether the memory was allocated from the per-device
 * coherent memory pool and if so, releases that memory.
 *
 * Returns 1 if we correctly released the memory, or 0 if the caller should
 * proceed with releasing memory from generic pools.
 */
int dma_release_from_dev_coherent(struct device *dev, ssize_t size, void *vaddr)
{
	struct dma_coherent_mem *mem = dev_get_coherent_memory(dev);

	dev_dbg(dev, "%s, vaddr: %p, size: %d\n", __func__, vaddr, size);

	return __dma_release_from_coherent(dev, mem, size, vaddr);
}

int dma_release_from_global_coherent(ssize_t size, void *vaddr)
{
	if (!dma_coherent_default_memory)
		return 0;

	return __dma_release_from_coherent(NULL, dma_coherent_default_memory, size,
			vaddr);
}

static int __dma_mmap_from_coherent(struct dma_coherent_mem *mem,
		struct vm_area_struct *vma, void *vaddr, size_t size, int *ret)
{
	if (mem && vaddr >= mem->virt_base && vaddr + size <=
		   (mem->virt_base + ((dma_addr_t)mem->size << PAGE_SHIFT))) {
		unsigned long off = vma->vm_pgoff;
		int start = (vaddr - mem->virt_base) >> PAGE_SHIFT;
		unsigned long user_count = vma_pages(vma);
		int count = PAGE_ALIGN(size) >> PAGE_SHIFT;

		*ret = -ENXIO;
		if (off < count && user_count <= count - off) {
			unsigned long pfn = mem->pfn_base + start + off;
			*ret = remap_pfn_range(vma, vma->vm_start, pfn,
					       user_count << PAGE_SHIFT,
					       vma->vm_page_prot);
		}
		return 1;
	}
	return 0;
}

/**
 * dma_mmap_from_dev_coherent() - mmap memory from the device coherent pool
 * @dev:	device from which the memory was allocated
 * @vma:	vm_area for the userspace memory
 * @vaddr:	cpu address returned by dma_alloc_from_dev_coherent
 * @size:	size of the memory buffer allocated
 * @ret:	result from remap_pfn_range()
 *
 * This checks whether the memory was allocated from the per-device
 * coherent memory pool and if so, maps that memory to the provided vma.
 *
 * Returns 1 if @vaddr belongs to the device coherent pool and the caller
 * should return @ret, or 0 if they should proceed with mapping memory from
 * generic areas.
 */
int dma_mmap_from_dev_coherent(struct device *dev, struct vm_area_struct *vma,
			   void *vaddr, size_t size, int *ret)
{
	struct dma_coherent_mem *mem = dev_get_coherent_memory(dev);

	return __dma_mmap_from_coherent(mem, vma, vaddr, size, ret);
}

int dma_mmap_from_global_coherent(struct vm_area_struct *vma, void *vaddr,
				   size_t size, int *ret)
{
	if (!dma_coherent_default_memory)
		return 0;

	return __dma_mmap_from_coherent(dma_coherent_default_memory, vma,
					vaddr, size, ret);
}

/*
 * Support for reserved memory regions defined in device tree
 */
#ifdef CONFIG_OF_RESERVED_MEM
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_reserved_mem.h>

static struct reserved_mem *dma_reserved_default_memory __initdata;

static int rmem_dma_device_init(struct reserved_mem *rmem, struct device *dev)
{
	struct dma_coherent_mem *mem = rmem->priv;
	int ret;

	if (!mem) {
		ret = dma_init_coherent_memory(rmem->base, rmem->base,
					       rmem->size, &mem);
		if (ret) {
			pr_err("Reserved memory: init DMA memory pool at %pa, size %ld MiB, %ld KiB [FAILED]\n",
				&rmem->base, (unsigned long)rmem->size / SZ_1M, (unsigned long)rmem->size / SZ_1K);
			return ret;
		}
	}
	mem->use_dev_dma_pfn_offset = true;
	rmem->priv = mem;
	dma_assign_coherent_memory(dev, mem);
	return 0;
}

static void rmem_dma_device_release(struct reserved_mem *rmem,
				    struct device *dev)
{
	if (dev)
		dev->dma_mem = NULL;
}

static const struct reserved_mem_ops rmem_dma_ops = {
	.device_init	= rmem_dma_device_init,
	.device_release	= rmem_dma_device_release,
};

static int __init rmem_dma_setup(struct reserved_mem *rmem)
{
	unsigned long node = rmem->fdt_node;

	/*
	 * 默认的Reserved memory管理 (模块独享内存)
	 *
	 * 若dts还设置了 reusable属性，则此处退出，此类型Reserved memory管理初始失败
	*/
	if (of_get_flat_dt_prop(node, "reusable", NULL)) {
		pr_err("Reserved memory: DMA memory pool config matching [FAILED]\n");
		return -EINVAL;
	}

#ifdef CONFIG_ARM
	if (!of_get_flat_dt_prop(node, "no-map", NULL)) {
		pr_err("Reserved memory: regions without no-map are not yet supported\n");
		return -EINVAL;
	}

	if (of_get_flat_dt_prop(node, "linux,dma-default", NULL)) {
		WARN(dma_reserved_default_memory,
		     "Reserved memory: region for default DMA coherent area is redefined\n");
		dma_reserved_default_memory = rmem;
	}
#endif

	rmem->ops = &rmem_dma_ops;
	pr_info("Reserved memory: created DMA memory pool at %pa, size %ld MiB, %ld KiB [OK]\n",
		&rmem->base, (unsigned long)rmem->size / SZ_1M, (unsigned long)rmem->size / SZ_1K);
	return 0;
}

static int __init dma_init_reserved_memory(void)
{
	const struct reserved_mem_ops *ops;
	int ret;

	if (!dma_reserved_default_memory)
		return -ENOMEM;

	ops = dma_reserved_default_memory->ops;

	/*
	 * We rely on rmem_dma_device_init() does not propagate error of
	 * dma_assign_coherent_memory() for "NULL" device.
	 */
	ret = ops->device_init(dma_reserved_default_memory, NULL);

	if (!ret) {
		dma_coherent_default_memory = dma_reserved_default_memory->priv;
		pr_info("DMA: default coherent area is set\n");
	}

	return ret;
}

core_initcall(dma_init_reserved_memory);

RESERVEDMEM_OF_DECLARE(dma, "shared-dma-pool", rmem_dma_setup);
#endif
