#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/phy.h>

#define RPC8201F_PHY_ID	0x00000128

#define RPC8201F_MII_REG_EXT_PTR	0x1e			/* EXT Register's Address Offset Register */
#define RPC8201F_MII_REG_EXT_DATA	0x1f			/* EXT Register's Data Register */

#define RPC8201F_MII_REG_EXT_SLEEP_CTRL	0x2027		/* EXT Register Sleep Control */

#define RPC8201F_SLEEP_MODE_EN	BIT(15)				/* Enable Sleep Mode */

static int rpc8201f_config_init(struct phy_device *phydev)
{
	int val, err;

	/* Software Reset PHY */
	val = phy_read(phydev, MII_BMCR);
	if (val < 0)
		return val;

	val |= BMCR_RESET;
	err = phy_write(phydev, MII_BMCR, val);
	if (err < 0)
		return err;

	do {
		val = phy_read(phydev, MII_BMCR);
	} while (val & BMCR_RESET);

	/* 关闭 PHY 芯片的休眠模式, 以确保 PHY 后续通讯正常 */
	// 向0x1E寄存器写入目标地址
	err = phy_write(phydev, RPC8201F_MII_REG_EXT_PTR, RPC8201F_MII_REG_EXT_SLEEP_CTRL);
	if (err < 0)
		return err;

	// 从数据寄存器0x1F读取休眠控制寄存器值
	val = phy_read(phydev, RPC8201F_MII_REG_EXT_DATA);

	// 清除bit15以禁用休眠模式
	val &= ~RPC8201F_SLEEP_MODE_EN;

	// 将修改后的值写回
	err = phy_write(phydev, RPC8201F_MII_REG_EXT_DATA, val);
	if (err < 0)
		return err;

	return 0;
}

static struct phy_driver rpc8201f_driver[] = { {
	.name = "RPC8201F",
	.phy_id = RPC8201F_PHY_ID,
	.phy_id_mask = 0xffffffff,
	.config_init = rpc8201f_config_init,
} };

module_phy_driver(rpc8201f_driver);

static struct mdio_device_id __maybe_unused rpc8201f_tbl[] = {
	{ RPC8201F_PHY_ID, 0xffffffff },
};

MODULE_DEVICE_TABLE(mdio, rpc8201f_tbl);

MODULE_DESCRIPTION("RPC8201F Ethernet PHY driver");
MODULE_LICENSE("GPL");