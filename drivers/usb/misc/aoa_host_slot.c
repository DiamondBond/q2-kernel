// SPDX-License-Identifier: GPL-2.0
/* Android Open Accessory host slot driver with a reusable URB queue. */

#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/math64.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/overflow.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/workqueue.h>
#include <linux/aoa_host_slot.h>

#define AOA_SLOT_VENDOR_ID                    0x18d1
#define AOA_SLOT_PRODUCT_ACCESSORY            0x2d00
#define AOA_SLOT_PRODUCT_ACCESSORY_ADB        0x2d01
#define AOA_SLOT_PRODUCT_ACCESSORY_AUDIO      0x2d04
#define AOA_SLOT_PRODUCT_ACCESSORY_AUDIO_ADB  0x2d05

#define AOA_SLOT_MINOR_BASE                   244
#define AOA_SLOT_MIN_SLOT_SIZE                512U
#define AOA_SLOT_MAX_SLOT_SIZE                (64U * 1024U)
#define AOA_SLOT_MIN_SLOT_COUNT               1U
#define AOA_SLOT_MAX_SLOT_COUNT               512U
#define AOA_SLOT_MAX_POOL_SIZE                (8U * 1024U * 1024U)
#define AOA_SLOT_MAX_SLOT_RETRIES             3U
#define AOA_SLOT_WRITE_TIMEOUT_MS             1000
#define AOA_SLOT_ADB_SUBCLASS                 0x42
#define AOA_SLOT_ADB_PROTOCOL                 0x01

struct aoa_slot_dev;

enum aoa_slot_state {
	AOA_SLOT_QUEUED = 0,
	AOA_SLOT_SUBMITTED,
	AOA_SLOT_DONE,
	AOA_SLOT_DEQUEUED,
};

struct aoa_slot {
	struct aoa_slot_dev *dev;
	struct urb *urb;
	void *buf;
	dma_addr_t dma;
	size_t alloc_len;
	struct list_head list;
	enum aoa_slot_state state;
	size_t bytesused;
	int status;
	unsigned int retry_count;
	u64 sequence;
	u64 timestamp_ns;
};

struct aoa_slot_stats {
	u64 submitted_urbs;
	u64 completed_urbs;
	u64 completed_bytes;
	u64 dqbuf_calls;
	u64 qbuf_calls;
	u64 dqbuf_waits;
	u64 zero_length;
	u64 errors;
	u64 write_calls;
	u64 write_bytes;
	u64 max_done_slots;
	u64 max_user_slots;
};

struct aoa_slot_dev {
	struct usb_device *udev;
	struct usb_interface *intf;
	struct kref kref;
	struct mutex open_mutex;
	/*
	 * Serializes sleepable control, synchronous URB teardown, and slot/mmap
	 * lifetime; dev->lock protects queue state shared with completions.
	 */
	struct mutex io_mutex;
	/* Serializes synchronous bulk OUT against suspend and reset. */
	struct mutex write_mutex;
	spinlock_t lock;
	wait_queue_head_t read_wait;
	struct workqueue_struct *refill_wq;
	struct work_struct refill_work;
	struct work_struct stop_work;
	struct aoa_slot *slots;
	struct list_head queued;
	struct list_head done;
	unsigned int slot_count;
	unsigned int slot_size;
	unsigned int slot_alloc_len;
	unsigned int slot_mmap_stride;
	unsigned int active_urbs;
	unsigned int queued_count;
	unsigned int done_count;
	unsigned int user_count;
	unsigned int mmap_count;
	unsigned char bulk_in_ep;
	unsigned char bulk_out_ep;
	bool disconnected;
	bool opened;
	bool streaming;
	bool suspended;
	bool failed;
	bool clear_halt_pending;
	bool deferred_free;
	int last_error;
	u64 next_sequence;
	struct aoa_slot_stats stats;
};

static struct usb_driver aoa_slot_driver;
static void aoa_slot_delete(struct kref *kref);
static void aoa_slot_complete(struct urb *urb);

static unsigned int aoa_slot_clamp(unsigned int value, unsigned int min,
				  unsigned int max)
{
	if (value < min)
		return min;
	if (value > max)
		return max;
	return value;
}

static bool aoa_slot_busy_locked(struct aoa_slot_dev *dev)
{
	return dev->failed || dev->active_urbs || dev->done_count ||
	       dev->user_count;
}

static bool aoa_slot_retryable_status(int status)
{
	switch (status) {
	case -EPROTO:
	case -EILSEQ:
	case -ETIME:
	case -EREMOTEIO:
		return true;
	default:
		return false;
	}
}

static bool aoa_slot_is_disconnected(struct aoa_slot_dev *dev)
{
	unsigned long flags;
	bool disconnected;

	spin_lock_irqsave(&dev->lock, flags);
	disconnected = dev->disconnected;
	spin_unlock_irqrestore(&dev->lock, flags);
	return disconnected;
}

static void aoa_slot_fill_urb(struct aoa_slot_dev *dev,
			     struct aoa_slot *slot)
{
	usb_fill_bulk_urb(slot->urb, dev->udev,
			  usb_rcvbulkpipe(dev->udev, dev->bulk_in_ep),
			  slot->buf, dev->slot_size, aoa_slot_complete, slot);
	slot->urb->transfer_dma = slot->dma;
	slot->urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
}

static void aoa_slot_free_slot_array(struct aoa_slot *slots,
				    unsigned int count)
{
	unsigned int i;

	if (!slots)
		return;

	for (i = 0; i < count; i++) {
		struct aoa_slot *slot = &slots[i];

		usb_free_urb(slot->urb);
		if (slot->buf)
			usb_free_coherent(slot->dev->udev, slot->alloc_len,
					  slot->buf, slot->dma);
	}
	kfree(slots);
}

