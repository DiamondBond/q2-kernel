// SPDX-License-Identifier: GPL-2.0+
/*
 * f_display.c - USB display function driver
 *
 * Copied from drivers/usb/gadget/legacy/display.c,
 * which was:
 *
 * display.c -- Display gadget driver
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
#include <linux/atomic.h>
#include <linux/dma-mapping.h>

#include <asm/byteorder.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/uaccess.h>
#include <asm/unaligned.h>
#include <linux/minmax.h>

#include <linux/usb/ch9.h>
#include <linux/usb/composite.h>
#include <linux/usb/gadget.h>

#include "u_display.h"

#define DISPLAY_MINORS                  4

static int major, minors;
static struct class *usb_gadget_class;
static DEFINE_IDA(display_ida);
static DEFINE_MUTEX(display_ida_lock); /* protects access do display_ida */

/*-------------------------------------------------------------------------*/

struct display_dev {
    spinlock_t        lock;        /* lock this structure */
    /* lock buffer lists during read/write calls */
    struct mutex        lock_display_io;
    struct usb_gadget    *gadget;
    s8            interface;
    struct usb_ep        *out_ep;
    struct kref             kref;
    struct list_head    rx_reqs;    /* List of free RX structs */
    struct list_head    rx_reqs_active;    /* List of Active RX xfers */
    struct list_head    rx_buffers;    /* List of completed xfers */
    /* wait until there is data to be read. */
    wait_queue_head_t   rx_wait;
    struct usb_request   *current_rx_req;
    size_t            current_rx_bytes;
    u8            *current_rx_buf;
    u8            display_status;
    u8            backlight;
    int           minor;
    struct cdev        display_cdev;
    u8            display_cdev_open;
    unsigned        rx_q_len;
    unsigned int rx_req_buf_size;
    struct usb_function    function;

    char *report_desc;              /* We don't own memory! */
    unsigned short report_length;

    char *edid_desc;                /* We don't own memory! */
    unsigned short edid_length;

    char *rotate_edid_desc;                /* We don't own memory! */
    unsigned short rotate_edid_length;
    u8 interface_id;
};

static inline struct display_dev *func_to_display(struct usb_function *f)
{
    return container_of(f, struct display_dev, function);
}

/*-------------------------------------------------------------------------*/

/*
 * DESCRIPTORS ... most are static, but strings and (full) configuration
 * descriptors are built on demand.
 */

#define DISPLAY_SPECIFIC_DES            (USB_TYPE_VENDOR | 0x01F)
#define DISPLAY_REQ_GET_STATUS          0x01
#define DISPLAY_REQ_GET_EDID            0x02
#define DISPLAY_REQ_SOFT_RESET          0x03
#define DISPLAY_REQ_GET_BACKLIGHT       0x04
#define DISPLAY_REQ_GET_ROTATE_EDID     0x05

static struct usb_interface_descriptor intf_desc = {
    .bLength =        sizeof(intf_desc),
    .bDescriptorType = USB_DT_INTERFACE,
    .bNumEndpoints = 1,
    .bInterfaceClass = USB_CLASS_VENDOR_SPEC,
    .bInterfaceSubClass = USB_SUBCLASS_VENDOR_SPEC,
    .bInterfaceProtocol = 0,
    .iInterface = 0,
};

static struct usb_endpoint_descriptor fs_ep_out_desc = {
    .bLength =        USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =    USB_DT_ENDPOINT,
    .bEndpointAddress =    USB_DIR_OUT,
    .bmAttributes =        USB_ENDPOINT_XFER_BULK
};

static struct usb_descriptor_header *fs_display_function[] = {
    (struct usb_descriptor_header *) &intf_desc,
    NULL, // (struct usb_descriptor_header *) &specific_descriptor,
    (struct usb_descriptor_header *) &fs_ep_out_desc,
    NULL
};

/*
 * usb 2.0 devices need to expose both high speed and full speed
 * descriptors, unless they only run at full speed.
 */

static struct usb_endpoint_descriptor hs_ep_out_desc = {
    .bLength =        USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =    USB_DT_ENDPOINT,
    .bmAttributes =        USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize =    cpu_to_le16(512)
};

static struct usb_descriptor_header *hs_display_function[] = {
    (struct usb_descriptor_header *) &intf_desc,
    NULL, // (struct usb_descriptor_header *) &specific_descriptor,
    (struct usb_descriptor_header *) &hs_ep_out_desc,
    NULL
};

/* maxpacket and other transfer characteristics vary by speed. */
static inline struct usb_endpoint_descriptor *ep_desc(struct usb_gadget *gadget,
                    struct usb_endpoint_descriptor *fs,
                    struct usb_endpoint_descriptor *hs,
                    struct usb_endpoint_descriptor *ss)
{
    switch (gadget->speed) {
    case USB_SPEED_HIGH:
        return hs;
    default:
        return fs;
    }
}

