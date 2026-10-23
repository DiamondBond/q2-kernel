// SPDX-License-Identifier: GPL-2.0+
/*
 * f_nero.c - USB nero function driver
 *
 * Copied from gadget/function/f_printer.c,
 * which was:
 *
 * nero.c -- Nero gadget driver
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/idr.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/moduleparam.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/cdev.h>
#include <linux/kref.h>
#include <linux/completion.h>
#include <linux/dma-mapping.h>

#include <asm/byteorder.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/uaccess.h>
#include <asm/unaligned.h>

#include <linux/usb/ch9.h>
#include <linux/usb/composite.h>
#include <linux/usb/gadget.h>

#include <linux/usb/composite.h>

struct f_nero_opts {
	struct usb_function_instance	func_inst;
	int				minor;
	unsigned int			tx_q_len;
	unsigned int			rx_q_len;
	unsigned int			tx_req_buf_size;
	unsigned int			rx_req_buf_size;

	/*
	 * Protect the data from concurrent access by read/write
	 * and create symlink/remove symlink
	 */
	struct mutex			lock;
	int				refcnt;
};
#define PRINTER_MINORS		4
#define GET_PORT_STATUS		1

static int major, minors;
static struct class *usb_gadget_class;
static DEFINE_IDA(nero_ida);
static DEFINE_MUTEX(nero_ida_lock); /* protects access do nero_ida */

/*-------------------------------------------------------------------------*/

struct nero_file;
struct nero_cdev;

struct nero_dev {
	spinlock_t		lock;		/* lock this structure */
	/* Serialize per-direction I/O without blocking the opposite path. */
	struct mutex		lock_nero_rx;
	struct mutex		lock_nero_tx;
	struct usb_gadget	*gadget;
	s8			interface;
	struct usb_ep		*in_ep, *out_ep;
	struct kref             kref;
	struct list_head	rx_reqs;	/* List of free RX structs */
	struct list_head	rx_reqs_active;	/* List of Active RX xfers */
	struct list_head	rx_buffers;	/* List of completed xfers */
	/* wait until there is data to be read. */
	wait_queue_head_t	rx_wait;
	struct list_head	tx_reqs;	/* List of free TX structs */
	struct list_head	tx_reqs_active; /* List of Active TX xfers */
	/* Wait until there are write buffers available to use. */
	wait_queue_head_t	tx_wait;
	/* Wait until all write buffers have been sent. */
	wait_queue_head_t	tx_flush_wait;
	struct usb_request	*current_rx_req;
	size_t			current_rx_bytes;
	u8			*current_rx_buf;
	u8			nero_status;
	unsigned int		reset_seq;
	unsigned int		lifecycle_seq;
	unsigned int		set_alt_inflight;
	struct completion	set_alt_idle;
	bool			unbound;
	bool			cdev_added;
	/* Once old cdev references survive unbind, this instance must not reuse it. */
	bool			cdev_reuse_blocked;
	int			minor;
	struct nero_cdev	*nero_cdev;
	u8			nero_cdev_open;
	struct nero_file	*open_file;
	wait_queue_head_t	wait;
	unsigned int		tx_q_len;
	unsigned int		rx_q_len;
	unsigned int		tx_req_buf_size;
	unsigned int		rx_req_buf_size;
	struct usb_function	function;
};

struct nero_file {
	struct nero_dev		*dev;
	unsigned int		lifecycle_seq;
};

struct nero_cdev {
	struct cdev		cdev;
	struct device		dev;
	struct nero_dev		*nero;
};

static const struct file_operations nero_io_operations;

static inline struct nero_dev *func_to_nero(struct usb_function *f)
{
	return container_of(f, struct nero_dev, function);
}

static inline bool nero_unavailable(struct nero_dev *dev)
{
	return READ_ONCE(dev->unbound) || READ_ONCE(dev->interface) < 0;
}

static inline bool nero_file_unavailable(struct nero_dev *dev,
		unsigned int lifecycle_seq)
{
	return READ_ONCE(dev->lifecycle_seq) != lifecycle_seq ||
	       nero_unavailable(dev);
}

static void nero_warn_cdev_boundary(struct nero_dev *dev, const char *where,
		const char *reason)
{
	unsigned long flags;
	bool cdev_added;
	bool cdev_open;
	bool cdev_reuse_blocked;
	unsigned int lifecycle_seq;
	s8 interface;

	spin_lock_irqsave(&dev->lock, flags);
	cdev_added = dev->cdev_added;
	cdev_open = dev->nero_cdev_open || dev->open_file;
	cdev_reuse_blocked = dev->cdev_reuse_blocked;
	lifecycle_seq = dev->lifecycle_seq;
	interface = dev->interface;
	spin_unlock_irqrestore(&dev->lock, flags);

	WARNING(dev, "%s: %s, pid=%d comm=%s minor=%d cdev_added=%u open=%u reuse_blocked=%u intf=%d lifecycle_seq=%u\n",
		where, reason, task_pid_nr(current), current->comm, dev->minor,
		cdev_added, cdev_open, cdev_reuse_blocked, interface,
		lifecycle_seq);
}

static int nero_reject_stale_cdev_rebind(struct nero_dev *dev)
{
	unsigned long flags;
	bool cdev_added;
	bool cdev_open;
	bool cdev_reuse_blocked;
	const char *reason;

	spin_lock_irqsave(&dev->lock, flags);
	cdev_added = dev->cdev_added;
	cdev_open = dev->nero_cdev_open || dev->open_file;
	cdev_reuse_blocked = dev->cdev_reuse_blocked;
	spin_unlock_irqrestore(&dev->lock, flags);

	if (!cdev_added && !cdev_open && !cdev_reuse_blocked)
		return 0;

	if (cdev_reuse_blocked)
		reason = "refuse cdev reuse after unbind with stale fd; recreate function instance";
	else
		reason = "refuse to reinitialize cdev while previous cdev/fd is still active";
	nero_warn_cdev_boundary(dev, "bind", reason);
	return -EBUSY;
}

static struct nero_cdev *nero_mark_cdev_deleted(struct nero_dev *dev,
		bool *cdev_added)
{
	unsigned long flags;
	struct nero_cdev *nero_cdev;

	spin_lock_irqsave(&dev->lock, flags);
	*cdev_added = dev->cdev_added;
	nero_cdev = dev->nero_cdev;
	dev->cdev_added = false;
	dev->nero_cdev = NULL;
	spin_unlock_irqrestore(&dev->lock, flags);

	return nero_cdev;
}

static bool nero_block_cdev_reuse_if_busy(struct nero_dev *dev,
		bool cdev_was_added, struct nero_cdev *nero_cdev)
{
	unsigned long flags;
	bool open;
	bool busy;
	unsigned int cdev_refs = 0;

	spin_lock_irqsave(&dev->lock, flags);
	open = dev->nero_cdev_open || dev->open_file;
	spin_unlock_irqrestore(&dev->lock, flags);

	if (cdev_was_added && nero_cdev)
		cdev_refs = kref_read(&nero_cdev->cdev.kobj.kref);

	/*
	 * Before cdev_device_del(), one cdev kref is the live cdev itself.
	 * Anything above that means an inode/fd path still references it.
	 */
	busy = open || cdev_refs > 1;
	if (!busy)
		return false;

	spin_lock_irqsave(&dev->lock, flags);
	if (open || cdev_refs > 1)
		dev->cdev_reuse_blocked = true;
	spin_unlock_irqrestore(&dev->lock, flags);

	return true;
}

