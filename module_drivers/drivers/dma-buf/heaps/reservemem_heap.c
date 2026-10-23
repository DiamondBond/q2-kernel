#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/dma-heap.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <asm/page.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/of_reserved_mem.h>
#include "heap-helpers.h"

/**
 * usage:
 * 1. dts:
 *      添加 reserved-memory 例如:
        reserved-memory {
	         sharedmem_memory: sharedmem_mem@0x5E00000{
	         compatible = "shared-dma-pool";
			 //  startAddress  size
	          reg = <0x05C00000 0x0400000>;
	        };
		};
 *    添加设备节点,例如:
       sharedmem {
           status = "okay";
           compatible = "ingenic,sharedmem";
           ingenic,devname = "sharedmem";
           memory-region = <&sharedmem_memory>;
       };
  * 2. 用户层
  *       frameworks/reserveheap
  */

struct dma_buffer_addr
{
	struct device *dev;
	void *vaddr;
	dma_addr_t paddr;
	int size;
};

struct attachmemlist
{
	struct list_head list;
	struct dma_buf_attachment *attach;
	struct dma_buf *dbuf;
	struct sg_table *sgt;
	int fd;
};


struct sharedmem
{
	struct miscdevice mdev;
	struct device *dev;
	struct dma_heap *sys_heap;
	const char* devname;
};

struct filedata
{
	struct list_head phymap_top;
	struct mutex mtx;
	struct sharedmem *smem;
};

static void reservemem_heap_free(struct heap_helper_buffer *buffer)
{
	struct dma_buffer_addr *dma_buffer = (struct dma_buffer_addr *)buffer->priv_virt;

	dma_free_coherent(dma_buffer->dev,dma_buffer->size,dma_buffer->vaddr,dma_buffer->paddr);
	kfree(dma_buffer);
	kfree(buffer->pages);
	kfree(buffer);
}

static int reservemem_heap_allocate(struct dma_heap *heap,
				unsigned long len,
				unsigned long fd_flags,
				unsigned long heap_flags)
{
	struct sharedmem *smem = dma_heap_get_drvdata(heap);
	struct heap_helper_buffer *helper_buffer;
	struct dma_buf *dmabuf;
	void *vaddr;
	dma_addr_t paddr;
	struct page *p;
	unsigned long pfn;
	struct dma_buffer_addr *dma_buffer;
	int ret = -ENOMEM;
	pgoff_t pg;

	helper_buffer = kzalloc(sizeof(*helper_buffer), GFP_KERNEL);
	if (!helper_buffer)
		return -ENOMEM;

	init_heap_helper_buffer(helper_buffer, reservemem_heap_free);
	helper_buffer->heap = heap;
	helper_buffer->size = len;

	helper_buffer->pagecount = len / PAGE_SIZE;
	helper_buffer->pages = kmalloc_array(helper_buffer->pagecount,
					     sizeof(*helper_buffer->pages),
					     GFP_KERNEL);
	if (!helper_buffer->pages) {
		ret = -ENOMEM;
		goto err0;
	}

	vaddr = dma_alloc_coherent(smem->dev,len,&paddr,GFP_KERNEL);
	pfn = __phys_to_pfn(paddr);
	p = pfn_to_page(pfn);

	for (pg = 0; pg < helper_buffer->pagecount; pg++) {
		helper_buffer->pages[pg] = p;
		p++;
	}

	/* create the dmabuf */
	dmabuf = heap_helper_export_dmabuf(helper_buffer, fd_flags);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto err1;
	}

	dma_buffer = kzalloc(sizeof(*dma_buffer),GFP_KERNEL);
	if(!dma_buffer) {
		ret = -ENOMEM;
		goto err1;
	}
	dma_buffer->dev = smem->dev;
	dma_buffer->paddr = paddr;
	dma_buffer->vaddr = vaddr;
	dma_buffer->size = len;

	helper_buffer->dmabuf = dmabuf;
	helper_buffer->priv_virt = dma_buffer;

	ret = dma_buf_fd(dmabuf, fd_flags);
	if (ret < 0) {
		dma_buf_put(dmabuf);
		/* just return, as put will call release and that will free */
		return ret;
	}
	return ret;

err1:
	dma_free_coherent(smem->dev,len,vaddr,paddr);
	kfree(helper_buffer->pages);
err0:
	kfree(helper_buffer);
	return ret;
}

static const struct dma_heap_ops reservemem_heap_ops = {
	.allocate = reservemem_heap_allocate,
};

