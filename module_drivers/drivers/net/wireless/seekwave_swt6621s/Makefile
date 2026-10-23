SKW_ROOT := $(srctree)/module_drivers/drivers/net/wireless/seekwave_swt6621s
skw_extra_flags := -I$(SKW_ROOT)/include/linux/platform_data
skw_extra_flags += -DCONFIG_SKW_HOST_PLATFORM_INGENIC
export skw_extra_flags
obj-$(CONFIG_SEEKWAVE_SWT6621S_DRIVERS) += seekwaveplatform_lite/
obj-$(CONFIG_WLAN_VENDOR_SWT6621S) += swt6621s_wifi/
obj-$(CONFIG_SEEKWAVE_BT) += swtbt4l/
