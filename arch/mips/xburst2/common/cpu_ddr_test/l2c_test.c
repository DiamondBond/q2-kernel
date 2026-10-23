#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/syscore_ops.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/dma-mapping.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/platform_device.h>
/**
  1.   start address align:  0-63
  2.   size align:           0-63
  3.   max size: 		     512k
  4.   invalid:
  5.   writeback:
  6.   blast:
  7.   8 thread.
*/

struct l2c_test_thread;
typedef int (*TEST_FUNC)(struct l2c_test_thread *);
struct l2c_test_thread{
	struct list_head	list;
	struct task_struct	*task;
	TEST_FUNC test_func;
	struct mutex		*lock;
	int id;
	volatile int done;
};

struct l2c_test {
	struct dentry		*root;
	int run;
	struct mutex		lock;
	struct mutex		resultlock;
	struct list_head	top;
	unsigned int thread_count;
	struct device *dev;
};
#define TO_UNC(x) ((unsigned int)x | 0x20000000)
//#define TO_UNC(x) ((unsigned int)x)

static unsigned long scache_size __read_mostly;
static unsigned long scache_linesize __read_mostly;
static struct l2c_test g_l2c_test;

#if 0
#define PALLADIUM_TRIGER() do{					\
		void (*func)(void);						\
		func = (void(*)(void)) 0xbfc00000;		\
		func();									\
	}while(0)
#else
#define PALLADIUM_TRIGER() do{					\
	dump_stack();						\
	printk("%s, %d src_org: 0x%x\n", __func__, __LINE__, (unsigned int)src_org);	\
	kfree(src_org);						\
	}while(0)
#endif

#define info(x,y...) do{							\
		printk(x,##y);								\
	}while(0)

static __maybe_unused int writeback_align_address(struct l2c_test_thread *data)
{
	unsigned char *src;
	unsigned char *src_org;
	unsigned char *p;
	unsigned char *unc_p;
	unsigned char unc_v;
	unsigned char cache_v;
	int i,j;
	struct device *dev;

	dev = g_l2c_test.dev;

	src_org = (unsigned char*)kmalloc(scache_linesize * 4,GFP_KERNEL);

	if(IS_ERR_OR_NULL(src_org)){
		src = 0;
		info("kmalloc error!\n");
	}else{
		src = src_org + scache_linesize;
		//info("%s test addr: %px\n",__func__,src);
		for(j = 0;j<scache_linesize;j++) {
			for(i = 0;i<scache_linesize * 2;i++){
				src[i] = i & 255;
			}

			dma_sync_single_for_device(dev, (dma_addr_t)(virt_to_phys(src + j)), scache_linesize, DMA_TO_DEVICE);
			p = src;
			unc_p = (unsigned char*)TO_UNC(p);
			//msleep(1);
			for(i = 0;i<scache_linesize * 2;i++){
				if(p >= &src[j] && p < (&src[j] + scache_linesize)){
					unc_v = *unc_p;
					if(unc_v != (i & 255)){
						PALLADIUM_TRIGER();
						info("uncache Err:addr:%px offset:%d e:0x%02x r:0x%02x, reread:0x%02x\n",src,p-src,i & 255,unc_v, *(unc_p));
						return -1;
					}
				}else{
					cache_v = *p;
					if(cache_v != (i & 255)){
						PALLADIUM_TRIGER();
						info("cache Err:addr:%px offset:%d e:0x%02x r:0x%02x, reread:0x%02x\n",src,p-src,i & 255,cache_v, *(p));
						return -1;
					}
				}
				p++;
				unc_p++;
			}
			for(i = 0;i<scache_linesize * 2;i++){
				src[i] = i & 255;
			}
		}
	}
	if(src_org)
		kfree(src_org);

	return 0;
}

