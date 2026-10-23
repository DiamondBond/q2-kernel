 ## Makefile -- project specific makefile to build ZTOP Wifi driver
 ##
 ## (C) Copyright (c) 2021 Shandong ZTop Microelectronics Co., Ltds
 ##
 ## Mandatory settings:
 ##
 ## o TOPDIR                        = the toplevel directory (using slashes as path separator)
 ## o SUBDIR                        = the make file folder
 ## o CT                            = display driver compile time(y)
 ## o CONFIG_DEBUG                  = enable debug information(y)
 ## o CONFIG_DRIVER_VER             = null(use svn version), else this is version
 ## o CONFIG_MODULE_IMPORT_NS       = whether import ns or not(y/n)
 ## o CONFIG_FW_FILE                = firmware is file or array(y)
 ## o CONFIG_WIFI_INTERFACE_TWO     = Second WiFi interface (y)
 ## o CONFIG_TX_SOFT_AGG            = switch for tx soft agg(n)
 ## o CONFIG_RX_SOFT_AGG            = switch for rx soft agg(y)
 ## o CONFIG_WIFI_MODE              = all(sta/ap/adhoc/monitor), sta, ap, adhoc
 ## o CONFIG_WIFI_FRAMEWORK         = wext, nl80211, mp
 ## o CONFIG_HIF_PORT               = usb, sdio, both
 ## o CONFIG_CLOCK_24MHZ            = y/n  sdio if use 24MHZ clock
 ## o CONFIG_ZT9101XV20_SUPPORT     = enable support ZT9101xV20
 ## o CONFIG_ZT9101XV30_SUPPORT     = enable support ZT9101xV30
 ## o CONFGI_NAME                   = any string(zt9101_ztopmac)
 ################################################################################

 export WDRV_DIR ?= $(shell pwd)
 PLATDIR = platform
 CT                       ?= n
 CONFIG_DEBUG              = n
 CONFIG_DRIVER_VER         = V1.2.1713.20241030
 CONFIG_MODULE_IMPORT_NS   = n
 CONFIG_FW_FILE            = n
 CONFIG_STA_AND_AP_MODE    = n
 CONFIG_TX_SOFT_AGG        = n
 CONFIG_RX_SOFT_AGG        = n
 CONFIG_WIFI_MODE          = all
 CONFIG_WIFI_FRAMEWORK     = nl80211
 CONFIG_HIF_PORT           = both
 CONFIG_CLOCK_24MHZ        = n
 CONFIG_ZT9101XV20_SUPPORT = y
 CONFIG_ZT9101XV30_SUPPORT = y
 CONFIG_POWER_SAVING       = n
 CONFIG_NAME               = zt9101_ztopmac
########### COMMON  #################################
ifeq ($(CONFIG_HIF_PORT), usb)
MODULE_NAME := $(CONFIG_NAME)_usb
else ifeq ($(CONFIG_HIF_PORT), sdio)
MODULE_NAME := $(CONFIG_NAME)_sdio
else
MODULE_NAME := $(CONFIG_NAME)
endif
$(info MODULE_NAME=$(MODULE_NAME))
$(info -------------->ZTdebugsupport $(CONFIG_DEBUG))
#####################################################
ifeq ($(CONFIG_DRIVER_VER), null)
# get svn version
#EXTRA_CFLAGS += -D'ZT_VERSION="[svn]$(shell svnversion -n $$WDRV_DIR)"'
EXTRA_CFLAGS += -D'ZT_VERSION="[svn]$(shell svnversion -c $$WDRV_DIR | cut -d : -f 2)"'
else
# use Driver version
EXTRA_CFLAGS += -D'ZT_VERSION="$(CONFIG_DRIVER_VER)"'
endif
#get compile time
ifeq ($(CT), y)
EXTRA_CFLAGS += -D'COMPILE_TIME="$(shell date +"%Y-%m-%d %H:%M:%S")"'
endif

ifeq ($(CONFIG_MODULE_IMPORT_NS), y)
EXTRA_CFLAGS += -DCONFIG_MODULE_IMPORT_NS
endif

ifeq ($(CONFIG_FW_FILE), y)
EXTRA_CFLAGS += -DCONFIG_FW_FILE
endif

ifeq ($(CONFIG_ZT9101XV20_SUPPORT), y)
EXTRA_CFLAGS += -DCONFIG_ZT9101XV20_SUPPORT
endif

ifeq ($(CONFIG_ZT9101XV30_SUPPORT), y)
EXTRA_CFLAGS += -DCONFIG_ZT9101XV30_SUPPORT
endif