static int aoa_slot_alloc_slots_locked(struct aoa_slot_dev *dev,
				      unsigned int count,
				      unsigned int size)
{
	LIST_HEAD(queued);
	struct aoa_slot *slots;
	size_t alloc_len;
	size_t pool_size;
	unsigned int i;
	unsigned long flags;
	int ret = -ENOMEM;

	alloc_len = PAGE_ALIGN(ALIGN(size, dma_get_cache_alignment()));
	if (alloc_len > UINT_MAX)
		return -EINVAL;
	if (check_mul_overflow((size_t)count, alloc_len, &pool_size) ||
	    pool_size > AOA_SLOT_MAX_POOL_SIZE)
		return -ENOMEM;

	slots = kcalloc(count, sizeof(*slots), GFP_KERNEL);
	if (!slots)
		return -ENOMEM;

	for (i = 0; i < count; i++) {
		struct aoa_slot *slot = &slots[i];

		slot->dev = dev;
		slot->alloc_len = alloc_len;
		INIT_LIST_HEAD(&slot->list);
		slot->buf = usb_alloc_coherent(dev->udev, alloc_len, GFP_KERNEL,
					       &slot->dma);
		if (!slot->buf)
			goto error;
		memset(slot->buf, 0, alloc_len);

		slot->urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!slot->urb)
			goto error;
		slot->state = AOA_SLOT_QUEUED;
		list_add_tail(&slot->list, &queued);
	}

	spin_lock_irqsave(&dev->lock, flags);
	if (dev->disconnected) {
		ret = -ENODEV;
		spin_unlock_irqrestore(&dev->lock, flags);
		goto error;
	}
	INIT_LIST_HEAD(&dev->queued);
	INIT_LIST_HEAD(&dev->done);
	list_splice_tail_init(&queued, &dev->queued);
	dev->slots = slots;
	dev->slot_count = count;
	dev->slot_size = size;
	dev->slot_alloc_len = alloc_len;
	dev->slot_mmap_stride = alloc_len;
	dev->active_urbs = 0;
	dev->queued_count = count;
	dev->done_count = 0;
	dev->user_count = 0;
	dev->failed = false;
	dev->last_error = 0;
	dev->next_sequence = 0;
	memset(&dev->stats, 0, sizeof(dev->stats));
	spin_unlock_irqrestore(&dev->lock, flags);

	for (i = 0; i < count; i++)
		aoa_slot_fill_urb(dev, &slots[i]);

	return 0;

error:
	aoa_slot_free_slot_array(slots, count);
	return ret;
}

/* The caller must block new submissions before entering this stop path. */
static void aoa_slot_quiesce_urbs_locked(struct aoa_slot_dev *dev)
{
	unsigned long flags;
	unsigned int i;

	for (i = 0; i < dev->slot_count; i++)
		usb_poison_urb(dev->slots[i].urb);

	/* Keep URBs poisoned until an in-progress refill worker has exited. */
	if (dev->refill_wq)
		flush_workqueue(dev->refill_wq);

	spin_lock_irqsave(&dev->lock, flags);
	if (WARN_ON_ONCE(dev->active_urbs))
		dev->active_urbs = 0;
	spin_unlock_irqrestore(&dev->lock, flags);

	/* Keep every URB poisoned until all completion callbacks have exited. */
	for (i = 0; i < dev->slot_count; i++)
		usb_unpoison_urb(dev->slots[i].urb);
}

/* The caller holds io_mutex and has stopped new submissions. */
static int aoa_slot_clear_halt_locked(struct aoa_slot_dev *dev)
{
	unsigned long flags;
	bool clear_halt;
	int ret;

	spin_lock_irqsave(&dev->lock, flags);
	clear_halt = dev->clear_halt_pending && !dev->disconnected &&
		     !dev->suspended;
	spin_unlock_irqrestore(&dev->lock, flags);
	if (!clear_halt)
		return 0;

	ret = usb_clear_halt(dev->udev,
			     usb_rcvbulkpipe(dev->udev, dev->bulk_in_ep));
	spin_lock_irqsave(&dev->lock, flags);
	if (!ret) {
		dev->clear_halt_pending = false;
	} else if (!dev->disconnected) {
		dev->last_error = ret;
		dev->stats.errors++;
	}
	spin_unlock_irqrestore(&dev->lock, flags);
	if (ret)
		wake_up_interruptible(&dev->read_wait);

	return ret;
}

static void aoa_slot_stop_work(struct work_struct *work)
{
	struct aoa_slot_dev *dev = container_of(work, struct aoa_slot_dev,
					       stop_work);
	unsigned long flags;
	bool stop;

	mutex_lock(&dev->io_mutex);
	spin_lock_irqsave(&dev->lock, flags);
	stop = dev->failed && !dev->disconnected;
	spin_unlock_irqrestore(&dev->lock, flags);
	if (stop) {
		aoa_slot_quiesce_urbs_locked(dev);
		(void)aoa_slot_clear_halt_locked(dev);
	}
	mutex_unlock(&dev->io_mutex);
}

static void aoa_slot_schedule_stop(struct aoa_slot_dev *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	if (dev->failed && !dev->disconnected)
		/* Quiesce flushes refill_wq, so stop work uses system_wq. */
		schedule_work(&dev->stop_work);
	spin_unlock_irqrestore(&dev->lock, flags);
}

static void aoa_slot_rebuild_queues_locked(struct aoa_slot_dev *dev)
{
	unsigned int i;

	INIT_LIST_HEAD(&dev->queued);
	INIT_LIST_HEAD(&dev->done);
	dev->active_urbs = 0;
	dev->queued_count = 0;
	dev->done_count = 0;
	dev->user_count = 0;

	for (i = 0; i < dev->slot_count; i++) {
		struct aoa_slot *slot = &dev->slots[i];

		INIT_LIST_HEAD(&slot->list);
		if (slot->state == AOA_SLOT_DEQUEUED) {
			dev->user_count++;
			continue;
		}
		slot->bytesused = 0;
		slot->status = 0;
		slot->retry_count = 0;
		slot->state = AOA_SLOT_QUEUED;
		list_add_tail(&slot->list, &dev->queued);
		dev->queued_count++;
	}
}

