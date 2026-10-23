#include "proc.h"
#include "wq_log.h"
#include "bmi_core.h"
#include "wq_fw.h"
#include "bmi_core.h"

#define PROC_DIR "driver/wifi_mp"

#define IOCTL_MAGIC 'e'
#define PROC_MP_TEST_CMD _IOWR(IOCTL_MAGIC, 0, mp_test_cmd_rsp_t)

#define MP_TEST_DATA_LEN 500
#define MP_TEST_API_DATA 0x02

#define MP_TEST_CMD_TIMEOUT_MS (10 * 1000)

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0))
#define wq_proc_ops file_operations
#else
#define wq_proc_ops proc_ops
#endif /* #if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)*/

typedef struct __attribute__((packed)) {
	uint8_t type;
	uint8_t total_len;
	uint8_t resv1;
	uint8_t resv2;
	uint16_t data_len;
	uint8_t data[MP_TEST_DATA_LEN];
} mp_test_cmd_rsp_t;

static mp_test_cmd_rsp_t pt_cmd;
static mp_test_cmd_rsp_t pt_rsp;

static int mp_test_proc_show(struct seq_file *seq, void *v)
{
	unsigned int *ptr_var = seq->private;
	seq_printf(seq, "%u\n", *ptr_var);
	return 0;
}

static int mp_test_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, mp_test_proc_show, PDE_DATA(inode));
}

static long mp_test_proc_ioctl(struct file *file, unsigned int cmd,
			       unsigned long param)
{
	long ret = 0;
	struct inode *inode = file_inode(file);
	struct wq_core *core = (struct wq_core *)PDE_DATA(inode);

	switch (cmd) {
	case PROC_MP_TEST_CMD: {
		uint8_t *usr_buf = (uint8_t *)param;

		memset(&pt_rsp, 0x00, sizeof(pt_rsp));
		if (copy_from_user(&pt_cmd, usr_buf, sizeof(pt_cmd))) {
			WQ_DBG(DM_GENERIC, DL_ERR, "copy_from_user failed!\n");
			ret = -1;
			break;
		}

		if ((pt_cmd.total_len >= sizeof(pt_cmd)) ||
		    pt_cmd.data_len >= sizeof(pt_cmd.data)) {
			WQ_DBG(DM_GENERIC, DL_ERR,
			       "invalid cmd:(%u > %zu) || (%u > %zu)\n",
			       pt_cmd.total_len, sizeof(pt_cmd),
			       pt_cmd.data_len, sizeof(pt_cmd.data));
			ret = -1;
			break;
		}

		if (bmi_cmd_exchange(core, WQ_VREQ_ID_EXE_MP_TEST_CMD, &pt_cmd,
				     pt_cmd.total_len, &pt_rsp, sizeof(pt_rsp),
				     MP_TEST_CMD_TIMEOUT_MS) < 0) {
			WQ_DBG(DM_GENERIC, DL_ERR,
			       "%s: execute command failed\n", __func__);
			ret = -1;
			break;
		} else {
			WQ_DBG(DM_GENERIC, DL_INF,
			       "%s: execute command success\n", __func__);
			if (copy_to_user(usr_buf, &pt_rsp, sizeof(pt_rsp))) {
				WQ_DBG(DM_GENERIC, DL_ERR,
				       "copy_from_user failed!\n");
				ret = -1;
				break;
			}
		}
		break;
	}
	default:
		WQ_DBG(DM_GENERIC, DL_ERR, "%s: unknown command!\n", __func__);
		break;
	}

	return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct wq_proc_ops proc_mp_test_fops = {
	.owner = THIS_MODULE,
	.open = mp_test_proc_open,
	.unlocked_ioctl = mp_test_proc_ioctl,
	.compat_ioctl = mp_test_proc_ioctl,
	.llseek = noop_llseek,
	.release = single_release,
};
#else
static const struct wq_proc_ops proc_mp_test_fops = {
	.proc_open = mp_test_proc_open,
	.proc_ioctl = mp_test_proc_ioctl,
	.proc_lseek = noop_llseek,
	.proc_release = single_release,
#ifdef CONFIG_COMPAT
	.proc_compat_ioctl = mp_test_proc_ioctl,
#endif
};
#endif

void wq_mp_test_proc_init(struct wq_core *core)
{
	proc_mkdir(PROC_DIR, NULL);
	proc_create_data(PROC_DIR "/mp_test", 0666, NULL, &proc_mp_test_fops,
			 core);
	core->flags.mp_test_proc_init = 1;
}
EXPORT_SYMBOL(wq_mp_test_proc_init);

void wq_mp_test_proc_deinit(struct wq_core *core)
{
	if (core->flags.mp_test_proc_init == 1) {
		remove_proc_entry(PROC_DIR "/mp_test", NULL);
		remove_proc_entry(PROC_DIR, NULL);
	}
}
EXPORT_SYMBOL(wq_mp_test_proc_deinit);