/*-------------------------------------------------------------------------*/

static void display_dev_free(struct kref *kref)
{
    struct display_dev *dev = container_of(kref, struct display_dev, kref);

    kfree(dev);
}

static struct usb_request *
display_req_alloc(struct usb_ep *ep, unsigned len, gfp_t gfp_flags)
{
    struct usb_request *req;

    req = usb_ep_alloc_request(ep, gfp_flags);

    if (req != NULL) {
        req->length = len;
        req->buf = kmalloc(ALIGN(len, dma_get_cache_alignment()),
                         gfp_flags);
        if (req->buf == NULL) {
            usb_ep_free_request(ep, req);
            return NULL;
        }
    }

    return req;
}

static void
display_req_free(struct usb_ep *ep, struct usb_request *req)
{
    if (ep != NULL && req != NULL) {
        kfree(req->buf);
        usb_ep_free_request(ep, req);
    }
}

/*-------------------------------------------------------------------------*/

static void rx_complete(struct usb_ep *ep, struct usb_request *req)
{
    struct display_dev *dev = ep->driver_data;
    int status = req->status;
    unsigned long flags;

    spin_lock_irqsave(&dev->lock, flags);

    list_del_init(&req->list);    /* Remode from Active List */

    switch (status) {

    /* normal completion */
    case 0:
        if (req->actual > 0) {
            list_add_tail(&req->list, &dev->rx_buffers);
            DBG(dev, "G_Display : rx length %d\n", req->actual);
        } else {
            list_add(&req->list, &dev->rx_reqs);
        }
        break;

    /* software-driven interface shutdown */
    case -ECONNRESET:        /* unlink */
    case -ESHUTDOWN:        /* disconnect etc */
        VDBG(dev, "rx shutdown, code %d\n", status);
        list_add(&req->list, &dev->rx_reqs);
        break;

    /* for hardware automagic (such as pxa) */
    case -ECONNABORTED:        /* endpoint reset */
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

/*-------------------------------------------------------------------------*/

static int
display_open(struct inode *inode, struct file *fd)
{
    struct display_dev *dev;
    unsigned long flags;
    int ret = -EBUSY;

    dev = container_of(inode->i_cdev, struct display_dev, display_cdev);

    spin_lock_irqsave(&dev->lock, flags);

    if (dev->interface < 0) {
        spin_unlock_irqrestore(&dev->lock, flags);
        return -ENODEV;
    }

    if (!dev->display_cdev_open) {
        dev->display_cdev_open = 1;
        fd->private_data = dev;
        ret = 0;
    }

    spin_unlock_irqrestore(&dev->lock, flags);

    if (!ret)
        kref_get(&dev->kref);

    DBG(dev, "display_open returned %x\n", ret);
    return ret;
}

static int
display_close(struct inode *inode, struct file *fd)
{
    struct display_dev *dev = fd->private_data;
    unsigned long flags;

    spin_lock_irqsave(&dev->lock, flags);
    dev->display_cdev_open = 0;
    dev->display_status = 0;
    fd->private_data = NULL;
    spin_unlock_irqrestore(&dev->lock, flags);

    kref_put(&dev->kref, display_dev_free);
    DBG(dev, "display_close\n");

    return 0;
}

/* This function must be called with interrupts turned off. */
static void
setup_rx_reqs(struct display_dev *dev)
{
    struct usb_request *req;

    while (likely(!list_empty(&dev->rx_reqs))) {
        int error;

        req = container_of(dev->rx_reqs.next,
                struct usb_request, list);
        list_del_init(&req->list);

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
        /* if the req is empty, then add it into dev->rx_reqs_active. */
        else if (list_empty(&req->list))
            list_add(&req->list, &dev->rx_reqs_active);
    }
}

static ssize_t
display_read(struct file *fd, char __user *buf, size_t len, loff_t *ptr)
{
    struct display_dev *dev = fd->private_data;
    unsigned long flags;
    size_t size;
    size_t bytes_copied;
    struct usb_request *req;
    /* This is a pointer to the current USB rx request. */
    struct usb_request *current_rx_req;
    /* This is the number of bytes in the current rx buffer. */
    size_t current_rx_bytes;
    /* This is a pointer to the current rx buffer. */
    u8 *current_rx_buf;
    int ret;

    if (len == 0)
        return -EINVAL;

    DBG(dev, "display_read trying to read %d bytes\n", (int)len);

    mutex_lock(&dev->lock_display_io);
    spin_lock_irqsave(&dev->lock, flags);

    if (dev->interface < 0) {
        spin_unlock_irqrestore(&dev->lock, flags);
        mutex_unlock(&dev->lock_display_io);
        return -ENODEV;
    }

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
            mutex_unlock(&dev->lock_display_io);
            return -EAGAIN;
        }

        /* Sleep until data is available */
        wait_event_interruptible(dev->rx_wait,
                (likely(!list_empty(&dev->rx_buffers))) || list_empty(&dev->rx_reqs_active));
        spin_lock_irqsave(&dev->lock, flags);

        if (list_empty(&dev->rx_reqs_active)) {
            spin_unlock_irqrestore(&dev->lock, flags);
            mutex_unlock(&dev->lock_display_io);
            return -EAGAIN;
        }
    }

read_again:
    /* We have data to return then copy it to the caller's buffer.*/
    if ((current_rx_bytes || likely(!list_empty(&dev->rx_buffers)))
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
                goto read_again;
            }
        }

        /* Don't leave irqs off while doing memory copies */
        spin_unlock_irqrestore(&dev->lock, flags);

        if (len > current_rx_bytes)
            size = current_rx_bytes;
        else
            size = len;

        /* Ensure that the copy is completed */
        do {
            ret = copy_to_user(buf, current_rx_buf, size);

            size -= ret;
            bytes_copied += size;

            buf += size;
            current_rx_buf += size;

            size = ret;
        } while (ret);
        size = bytes_copied;

        spin_lock_irqsave(&dev->lock, flags);

        /* If we not returning all the data left in this RX request
         * buffer then adjust the amount of data left in the buffer.
         * Othewise if we are done with this RX request buffer then
         * requeue it to get any incoming data from the USB host.
         */
        if (size < current_rx_bytes) {
            current_rx_bytes -= size;
            current_rx_buf += size;
            ERROR(dev, "Gadget display data not fully read!\n");
        } else {
            list_add(&current_rx_req->list, &dev->rx_reqs);
            current_rx_bytes = 0;
            current_rx_buf = NULL;
            current_rx_req = NULL;
        }
    }

    dev->current_rx_req = current_rx_req;
    dev->current_rx_bytes = current_rx_bytes;
    dev->current_rx_buf = current_rx_buf;

    spin_unlock_irqrestore(&dev->lock, flags);
    mutex_unlock(&dev->lock_display_io);

    DBG(dev, "display_read returned %d bytes\n", (int)bytes_copied);

    if (bytes_copied)
        return bytes_copied;
    else
        return -EAGAIN;
}