static int nero_enter_set_alt(struct nero_dev *dev)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&dev->lock, flags);
	if (dev->unbound) {
		ret = -ENODEV;
	} else {
		if (!dev->set_alt_inflight)
			reinit_completion(&dev->set_alt_idle);
		dev->set_alt_inflight++;
	}
	spin_unlock_irqrestore(&dev->lock, flags);

	return ret;
}

static void nero_leave_set_alt(struct nero_dev *dev)
{
	unsigned long flags;
	bool complete_idle = false;

	spin_lock_irqsave(&dev->lock, flags);
	if (WARN_ON(!dev->set_alt_inflight)) {
		spin_unlock_irqrestore(&dev->lock, flags);
		return;
	}

	dev->set_alt_inflight--;
	if (!dev->set_alt_inflight && dev->unbound)
		complete_idle = true;
	spin_unlock_irqrestore(&dev->lock, flags);

	if (complete_idle)
		complete_all(&dev->set_alt_idle);
}

static void nero_begin_unbind(struct nero_dev *dev)
{
	unsigned long flags;
	bool wait_set_alt;

	spin_lock_irqsave(&dev->lock, flags);
	dev->unbound = true;
	dev->interface = -1;
	dev->reset_seq++;
	dev->lifecycle_seq++;
	wait_set_alt = dev->set_alt_inflight != 0;
	spin_unlock_irqrestore(&dev->lock, flags);

	wake_up_interruptible(&dev->rx_wait);
	wake_up_interruptible(&dev->tx_wait);
	wake_up_interruptible(&dev->tx_flush_wait);

	if (wait_set_alt)
		wait_for_completion(&dev->set_alt_idle);
}

static void nero_mark_bound(struct nero_dev *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	dev->unbound = false;
	dev->interface = -1;
	spin_unlock_irqrestore(&dev->lock, flags);
}

static void nero_mark_unbound(struct nero_dev *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	dev->unbound = true;
	dev->interface = -1;
	dev->reset_seq++;
	dev->lifecycle_seq++;
	spin_unlock_irqrestore(&dev->lock, flags);
}

/*-------------------------------------------------------------------------*/

/*
 * DESCRIPTORS ... most are static, but strings and (full) configuration
 * descriptors are built on demand.
 */

/* holds our biggest descriptor */
#define NERO_REQ_BUF_SIZE		8192
#define NERO_RX_Q_LEN			48
#define NERO_TX_Q_LEN			8
#define NERO_INTERFACE_STRING		0

static struct usb_interface_assoc_descriptor iad_desc = {
	.bLength =		sizeof(iad_desc),
	.bDescriptorType =	USB_DT_INTERFACE_ASSOCIATION,
	.bInterfaceCount =	1,
	.bFunctionClass =	USB_CLASS_VENDOR_SPEC,
	.bFunctionSubClass =	0x2a,
	.bFunctionProtocol =	0xff,
	.iFunction =		0,
};

static struct usb_interface_descriptor intf_desc = {
	.bLength =		sizeof(intf_desc),
	.bDescriptorType =	USB_DT_INTERFACE,
	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass =	0x2a,
	.bInterfaceProtocol =	0xff,
	.iInterface =		0
};

static struct usb_endpoint_descriptor fs_ep_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK
};

static struct usb_endpoint_descriptor fs_ep_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK
};

static struct usb_descriptor_header *fs_nero_function[] = {
	(struct usb_descriptor_header *) &iad_desc,
	(struct usb_descriptor_header *) &intf_desc,
	(struct usb_descriptor_header *) &fs_ep_out_desc,
	(struct usb_descriptor_header *) &fs_ep_in_desc,
	NULL
};

/*
 * usb 2.0 devices need to expose both high speed and full speed
 * descriptors, unless they only run at full speed.
 */

static struct usb_endpoint_descriptor hs_ep_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512)
};

static struct usb_endpoint_descriptor hs_ep_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512)
};

static struct usb_descriptor_header *hs_nero_function[] = {
	(struct usb_descriptor_header *) &iad_desc,
	(struct usb_descriptor_header *) &intf_desc,
	(struct usb_descriptor_header *) &hs_ep_out_desc,
	(struct usb_descriptor_header *) &hs_ep_in_desc,
	NULL
};

/*
 * Added endpoint descriptors for 3.0 devices
 */

static struct usb_endpoint_descriptor ss_ep_in_desc = {
	.bLength =              USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =      USB_DT_ENDPOINT,
	.bmAttributes =         USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =       cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor ss_ep_in_comp_desc = {
	.bLength =              sizeof(ss_ep_in_comp_desc),
	.bDescriptorType =      USB_DT_SS_ENDPOINT_COMP,
};

static struct usb_endpoint_descriptor ss_ep_out_desc = {
	.bLength =              USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =      USB_DT_ENDPOINT,
	.bmAttributes =         USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =       cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor ss_ep_out_comp_desc = {
	.bLength =              sizeof(ss_ep_out_comp_desc),
	.bDescriptorType =      USB_DT_SS_ENDPOINT_COMP,
};

static struct usb_descriptor_header *ss_nero_function[] = {
	(struct usb_descriptor_header *) &iad_desc,
	(struct usb_descriptor_header *) &intf_desc,
	(struct usb_descriptor_header *) &ss_ep_out_desc,
	(struct usb_descriptor_header *) &ss_ep_out_comp_desc,
	(struct usb_descriptor_header *) &ss_ep_in_desc,
	(struct usb_descriptor_header *) &ss_ep_in_comp_desc,
	NULL
};

static struct usb_string nero_string_defs[] = {
	[NERO_INTERFACE_STRING].s = "Nero",
	{  }
};

static struct usb_gadget_strings nero_string_table = {
	.language =		0x0409,
	.strings =		nero_string_defs,
};

static struct usb_gadget_strings *nero_strings[] = {
	&nero_string_table,
	NULL,
};

/* maxpacket and other transfer characteristics vary by speed. */
static inline struct usb_endpoint_descriptor *ep_desc(struct usb_gadget *gadget,
					struct usb_endpoint_descriptor *fs,
					struct usb_endpoint_descriptor *hs,
					struct usb_endpoint_descriptor *ss)
{
	switch (gadget->speed) {
	case USB_SPEED_SUPER:
		return ss;
	case USB_SPEED_HIGH:
		return hs;
	default:
		return fs;
	}
}

/*-------------------------------------------------------------------------*/

static void nero_dev_free(struct kref *kref)
{
	struct nero_dev *dev = container_of(kref, struct nero_dev, kref);

	kfree(dev);
}

static void nero_cdev_release(struct device *device)
{
	struct nero_cdev *nero_cdev;

	nero_cdev = container_of(device, struct nero_cdev, dev);
	if (nero_cdev->nero)
		kref_put(&nero_cdev->nero->kref, nero_dev_free);
	kfree(nero_cdev);
}

static struct nero_cdev *nero_cdev_alloc(struct nero_dev *dev)
{
	struct nero_cdev *nero_cdev;
	int ret;

	nero_cdev = kzalloc(sizeof(*nero_cdev), GFP_KERNEL);
	if (!nero_cdev)
		return ERR_PTR(-ENOMEM);

	nero_cdev->nero = dev;
	kref_get(&dev->kref);

