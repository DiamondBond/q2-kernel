#ifndef WQ_WLAN_UTILS_H_
#define WQ_WLAN_UTILS_H_

#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/sched.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <uapi/linux/sched/types.h>
#include <linux/sched/task.h>
#endif

#include "wq_log.h"

#define WQ_STATS

#ifdef WQ_STATS
#define WQ_STATS_RESET(_counter)                                               \
	do {                                                                   \
		(_counter) = 0;                                                \
	} while (0)
#define WQ_STATS_INC(_counter)                                                 \
	do {                                                                   \
		++(_counter);                                                  \
	} while (0)
#define WQ_STATS_DEC(_counter)                                                 \
	do {                                                                   \
		--(_counter);                                                  \
	} while (0)
#else
#define WQ_STATS_RESET(_counter)                                               \
	do {                                                                   \
	} while (0)
#define WQ_STATS_INC(_counter)                                                 \
	do {                                                                   \
	} while (0)
#define WQ_STATS_DEC(_counter)                                                 \
	do {                                                                   \
	} while (0)
#endif

#ifdef WQ_STATS
struct q_stats {
	u32 depth;
	u32 in;
	u32 out;
	s32 max; /* max of "in - out" */
};

#define Q_STATS(_name) struct q_stats _name;

static inline void q_stats_reset(struct q_stats *stats)
{
	stats->in = 0;
	stats->out = 0;
	stats->max = 0;
}

static inline void q_stats_init(struct q_stats *stats, u32 depth)
{
	stats->depth = depth;
	q_stats_reset(stats);
}

static inline s32 q_stats_n(struct q_stats *stats)
{
	return stats->in - stats->out;
}

static inline void q_stats_tx(struct q_stats *stats, u32 inc)
{
	s32 n;

	stats->in += inc;
	n = q_stats_n(stats);
	if (stats->max < n)
		stats->max = n;
}

static inline void q_stats_txdone(struct q_stats *stats, u32 n)
{
	stats->out += n;
}

static inline void q_stats_rx_refill(struct q_stats *stats, u32 n)
{
	stats->in += n;
}

static inline void q_stats_rx(struct q_stats *stats, u32 inc)
{
	s32 n;

	stats->out += inc;
	n = stats->depth - q_stats_n(stats);
	if (stats->max > n)
		stats->max = n;
}
#else
#define Q_STATS(_name)

static inline void q_stats_reset(struct q_stats *stats)
{
}
static inline void q_stats_init(struct q_stats *stats, u32 depth)
{
}
static inline u32 q_stats_n(struct q_stats *stats)
{
	return 0;
}
static inline void q_stats_tx(struct q_stats *stats, u32 n)
{
}
static inline void q_stats_txdone(struct q_stats *stats, u32 n)
{
}
static inline void q_stats_rx_refill(struct q_stats *stats, u32 n)
{
}
static inline void q_stats_rx(struct q_stats *stats, u32 n)
{
}
#endif

struct wq_list_head {
	spinlock_t lock;
	struct list_head head;
#ifdef WQ_STATS
	u32 num; /* entry number in this list */

	u32 add;
	u32 del;
#endif
};

struct wq_list_pool {
	struct wq_list_head list; /* free entry list */

	/* pool attributes */
	u16 size; /* entry size */
	u16 num; /* total number */
	void *entries;
};

static inline void INIT_WQ_LIST_HEAD(struct wq_list_head *list)
{
	spin_lock_init(&list->lock);
	INIT_LIST_HEAD(&list->head);
	WQ_STATS_RESET(list->num);
	WQ_STATS_RESET(list->add);
	WQ_STATS_RESET(list->del);
}

static inline int wq_list_is_empty(struct wq_list_head *list)
{
	int empty;
	unsigned long flags;

	spin_lock_irqsave(&list->lock, flags);
	empty = list_empty(&list->head);
	spin_unlock_irqrestore(&list->lock, flags);

	return empty;
}

#ifdef WQ_STATS
#define WQ_LIST_CHECK(list)                                                    \
	do {                                                                   \
		WARN((list)->add != ((list)->del + (list)->num),               \
		     "%s(%p): add %d != del %d + num %d\n", __func__, list,    \
		     (list)->add, (list)->del, (list)->num);                   \
	} while (0)
#else
#define WQ_LIST_CHECK(list)                                                    \
	do {                                                                   \
	} while (0)
#endif

#define list_last_entry_or_null(ptr, type, member) ({ \
	struct list_head *head__ = (ptr); \
	struct list_head *pos__ = READ_ONCE(head__->prev); \
	pos__ != head__ ? list_entry(pos__, type, member) : NULL; \
})

#define WQ_LIST_ADD(ptr, type, member, entry, how)                             \
	do {                                                                   \
		struct wq_list_head *list__ = (ptr);                           \
		unsigned long flags;                                           \
                                                                               \
		spin_lock_irqsave(&list__->lock, flags);                       \
		WQ_LIST_CHECK(list__);                                         \
		WQ_STATS_INC(list__->num);                                     \
		WQ_STATS_INC(list__->add);                                     \
		how(&(entry)->member, &list__->head);                          \
		spin_unlock_irqrestore(&list__->lock, flags);                  \
	} while (0)

