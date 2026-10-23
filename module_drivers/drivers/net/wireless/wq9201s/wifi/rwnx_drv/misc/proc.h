#ifndef _WQ_PROC_H
#define _WQ_PROC_H

#include <linux/version.h>
#include <linux/proc_fs.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 17, 0))
#define PDE_DATA(inode) pde_data(inode)
#endif

/* FIXME: use DEFINE_SHOW_ATTRIBUTE instead of WQ_PROC_OPS_RW */
#define WQ_PROC_OPS(_name) _name##_fops

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
#define _WQ_PROC_OPS_DEF(_name) const struct file_operations WQ_PROC_OPS(_name)

#define WQ_PROC_OPS_RW(_name)                                                  \
	WQ_PROC_OPS_DEF(_name) = {                                             \
		.owner = THIS_MODULE,                                          \
		.read = _name##_read,                                          \
		.write = _name##_write,                                        \
		.llseek = noop_llseek,                                         \
	}

#else

#define _WQ_PROC_OPS_DEF(_name) const struct proc_ops WQ_PROC_OPS(_name)
#define WQ_PROC_OPS_RW(_name)                                                  \
	_WQ_PROC_OPS_DEF(_name) = {                                            \
		.proc_read = _name##_read,                                     \
		.proc_write = _name##_write,                                   \
		.proc_lseek = noop_llseek,                                     \
	}

#endif

#endif