EXTRA_CFLAGS += $(USER_EXTRA_CFLAGS)
EXTRA_CFLAGS += -O1
EXTRA_CFLAGS += -I$(WDRV_DIR)/os/
EXTRA_CFLAGS += -I$(WDRV_DIR)/os/linux
EXTRA_CFLAGS += -I$(WDRV_DIR)/os/linux/os_porting
EXTRA_CFLAGS += -I$(WDRV_DIR)/os/linux/hif
EXTRA_CFLAGS += -I$(WDRV_DIR)/os/linux/netdev
EXTRA_CFLAGS += -I$(WDRV_DIR)/os/linux/cfg80211
EXTRA_CFLAGS += -I$(WDRV_DIR)/os/linux/iw
EXTRA_CFLAGS += -I$(WDRV_DIR)/os/linux/pwr
EXTRA_CFLAGS += -I$(WDRV_DIR)/nic
EXTRA_CFLAGS += -I$(WDRV_DIR)/nic/utility
EXTRA_CFLAGS += -I$(WDRV_DIR)/nic/p2p
ifndef __linux__
EXTRA_CFLAGS += -D__linux__
endif

ifeq ($(CONFIG_DEBUG), y)
EXTRA_CFLAGS += -DZT_DEBUG_ENABLE
EXTRA_CFLAGS += -Wall
EXTRA_CFLAGS += -Wextra
EXTRA_CFLAGS += -Wno-unused-parameter
EXTRA_CFLAGS += -Wno-implicit-fallthrough
EXTRA_CFLAGS += -Wno-sign-compare
EXTRA_CFLAGS += -Wno-missing-field-initializers
EXTRA_CFLAGS += -Wno-address-of-packed-member
EXTRA_CFLAGS += -Wno-uninitialized
EXTRA_CFLAGS += -Wno-type-limits
EXTRA_CFLAGS += -Wno-empty-body
EXTRA_CFLAGS += -w
else
EXTRA_CFLAGS += -Wno-all
EXTRA_CFLAGS += -Wno-extra
EXTRA_CFLAGS += -w
endif

ifeq ($(CONFIG_TX_SOFT_AGG), y)
EXTRA_CFLAGS += -DCONFIG_SOFT_TX_AGGREGATION
endif

ifeq ($(CONFIG_RX_SOFT_AGG), y)
EXTRA_CFLAGS += -DCONFIG_SOFT_RX_AGGREGATION
endif

ifeq ($(CONFIG_HIF_PORT), usb)
EXTRA_CFLAGS += -DCONFIG_USB_FLAG
else ifeq ($(CONFIG_HIF_PORT), sdio)
EXTRA_CFLAGS += -DCONFIG_SDIO_FLAG
else
EXTRA_CFLAGS += -DCONFIG_BOTH_FLAG
endif

ifeq ($(CONFIG_CLOCK_24MHZ), y)
EXTRA_CFLAGS += -DCONFIG_CLOCK_24MHZ
endif

ifeq ($(CONFIG_WIFI_MODE), ap)
EXTRA_CFLAGS += -DCFG_ENABLE_AP_MODE
EXTRA_CFLAGS += -DZT_WEP_AUTH_ENABLE_WEXT
endif

ifeq ($(CONFIG_WIFI_MODE), monitor)
EXTRA_CFLAGS += -DCFG_ENABLE_MONITOR_MODE
endif

ifeq ($(CONFIG_WIFI_MODE), adhoc)
EXTRA_CFLAGS += -DCFG_ENABLE_ADHOC_MODE
endif

ifeq ($(CONFIG_WIFI_MODE), all)
EXTRA_CFLAGS += -DCFG_ENABLE_AP_MODE
EXTRA_CFLAGS += -DCFG_ENABLE_ADHOC_MODE
EXTRA_CFLAGS += -DCFG_ENABLE_MONITOR_MODE
EXTRA_CFLAGS += -DZT_WEP_AUTH_ENABLE_WEXT
endif

ifeq ($(CONFIG_WIFI_FRAMEWORK), nl80211)
EXTRA_CFLAGS += -DCONFIG_IOCTL_CFG80211 -DZT_USE_CFG80211_STA_EVENT
else ifeq ($(CONFIG_WIFI_FRAMEWORK), mp)
EXTRA_CFLAGS += -DCONFIG_MP_MODE
endif

ifeq ($(CONFIG_STA_AND_AP_MODE), y)
EXTRA_CFLAGS += -DCONFIG_STA_AND_AP_MODE
endif

ifeq ($(CONFIG_POWER_SAVING), y)
EXTRA_CFLAGS += -DCONFIG_LPS
endif


EXTRA_CFLAGS += -DCONFIG_LITTLE_ENDIAN
EXTRA_LDFLAGS += --strip-debug

########################## Features ###########################

ifneq ($(KERNELRELEASE),)

ifeq ($(CONFIG_HIF_PORT), usb)
$(MODULE_NAME)-y += os/linux/hif/usb.o
else ifeq ($(CONFIG_HIF_PORT), sdio)
$(MODULE_NAME)-y += os/linux/hif/sdio.o
else
$(MODULE_NAME)-y += os/linux/hif/usb.o
$(MODULE_NAME)-y += os/linux/hif/sdio.o
endif