static __maybe_unused int writeback_align_size(struct l2c_test_thread *data)
{
	unsigned char *src;
	unsigned char *src_org;
	unsigned char *p;
	unsigned char *unc_p;
	unsigned char cache_v;
	int i,j;
	struct device *dev;
	dev = g_l2c_test.dev;
	src_org = (unsigned char*)kmalloc(scache_linesize * 4,GFP_KERNEL);
	if(IS_ERR_OR_NULL(src_org)){
		src = 0;
		info("kmalloc error!\n");
	}else{
		src = src_org + scache_linesize;
		//info("%s test addr: %px\n",__func__,src);
		for(j = 0;j<scache_linesize;j++) {
			unc_p = (unsigned char *)TO_UNC(src);
			for(i = 0;i<scache_linesize * 2;i++){
				unc_p[i] = i & 0xff;
				src[i] = i & 0xff;
			}
			fast_iob();
			p = src;
			for(i = 0;i < scache_linesize*2-j;i++){
				*p = 0xff - i;
				p++;
			}
			dma_sync_single_for_device(dev, (dma_addr_t)(virt_to_phys(src)), scache_linesize * 2 - j, DMA_TO_DEVICE);
			p = src;
			//msleep(1);
			for(i = 0;i < scache_linesize * 2 - j;i++){
				cache_v = *p;
				if(cache_v != 0xff - i){
					PALLADIUM_TRIGER();
					info("cache 1 Err:addr:%px offset:%d e:0x%02x r:0x%02x, reared:0x%02x\n",src,p-src,0xff - i,cache_v, *p);
					return -1;
				}
				p++;
			}
			for(;i < scache_linesize * 2;i++){
				cache_v = *p;
				if(cache_v != (i & 255)){
					PALLADIUM_TRIGER();
					info("cache 2 Err:addr:%px offset:%d e:0x%02x r:0x%02x, reared:0x%02x\n",src,p-src,i & 255,cache_v, *p);
					return -1;
				}
				p++;
			}
		}
	}
	if(src_org)
		kfree(src_org);
	return 0;
}
static __maybe_unused int invalid_align_address(struct l2c_test_thread *data)
{
	unsigned char *src_org;
	unsigned char *src;
	unsigned char *p;
	unsigned char *unc_p;
	unsigned char r;
	int i,j,n;
	struct device *dev;
	dev = g_l2c_test.dev;
	src_org = (unsigned char*)kmalloc(scache_linesize * 5,GFP_KERNEL);
	if(IS_ERR_OR_NULL(src_org)){
		src = 0;
		info("kmalloc error!\n");
	}else{
		src = src_org + scache_linesize;
		//info("%s test addr: %px\n",__func__,src);
		for(j = 0;j<scache_linesize;j++) {
			unc_p = (unsigned char *)TO_UNC(src);
			for(i = 0;i<scache_linesize * 3;i++){
				unc_p[i] = i & 0xff;
				src[i] = i & 0xff;
			}

			dma_sync_single_for_device(dev, (dma_addr_t)(virt_to_phys(src + j)), scache_linesize * 2, DMA_FROM_DEVICE);
			p = &src[j];
			unc_p = (unsigned char*)TO_UNC(p);
			for(i = 0;i < scache_linesize * 2;i++){
				unc_p[i] = 0xff - i;
			}
			fast_iob();
			//dma_sync_single_for_device(dev, (dma_addr_t)(virt_to_phys(src + j)), scache_linesize * 2, DMA_FROM_DEVICE);
			//msleep(1);
			p = src;
			n = 0;
			for(i = 0;i<scache_linesize * 3;i++){
				if(p >= &src[j] && p < (&src[j] + scache_linesize * 2)){
					r = *p;
					if(r != (0xff - n)){
						PALLADIUM_TRIGER();
						info("Err: 1 addr:%px offset:%d e:0x%02x r:0x%02x, reread:0x%02x\n",src,p-src,0xff - n,r,*p);
						return -1;
					}
					n++;
				}else{
					r = *p;
					if(r != (i & 255)){
						PALLADIUM_TRIGER();
						info("Err: 2 addr:%px offset:%d e:0x%02x r:0x%02x, reread:0x%02x\n",src,p-src,i & 255,r,*p);
						return -1;
					}
				}
				p++;
			}
			for(i = 0;i<scache_linesize * 3;i++){
				src[i] = i & 255;
			}
		}
	}
	if(src_org)
		kfree(src_org);
	return 0;
}
static __maybe_unused int invalid_align_size(struct l2c_test_thread *data)
{
	unsigned char *src;
	unsigned char *src_org;
	unsigned char *p;
	unsigned char *unc_p;
	int i,j;
	struct device *dev;
	dev = g_l2c_test.dev;
	src_org = (unsigned char*)kmalloc(scache_linesize * 5,GFP_KERNEL);
	if(IS_ERR_OR_NULL(src_org)){
		src = 0;
		info("kmalloc error!\n");
	}else{
		src = src_org + scache_linesize;
		//info("%s test addr: %px\n",__func__,src);
		for(j = 0;j < scache_linesize;j++) {
			for(i = 0;i<scache_linesize * 3;i++){
				src[i] = i & 255;
			}
			dma_sync_single_for_device(dev, (dma_addr_t)(virt_to_phys(src)), scache_linesize * 3 - j, DMA_FROM_DEVICE);
			p = src;
			unc_p = (unsigned char*)TO_UNC(p);
			for(i = 0;i < scache_linesize*3-j;i++){
				*unc_p = 0xff - i;
				unc_p++;
			}
			fast_iob();
			//dma_sync_single_for_device(dev, (dma_addr_t)(virt_to_phys(src)), scache_linesize * 3 - j, DMA_FROM_DEVICE);

			//msleep(1);
			for(i = 0;i < scache_linesize * 3 - j;i++){
				if(*p != 0xff - i){
					PALLADIUM_TRIGER();
					info("Err: 1 addr:%px offset:%d e:0x%02x r:0x%02x\n",src,p-src,0xff - i,*p);
					return -1;
				}
				p++;
			}
			for(;i < scache_linesize * 3;i++){
				if(*p != (i & 255)){
					PALLADIUM_TRIGER();
					info("Err:2 addr:%px offset:%d e:0x%02x r:0x%02x\n",src,p-src,i & 255,*p);
					return -1;
				}
				p++;
			}
		}
	}
	if(src_org)
		kfree(src_org);
	return 0;
}
static __maybe_unused int blast_invalid_all(struct l2c_test_thread *data)
{
	unsigned char *src;
	unsigned char *src_org;
	unsigned char *p;
	unsigned char *unc_p;
	unsigned char eq;
	unsigned char r;
	int i,j;
	struct device *dev;

	dev = g_l2c_test.dev;
	src_org = (unsigned char*)kmalloc(scache_size * 2 + scache_linesize * 2,GFP_KERNEL);
	if(IS_ERR_OR_NULL(src_org)){
		src = 0;
		info("kmalloc error!\n");
	}else{
		src = src_org + scache_linesize;
		//info("%s test addr: %px\n",__func__,src);
		for(i = 0;i < scache_size * 2;i++){
			src[i] = i & 0xff;
		}
		dma_sync_single_for_device(dev, (dma_addr_t)(virt_to_phys(src)), scache_size * 2, DMA_FROM_DEVICE);
		p = src;
		unc_p = (unsigned char *)TO_UNC(p);
		for(i = 0;i < scache_size;i++){
			unc_p[scache_size / 2 + i] = 0xff - (i & 0xff);
		}
		fast_iob();
		//dma_sync_single_for_device(dev, (dma_addr_t)(virt_to_phys(src)), scache_size * 2, DMA_FROM_DEVICE);
		j = 0;
		for(i = 0;i < scache_size * 2;i++){

			if((i >= scache_size / 2) && (i < scache_size / 2 + scache_size))
			{
				eq = 0xff - (j & 0xff);
				j++;
			}else {
				eq = i & 0xff;
			}
			r = p[i];
			if(r != eq){
				PALLADIUM_TRIGER();
				info("Err:addr:%px e:0x%02x r:0x%02x reread:0x%02x\n",p,eq,r,p[i]);
				return -1;
			}
		}
	}
	if(src_org)
		kfree(src_org);
	return 0;
}
static __maybe_unused int blast_writeback_all(struct l2c_test_thread *data)
{
	unsigned char *src;
	unsigned char *src_org;
	unsigned char *p;
	unsigned char *unc_p;
	unsigned char r;
	int i;
	unsigned char eq;
	struct device *dev;
	dev = g_l2c_test.dev;
	src_org = (unsigned char*)kmalloc(scache_size * 2 + scache_linesize * 2,GFP_KERNEL);
	if(IS_ERR_OR_NULL(src_org)){
		src = 0;
		info("kmalloc error!\n");
	}else{
		src = src_org + scache_linesize;
		//info("%s test addr: %px\n",__func__,src);
		p = src;
		unc_p = (unsigned char *)TO_UNC(p);
		for(i = 0;i < scache_size;i++){
			unc_p[scache_size / 2 + i] = 0xff - (i & 0xff);
		}
		fast_iob();
		for(i = 0;i < scache_size * 2;i++){
			src[i] = i & 0xff;
		}
		dma_sync_single_for_device(dev, (dma_addr_t)(virt_to_phys(src)), scache_size * 2, DMA_TO_DEVICE);

		for(i = 0;i < scache_size * 2;i++){
			eq = i & 0xff;
			r = p[i];
			if(r != eq){
				PALLADIUM_TRIGER();
				info("Err:addr:%px e:0x%02x r:0x%02x reread:0x%02x\n",p,eq,r,p[i]);
				return -1;
			}
		}
	}
	if(src_org)
		kfree(src_org);
	return 0;
}

