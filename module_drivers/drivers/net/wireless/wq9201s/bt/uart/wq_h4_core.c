/** @file wq_uart_h4.c
 *
 *  @brief Bluetooth HCI H4 UART driver
 *  related functions.
 *
 * Copyright (C) 2016-2025, WuQi Ltd.
 *
 * This software file (the "File") is distributed by WuQi Ltd.
 * Under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/serdev.h>
#include <linux/skbuff.h>
#include <linux/of.h>
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "include/hci_uart.h"

#define DRIVER_NAME "wq_uart_h4"
#define MAX_QUEUED_PACKETS 10

static int hci_uart_open(struct hci_dev *hdev)
{
	bt_dev_info(hdev, "HCI UART open");
	return 0;
}

static int hci_uart_close(struct hci_dev *hdev)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);
	// struct serdev_device *serdev = hu->serdev;
	bt_dev_info(hdev, "HCI UART close");
	kfree_skb(hu->rx_skb);
	return 0;
}

static int hci_uart_flush(struct hci_dev *hdev)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);
	struct serdev_device *serdev = hu->serdev;

	/* Flush any pending data */
	serdev_device_wait_until_sent(serdev, 0);
	skb_queue_purge(&hu->txq);

	return 0;
}

static int hci_uart_setup(struct hci_dev *hdev)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);

	bt_dev_info(hdev, "HCI UART setup");

	/* Perform any necessary setup for the Bluetooth device */
	set_bit(HCI_UART_PROTO_SET, &hu->hdev_flags);

	return 0;
}

static void wq_uart_tx_wakeup(struct serdev_device *serdev)
{
	// if (test_and_set_bit(BTMTKUART_TX_STATE_ACTIVE, &bdev->tx_state))
	// 	set_bit(BTMTKUART_TX_STATE_WAKEUP, &bdev->tx_state);
	struct hci_uart *hu = serdev_device_get_drvdata(serdev);
	schedule_work(&hu->write_work);
}

static int hci_uart_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_uart *hu = hci_get_drvdata(hdev);
	bt_dev_info(hdev, "hci_uart_send_frame type:%x, len:%u",
		    hci_skb_pkt_type(skb), skb->len);

	/* Prepend packet type */
	memcpy(skb_push(skb, 1), &hci_skb_pkt_type(skb), 1);

	/* Queue the packet for transmission */
	skb_queue_tail(&hu->txq, skb);

	/* Try to send immediately */
	wq_uart_tx_wakeup(hu->serdev);

	return 0;
}

static const struct h4_recv_pkt wq_recv_pkts[] = {
	{ H4_RECV_ACL, .recv = hci_recv_frame },
	{ H4_RECV_SCO, .recv = hci_recv_frame },
	{ H4_RECV_EVENT, .recv = hci_recv_frame },
};

static int wq_recv(struct serdev_device *serdev, const unsigned char *data,
		   size_t count)
{
	struct hci_uart *hu = serdev_device_get_drvdata(serdev);
	bt_dev_info(hu->hdev, "wq_recv");
	if (!test_bit(HCI_UART_PROTO_SET, &hu->hdev_flags)) {
		return 0;
	}

	hu->rx_skb = h4_recv_buf(hu->hdev, hu->rx_skb, data, count,
				 wq_recv_pkts, ARRAY_SIZE(wq_recv_pkts));
	if (IS_ERR(hu->rx_skb)) {
		int err = PTR_ERR(hu->rx_skb);
		bt_dev_err(hu->hdev, "Frame reassembly failed (%d)", err);
		hu->rx_skb = NULL;
		return err;
	}

	return count;
}