$(MODULE_NAME)-y += os/linux/os_porting/zt_os_api_mix.o
$(MODULE_NAME)-y += os/linux/os_porting/zt_os_api_lock.o
$(MODULE_NAME)-y += os/linux/os_porting/zt_os_api_sema.o
$(MODULE_NAME)-y += os/linux/os_porting/zt_os_api_thread.o
$(MODULE_NAME)-y += os/linux/os_porting/zt_os_api_timer.o
ifeq ($(CONFIG_FW_FILE), y)
$(MODULE_NAME)-y += os/linux/os_porting/zt_os_api_file.o
endif
$(MODULE_NAME)-y += os/linux/os_porting/zt_os_api_workqueue.o
$(MODULE_NAME)-y += os/linux/hif/hif.o
$(MODULE_NAME)-y += os/linux/hif/hif_io.o
$(MODULE_NAME)-y += os/linux/hif/cfg_parse.o
$(MODULE_NAME)-y += os/linux/hif/hw_ctrl.o
$(MODULE_NAME)-y += os/linux/hif/fw_download.o
$(MODULE_NAME)-y += os/linux/hif/hif_queue.o
$(MODULE_NAME)-y += os/linux/netdev/ndev_linux.o
$(MODULE_NAME)-y += os/linux/iw/iw.o
$(MODULE_NAME)-y += os/linux/proc.o
$(MODULE_NAME)-y += os/linux/iw/iw_func.o

ifeq ($(CONFIG_WIFI_FRAMEWORK), nl80211)
$(MODULE_NAME)-y += os/linux/cfg80211/zt_cfg80211.o
else ifeq ($(CONFIG_WIFI_FRAMEWORK), mp)
$(MODULE_NAME)-y += os/linux/proc_trx.o
endif

$(MODULE_NAME)-y += os/android/android_priv_cmd.o
$(MODULE_NAME)-y += os/linux/rx_linux.o
$(MODULE_NAME)-y += os/linux/tx_linux.o
$(MODULE_NAME)-y += nic/p2p/p2p.o
$(MODULE_NAME)-y += nic/p2p/p2p_frame_mgt.o
$(MODULE_NAME)-y += nic/p2p/p2p_proto_mgt.o
$(MODULE_NAME)-y += nic/p2p/p2p_wfd.o
$(MODULE_NAME)-y += nic/p2p/p2p_wowlan.o
$(MODULE_NAME)-y += nic/nic.o
$(MODULE_NAME)-y += nic/nic_io.o
$(MODULE_NAME)-y += nic/rx.o
$(MODULE_NAME)-y += nic/tx.o
$(MODULE_NAME)-y += nic/sec.o
$(MODULE_NAME)-y += nic/sec_arc4.o
$(MODULE_NAME)-y += nic/sec_tkip.o
$(MODULE_NAME)-y += nic/sec_wep.o
$(MODULE_NAME)-y += nic/mlme.o
$(MODULE_NAME)-y += nic/wdn.o
$(MODULE_NAME)-y += nic/scan.o
$(MODULE_NAME)-y += nic/auth.o
$(MODULE_NAME)-y += nic/assoc.o
$(MODULE_NAME)-y += nic/action.o
$(MODULE_NAME)-y += nic/wlan_mgmt.o
$(MODULE_NAME)-y += nic/zt_80211.o
$(MODULE_NAME)-y += nic/ie.o
$(MODULE_NAME)-y += nic/local_config.o
$(MODULE_NAME)-y += nic/hw_info.o
$(MODULE_NAME)-y += nic/mcu_cmd.o
$(MODULE_NAME)-y += nic/lps.o

$(MODULE_NAME)-y += nic/efuse.o
$(MODULE_NAME)-y += nic/utility/zt_que.o
$(MODULE_NAME)-y += nic/utility/zt_msg.o
$(MODULE_NAME)-y += nic/utility/zt_timer.o
$(MODULE_NAME)-y += nic/utility/zt_mix.o

ifeq ($(CONFIG_WIFI_MODE), adhoc)
$(MODULE_NAME)-y += nic/adhoc.o
endif

ifeq ($(CONFIG_WIFI_MODE), ap)
$(MODULE_NAME)-y += nic/ap.o
endif

ifeq ($(CONFIG_WIFI_MODE), all)
$(MODULE_NAME)-y += nic/ap.o
$(MODULE_NAME)-y += nic/adhoc.o
endif

$(MODULE_NAME)-y += os/linux/pwr/power_hw_rich200.o

obj-$(CONFIG_ZT9101_WLAN_SUPPORT) := $(MODULE_NAME).o

else

export CONFIG_ZT9101_WLAN_SUPPORT = m

all: modules

modules:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KSRC) M=$(WDRV_DIR)  modules

strip:
	$(CROSS_COMPILE)strip $(MODULE_NAME).ko --strip-unneeded

install:
	install -p -m 644 $(MODULE_NAME).ko  $(MODDESTDIR)
	/sbin/depmod -a ${KVER}

uninstall:
	rm -f $(MODDESTDIR)/$(MODULE_NAME).ko
	/sbin/depmod -a ${KVER}

config_r:
	@echo "make config"
	/bin/bash script/Configure script/config.in


.PHONY: modules clean

clean:
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KSRC) M=$(WDRV_DIR)  clean
endif