static void aoa_slot_streamoff_locked(struct aoa_slot_dev *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	dev->streaming = false;
	spin_unlock_irqrestore(&dev->lock, flags);

	aoa_slot_quiesce_urbs_locked(dev);
	(void)aoa_slot_clear_halt_locked(dev);
	spin_lock_irqsave(&dev->lock, flags);
	aoa_slot_rebuild_queues_locked(dev);
	dev->failed = false;
	spin_unlock_irqrestore(&dev->lock, flags);
	wake_up_interruptible(&dev->read_wait);
}

static void aoa_slot_free_slots_locked(struct aoa_slot_dev *dev)
{
	struct aoa_slot *slots;
	unsigned int count;
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	dev->streaming = false;
	spin_unlock_irqrestore(&dev->lock, flags);

	aoa_slot_quiesce_urbs_locked(dev);
	(void)aoa_slot_clear_halt_locked(dev);

	spin_lock_irqsave(&dev->lock, flags);
	slots = dev->slots;
	count = dev->slot_count;
	INIT_LIST_HEAD(&dev->queued);
	INIT_LIST_HEAD(&dev->done);
	dev->slots = NULL;
	dev->slot_count = 0;
	dev->slot_size = 0;
	dev->slot_alloc_len = 0;
	dev->slot_mmap_stride = 0;
	dev->active_urbs = 0;
	dev->queued_count = 0;
	dev->done_count = 0;
	dev->user_count = 0;
	dev->failed = false;
	dev->deferred_free = false;
	spin_unlock_irqrestore(&dev->lock, flags);

	aoa_slot_free_slot_array(slots, count);
}

static int aoa_slot_submit_queued(struct aoa_slot_dev *dev, gfp_t mem_flags)
{
	struct aoa_slot *slot;
	unsigned long flags;
	bool report_error;
	int ret;

	for (;;) {
		spin_lock_irqsave(&dev->lock, flags);
		if (dev->disconnected || dev->suspended || !dev->streaming ||
		    !dev->slots || list_empty(&dev->queued)) {
			spin_unlock_irqrestore(&dev->lock, flags);
			return 0;
		}

		slot = list_first_entry(&dev->queued, struct aoa_slot, list);
		list_del_init(&slot->list);
		dev->queued_count--;
		slot->state = AOA_SLOT_SUBMITTED;
		dev->active_urbs++;
		dev->stats.submitted_urbs++;
		spin_unlock_irqrestore(&dev->lock, flags);

		ret = usb_submit_urb(slot->urb, mem_flags);
		if (!ret)
			continue;

		spin_lock_irqsave(&dev->lock, flags);
		if (dev->active_urbs)
			dev->active_urbs--;
		slot->state = AOA_SLOT_QUEUED;
		list_add_tail(&slot->list, &dev->queued);
		dev->queued_count++;
		report_error = dev->streaming && !dev->suspended &&
			       !dev->disconnected;
		if (report_error) {
			dev->streaming = false;
			dev->failed = true;
			dev->last_error = ret;
			dev->stats.errors++;
		}
		spin_unlock_irqrestore(&dev->lock, flags);

		if (report_error) {
			aoa_slot_schedule_stop(dev);
			wake_up_interruptible(&dev->read_wait);
		}
		return ret;
	}
}

static void aoa_slot_refill_work(struct work_struct *work)
{
	struct aoa_slot_dev *dev = container_of(work, struct aoa_slot_dev,
					       refill_work);

	(void)aoa_slot_submit_queued(dev, GFP_KERNEL);
}

static void aoa_slot_complete(struct urb *urb)
{
	struct aoa_slot *slot = urb->context;
	struct aoa_slot_dev *dev = slot->dev;
	unsigned long flags;
	bool refill = false;
	bool stop = false;
	bool wake = false;

	spin_lock_irqsave(&dev->lock, flags);
	if (dev->active_urbs)
		dev->active_urbs--;

	if (dev->disconnected || dev->suspended || !dev->streaming) {
		slot->state = AOA_SLOT_QUEUED;
		list_add_tail(&slot->list, &dev->queued);
		dev->queued_count++;
		wake = true;
	} else if (!urb->status) {
		slot->retry_count = 0;
		if (!urb->actual_length) {
			slot->state = AOA_SLOT_QUEUED;
			list_add_tail(&slot->list, &dev->queued);
			dev->queued_count++;
			dev->stats.zero_length++;
			refill = true;
		} else {
			slot->bytesused = urb->actual_length;
			slot->status = 0;
			slot->sequence = ++dev->next_sequence;
			slot->timestamp_ns = ktime_get_ns();
			slot->state = AOA_SLOT_DONE;
			list_add_tail(&slot->list, &dev->done);
			dev->done_count++;
			dev->stats.completed_urbs++;
			dev->stats.completed_bytes += urb->actual_length;
			if (dev->done_count > dev->stats.max_done_slots)
				dev->stats.max_done_slots = dev->done_count;
			refill = true;
			wake = true;
		}
	} else if (!urb->actual_length &&
		   aoa_slot_retryable_status(urb->status) &&
		   slot->retry_count < AOA_SLOT_MAX_SLOT_RETRIES) {
		slot->bytesused = 0;
		slot->status = 0;
		slot->retry_count++;
		slot->state = AOA_SLOT_QUEUED;
		list_add_tail(&slot->list, &dev->queued);
		dev->queued_count++;
		dev->stats.errors++;
		refill = true;
	} else {
		slot->bytesused = 0;
		slot->status = 0;
		slot->state = AOA_SLOT_QUEUED;
		list_add_tail(&slot->list, &dev->queued);
		dev->queued_count++;
		dev->streaming = false;
		dev->failed = true;
		dev->last_error = urb->status;
		if (urb->status == -EPIPE)
			dev->clear_halt_pending = true;
		dev->stats.errors++;
		stop = true;
		wake = true;
	}
	spin_unlock_irqrestore(&dev->lock, flags);

	if (refill)
		queue_work(dev->refill_wq, &dev->refill_work);
	if (stop)
		aoa_slot_schedule_stop(dev);
	if (wake)
		wake_up_interruptible(&dev->read_wait);
}