static void wq_uart_tx_work(struct work_struct *work)
{
	struct hci_uart *hu = container_of(work, struct hci_uart, write_work);
	struct serdev_device *serdev = hu->serdev;
	struct hci_dev *hdev = hu->hdev;

	while (1) {
		// clear_bit(BTMTKUART_TX_STATE_WAKEUP, &bdev->tx_state);

		while (1) {
			struct sk_buff *skb = skb_dequeue(&hu->txq);
			int len;

			if (!skb)
				break;
			bt_dev_info(hdev, "0x%x tobe send",
				    *(uint32_t *)(skb->data));
			len = serdev_device_write_buf(serdev, skb->data,
						      skb->len);
			hdev->stat.byte_tx += len;

			skb_pull(skb, len);
			if (skb->len > 0) {
				skb_queue_head(&hu->txq, skb);
				break;
			}

			switch (hci_skb_pkt_type(skb)) {
			case HCI_COMMAND_PKT:
				hdev->stat.cmd_tx++;
				break;
			case HCI_ACLDATA_PKT:
				hdev->stat.acl_tx++;
				break;
			case HCI_SCODATA_PKT:
				hdev->stat.sco_tx++;
				break;
			}

			kfree_skb(skb);
		}

		// if (!test_bit(BTMTKUART_TX_STATE_WAKEUP, &bdev->tx_state))
		// 	break;
	}

	// clear_bit(BTMTKUART_TX_STATE_ACTIVE, &bdev->tx_state);
}

static const struct serdev_device_ops hci_serdev_ops = {
	.receive_buf = wq_recv,
	.write_wakeup = wq_uart_tx_wakeup,
};

static int hci_uart_probe(struct serdev_device *serdev)
{
	printk("11111111111 enter func %s ------------\n",__func__);
	struct hci_uart *hu;
	struct hci_dev *hdev;
	int err;

	dev_info(&serdev->dev, "HCI UART driver probe");

	/* Allocate HCI UART structure */
	hu = devm_kzalloc(&serdev->dev, sizeof(*hu), GFP_KERNEL);
	if (!hu) {
		dev_err(&serdev->dev, "Can't allocate hci_uart device");
		return -ENOMEM;
	}

	hu->serdev = serdev;
	INIT_WORK(&hu->write_work, wq_uart_tx_work);
	skb_queue_head_init(&hu->txq);
	serdev_device_set_drvdata(serdev, hu);

	/* Set up serial device */
	serdev_device_set_client_ops(serdev, &hci_serdev_ops);

	/* Allocate HCI device */
	hdev = hci_alloc_dev();
	if (!hdev) {
		dev_err(&serdev->dev, "Can't allocate HCI device");
		return -ENOMEM;
	}

	hu->hdev = hdev;
	hdev->bus = HCI_UART;
	hci_set_drvdata(hdev, hu);

	/* Set HCI device operations */
	hdev->open = hci_uart_open;
	hdev->close = hci_uart_close;
	hdev->flush = hci_uart_flush;
	hdev->send = hci_uart_send_frame;
	hdev->setup = hci_uart_setup;

	/* Set device parent */
	SET_HCIDEV_DEV(hdev, &serdev->dev);

	/* Open the UART */
	err = serdev_device_open(serdev);
	if (err) {
		dev_err(&serdev->dev, "Cannot open serial device: %d", err);
		goto err_hci_free;
	}
	/* Configure UART settings */
	serdev_device_set_baudrate(serdev, 3000000);
	serdev_device_set_flow_control(serdev, true);
	serdev_device_set_parity(serdev, SERDEV_PARITY_NONE);

	/* Register HCI device */
	err = hci_register_dev(hdev);
	if (err < 0) {
		bt_dev_err(hdev, "Can't register HCI device (%d)", err);
		goto close_serdev;
		;
	}

	return 0;
close_serdev:
	serdev_device_close(serdev);
err_hci_free:
	hci_free_dev(hdev);
	return err;
}

static void hci_uart_remove(struct serdev_device *serdev)
{
	struct hci_uart *hu = serdev_device_get_drvdata(serdev);
	struct hci_dev *hdev = hu->hdev;
	dev_info(&serdev->dev, "HCI UART driver remove");
	/* Unregister and free HCI device */
	serdev_device_close(serdev);
	hci_unregister_dev(hdev);
	hci_free_dev(hdev);
	skb_queue_purge(&hu->txq);
}

//#ifdef CONFIG_OF
static const struct of_device_id hci_uart_of_match[] = {
	{ .compatible = "wuqi,wq9201s_bt" },
	{},
};
MODULE_DEVICE_TABLE(of, hci_uart_of_match);
//#endif

static struct serdev_device_driver hci_uart_driver = {
    .probe = hci_uart_probe,
    .remove = hci_uart_remove,
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = of_match_ptr(hci_uart_of_match),
    },
};

module_serdev_device_driver(hci_uart_driver);

MODULE_AUTHOR("WuQi Technologies");
MODULE_DESCRIPTION("HCI UART driver for WuQi bluetooth devices");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION("1.0");
