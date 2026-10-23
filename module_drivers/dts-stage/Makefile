# SPDX-License-Identifier: GPL-2.0
dtb-$(CONFIG_DT_X1600_HALLEY6_V20)	+= x1600/halley6_v20.dtb
dtb-$(CONFIG_DT_X1600_PANDA_V10)	+= x1600/panda_v10.dtb

dtb-$(CONFIG_DT_M300_GEWU_V12)		+= m300/gewu_v12.dtb
dtb-$(CONFIG_DT_X2100_KALE_V12)		+= x2000/kale_v12.dtb
dtb-$(CONFIG_DT_X2000_HALLEY5_V30)	+= x2000/halley5_v30.dtb

dtb-$(CONFIG_DT_X2500_HIPPO_V12)	+= x2500/hippo_v12.dtb

dtb-$(CONFIG_DT_X2600_HALLEY7_V10)		+= x2600/halley7.dtb
dtb-$(CONFIG_DT_X2670_HARE_V10)		+= x2600/hare.dtb

dtb-$(CONFIG_DT_AD100_EVB_V10)		+= ad100/evb_v10.dtb
dtb-$(CONFIG_DT_AD102_APUS_V10)		+= ad100/apus_v10.dtb




# 定义 findfile 宏（包含路径替换）
define findfile
$(patsubst %.dts,%.dtb,$(shell cd $(srctree)/module_drivers/dts-stage/ && find -name "$1" -printf "%P\n" -quit))
endef

# AD100 模块
dtb-$(CONFIG_DT_AD100_MODULE_BASE) += $(call findfile,$(CONFIG_DT_AD100_MODULE_BASE_DTS_FILE))
# X2600 模块
dtb-$(CONFIG_DT_STAGE_X2600_MODULE_BASE) += $(call findfile,$(CONFIG_DT_X2600_MODULE_BASE_DTS_STAGE_FILE))

obj-$(CONFIG_BUILTIN_DTB)	+= $(addsuffix .o, $(dtb-y))
