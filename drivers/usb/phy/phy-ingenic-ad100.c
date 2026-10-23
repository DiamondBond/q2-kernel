#include <linux/delay.h>
#include <linux/syscore_ops.h>
#include "phy-ingenic.h"

#define CPM_USB0PCR     0x3C
#define CPM_USB0RDT     0x40
#define CPM_USBVBFIL    0x44
#define CPM_USB0PCR1    0x48

#define CPM_SRBC    0xC4
#define CPM_OPCR    0x24

#define OPCR_SPENDN0_BIT			7

#define OPCR_GATE_USBPHY_CLK_BIT	23
#define SRBC_USB_SR					14

#define USBRDT_RESUME_IRQ_ENABLE			31
#define USBRDT_RESUME_CLEAR_IRQ				30
#define USBRDT_RESUME_SPEED				28
#define USBRDT_RESUME_STATUS				27

static struct usb_phy_data *ad100_usb_phy0;
static unsigned int ad100_port0_set_wakeup;

int ad100_phy_port0_suspend(void)
{
	unsigned int value;

	/*In suspend mode, turnning off differential receiver to save power.*/
	value = usb_phy_readl(ad100_usb_phy0, 0x30);
	value &= ~(0x1 << 2);
	usb_phy_writel(ad100_usb_phy0, value, 0x30);

	usb_cpm_clear_bit(ad100_usb_phy0, OPCR_SPENDN0_BIT, CPM_OPCR);

	if (ad100_usb_phy0->usb_wakeup && ad100_port0_set_wakeup) {
		/* set wakeup speed */
		value = usb_cpm_readl(ad100_usb_phy0, CPM_USB0RDT);
		value &= ~(0x3 << USBRDT_RESUME_SPEED);
		value |= ((ad100_port0_set_wakeup - 1) & 0x3) << USBRDT_RESUME_SPEED;
		usb_cpm_writel(ad100_usb_phy0, value, CPM_USB0RDT);

		usb_cpm_clear_bit(ad100_usb_phy0, USBRDT_RESUME_CLEAR_IRQ, CPM_USB0RDT);
		usb_cpm_set_bit(ad100_usb_phy0, USBRDT_RESUME_IRQ_ENABLE, CPM_USB0RDT);
	}

	return 0;
}

void ad100_phy_port0_resume(void)
{
	unsigned int value;

	if (ad100_usb_phy0->usb_wakeup && ad100_port0_set_wakeup)
		usb_cpm_clear_bit(ad100_usb_phy0, USBRDT_RESUME_IRQ_ENABLE, CPM_USB0RDT);
	usb_cpm_set_bit(ad100_usb_phy0, OPCR_SPENDN0_BIT, CPM_OPCR);

	/*turnning on differential receiver.*/
	value = usb_phy_readl(ad100_usb_phy0, 0x30);
	value |= (0x1 << 2);
	usb_phy_writel(ad100_usb_phy0, value, 0x30);
}

static struct syscore_ops ad100_phy_port0_syscore_ops = {
	.suspend = ad100_phy_port0_suspend,
	.resume = ad100_phy_port0_resume,
};

static int ad100_priv_data_port0_init(struct usb_phy_data *usb_phy)
{
	/* set controller reset and phy suspends*/
	usb_cpm_set_bit(usb_phy, SRBC_USB_SR, CPM_SRBC);

	usleep_range(10, 20);
	
	/* clear controller reset */
	usb_cpm_clear_bit(usb_phy, SRBC_USB_SR, CPM_SRBC);

	ad100_usb_phy0 = usb_phy;
	register_syscore_ops(&ad100_phy_port0_syscore_ops);
	return 0;
}

