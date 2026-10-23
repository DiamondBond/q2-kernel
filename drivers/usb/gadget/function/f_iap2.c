// SPDX-License-Identifier: GPL-2.0+
/*
 * f_iap2.c - Apple iAP2 USB gadget pass-through function
 *
 * This function exposes the iAP2 bulk interface to user space as /dev/iAP2.
 * The kernel owns only the USB endpoints and character-device transport;
 * iAP2 frame parsing and replies are intentionally left to user space.
 */

#include <linux/cdev.h>
#include <linux/completion.h>
#include <linux/configfs.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/usb/ch9.h>
#include <linux/usb/composite.h>
#include <linux/usb/gadget.h>
#include <linux/wait.h>

#include "configfs.h"

#define IAP2_INTERFACE_CLASS          0xff
#define IAP2_INTERFACE_SUBCLASS       0xf0
#define IAP2_INTERFACE_PROTOCOL       0x00

#define IAP2_MINORS                   1
#define IAP2_TX_Q_LEN                 2
#define IAP2_RX_Q_LEN                 2
#define IAP2_REQ_BUF_SIZE             8192
#define IAP2_BULK_INTERVAL            0x0a

struct iap2_opts {
	struct usb_function_instance func_inst;
	struct mutex lock;
	int refcnt;
	int minor;
	unsigned int tx_q_len;
	unsigned int rx_q_len;
	unsigned int req_buf_size;
};

struct iap2_dev;
struct iap2_file;

struct iap2_req {
	struct iap2_dev *dev;
	struct completion done;
	unsigned int session_seq;
	bool active;
	bool canceling;
};

struct iap2_cdev {
	struct cdev cdev;
	struct device dev;
	struct iap2_dev *iap2;
};

struct iap2_file {
	struct iap2_dev *dev;
	unsigned int lifecycle_seq;
	unsigned int session_seq;
};

struct iap2_dev {
	struct usb_function function;
	struct usb_gadget *gadget;
	struct usb_ep *in_ep;
	struct usb_ep *out_ep;
	s8 interface;
	bool unbound;
	bool cdev_added;
	bool cdev_reuse_blocked;
	bool opened;
	bool session_closing;
	unsigned int reset_seq;
	unsigned int lifecycle_seq;
	unsigned int session_seq;
	int tx_status;
	unsigned int set_alt_inflight;
	unsigned int tx_q_len;
	unsigned int rx_q_len;
	unsigned int req_buf_size;
	int minor;
	struct kref kref;
	struct iap2_cdev *node;
	struct iap2_file *open_file;
	struct list_head tx_reqs;
	struct list_head tx_reqs_pending;
	struct list_head tx_reqs_active;
	struct list_head rx_reqs;
	struct list_head rx_reqs_active;
	struct list_head rx_buffers;
	struct usb_request *current_rx_req;
	size_t current_rx_bytes;
	u8 *current_rx_buf;
	spinlock_t lock;
	spinlock_t ep_lock;
	struct mutex read_lock;
	struct mutex write_lock;
	struct completion set_alt_idle;
	wait_queue_head_t rx_wait;
	wait_queue_head_t tx_wait;
	wait_queue_head_t tx_flush_wait;
};

static const struct file_operations iap2_fops;
static DEFINE_IDA(iap2_ida);
static DEFINE_MUTEX(iap2_ida_lock);
static struct class *iap2_class;
static int iap2_major;
static int iap2_minors;

static inline struct iap2_dev *func_to_iap2(struct usb_function *f)
{
	return container_of(f, struct iap2_dev, function);
}

static inline struct iap2_opts *to_iap2_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct iap2_opts,
			   func_inst.group);
}

static inline struct iap2_req *iap2_req_ctx(struct usb_request *req)
{
	return req->context;
}

static bool iap2_file_unavailable(struct iap2_file *iap2_file)
{
	struct iap2_dev *dev = iap2_file->dev;

	return READ_ONCE(dev->unbound) ||
	       READ_ONCE(dev->lifecycle_seq) != iap2_file->lifecycle_seq ||
	       READ_ONCE(dev->session_seq) != iap2_file->session_seq;
}

static int iap2_reject_stale_cdev_rebind(struct iap2_dev *dev)
{
	unsigned long flags;
	bool busy;

	spin_lock_irqsave(&dev->lock, flags);
	busy = dev->cdev_added || dev->opened || dev->session_closing ||
	       dev->cdev_reuse_blocked;
	spin_unlock_irqrestore(&dev->lock, flags);

	if (!busy)
		return 0;

	pr_warn("iap2: refusing to reuse minor %d with a stale cdev or fd\n",
		dev->minor);
	return -EBUSY;
}

static struct usb_string iap2_string_defs[] = {
	{ .s = "iAP Interface" },
	{ },
};

static struct usb_gadget_strings iap2_string_table = {
	.language = 0x0409,
	.strings = iap2_string_defs,
};

static struct usb_gadget_strings *iap2_strings[] = {
	&iap2_string_table,
	NULL,
};

static struct usb_interface_descriptor iap2_interface_desc = {
	.bLength = sizeof(iap2_interface_desc),
	.bDescriptorType = USB_DT_INTERFACE,
	.bAlternateSetting = 0,
	.bNumEndpoints = 2,
	.bInterfaceClass = IAP2_INTERFACE_CLASS,
	.bInterfaceSubClass = IAP2_INTERFACE_SUBCLASS,
	.bInterfaceProtocol = IAP2_INTERFACE_PROTOCOL,
};

static struct usb_endpoint_descriptor fs_iap2_in_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.bInterval = IAP2_BULK_INTERVAL,
};

static struct usb_endpoint_descriptor fs_iap2_out_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.bInterval = IAP2_BULK_INTERVAL,
};

static struct usb_endpoint_descriptor hs_iap2_in_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = cpu_to_le16(512),
	.bInterval = IAP2_BULK_INTERVAL,
};

static struct usb_endpoint_descriptor hs_iap2_out_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = cpu_to_le16(512),
	.bInterval = IAP2_BULK_INTERVAL,
};

static struct usb_endpoint_descriptor ss_iap2_in_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = cpu_to_le16(1024),
	.bInterval = IAP2_BULK_INTERVAL,
};