#define __WQ_LIST_ADD(ptr, type, member, entry, how)                           \
	do {                                                                   \
		struct wq_list_head *list__ = (ptr);                           \
                                                                               \
		WQ_LIST_CHECK(list__);                                         \
		WQ_STATS_INC(list__->num);                                     \
		WQ_STATS_INC(list__->add);                                     \
		how(&(entry)->member, &list__->head);                          \
	} while (0)

#define WQ_LIST_PUSH(ptr, type, member, entry)                                 \
	WQ_LIST_ADD(ptr, type, member, entry, list_add_tail)

#define __WQ_LIST_PUSH(ptr, type, member, entry)                               \
	__WQ_LIST_ADD(ptr, type, member, entry, list_add_tail)

#define WQ_LIST_UNPOP(ptr, type, member, entry)                                \
	WQ_LIST_ADD(ptr, type, member, entry, list_add)

#define __WQ_LIST_UNPOP(ptr, type, member, entry)                              \
	__WQ_LIST_ADD(ptr, type, member, entry, list_add)

#define WQ_LIST_POP(ptr, type, member)                                         \
	({                                                                     \
		struct wq_list_head *list__ = (ptr);                           \
		type *entry;                                                   \
		unsigned long flags;                                           \
                                                                               \
		spin_lock_irqsave(&list__->lock, flags);                       \
		WQ_LIST_CHECK(list__);                                         \
		entry = list_first_entry_or_null(&list__->head, type, member); \
		if (entry) {                                                   \
			WQ_STATS_DEC(list__->num);                             \
			WQ_STATS_INC(list__->del);                             \
			list_del(&entry->member);                              \
		}                                                              \
		spin_unlock_irqrestore(&list__->lock, flags);                  \
                                                                               \
		entry;                                                         \
	})

#define __WQ_LIST_POP(ptr, type, member)                                       \
	({                                                                     \
		struct wq_list_head *list__ = (ptr);                           \
		type *entry;                                                   \
                                                                               \
		WQ_LIST_CHECK(list__);                                         \
		entry = list_first_entry_or_null(&list__->head, type, member); \
		if (entry) {                                                   \
			WQ_STATS_DEC(list__->num);                             \
			WQ_STATS_INC(list__->del);                             \
			list_del(&entry->member);                              \
		}                                                              \
                                                                               \
		entry;                                                         \
	})

#define WQ_LIST_POP_LAST(ptr, type, member)                                         \
	({                                                                     \
		struct wq_list_head *list__ = (ptr);                           \
		type *entry;                                                   \
		unsigned long flags;                                           \
                                                                               \
		spin_lock_irqsave(&list__->lock, flags);                       \
		WQ_LIST_CHECK(list__);                                         \
		entry = list_last_entry_or_null(&list__->head, type, member);  \
		if (entry) {                                                   \
			WQ_STATS_DEC(list__->num);                             \
			WQ_STATS_INC(list__->del);                             \
			list_del(&entry->member);                              \
		}                                                              \
		spin_unlock_irqrestore(&list__->lock, flags);                  \
                                                                               \
		entry;                                                         \
	})

#define __WQ_LIST_POP_LAST(ptr, type, member)                                       \
	({                                                                     \
		struct wq_list_head *list__ = (ptr);                           \
		type *entry;                                                   \
                                                                               \
		WQ_LIST_CHECK(list__);                                         \
		entry = list_last_entry_or_null(&list__->head, type, member);  \
		if (entry) {                                                   \
			WQ_STATS_DEC(list__->num);                             \
			WQ_STATS_INC(list__->del);                             \
			list_del(&entry->member);                              \
		}                                                              \
                                                                               \
		entry;                                                         \
	})

#define WQ_LIST_PEEK(ptr, type, member)                                \
	({                                                                 \
		struct wq_list_head *list__ = (ptr);                           \
		type *entry;                                                   \
		unsigned long flags;                                           \
                                                                       \
		spin_lock_irqsave(&list__->lock, flags);                       \
		WQ_LIST_CHECK(list__);                                         \
		entry = list_first_entry_or_null(&list__->head, type, member); \
		spin_unlock_irqrestore(&list__->lock, flags);                  \
                                                                       \
		entry;                                                         \
	})

#define __WQ_LIST_PEEK(ptr, type, member)                              \
	({                                                                 \
		struct wq_list_head *list__ = (ptr);                           \
		type *entry;                                                   \
                                                                       \
		WQ_LIST_CHECK(list__);                                         \
		entry = list_first_entry_or_null(&list__->head, type, member); \
                                                                       \
		entry;                                                         \
	})

