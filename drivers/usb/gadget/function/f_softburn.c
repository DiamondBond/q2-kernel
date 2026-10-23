// SPDX-License-Identifier: GPL-2.0+
/*
 * f_softburn.c - USB SOFTBURN Command Function Driver
 *
 * This driver provides a vendor-specific USB function that handles
 * vendor-specific SOFTBURN programming requests.
 */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/usb/composite.h>
#include <linux/reboot.h>
#include <linux/configfs.h>
#include <linux/workqueue.h>

#include "configfs.h"

/* Vendor-specific SOFTBURN request code */
#define VENDOR_REQ_SOFTBURN		0x50
/* Calculate 'SOFTBURN' CRC using the kernel's crc16 function. */
#define SOFTBURN_HANDSHAKE_MAGIC	0xBB7D

struct f_softburn {
	s8	interface;
	struct	work_struct work;
	struct	usb_function function;
};

struct f_softburn_opts {
	struct usb_function_instance func_inst;
	char softburn_ext_compat_id[16];
	struct usb_os_desc softburn_os_desc;
};

static inline struct f_softburn *func_to_softburn(struct usb_function *f)
{
	return container_of(f, struct f_softburn, function);
}

static inline struct f_softburn_opts *to_f_softburn_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_softburn_opts,
			    func_inst.group);
}

static void softburn_work_handler(struct work_struct *work)
{
	*(volatile unsigned int *)0xb00000cc = 0x42575302;
	kernel_restart("softburn command received");
}

/*-------------------------------------------------------------------------*/

static struct usb_interface_descriptor softburn_intf = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,

	.bInterfaceNumber = 0,
	.bNumEndpoints = 0, /* No endpoints, only control transfers */
	.bInterfaceClass = USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass = 0,
	.bInterfaceProtocol = 0,
	/* .iInterface = DYNAMIC */
};

static struct usb_descriptor_header *softburn_descs[] = {
	(struct usb_descriptor_header *) &softburn_intf,
	NULL,
};

/* String descriptors */
static struct usb_string softburn_string_defs[] = {
	[0].s = "ingenic softburn command function",
	{  } /* end of list */
};

static struct usb_gadget_strings softburn_string_table = {
	.language = 0x0409, /* en-us */
	.strings = softburn_string_defs,
};

static struct usb_gadget_strings *softburn_strings[] = {
	&softburn_string_table,
	NULL,
};

/*-------------------------------------------------------------------------*/

static bool softburn_req_match(struct usb_function *f,
				       const struct usb_ctrlrequest *ctrl,
				       bool config0)
{
	struct f_softburn	*softburn = func_to_softburn(f);
	u16			w_index = le16_to_cpu(ctrl->wIndex);
	u16			w_value = le16_to_cpu(ctrl->wValue);

		printk("control req%02x.%02x v%04x i%04x l%u\n",
	     ctrl->bRequestType, ctrl->bRequest,
	     w_value, w_index, ctrl->wLength);
	if (config0)
		return false;

	if ((ctrl->bRequestType & USB_RECIP_MASK) != USB_RECIP_INTERFACE ||
	    (ctrl->bRequestType & USB_TYPE_MASK) != USB_TYPE_VENDOR)
		return false;

	switch (ctrl->bRequest) {
	case VENDOR_REQ_SOFTBURN:
		if (w_value == SOFTBURN_HANDSHAKE_MAGIC)
			break;
		return false;
	default:
		return false;
	}
	return w_index == softburn->interface;
}

static int softburn_setup(struct usb_function *f,
		const struct usb_ctrlrequest *ctrl)
{
	struct f_softburn *softburn = func_to_softburn(f);
	struct usb_configuration *c = f->config;
	struct usb_request *req	= c->cdev->req;
	int value		= -EOPNOTSUPP;
	u16 w_index		= le16_to_cpu(ctrl->wIndex);
	u16 w_value		= le16_to_cpu(ctrl->wValue);
	u16 w_length		= le16_to_cpu(ctrl->wLength);

	switch (ctrl->bRequestType&USB_TYPE_MASK) {
	case USB_TYPE_VENDOR:
		switch (ctrl->bRequest) {
		case VENDOR_REQ_SOFTBURN: {
			if ((w_index >> 8) != softburn->interface)
				break;
			if (w_value == SOFTBURN_HANDSHAKE_MAGIC) {

				req->length = 0;
				req->zero = 0;
				value = usb_ep_queue(c->cdev->gadget->ep0, req, GFP_ATOMIC);
				if (value < 0) {
					ERROR(c->cdev, "%s:%d Error!\n", __func__, __LINE__);
					req->status = 0;
					break;
				}

				INIT_WORK(&softburn->work, softburn_work_handler);
				schedule_work(&softburn->work);

				value = 0;
			}
			break;
		}
		default:
			goto unknown;
		}
		break;

	default:

unknown:
		VDBG(c->cdev,
		     "unknown control req%02x.%02x v%04x i%04x l%u\n",
		     ctrl->bRequestType, ctrl->bRequest,
		     w_value, w_index, w_length);
		break;

	}
	return value;
}