static int
display_fsync(struct file *fd, loff_t start, loff_t end, int datasync)
{
    struct display_dev *dev = fd->private_data;
    struct inode *inode = file_inode(fd);
    unsigned long flags;
    int ret = 0;

    inode_lock(inode);
    spin_lock_irqsave(&dev->lock, flags);

    if (dev->interface < 0)
        ret = -ENODEV;

    spin_unlock_irqrestore(&dev->lock, flags);
    inode_unlock(inode);

    return ret;
}

static __poll_t
display_poll(struct file *fd, poll_table *wait)
{
    struct display_dev *dev = fd->private_data;
    unsigned long flags;
    __poll_t status = 0;

    mutex_lock(&dev->lock_display_io);
    spin_lock_irqsave(&dev->lock, flags);

    if (dev->interface < 0) {
        spin_unlock_irqrestore(&dev->lock, flags);
        mutex_unlock(&dev->lock_display_io);
        return EPOLLERR | EPOLLHUP;
    }

    setup_rx_reqs(dev);
    spin_unlock_irqrestore(&dev->lock, flags);
    mutex_unlock(&dev->lock_display_io);

    poll_wait(fd, &dev->rx_wait, wait);

    spin_lock_irqsave(&dev->lock, flags);
    if (likely(dev->current_rx_bytes) ||
            likely(!list_empty(&dev->rx_buffers)))
        status |= EPOLLIN | EPOLLRDNORM;

    spin_unlock_irqrestore(&dev->lock, flags);

    return status;
}

#define CMD_SET_DISPLAY_STATUS          _IOW('U', 111, int)
#define CMD_SET_BACKLIGHT_VAL           _IOW('U', 112, int)

static long
display_ioctl(struct file *fd, unsigned int code, unsigned long arg)
{
    struct display_dev *dev = fd->private_data;
    int status = 0;

    DBG(dev, "display_ioctl: cmd=0x%4.4x, arg=%lu\n", code, arg);

    switch (code) {
    case CMD_SET_DISPLAY_STATUS:
        dev->display_status = (u8)arg;
        break;
    case CMD_SET_BACKLIGHT_VAL:
        dev->backlight = (u8)arg;
        break;
    default:
        printk(KERN_ERR "display_ioctl: not support this cmd: %d\n", code);
        return -1;
    }

    return status;
}