static int reservemem_heap_create(struct sharedmem *smem)
{
	struct dma_heap_export_info exp_info;
	int ret = 0;

	exp_info.name = smem->devname;
	exp_info.ops = &reservemem_heap_ops;
	exp_info.priv = smem;

	smem->sys_heap = dma_heap_add(&exp_info);
	if (IS_ERR(smem->sys_heap))
		ret = PTR_ERR(smem->sys_heap);

	return ret;
}


static int sharedmem_dmabuf_attach_phy(struct device *dev,struct filedata *file_data,int fd,unsigned long *phyaddr)
{
	struct dma_buf_attachment *attach = NULL;
	struct dma_buf *dbuf  = NULL;
	struct sg_table *sgt  = NULL;
	struct attachmemlist *pos;
	int err = 0;
	mutex_lock(&file_data->mtx);
	list_for_each_entry(pos,&file_data->phymap_top,list) {
		if(pos->fd == fd) {
			attach = pos->attach;
			dbuf = pos->dbuf;
			sgt = pos->sgt;
			break;
		}
	}

	if(sgt) {
		*phyaddr = sg_dma_address(sgt->sgl);
		mutex_unlock(&file_data->mtx);
		return 0;
	}

	dbuf = dma_buf_get(fd);
	if (IS_ERR(dbuf)) {
		err = -EINVAL;
		goto error;
	}
	attach = dma_buf_attach(dbuf, dev);
	if (IS_ERR(attach)) {
		err = -EINVAL;
		goto error;
	}

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		err = -EINVAL;
		goto error;
	}

	*phyaddr = sg_dma_address(sgt->sgl);

	pos = devm_kzalloc(dev,sizeof(struct attachmemlist), GFP_KERNEL);
	pos->attach = attach;
	pos->dbuf = dbuf;
	pos->fd = fd;
	pos->sgt = sgt;
	list_add_tail(&pos->list,&file_data->phymap_top);
	mutex_unlock(&file_data->mtx);
	return 0;
error:
	if(sgt)
		dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
	if(attach)
		dma_buf_detach(dbuf, attach);
	if(dbuf)
		dma_buf_put(dbuf);
	mutex_unlock(&file_data->mtx);
	return err;
}

static int sharedmem_dmabuf_deattach_phy(struct device *dev,struct filedata *file_data,int fd)
{
	struct dma_buf_attachment *attach = NULL;
	struct dma_buf *dbuf  = NULL;
	struct sg_table *sgt  = NULL;
	struct attachmemlist *pos;
	mutex_lock(&file_data->mtx);
	list_for_each_entry(pos,&file_data->phymap_top,list) {
		if(pos->fd == fd) {
			attach = pos->attach;
			dbuf = pos->dbuf;
			sgt = pos->sgt;
			list_del(&pos->list);
			devm_kfree(dev,pos);
			break;
		}
	}
	if(sgt)
		dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
	if(attach)
		dma_buf_detach(dbuf, attach);
	if(dbuf)
		dma_buf_put(dbuf);

	mutex_unlock(&file_data->mtx);
	return 0;
}

static int sharedmem_open(struct inode *inode, struct file *file)
{
	struct miscdevice *mdev = file->private_data;
    struct sharedmem *smem = container_of(mdev, struct sharedmem, mdev);
	struct filedata *file_data = devm_kzalloc(smem->dev,sizeof(struct filedata), GFP_KERNEL);
	if(IS_ERR_OR_NULL(file_data)) {
		dev_err(smem->dev,"file data alloc failed,no memory\n");
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&file_data->phymap_top);
	mutex_init(&file_data->mtx);
	file_data->smem = smem;
	file->private_data = file_data;
	// dev_info(smem->dev,"sharedmem open! %s:%d\n",current->comm,current->tgid);
	return 0;
}

static int sharedmem_release(struct inode *inode, struct file *file)
{
	struct filedata *file_data = (struct filedata *)file->private_data;
    struct sharedmem *smem = file_data->smem;
	struct attachmemlist *pos,*n;
	mutex_lock(&file_data->mtx);
	list_for_each_entry_safe(pos,n,&file_data->phymap_top,list) {
		if(pos->sgt) {
			dma_buf_unmap_attachment(pos->attach, pos->sgt, DMA_BIDIRECTIONAL);
		}
		if(pos->attach) {
			dma_buf_detach(pos->dbuf, pos->attach);
		}
		if(pos->dbuf) {
			dma_buf_put(pos->dbuf);
		}
		list_del(&pos->list);
		devm_kfree(smem->dev,pos);
	}

	mutex_unlock(&file_data->mtx);
	devm_kfree(smem->dev,file_data);
	// dev_info(smem->dev,"sharedmem close! %s:%d\n",current->comm,current->tgid);
	return 0;
}

