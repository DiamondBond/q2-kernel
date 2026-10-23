#ifndef WQ_PKTLOG_H
#define WQ_PKTLOG_H

#include <linux/tty.h>
#include <linux/tty_driver.h>

#define WQ_PKT_LOG_CMD_SIZE 80

struct pktlog {
	struct semaphore sem;
	int open_count;

	struct {
		struct tty_driver *driver;
		struct tty_port port;
	} tty;

	/* used to accept filter like "no beacon" ... */
	struct {
		int pos;
		char buf[WQ_PKT_LOG_CMD_SIZE];
	} cmd;
};

#define MAC_FATCL_MGT_ST 0xc
#define MAC_FATCL_AUTH_ST 0xb
#define MAC_FATCL_ASSOCREQ_ST 0x0
#define MAC_FATCL_ASSOCRSP_ST 0x1
#define MAC_FATCL_REASSOCREQ_ST 0x2
#define MAC_FATCL_REASSOCRSP_ST 0x3

int wq_pktlog_init(struct pktlog *pktlog);
void wq_pktlog_deinit(struct pktlog *pktlog);

int wq_pktlog_save(struct pktlog *pktlog, void *pkt, int size);

#endif