/* used after endpoint configuration */
static const struct file_operations display_io_operations = {
    .owner =          THIS_MODULE,
    .open =           display_open,
    .read =           display_read,
    .fsync =          display_fsync,
    .poll =           display_poll,
    .unlocked_ioctl = display_ioctl,
    .release =        display_close,
    .llseek =         noop_llseek,
};

/*-------------------------------------------------------------------------*/

static int
set_display_interface(struct display_dev *dev)
{
    int result = 0;

    dev->out_ep->desc = ep_desc(dev->gadget, &fs_ep_out_desc, &hs_ep_out_desc, NULL);
    dev->out_ep->driver_data = dev;

    result = usb_ep_enable(dev->out_ep);
    if (result != 0) {
        DBG(dev, "enable %s --> %d\n", dev->out_ep->name, result);
        goto done;
    }

done:
    /* on error, disable any endpoints  */
    if (result != 0) {
        (void) usb_ep_disable(dev->out_ep);
        dev->out_ep->desc = NULL;
    }

    /* caller is responsible for cleanup on error */
    return result;
}

static void display_reset_interface(struct display_dev *dev)
{
    unsigned long flags;

    if (dev->interface < 0)
        return;

    DBG(dev, "%s\n", __func__);

    spin_lock_irqsave(&dev->lock, flags);
    dev->interface = -1;
    spin_unlock_irqrestore(&dev->lock, flags);


    if (dev->out_ep->desc)
        usb_ep_disable(dev->out_ep);

    spin_lock_irqsave(&dev->lock, flags);
    dev->out_ep->desc = NULL;
    dev->interface = -1;
    spin_unlock_irqrestore(&dev->lock, flags);
}

/* Change our operational Interface. */
static int set_interface(struct display_dev *dev, unsigned number)
{
    int result = 0;
    unsigned long flags;

    /* Free the current interface */
    display_reset_interface(dev);

    result = set_display_interface(dev);
    if (result)
        display_reset_interface(dev);
    else
        dev->interface = number;

    spin_lock_irqsave(&dev->lock, flags);
    if (dev->display_cdev_open) {
        wake_up_interruptible(&dev->rx_wait);
    }
    spin_unlock_irqrestore(&dev->lock, flags);

    if (!result)
        INFO(dev, "Using interface %x\n", number);

    return result;
}

static void display_soft_reset(struct display_dev *dev)
{
    struct usb_request    *req;

    INFO(dev, "Received Printer Reset Request\n");

    if (usb_ep_disable(dev->out_ep))
        DBG(dev, "Failed to disable USB out_ep\n");

    if (dev->current_rx_req != NULL) {
        list_add(&dev->current_rx_req->list, &dev->rx_reqs);
        dev->current_rx_req = NULL;
    }
    dev->current_rx_bytes = 0;
    dev->current_rx_buf = NULL;

    while (likely(!(list_empty(&dev->rx_buffers)))) {
        req = container_of(dev->rx_buffers.next, struct usb_request,
                list);
        list_del_init(&req->list);
        list_add(&req->list, &dev->rx_reqs);
    }

    while (likely(!(list_empty(&dev->rx_reqs_active)))) {
        req = container_of(dev->rx_reqs_active.next, struct usb_request,
                list);
        list_del_init(&req->list);
        list_add(&req->list, &dev->rx_reqs);
    }

    if (usb_ep_enable(dev->out_ep))
        DBG(dev, "Failed to enable USB out_ep\n");
}

/*-------------------------------------------------------------------------*/

static bool gdisplay_req_match(struct usb_function *f,
                   const struct usb_ctrlrequest *ctrl,
                   bool config0)
{
    struct display_dev *dev = func_to_display(f);

    if (config0 || ctrl->wIndex != dev->interface_id)
        return false;

    switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {
        case ((USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE) << 8 | USB_REQ_GET_DESCRIPTOR):
            switch (ctrl->wValue >> 8) {
                case DISPLAY_SPECIFIC_DES:
                    return true;
            }
            break;

        case ((USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) << 8 | DISPLAY_REQ_GET_STATUS):
            return true;

        case ((USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) << 8 | DISPLAY_REQ_GET_EDID):
            return true;

        case ((USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) << 8 | DISPLAY_REQ_GET_ROTATE_EDID):
            return true;

        case ((USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) << 8 | DISPLAY_REQ_SOFT_RESET):
            return true;

        case ((USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) << 8 | DISPLAY_REQ_GET_BACKLIGHT):
            return true;

        default:
            return false;
    }

    return false;
}

/*
 * The setup() callback implements all the ep0 functionality that's not
 * handled lower down.
 */