	device_initialize(&nero_cdev->dev);
	nero_cdev->dev.class = usb_gadget_class;
	nero_cdev->dev.devt = MKDEV(major, dev->minor);
	nero_cdev->dev.release = nero_cdev_release;
	ret = dev_set_name(&nero_cdev->dev, "nero%d", dev->minor);
	if (ret) {
		put_device(&nero_cdev->dev);
		return ERR_PTR(ret);
	}

	cdev_init(&nero_cdev->cdev, &nero_io_operations);
	nero_cdev->cdev.owner = THIS_MODULE;

	return nero_cdev;
}

static struct usb_request *
nero_req_alloc(struct usb_ep *ep, unsigned len, gfp_t gfp_flags)
{
	struct usb_request	*req;

	req = usb_ep_alloc_request(ep, gfp_flags);

	if (req != NULL) {
		req->length = len;
		req->buf = kmalloc(ALIGN(len, dma_get_cache_alignment()), gfp_flags);
		if (req->buf == NULL) {
			usb_ep_free_request(ep, req);
			return NULL;
		}
	}

	return req;
}

static void
nero_req_free(struct usb_ep *ep, struct usb_request *req)
{
	if (ep != NULL && req != NULL) {
		kfree(req->buf);
		usb_ep_free_request(ep, req);
	}
}

static void nero_move_reqs(struct list_head *from, struct list_head *to)
{
	struct usb_request *req;

	while (!list_empty(from)) {
		req = list_first_entry(from, struct usb_request, list);
		list_move_tail(&req->list, to);
	}
}

static void nero_free_reqs(struct usb_ep *ep, struct list_head *head)
{
	struct usb_request *req;

	while (!list_empty(head)) {
		req = list_first_entry(head, struct usb_request, list);
		list_del(&req->list);
		nero_req_free(ep, req);
	}
}

/*-------------------------------------------------------------------------*/

static void rx_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct nero_dev	*dev = ep->driver_data;
	int			status = req->status;
	unsigned long		flags;

	spin_lock_irqsave(&dev->lock, flags);

	list_del_init(&req->list);	/* Remode from Active List */

	switch (status) {

	/* normal completion */
	case 0:
		if (req->actual > 0) {
			list_add_tail(&req->list, &dev->rx_buffers);
			DBG(dev, "G_Printer : rx length %d\n", req->actual);
		} else {
			list_add(&req->list, &dev->rx_reqs);
		}
		break;

	/* software-driven interface shutdown */
	case -ECONNRESET:		/* unlink */
	case -ESHUTDOWN:		/* disconnect etc */
		VDBG(dev, "rx shutdown, code %d\n", status);
		list_add(&req->list, &dev->rx_reqs);
		break;

	/* for hardware automagic (such as pxa) */
	case -ECONNABORTED:		/* endpoint reset */
		DBG(dev, "rx %s reset\n", ep->name);
		list_add(&req->list, &dev->rx_reqs);
		break;

	/* data overrun */
	case -EOVERFLOW:
		fallthrough;

	default:
		DBG(dev, "rx status %d\n", status);
		list_add(&req->list, &dev->rx_reqs);
		break;
	}

	wake_up_interruptible(&dev->rx_wait);
	spin_unlock_irqrestore(&dev->lock, flags);
}

static void tx_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct nero_dev	*dev = ep->driver_data;

	switch (req->status) {
	default:
		VDBG(dev, "tx err %d\n", req->status);
		fallthrough;
	case -ECONNRESET:		/* unlink */
	case -ESHUTDOWN:		/* disconnect etc */
		break;
	case 0:
		break;
	}

	spin_lock(&dev->lock);
	/* Take the request struct off the active list and put it on the
	 * free list.
	 */
	list_del_init(&req->list);
	list_add(&req->list, &dev->tx_reqs);
	wake_up_interruptible(&dev->tx_wait);
	if (likely(list_empty(&dev->tx_reqs_active)))
		wake_up_interruptible(&dev->tx_flush_wait);

	spin_unlock(&dev->lock);
}

/*-------------------------------------------------------------------------*/

static int
nero_open(struct inode *inode, struct file *fd)
{
	struct nero_dev	*dev;
	struct nero_file *file;
	unsigned long		flags;
	int			ret = -EBUSY;

	dev = container_of(inode->i_cdev, struct nero_cdev, cdev)->nero;
	file = kzalloc(sizeof(*file), GFP_KERNEL);
	if (!file)
		return -ENOMEM;

	spin_lock_irqsave(&dev->lock, flags);

	if (nero_unavailable(dev) || !dev->cdev_added) {
		spin_unlock_irqrestore(&dev->lock, flags);
		kfree(file);
		return -ENODEV;
	}

	if (!dev->nero_cdev_open) {
		dev->nero_cdev_open = 1;
		file->dev = dev;
		file->lifecycle_seq = dev->lifecycle_seq;
		fd->private_data = file;
		dev->open_file = file;
		kref_get(&dev->kref);
		ret = 0;
	}

	spin_unlock_irqrestore(&dev->lock, flags);
	if (ret)
		kfree(file);
	DBG(dev, "nero_open returned %x\n", ret);
	return ret;
}

static int
nero_close(struct inode *inode, struct file *fd)
{
	struct nero_file *file = fd->private_data;
	struct nero_dev	*dev = file->dev;
	unsigned long		flags;

	spin_lock_irqsave(&dev->lock, flags);
	if (dev->open_file != file)
		WARN_ON_ONCE(1);
	else
		dev->open_file = NULL;
	dev->nero_cdev_open = 0;
	fd->private_data = NULL;
	spin_unlock_irqrestore(&dev->lock, flags);

	DBG(dev, "nero_close\n");
	kfree(file);
	kref_put(&dev->kref, nero_dev_free);

	return 0;
}

/* This function must be called with interrupts turned off. */
static void
setup_rx_reqs(struct nero_dev *dev)
{
	struct usb_request              *req;

	while (likely(!list_empty(&dev->rx_reqs)) && !nero_unavailable(dev)) {
		int error;
		unsigned int reset_seq = dev->reset_seq;

		req = container_of(dev->rx_reqs.next,
				struct usb_request, list);
		list_del_init(&req->list);

		/* The USB Host sends us whatever amount of data it wants to
		 * so we always set the length field to the full RX buffer size.
		 * If the amount of data is more than the read() caller asked
		 * for it will be stored in the request buffer until it is
		 * asked for by read().
		 */
		req->length = dev->rx_req_buf_size;
		req->complete = rx_complete;

		/* here, we unlock, and only unlock, to avoid deadlock. */
		spin_unlock(&dev->lock);
		error = usb_ep_queue(dev->out_ep, req, GFP_ATOMIC);
		spin_lock(&dev->lock);
		if (error) {
			DBG(dev, "rx submit --> %d\n", error);
			list_add(&req->list, &dev->rx_reqs);
			break;
		}
		/*
		 * If reset/unbind raced with queueing, endpoint disable owns
		 * completion/recycling.  Otherwise track the queued request.
		 */
		if (nero_unavailable(dev) || dev->reset_seq != reset_seq)
			break;
		if (list_empty(&req->list))
			list_add(&req->list, &dev->rx_reqs_active);
	}
}

static bool nero_rx_ready(struct nero_dev *dev, unsigned int reset_seq,
		unsigned int lifecycle_seq)
{
	return nero_file_unavailable(dev, lifecycle_seq) ||
	       READ_ONCE(dev->reset_seq) != reset_seq ||
	       READ_ONCE(dev->current_rx_bytes) ||
	       likely(!list_empty(&dev->rx_buffers));
}