static TEST_FUNC g_test_func[] ={
	writeback_align_address,
	writeback_align_size,
	invalid_align_address,
	invalid_align_size,
	blast_invalid_all,
	blast_writeback_all
};
static int g_thread_id = 0;


static int l2c_test_func(void *data)
{
	struct l2c_test_thread *thread = data;
	int index;
	while (!kthread_should_stop())
	{
		//printk("d:%px\n",thread->test_func);
		thread->test_func(thread);
		index = prandom_u32() % ARRAY_SIZE(g_test_func);
		thread->test_func = g_test_func[index];
	}
	thread->done = 0;
	return 0;
}



static int l2c_test_add_threads(struct l2c_test *l2ctest)
{
	struct l2c_test_thread *thread;
	unsigned int index;
	thread = (struct l2c_test_thread *)kmalloc(sizeof(struct l2c_test_thread), GFP_KERNEL);
	if(IS_ERR_OR_NULL(thread)){
		printk("thread kmalloc failed!\n");
		return -1;
	}
	memset(thread,0,sizeof(struct l2c_test_thread));
	thread->done = -1;
	index = prandom_u32() % ARRAY_SIZE(g_test_func);
	thread->test_func = g_test_func[index];
	thread->id = g_thread_id;
	thread->lock = &l2ctest->resultlock;
	thread->task = kthread_run(l2c_test_func, thread, "l2c_test%d-func%u",g_thread_id++,index);
	if (IS_ERR(thread->task)) {
		info("l2c_test: Failed to run thread l2c_test%d-func%u\n",g_thread_id,index);
		kfree(thread);
		return -1;
	}
	list_add_tail(&thread->list, &l2ctest->top);
	return 0;
}
static void stop_threaded_test(struct l2c_test *l2ctest)
{
	struct l2c_test_thread *thread,*_thread;

	list_for_each_entry_safe(thread, _thread, &l2ctest->top, list) {
		list_del(&thread->list);
		kthread_stop(thread->task);
		while(thread->done);
		kfree(thread);
	}
}

