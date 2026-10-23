#include "wq_pktlog.h"
#include "wq_log.h"
#include "wq_wifi_dbg.h"
#include "fw_api/wifi/mac/wq_pktdump.h"

#include <linux/version.h>
#include <linux/tty_flip.h>
#include <linux/time.h>

#define WQ_PKT_LOG_CMD_ROOM(pktlog)                                            \
	(WQ_PKT_LOG_CMD_SIZE - (pktlog)->cmd.pos - 1)

static int wq_pktlog_tty_install(struct tty_driver *driver,
				 struct tty_struct *tty)
{
	struct pktlog *pktlog = (struct pktlog *)driver->driver_state;
	int ret;

	ret = tty_standard_install(driver, tty);
	if (ret) {
		WQ_DBG(DM_PKTDUMP, DL_ERR, "%s tty install fail!\n", __func__);
	} else {
		tty->driver_data = pktlog;
	}

	return ret;
}

static int wq_pktlog_tty_open(struct tty_struct *tty, struct file *filp)
{
	struct pktlog *pktlog = (struct pktlog *)tty->driver_data;

	down(&pktlog->sem);

	pktlog->open_count++;
	if (pktlog->open_count == 1) {
		/* FIXME: send command to firmware to enable pkt log */
	}

	up(&pktlog->sem);
	return 0;
}

static void wq_pktlog_tty_close(struct tty_struct *tty, struct file *filp)
{
	struct pktlog *pktlog = (struct pktlog *)tty->driver_data;

	down(&pktlog->sem);
	pktlog->open_count--;
	if (pktlog->open_count == 0) {
		/* FIXME: send command to firmware to disable pkt log */
	}
	up(&pktlog->sem);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
static ssize_t wq_pktlog_tty_write(struct tty_struct *tty, const unsigned char *buf, size_t count)
#else
static int wq_pktlog_tty_write(struct tty_struct *tty, const unsigned char *buf,
			       int count)
#endif
{
	struct pktlog *pktlog = (struct pktlog *)tty->driver_data;
	int room;
	char *crlf;

	/*
	 * FIXME: directly setup pkt log filter by the following similar way
	 *	echo "no beacon" > /dev/ttyPkt
	 */
	down(&pktlog->sem);
	room = WQ_PKT_LOG_CMD_ROOM(pktlog);
	if (count > room) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
                WQ_DBG(DM_PKTDUMP, DL_ERR, "%s: overflow %lu, room %d\n",
#else
		WQ_DBG(DM_PKTDUMP, DL_ERR, "%s: overflow %d, room %d\n",
#endif
		       __func__, count, room);
		count = room;
	}
	memcpy(pktlog->cmd.buf + pktlog->cmd.pos, buf, count);
	pktlog->cmd.pos += count;
	pktlog->cmd.buf[pktlog->cmd.pos] = '\0';

	crlf = strpbrk(pktlog->cmd.buf, "\r\n");
	if (crlf) {
		*crlf = '\0';
		WQ_DBG(DM_PKTDUMP, DL_WRN, "%s: %s\n", __func__,
		       pktlog->cmd.buf);
		pktlog->cmd.pos = 0;
	}
	up(&pktlog->sem);

	return count;
}

#if LINUX_VERSION_CODE <= KERNEL_VERSION(5, 11, 0)
static int wq_pktlog_tty_write_room(struct tty_struct *tty)
#else
static unsigned int wq_pktlog_tty_write_room(struct tty_struct *tty)
#endif
{
	struct pktlog *pktlog = (struct pktlog *)tty->driver_data;
	int room = -EINVAL;

	if (!pktlog)
		return -ENODEV;

	down(&pktlog->sem);
	if (pktlog->open_count) {
		/* calculate how much room is left for pkt log command (filter) */
		room = WQ_PKT_LOG_CMD_ROOM(pktlog);
	}
	up(&pktlog->sem);
	return room;
}

static void wq_pktlog_tty_set_termios(struct tty_struct *tty,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
				      const struct ktermios *old)
#else
 				      struct ktermios *old)
#endif
{
}

static int wq_pktlog_tty_tiocmget(struct tty_struct *tty)
{
	return 0;
}

static int wq_pktlog_tty_tiocmset(struct tty_struct *tty, unsigned int set,
				  unsigned int clear)
{
	return 0;
}

static int wq_pktlog_tty_ioctl(struct tty_struct *tty, unsigned int cmd,
			       unsigned long arg)
{
	return -ENOIOCTLCMD;
}

static const struct tty_operations wq_pktlog_tty_ops = {
	.install = wq_pktlog_tty_install,
	.open = wq_pktlog_tty_open,
	.close = wq_pktlog_tty_close,
	.write = wq_pktlog_tty_write,
	.write_room = wq_pktlog_tty_write_room,
	.set_termios = wq_pktlog_tty_set_termios,
	.tiocmget = wq_pktlog_tty_tiocmget,
	.tiocmset = wq_pktlog_tty_tiocmset,
	.ioctl = wq_pktlog_tty_ioctl,
};