static int aoa_slot_query_caps(struct aoa_host_slot_caps __user *arg)
{
	struct aoa_host_slot_caps caps;

	memset(&caps, 0, sizeof(caps));
	caps.version = AOA_HOST_SLOT_VERSION;
	caps.flags = AOA_HOST_SLOT_FLAG_MMAP_PAYLOAD |
		     AOA_HOST_SLOT_FLAG_WRITE_BULK_OUT;
	caps.min_slot_size = AOA_SLOT_MIN_SLOT_SIZE;
	caps.max_slot_size = AOA_SLOT_MAX_SLOT_SIZE;
	caps.min_slot_count = AOA_SLOT_MIN_SLOT_COUNT;
	caps.max_slot_count = AOA_SLOT_MAX_SLOT_COUNT;

	return copy_to_user(arg, &caps, sizeof(caps)) ? -EFAULT : 0;
}

static int aoa_slot_reqbufs(struct aoa_slot_dev *dev,
			   struct aoa_host_slot_reqbufs __user *arg)
{
	struct aoa_host_slot_reqbufs req;
	unsigned int count;
	unsigned int size;
	unsigned long flags;
	bool busy;
	int ret = 0;

	if (copy_from_user(&req, arg, sizeof(req)))
		return -EFAULT;
	if (req.flags)
		return -EINVAL;

	mutex_lock(&dev->io_mutex);
	spin_lock_irqsave(&dev->lock, flags);
	if (dev->disconnected) {
		spin_unlock_irqrestore(&dev->lock, flags);
		ret = -ENODEV;
		goto out;
	}
	busy = dev->streaming || aoa_slot_busy_locked(dev);
	spin_unlock_irqrestore(&dev->lock, flags);
	if (busy) {
		ret = -EBUSY;
		goto out;
	}

	if (!req.slot_count) {
		if (dev->mmap_count) {
			ret = -EBUSY;
			goto out;
		}
		aoa_slot_free_slots_locked(dev);
		goto copy_back;
	}

	if (dev->mmap_count) {
		ret = -EBUSY;
		goto out;
	}
	if (!req.slot_size) {
		ret = -EINVAL;
		goto out;
	}
	aoa_slot_free_slots_locked(dev);
	if (aoa_slot_is_disconnected(dev)) {
		ret = -ENODEV;
		goto out;
	}

	count = aoa_slot_clamp(req.slot_count, AOA_SLOT_MIN_SLOT_COUNT,
			      AOA_SLOT_MAX_SLOT_COUNT);
	size = req.slot_size;
	size = aoa_slot_clamp(size, AOA_SLOT_MIN_SLOT_SIZE,
			     AOA_SLOT_MAX_SLOT_SIZE);

	ret = aoa_slot_alloc_slots_locked(dev, count, size);
	if (ret)
		goto out;
	req.slot_count = dev->slot_count;
	req.slot_size = dev->slot_size;

copy_back:
	if (copy_to_user(arg, &req, sizeof(req)))
		ret = -EFAULT;
out:
	mutex_unlock(&dev->io_mutex);
	return ret;
}

static int aoa_slot_querybuf(struct aoa_slot_dev *dev,
			    struct aoa_host_slot_buffer __user *arg)
{
	struct aoa_host_slot_buffer buffer;
	int ret = 0;

	if (copy_from_user(&buffer, arg, sizeof(buffer)))
		return -EFAULT;

	mutex_lock(&dev->io_mutex);
	if (aoa_slot_is_disconnected(dev)) {
		ret = -ENODEV;
		goto out;
	}
	if (!dev->slots || buffer.index >= dev->slot_count) {
		ret = -EINVAL;
		goto out;
	}
	buffer.length = dev->slot_size;
	buffer.mmap_length = dev->slot_alloc_len;
	buffer.data_offset = 0;
	buffer.mmap_offset = AOA_HOST_SLOT_MMAP_SLOT_BASE +
			     (u64)buffer.index * dev->slot_mmap_stride;
	if (copy_to_user(arg, &buffer, sizeof(buffer)))
		ret = -EFAULT;
out:
	mutex_unlock(&dev->io_mutex);
	return ret;
}

static int aoa_slot_streamon(struct aoa_slot_dev *dev)
{
	unsigned long flags;
	int ret;

	mutex_lock(&dev->io_mutex);
	if (!dev->slots) {
		ret = -EINVAL;
		goto out;
	}

	spin_lock_irqsave(&dev->lock, flags);
	if (dev->disconnected) {
		spin_unlock_irqrestore(&dev->lock, flags);
		ret = -ENODEV;
		goto out;
	}
	if (dev->streaming) {
		spin_unlock_irqrestore(&dev->lock, flags);
		ret = 0;
		goto out;
	}
	if (aoa_slot_busy_locked(dev)) {
		spin_unlock_irqrestore(&dev->lock, flags);
		ret = -EBUSY;
		goto out;
	}
	spin_unlock_irqrestore(&dev->lock, flags);

	ret = aoa_slot_clear_halt_locked(dev);
	if (ret)
		goto out;

	spin_lock_irqsave(&dev->lock, flags);
	if (dev->disconnected) {
		spin_unlock_irqrestore(&dev->lock, flags);
		ret = -ENODEV;
		goto out;
	}
	if (dev->streaming || aoa_slot_busy_locked(dev)) {
		spin_unlock_irqrestore(&dev->lock, flags);
		ret = -EBUSY;
		goto out;
	}
	dev->streaming = true;
	dev->last_error = 0;
	spin_unlock_irqrestore(&dev->lock, flags);

	ret = aoa_slot_submit_queued(dev, GFP_KERNEL);
	if (ret)
		aoa_slot_streamoff_locked(dev);
out:
	mutex_unlock(&dev->io_mutex);
	return ret;
}