static int run_l2c_test(struct l2c_test *l2c)
{
	int i = 0;

	printk("start running l2c test, l2csize: %lu, l2c linesize: %lu\n", scache_size, scache_linesize);

	if(l2c->thread_count > 0){
		for(i = 0;i < l2c->thread_count;i++){
			l2c_test_add_threads(l2c);
		}
	}
	return 0;
}

static ssize_t l2c_test_read_run(struct file *file, char __user *user_buf,
							size_t count, loff_t *ppos)
{
	struct l2c_test *l2c = file->private_data;
	char *buf;
	mutex_lock(&l2c->lock);
	if(!list_empty(&l2c->top))
		buf = "Runing\n";
	else
		buf = "Stop\n";
	mutex_unlock(&l2c->lock);
	return simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));
}
static ssize_t l2c_test_write_run(struct file *file, const char __user *user_buf,
							 size_t count, loff_t *ppos)
{
	struct l2c_test *l2c = file->private_data;
	char buf[16];
	bool bv;
	int ret = 0;
	if (copy_from_user(buf, user_buf, min(count, (sizeof(buf) - 1))))
		return -EFAULT;

	if (strtobool(buf, &bv) == 0) {
		mutex_lock(&l2c->lock);
		if(bv){
			run_l2c_test(l2c);
		}else{
			stop_threaded_test(l2c);
		}
		mutex_unlock(&l2c->lock);
	}

	return ret ? ret : count;
}