static bool nero_tx_ready(struct nero_dev *dev, unsigned int reset_seq,
		unsigned int lifecycle_seq)
{
	return nero_file_unavailable(dev, lifecycle_seq) ||
	       READ_ONCE(dev->reset_seq) != reset_seq ||
	       likely(!list_empty(&dev->tx_reqs));
}

static bool nero_tx_flushed(struct nero_dev *dev, unsigned int reset_seq,
		unsigned int lifecycle_seq)
{
	unsigned long flags;
	bool flushed;

	spin_lock_irqsave(&dev->lock, flags);
	flushed = dev->lifecycle_seq != lifecycle_seq ||
		  dev->unbound || dev->interface < 0 ||
		  dev->reset_seq != reset_seq ||
		  likely(list_empty(&dev->tx_reqs_active));
	spin_unlock_irqrestore(&dev->lock, flags);

	return flushed;
}

static ssize_t
nero_read(struct file *fd, char __user *buf, size_t len, loff_t *ptr)
{
	struct nero_file	*file = fd->private_data;
	struct nero_dev		*dev = file->dev;
	unsigned long			flags;
	unsigned int			reset_seq;
	unsigned int			lifecycle_seq = file->lifecycle_seq;
	size_t				size;
	size_t				not_copied;
	size_t				bytes_copied;
	struct usb_request		*req;
	/* This is a pointer to the current USB rx request. */
	struct usb_request		*current_rx_req;
	/* This is the number of bytes in the current rx buffer. */
	size_t				current_rx_bytes;
	/* This is a pointer to the current rx buffer. */
	u8				*current_rx_buf;
	int				value;
	ssize_t				ret;

	if (len == 0)
		return -EINVAL;

	DBG(dev, "nero_read trying to read %d bytes\n", (int)len);

	mutex_lock(&dev->lock_nero_rx);
	spin_lock_irqsave(&dev->lock, flags);

	if (nero_file_unavailable(dev, lifecycle_seq)) {
		spin_unlock_irqrestore(&dev->lock, flags);
		mutex_unlock(&dev->lock_nero_rx);
		return -ENODEV;
	}

	reset_seq = dev->reset_seq;
	setup_rx_reqs(dev);

	bytes_copied = 0;
	current_rx_req = dev->current_rx_req;
	current_rx_bytes = dev->current_rx_bytes;
	current_rx_buf = dev->current_rx_buf;
	dev->current_rx_req = NULL;
	dev->current_rx_bytes = 0;
	dev->current_rx_buf = NULL;

	/* Check if there is any data in the read buffers. Please note that
	 * current_rx_bytes is the number of bytes in the current rx buffer.
	 * If it is zero then check if there are any other rx_buffers that
	 * are on the completed list. We are only out of data if all rx
	 * buffers are empty.
	 */
	if ((current_rx_bytes == 0) &&
			(likely(list_empty(&dev->rx_buffers)))) {
		/* Turn interrupts back on before sleeping. */
		spin_unlock_irqrestore(&dev->lock, flags);

		/*
		 * If no data is available check if this is a NON-Blocking
		 * call or not.
		 */
		if (fd->f_flags & (O_NONBLOCK|O_NDELAY)) {
			ret = -EAGAIN;
			spin_lock_irqsave(&dev->lock, flags);
			if (nero_file_unavailable(dev, lifecycle_seq))
				goto out_rx_unavailable;
			if (dev->reset_seq != reset_seq)
				goto out_rx_reset;
			goto out_save_current;
		}

		/* Sleep until data is available */
		value = wait_event_interruptible(dev->rx_wait,
				nero_rx_ready(dev, reset_seq, lifecycle_seq));
		if (value) {
			ret = value;
			spin_lock_irqsave(&dev->lock, flags);
			if (nero_file_unavailable(dev, lifecycle_seq))
				goto out_rx_unavailable;
			if (dev->reset_seq != reset_seq)
				goto out_rx_reset;
			goto out_save_current;
		}
		spin_lock_irqsave(&dev->lock, flags);

		if (nero_file_unavailable(dev, lifecycle_seq))
			goto out_rx_unavailable;

		if (dev->reset_seq != reset_seq)
			goto out_rx_reset;
	}

	/* We have data to return then copy it to the caller's buffer.*/
	while ((current_rx_bytes || likely(!list_empty(&dev->rx_buffers)))
			&& len) {
		if (current_rx_bytes == 0) {
			req = container_of(dev->rx_buffers.next,
					struct usb_request, list);
			list_del_init(&req->list);

			if (req->actual && req->buf) {
				current_rx_req = req;
				current_rx_bytes = req->actual;
				current_rx_buf = req->buf;
			} else {
				list_add(&req->list, &dev->rx_reqs);
				continue;
			}
		}

		/* Don't leave irqs off while doing memory copies */
		spin_unlock_irqrestore(&dev->lock, flags);

		if (len > current_rx_bytes)
			size = current_rx_bytes;
		else
			size = len;

		not_copied = copy_to_user(buf, current_rx_buf, size);
		size -= not_copied;

		spin_lock_irqsave(&dev->lock, flags);

		if (!size) {
			if (nero_file_unavailable(dev, lifecycle_seq))
				goto out_rx_unavailable;
			if (dev->reset_seq != reset_seq)
				goto out_rx_reset;
			ret = bytes_copied ? bytes_copied : -EFAULT;
			goto out_save_current;
		}

		bytes_copied += size;
		len -= size;
		buf += size;

		/* We've disconnected or reset so return. */
		if (nero_file_unavailable(dev, lifecycle_seq))
			goto out_rx_unavailable;

		if (dev->reset_seq != reset_seq)
			goto out_rx_reset;

		/* If we not returning all the data left in this RX request
		 * buffer then adjust the amount of data left in the buffer.
		 * Othewise if we are done with this RX request buffer then
		 * requeue it to get any incoming data from the USB host.
		 */
		if (size < current_rx_bytes) {
			current_rx_bytes -= size;
			current_rx_buf += size;
		} else {
			list_add(&current_rx_req->list, &dev->rx_reqs);
			current_rx_bytes = 0;
			current_rx_buf = NULL;
			current_rx_req = NULL;
		}

		if (not_copied) {
			ret = bytes_copied;
			goto out_save_current;
		}
	}

	ret = bytes_copied ? bytes_copied : -EAGAIN;

out_save_current:
	dev->current_rx_req = current_rx_req;
	dev->current_rx_bytes = current_rx_bytes;
	dev->current_rx_buf = current_rx_buf;

	spin_unlock_irqrestore(&dev->lock, flags);
	mutex_unlock(&dev->lock_nero_rx);

	DBG(dev, "nero_read returned %d bytes\n", (int)bytes_copied);
	return ret;

out_rx_unavailable:
	if (current_rx_req) {
		list_add(&current_rx_req->list, &dev->rx_reqs);
		current_rx_req = NULL;
	}
	dev->current_rx_req = NULL;
	dev->current_rx_bytes = 0;
	dev->current_rx_buf = NULL;
	ret = bytes_copied ? bytes_copied : -ENODEV;
	spin_unlock_irqrestore(&dev->lock, flags);
	mutex_unlock(&dev->lock_nero_rx);
	return ret;

out_rx_reset:
	if (current_rx_req) {
		list_add(&current_rx_req->list, &dev->rx_reqs);
		current_rx_req = NULL;
	}
	dev->current_rx_req = NULL;
	dev->current_rx_bytes = 0;
	dev->current_rx_buf = NULL;
	ret = bytes_copied ? bytes_copied : -EAGAIN;
	spin_unlock_irqrestore(&dev->lock, flags);
	mutex_unlock(&dev->lock_nero_rx);
	return ret;
}

