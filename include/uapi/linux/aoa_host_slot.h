/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_AOA_HOST_SLOT_H
#define _UAPI_LINUX_AOA_HOST_SLOT_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define AOA_HOST_SLOT_VERSION              1U
#define AOA_HOST_SLOT_MMAP_SLOT_BASE       0x10000000ULL

#define AOA_HOST_SLOT_FLAG_MMAP_PAYLOAD    (1U << 0)
#define AOA_HOST_SLOT_FLAG_WRITE_BULK_OUT  (1U << 1)

struct aoa_host_slot_caps {
	__u32 version;
	__u32 flags;
	__u32 min_slot_size;
	__u32 max_slot_size;
	__u32 min_slot_count;
	__u32 max_slot_count;
	__u32 reserved[10];
};

struct aoa_host_slot_reqbufs {
	__u32 slot_count;
	__u32 slot_size;
	__u32 flags;
	__u32 reserved[13];
};

struct aoa_host_slot_buffer {
	__u32 index;
	__u32 length;
	__u32 mmap_length;
	__u32 data_offset;
	__u64 mmap_offset;
	__u32 reserved[10];
};

struct aoa_host_slot_desc {
	__u32 index;
	__u32 bytesused;
	__s32 status;
	__u32 flags;
	__u64 sequence;
	__u64 timestamp_ns;
	__u32 reserved[8];
};

struct aoa_host_slot_stats {
	__u32 version;
	__u32 slot_size;
	__u32 slot_alloc_len;
	__u32 slot_count;
	__u32 submitted_slots;
	__u32 queued_slots;
	__u32 done_slots;
	__u32 user_slots;
	__u32 cache_alignment;
	__u32 page_size;
	__u32 mmap_count;
	__u32 reserved0;
	__u64 submitted_urbs;
	__u64 completed_urbs;
	__u64 completed_bytes;
	__u64 dqbuf_calls;
	__u64 qbuf_calls;
	__u64 dqbuf_waits;
	__u64 zero_length;
	__u64 errors;
	__u64 write_calls;
	__u64 write_bytes;
	__u64 max_done_slots;
	__u64 max_user_slots;
	__u64 max_submit_gap_us;
	__u64 max_complete_to_resubmit_us;
	__u64 max_completion_interval_us;
	__s32 last_error;
	__u32 reserved[19];
};

#define AOA_HOST_SLOT_IOC_MAGIC       'D'
#define AOA_HOST_SLOT_IOC_QUERY_CAPS  _IOR(AOA_HOST_SLOT_IOC_MAGIC, 0x00, struct aoa_host_slot_caps)
#define AOA_HOST_SLOT_IOC_REQBUFS     _IOWR(AOA_HOST_SLOT_IOC_MAGIC, 0x01, struct aoa_host_slot_reqbufs)
#define AOA_HOST_SLOT_IOC_QUERYBUF    _IOWR(AOA_HOST_SLOT_IOC_MAGIC, 0x02, struct aoa_host_slot_buffer)
#define AOA_HOST_SLOT_IOC_STREAMON    _IO(AOA_HOST_SLOT_IOC_MAGIC, 0x03)
#define AOA_HOST_SLOT_IOC_STREAMOFF   _IO(AOA_HOST_SLOT_IOC_MAGIC, 0x04)
#define AOA_HOST_SLOT_IOC_DQBUF       _IOWR(AOA_HOST_SLOT_IOC_MAGIC, 0x05, struct aoa_host_slot_desc)
#define AOA_HOST_SLOT_IOC_QBUF        _IOWR(AOA_HOST_SLOT_IOC_MAGIC, 0x06, struct aoa_host_slot_desc)
#define AOA_HOST_SLOT_IOC_GET_STATS   _IOR(AOA_HOST_SLOT_IOC_MAGIC, 0x07, struct aoa_host_slot_stats)

#endif /* _UAPI_LINUX_AOA_HOST_SLOT_H */