static struct usb_ss_ep_comp_descriptor ss_iap2_in_comp_desc = {
	.bLength = sizeof(ss_iap2_in_comp_desc),
	.bDescriptorType = USB_DT_SS_ENDPOINT_COMP,
};

static struct usb_endpoint_descriptor ss_iap2_out_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize = cpu_to_le16(1024),
	.bInterval = IAP2_BULK_INTERVAL,
};

static struct usb_ss_ep_comp_descriptor ss_iap2_out_comp_desc = {
	.bLength = sizeof(ss_iap2_out_comp_desc),
	.bDescriptorType = USB_DT_SS_ENDPOINT_COMP,
};

static struct usb_descriptor_header *fs_iap2_function[] = {
	(struct usb_descriptor_header *)&iap2_interface_desc,
	(struct usb_descriptor_header *)&fs_iap2_in_desc,
	(struct usb_descriptor_header *)&fs_iap2_out_desc,
	NULL,
};

static struct usb_descriptor_header *hs_iap2_function[] = {
	(struct usb_descriptor_header *)&iap2_interface_desc,
	(struct usb_descriptor_header *)&hs_iap2_in_desc,
	(struct usb_descriptor_header *)&hs_iap2_out_desc,
	NULL,
};

static struct usb_descriptor_header *ss_iap2_function[] = {
	(struct usb_descriptor_header *)&iap2_interface_desc,
	(struct usb_descriptor_header *)&ss_iap2_in_desc,
	(struct usb_descriptor_header *)&ss_iap2_in_comp_desc,
	(struct usb_descriptor_header *)&ss_iap2_out_desc,
	(struct usb_descriptor_header *)&ss_iap2_out_comp_desc,
	NULL,
};

static struct usb_endpoint_descriptor *iap2_ep_desc(struct usb_gadget *gadget,
		struct usb_endpoint_descriptor *fs,
		struct usb_endpoint_descriptor *hs,
		struct usb_endpoint_descriptor *ss)
{
	switch (gadget->speed) {
	case USB_SPEED_SUPER:
	case USB_SPEED_SUPER_PLUS:
		return ss;
	case USB_SPEED_HIGH:
		return hs;
	default:
		return fs;
	}
}

static bool iap2_active(struct iap2_dev *dev)
{
	return !dev->unbound && dev->interface >= 0 && dev->in_ep &&
	       dev->out_ep && dev->in_ep->desc && dev->out_ep->desc;
}

static void iap2_dev_free(struct kref *kref)
{
	struct iap2_dev *dev = container_of(kref, struct iap2_dev, kref);

	kfree(dev);
}

static struct usb_request *iap2_req_alloc(struct iap2_dev *dev,
		struct usb_ep *ep, unsigned int len)
{
	struct usb_request *req;
	struct iap2_req *iap2_req;

	req = usb_ep_alloc_request(ep, GFP_KERNEL);
	if (!req)
		return NULL;

	iap2_req = kzalloc(sizeof(*iap2_req), GFP_KERNEL);
	if (!iap2_req) {
		usb_ep_free_request(ep, req);
		return NULL;
	}

	req->buf = kmalloc(len, GFP_KERNEL);
	if (!req->buf) {
		kfree(iap2_req);
		usb_ep_free_request(ep, req);
		return NULL;
	}

	iap2_req->dev = dev;
	init_completion(&iap2_req->done);
	req->context = iap2_req;
	INIT_LIST_HEAD(&req->list);
	return req;
}

static void iap2_req_free(struct usb_ep *ep, struct usb_request *req)
{
	if (!req)
		return;

	kfree(iap2_req_ctx(req));
	kfree(req->buf);
	usb_ep_free_request(ep, req);
}

static void iap2_free_reqs(struct usb_ep *ep, struct list_head *head)
{
	struct usb_request *req;

	while (!list_empty(head)) {
		req = list_first_entry(head, struct usb_request, list);
		list_del(&req->list);
		iap2_req_free(ep, req);
	}
}

static void iap2_move_reqs(struct list_head *from, struct list_head *to)
{
	struct usb_request *req;
	struct usb_request *tmp;

	list_for_each_entry_safe(req, tmp, from, list) {
		list_del_init(&req->list);
		list_add_tail(&req->list, to);
	}
}

static void iap2_rx_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct iap2_req *iap2_req = iap2_req_ctx(req);
	struct iap2_dev *dev = iap2_req->dev;
	unsigned long flags;

	(void)ep;

	spin_lock_irqsave(&dev->lock, flags);
	list_del_init(&req->list);
	iap2_req->active = false;
	iap2_req->canceling = false;
	if (!req->status && req->actual &&
	    iap2_req->session_seq == dev->session_seq)
		list_add_tail(&req->list, &dev->rx_buffers);
	else
		list_add_tail(&req->list, &dev->rx_reqs);
	complete_all(&iap2_req->done);
	spin_unlock_irqrestore(&dev->lock, flags);

	wake_up_interruptible(&dev->rx_wait);
}

static void iap2_tx_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct iap2_req *iap2_req = iap2_req_ctx(req);
	struct iap2_dev *dev = iap2_req->dev;
	unsigned long flags;

	(void)ep;

	spin_lock_irqsave(&dev->lock, flags);
	list_del_init(&req->list);
	iap2_req->active = false;
	iap2_req->canceling = false;
	if (req->status && iap2_req->session_seq == dev->session_seq &&
	    !dev->tx_status)
		dev->tx_status = req->status;
	list_add_tail(&req->list, &dev->tx_reqs);
	complete_all(&iap2_req->done);
	spin_unlock_irqrestore(&dev->lock, flags);

	wake_up_interruptible(&dev->tx_wait);
	wake_up_interruptible(&dev->tx_flush_wait);
}