static const struct file_operations l2c_test_run_fops = {
	.read	= l2c_test_read_run,
	.write	= l2c_test_write_run,
	.open	= simple_open,
	.llseek	= default_llseek,
};

static int l2c_test_probe(struct platform_device * pdev)
{
	struct dentry * d;
	struct l2c_test *l2c= &g_l2c_test;
	struct cpuinfo_mips *c = &current_cpu_data;

	scache_size = c->scache.ways * c->scache.sets * c->scache.linesz;
	scache_linesize = c->scache.linesz;

	memset(l2c,0,sizeof(struct l2c_test));
	mutex_init(&l2c->lock);
	mutex_init(&l2c->resultlock);
	INIT_LIST_HEAD(&l2c->top);
	l2c->thread_count = 1;

	d = debugfs_create_dir("l2c_test", NULL);
	if (IS_ERR(d)){
		pr_err("create debugfs for l2c_test failed.\n");
		return PTR_ERR(d);
	}
	l2c->root = d;

	debugfs_create_u32("thread_count", S_IWUSR | S_IRUGO, l2c->root,
						   (u32 *)&l2c->thread_count);

	d = debugfs_create_file("run", S_IWUSR | S_IRUGO, l2c->root,
							l2c, &l2c_test_run_fops);
	if (IS_ERR_OR_NULL(d)){
		pr_err("debugfs create run node failed\n");
		goto err_node;
	}

	l2c->dev = &pdev->dev;

	pr_info("l2c_test debugfs init ok.\n");
	return 0;
err_node:
	debugfs_remove_recursive(l2c->root);
	pr_err("l2c_test debugfs init failed.\n");
	return -1;
}
//late_initcall(l2c_test_init);

static int l2c_test_remove(struct platform_device *pdev)
{
	struct l2c_test *l2c= &g_l2c_test;
	stop_threaded_test(l2c);
	debugfs_remove_recursive(l2c->root);

	return 0;
}
//module_exit(l2c_test_deinit);


static struct platform_device l2c_test_pdev = {
	.name		= "l2c_test_dev",
};

static struct platform_driver l2c_test_pdrv = {
	.probe		= l2c_test_probe,
	.remove		= l2c_test_remove,
	.driver		= {
		.name	= "l2c_test_dev",
		.owner	= THIS_MODULE,
	},
};

static int __init init_l2c_test(void)
{
	platform_driver_register(&l2c_test_pdrv);
	platform_device_register(&l2c_test_pdev);
	return 0;
}

static void __exit exit_l2c_test(void)
{
	platform_driver_unregister(&l2c_test_pdrv);
	platform_device_unregister(&l2c_test_pdev);
}

module_init(init_l2c_test);
module_exit(exit_l2c_test);