static ssize_t
nero_write(struct file *fd, const char __user *buf, size_t len, loff_t *ptr)
{
	struct nero_file *file = fd->private_data;
	struct nero_dev	*dev = file->dev;
	unsigned long		flags;
	unsigned int		reset_seq;
	unsigned int		lifecycle_seq = file->lifecycle_seq;
	size_t			size;	/* Amount of data in a TX request. */
	size_t			bytes_copied = 0;
	struct usb_request	*req;
	int			value;
	ssize_t			ret;

	DBG(dev, "nero_write trying to send %d bytes\n", (int)len);

	if (len == 0)
		return -EINVAL;

	mutex_lock(&dev->lock_nero_tx);
	spin_lock_irqsave(&dev->lock, flags);

	if (nero_file_unavailable(dev, lifecycle_seq)) {
		spin_unlock_irqrestore(&dev->lock, flags);
		mutex_unlock(&dev->lock_nero_tx);
		return -ENODEV;
	}

	reset_seq = dev->reset_seq;

	/* Check if there is any available write buffers */
	if (likely(list_empty(&dev->tx_reqs))) {
		/* Turn interrupts back on before sleeping. */
		spin_unlock_irqrestore(&dev->lock, flags);

		/*
		 * If write buffers are available check if this is
		 * a NON-Blocking call or not.
		 */
		if (fd->f_flags & (O_NONBLOCK|O_NDELAY)) {
			mutex_unlock(&dev->lock_nero_tx);
			return -EAGAIN;
		}

		/* Sleep until a write buffer is available */
		value = wait_event_interruptible(dev->tx_wait,
				nero_tx_ready(dev, reset_seq, lifecycle_seq));
		if (value) {
			mutex_unlock(&dev->lock_nero_tx);
			return value;
		}
		spin_lock_irqsave(&dev->lock, flags);

		if (nero_file_unavailable(dev, lifecycle_seq)) {
			spin_unlock_irqrestore(&dev->lock, flags);
			mutex_unlock(&dev->lock_nero_tx);
			return -ENODEV;
		}

		if (dev->reset_seq != reset_seq) {
			spin_unlock_irqrestore(&dev->lock, flags);
			mutex_unlock(&dev->lock_nero_tx);
			return -EAGAIN;
		}
	}

	while (likely(!list_empty(&dev->tx_reqs)) && len) {

		if (len > dev->tx_req_buf_size)
			size = dev->tx_req_buf_size;
		else
			size = len;

		req = container_of(dev->tx_reqs.next, struct usb_request,
				list);
		list_del_init(&req->list);

		req->complete = tx_complete;
		req->length = size;

		/* Check if we need to send a zero length packet. */
		if (len > size)
			/* They will be more TX requests so no yet. */
			req->zero = 0;
		else
			/* If the data amount is not a multiple of the
			 * maxpacket size then send a zero length packet.
			 */
			req->zero = ((len % dev->in_ep->maxpacket) == 0);

		/* Don't leave irqs off while doing memory copies */
		spin_unlock_irqrestore(&dev->lock, flags);

		if (copy_from_user(req->buf, buf, size)) {
			spin_lock_irqsave(&dev->lock, flags);
			list_add(&req->list, &dev->tx_reqs);
			spin_unlock_irqrestore(&dev->lock, flags);
			mutex_unlock(&dev->lock_nero_tx);
			return bytes_copied ? bytes_copied : -EFAULT;
		}

		spin_lock_irqsave(&dev->lock, flags);

		/* We've disconnected or reset so free the req and buffer */
		if (nero_file_unavailable(dev, lifecycle_seq)) {
			list_add(&req->list, &dev->tx_reqs);
			spin_unlock_irqrestore(&dev->lock, flags);
			mutex_unlock(&dev->lock_nero_tx);
			return bytes_copied ? bytes_copied : -ENODEV;
		}

		if (dev->reset_seq != reset_seq) {
			list_add(&req->list, &dev->tx_reqs);
			spin_unlock_irqrestore(&dev->lock, flags);
			mutex_unlock(&dev->lock_nero_tx);
			return bytes_copied ? bytes_copied : -EAGAIN;
		}

		list_add(&req->list, &dev->tx_reqs_active);

		/* here, we unlock, and only unlock, to avoid deadlock. */
		spin_unlock(&dev->lock);
		value = usb_ep_queue(dev->in_ep, req, GFP_ATOMIC);
		spin_lock(&dev->lock);
		if (value) {
			ret = bytes_copied ? bytes_copied : value;
			list_move(&req->list, &dev->tx_reqs);
			spin_unlock_irqrestore(&dev->lock, flags);
			mutex_unlock(&dev->lock_nero_tx);
			return ret;
		}

		bytes_copied += size;
		len -= size;
		buf += size;
	}

	spin_unlock_irqrestore(&dev->lock, flags);
	mutex_unlock(&dev->lock_nero_tx);

	DBG(dev, "nero_write sent %d bytes\n", (int)bytes_copied);

	if (bytes_copied)
		return bytes_copied;
	else
		return -EAGAIN;
}

static int
nero_fsync(struct file *fd, loff_t start, loff_t end, int datasync)
{
	struct nero_file *file = fd->private_data;
	struct nero_dev	*dev = file->dev;
	struct inode *inode = file_inode(fd);
	unsigned long		flags;
	unsigned int		reset_seq;
	unsigned int		lifecycle_seq = file->lifecycle_seq;
	int			ret;

	inode_lock(inode);
	spin_lock_irqsave(&dev->lock, flags);

	if (nero_file_unavailable(dev, lifecycle_seq)) {
		spin_unlock_irqrestore(&dev->lock, flags);
		inode_unlock(inode);
		return -ENODEV;
	}

	reset_seq = dev->reset_seq;
	spin_unlock_irqrestore(&dev->lock, flags);

	/* Sleep until all queued writes have completed or the session changes. */
	ret = wait_event_interruptible(dev->tx_flush_wait,
			nero_tx_flushed(dev, reset_seq, lifecycle_seq));
	inode_unlock(inode);

	if (ret)
		return ret;
	if (nero_file_unavailable(dev, lifecycle_seq))
		return -ENODEV;
	if (READ_ONCE(dev->reset_seq) != reset_seq)
		return -EAGAIN;

	return 0;
}

static __poll_t
nero_poll(struct file *fd, poll_table *wait)
{
	struct nero_file *file = fd->private_data;
	struct nero_dev	*dev = file->dev;
	unsigned long		flags;
	unsigned int		lifecycle_seq = file->lifecycle_seq;
	__poll_t		status = 0;

	mutex_lock(&dev->lock_nero_rx);
	spin_lock_irqsave(&dev->lock, flags);

	if (nero_file_unavailable(dev, lifecycle_seq)) {
		spin_unlock_irqrestore(&dev->lock, flags);
		mutex_unlock(&dev->lock_nero_rx);
		return EPOLLERR | EPOLLHUP;
	}

	setup_rx_reqs(dev);
	spin_unlock_irqrestore(&dev->lock, flags);

	poll_wait(fd, &dev->rx_wait, wait);
	poll_wait(fd, &dev->tx_wait, wait);

	spin_lock_irqsave(&dev->lock, flags);
	if (nero_file_unavailable(dev, lifecycle_seq))
		status |= EPOLLERR | EPOLLHUP;

	if (!nero_file_unavailable(dev, lifecycle_seq) &&
			likely(!list_empty(&dev->tx_reqs)))
		status |= EPOLLOUT | EPOLLWRNORM;

	if (likely(dev->current_rx_bytes) ||
			likely(!list_empty(&dev->rx_buffers)))
		status |= EPOLLIN | EPOLLRDNORM;

	spin_unlock_irqrestore(&dev->lock, flags);
	mutex_unlock(&dev->lock_nero_rx);

	return status;
}