#define SHAREDMEM_ATTACH_PHY_ADDR		_IOW('F', 0x1, unsigned int)
#define SHAREDMEM_DEATTACH_PHY_ADDR		_IOW('F', 0x2, unsigned int)
#define SHAREDMEM_AVAILABLE             _IOW('F', 0x3, unsigned int)
#define SHAREDMEM_TOTAL_SIZE            _IOW('F', 0x4, unsigned int)
int dma_coherent_mem_size(struct device *dev);
int dma_coherent_mem_available(struct device *dev);
static long sharedmem_ioctl(struct file *file, unsigned int cmd, unsigned long args)
{
	struct filedata *file_data = (struct filedata *)file->private_data;
    struct sharedmem *smem = file_data->smem;

	int fd = 0;
	int ret = 0;
	unsigned long phyaddr = 0;
	switch (cmd) {
	case SHAREDMEM_ATTACH_PHY_ADDR:
		if (copy_from_user(&fd, (void *)args, sizeof(fd))) {
			dev_err(smem->dev, "copy_from_user error!!!\n");
			ret = -EFAULT;
			break;
		}
		ret = sharedmem_dmabuf_attach_phy(smem->dev,file_data,fd,&phyaddr);
		if (copy_to_user((void *)args, &phyaddr, sizeof(phyaddr)))
			return -EFAULT;
		break;
	case SHAREDMEM_DEATTACH_PHY_ADDR:

		if (copy_from_user(&fd, (void *)args, sizeof(fd))) {
			dev_err(smem->dev, "copy_from_user error!!!\n");
			ret = -EFAULT;
			break;
		}
		ret = sharedmem_dmabuf_deattach_phy(smem->dev,file_data,fd);
		break;
	case SHAREDMEM_AVAILABLE:
		*(unsigned long*) args = dma_coherent_mem_available(smem->dev);
		break;
	case SHAREDMEM_TOTAL_SIZE:
		*(unsigned long*) args = dma_coherent_mem_size(smem->dev);
		break;
	default:
		dev_err(smem->dev,"No Support cmd:%x\n",cmd);
	}
    return ret;
}

static struct file_operations sharedmem_fops = {
    .owner = THIS_MODULE,
	.open = sharedmem_open,
    .unlocked_ioctl = sharedmem_ioctl,
    .release = sharedmem_release,
};

static int ingenic_sharedmem_probe(struct platform_device *pdev)
{
	struct sharedmem *smem;
	int ret;
	const char *devname;
    smem = devm_kzalloc(&pdev->dev,sizeof(struct sharedmem), GFP_KERNEL);
    if(IS_ERR_OR_NULL(smem)) {
        pr_err("Failed to alloc mem for sharedmem!\n");
        return -ENOMEM;
    }
	smem->dev = &pdev->dev;
	if(of_property_read_string(pdev->dev.of_node,"ingenic,devname",&devname)) {
		pr_err("unrecognized value for ingenic,devname");
		return -1;
	}
	smem->devname = devname;

	ret = reservemem_heap_create(smem);
	if(ret != 0)
	{
		pr_err("Failed to create dma-buf heap\n");
		return ret;
	}

    platform_set_drvdata(pdev, smem);

    smem->mdev.minor = MISC_DYNAMIC_MINOR;
    smem->mdev.name = smem->devname;
    smem->mdev.fops = &sharedmem_fops;

    ret = misc_register(&smem->mdev);
    if(ret < 0) {
        dev_err(&pdev->dev, "Failed to register misc driver!\n");
        return ret;
    }

	ret = of_reserved_mem_device_init(smem->dev);
	if(ret)
		dev_warn(smem->dev, "failed to init reserved mem check dts\n");

    dev_info(&pdev->dev, "sharedmem driver probe ok!\n");

    return 0;

}

static int ingenic_sharedmem_remove(struct platform_device *pdev)
{
    struct sharedmem *smem = platform_get_drvdata(pdev);
    misc_deregister(&smem->mdev);
	pr_err("the driver isn't support remove\n");
    return 0;
}

static const struct of_device_id ingenic_sharedmem_dt_match[] = {
    { .compatible = "ingenic,sharedmem", .data = NULL },
    {},
};

MODULE_DEVICE_TABLE(of, ingenic_sharedmem_dt_match);

static struct platform_driver ingenic_sharedmem_driver = {
    .probe	= ingenic_sharedmem_probe,
    .remove	= ingenic_sharedmem_remove,
    .driver	= {
        .name	= "sharedmem",
        .owner	= THIS_MODULE,
        .of_match_table = of_match_ptr(ingenic_sharedmem_dt_match),
    },
};

module_platform_driver(ingenic_sharedmem_driver);

MODULE_LICENSE("GPL v2");