static int ad100_phy_port0_init(struct usb_phy_data *usb_phy)
{
	unsigned int value;

	usleep_range(10, 20);

	/* set phy reset */
	value = usb_cpm_readl(usb_phy, CPM_USB0PCR);
	value |= 1 << 31; // work as OTG
	value &= ~(3 << 28); // idpullup from otg controller
	value |= 1 << 22; // phy power on reset
	value |= 1 << 20; // phy utmi on reset
	usb_cpm_writel(usb_phy, value, CPM_USB0PCR);

	usleep_range(10, 20);

	/* clear phy power reset */
	value = usb_cpm_readl(usb_phy, CPM_USB0PCR);
	value &= ~(1 << 22);
	usb_cpm_writel(usb_phy, value, CPM_USB0PCR);

	usleep_range(10, 20);

	/* clear phy suspend */
	usb_cpm_set_bit(usb_phy, OPCR_SPENDN0_BIT, CPM_OPCR);

	/* wait phy PLL */
	usleep_range(1000, 1000);

	/* clear phy utmi reset */
	value = usb_cpm_readl(usb_phy, CPM_USB0PCR);
	value &= ~(1 << 20);
	usb_cpm_writel(usb_phy, value, CPM_USB0PCR);

	/* wait phy operational */
	usleep_range(10, 20);

	/* DP DM pull-down by the controller */
	value = usb_cpm_readl(usb_phy, CPM_USB0PCR1);
	// value |= 1 << 26; // Reduce ID debounce time
	value |= 1 << 28; // DM pull down by controller
	value |= 1 << 29; // DP pull down by controller
	usb_cpm_writel(usb_phy, value, CPM_USB0PCR1);

	/* Chirp K or SE0 resume enable */
	value = usb_cpm_readl(usb_phy, CPM_USB0RDT);
	value |= 0x1 << 26;
	usb_cpm_writel(usb_phy, value, CPM_USB0RDT);

	/* In fact, when the high-speed eye height is set to the highest,
	   the register value should be 3'b110. The default value of 3'b111 in PM
	   is the lowest. */
	value = usb_phy_readl(usb_phy, 0x30);
	value &= ~(0x7 << 4);
	value |= 0x6 << 4;
	usb_phy_writel(usb_phy, value, 0x30);

	/* VBUS voltage level detection power down. */
	value = usb_phy_readl(usb_phy, 0x3c);
	value |= 0x1 << 7;
	usb_phy_writel(usb_phy, value, 0x3c);

	/* otg bypass */
	value = usb_phy_readl(usb_phy, 0xa0);
	value |= 0x1 << 5; //otg bypass
	value |= 0x1 << 6; //utmi_avalid
	value |= 0x1 << 7; //utmi_bvalid
	usb_phy_writel(usb_phy, value, 0xa0);

	/* otg bypass vbus valid */
	value = usb_phy_readl(usb_phy, 0xa4);
	//value |= 0x1 << 0; //utmi_sessend
	value |= 0x1 << 1; //utmi_vbusvalid
	usb_phy_writel(usb_phy, value, 0xa4);


#if 0 /* bypass host disconnect for debug */
	value = usb_phy_readl(usb_phy, 0x30);
	value &= ~(0x3 << 0);
	usb_phy_writel(usb_phy, value, 0x30);

	value = usb_phy_readl(usb_phy, 0x3c);
	value &= ~(0x7 << 0);
	value |= 0x2 << 0;
	usb_phy_writel(usb_phy, value, 0x3c);
#endif

	return 0;
}

static int ad100_phy_port0_set_wakeup(struct usb_phy_data *usb_phy, int enabled)
{
	ad100_port0_set_wakeup = enabled;

	return 0;
}

static int ad100_phy_port0_get_wakeup(struct usb_phy_data *usb_phy)
{
	if (usb_phy->usb_wakeup) {
		if (usb_cpm_test_bit(usb_phy, USBRDT_RESUME_STATUS,CPM_USB0RDT)) {
			usb_cpm_set_bit(usb_phy, USBRDT_RESUME_CLEAR_IRQ, CPM_USB0RDT);
			return 1;
		}
	}

	return 0;
}

struct usb_phy_priv usb0_phy_ad100_priv = {
	.priv_data_init = ad100_priv_data_port0_init,

	.phy_init = ad100_phy_port0_init,
	.phy_set_wakeup = ad100_phy_port0_set_wakeup,
	.phy_get_wakeup = ad100_phy_port0_get_wakeup,
};

// smaple

#define CPM_USB1PCR     0x4C
#define CPM_USB1RDT     0x50
#define CPM_USB1VBFIL   0x58
#define CPM_USB1PCR1    0xe8
#define SRBC_USB1_SR					15
#define OPCR_SPENDN0_BIT			7
#define OPCR_SPENDN1_BIT			6

static struct usb_phy_data *ad100_usb_phy1;
static unsigned int ad100_port1_set_wakeup;

int ad100_phy_port1_suspend(void)
{
	unsigned int value;

	/*In suspend mode, turnning off differential receiver to save power.*/
	value = usb_phy_readl(ad100_usb_phy1, 0x30);
	value &= ~(0x1 << 2);
	usb_phy_writel(ad100_usb_phy1, value, 0x30);

	usb_cpm_clear_bit(ad100_usb_phy1, OPCR_SPENDN1_BIT, CPM_OPCR);

	if (ad100_usb_phy1->usb_wakeup && ad100_port1_set_wakeup) {
		/* set wakeup speed */
		value = usb_cpm_readl(ad100_usb_phy1, CPM_USB1RDT);
		value &= ~(0x3 << USBRDT_RESUME_SPEED);
		value |= ((ad100_port1_set_wakeup - 1) & 0x3) << USBRDT_RESUME_SPEED;
		usb_cpm_writel(ad100_usb_phy1, value, CPM_USB1RDT);

		usb_cpm_clear_bit(ad100_usb_phy1, USBRDT_RESUME_CLEAR_IRQ, CPM_USB1RDT);
		usb_cpm_set_bit(ad100_usb_phy1, USBRDT_RESUME_IRQ_ENABLE, CPM_USB1RDT);
	}

	return 0;
}

void ad100_phy_port1_resume(void)
{
	unsigned int value;

	if (ad100_usb_phy1->usb_wakeup && ad100_port1_set_wakeup)
		usb_cpm_clear_bit(ad100_usb_phy1, USBRDT_RESUME_IRQ_ENABLE, CPM_USB1RDT);
	usb_cpm_set_bit(ad100_usb_phy1, OPCR_SPENDN1_BIT, CPM_OPCR);

	/*turnning on differential receiver.*/
	value = usb_phy_readl(ad100_usb_phy1, 0x30);
	value |= (0x1 << 2);
	usb_phy_writel(ad100_usb_phy1, value, 0x30);
}