static int display_func_setup(struct usb_function *f,
        const struct usb_ctrlrequest *ctrl)
{
    int offset = 0;
    struct display_dev *dev = func_to_display(f);
    struct usb_composite_dev *cdev = f->config->cdev;
    struct usb_request *req = cdev->req;
    u8 *buf = req->buf;
    int len = -EOPNOTSUPP;
    u16 wIndex = le16_to_cpu(ctrl->wIndex);
    u16 wValue = le16_to_cpu(ctrl->wValue);
    u16 wLength = le16_to_cpu(ctrl->wLength);

    DBG(cdev, "ctrl req%02x.%02x v%04x i%04x l%d\n",
        ctrl->bRequestType, ctrl->bRequest, wValue, wIndex, wLength);

    switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {
        case ((USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE) << 8 | USB_REQ_GET_DESCRIPTOR):
            switch (ctrl->wValue >> 8) {
                case DISPLAY_SPECIFIC_DES:
                    len = min_t(unsigned short, ctrl->wLength, dev->report_length);
                    memcpy(buf, dev->report_desc, len);
                    break;

                default:
                    VDBG(cdev, "%s %d: Unknown descriptor request\n", __func__, __LINE__);
                    break;
            }
            break;

        case ((USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) << 8 | DISPLAY_REQ_GET_STATUS):
            buf[0] = dev->display_status;
            len = min_t(unsigned short, ctrl->wLength, 1);
            break;

        case ((USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) << 8 | DISPLAY_REQ_GET_EDID):
            offset = wValue;
            if (offset < dev->edid_length)
                len = min_t(unsigned short, ctrl->wLength, dev->edid_length - offset);
            else
                len = 0;

            memcpy(buf, &dev->edid_desc[offset], len);
            if (len < ctrl->wLength) {
                memset(buf + len, 0, ctrl->wLength - len);
                len = ctrl->wLength;
            }
            break;

        case ((USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) << 8 | DISPLAY_REQ_GET_ROTATE_EDID):
            offset = wValue;
            if (offset < dev->rotate_edid_length)
                len = min_t(unsigned short, ctrl->wLength, dev->rotate_edid_length - offset);
            else
                len = 0;

            memcpy(buf, &dev->rotate_edid_desc[offset], len);
            if (len < ctrl->wLength) {
                memset(buf + len, 0, ctrl->wLength - len);
                len = ctrl->wLength;
            }
            break;

        case ((USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) << 8 | DISPLAY_REQ_SOFT_RESET):
            display_soft_reset(dev);
            len = 0;
            break;

        case ((USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE) << 8 | DISPLAY_REQ_GET_BACKLIGHT):
            buf[0] = dev->backlight;
            len = min_t(unsigned short, ctrl->wLength, 1);
            break;

        default:
            VDBG(cdev, "%s %d: Unknown descriptor request\n", __func__, __LINE__);
            break;
    }

    if (len >= 0) {
        req->zero = 0;
        req->length = len;
        len = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
        if (len < 0) {
            ERROR(cdev, "%s:%d Error!\n", __func__, __LINE__);
            req->status = 0;
        }
    }
    return len;
}

static int display_func_bind(struct usb_configuration *c,
        struct usb_function *f)
{
    struct usb_gadget *gadget = c->cdev->gadget;
    struct display_dev *dev = func_to_display(f);
    struct device *pdev;
    struct usb_composite_dev *cdev = c->cdev;
    struct usb_ep *out_ep = NULL;
    struct usb_request *req;
    dev_t devt;
    int id;
    int ret;
    u32 i;

    id = usb_interface_id(c, f);
    if (id < 0)
        return id;
    intf_desc.bInterfaceNumber = id;
    dev->interface_id = id;

    /* finish hookup to lower layer ... */
    dev->gadget = gadget;

    out_ep = usb_ep_autoconfig(cdev->gadget, &fs_ep_out_desc);
    if (!out_ep) {
        dev_err(&cdev->gadget->dev, "can't autoconfigure on %s\n",
            cdev->gadget->name);
        return -ENODEV;
    }

    /* assumes that all endpoints are dual-speed */
    hs_ep_out_desc.bEndpointAddress = fs_ep_out_desc.bEndpointAddress;

    fs_display_function[1] = (struct usb_descriptor_header *)dev->report_desc;
    hs_display_function[1] = (struct usb_descriptor_header *)dev->report_desc;

    ret = usb_assign_descriptors(f, fs_display_function, hs_display_function, NULL, NULL);
    if (ret)
        return ret;

    dev->out_ep = out_ep;

    ret = -ENOMEM;
    for (i = 0; i < dev->rx_q_len; i++) {
        req = display_req_alloc(dev->out_ep, dev->rx_req_buf_size, GFP_KERNEL);
        if (!req)
            goto fail_rx_reqs;
        list_add(&req->list, &dev->rx_reqs);
    }