static void iap2_queue_rx_locked(struct iap2_dev *dev, bool ep_locked)
{
	struct iap2_req *iap2_req;
	struct usb_request *req;
	unsigned int reset_seq;
	int ret;

	while (iap2_active(dev) && !dev->session_closing &&
	       !list_empty(&dev->rx_reqs)) {
		reset_seq = dev->reset_seq;
		req = list_first_entry(&dev->rx_reqs, struct usb_request, list);
		iap2_req = iap2_req_ctx(req);
		list_move_tail(&req->list, &dev->rx_reqs_active);
		reinit_completion(&iap2_req->done);
		iap2_req->session_seq = dev->session_seq;
		iap2_req->active = true;
		req->length = dev->req_buf_size;
		req->actual = 0;
		req->complete = iap2_rx_complete;

		spin_unlock(&dev->lock);
		if (!ep_locked)
			spin_lock(&dev->ep_lock);
		spin_lock(&dev->lock);
		if (dev->reset_seq != reset_seq || !iap2_active(dev) ||
		    dev->session_closing) {
			iap2_req->active = false;
			complete_all(&iap2_req->done);
			list_move_tail(&req->list, &dev->rx_reqs);
			if (!ep_locked)
				spin_unlock(&dev->ep_lock);
			break;
		}
		spin_unlock(&dev->lock);
		ret = usb_ep_queue(dev->out_ep, req, GFP_ATOMIC);
		spin_lock(&dev->lock);
		if (ret) {
			iap2_req->active = false;
			complete_all(&iap2_req->done);
			list_move_tail(&req->list, &dev->rx_reqs);
			if (!ep_locked)
				spin_unlock(&dev->ep_lock);
			break;
		}
		if (!ep_locked)
			spin_unlock(&dev->ep_lock);

		if (dev->reset_seq != reset_seq || !iap2_active(dev))
			break;
	}
}

static bool iap2_read_ready(struct iap2_file *iap2_file)
{
	struct iap2_dev *dev = iap2_file->dev;

	return iap2_file_unavailable(iap2_file) || dev->current_rx_bytes ||
	       !list_empty(&dev->rx_buffers);
}

static bool iap2_write_ready(struct iap2_file *iap2_file)
{
	struct iap2_dev *dev = iap2_file->dev;

	return iap2_file_unavailable(iap2_file) ||
	       !list_empty(&dev->tx_reqs);
}

static bool iap2_tx_flushed(struct iap2_file *iap2_file)
{
	struct iap2_dev *dev = iap2_file->dev;
	unsigned long flags;
	bool flushed;

	spin_lock_irqsave(&dev->lock, flags);
	flushed = iap2_file_unavailable(iap2_file) ||
		  dev->tx_status ||
		  (list_empty(&dev->tx_reqs_pending) &&
		   list_empty(&dev->tx_reqs_active));
	spin_unlock_irqrestore(&dev->lock, flags);

	return flushed;
}

/* Queue user-provided IN data as soon as the iAP2 endpoints become active. */
static int iap2_queue_pending_tx_locked(struct iap2_dev *dev, bool ep_locked)
{
	struct iap2_req *iap2_req;
	struct usb_request *req;
	unsigned int reset_seq;
	int ret;

	while (iap2_active(dev) && !dev->session_closing &&
	       !list_empty(&dev->tx_reqs_pending)) {
		reset_seq = dev->reset_seq;
		req = list_first_entry(&dev->tx_reqs_pending,
				       struct usb_request, list);
		iap2_req = iap2_req_ctx(req);
		list_move_tail(&req->list, &dev->tx_reqs_active);
		reinit_completion(&iap2_req->done);
		iap2_req->session_seq = dev->session_seq;
		iap2_req->active = true;

		spin_unlock(&dev->lock);
		if (!ep_locked)
			spin_lock(&dev->ep_lock);
		spin_lock(&dev->lock);
		if (dev->reset_seq != reset_seq || !iap2_active(dev) ||
		    dev->session_closing) {
			iap2_req->active = false;
			complete_all(&iap2_req->done);
			list_move_tail(&req->list, &dev->tx_reqs);
			if (!ep_locked)
				spin_unlock(&dev->ep_lock);
			return -ENODEV;
		}
		spin_unlock(&dev->lock);
		ret = usb_ep_queue(dev->in_ep, req, GFP_ATOMIC);
		spin_lock(&dev->lock);
		if (ret) {
			iap2_req->active = false;
			complete_all(&iap2_req->done);
			if (!list_empty(&req->list))
				list_move_tail(&req->list, &dev->tx_reqs);
			if (!ep_locked)
				spin_unlock(&dev->ep_lock);
			return ret;
		}
		if (!ep_locked)
			spin_unlock(&dev->ep_lock);

		if (dev->reset_seq != reset_seq || !iap2_active(dev))
			break;
	}

	return 0;
}

static void iap2_discard_pending_tx_locked(struct iap2_dev *dev)
{
	iap2_move_reqs(&dev->tx_reqs_pending, &dev->tx_reqs);
}

static void iap2_discard_rx_locked(struct iap2_dev *dev)
{
	if (dev->current_rx_req) {
		list_add_tail(&dev->current_rx_req->list, &dev->rx_reqs);
		dev->current_rx_req = NULL;
		dev->current_rx_bytes = 0;
		dev->current_rx_buf = NULL;
	}
	iap2_move_reqs(&dev->rx_buffers, &dev->rx_reqs);
}

static bool iap2_has_stale_active_locked(struct iap2_dev *dev)
{
	struct usb_request *req;

	list_for_each_entry(req, &dev->tx_reqs_active, list) {
		if (iap2_req_ctx(req)->session_seq != dev->session_seq)
			return true;
	}
	list_for_each_entry(req, &dev->rx_reqs_active, list) {
		if (iap2_req_ctx(req)->session_seq != dev->session_seq)
			return true;
	}

	return false;
}

static void iap2_cancel_active_session(struct iap2_dev *dev,
		struct usb_ep *ep, struct list_head *head,
		unsigned int session_seq)
{
	struct iap2_req *iap2_req;
	struct usb_request *req;
	struct usb_request *target;
	unsigned long flags;
	unsigned long ep_flags;
	int ret;

	if (!ep)
		return;

	for (;;) {
		target = NULL;
		spin_lock_irqsave(&dev->lock, flags);
		list_for_each_entry(req, head, list) {
			iap2_req = iap2_req_ctx(req);
			if (iap2_req->active && !iap2_req->canceling &&
			    iap2_req->session_seq == session_seq) {
				iap2_req->canceling = true;
				target = req;
				break;
			}
		}
		spin_unlock_irqrestore(&dev->lock, flags);
		if (!target)
			break;

		iap2_req = iap2_req_ctx(target);
		spin_lock_irqsave(&dev->ep_lock, ep_flags);
		ret = usb_ep_dequeue(ep, target);
		spin_unlock_irqrestore(&dev->ep_lock, ep_flags);
		if (!ret) {
			wait_for_completion(&iap2_req->done);
		} else {
			dev_warn_ratelimited(&dev->gadget->dev,
				"iap2: failed to cancel stale request: %d\n", ret);
		}
	}
}

