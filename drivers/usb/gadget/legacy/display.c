// SPDX-License-Identifier: GPL-2.0+
/*
 * display.c -- Display gadget driver
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <asm/byteorder.h>

#include <linux/usb/ch9.h>
#include <linux/usb/composite.h>
#include <linux/usb/gadget.h>

#include "u_display.h"

USB_GADGET_COMPOSITE_OPTIONS();

#define DRIVER_DESC                     "USB Display"

static const char shortname [] = "display";

static u8 default_edid_desc[128] = {
    // 1920 * 1080 * 30
    // 1280 * 800 * 30
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x31, 0xD8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x2A, 0x22, 0x01, 0x03, 0x80, 0x00, 0x00, 0x00, 0x08, 0x5E, 0xC4, 0xA4, 0x59, 0x4A, 0x98, 0x25,
    0x20, 0x50, 0x54, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x64, 0x19, 0x80, 0x40, 0x70, 0x38, 0x12, 0x40, 0x10, 0x10,
    0x35, 0x00, 0x4D, 0xD0, 0x10, 0x00, 0x00, 0x10, 0xE4, 0x0C, 0x00, 0x40, 0x50, 0x20, 0x12, 0x30,
    0x10, 0x10, 0x35, 0x00, 0x4D, 0xD0, 0x10, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xCE,
};

static u8 default_report_desc[] = {
    0x38, /* length */
    0x5F, /* descriptor type */
    0x01, 0x00, /* version 1.0 */
    0x36, /* length after type */
    0x01, 0x00, /* key min_width_height */
    0x04, /* key length */
    0xF0, 0x00, 0xF0, 0x00, /* key valule min_width 240 min_height 240 */
    0x02, 0x00, /* key max_width_height */
    0x04, /* key length */
    0xFF, 0x07, 0xFF, 0x07, /* key valule max_width 2047 max_height 2047 */
    0x03, 0x00, /* key max_transfer */
    0x04, /* key length */
    0xE0, 0xFF, 0x01, 0x00, /* key valule max_transfer 128*1024-32 */
    0x00, 0x01, /* key compression */
    0x04, /* key length */
    0x01, 0x00, 0x00, 0x40, /* key valule mjpeg compression_64 */
    0x00, 0x01, /* key compression */
    0x04, /* key length */
    0x02, 0x00, 0x00, 0x40, /* key valule h264 compression_64 */
    0x00, 0x02, /* key resolution */
    0x05, /* key length */
    0x80, 0x07, 0x38, 0x04, 0x1E,/* key valule width 1920 height 1080 sync 30 */
    0x00, 0x02, /* key resolution */
    0x05, /* key length */
    0x00, 0x05, 0x20, 0x03, 0x1E,/* key valule width 1280 height 800 sync 30 */
};

/*-------------------------------------------------------------------------*/

#define DISPLAY_VENDOR_NUM    0xa108
#define DISPLAY_PRODUCT_NUM   0xad10

