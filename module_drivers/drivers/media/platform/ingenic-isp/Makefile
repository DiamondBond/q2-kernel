obj-y	+= isp-drv.o
obj-y	+= csi.o
obj-y	+= sensor.o
obj-y	+= isp-video.o
obj-y   += vic.o
obj-$(CONFIG_ISP_MSCALER_DMA_ROUTE) += mscaler.o \
				   isp.o \
			           isp-core-tuning.o \
			           isp-core/
obj-$(CONFIG_MSCA_BDEV)	+= mscaler-bdev.o

ccflags-y += -I$(srctree)/module_drivers/drivers/media/platform/ingenic-isp/isp-core/inc
ccflags-$(CONFIG_MSCA_BDEV) += -I$(srctree)/module_drivers/drivers/media/platform/ingenic-vcodec/helix

FILE=$(srctree)/module_drivers/drivers/media/platform/ingenic-isp/isp-core/isp-core.a_shipped
ifeq ($(FILE), $(wildcard $(FILE)))
$(error "please remove overlay isp-core.a_shipped")
endif