static int aoa_slot_streamoff(struct aoa_slot_dev *dev)
{
	mutex_lock(&dev->io_mutex);
	aoa_slot_streamoff_locked(dev);
	mutex_unlock(&dev->io_mutex);
	return 0;
}

static bool aoa_slot_dqbuf_ready(struct aoa_slot_dev *dev)
{
	unsigned long flags;
	bool ready;

	spin_lock_irqsave(&dev->lock, flags);
	ready = dev->disconnected || !dev->slots || dev->failed ||
		dev->last_error || !dev->streaming || !list_empty(&dev->done);
	spin_unlock_irqrestore(&dev->lock, flags);
	return ready;
}

static int aoa_slot_dqbuf(struct aoa_slot_dev *dev, struct file *file,
			 struct aoa_host_slot_desc __user *arg)
{
	struct aoa_host_slot_desc desc;
	struct aoa_slot *slot;
	unsigned long flags;
	int ret;

	memset(&desc, 0, sizeof(desc));
	for (;;) {
		if (mutex_lock_interruptible(&dev->io_mutex))
			return -ERESTARTSYS;

		spin_lock_irqsave(&dev->lock, flags);
		if (!dev->slots) {
			ret = -EINVAL;
			goto unlock;
		}
		if (!list_empty(&dev->done)) {
			slot = list_first_entry(&dev->done, struct aoa_slot,
						list);
			list_del_init(&slot->list);
			dev->done_count--;
			slot->state = AOA_SLOT_DEQUEUED;
			dev->user_count++;
			dev->stats.dqbuf_calls++;
			if (dev->user_count > dev->stats.max_user_slots)
				dev->stats.max_user_slots = dev->user_count;
			desc.index = slot - dev->slots;
			desc.bytesused = slot->bytesused;
			desc.status = slot->status;
			desc.sequence = slot->sequence;
			desc.timestamp_ns = slot->timestamp_ns;
			spin_unlock_irqrestore(&dev->lock, flags);

			if (!copy_to_user(arg, &desc, sizeof(desc))) {
				mutex_unlock(&dev->io_mutex);
				return 0;
			}

			spin_lock_irqsave(&dev->lock, flags);
			slot->state = AOA_SLOT_DONE;
			list_add(&slot->list, &dev->done);
			dev->done_count++;
			dev->user_count--;
			spin_unlock_irqrestore(&dev->lock, flags);
			mutex_unlock(&dev->io_mutex);
			return -EFAULT;
		}
		if (dev->last_error) {
			ret = dev->last_error;
			dev->last_error = 0;
			goto unlock;
		}
		if (dev->disconnected) {
			ret = -ENODEV;
			goto unlock;
		}
		if (!dev->streaming) {
			ret = -EPIPE;
			goto unlock;
		}
		dev->stats.dqbuf_waits++;
		spin_unlock_irqrestore(&dev->lock, flags);
		mutex_unlock(&dev->io_mutex);

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		ret = wait_event_interruptible(dev->read_wait,
					       aoa_slot_dqbuf_ready(dev));
		if (ret)
			return ret;
		continue;

unlock:
		spin_unlock_irqrestore(&dev->lock, flags);
		mutex_unlock(&dev->io_mutex);
		return ret;
	}
}

static int aoa_slot_qbuf(struct aoa_slot_dev *dev,
			struct aoa_host_slot_desc __user *arg)
{
	struct aoa_host_slot_desc desc;
	struct aoa_slot *slot;
	unsigned long flags;
	bool disconnected = false;
	int ret = 0;

	if (copy_from_user(&desc, arg, sizeof(desc)))
		return -EFAULT;

	mutex_lock(&dev->io_mutex);
	spin_lock_irqsave(&dev->lock, flags);
	if (!dev->slots || desc.index >= dev->slot_count) {
		ret = -EINVAL;
		goto unlock;
	}
	slot = &dev->slots[desc.index];
	if (slot->state != AOA_SLOT_DEQUEUED) {
		ret = -EINVAL;
		goto unlock;
	}
	disconnected = dev->disconnected;
	slot->bytesused = 0;
	slot->status = 0;
	slot->state = AOA_SLOT_QUEUED;
	list_add_tail(&slot->list, &dev->queued);
	dev->queued_count++;
	dev->user_count--;
	dev->stats.qbuf_calls++;
unlock:
	spin_unlock_irqrestore(&dev->lock, flags);
	if (!ret && !disconnected)
		ret = aoa_slot_submit_queued(dev, GFP_KERNEL);
	mutex_unlock(&dev->io_mutex);
	return ret;
}

static void aoa_slot_fill_stats(struct aoa_slot_dev *dev,
			       struct aoa_host_slot_stats *out)
{
	struct aoa_slot_stats stats;
	unsigned long flags;

	memset(out, 0, sizeof(*out));
	mutex_lock(&dev->io_mutex);
	spin_lock_irqsave(&dev->lock, flags);
	stats = dev->stats;
	out->version = AOA_HOST_SLOT_VERSION;
	out->slot_size = dev->slot_size;
	out->slot_alloc_len = dev->slot_alloc_len;
	out->slot_count = dev->slot_count;
	out->submitted_slots = dev->active_urbs;
	out->queued_slots = dev->queued_count;
	out->done_slots = dev->done_count;
	out->user_slots = dev->user_count;
	out->last_error = dev->last_error;
	spin_unlock_irqrestore(&dev->lock, flags);
	out->mmap_count = dev->mmap_count;
	mutex_unlock(&dev->io_mutex);

	out->cache_alignment = dma_get_cache_alignment();
	out->page_size = PAGE_SIZE;
	out->submitted_urbs = stats.submitted_urbs;
	out->completed_urbs = stats.completed_urbs;
	out->completed_bytes = stats.completed_bytes;
	out->dqbuf_calls = stats.dqbuf_calls;
	out->qbuf_calls = stats.qbuf_calls;
	out->dqbuf_waits = stats.dqbuf_waits;
	out->zero_length = stats.zero_length;
	out->errors = stats.errors;
	out->write_calls = stats.write_calls;
	out->write_bytes = stats.write_bytes;
	out->max_done_slots = stats.max_done_slots;
	out->max_user_slots = stats.max_user_slots;
}