#define WQ_LIST_PEEK_LAST(ptr, type, member)                           \
	({                                                                 \
		struct wq_list_head *list__ = (ptr);                           \
		type *entry;                                                   \
		unsigned long flags;                                           \
                                                                       \
		spin_lock_irqsave(&list__->lock, flags);                       \
		WQ_LIST_CHECK(list__);                                         \
		entry = list_last_entry_or_null(&list__->head, type, member);  \
		spin_unlock_irqrestore(&list__->lock, flags);                  \
                                                                       \
		entry;                                                         \
	})

#define __WQ_LIST_PEEK_LAST(ptr, type, member)                         \
	({                                                                 \
		struct wq_list_head *list__ = (ptr);                           \
		type *entry;                                                   \
                                                                       \
		WQ_LIST_CHECK(list__);                                         \
		entry = list_last_entry_or_null(&list__->head, type, member);  \
                                                                       \
		entry;                                                         \
	})

#define WQ_LIST_DEL(ptr, entry)                                                \
	do {                                                                   \
		struct wq_list_head *list__ = (ptr);                           \
		unsigned long flags;                                           \
                                                                               \
		spin_lock_irqsave(&list__->lock, flags);                       \
		WQ_LIST_CHECK(list__);                                         \
		WQ_STATS_DEC(list__->num);                                     \
		WQ_STATS_INC(list__->del);                                     \
		list_del(entry);                                               \
		spin_unlock_irqrestore(&list__->lock, flags);                  \
	} while (0)

#define WQ_LIST_POOL_INIT_FN(_fn, _struct, _list_member, _ctx)                 \
	int _fn(struct wq_list_pool *pool, u16 num,                            \
		int (*_init)(_struct *, _ctx), _ctx ctx)                       \
	{                                                                      \
		u16 size = ALIGN(sizeof(_struct), sizeof(u32));                \
		_struct *entry = kcalloc(num, size, GFP_KERNEL);               \
		int ok = 0;                                                    \
		int i;                                                         \
                                                                               \
		INIT_WQ_LIST_HEAD(&pool->list);                                \
		pool->entries = entry;                                         \
		pool->size = size;                                             \
		pool->num = num;                                               \
		if (!entry) {                                                  \
			pool->num = 0;                                         \
			return -1;                                             \
		}                                                              \
                                                                               \
		for (i = 0; i < num; i++) {                                    \
			INIT_LIST_HEAD(&entry->_list_member);                  \
			if (_init(entry, ctx) == 0)                            \
				++ok;                                          \
			WQ_LIST_PUSH(&pool->list, _struct, _list_member,       \
				     entry);                                   \
			entry++;                                               \
		}                                                              \
                                                                               \
		return ok;                                                     \
	}

#define WQ_LIST_POOL_DEINIT_FN(_fn, _struct, _list_member, _param)             \
	void _fn(struct wq_list_pool *pool,                                    \
		 void (*_deinit)(_struct *, _param), _param param)             \
	{                                                                      \
		_struct *entry = pool->entries;                                \
		int i;                                                         \
                                                                               \
		if (!entry)                                                    \
			return;                                                \
                                                                               \
		for (i = 0; i < pool->num; i++) {                              \
			_deinit(entry, param);                                 \
			entry++;                                               \
		}                                                              \
		kfree(pool->entries);                                          \
		pool->entries = NULL;                                          \
	}

struct wq_workq {
	spinlock_t lock;
	struct work_struct work;
	struct workqueue_struct *workqueue;
};

struct wq_kthread {
	struct task_struct *thread;
	struct completion comp;
};

static inline int wq_workq_init(struct wq_workq *workq, unsigned int flags,
				void (*handler)(struct work_struct *work),
				const char *fmt, const char *sub)
{
	char name[24]; /* WQ_NAME_LEN */

	snprintf(name, sizeof(name), fmt, sub);

	spin_lock_init(&workq->lock);
	INIT_WORK(&workq->work, handler);
	workq->workqueue = alloc_workqueue("%s", flags, 1, name);
	if (workq->workqueue)
		return 0;

	WQ_DBG(DM_TRBUS, DL_ERR, "%s: alloc_workqueue %s failed\n", __func__,
	       name);
	return -1;
}

static inline void wq_workq_deinit(struct wq_workq *workq)
{
	if (workq->workqueue) {
		flush_workqueue(workq->workqueue);
		destroy_workqueue(workq->workqueue);
		workq->workqueue = NULL;
	}
}

static inline int wq_thread_init(struct wq_kthread *thread,
				 int (*handler)(void *data), const char *fmt,
				 const char *sub)
{
	char name[24]; /* WQ_NAME_LEN */

	snprintf(name, sizeof(name), fmt, sub);

	init_completion(&thread->comp);

	thread->thread = kthread_create(handler, (void *)thread, name);
	if (thread->thread) {
		get_task_struct(thread->thread);
		wake_up_process(thread->thread);
		return 0;
	} else {
		return -1;
	}
}

static inline void wq_thread_deinit(struct wq_kthread *thread)
{
	if (thread->thread) {
		complete(&thread->comp);
		kthread_stop(thread->thread);
		put_task_struct(thread->thread);
		thread->thread = NULL;
	}
}

#endif /* WQ_WLAN_UTILS_H_ */
