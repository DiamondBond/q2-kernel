# copyright (c) 2011 - 2012 espressif system
# linux_sdio
#ifneq ($(KERNELRELEASE),)
# kbuild part of makefile
ARCH := arm
export CROSS_COMPILE=$(shell pwd)/../../x2670/prebuilts/toolchains/mips-gcc720-glibc238/bin/mips-linux-gnu-
#kernel director
KSRC := $(shell pwd)
#driver directory
EAGLE_BASE_DIR := $(shell pwd)/module_drivers/drivers/net/wireless/esp8089_5.4.154/
MODPATH := $(shell pwd)
DRIVER_NAME := eagle
include $(EAGLE_BASE_DIR)/esp_config.mk

#-DHAS_FW  -DESP_ACK_INTERRUPT
#EXTRA_CFLAGS += -DHAS_INIT_DATA -DDEBUG -DSIP_DEBUG -DFAST_TX_STATUS -DKERNEL_IV_WAR -DRX_SENDUP_SYNC -DTEST_MODE -DINIT_DATA_CONF -DESP_CLASS -DFIX_IOCTL_ISSUE
EXTRA_CFLAGS += -DHAS_INIT_DATA -DDEBUG -DSIP_DEBUG -DFAST_TX_STATUS -DKERNEL_IV_WAR -DRX_SENDUP_SYNC -DTEST_MODE -DESP_CLASS -DFIX_IOCTL_ISSUE -DHAS_FW

EXTRA_CFLAGS += -Wno-error=date-time
EXTRA_CFLAGS += -Wno-date-time

#EXTRA_CFLAGS += -DSSD222 -DCRYSTAL_26M
EXTRA_CFLAGS += -DCRYSTAL_26M
EXTRA_CFLAGS += -I$(KSRC)/../drivers/sstar/sdmmc/inc/ -I$(KSRC)/../drivers/sstar/sdmmc/inc/pioneer3/ -I$(KSRC)/../drivers/sstar/include/pioneer3/

obj-$(CONFIG_ESP8089) := $(DRIVER_NAME).o
$(DRIVER_NAME)-y += esp_init.o
$(DRIVER_NAME)-y += esp_debug.o
$(DRIVER_NAME)-y += sdio_sif_esp.o
$(DRIVER_NAME)-y += spi_sif_esp.o
$(DRIVER_NAME)-y += esp_io.o
$(DRIVER_NAME)-y += esp_file.o
$(DRIVER_NAME)-y += esp_main.o
$(DRIVER_NAME)-y += esp_sip.o
$(DRIVER_NAME)-y += esp_ext.o
$(DRIVER_NAME)-y += esp_ctrl.o
$(DRIVER_NAME)-y += esp_mac80211.o
$(DRIVER_NAME)-y += esp_debug.o
$(DRIVER_NAME)-y += esp_utils.o
$(DRIVER_NAME)-y += esp_pm.o
$(DRIVER_NAME)-y += testmode.o

$(DRIVER_NAME):
	$(MAKE) ARCH=$(ARCH) -C $(KSRC) M=$(MODPATH) modules
	$(CROSS_COMPILE)strip -g $(DRIVER_NAME).ko

.PHONE:clean
clean:
	echo @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	echo $(MODPATH)
	echo @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	$(MAKE) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KSRC) M=$(MODPATH) clean 

#else
#KDIR ?= /lib/modules/`uname -r`/build
##KDIR ?= /home/espressif/src/sdio/cleanup

#default:
#	$(MAKE) -C $(KDIR) M=$$PWD

#endif