static int iap2_open(struct inode *inode, struct file *file)
{
	struct iap2_dev *dev;
	struct iap2_file *iap2_file;
	unsigned long flags;
	int ret = 0;

	dev = container_of(inode->i_cdev, struct iap2_cdev, cdev)->iap2;
	iap2_file = kzalloc(sizeof(*iap2_file), GFP_KERNEL);
	if (!iap2_file)
		return -ENOMEM;

	mutex_lock(&dev->read_lock);
	spin_lock_irqsave(&dev->lock, flags);
	if (dev->unbound || !dev->cdev_added) {
		ret = -ENODEV;
	} else if (dev->opened || dev->session_closing ||
		   iap2_has_stale_active_locked(dev)) {
		ret = -EBUSY;
	} else {
		dev->opened = true;
		dev->tx_status = 0;
		iap2_file->dev = dev;
		iap2_file->lifecycle_seq = dev->lifecycle_seq;
		iap2_file->session_seq = dev->session_seq;
		dev->open_file = iap2_file;
		file->private_data = iap2_file;
		kref_get(&dev->kref);
		iap2_queue_rx_locked(dev, false);
	}
	spin_unlock_irqrestore(&dev->lock, flags);
	mutex_unlock(&dev->read_lock);

	if (ret)
		kfree(iap2_file);

	return ret;
}

static int iap2_release(struct inode *inode, struct file *file)
{
	struct iap2_file *iap2_file = file->private_data;
	struct iap2_dev *dev = iap2_file->dev;
	unsigned long flags;
	unsigned int closing_session_seq;
	bool wait_set_alt;

	(void)inode;

	mutex_lock(&dev->read_lock);
	mutex_lock(&dev->write_lock);
	spin_lock_irqsave(&dev->lock, flags);
	if (dev->open_file == iap2_file) {
		dev->open_file = NULL;
		dev->opened = false;
	}
	dev->session_closing = true;
	dev->reset_seq++;
	closing_session_seq = dev->session_seq;
	dev->session_seq++;
	wait_set_alt = dev->set_alt_inflight != 0;
	iap2_discard_pending_tx_locked(dev);
	iap2_discard_rx_locked(dev);
	file->private_data = NULL;
	spin_unlock_irqrestore(&dev->lock, flags);

	if (wait_set_alt)
		wait_for_completion(&dev->set_alt_idle);
	iap2_cancel_active_session(dev, dev->in_ep, &dev->tx_reqs_active,
				   closing_session_seq);
	iap2_cancel_active_session(dev, dev->out_ep, &dev->rx_reqs_active,
				   closing_session_seq);

	spin_lock_irqsave(&dev->lock, flags);
	dev->session_closing = false;
	spin_unlock_irqrestore(&dev->lock, flags);
	mutex_unlock(&dev->write_lock);
	mutex_unlock(&dev->read_lock);

	wake_up_interruptible(&dev->rx_wait);
	wake_up_interruptible(&dev->tx_wait);
	wake_up_interruptible(&dev->tx_flush_wait);
	kfree(iap2_file);
	kref_put(&dev->kref, iap2_dev_free);
	return 0;
}

static ssize_t iap2_read(struct file *file, char __user *buf, size_t len,
				 loff_t *ppos)
{
	struct iap2_file *iap2_file = file->private_data;
	struct iap2_dev *dev = iap2_file->dev;
	struct usb_request *current_rx_req = NULL;
	struct usb_request *req;
	unsigned long flags;
	size_t current_rx_bytes = 0;
	u8 *current_rx_buf = NULL;
	size_t size;
	size_t done;
	size_t not_copied;
	size_t copied = 0;
	int ret;

	(void)ppos;

	if (!len)
		return -EINVAL;

	mutex_lock(&dev->read_lock);
	spin_lock_irqsave(&dev->lock, flags);
	if (iap2_file_unavailable(iap2_file)) {
		ret = -ENODEV;
		goto out_unlock;
	}

	current_rx_req = dev->current_rx_req;
	current_rx_bytes = dev->current_rx_bytes;
	current_rx_buf = dev->current_rx_buf;
	dev->current_rx_req = NULL;
	dev->current_rx_bytes = 0;
	dev->current_rx_buf = NULL;

	for (;;) {
		if (iap2_file_unavailable(iap2_file)) {
			ret = copied ? copied : -ENODEV;
			goto out_drop_current;
		}

		iap2_queue_rx_locked(dev, false);
		if (current_rx_bytes || !list_empty(&dev->rx_buffers))
			break;

		if (file->f_flags & O_NONBLOCK) {
			ret = copied ? copied : -EAGAIN;
			goto out_save_current;
		}

		spin_unlock_irqrestore(&dev->lock, flags);
		ret = wait_event_interruptible(dev->rx_wait,
					       iap2_read_ready(iap2_file));
		spin_lock_irqsave(&dev->lock, flags);
		if (ret)
			goto out_save_current;
	}

	while (len && (current_rx_bytes || !list_empty(&dev->rx_buffers))) {
		if (!current_rx_bytes) {
			req = list_first_entry(&dev->rx_buffers,
					       struct usb_request, list);
			list_del_init(&req->list);
			if (!req->actual) {
				list_add_tail(&req->list, &dev->rx_reqs);
				continue;
			}
			current_rx_req = req;
			current_rx_bytes = req->actual;
			current_rx_buf = req->buf;
		}

		size = min(len, current_rx_bytes);
		spin_unlock_irqrestore(&dev->lock, flags);
		not_copied = copy_to_user(buf, current_rx_buf, size);
		spin_lock_irqsave(&dev->lock, flags);

		done = size - not_copied;
		copied += done;
		buf += done;
		len -= done;
		current_rx_buf += done;
		current_rx_bytes -= done;

		if (!current_rx_bytes) {
			list_add_tail(&current_rx_req->list, &dev->rx_reqs);
			current_rx_req = NULL;
			current_rx_buf = NULL;
				iap2_queue_rx_locked(dev, false);
		}

		if (not_copied) {
			ret = copied ? copied : -EFAULT;
			goto out_save_current;
		}

		if (iap2_file_unavailable(iap2_file)) {
			ret = copied ? copied : -ENODEV;
			goto out_drop_current;
		}
	}

	ret = copied ? copied : -EAGAIN;
	goto out_save_current;

out_drop_current:
	if (current_rx_req) {
		list_add_tail(&current_rx_req->list, &dev->rx_reqs);
		current_rx_req = NULL;
	}
out_save_current:
	dev->current_rx_req = current_rx_req;
	dev->current_rx_bytes = current_rx_bytes;
	dev->current_rx_buf = current_rx_buf;
out_unlock:
	spin_unlock_irqrestore(&dev->lock, flags);
	mutex_unlock(&dev->read_lock);
	return ret;
}