static long
nero_ioctl(struct file *fd, unsigned int code, unsigned long arg)
{
	struct nero_file *file = fd->private_data;
	struct nero_dev	*dev = file->dev;
	unsigned long		flags;
	unsigned int		lifecycle_seq = file->lifecycle_seq;
	int			status = 0;

	DBG(dev, "nero_ioctl: cmd=0x%4.4x, arg=%lu\n", code, arg);

	/* handle ioctls */

	spin_lock_irqsave(&dev->lock, flags);

	if (nero_file_unavailable(dev, lifecycle_seq)) {
		spin_unlock_irqrestore(&dev->lock, flags);
		return -ENODEV;
	}

	switch (code) {
	default:
		/* could not handle ioctl */
		DBG(dev, "nero_ioctl: ERROR cmd=0x%4.4xis not supported\n",
				code);
		status = -ENOTTY;
	}

	spin_unlock_irqrestore(&dev->lock, flags);

	return status;
}

/* used after endpoint configuration */
static const struct file_operations nero_io_operations = {
	.owner =	THIS_MODULE,
	.open =		nero_open,
	.read =		nero_read,
	.write =	nero_write,
	.fsync =	nero_fsync,
	.poll =		nero_poll,
	.unlocked_ioctl = nero_ioctl,
	.release =	nero_close,
	.llseek =	noop_llseek,
};

/*-------------------------------------------------------------------------*/

static int
set_nero_interface(struct nero_dev *dev)
{
	int			result = 0;

	dev->in_ep->desc = ep_desc(dev->gadget, &fs_ep_in_desc, &hs_ep_in_desc,
				&ss_ep_in_desc);
	dev->in_ep->driver_data = dev;

	dev->out_ep->desc = ep_desc(dev->gadget, &fs_ep_out_desc,
				    &hs_ep_out_desc, &ss_ep_out_desc);
	dev->out_ep->driver_data = dev;

	result = usb_ep_enable(dev->in_ep);
	if (result != 0) {
		DBG(dev, "enable %s --> %d\n", dev->in_ep->name, result);
		goto done;
	}

	result = usb_ep_enable(dev->out_ep);
	if (result != 0) {
		DBG(dev, "enable %s --> %d\n", dev->out_ep->name, result);
		goto done;
	}

done:
	/* on error, disable any endpoints  */
	if (result != 0) {
		(void) usb_ep_disable(dev->in_ep);
		(void) usb_ep_disable(dev->out_ep);
		dev->in_ep->desc = NULL;
		dev->out_ep->desc = NULL;
	}

	/* caller is responsible for cleanup on error */
	return result;
}

static void nero_reset_interface(struct nero_dev *dev)
{
	struct usb_request *req;
	unsigned long	flags;
	bool disable_in;
	bool disable_out;

	DBG(dev, "%s\n", __func__);

	spin_lock_irqsave(&dev->lock, flags);
	dev->interface = -1;
	dev->reset_seq++;
	disable_in = dev->in_ep && dev->in_ep->desc;
	disable_out = dev->out_ep && dev->out_ep->desc;
	spin_unlock_irqrestore(&dev->lock, flags);

	if (disable_in)
		usb_ep_disable(dev->in_ep);

	if (disable_out)
		usb_ep_disable(dev->out_ep);

	spin_lock_irqsave(&dev->lock, flags);
	if (disable_in)
		dev->in_ep->desc = NULL;
	if (disable_out)
		dev->out_ep->desc = NULL;

	if (dev->current_rx_req) {
		req = dev->current_rx_req;
		dev->current_rx_req = NULL;
		dev->current_rx_bytes = 0;
		dev->current_rx_buf = NULL;
		list_add(&req->list, &dev->rx_reqs);
	}

	nero_move_reqs(&dev->rx_buffers, &dev->rx_reqs);
	nero_move_reqs(&dev->rx_reqs_active, &dev->rx_reqs);
	nero_move_reqs(&dev->tx_reqs_active, &dev->tx_reqs);
	spin_unlock_irqrestore(&dev->lock, flags);

	wake_up_interruptible(&dev->rx_wait);
	wake_up_interruptible(&dev->tx_wait);
	wake_up_interruptible(&dev->tx_flush_wait);
}

/* Change our operational Interface. */
static int set_interface(struct nero_dev *dev, unsigned number)
{
	int			result = 0;
	unsigned long		flags;

	result = nero_enter_set_alt(dev);
	if (result)
		return result;

	/* Free the current interface */
	nero_reset_interface(dev);

	if (READ_ONCE(dev->unbound)) {
		result = -ENODEV;
		goto out;
	}

	result = set_nero_interface(dev);
	if (result) {
		nero_reset_interface(dev);
		goto out;
	}

	spin_lock_irqsave(&dev->lock, flags);
	if (dev->unbound) {
		spin_unlock_irqrestore(&dev->lock, flags);
		nero_reset_interface(dev);
		result = -ENODEV;
		goto out;
	}
	dev->interface = number;
	dev->reset_seq++;
	spin_unlock_irqrestore(&dev->lock, flags);
	wake_up_interruptible(&dev->rx_wait);
	wake_up_interruptible(&dev->tx_wait);

	if (!result)
		INFO(dev, "Using interface %x\n", number);

out:
	nero_leave_set_alt(dev);
	return result;
}

static int nero_func_bind(struct usb_configuration *c,
		struct usb_function *f)
{
	struct usb_gadget *gadget = c->cdev->gadget;
	struct nero_dev *dev = func_to_nero(f);
	struct nero_cdev *nero_cdev;
	struct usb_composite_dev *cdev = c->cdev;
	struct usb_ep *in_ep;
	struct usb_ep *out_ep = NULL;
	struct usb_request *req;
	unsigned long flags;
	int id;
	int ret;
	u32 i;

	id = usb_interface_id(c, f);
	if (id < 0)
		return id;
	iad_desc.bFirstInterface = id;
	intf_desc.bInterfaceNumber = id;

	/* finish hookup to lower layer ... */
	dev->gadget = gadget;

	ret = nero_reject_stale_cdev_rebind(dev);
	if (ret)
		return ret;

	ret = usb_string_ids_tab(cdev, nero_string_defs);
	if (ret < 0)
		return ret;
	iad_desc.iFunction = nero_string_defs[NERO_INTERFACE_STRING].id;
	intf_desc.iInterface = nero_string_defs[NERO_INTERFACE_STRING].id;

	/* all we really need is bulk IN/OUT */
	in_ep = usb_ep_autoconfig(cdev->gadget, &fs_ep_in_desc);
	if (!in_ep) {
autoconf_fail:
		dev_err(&cdev->gadget->dev, "can't autoconfigure on %s\n",
			cdev->gadget->name);
		return -ENODEV;
	}

	out_ep = usb_ep_autoconfig(cdev->gadget, &fs_ep_out_desc);
	if (!out_ep)
		goto autoconf_fail;

