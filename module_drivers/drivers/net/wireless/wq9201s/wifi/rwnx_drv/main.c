#include <linux/kernel.h>
#include <linux/module.h>

#ifdef WQ_WLAN_ALL_IN_ONE

int __init wq_pcie_init(void);
void __exit wq_pcie_exit(void);

int __init wq_sdio_init(void);
void __exit wq_sdio_exit(void);

int __init wq_usb_init(void);
void __exit wq_usb_exit(void);

static int __init wq_wlan_init(void)
{
	int ret = 0;

#if IS_ENABLED(CONFIG_WQ_WLAN_PCIE)
	ret = wq_pcie_init();
#endif

#if IS_ENABLED(CONFIG_WQ_WLAN_SDIO)
	ret |= wq_sdio_init();
#endif

#if IS_ENABLED(CONFIG_WQ_WLAN_USB)
	ret |= wq_usb_init();
#endif
	return ret;
}

static void __exit wq_wlan_exit(void)
{
#if IS_ENABLED(CONFIG_WQ_WLAN_USB)
	wq_usb_exit();
#endif
#if IS_ENABLED(CONFIG_WQ_WLAN_SDIO)
	wq_sdio_exit();
#endif
#if IS_ENABLED(CONFIG_WQ_WLAN_PCIE)
	wq_pcie_exit();
#endif
}

module_init(wq_wlan_init);
module_exit(wq_wlan_exit);

#endif