static ssize_t iap2_write(struct file *file, const char __user *buf,
				  size_t len, loff_t *ppos)
{
	struct iap2_file *iap2_file = file->private_data;
	struct iap2_dev *dev = iap2_file->dev;
	struct usb_request *req;
	unsigned long flags;
	size_t size;
	size_t copied = 0;
	int ret;

	(void)ppos;

	if (!len)
		return -EINVAL;

	mutex_lock(&dev->write_lock);
	while (len) {
		spin_lock_irqsave(&dev->lock, flags);
		while (!iap2_write_ready(iap2_file)) {
			if (file->f_flags & O_NONBLOCK) {
				spin_unlock_irqrestore(&dev->lock, flags);
				ret = copied ? copied : -EAGAIN;
				goto out;
			}

			spin_unlock_irqrestore(&dev->lock, flags);
			ret = wait_event_interruptible(dev->tx_wait,
							 iap2_write_ready(iap2_file));
			if (ret)
				goto out;
			spin_lock_irqsave(&dev->lock, flags);
		}

		if (iap2_file_unavailable(iap2_file)) {
			spin_unlock_irqrestore(&dev->lock, flags);
			ret = copied ? copied : -ENODEV;
			goto out;
		}

		req = list_first_entry(&dev->tx_reqs, struct usb_request, list);
		list_del_init(&req->list);
		size = min_t(size_t, len, dev->req_buf_size);
		spin_unlock_irqrestore(&dev->lock, flags);

		if (copy_from_user(req->buf, buf, size)) {
			spin_lock_irqsave(&dev->lock, flags);
			list_add_tail(&req->list, &dev->tx_reqs);
			spin_unlock_irqrestore(&dev->lock, flags);
			ret = copied ? copied : -EFAULT;
			goto out;
		}

		spin_lock_irqsave(&dev->lock, flags);
		if (iap2_file_unavailable(iap2_file)) {
			list_add_tail(&req->list, &dev->tx_reqs);
			spin_unlock_irqrestore(&dev->lock, flags);
			ret = copied ? copied : -ENODEV;
			goto out;
		}

		req->length = size;
		req->complete = iap2_tx_complete;
		req->zero = iap2_active(dev) &&
			    (len == size && (size % dev->in_ep->maxpacket) == 0);
		list_add_tail(&req->list, &dev->tx_reqs_pending);

		ret = iap2_queue_pending_tx_locked(dev, false);
		spin_unlock_irqrestore(&dev->lock, flags);
		if (ret) {
			ret = copied ? copied : ret;
			goto out;
		}

		copied += size;
		buf += size;
		len -= size;
	}

	ret = copied;

out:
	mutex_unlock(&dev->write_lock);
	return ret;
}

static int iap2_fsync(struct file *file, loff_t start, loff_t end,
		      int datasync)
{
	struct iap2_file *iap2_file = file->private_data;
	struct iap2_dev *dev = iap2_file->dev;
	unsigned long flags;
	int ret;

	(void)start;
	(void)end;
	(void)datasync;

	mutex_lock(&dev->write_lock);
	ret = wait_event_interruptible(dev->tx_flush_wait,
					 iap2_tx_flushed(iap2_file));
	if (!ret) {
		spin_lock_irqsave(&dev->lock, flags);
		if (iap2_file_unavailable(iap2_file))
			ret = -ENODEV;
		else
			ret = dev->tx_status;
		spin_unlock_irqrestore(&dev->lock, flags);
	}
	mutex_unlock(&dev->write_lock);

	return ret;
}

static __poll_t iap2_poll(struct file *file, poll_table *wait)
{
	struct iap2_file *iap2_file = file->private_data;
	struct iap2_dev *dev = iap2_file->dev;
	unsigned long flags;
	__poll_t status = 0;

	poll_wait(file, &dev->rx_wait, wait);
	poll_wait(file, &dev->tx_wait, wait);

	mutex_lock(&dev->read_lock);
	spin_lock_irqsave(&dev->lock, flags);
	if (iap2_file_unavailable(iap2_file)) {
		status = EPOLLERR | EPOLLHUP;
	} else {
		iap2_queue_rx_locked(dev, false);
		if (dev->current_rx_bytes || !list_empty(&dev->rx_buffers))
			status |= EPOLLIN | EPOLLRDNORM;
		if (!list_empty(&dev->tx_reqs))
			status |= EPOLLOUT | EPOLLWRNORM;
	}
	spin_unlock_irqrestore(&dev->lock, flags);
	mutex_unlock(&dev->read_lock);

	return status;
}

static const struct file_operations iap2_fops = {
	.owner = THIS_MODULE,
	.open = iap2_open,
	.read = iap2_read,
	.write = iap2_write,
	.fsync = iap2_fsync,
	.poll = iap2_poll,
	.release = iap2_release,
	.llseek = noop_llseek,
};

static void iap2_cdev_release(struct device *device)
{
	struct iap2_cdev *node = container_of(device, struct iap2_cdev, dev);

	if (node->iap2)
		kref_put(&node->iap2->kref, iap2_dev_free);
	kfree(node);
}

