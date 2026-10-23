#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/limits.h>
#include <linux/syscore_ops.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/dma-mapping.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/vmalloc.h>
#include <linux/list.h>

#include "multithread_test.h"  // 多线程测试框架头文件
// 访问物理地址0xAFFFFFFC，触发特定硬件操作
#define PALLADIUM_TRIGER_2()	do {\
	volatile unsigned int *a; \
	a = (unsigned int *)0xAFFFFFFC; \
	*a = *a; \
	} while(0)

void* multithread;          // 多线程测试框架句柄
// IPI测试结构体：管理测试线程、CPU计数、内存等资源
struct csd_test {
	atomic_t acnt[NR_CPUS];     // 各CPU的IPI处理计数（原子变量）
    atomic_t lock[NR_CPUS];     // 各CPU的IPI处理完成标记
    void* space[NR_CPUS];       // 各CPU的测试内存空间
};  // 全局测试实例

// 同步IPI处理函数：递增当前CPU的处理计数
static void test_ipi_wait(void * args)
{
    struct csd_test *p = (struct csd_test *)args;
    int cpu = raw_smp_processor_id();  // 获取当前CPU编号（中断上下文）
    atomic_inc(&p->acnt[cpu]);
}

// 异步IPI处理函数：标记当前CPU处理完成
static void test_ipi_nowait(void * args)
{
    struct csd_test *p = (struct csd_test *)args;
    int cpu = raw_smp_processor_id();  // 获取当前CPU编号（中断上下文）
    atomic_set(&p->lock[cpu], 1);
}

static int test_func_cnt[NR_CPUS] = {0};
// 核心测试函数：执行IPI发送、数据读写及结果验证
static int csd_test_func(void * param)
{
	struct csd_test local_csd = {0};
	struct csd_test *p = &local_csd; //(struct csd_test *)kzalloc(sizeof(struct csd_test), GFP_KERNEL);
    int cpu = smp_processor_id();  // 获取当前CPU编号（进程上下文）

    // 为当前CPU分配64KB测试内存（64*1024）
    p->space[cpu] = (void*)vmalloc(64*1024);
    if(p->space[cpu]) {

        unsigned int *d = (unsigned int*)p->space[cpu];
        unsigned int off = (get_random_int() & 0xff);  // 随机偏移量
        unsigned int pat = get_random_int();  // 随机测试数据
        int i;
        int timeout = 1000000;  // 等待超时计数
	
	if(!(test_func_cnt[cpu])) {
		printk("----p->space[%d]: %x, offset: %x, pat: %x\n", cpu, (unsigned int)p->space[cpu], off, pat);
	}
	test_func_cnt[cpu] ++;
	test_func_cnt[cpu] %= 1000;

        // 初始化所有在线CPU的完成标记为未处理
        for_each_online_cpu(i) {
            atomic_set(&p->lock[i], 0);
        }

        // 发送异步IPI（不等待），触发test_ipi_nowait
        on_each_cpu(test_ipi_nowait, p, 0);

        // 等待所有CPU完成异步IPI处理
        while(--timeout) {
            int lock = 1;
            for_each_online_cpu(i) {
                lock &= atomic_read(&p->lock[i]);
            }
            if(lock == 1) break;
        }
	if(timeout <= 0) {  // 超时处理
		pr_info("test ipi nowait failed");
		dump_stack();
		vfree(p->space[cpu]);
		return -1;
	}

        // 向测试内存写入数据（从随机偏移开始）
        for(i = off; i < 64 * 1024 / 4; i++) {
            d[i] = pat;
        }

        // 发送同步IPI（等待完成），触发test_ipi_wait
        on_each_cpu(test_ipi_wait, p, 1);

        // 验证同步IPI处理结果
	for_each_online_cpu(i) {
		atomic_dec(&p->acnt[i]);
	}

	{
		int acnt = 0;
		int flag = 0;
		for_each_online_cpu(i) {
			acnt = atomic_read(&p->acnt[i]);
			if(acnt != 0) {  // 计数异常则报错
				pr_info("test_ipi_wait: failed!!!\n");
				pr_info("i: %d acnt: %d cpuid:%d \n", i, acnt,raw_smp_processor_id());
				flag = 1;
			}
		}
		if(flag == 1){
			dump_stack();
			vfree(p->space[cpu]);
			return -1;
		}
        }
        vfree(p->space[cpu]);  // 释放测试内存
    }


	return 0;
}

// 模块初始化：初始化多线程框架并注册测试函数
static int __init csd_test_init(void)
{
	multithread = multithread_test_init("csd_test", 1);
	if(multithread){
		multithread_test_add_func(multithread, "csd_test", csd_test_func, (void*)NULL);
	}
	return 0;
}

// 模块退出：清理多线程框架资源
static void __exit csd_test_deinit(void)
{
    if (multithread) {
        multithread_test_deinit(multithread);
        multithread = NULL;
    }
}

late_initcall(csd_test_init);
module_exit(csd_test_deinit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CSD IPI Test Module");
