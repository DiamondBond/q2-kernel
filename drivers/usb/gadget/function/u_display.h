/* SPDX-License-Identifier: GPL-2.0 */
/*
 * u_display.h
 *
 * Utility definitions for the display function
 *
 */

#ifndef U_DISPLAY_H
#define U_DISPLAY_H

#include <linux/usb/composite.h>

struct f_display_opts {
    struct usb_function_instance func_inst;
    int minor;

    unsigned rx_q_len;
    unsigned int rx_req_buf_size;

    unsigned short report_desc_length;
    unsigned char *report_desc;
    bool report_desc_alloc;

    unsigned short edid_desc_length;
    unsigned char *edid_desc;
    bool edid_desc_alloc;

    unsigned short rotate_edid_desc_length;
    unsigned char *rotate_edid_desc;
    bool rotate_edid_desc_alloc;

    /*
     * Protect the data from concurrent access by read/write
     * and create symlink/remove symlink
     */
    struct mutex lock;
    int refcnt;
};

#endif /* U_DISPLAY_H */