static int aoa_slot_get_stats(struct aoa_slot_dev *dev,
			     struct aoa_host_slot_stats __user *arg)
{
	struct aoa_host_slot_stats stats;

	aoa_slot_fill_stats(dev, &stats);
	return copy_to_user(arg, &stats, sizeof(stats)) ? -EFAULT : 0;
}

static long aoa_slot_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	struct aoa_slot_dev *dev = file->private_data;
	void __user *uarg = (void __user *)arg;

	if (!dev)
		return -ENODEV;

	switch (cmd) {
	case AOA_HOST_SLOT_IOC_QUERY_CAPS:
		return aoa_slot_query_caps(uarg);
	case AOA_HOST_SLOT_IOC_REQBUFS:
		return aoa_slot_reqbufs(dev, uarg);
	case AOA_HOST_SLOT_IOC_QUERYBUF:
		return aoa_slot_querybuf(dev, uarg);
	case AOA_HOST_SLOT_IOC_STREAMON:
		return aoa_slot_streamon(dev);
	case AOA_HOST_SLOT_IOC_STREAMOFF:
		return aoa_slot_streamoff(dev);
	case AOA_HOST_SLOT_IOC_DQBUF:
		return aoa_slot_dqbuf(dev, file, uarg);
	case AOA_HOST_SLOT_IOC_QBUF:
		return aoa_slot_qbuf(dev, uarg);
	case AOA_HOST_SLOT_IOC_GET_STATS:
		return aoa_slot_get_stats(dev, uarg);
	default:
		return -ENOTTY;
	}
}

static void aoa_slot_vma_open(struct vm_area_struct *vma)
{
	struct aoa_slot_dev *dev = vma->vm_private_data;

	if (!dev)
		return;
	kref_get(&dev->kref);
	mutex_lock(&dev->io_mutex);
	dev->mmap_count++;
	mutex_unlock(&dev->io_mutex);
}

static void aoa_slot_vma_close(struct vm_area_struct *vma)
{
	struct aoa_slot_dev *dev = vma->vm_private_data;
	bool free_slots = false;

	if (!dev)
		return;

	mutex_lock(&dev->io_mutex);
	if (dev->mmap_count)
		dev->mmap_count--;
	if (!dev->mmap_count && dev->deferred_free)
		free_slots = true;
	if (free_slots)
		aoa_slot_free_slots_locked(dev);
	mutex_unlock(&dev->io_mutex);
	kref_put(&dev->kref, aoa_slot_delete);
}

static const struct vm_operations_struct aoa_slot_vm_ops = {
	.open = aoa_slot_vma_open,
	.close = aoa_slot_vma_close,
};

static int aoa_slot_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct aoa_slot_dev *dev = file->private_data;
	struct aoa_slot *slot;
	struct usb_hcd *hcd;
	u64 offset;
	u64 relative;
	u64 index64;
	u32 rem;
	unsigned int index;
	unsigned long flags;
	unsigned long size;
	unsigned long vm_pgoff;
	unsigned long pfn;
	int ret = 0;

	if (!dev)
		return -ENODEV;
	if (!(vma->vm_flags & VM_SHARED) ||
	    !(vma->vm_flags & VM_READ) ||
	    (vma->vm_flags & (VM_WRITE | VM_EXEC)))
		return -EINVAL;
	vma->vm_flags &= ~(VM_MAYWRITE | VM_MAYEXEC);

	offset = (u64)vma->vm_pgoff << PAGE_SHIFT;
	size = vma->vm_end - vma->vm_start;
	mutex_lock(&dev->io_mutex);
	if (aoa_slot_is_disconnected(dev)) {
		ret = -ENODEV;
		goto out;
	}
	if (!dev->slots || offset < AOA_HOST_SLOT_MMAP_SLOT_BASE ||
	    !dev->slot_mmap_stride) {
		ret = -EINVAL;
		goto out;
	}

	relative = offset - AOA_HOST_SLOT_MMAP_SLOT_BASE;
	index64 = div_u64_rem(relative, dev->slot_mmap_stride, &rem);
	if (rem || index64 >= dev->slot_count) {
		ret = -EINVAL;
		goto out;
	}
	index = index64;
	slot = &dev->slots[index];
	if (size != slot->alloc_len || offset_in_page(slot->buf)) {
		ret = -EINVAL;
		goto out;
	}

	hcd = bus_to_hcd(dev->udev->bus);
	vma->vm_flags |= VM_IO | VM_DONTEXPAND | VM_DONTDUMP;
	vma->vm_ops = &aoa_slot_vm_ops;
	vma->vm_private_data = dev;
	if (hcd->localmem_pool || !hcd_uses_dma(hcd)) {
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
		pfn = virt_to_phys(slot->buf) >> PAGE_SHIFT;
		ret = remap_pfn_range(vma, vma->vm_start, pfn, size,
				      vma->vm_page_prot);
	} else {
		vm_pgoff = vma->vm_pgoff;
		vma->vm_pgoff = 0;
		ret = dma_mmap_coherent(hcd->self.sysdev, vma, slot->buf,
					slot->dma, size);
		vma->vm_pgoff = vm_pgoff;
	}

	if (!ret) {
		spin_lock_irqsave(&dev->lock, flags);
		if (dev->disconnected) {
			/* mmap core tears down the new mapping on this error. */
			ret = -ENODEV;
		} else {
			kref_get(&dev->kref);
			dev->mmap_count++;
		}
		spin_unlock_irqrestore(&dev->lock, flags);
	}
out:
	mutex_unlock(&dev->io_mutex);
	return ret;
}