    /* Setup the sysfs files for the display gadget. */
    devt = MKDEV(major, dev->minor);
    pdev = device_create(usb_gadget_class, NULL, devt,
                  NULL, "g_display%d", dev->minor);
    if (IS_ERR(pdev)) {
        ERROR(dev, "Failed to create device: g_display\n");
        ret = PTR_ERR(pdev);
        goto fail_rx_reqs;
    }

    /*
     * Register a character device as an interface to a user mode
     * program that handles the display specific functionality.
     */
    cdev_init(&dev->display_cdev, &display_io_operations);
    dev->display_cdev.owner = THIS_MODULE;
    ret = cdev_add(&dev->display_cdev, devt, 1);
    if (ret) {
        ERROR(dev, "Failed to open char device\n");
        goto fail_cdev_add;
    }

    return 0;

fail_cdev_add:
    device_destroy(usb_gadget_class, devt);

fail_rx_reqs:
    while (!list_empty(&dev->rx_reqs)) {
        req = container_of(dev->rx_reqs.next, struct usb_request, list);
        list_del(&req->list);
        display_req_free(dev->out_ep, req);
    }

    usb_free_all_descriptors(f);
    return ret;

}

static int display_func_set_alt(struct usb_function *f,
        unsigned intf, unsigned alt)
{
    struct display_dev *dev = func_to_display(f);
    int ret = -ENOTSUPP;

    if (!alt)
        ret = set_interface(dev, intf);

    return ret;
}

static void display_func_disable(struct usb_function *f)
{
    struct display_dev *dev = func_to_display(f);

    DBG(dev, "%s\n", __func__);

    display_reset_interface(dev);
}

static inline struct f_display_opts
*to_f_display_opts(struct config_item *item)
{
    return container_of(to_config_group(item), struct f_display_opts,
                func_inst.group);
}