static struct iap2_cdev *iap2_cdev_alloc(struct iap2_dev *dev)
{
	struct iap2_cdev *node;
	int ret;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return ERR_PTR(-ENOMEM);

	node->iap2 = dev;
	kref_get(&dev->kref);
	device_initialize(&node->dev);
	node->dev.class = iap2_class;
	node->dev.devt = MKDEV(iap2_major, dev->minor);
	node->dev.release = iap2_cdev_release;
	if (dev->minor)
		ret = dev_set_name(&node->dev, "iAP2%d", dev->minor);
	else
		ret = dev_set_name(&node->dev, "iAP2");
	if (ret) {
		put_device(&node->dev);
		return ERR_PTR(ret);
	}

	cdev_init(&node->cdev, &iap2_fops);
	node->cdev.owner = THIS_MODULE;
	return node;
}

static void iap2_reset_interface(struct iap2_dev *dev, bool end_session)
{
	unsigned long flags;
	bool disable_in;
	bool disable_out;

	spin_lock_irqsave(&dev->lock, flags);
	dev->interface = -1;
	dev->reset_seq++;
	if (end_session) {
		dev->session_seq++;
		iap2_discard_pending_tx_locked(dev);
		iap2_discard_rx_locked(dev);
	}
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

	if (end_session)
		iap2_discard_rx_locked(dev);
	iap2_move_reqs(&dev->rx_reqs_active, &dev->rx_reqs);
	iap2_move_reqs(&dev->tx_reqs_active, &dev->tx_reqs);
	spin_unlock_irqrestore(&dev->lock, flags);

	wake_up_interruptible(&dev->rx_wait);
	wake_up_interruptible(&dev->tx_wait);
	wake_up_interruptible(&dev->tx_flush_wait);
}

static int iap2_set_interface(struct iap2_dev *dev, unsigned int intf)
{
	unsigned long flags;
	bool end_session;
	int ret;

	spin_lock_irqsave(&dev->lock, flags);
	end_session = dev->interface >= 0;
	spin_unlock_irqrestore(&dev->lock, flags);
	iap2_reset_interface(dev, end_session);
	if (READ_ONCE(dev->unbound))
		return -ENODEV;

	dev->out_ep->desc = iap2_ep_desc(dev->gadget, &fs_iap2_out_desc,
					   &hs_iap2_out_desc, &ss_iap2_out_desc);
	dev->in_ep->desc = iap2_ep_desc(dev->gadget, &fs_iap2_in_desc,
					  &hs_iap2_in_desc, &ss_iap2_in_desc);
	dev->out_ep->driver_data = dev;
	dev->in_ep->driver_data = dev;

	ret = usb_ep_enable(dev->out_ep);
	if (ret)
		goto fail;

	ret = usb_ep_enable(dev->in_ep);
	if (ret)
		goto fail;

	spin_lock_irqsave(&dev->lock, flags);
	if (dev->unbound) {
		spin_unlock_irqrestore(&dev->lock, flags);
		ret = -ENODEV;
		goto fail;
	}
	dev->interface = intf;
	dev->reset_seq++;
	iap2_queue_rx_locked(dev, true);
	ret = iap2_queue_pending_tx_locked(dev, true);
	spin_unlock_irqrestore(&dev->lock, flags);
	if (ret)
		goto fail;

	wake_up_interruptible(&dev->rx_wait);
	wake_up_interruptible(&dev->tx_wait);
	dev_info(&dev->gadget->dev, "iap2: /dev/iAP2 active on interface %u\n",
		 intf);
	return 0;

fail:
	dev_warn(&dev->gadget->dev, "iap2: enable interface %u failed %d\n",
		 intf, ret);
	iap2_reset_interface(dev, true);
	return ret;
}

static int iap2_enter_set_alt(struct iap2_dev *dev)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&dev->lock, flags);
	if (dev->unbound || dev->session_closing) {
		ret = -ENODEV;
	} else {
		if (!dev->set_alt_inflight)
			reinit_completion(&dev->set_alt_idle);
		dev->set_alt_inflight++;
	}
	spin_unlock_irqrestore(&dev->lock, flags);

	return ret;
}

static void iap2_leave_set_alt(struct iap2_dev *dev)
{
	unsigned long flags;
	bool complete_idle = false;

	spin_lock_irqsave(&dev->lock, flags);
	if (WARN_ON(!dev->set_alt_inflight)) {
		spin_unlock_irqrestore(&dev->lock, flags);
		return;
	}
	dev->set_alt_inflight--;
	if (!dev->set_alt_inflight &&
	    (dev->unbound || dev->session_closing))
		complete_idle = true;
	spin_unlock_irqrestore(&dev->lock, flags);

	if (complete_idle)
		complete_all(&dev->set_alt_idle);
}

static int iap2_func_set_alt(struct usb_function *f, unsigned int intf,
			     unsigned int alt)
{
	struct iap2_dev *dev = func_to_iap2(f);
	unsigned long flags;
	int ret;

	if (alt)
		return -EINVAL;

	ret = iap2_enter_set_alt(dev);
	if (ret)
		return ret;
	spin_lock_irqsave(&dev->ep_lock, flags);
	ret = iap2_set_interface(dev, intf);
	spin_unlock_irqrestore(&dev->ep_lock, flags);
	iap2_leave_set_alt(dev);

	return ret;
}

static void iap2_func_disable(struct usb_function *f)
{
	struct iap2_dev *dev = func_to_iap2(f);
	unsigned long flags;

	spin_lock_irqsave(&dev->ep_lock, flags);
	iap2_reset_interface(dev, true);
	spin_unlock_irqrestore(&dev->ep_lock, flags);
}

