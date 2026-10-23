/**
 * rwnx_utils.c
 *
 * IPC utility function definitions
 *
 * Copyright (C) RivieraWaves 2012-2020
 */
#include "rwnx_utils.h"
#include "wq_log.h"

#ifdef MEM_RECORED_CHECK
#define MEM_RECORED_NUM 300
typedef struct _mem_record {
	int size;
	char func[100];
	int line_num;
	u64 alloc_time;
	struct sk_buff *addr;
} mem_record_s;

mem_record_s mem_record[MEM_RECORED_NUM] = { { 0 } };
spinlock_t mem_record_lock;

void add_mem_record(unsigned int size, const char *func, int line_num,
		    struct sk_buff *addr)
{
	int idx;

	WARN_ON(!addr);

	for (idx = 0; idx < MEM_RECORED_NUM; idx++) {
		spin_lock(&mem_record_lock);
		if (mem_record[idx].size == 0 &&
		    mem_record[idx].line_num == 0) {
			mem_record[idx].size = size;
			mem_record[idx].line_num = line_num;
			strncpy(mem_record[idx].func, func,
				sizeof(mem_record[idx].func));
			mem_record[idx].alloc_time = jiffies_64;
			mem_record[idx].addr = addr;
			printk("add_mem_record-- <%s;%d> size:%d, addr:0x%p, alloc_time:%llu, idx:%d\n",
			       mem_record[idx].func, mem_record[idx].line_num,
			       mem_record[idx].size, mem_record[idx].addr,
			       mem_record[idx].alloc_time, idx);
			spin_unlock(&mem_record_lock);
			break;
		}
		spin_unlock(&mem_record_lock);
	}

	WARN_ON(idx == MEM_RECORED_NUM);
}

void del_mem_record(struct sk_buff *addr, const char *func, int line_num)
{
	int idx;

	printk("del_mem_record:%s:%d\n", func, line_num);
	WARN_ON(!addr);

	for (idx = 0; idx < MEM_RECORED_NUM; idx++) {
		spin_lock(&mem_record_lock);
		if (mem_record[idx].addr == addr) {
			printk("del_mem_record-- <%s;%d> size:%d, addr:0x%p, alloc_time:%llu, idx:%d\n",
			       mem_record[idx].func, mem_record[idx].line_num,
			       mem_record[idx].size, mem_record[idx].addr,
			       mem_record[idx].alloc_time, idx);
			mem_record[idx].addr = 0;
			mem_record[idx].size = 0;
			mem_record[idx].line_num = 0;
			memset(mem_record[idx].func, 0,
			       sizeof(mem_record[idx].func));
			mem_record[idx].alloc_time = 0;
			spin_unlock(&mem_record_lock);
			break;
		}
		spin_unlock(&mem_record_lock);
	}

	WARN_ON(idx == MEM_RECORED_NUM);
}

void dump_mem_record(void)
{
	int idx;

	printk("===== SKB allocated records start ======\n");
	for (idx = 0; idx < MEM_RECORED_NUM; idx++) {
		spin_lock(&mem_record_lock);
		if (mem_record[idx].addr != 0) {
			printk("<%s;%d> size:%d, addr:0x%p, alloc_time:%llu, idx:%d\n",
			       mem_record[idx].func, mem_record[idx].line_num,
			       mem_record[idx].size, mem_record[idx].addr,
			       mem_record[idx].alloc_time, idx);
		}
		spin_unlock(&mem_record_lock);
	}
	printk("===== SKB allocated records end ======\n");
}

struct sk_buff *_dev_alloc_skb_dbg(unsigned int size, const char *function_name,
				   int line_num)
{
	struct sk_buff *addr = NULL;

	addr = netdev_alloc_skb(NULL, size);
	if (addr)
		add_mem_record(size, function_name, line_num, addr);
	return addr;
}

void _dev_kfree_skb_any_dbg(struct sk_buff *skb, const char *function_name,
			    int line_num)
{
	del_mem_record(skb, function_name, line_num);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
		dev_kfree_skb_any_reason(skb, SKB_DROP_REASON_NOT_SPECIFIED);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	dev_kfree_skb_any_reason(skb, SKB_CONSUMED);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	dev_kfree_skb_any_reason(skb, SKB_REASON_DROPPED);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	__dev_kfree_skb_any(skb, SKB_REASON_DROPPED);
#else
	dev_kfree_skb_any(skb);
#endif
}

void _dev_kfree_skb_dbg(struct sk_buff *skb, const char *function_name,
			int line_num)
{
	del_mem_record(skb, function_name, line_num);
	consume_skb(
		skb); //dev_kfree_skb(a) is defined to consume_skb(a) skbuff.h
}
#endif