static int aoa_slot_open(struct inode *inode, struct file *file)
{
	struct usb_interface *intf;
	struct aoa_slot_dev *dev;
	int ret = 0;

	intf = usb_find_interface(&aoa_slot_driver, iminor(inode));
	if (!intf)
		return -ENODEV;
	dev = usb_get_intfdata(intf);
	if (!dev)
		return -ENODEV;

	ret = usb_autopm_get_interface(intf);
	if (ret)
		return ret;

	mutex_lock(&dev->open_mutex);
	if (aoa_slot_is_disconnected(dev))
		ret = -ENODEV;
	else if (dev->opened)
		ret = -EBUSY;
	else {
		dev->opened = true;
		kref_get(&dev->kref);
		file->private_data = dev;
	}
	mutex_unlock(&dev->open_mutex);
	if (ret)
		usb_autopm_put_interface(intf);
	return ret;
}

static int aoa_slot_release(struct inode *inode, struct file *file)
{
	struct aoa_slot_dev *dev = file->private_data;

	if (!dev)
		return 0;

	mutex_lock(&dev->io_mutex);
	if (dev->mmap_count) {
		aoa_slot_streamoff_locked(dev);
		dev->deferred_free = true;
	} else {
		aoa_slot_free_slots_locked(dev);
	}
	mutex_unlock(&dev->io_mutex);

	mutex_lock(&dev->open_mutex);
	dev->opened = false;
	mutex_unlock(&dev->open_mutex);
	file->private_data = NULL;
	usb_autopm_put_interface(dev->intf);
	kref_put(&dev->kref, aoa_slot_delete);
	return 0;
}

static ssize_t aoa_slot_write(struct file *file, const char __user *buffer,
			     size_t count, loff_t *ppos)
{
	struct aoa_slot_dev *dev = file->private_data;
	unsigned long flags;
	void *kbuf;
	bool disconnected;
	bool suspended;
	int actual = 0;
	int ret;

	if (!dev || !count)
		return 0;
	if (count > AOA_SLOT_MAX_SLOT_SIZE)
		return -EINVAL;

	kbuf = memdup_user(buffer, count);
	if (IS_ERR(kbuf))
		return PTR_ERR(kbuf);

	mutex_lock(&dev->write_mutex);
	spin_lock_irqsave(&dev->lock, flags);
	disconnected = dev->disconnected;
	suspended = dev->suspended;
	spin_unlock_irqrestore(&dev->lock, flags);
	if (disconnected || !dev->bulk_out_ep) {
		ret = -ENODEV;
	} else if (suspended) {
		ret = -EHOSTUNREACH;
	} else {
		ret = usb_bulk_msg(dev->udev,
				   usb_sndbulkpipe(dev->udev, dev->bulk_out_ep),
				   kbuf, count, &actual,
				   AOA_SLOT_WRITE_TIMEOUT_MS);
		if (!ret) {
			spin_lock_irqsave(&dev->lock, flags);
			dev->stats.write_calls++;
			dev->stats.write_bytes += actual;
			spin_unlock_irqrestore(&dev->lock, flags);
			ret = actual;
		}
	}
	mutex_unlock(&dev->write_mutex);
	kfree(kbuf);
	return ret;
}

static __poll_t aoa_slot_poll(struct file *file, poll_table *wait)
{
	struct aoa_slot_dev *dev = file->private_data;
	unsigned long flags;
	__poll_t mask = 0;

	if (!dev)
		return EPOLLERR;

	poll_wait(file, &dev->read_wait, wait);
	spin_lock_irqsave(&dev->lock, flags);
	if (dev->done_count)
		mask |= EPOLLIN | EPOLLRDNORM;
	if (!dev->disconnected && !dev->suspended && dev->bulk_out_ep)
		mask |= EPOLLOUT | EPOLLWRNORM;
	if (dev->disconnected || dev->failed || dev->last_error ||
	    !dev->streaming)
		mask |= EPOLLERR | EPOLLHUP;
	spin_unlock_irqrestore(&dev->lock, flags);
	return mask;
}

static const struct file_operations aoa_slot_fops = {
	.owner = THIS_MODULE,
	.open = aoa_slot_open,
	.release = aoa_slot_release,
	.unlocked_ioctl = aoa_slot_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = aoa_slot_ioctl,
#endif
	.mmap = aoa_slot_mmap,
	.write = aoa_slot_write,
	.poll = aoa_slot_poll,
	.llseek = noop_llseek,
};

static struct usb_class_driver aoa_slot_class = {
	.name = "aoa_host_slot%d",
	.fops = &aoa_slot_fops,
	.minor_base = AOA_SLOT_MINOR_BASE,
};

static void aoa_slot_delete(struct kref *kref)
{
	struct aoa_slot_dev *dev = container_of(kref, struct aoa_slot_dev, kref);

	cancel_work_sync(&dev->stop_work);
	mutex_lock(&dev->io_mutex);
	aoa_slot_free_slots_locked(dev);
	mutex_unlock(&dev->io_mutex);
	if (dev->refill_wq)
		destroy_workqueue(dev->refill_wq);
	usb_put_intf(dev->intf);
	usb_put_dev(dev->udev);
	kfree(dev);
}