static int iap2_func_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct iap2_dev *dev = func_to_iap2(f);
	struct usb_composite_dev *cdev = c->cdev;
	struct iap2_cdev *node;
	struct usb_request *req;
	unsigned long flags;
	unsigned int i;
	int id;
	int ret;

	dev->gadget = cdev->gadget;
	ret = iap2_reject_stale_cdev_rebind(dev);
	if (ret)
		return ret;

	id = usb_string_id(cdev);
	if (id < 0)
		return id;
	iap2_string_defs[0].id = id;
	iap2_interface_desc.iInterface = id;

	id = usb_interface_id(c, f);
	if (id < 0)
		return id;
	iap2_interface_desc.bInterfaceNumber = id;

	dev->out_ep = usb_ep_autoconfig(cdev->gadget, &fs_iap2_out_desc);
	if (!dev->out_ep)
		return -ENODEV;

	dev->in_ep = usb_ep_autoconfig(cdev->gadget, &fs_iap2_in_desc);
	if (!dev->in_ep)
		return -ENODEV;

	hs_iap2_out_desc.bEndpointAddress = fs_iap2_out_desc.bEndpointAddress;
	hs_iap2_in_desc.bEndpointAddress = fs_iap2_in_desc.bEndpointAddress;
	ss_iap2_out_desc.bEndpointAddress = fs_iap2_out_desc.bEndpointAddress;
	ss_iap2_in_desc.bEndpointAddress = fs_iap2_in_desc.bEndpointAddress;

	ret = usb_assign_descriptors(f, fs_iap2_function, hs_iap2_function,
				     ss_iap2_function, ss_iap2_function);
	if (ret)
		return ret;

	ret = -ENOMEM;
	for (i = 0; i < dev->tx_q_len; i++) {
		req = iap2_req_alloc(dev, dev->in_ep, dev->req_buf_size);
		if (!req)
			goto fail_reqs;
		list_add_tail(&req->list, &dev->tx_reqs);
	}

	for (i = 0; i < dev->rx_q_len; i++) {
		req = iap2_req_alloc(dev, dev->out_ep, dev->req_buf_size);
		if (!req)
			goto fail_reqs;
		list_add_tail(&req->list, &dev->rx_reqs);
	}

	node = iap2_cdev_alloc(dev);
	if (IS_ERR(node)) {
		ret = PTR_ERR(node);
		goto fail_reqs;
	}

	ret = cdev_device_add(&node->cdev, &node->dev);
	if (ret)
		goto fail_cdev;

	spin_lock_irqsave(&dev->lock, flags);
	dev->node = node;
	dev->cdev_added = true;
	dev->unbound = false;
	dev->interface = -1;
	spin_unlock_irqrestore(&dev->lock, flags);

	dev_info(&dev->gadget->dev, "iap2: created /dev/%s\n",
		 dev_name(&node->dev));
	return 0;

fail_cdev:
	put_device(&node->dev);
fail_reqs:
	iap2_free_reqs(dev->in_ep, &dev->tx_reqs);
	iap2_free_reqs(dev->in_ep, &dev->tx_reqs_pending);
	iap2_free_reqs(dev->out_ep, &dev->rx_reqs);
	usb_free_all_descriptors(f);
	return ret;
}

static void iap2_begin_unbind(struct iap2_dev *dev)
{
	unsigned long flags;
	bool wait_set_alt;

	spin_lock_irqsave(&dev->lock, flags);
	dev->unbound = true;
	dev->interface = -1;
	dev->reset_seq++;
	dev->lifecycle_seq++;
	dev->session_seq++;
	iap2_discard_pending_tx_locked(dev);
	iap2_discard_rx_locked(dev);
	wait_set_alt = dev->set_alt_inflight != 0;
	spin_unlock_irqrestore(&dev->lock, flags);

	wake_up_interruptible(&dev->rx_wait);
	wake_up_interruptible(&dev->tx_wait);
	wake_up_interruptible(&dev->tx_flush_wait);
	if (wait_set_alt)
		wait_for_completion(&dev->set_alt_idle);
}

static struct iap2_cdev *iap2_mark_cdev_deleted(struct iap2_dev *dev,
		bool *cdev_added)
{
	struct iap2_cdev *node;
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);
	*cdev_added = dev->cdev_added;
	node = dev->node;
	dev->node = NULL;
	dev->cdev_added = false;
	spin_unlock_irqrestore(&dev->lock, flags);

	return node;
}

static bool iap2_block_cdev_reuse_if_busy(struct iap2_dev *dev,
		struct iap2_cdev *node)
{
	unsigned int cdev_refs;
	unsigned long flags;
	bool open;
	bool busy;

	spin_lock_irqsave(&dev->lock, flags);
	open = dev->opened || dev->open_file || dev->session_closing;
	spin_unlock_irqrestore(&dev->lock, flags);
	cdev_refs = kref_read(&node->cdev.kobj.kref);

	/* One reference is held by unbind while checking for stale users. */
	busy = open || cdev_refs > 1;
	if (!busy)
		return false;

	spin_lock_irqsave(&dev->lock, flags);
	dev->cdev_reuse_blocked = true;
	spin_unlock_irqrestore(&dev->lock, flags);
	return true;
}

static void iap2_func_unbind(struct usb_configuration *c,
			     struct usb_function *f)
{
	struct iap2_dev *dev = func_to_iap2(f);
	struct iap2_cdev *node;
	unsigned long flags;
	unsigned long ep_flags;
	bool cdev_added;
	bool cdev_busy;

	(void)c;

	iap2_begin_unbind(dev);
	spin_lock_irqsave(&dev->ep_lock, ep_flags);
	iap2_reset_interface(dev, false);
	spin_unlock_irqrestore(&dev->ep_lock, ep_flags);
	node = iap2_mark_cdev_deleted(dev, &cdev_added);

	if (cdev_added && node) {
		kobject_get(&node->cdev.kobj);
		cdev_device_del(&node->cdev, &node->dev);
		cdev_busy = iap2_block_cdev_reuse_if_busy(dev, node);
		kobject_put(&node->cdev.kobj);
	} else {
		cdev_busy = false;
		dev_warn(&dev->gadget->dev,
			 "iap2: cdev was not marked added during unbind\n");
	}
	if (cdev_busy)
		dev_warn(&dev->gadget->dev,
			 "iap2: stale cdev/fd blocks reuse of this function instance\n");
	if (node)
		put_device(&node->dev);

