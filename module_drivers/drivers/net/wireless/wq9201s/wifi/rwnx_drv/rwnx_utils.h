/**
 * rwnx_utils.h
 *
 * IPC utility function declarations
 *
 * Copyright (C) RivieraWaves 2012-2020
 */
#ifndef _RWNX_IPC_UTILS_H_
#define _RWNX_IPC_UTILS_H_

#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/skbuff.h>

#include "fw_api/wifi/mac/cp_api.h"

#define MEM_RECORED_CHECK 0
#if MEM_RECORED_CHECK
#include <linux/netdevice.h>

void add_mem_record(unsigned int size, const char *func, int line_num,
		    struct sk_buff *addr);
void del_mem_record(struct sk_buff *addr, const char *func, int line_num);
void dump_mem_record(void);
struct sk_buff *_dev_alloc_skb_dbg(unsigned int size, const char *function_name,
				   int line_num);
#define dev_alloc_skb(x) _dev_alloc_skb_dbg(x, __func__, __LINE__)
void _dev_kfree_skb_any_dbg(struct sk_buff *skb, const char *function_name,
			    int line_num);
#define dev_kfree_skb_any(x) _dev_kfree_skb_any_dbg(x, __func__, __LINE__)
void _dev_kfree_skb_dbg(struct sk_buff *skb, const char *function_name,
			int line_num);
#undef dev_kfree_skb
#define dev_kfree_skb(x) _dev_kfree_skb_dbg(x, __func__, __LINE__)
#endif

enum rwnx_dev_flag {
	RWNX_DEV_RESTARTING,
	RWNX_DEV_STACK_RESTARTING,
	RWNX_DEV_STARTED,
	RWNX_DEV_ADDING_STA,
};

struct rwnx_hw;
struct rwnx_sta;

/**
 * struct rwnx_ipc_elem - Generic IPC buffer of fixed size
 *
 * @addr: Host address of the buffer.
 * @dma_addr: DMA address of the buffer.
 */
struct rwnx_ipc_elem {
	void *addr;
	dma_addr_t dma_addr;
};

/**
 * struct rwnx_ipc_elem_pool - Generic pool of IPC buffers of fixed size
 *
 * @nb: Number of buffers currenlty allocated in the pool
 * @buf: Array of buffers (size of array is @nb)
 * @pool: DMA pool in which buffers have been allocated
 */
struct rwnx_ipc_elem_pool {
	int nb;
	struct rwnx_ipc_elem *buf;
	struct dma_pool *pool;
};

/**
 * struct rwnx_ipc_elem - Generic IPC buffer of variable size
 *
 * @addr: Host address of the buffer.
 * @dma_addr: DMA address of the buffer.
 * @size: Size, in bytes, of the buffer
 */
struct rwnx_ipc_elem_var {
	void *addr;
	dma_addr_t dma_addr;
	size_t size;
};

/**
 * struct rwnx_ipc_dbgdump_elem - IPC buffer for debug dump
 *
 * @mutex: Mutex to protect access to debug dump
 * @buf: IPC buffer
 */
struct rwnx_ipc_dbgdump_elem {
	struct mutex mutex;
	struct rwnx_ipc_elem_var buf;
};

/**
 * rwnx_ipc_fw_trace_desc_get() - Return pointer to the start of trace
 * description in IPC environment
 *
 * @rwnx_hw: Main driver data
 */
static inline void *rwnx_ipc_fw_trace_desc_get(struct rwnx_hw *rwnx_hw)
{
	return NULL;
}

/**
 * rwnx_ipc_sta_buffer_init - Initialize counter of bufferred data for a given sta
 *
 * @rwnx_hw: Main driver data
 * @sta_idx: Index of the station to initialize
 */
static inline void rwnx_ipc_sta_buffer_init(struct rwnx_hw *rwnx_hw,
					    int sta_idx)
{
}

/**
 * rwnx_ipc_sta_buffer - Update counter of bufferred data for a given sta
 *
 * @rwnx_hw: Main driver data
 * @sta: Managed station
 * @tid: TID on which data has been added or removed
 * @size: Size of data to add (or remove if < 0) to STA buffer.
 */
static inline void rwnx_ipc_sta_buffer(struct rwnx_hw *rwnx_hw,
				       struct rwnx_sta *sta, int tid, int size)
{
}

#endif /* _RWNX_IPC_UTILS_H_ */