static int aoa_slot_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct usb_host_interface *alt = intf->cur_altsetting;
	struct usb_endpoint_descriptor *endpoint;
	struct aoa_slot_dev *dev;
	unsigned int i;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	kref_init(&dev->kref);
	mutex_init(&dev->open_mutex);
	mutex_init(&dev->io_mutex);
	mutex_init(&dev->write_mutex);
	spin_lock_init(&dev->lock);
	init_waitqueue_head(&dev->read_wait);
	INIT_WORK(&dev->refill_work, aoa_slot_refill_work);
	INIT_WORK(&dev->stop_work, aoa_slot_stop_work);
	INIT_LIST_HEAD(&dev->queued);
	INIT_LIST_HEAD(&dev->done);
	dev->udev = usb_get_dev(interface_to_usbdev(intf));
	dev->intf = usb_get_intf(intf);

	if (alt->desc.bInterfaceClass == USB_CLASS_VENDOR_SPEC &&
	    alt->desc.bInterfaceSubClass == AOA_SLOT_ADB_SUBCLASS &&
	    alt->desc.bInterfaceProtocol == AOA_SLOT_ADB_PROTOCOL) {
		ret = -ENODEV;
		goto error;
	}

	for (i = 0; i < alt->desc.bNumEndpoints; i++) {
		endpoint = &alt->endpoint[i].desc;
		if (!dev->bulk_in_ep && usb_endpoint_is_bulk_in(endpoint))
			dev->bulk_in_ep = endpoint->bEndpointAddress;
		if (!dev->bulk_out_ep && usb_endpoint_is_bulk_out(endpoint))
			dev->bulk_out_ep = endpoint->bEndpointAddress;
	}
	if (!dev->bulk_in_ep || !dev->bulk_out_ep) {
		ret = -ENODEV;
		goto error;
	}
	dev->refill_wq = alloc_workqueue("aoa_slot_refill",
					 WQ_UNBOUND | WQ_HIGHPRI, 1);
	if (!dev->refill_wq) {
		ret = -ENOMEM;
		goto error;
	}

	usb_set_intfdata(intf, dev);
	ret = usb_register_dev(intf, &aoa_slot_class);
	if (ret) {
		usb_set_intfdata(intf, NULL);
		goto error;
	}

	dev_info(&intf->dev,
		 "AOA host slot ready: /dev/aoa_host_slot%d\n",
		 intf->minor - AOA_SLOT_MINOR_BASE);
	return 0;

error:
	kref_put(&dev->kref, aoa_slot_delete);
	return ret;
}

static void aoa_slot_disconnect(struct usb_interface *intf)
{
	struct aoa_slot_dev *dev = usb_get_intfdata(intf);
	unsigned long flags;

	usb_set_intfdata(intf, NULL);
	if (!dev)
		return;
	usb_deregister_dev(intf, &aoa_slot_class);

	spin_lock_irqsave(&dev->lock, flags);
	dev->disconnected = true;
	dev->streaming = false;
	dev->clear_halt_pending = false;
	dev->last_error = -ENODEV;
	spin_unlock_irqrestore(&dev->lock, flags);

	cancel_work_sync(&dev->stop_work);
	mutex_lock(&dev->io_mutex);
	aoa_slot_streamoff_locked(dev);
	mutex_unlock(&dev->io_mutex);
	wake_up_interruptible(&dev->read_wait);
	dev_info(&intf->dev, "AOA host slot disconnected\n");
	kref_put(&dev->kref, aoa_slot_delete);
}

static int aoa_slot_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct aoa_slot_dev *dev = usb_get_intfdata(intf);
	unsigned long flags;
	bool quiesce = false;
	int ret = 0;

	if (!dev)
		return 0;

	mutex_lock(&dev->io_mutex);
	mutex_lock(&dev->write_mutex);
	spin_lock_irqsave(&dev->lock, flags);
	if (dev->disconnected || dev->suspended) {
		spin_unlock_irqrestore(&dev->lock, flags);
		goto out;
	}
	if (PMSG_IS_AUTO(message) && dev->streaming) {
		spin_unlock_irqrestore(&dev->lock, flags);
		ret = -EBUSY;
		goto out;
	}
	dev->suspended = true;
	quiesce = true;
	spin_unlock_irqrestore(&dev->lock, flags);

	if (quiesce)
		aoa_slot_quiesce_urbs_locked(dev);
out:
	mutex_unlock(&dev->write_mutex);
	mutex_unlock(&dev->io_mutex);
	return ret;
}

static int aoa_slot_resume_locked(struct aoa_slot_dev *dev)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&dev->lock, flags);
	if (dev->disconnected) {
		spin_unlock_irqrestore(&dev->lock, flags);
		return -ENODEV;
	}
	dev->suspended = false;
	spin_unlock_irqrestore(&dev->lock, flags);

	ret = aoa_slot_clear_halt_locked(dev);
	if (ret)
		return ret;
	ret = aoa_slot_submit_queued(dev, GFP_NOIO);
	if (ret)
		aoa_slot_quiesce_urbs_locked(dev);
	return ret;
}

static int aoa_slot_resume(struct usb_interface *intf)
{
	struct aoa_slot_dev *dev = usb_get_intfdata(intf);
	int ret;

	if (!dev)
		return 0;

	mutex_lock(&dev->io_mutex);
	mutex_lock(&dev->write_mutex);
	ret = aoa_slot_resume_locked(dev);
	mutex_unlock(&dev->write_mutex);
	mutex_unlock(&dev->io_mutex);
	return ret;
}

static const struct usb_device_id aoa_slot_table[] = {
	{ USB_DEVICE(AOA_SLOT_VENDOR_ID, AOA_SLOT_PRODUCT_ACCESSORY) },
	{ USB_DEVICE(AOA_SLOT_VENDOR_ID, AOA_SLOT_PRODUCT_ACCESSORY_ADB) },
	{ USB_DEVICE(AOA_SLOT_VENDOR_ID, AOA_SLOT_PRODUCT_ACCESSORY_AUDIO) },
	{ USB_DEVICE(AOA_SLOT_VENDOR_ID, AOA_SLOT_PRODUCT_ACCESSORY_AUDIO_ADB) },
	{ }
};
MODULE_DEVICE_TABLE(usb, aoa_slot_table);

static struct usb_driver aoa_slot_driver = {
	.name = "aoa_host_slot",
	.probe = aoa_slot_probe,
	.disconnect = aoa_slot_disconnect,
	.suspend = aoa_slot_suspend,
	.resume = aoa_slot_resume,
	.reset_resume = aoa_slot_resume,
	.id_table = aoa_slot_table,
	.supports_autosuspend = 1,
};

module_usb_driver(aoa_slot_driver);

MODULE_DESCRIPTION("Android Open Accessory host slot driver");
MODULE_LICENSE("GPL");
