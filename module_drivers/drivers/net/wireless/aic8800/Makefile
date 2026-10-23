CONFIG_AIC8800_BTLPM_SUPPORT := m
CONFIG_AIC8800_WLAN_SUPPORT := m
CONFIG_AIC_WLAN_SUPPORT := m

obj-$(CONFIG_AIC8800_BTLPM_SUPPORT) += aic8800_btlpm/
obj-$(CONFIG_AIC8800_WLAN_SUPPORT) += aic8800_fdrv/
obj-$(CONFIG_AIC_WLAN_SUPPORT) += aic8800_bsp/


# Platform support list
CONFIG_PLATFORM_UBUNTU = y

MAKEFLAGS +=-j$(shell nproc)


ifeq ($(CONFIG_PLATFORM_UBUNTU), y)
KDIR  = /lib/modules/$(shell uname -r)/build
PWD   = $(shell pwd)
KVER = $(shell uname -r)
MODDESTDIR = /lib/modules/$(KVER)/kernel/drivers/net/wireless/aic8800
ARCH = x86_64
CROSS_COMPILE =
endif
all: modules
modules:
	make -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

install:
	mkdir -p $(MODDESTDIR)
	install -p -m 644 aic8800_bsp/aic8800_bsp.ko  $(MODDESTDIR)/
	install -p -m 644 aic8800_fdrv/aic8800_fdrv.ko  $(MODDESTDIR)/
	install -p -m 644 aic8800_btlpm/aic8800_btlpm.ko  $(MODDESTDIR)/
	/sbin/depmod -a ${KVER}

uninstall:
	rm -rfv $(MODDESTDIR)/aic8800_bsp.ko
	rm -rfv $(MODDESTDIR)/aic8800_fdrv.ko
	rm -rfv $(MODDESTDIR)/aic8800_btlpm.ko
	/sbin/depmod -a ${KVER}

clean:
	cd aic8800_bsp/;make clean;cd ..
	cd aic8800_fdrv/;make clean;cd ..
	cd aic8800_btlpm/;make clean;cd ..
	rm -rf modules.order Module.symvers .tmp_versions/