static void display_attr_release(struct config_item *item)
{
    struct f_display_opts *opts = to_f_display_opts(item);

    usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations display_item_ops = {
    .release = display_attr_release,
};

static ssize_t f_display_opts_rx_q_len_show(struct config_item *item,
                     char *page)
{
    struct f_display_opts *opts = to_f_display_opts(item);
    int result;

    mutex_lock(&opts->lock);
    result = sprintf(page, "%d\n", opts->rx_q_len);
    mutex_unlock(&opts->lock);

    return result;
}

static ssize_t f_display_opts_rx_q_len_store(struct config_item *item,
                      const char *page, size_t len)
{
    struct f_display_opts *opts = to_f_display_opts(item);
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

    opts->rx_q_len = (unsigned)num;

    ret = len;
end:
    mutex_unlock(&opts->lock);
    return ret;
}

CONFIGFS_ATTR(f_display_opts_, rx_q_len);

static ssize_t f_display_opts_rx_req_buf_size_show(struct config_item *item,
                     char *page)
{
    struct f_display_opts *opts = to_f_display_opts(item);
    int result;

    mutex_lock(&opts->lock);
    result = sprintf(page, "%d\n", opts->rx_req_buf_size);
    mutex_unlock(&opts->lock);

    return result;
}

static ssize_t f_display_opts_rx_req_buf_size_store(struct config_item *item,
                      const char *page, size_t len)
{
    struct f_display_opts *opts = to_f_display_opts(item);
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

CONFIGFS_ATTR(f_display_opts_, rx_req_buf_size);

/* display resport */
static ssize_t f_display_opts_report_desc_show(struct config_item *item, char *page)
{
    struct f_display_opts *opts = to_f_display_opts(item);
    int result;

    mutex_lock(&opts->lock);
    result = opts->report_desc_length;
    memcpy(page, opts->report_desc, opts->report_desc_length);
    mutex_unlock(&opts->lock);

    return result;
}

static ssize_t f_display_opts_report_desc_store(struct config_item *item,
                        const char *page, size_t len)
{
    struct f_display_opts *opts = to_f_display_opts(item);
    int ret = -EBUSY;
    char *d;

    mutex_lock(&opts->lock);

    if (opts->refcnt)
        goto end;
    if (len > PAGE_SIZE) {
        ret = -ENOSPC;
        goto end;
    }
    d = kmemdup(page, len, GFP_KERNEL);
    if (!d) {
        ret = -ENOMEM;
        goto end;
    }

    kfree(opts->report_desc);
    opts->report_desc = d;
    opts->report_desc_length = len;
    opts->report_desc_alloc = true;
    ret = len;
end:
    mutex_unlock(&opts->lock);
    return ret;
}

CONFIGFS_ATTR(f_display_opts_, report_desc);


static ssize_t f_display_opts_edid_desc_show(struct config_item *item, char *page)
{
    struct f_display_opts *opts = to_f_display_opts(item);
    int result;

    mutex_lock(&opts->lock);
    result = opts->edid_desc_length;
    memcpy(page, opts->edid_desc, opts->edid_desc_length);
    mutex_unlock(&opts->lock);

    return result;
}

static ssize_t f_display_opts_edid_desc_store(struct config_item *item,
                        const char *page, size_t len)
{
    struct f_display_opts *opts = to_f_display_opts(item);
    int ret = -EBUSY;
    char *d;

    mutex_lock(&opts->lock);

    if (opts->refcnt)
        goto end;
    if (len > PAGE_SIZE) {
        ret = -ENOSPC;
        goto end;
    }
    d = kmemdup(page, len, GFP_KERNEL);
    if (!d) {
        ret = -ENOMEM;
        goto end;
    }
    kfree(opts->edid_desc);
    opts->edid_desc = d;
    opts->edid_desc_length = len;
    opts->edid_desc_alloc = true;
    ret = len;
end:
    mutex_unlock(&opts->lock);
    return ret;
}

CONFIGFS_ATTR(f_display_opts_, edid_desc);

static ssize_t f_display_opts_rotate_edid_desc_show(struct config_item *item, char *page)
{
    struct f_display_opts *opts = to_f_display_opts(item);
    int result;

    mutex_lock(&opts->lock);
    result = opts->rotate_edid_desc_length;
    memcpy(page, opts->rotate_edid_desc, opts->rotate_edid_desc_length);
    mutex_unlock(&opts->lock);

    return result;
}


static ssize_t f_display_opts_rotate_edid_desc_store(struct config_item *item,
                        const char *page, size_t len)
{
    struct f_display_opts *opts = to_f_display_opts(item);
    int ret = -EBUSY;
    char *d;

    mutex_lock(&opts->lock);

    if (opts->refcnt)
        goto end;
    if (len > PAGE_SIZE) {
        ret = -ENOSPC;
        goto end;
    }
    d = kmemdup(page, len, GFP_KERNEL);
    if (!d) {
        ret = -ENOMEM;
        goto end;
    }
    kfree(opts->rotate_edid_desc);
    opts->rotate_edid_desc = d;
    opts->rotate_edid_desc_length = len;
    opts->rotate_edid_desc_alloc = true;
    ret = len;
end:
    mutex_unlock(&opts->lock);
    return ret;
}

CONFIGFS_ATTR(f_display_opts_, rotate_edid_desc);

/* */

static struct configfs_attribute *display_attrs[] = {
    &f_display_opts_attr_rx_q_len,
    &f_display_opts_attr_rx_req_buf_size,
    &f_display_opts_attr_report_desc,
    &f_display_opts_attr_edid_desc,
    &f_display_opts_attr_rotate_edid_desc,
    NULL,
};

static const struct config_item_type display_func_type = {
    .ct_item_ops    = &display_item_ops,
    .ct_attrs       = display_attrs,
    .ct_owner       = THIS_MODULE,
};

static inline int gdisplay_get_minor(void)
{
    int ret;

    ret = ida_simple_get(&display_ida, 0, 0, GFP_KERNEL);
    if (ret >= DISPLAY_MINORS) {
        ida_simple_remove(&display_ida, ret);
        ret = -ENODEV;
    }

    return ret;
}

static inline void gdisplay_put_minor(int minor)
{
    ida_simple_remove(&display_ida, minor);
}

static int gdisplay_setup(int);
static void gdisplay_cleanup(void);

static void gdisplay_free_inst(struct usb_function_instance *f)
{
    struct f_display_opts *opts;

    opts = container_of(f, struct f_display_opts, func_inst);

    mutex_lock(&display_ida_lock);

    gdisplay_put_minor(opts->minor);
    if (ida_is_empty(&display_ida))
        gdisplay_cleanup();

    mutex_unlock(&display_ida_lock);

    if (opts->report_desc_alloc)
        kfree(opts->report_desc);

    if (opts->edid_desc_alloc)
        kfree(opts->edid_desc);

    if (opts->rotate_edid_desc_alloc)
        kfree(opts->rotate_edid_desc);

    kfree(opts);
}

static struct usb_function_instance *gdisplay_alloc_inst(void)
{
    struct f_display_opts *opts;
    struct usb_function_instance *ret;
    int status = 0;

    opts = kzalloc(sizeof(*opts), GFP_KERNEL);
    if (!opts)
        return ERR_PTR(-ENOMEM);

    mutex_init(&opts->lock);
    opts->func_inst.free_func_inst = gdisplay_free_inst;
    ret = &opts->func_inst;

    mutex_lock(&display_ida_lock);

    if (ida_is_empty(&display_ida)) {
        status = gdisplay_setup(DISPLAY_MINORS);
        if (status) {
            ret = ERR_PTR(status);
            kfree(opts);
            goto unlock;
        }
    }

    opts->minor = gdisplay_get_minor();
    if (opts->minor < 0) {
        ret = ERR_PTR(opts->minor);
        kfree(opts);
        if (ida_is_empty(&display_ida))
            gdisplay_cleanup();
        goto unlock;
    }
    config_group_init_type_name(&opts->func_inst.group, "",
                    &display_func_type);

unlock:
    mutex_unlock(&display_ida_lock);
    return ret;
}

static void gdisplay_free(struct usb_function *f)
{
    struct display_dev *dev = func_to_display(f);
    struct f_display_opts *opts;

    opts = container_of(f->fi, struct f_display_opts, func_inst);

    kref_put(&dev->kref, display_dev_free);
    mutex_lock(&opts->lock);
    --opts->refcnt;
    mutex_unlock(&opts->lock);
}

static void display_func_unbind(struct usb_configuration *c,
        struct usb_function *f)
{
    struct display_dev *dev;
    struct usb_request *req;

    dev = func_to_display(f);

    device_destroy(usb_gadget_class, MKDEV(major, dev->minor));

    /* Remove Character Device */
    cdev_del(&dev->display_cdev);

    /* we must already have been disconnected ... no i/o may be active */
    WARN_ON(!list_empty(&dev->rx_reqs_active));

    if (dev->current_rx_req != NULL)
        display_req_free(dev->out_ep, dev->current_rx_req);

    while (!list_empty(&dev->rx_reqs)) {
        req = container_of(dev->rx_reqs.next,
                struct usb_request, list);
        list_del(&req->list);
        display_req_free(dev->out_ep, req);
    }

    while (!list_empty(&dev->rx_buffers)) {
        req = container_of(dev->rx_buffers.next,
                struct usb_request, list);
        list_del(&req->list);
        display_req_free(dev->out_ep, req);
    }
    usb_free_all_descriptors(f);
}

static struct usb_function *gdisplay_alloc(struct usb_function_instance *fi)
{
    struct display_dev *dev;
    struct f_display_opts *opts;

    opts = container_of(fi, struct f_display_opts, func_inst);

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

    dev->rx_q_len = opts->rx_q_len;
    dev->rx_req_buf_size = opts->rx_req_buf_size;

    dev->report_desc = opts->report_desc;
    dev->report_length = opts->report_desc_length;

    dev->edid_desc = opts->edid_desc;
    dev->edid_length = opts->edid_desc_length;

    dev->rotate_edid_desc = opts->rotate_edid_desc;
    dev->rotate_edid_length = opts->rotate_edid_desc_length;

    mutex_unlock(&opts->lock);

    dev->function.name = "display";
    dev->function.bind = display_func_bind;
    dev->function.setup = display_func_setup;
    dev->function.unbind = display_func_unbind;
    dev->function.set_alt = display_func_set_alt;
    dev->function.disable = display_func_disable;
    dev->function.req_match = gdisplay_req_match;
    dev->function.free_func = gdisplay_free;

    INIT_LIST_HEAD(&dev->rx_reqs);
    INIT_LIST_HEAD(&dev->rx_buffers);
    INIT_LIST_HEAD(&dev->rx_reqs_active);

    spin_lock_init(&dev->lock);
    mutex_init(&dev->lock_display_io);
    init_waitqueue_head(&dev->rx_wait);

    dev->interface = -1;
    dev->display_cdev_open = 0;
    dev->display_status = 0;
    dev->current_rx_req = NULL;
    dev->current_rx_bytes = 0;
    dev->current_rx_buf = NULL;

    return &dev->function;
}

DECLARE_USB_FUNCTION_INIT(display, gdisplay_alloc_inst, gdisplay_alloc);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("INGENIC");

static int gdisplay_setup(int count)
{
    int status;
    dev_t devt;

    usb_gadget_class = class_create(THIS_MODULE, "usb_display_gadget");
    if (IS_ERR(usb_gadget_class)) {
        status = PTR_ERR(usb_gadget_class);
        usb_gadget_class = NULL;
        pr_err("unable to create usb_gadget class %d\n", status);
        return status;
    }

    status = alloc_chrdev_region(&devt, 0, count, "USB display gadget");
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

static void gdisplay_cleanup(void)
{
    if (major) {
        unregister_chrdev_region(MKDEV(major, 0), minors);
        major = minors = 0;
    }
    class_destroy(usb_gadget_class);
    usb_gadget_class = NULL;
}