	mutex_lock(&dev->read_lock);
	mutex_lock(&dev->write_lock);
	spin_lock_irqsave(&dev->lock, flags);
	WARN_ON(!list_empty(&dev->tx_reqs_active));
	WARN_ON(!list_empty(&dev->rx_reqs_active));
	iap2_discard_pending_tx_locked(dev);
	iap2_discard_rx_locked(dev);
	spin_unlock_irqrestore(&dev->lock, flags);
	iap2_free_reqs(dev->in_ep, &dev->tx_reqs);
	iap2_free_reqs(dev->out_ep, &dev->rx_reqs);

	if (dev->in_ep)
		dev->in_ep->driver_data = NULL;
	if (dev->out_ep)
		dev->out_ep->driver_data = NULL;
	mutex_unlock(&dev->write_lock);
	mutex_unlock(&dev->read_lock);
	usb_free_all_descriptors(f);
}

static void iap2_free_func(struct usb_function *f)
{
	struct iap2_dev *dev = func_to_iap2(f);
	struct iap2_opts *opts = container_of(f->fi, struct iap2_opts,
					      func_inst);

	kref_put(&dev->kref, iap2_dev_free);
	mutex_lock(&opts->lock);
	opts->refcnt--;
	mutex_unlock(&opts->lock);
}

static struct usb_function *iap2_alloc_func(struct usb_function_instance *fi)
{
	struct iap2_opts *opts;
	struct iap2_dev *dev;

	opts = container_of(fi, struct iap2_opts, func_inst);

	mutex_lock(&opts->lock);
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		mutex_unlock(&opts->lock);
		return ERR_PTR(-ENOMEM);
	}

	kref_init(&dev->kref);
	opts->refcnt++;
	dev->minor = opts->minor;
	dev->tx_q_len = opts->tx_q_len;
	dev->rx_q_len = opts->rx_q_len;
	dev->req_buf_size = opts->req_buf_size;
	mutex_unlock(&opts->lock);

	dev->function.name = "iap2";
	dev->function.bind = iap2_func_bind;
	dev->function.unbind = iap2_func_unbind;
	dev->function.set_alt = iap2_func_set_alt;
	dev->function.disable = iap2_func_disable;
	dev->function.free_func = iap2_free_func;
	dev->function.strings = iap2_strings;
	dev->interface = -1;
	dev->unbound = true;
	dev->cdev_reuse_blocked = false;
	dev->session_closing = false;

	INIT_LIST_HEAD(&dev->tx_reqs);
	INIT_LIST_HEAD(&dev->tx_reqs_pending);
	INIT_LIST_HEAD(&dev->tx_reqs_active);
	INIT_LIST_HEAD(&dev->rx_reqs);
	INIT_LIST_HEAD(&dev->rx_reqs_active);
	INIT_LIST_HEAD(&dev->rx_buffers);
	spin_lock_init(&dev->lock);
	spin_lock_init(&dev->ep_lock);
	mutex_init(&dev->read_lock);
	mutex_init(&dev->write_lock);
	init_completion(&dev->set_alt_idle);
	init_waitqueue_head(&dev->rx_wait);
	init_waitqueue_head(&dev->tx_wait);
	init_waitqueue_head(&dev->tx_flush_wait);

	return &dev->function;
}

static void iap2_attr_release(struct config_item *item)
{
	struct iap2_opts *opts = to_iap2_opts(item);

	usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations iap2_item_ops = {
	.release = iap2_attr_release,
};

static struct config_item_type iap2_func_type = {
	.ct_item_ops = &iap2_item_ops,
	.ct_owner = THIS_MODULE,
};

static int iap2_setup_cdev(int count)
{
	dev_t devt;
	int ret;

	iap2_class = class_create(THIS_MODULE, "usb_iap2");
	if (IS_ERR(iap2_class)) {
		ret = PTR_ERR(iap2_class);
		iap2_class = NULL;
		return ret;
	}

	ret = alloc_chrdev_region(&devt, 0, count, "USB iAP2 gadget");
	if (ret) {
		class_destroy(iap2_class);
		iap2_class = NULL;
		return ret;
	}

	iap2_major = MAJOR(devt);
	iap2_minors = count;
	return 0;
}

static void iap2_cleanup_cdev(void)
{
	if (iap2_major) {
		unregister_chrdev_region(MKDEV(iap2_major, 0), iap2_minors);
		iap2_major = 0;
		iap2_minors = 0;
	}
	if (iap2_class) {
		class_destroy(iap2_class);
		iap2_class = NULL;
	}
}

static void iap2_put_minor(int minor)
{
	ida_simple_remove(&iap2_ida, minor);
}

static void iap2_free_inst(struct usb_function_instance *fi)
{
	struct iap2_opts *opts = container_of(fi, struct iap2_opts, func_inst);

	mutex_lock(&iap2_ida_lock);
	iap2_put_minor(opts->minor);
	if (ida_is_empty(&iap2_ida))
		iap2_cleanup_cdev();
	mutex_unlock(&iap2_ida_lock);

	kfree(opts);
}

static struct usb_function_instance *iap2_alloc_inst(void)
{
	struct iap2_opts *opts;
	int ret;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);

	mutex_init(&opts->lock);
	opts->func_inst.free_func_inst = iap2_free_inst;
	opts->tx_q_len = IAP2_TX_Q_LEN;
	opts->rx_q_len = IAP2_RX_Q_LEN;
	opts->req_buf_size = IAP2_REQ_BUF_SIZE;

	mutex_lock(&iap2_ida_lock);
	if (ida_is_empty(&iap2_ida)) {
		ret = iap2_setup_cdev(IAP2_MINORS);
		if (ret)
			goto fail;
	}

	ret = ida_simple_get(&iap2_ida, 0, IAP2_MINORS, GFP_KERNEL);
	if (ret < 0)
		goto fail_cleanup;
	opts->minor = ret;
	mutex_unlock(&iap2_ida_lock);

	config_group_init_type_name(&opts->func_inst.group, "",
				    &iap2_func_type);
	return &opts->func_inst;

fail_cleanup:
	if (ida_is_empty(&iap2_ida))
		iap2_cleanup_cdev();
fail:
	mutex_unlock(&iap2_ida_lock);
	kfree(opts);
	return ERR_PTR(ret);
}

DECLARE_USB_FUNCTION_INIT(iap2, iap2_alloc_inst, iap2_alloc_func);
MODULE_LICENSE("GPL");