static struct syscore_ops ad100_phy_port1_syscore_ops = {
	.suspend = ad100_phy_port1_suspend,
	.resume = ad100_phy_port1_resume,
};

static int ad100_priv_data_port1_init(struct usb_phy_data *usb_phy)
{
	/* set controller reset and phy suspends*/
	usb_cpm_set_bit(usb_phy, SRBC_USB1_SR, CPM_SRBC);

	usleep_range(10, 20);

	/* clear controller reset */
	usb_cpm_clear_bit(usb_phy, SRBC_USB1_SR, CPM_SRBC);


	ad100_usb_phy1 = usb_phy;
	register_syscore_ops(&ad100_phy_port1_syscore_ops);
	return 0;
}

static int ad100_phy_port1_init(struct usb_phy_data *usb_phy)
{
	unsigned int value;

	usleep_range(10, 20);

	/* set phy reset */
	value = usb_cpm_readl(usb_phy, CPM_USB1PCR);
	value |= 1 << 31; // work as OTG
	value &= ~(3 << 28); // idpullup from otg controller
	value |= 1 << 20; // phy utmi on reset
	usb_cpm_writel(usb_phy, value, CPM_USB1PCR);

	usleep_range(10, 20);

	/* clear phy suspend */
	usb_cpm_set_bit(usb_phy, OPCR_SPENDN1_BIT, CPM_OPCR);

	/* wait phy PLL */
	usleep_range(1000, 1000);

	/* clear phy utmi reset */
	value = usb_cpm_readl(usb_phy, CPM_USB1PCR);
	value &= ~(1 << 20);
	usb_cpm_writel(usb_phy, value, CPM_USB1PCR);

	/* wait phy operational */
	usleep_range(10, 20);

	/* DP DM pull-down by the controller */
	value = usb_cpm_readl(usb_phy, CPM_USB1PCR1);
	value |= 1 << 28; // DM pull down by controller
	value |= 1 << 29; // DP pull down by controller
	usb_cpm_writel(usb_phy, value, CPM_USB1PCR1);

	/* Chirp K or SE0 resume enable */
	value = usb_cpm_readl(usb_phy, CPM_USB1RDT);
	value |= 0x1 << 26;
	usb_cpm_writel(usb_phy, value, CPM_USB1RDT);

	/* In fact, when the high-speed eye height is set to the highest,
	   the register value should be 3'b110. The default value of 3'b111 in PM
	   is the lowest. */
	value = usb_phy_readl(usb_phy, 0x30);
	value &= ~(0x7 << 4);
	value |= 0x6 << 4;
	usb_phy_writel(usb_phy, value, 0x30);

	/* VBUS voltage level detection power down. */
	value = usb_phy_readl(usb_phy, 0x3c);
	value |= 0x1 << 7;
	usb_phy_writel(usb_phy, value, 0x3c);

	/* otg bypass */
	value = usb_phy_readl(usb_phy, 0xa0);
	value |= 0x1 << 5; //otg bypass
	value |= 0x1 << 6; //utmi_avalid
	value |= 0x1 << 7; //utmi_bvalid
	usb_phy_writel(usb_phy, value, 0xa0);

	/* otg bypass vbus valid */
	value = usb_phy_readl(usb_phy, 0xa4);
	//value |= 0x1 << 0; //utmi_sessend
	value |= 0x1 << 1; //utmi_vbusvalid
	usb_phy_writel(usb_phy, value, 0xa4);

#if 0 /* bypass host disconnect for debug */
	value = usb_phy_readl(usb_phy, 0x30);
	value &= ~(0x3 << 0);
	usb_phy_writel(usb_phy, value, 0x30);

	value = usb_phy_readl(usb_phy, 0x3c);
	value &= ~(0x7 << 0);
	value |= 0x2 << 0;
	usb_phy_writel(usb_phy, value, 0x3c);
#endif

	return 0;
}

static int ad100_phy_port1_set_wakeup(struct usb_phy_data *usb_phy, int enabled)
{
	ad100_port1_set_wakeup = enabled;

	return 0;
}

static int ad100_phy_port1_get_wakeup(struct usb_phy_data *usb_phy)
{
	if (usb_phy->usb_wakeup) {
		if (usb_cpm_test_bit(usb_phy, USBRDT_RESUME_STATUS,CPM_USB1RDT)) {
			usb_cpm_set_bit(usb_phy, USBRDT_RESUME_CLEAR_IRQ, CPM_USB1RDT);
			return 1;
		}
	}

	return 0;
}

struct usb_phy_priv usb1_phy_ad100_priv = {
	.priv_data_init = ad100_priv_data_port1_init,

	.phy_init = ad100_phy_port1_init,
	.phy_set_wakeup = ad100_phy_port1_set_wakeup,
	.phy_get_wakeup = ad100_phy_port1_get_wakeup,
};