	/* assumes that all endpoints are dual-speed */
	hs_ep_in_desc.bEndpointAddress = fs_ep_in_desc.bEndpointAddress;
	hs_ep_out_desc.bEndpointAddress = fs_ep_out_desc.bEndpointAddress;
	ss_ep_in_desc.bEndpointAddress = fs_ep_in_desc.bEndpointAddress;
	ss_ep_out_desc.bEndpointAddress = fs_ep_out_desc.bEndpointAddress;

	ret = usb_assign_descriptors(f, fs_nero_function,
			hs_nero_function, ss_nero_function,
			ss_nero_function);
	if (ret)
		return ret;

	dev->in_ep = in_ep;
	dev->out_ep = out_ep;

	ret = -ENOMEM;
	for (i = 0; i < dev->tx_q_len; i++) {
		req = nero_req_alloc(dev->in_ep, dev->tx_req_buf_size,
				     GFP_KERNEL);
		if (!req)
			goto fail_tx_reqs;
		list_add(&req->list, &dev->tx_reqs);
	}

	for (i = 0; i < dev->rx_q_len; i++) {
		req = nero_req_alloc(dev->out_ep, dev->rx_req_buf_size,
				     GFP_KERNEL);
		if (!req)
			goto fail_rx_reqs;
		list_add(&req->list, &dev->rx_reqs);
	}

	/*
	 * Register a character device and matching device node as an interface
	 * to the user mode program that handles the nero functionality.
	 */
	nero_cdev = nero_cdev_alloc(dev);
	if (IS_ERR(nero_cdev)) {
		ret = PTR_ERR(nero_cdev);
		goto fail_rx_reqs;
	}

	ret = cdev_device_add(&nero_cdev->cdev, &nero_cdev->dev);
	if (ret) {
		ERROR(dev, "Failed to open char device\n");
		goto fail_cdev_add;
	}

	spin_lock_irqsave(&dev->lock, flags);
	dev->nero_cdev = nero_cdev;
	dev->cdev_added = true;
	spin_unlock_irqrestore(&dev->lock, flags);

	nero_mark_bound(dev);

	return 0;

fail_cdev_add:
	put_device(&nero_cdev->dev);

fail_rx_reqs:
	while (!list_empty(&dev->rx_reqs)) {
		req = container_of(dev->rx_reqs.next, struct usb_request, list);
		list_del(&req->list);
		nero_req_free(dev->out_ep, req);
	}

fail_tx_reqs:
	while (!list_empty(&dev->tx_reqs)) {
		req = container_of(dev->tx_reqs.next, struct usb_request, list);
		list_del(&req->list);
		nero_req_free(dev->in_ep, req);
	}

	nero_mark_unbound(dev);
	usb_free_all_descriptors(f);
	return ret;

}

static int nero_func_set_alt(struct usb_function *f,
		unsigned intf, unsigned alt)
{
	struct nero_dev *dev = func_to_nero(f);
	int ret = -ENOTSUPP;

	if (!alt)
		ret = set_interface(dev, intf);

	return ret;
}

static void nero_func_disable(struct usb_function *f)
{
	struct nero_dev *dev = func_to_nero(f);

	DBG(dev, "%s\n", __func__);

	nero_reset_interface(dev);
}

static inline struct f_nero_opts
*to_f_nero_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_nero_opts,
			    func_inst.group);
}

