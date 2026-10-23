/**
 ******************************************************************************
 *
 * @file rwnx_debugfs.h
 *
 * @brief Miscellaneous utility function definitions
 *
 * Copyright (C) RivieraWaves 2012-2020
 *
 ******************************************************************************
 */

#ifndef _RWNX_DEBUGFS_H_
#define _RWNX_DEBUGFS_H_

#include <linux/workqueue.h>
#include <linux/if_ether.h>
#include "rwnx_fw_trace.h"

struct rwnx_hw;
struct rwnx_sta;

#define DEBUGFS_ADD_FILE(name, parent, mode)                                   \
	do {                                                                   \
		struct dentry *__tmp;                                          \
		__tmp = debugfs_create_file(#name, mode, parent, rwnx_hw,      \
					    &rwnx_dbgfs_##name##_ops);         \
		if (IS_ERR_OR_NULL(__tmp))                                     \
			goto err;                                              \
	} while (0)

#define DEBUGFS_ADD_BOOL(name, parent, ptr)                                    \
	do {                                                                   \
		struct dentry *__tmp;                                          \
		__tmp = debugfs_create_bool(#name, S_IWUSR | S_IRUSR, parent,  \
					    ptr);                              \
		if (IS_ERR_OR_NULL(__tmp))                                     \
			goto err;                                              \
	} while (0)

#define DEBUGFS_ADD_X64(name, parent, ptr)                                     \
	do {                                                                   \
		debugfs_create_x64(#name, S_IWUSR | S_IRUSR, parent, ptr);     \
	} while (0)

#define DEBUGFS_ADD_U64(name, parent, ptr, mode)                               \
	do {                                                                   \
		debugfs_create_u64(#name, mode, parent, ptr);                  \
	} while (0)

#define DEBUGFS_ADD_X32(name, parent, ptr)                                     \
	do {                                                                   \
		debugfs_create_x32(#name, S_IWUSR | S_IRUSR, parent, ptr);     \
	} while (0)

#define DEBUGFS_ADD_U32(name, parent, ptr, mode)                               \
	do {                                                                   \
		debugfs_create_u32(#name, mode, parent, ptr);                  \
	} while (0)

/* file operation */
#define DEBUGFS_READ_FUNC(name)                                                \
	static ssize_t rwnx_dbgfs_##name##_read(struct file *file,             \
						char __user *user_buf,         \
						size_t count, loff_t *ppos);

#define DEBUGFS_WRITE_FUNC(name)                                               \
	static ssize_t rwnx_dbgfs_##name##_write(struct file *file,            \
						 const char __user *user_buf,  \
						 size_t count, loff_t *ppos);

#define DEBUGFS_OPEN_FUNC(name)                                                \
	static int rwnx_dbgfs_##name##_open(struct inode *inode,               \
					    struct file *file);

#define DEBUGFS_RELEASE_FUNC(name)                                             \
	static int rwnx_dbgfs_##name##_release(struct inode *inode,            \
					       struct file *file);

#define DEBUGFS_READ_FILE_OPS(name)                                            \
	DEBUGFS_READ_FUNC(name);                                               \
	static const struct file_operations rwnx_dbgfs_##name##_ops = {        \
		.read = rwnx_dbgfs_##name##_read,                              \
		.open = simple_open,                                           \
		.llseek = generic_file_llseek,                                 \
	};

#define DEBUGFS_WRITE_FILE_OPS(name)                                           \
	DEBUGFS_WRITE_FUNC(name);                                              \
	static const struct file_operations rwnx_dbgfs_##name##_ops = {        \
		.write = rwnx_dbgfs_##name##_write,                            \
		.open = simple_open,                                           \
		.llseek = generic_file_llseek,                                 \
	};

#define DEBUGFS_READ_WRITE_FILE_OPS(name)                                      \
	DEBUGFS_READ_FUNC(name);                                               \
	DEBUGFS_WRITE_FUNC(name);                                              \
	static const struct file_operations rwnx_dbgfs_##name##_ops = {        \
		.write = rwnx_dbgfs_##name##_write,                            \
		.read = rwnx_dbgfs_##name##_read,                              \
		.open = simple_open,                                           \
		.llseek = generic_file_llseek,                                 \
	};

#define DEBUGFS_READ_OPEN_RELEASE_FILE_OPS(name)                               \
	DEBUGFS_READ_FUNC(name);                                               \
	DEBUGFS_OPEN_FUNC(name);                                               \
	DEBUGFS_RELEASE_FUNC(name);                                            \
	static const struct file_operations rwnx_dbgfs_##name##_ops = {        \
		.read = rwnx_dbgfs_##name##_read,                              \
		.open = rwnx_dbgfs_##name##_open,                              \
		.release = rwnx_dbgfs_##name##_release,                        \
		.llseek = generic_file_llseek,                                 \
	};