/* Number of requests to allocate per endpoint, not used for ep0. */
static unsigned rx_qlen = 16;
module_param(rx_qlen, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(rx_qlen, "The number of each buffers to use per endpoint");

#define RX_QLEN rx_qlen

/* Number of requests to allocate per endpoint, not used for ep0. */
static unsigned rx_req_buf_size = 128*1024;
module_param(rx_req_buf_size, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(rx_req_buf_size, "The size of each rx buffers");

#define RX_REQ_BUF_SIZE rx_req_buf_size

static struct usb_function_instance *fi_display;
static struct usb_function *f_display;

/*-------------------------------------------------------------------------*/

/*
 * DESCRIPTORS ... most are static, but strings and (full) configuration
 * descriptors are built on demand.
 */

static struct usb_device_descriptor device_desc = {
    .bLength =        sizeof device_desc,
    .bDescriptorType =    USB_DT_DEVICE,
    /* .bcdUSB = DYNAMIC */
    .bDeviceClass =        0,
    .bDeviceSubClass =    0,
    .bDeviceProtocol =    0,
    .idVendor =        cpu_to_le16(DISPLAY_VENDOR_NUM),
    .idProduct =        cpu_to_le16(DISPLAY_PRODUCT_NUM),
    .bNumConfigurations =    1
};

/*-------------------------------------------------------------------------*/
/* static strings, in UTF-8 */
static struct usb_string strings [] = {
    [USB_GADGET_MANUFACTURER_IDX].s = "INGENIC",
    [USB_GADGET_PRODUCT_IDX].s = DRIVER_DESC,
    [USB_GADGET_SERIAL_IDX].s = "ad100",
    {  }        /* end of list */
};

static struct usb_gadget_strings stringtab_dev = {
    .language    = 0x0409,    /* en-us */
    .strings     = strings,
};

static struct usb_gadget_strings *dev_strings[] = {
    &stringtab_dev,
    NULL,
};

static struct usb_configuration display_cfg_driver = {
    .label            = DRIVER_DESC,
    .bConfigurationValue    = 1,
    .MaxPower = 500,
};

static int display_do_config(struct usb_configuration *c)
{
    struct usb_gadget *gadget = c->cdev->gadget;
    int status = 0;

    usb_ep_autoconfig_reset(gadget);

    f_display = usb_get_function(fi_display);
    if (IS_ERR(f_display))
        return PTR_ERR(f_display);

    status = usb_add_function(c, f_display);
    if (status < 0)
        usb_put_function(f_display);

    return status;
}

static int display_bind(struct usb_composite_dev *cdev)
{
    struct f_display_opts *display_opts;
    int ret;

    fi_display = usb_get_function_instance("display");
    if (IS_ERR(fi_display))
        return PTR_ERR(fi_display);

    display_opts = container_of(fi_display, struct f_display_opts, func_inst);
    display_opts->minor = 0;
    display_opts->rx_q_len = RX_QLEN;
    display_opts->rx_req_buf_size = RX_REQ_BUF_SIZE;

    /* use default desc */
    display_opts->edid_desc = default_edid_desc;
    display_opts->edid_desc_length = sizeof(default_edid_desc);
    display_opts->edid_desc_alloc = false;

    display_opts->rotate_edid_desc = default_edid_desc;
    display_opts->rotate_edid_desc_length = sizeof(default_edid_desc);
    display_opts->rotate_edid_desc_alloc = false;

    display_opts->report_desc = default_report_desc;
    display_opts->report_desc_length = sizeof(default_report_desc);
    display_opts->report_desc_alloc = false;

    ret = usb_string_ids_tab(cdev, strings);
    if (ret < 0)
        goto fail_put_func_inst;

    device_desc.iManufacturer = strings[USB_GADGET_MANUFACTURER_IDX].id;
    device_desc.iProduct = strings[USB_GADGET_PRODUCT_IDX].id;
    device_desc.iSerialNumber = strings[USB_GADGET_SERIAL_IDX].id;

    ret = usb_add_config(cdev, &display_cfg_driver, display_do_config);
    if (ret)
        goto fail_put_func_inst;

    usb_composite_overwrite_options(cdev, &coverwrite);
    return ret;

fail_put_func_inst:
    usb_put_function_instance(fi_display);
    return ret;
}

static int display_unbind(struct usb_composite_dev *cdev)
{
    usb_put_function(f_display);
    usb_put_function_instance(fi_display);

    return 0;
}

static struct usb_composite_driver display_driver = {
    .name           = shortname,
    .dev            = &device_desc,
    .strings        = dev_strings,
    .max_speed      = USB_SPEED_HIGH,
    .bind           = display_bind,
    .unbind         = display_unbind,
};

module_usb_composite_driver(display_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("INGENIC");
MODULE_LICENSE("GPL");
