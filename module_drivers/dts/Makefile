# SPDX-License-Identifier: GPL-2.0
dtb-$(CONFIG_DT_HALLEY5_V20)	+= halley5_v20.dtb
dtb-$(CONFIG_DT_HALLEY5_V30)	+= halley5_v30.dtb
dtb-$(CONFIG_DT_ZEBRA)		+= zebra.dtb
dtb-$(CONFIG_DT_HIPPO_V10)	+= hippo_v10.dtb
dtb-$(CONFIG_DT_HIPPO_V12)	+= hippo_v12.dtb
dtb-$(CONFIG_DT_PANDA_V10)	+= panda_v10.dtb
dtb-$(CONFIG_DT_HALLEY6_V10)	+= halley6_v10.dtb
dtb-$(CONFIG_DT_HALLEY6_V20)	+= halley6_v20.dtb
dtb-$(CONFIG_DT_HALLEY6_V20_SEEKWAVE)	+= halley6_v20_seekwave.dtb
dtb-$(CONFIG_DT_GEWU_V10)	+= gewu_v10.dtb
dtb-$(CONFIG_DT_GEWU_V12)	+= gewu_v12.dtb
dtb-$(CONFIG_DT_KALE_V10)	+= kale_v10.dtb
dtb-$(CONFIG_DT_KALE_V12)	+= kale_v12.dtb

dtb-$(CONFIG_DT_X2660_EVB)  	+= x2660_evb_board.dtb
dtb-$(CONFIG_DT_X2600_TUCANA)	+= x2600_tucana_board.dtb
dtb-$(CONFIG_DT_X2660_HALLEY_V10)   += x2660_halley_v1.0.dtb
dtb-$(CONFIG_DT_X2660_HALLEY_V11)   += x2660_halley_v1.1.dtb
dtb-$(CONFIG_DT_X2670_HALLEY_V10)   += x2670_halley_v1.0.dtb
dtb-$(CONFIG_DT_X2600E_HALLEY_V10)   += x2600e_halley_v1.0.dtb
dtb-$(CONFIG_DT_X2670M_HARE_V10)   += x2670m_hare_v1.0.dtb
dtb-$(CONFIG_DT_X2600_HALLEY_V10)   += x2600_halley_v1.0.dtb
dtb-$(CONFIG_DT_X2600H_HALLEY7_V10)   += x2600h_halley7_v1.0.dtb
dtb-$(CONFIG_DT_X2600M_HALLEY7_V10)   += x2600m_halley7_v1.0.dtb

# x1600_module_base.dts
module_driver_x1600_dst_file := $(shell echo $(CONFIG_DT_X1600_MODULE_BASE_DTS_FILE))
module_driver_x1600_dst_file := $(basename $(module_driver_x1600_dst_file)).dtb
dtb-$(CONFIG_DT_X1600_MODULE_BASE) += $(module_driver_x1600_dst_file)

# x2000_module_base.dts
module_driver_x2000_dst_file := $(shell echo $(CONFIG_DT_X2000_MODULE_BASE_DTS_FILE))
module_driver_x2000_dst_file := $(basename $(module_driver_x2000_dst_file)).dtb
dtb-$(CONFIG_DT_X2000_MODULE_BASE) += $(module_driver_x2000_dst_file)

# x2500_module_base.dts
module_driver_x2500_dst_file := $(shell echo $(CONFIG_DT_X2500_MODULE_BASE_DTS_FILE))
module_driver_x2500_dst_file := $(basename $(module_driver_x2500_dst_file)).dtb
dtb-$(CONFIG_DT_X2500_MODULE_BASE) += $(module_driver_x2500_dst_file)

# x2600_module_base.dts
module_driver_x2600_dst_file := $(shell echo $(CONFIG_DT_X2600_MODULE_BASE_DTS_FILE))
module_driver_x2600_dst_file := $(basename $(module_driver_x2600_dst_file)).dtb
dtb-$(CONFIG_DT_X2600_MODULE_BASE) += $(module_driver_x2600_dst_file)

# wicom
dtb-$(CONFIG_DT_WICOM_X2000_LINPHONE_OUTDOOR)	+= wicom_x2000_linphone_outdoor.dtb
dtb-$(CONFIG_DT_WICOM_X2000_LINPHONE_INDOOR)	+= wicom_x2000_linphone_indoor.dtb

# x1000_module_base.dts
module_driver_x1000_dst_file := $(shell echo $(CONFIG_DT_X1000_MODULE_BASE_DTS_FILE))
module_driver_x1000_dst_file := $(basename $(module_driver_x1000_dst_file)).dtb
dtb-$(CONFIG_DT_X1000_MODULE_BASE) += $(module_driver_x1000_dst_file)

obj-$(CONFIG_BUILTIN_DTB)	+= $(addsuffix .o, $(dtb-y))