#define DEBUGFS_READ_WRITE_OPEN_RELEASE_FILE_OPS(name)                         \
	DEBUGFS_READ_FUNC(name);                                               \
	DEBUGFS_WRITE_FUNC(name);                                              \
	DEBUGFS_OPEN_FUNC(name);                                               \
	DEBUGFS_RELEASE_FUNC(name);                                            \
	static const struct file_operations rwnx_dbgfs_##name##_ops = {        \
		.write = rwnx_dbgfs_##name##_write,                            \
		.read = rwnx_dbgfs_##name##_read,                              \
		.open = rwnx_dbgfs_##name##_open,                              \
		.release = rwnx_dbgfs_##name##_release,                        \
		.llseek = generic_file_llseek,                                 \
	};

#ifdef CONFIG_RWNX_DEBUGFS

struct rwnx_debugfs {
	unsigned long long rateidx;
	struct dentry *dir;
	struct dentry *dir_stas;
	bool trace_prst;

	char helper_cmd[64];
	struct work_struct helper_work;
	bool helper_scheduled;
	spinlock_t umh_lock;
	bool unregistering;

	struct rwnx_fw_trace fw_trace;

	struct work_struct sta_work;
	struct dentry **dir_sta;
	uint8_t sta_idx;
	struct dentry *dir_rc;
	struct dentry **dir_rc_sta;
	int *rc_config;
	struct list_head rc_config_save;
	struct dentry *dir_twt;
	struct dentry **dir_twt_sta;
	uint8_t txq_num;
	uint8_t mac_id;
};

struct rwnx_sta_ws {
	struct work_struct sta_work;
	struct rwnx_debugfs *debugfs;
	uint8_t sta_idx;
};

// Max duration in msecs to save rate config for a sta after disconnection
#define RC_CONFIG_DUR 600000

struct rwnx_rc_config_save {
	struct list_head list;
	unsigned long timestamp;
	int rate;
	u8 mac_addr[ETH_ALEN];
};

int rwnx_dbgfs_register(struct rwnx_hw *rwnx_hw, const char *name);
void rwnx_dbgfs_unregister(struct rwnx_hw *rwnx_hw);
int rwnx_um_helper(struct rwnx_debugfs *rwnx_debugfs, const char *cmd);
int rwnx_trigger_um_helper(struct rwnx_debugfs *rwnx_debugfs);
void rwnx_wait_um_helper(struct rwnx_hw *rwnx_hw);
void rwnx_dbgfs_register_sta(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta);
void rwnx_dbgfs_unregister_sta(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta);

int rwnx_dbgfs_register_fw_dump(struct rwnx_hw *rwnx_hw, struct dentry *dir_drv,
				struct dentry *dir_diags);
void rwnx_dbgfs_trigger_fw_dump(struct rwnx_hw *rwnx_hw, char *reason);

void rwnx_fw_trace_dump(struct rwnx_hw *rwnx_hw);
void rwnx_fw_trace_reset(struct rwnx_hw *rwnx_hw);

void rwnx_dbgfs_flush_sta_work(struct rwnx_hw *rwnx_hw);
void rwnx_debugfs_deinit(struct rwnx_debugfs *rwnx_debugfs);
int print_rate_from_cfg(char *buf, int size, u32 rate_config, int *r_idx,
	int ru_size);
#else

struct rwnx_debugfs {
};

static inline int rwnx_dbgfs_register(struct rwnx_hw *rwnx_hw, const char *name)
{
	return 0;
}
static inline void rwnx_dbgfs_unregister(struct rwnx_hw *rwnx_hw)
{
}
static inline int rwnx_um_helper(struct rwnx_debugfs *rwnx_debugfs,
				 const char *cmd)
{
	return 0;
}
static inline int rwnx_trigger_um_helper(struct rwnx_debugfs *rwnx_debugfs)
{
	return 0;
}
static inline void rwnx_wait_um_helper(struct rwnx_hw *rwnx_hw)
{
}
static inline void rwnx_dbgfs_register_sta(struct rwnx_hw *rwnx_hw,
					   struct rwnx_sta *sta)
{
}
static inline void rwnx_dbgfs_unregister_sta(struct rwnx_hw *rwnx_hw,
					     struct rwnx_sta *sta)
{
}

static inline void rwnx_fw_trace_dump(struct rwnx_hw *rwnx_hw)
{
}

static inline void rwnx_fw_trace_reset(struct rwnx_hw *rwnx_hw)
{
}

static inline int print_rate_from_cfg(char *buf, int size, u32 rate_config, int *r_idx,
	int ru_size)
{
	return 0;
}

static inline void rwnx_dbgfs_flush_sta_work(struct rwnx_hw *rwnx_hw)
{

}

static inline void rwnx_debugfs_deinit(struct rwnx_debugfs *rwnx_debugfs)
{

}

#endif /* CONFIG_RWNX_DEBUGFS */

#endif /* _RWNX_DEBUGFS_H_ */