static void nero_attr_release(struct config_item *item)
{
	struct f_nero_opts *opts = to_f_nero_opts(item);

	usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations nero_item_ops = {
	.release	= nero_attr_release,
};

static ssize_t f_nero_opts_tx_q_len_show(struct config_item *item,
					    char *page)
{
	struct f_nero_opts *opts = to_f_nero_opts(item);
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%u\n", opts->tx_q_len);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_nero_opts_tx_q_len_store(struct config_item *item,
					     const char *page, size_t len)
{
	struct f_nero_opts *opts = to_f_nero_opts(item);
	int ret;
	u16 num;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = kstrtou16(page, 0, &num);
	if (ret)
		goto end;

	if (!num)
		pr_err("nero: tx_q_len is 0, no TX request buffers will be allocated\n");

	opts->tx_q_len = (unsigned int)num;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	return ret;
}

CONFIGFS_ATTR(f_nero_opts_, tx_q_len);

static ssize_t f_nero_opts_rx_q_len_show(struct config_item *item,
					    char *page)
{
	struct f_nero_opts *opts = to_f_nero_opts(item);
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%u\n", opts->rx_q_len);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_nero_opts_rx_q_len_store(struct config_item *item,
					     const char *page, size_t len)
{
	struct f_nero_opts *opts = to_f_nero_opts(item);
	int ret;
	u16 num;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = kstrtou16(page, 0, &num);
	if (ret)
		goto end;

	if (!num)
		pr_err("nero: rx_q_len is 0, no RX request buffers will be allocated\n");

	opts->rx_q_len = (unsigned int)num;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	return ret;
}

CONFIGFS_ATTR(f_nero_opts_, rx_q_len);

static ssize_t f_nero_opts_tx_req_buf_size_show(struct config_item *item,
						   char *page)
{
	struct f_nero_opts *opts = to_f_nero_opts(item);
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%u\n", opts->tx_req_buf_size);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_nero_opts_tx_req_buf_size_store(struct config_item *item,
						    const char *page,
						    size_t len)
{
	struct f_nero_opts *opts = to_f_nero_opts(item);
	int ret;
	u32 num;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = kstrtou32(page, 0, &num);
	if (ret)
		goto end;

	opts->tx_req_buf_size = (unsigned int)num;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	return ret;
}

CONFIGFS_ATTR(f_nero_opts_, tx_req_buf_size);

static ssize_t f_nero_opts_rx_req_buf_size_show(struct config_item *item,
						   char *page)
{
	struct f_nero_opts *opts = to_f_nero_opts(item);
	int result;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%u\n", opts->rx_req_buf_size);
	mutex_unlock(&opts->lock);

	return result;
}

static ssize_t f_nero_opts_rx_req_buf_size_store(struct config_item *item,
						    const char *page,
						    size_t len)
{
	struct f_nero_opts *opts = to_f_nero_opts(item);
	int ret;
	u32 num;

	mutex_lock(&opts->lock);
	if (opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = kstrtou32(page, 0, &num);
	if (ret)
		goto end;

	opts->rx_req_buf_size = (unsigned int)num;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	return ret;
}

CONFIGFS_ATTR(f_nero_opts_, rx_req_buf_size);

static struct configfs_attribute *nero_attrs[] = {
	&f_nero_opts_attr_tx_q_len,
	&f_nero_opts_attr_rx_q_len,
	&f_nero_opts_attr_tx_req_buf_size,
	&f_nero_opts_attr_rx_req_buf_size,
	NULL,
};

static const struct config_item_type nero_func_type = {
	.ct_item_ops	= &nero_item_ops,
	.ct_attrs	= nero_attrs,
	.ct_owner	= THIS_MODULE,
};

static inline int gnero_get_minor(void)
{
	int ret;

	ret = ida_simple_get(&nero_ida, 0, 0, GFP_KERNEL);
	if (ret >= PRINTER_MINORS) {
		ida_simple_remove(&nero_ida, ret);
		ret = -ENODEV;
	}

	return ret;
}

static inline void gnero_put_minor(int minor)
{
	ida_simple_remove(&nero_ida, minor);
}

static int gnero_setup(int);
static void gnero_cleanup(void);

static void gnero_free_inst(struct usb_function_instance *f)
{
	struct f_nero_opts *opts;

	opts = container_of(f, struct f_nero_opts, func_inst);

	mutex_lock(&nero_ida_lock);

	gnero_put_minor(opts->minor);
	if (ida_is_empty(&nero_ida))
		gnero_cleanup();

	mutex_unlock(&nero_ida_lock);

	kfree(opts);
}

static struct usb_function_instance *gnero_alloc_inst(void)
{
	struct f_nero_opts *opts;
	struct usb_function_instance *ret;
	int status = 0;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);

	mutex_init(&opts->lock);
	opts->func_inst.free_func_inst = gnero_free_inst;
	opts->tx_q_len = NERO_TX_Q_LEN;
	opts->rx_q_len = NERO_RX_Q_LEN;
	opts->tx_req_buf_size = NERO_REQ_BUF_SIZE;
	opts->rx_req_buf_size = NERO_REQ_BUF_SIZE;
	ret = &opts->func_inst;

	mutex_lock(&nero_ida_lock);

	if (ida_is_empty(&nero_ida)) {
		status = gnero_setup(PRINTER_MINORS);
		if (status) {
			ret = ERR_PTR(status);
			kfree(opts);
			goto unlock;
		}
	}

	opts->minor = gnero_get_minor();
	if (opts->minor < 0) {
		ret = ERR_PTR(opts->minor);
		kfree(opts);
		if (ida_is_empty(&nero_ida))
			gnero_cleanup();
		goto unlock;
	}
	config_group_init_type_name(&opts->func_inst.group, "",
				    &nero_func_type);

unlock:
	mutex_unlock(&nero_ida_lock);
	return ret;
}

static void gnero_free(struct usb_function *f)
{
	struct nero_dev *dev = func_to_nero(f);
	struct f_nero_opts *opts;

	opts = container_of(f->fi, struct f_nero_opts, func_inst);

	kref_put(&dev->kref, nero_dev_free);
	mutex_lock(&opts->lock);
	--opts->refcnt;
	mutex_unlock(&opts->lock);
}

static void nero_func_unbind(struct usb_configuration *c,
		struct usb_function *f)
{
	struct nero_dev	*dev;
	bool cdev_was_added;
	bool cdev_busy;
	struct nero_cdev *nero_cdev;

	dev = func_to_nero(f);

	nero_begin_unbind(dev);

	nero_reset_interface(dev);

	/* Remove Character Device */
	nero_cdev = nero_mark_cdev_deleted(dev, &cdev_was_added);
	cdev_busy = nero_block_cdev_reuse_if_busy(dev, cdev_was_added,
			nero_cdev);
	if (cdev_was_added && nero_cdev)
		cdev_device_del(&nero_cdev->cdev, &nero_cdev->dev);
	else
		nero_warn_cdev_boundary(dev, "unbind",
				"cdev was not marked added before unbind");

	if (cdev_busy)
		nero_warn_cdev_boundary(dev, "unbind",
				"cdev still busy; expected userspace to close fd before unbind");

	if (nero_cdev)
		put_device(&nero_cdev->dev);

	/*
	 * Wait for in-flight file operations to drop local request pointers
	 * before freeing the request pools.
	 */
	mutex_lock(&dev->lock_nero_rx);
	mutex_lock(&dev->lock_nero_tx);

	/* we must already have been disconnected ... no i/o may be active */
	WARN_ON(!list_empty(&dev->tx_reqs_active));
	WARN_ON(!list_empty(&dev->rx_reqs_active));

	/* Free all memory for this driver. */
	nero_free_reqs(dev->in_ep, &dev->tx_reqs);
	nero_free_reqs(dev->out_ep, &dev->rx_reqs);
	nero_free_reqs(dev->out_ep, &dev->rx_buffers);

	if (dev->in_ep)
		dev->in_ep->driver_data = NULL;
	if (dev->out_ep)
		dev->out_ep->driver_data = NULL;

	mutex_unlock(&dev->lock_nero_tx);
	mutex_unlock(&dev->lock_nero_rx);

	usb_free_all_descriptors(f);
}

static struct usb_function *gnero_alloc(struct usb_function_instance *fi)
{
	struct nero_dev	*dev;
	struct f_nero_opts	*opts;

	opts = container_of(fi, struct f_nero_opts, func_inst);

	mutex_lock(&opts->lock);
	if (opts->minor >= minors) {
		mutex_unlock(&opts->lock);
		return ERR_PTR(-ENOENT);
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		mutex_unlock(&opts->lock);
		return ERR_PTR(-ENOMEM);
	}

	kref_init(&dev->kref);
	++opts->refcnt;
	dev->minor = opts->minor;
	dev->tx_q_len = opts->tx_q_len;
	dev->rx_q_len = opts->rx_q_len;
	dev->tx_req_buf_size = opts->tx_req_buf_size;
	dev->rx_req_buf_size = opts->rx_req_buf_size;
	mutex_unlock(&opts->lock);

	dev->function.name = "nero";
	dev->function.bind = nero_func_bind;
	dev->function.unbind = nero_func_unbind;
	dev->function.set_alt = nero_func_set_alt;
	dev->function.disable = nero_func_disable;
	dev->function.free_func = gnero_free;
	dev->function.strings = nero_strings;

	INIT_LIST_HEAD(&dev->tx_reqs);
	INIT_LIST_HEAD(&dev->rx_reqs);
	INIT_LIST_HEAD(&dev->rx_buffers);
	INIT_LIST_HEAD(&dev->tx_reqs_active);
	INIT_LIST_HEAD(&dev->rx_reqs_active);

	spin_lock_init(&dev->lock);
	mutex_init(&dev->lock_nero_rx);
	mutex_init(&dev->lock_nero_tx);
	init_waitqueue_head(&dev->rx_wait);
	init_waitqueue_head(&dev->tx_wait);
	init_waitqueue_head(&dev->tx_flush_wait);

	dev->interface = -1;
	dev->cdev_added = false;
	dev->cdev_reuse_blocked = false;
	dev->nero_cdev = NULL;
	dev->nero_cdev_open = 0;
	dev->open_file = NULL;
	dev->current_rx_req = NULL;
	dev->current_rx_bytes = 0;
	dev->current_rx_buf = NULL;
	dev->reset_seq = 0;
	dev->lifecycle_seq = 0;
	dev->set_alt_inflight = 0;
	init_completion(&dev->set_alt_idle);
	dev->unbound = true;

	return &dev->function;
}

DECLARE_USB_FUNCTION_INIT(nero, gnero_alloc_inst, gnero_alloc);
MODULE_LICENSE("GPL");

static int gnero_setup(int count)
{
	int status;
	dev_t devt;

	usb_gadget_class = class_create(THIS_MODULE, "usb_nero");
	if (IS_ERR(usb_gadget_class)) {
		status = PTR_ERR(usb_gadget_class);
		usb_gadget_class = NULL;
		pr_err("unable to create usb_gadget class %d\n", status);
		return status;
	}

	status = alloc_chrdev_region(&devt, 0, count, "USB nero gadget");
	if (status) {
		pr_err("alloc_chrdev_region %d\n", status);
		class_destroy(usb_gadget_class);
		usb_gadget_class = NULL;
		return status;
	}

	major = MAJOR(devt);
	minors = count;

	return status;
}

static void gnero_cleanup(void)
{
	if (major) {
		unregister_chrdev_region(MKDEV(major, 0), minors);
		major = minors = 0;
	}
	class_destroy(usb_gadget_class);
	usb_gadget_class = NULL;
}