static int softburn_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct f_softburn_opts *softburn_opts;
	int status;

	status = usb_interface_id(c, f);
	if (status < 0)
		return status;
	softburn_intf.bInterfaceNumber = status;

	status = usb_string_ids_tab(cdev, softburn_string_defs);
	if (status < 0)
		return status;
	softburn_intf.iInterface = softburn_string_defs[0].id;

	if (cdev->use_os_string) {
		softburn_opts = container_of(f->fi, struct f_softburn_opts, func_inst);

		f->os_desc_table = kzalloc(sizeof(*f->os_desc_table), GFP_KERNEL);
		if (!f->os_desc_table)
			return -ENOMEM;

		f->os_desc_n = 1;
		f->os_desc_table[0].os_desc = &softburn_opts->softburn_os_desc;
		f->os_desc_table[0].if_id = softburn_intf.bInterfaceNumber;
	}

	status = usb_assign_descriptors(f, softburn_descs,
			softburn_descs, NULL, NULL);
	if (status)
		return status;

	return 0;
}

static void softburn_unbind(struct usb_configuration *c, struct usb_function *f)
{
	usb_free_all_descriptors(f);
	kfree(f->os_desc_table);
	f->os_desc_n = 0;
}

static int softburn_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	struct f_softburn *softburn = func_to_softburn(f);
	int ret = -ENOTSUPP;

	if (!alt) {
		softburn->interface = intf;
		ret = 0;
	}
	return ret;
}

static void softburn_disable(struct usb_function *f)
{
}

/*-------------------------------------------------------------------------*/

static void softburn_free_func(struct usb_function *f)
{
	struct f_softburn *softburn = func_to_softburn(f);
	kfree(softburn);
}

static struct usb_function *softburn_alloc_func(struct usb_function_instance *fi)
{
	struct f_softburn *softburn;

	softburn = kzalloc(sizeof(*softburn), GFP_KERNEL);
	if (!softburn)
		return ERR_PTR(-ENOMEM);

	softburn->function.name = "softburn";
	softburn->function.bind = softburn_bind;
	softburn->function.unbind = softburn_unbind;
	softburn->function.set_alt = softburn_set_alt;
	softburn->function.disable = softburn_disable;
	softburn->function.setup = softburn_setup;
	softburn->function.strings = softburn_strings;
	softburn->function.req_match = softburn_req_match;
	softburn->function.free_func = softburn_free_func;
	softburn->interface = -1;

	return &softburn->function;
}

static void softburn_free_instance(struct usb_function_instance *fi)
{
	struct f_softburn_opts *opts;

	opts = container_of(fi, struct f_softburn_opts, func_inst);
	kfree(opts);
}

/* ConfigFS support */
static struct configfs_item_operations softburn_item_ops = {
	.release	= NULL,
};

static struct configfs_attribute *softburn_attrs[] = {
	NULL,
};

static const struct config_item_type softburn_func_type = {
	.ct_item_ops    = &softburn_item_ops,
	.ct_attrs       = softburn_attrs,
	.ct_owner       = THIS_MODULE,
};

static struct usb_function_instance *softburn_alloc_instance(void)
{
	struct f_softburn_opts *opts;
	struct usb_os_desc *descs[1];
	char *names[1];
	struct config_group *softburn_interf_group;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);

	opts->softburn_os_desc.ext_compat_id = opts->softburn_ext_compat_id;
	INIT_LIST_HEAD(&opts->softburn_os_desc.ext_prop);

	opts->func_inst.free_func_inst = softburn_free_instance;

	descs[0] = &opts->softburn_os_desc;
	names[0] = "softburn";

	config_group_init_type_name(&opts->func_inst.group, "",
				    &softburn_func_type);

	softburn_interf_group = usb_os_desc_prepare_interf_dir(&opts->func_inst.group, 1, descs,
						       names, THIS_MODULE);
	if (IS_ERR(softburn_interf_group)) {
		softburn_free_instance(&opts->func_inst);
		return ERR_CAST(softburn_interf_group);
	}

	return &opts->func_inst;
}

DECLARE_USB_FUNCTION_INIT(softburn, softburn_alloc_instance, softburn_alloc_func);

MODULE_LICENSE("GPL");