int wq_pktlog_init(struct pktlog *pktlog)
{
	struct device *dev;
	int ret = 0;

	sema_init(&pktlog->sem, 1);
	pktlog->open_count = 0;
	pktlog->tty.driver =
		tty_alloc_driver(1, TTY_DRIVER_REAL_RAW |
					    TTY_DRIVER_DYNAMIC_DEV |
					    TTY_DRIVER_UNNUMBERED_NODE);
	if (IS_ERR(pktlog->tty.driver)) {
		ret = PTR_ERR(pktlog->tty.driver);
		WQ_DBG(DM_PKTDUMP, DL_ERR, "%s: fail to alloc tty driver(%d)\n",
		       __func__, ret);
		goto fail;
	}

	pktlog->tty.driver->driver_name = "wq_ttyPkt_drv";
	pktlog->tty.driver->name = "ttyPkt";
	pktlog->tty.driver->type = TTY_DRIVER_TYPE_SERIAL;
	pktlog->tty.driver->subtype = SERIAL_TYPE_NORMAL;

	pktlog->tty.driver->init_termios = tty_std_termios;
	pktlog->tty.driver->init_termios.c_iflag = 0;
	pktlog->tty.driver->init_termios.c_oflag = OPOST;
	pktlog->tty.driver->init_termios.c_cflag = B4000000 | CS8 | CREAD;
	pktlog->tty.driver->init_termios.c_lflag = 0;
	pktlog->tty.driver->init_termios.c_ispeed = 4000000;
	pktlog->tty.driver->init_termios.c_ospeed = 4000000;

	pktlog->tty.driver->driver_state = pktlog;
	tty_set_operations(pktlog->tty.driver, &wq_pktlog_tty_ops);

	ret = tty_register_driver(pktlog->tty.driver);
	if (ret) {
		WQ_DBG(DM_PKTDUMP, DL_ERR,
		       "%s: fail to register tty driver(%d)\n", __func__, ret);
		goto release_driver;
	}

	tty_port_init(&pktlog->tty.port);
	tty_port_link_device(&pktlog->tty.port, pktlog->tty.driver, 0);

	dev = tty_register_device(pktlog->tty.driver, 0, NULL);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		WQ_DBG(DM_PKTDUMP, DL_ERR,
		       "%s: fail to register tty device(%d)\n", __func__, ret);
		goto unregister_driver;
	}
	return 0;

unregister_driver:
	tty_port_destroy(&pktlog->tty.port);
	tty_unregister_driver(pktlog->tty.driver);
release_driver:
	tty_driver_kref_put(pktlog->tty.driver);
fail:
	pktlog->tty.driver = NULL;
	return ret;
}

void wq_pktlog_deinit(struct pktlog *pktlog)
{
	if (!pktlog->tty.driver)
		return;

	tty_unregister_device(pktlog->tty.driver, 0);
	tty_port_destroy(&pktlog->tty.port);
	tty_unregister_driver(pktlog->tty.driver);
	tty_driver_kref_put(pktlog->tty.driver);
	pktlog->tty.driver = NULL;
}

int wq_pktlog_save(struct pktlog *pktlog, void *pkt, int size)
{
	int copied = 0;
#ifndef PHY_ADC_DUMP
	struct tm tm_now;
	WIFI_DBG_PKTDUMP *dpkt = pkt;
	uint8_t frame_ctrl = dpkt->pkt_data[0];
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
	time64_to_tm(ktime_get_real_seconds(), 0, &tm_now);
#else
	time64_to_tm(get_seconds(), 0, &tm_now);
#endif
#else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
	time_to_tm(ktime_get_real_seconds(), 0, &tm_now);
#endif
	time_to_tm(get_seconds(), 0, &tm_now);
#endif
	dpkt->driver_ts_hour = tm_now.tm_hour;
	dpkt->driver_ts_min = tm_now.tm_min;
	dpkt->driver_ts_sec = tm_now.tm_sec;

	if (!pktlog || !pktlog->tty.driver)
		return 0;

	//dump connection process
	if ((frame_ctrl & MAC_FATCL_MGT_ST) == 0) {
		if (frame_ctrl >> 4 == MAC_FATCL_AUTH_ST ||
		    frame_ctrl >> 4 == MAC_FATCL_ASSOCREQ_ST ||
		    frame_ctrl >> 4 == MAC_FATCL_ASSOCRSP_ST ||
		    frame_ctrl >> 4 == MAC_FATCL_REASSOCREQ_ST ||
		    frame_ctrl >> 4 == MAC_FATCL_REASSOCRSP_ST) {
			wq_packet_dump_evt_handler(dpkt);
		}
	}
#endif

	down(&pktlog->sem);
	if (pktlog->open_count) {
		copied = tty_insert_flip_string(&pktlog->tty.port, pkt, size);
		if (copied)
			tty_flip_buffer_push(&pktlog->tty.port);

		if (copied != size)
			WQ_DBG(DM_PKTDUMP, DL_ERR, "%s: %d < %u\n", __func__,
			       copied, size);
	}
	up(&pktlog->sem);

	return copied;
}
