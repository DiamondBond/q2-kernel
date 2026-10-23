
需要打印信息，在Makefile配置CONFIG_DEBUG = y
打印信息在/os/zt_debug.h  中配置；
当前CONFIG_DEBUG = n;打印　ZT_DEBUG_LEVEL为ZT_DEBUG_DEBUG；可配置为以下几种：
	ZT_DEBUG_DEBUG
	ZT_DEBUG_INFO
	ZT_DEBUG_WARN
	ZT_DEBUG_ERROR
	ZT_DEBUG_MASK

wext和nl80211在Makefile配置CONFIG_WIFI_FRAMEWORK
make clean
make

// 驱动加载
insmod zt9101_ztopmac.ko cfg=/lib/firmware/wifi.cfg  //　hif.c驱动中已配置wifi.cfg路径也可以直接insmod zt9101_ztopmac.ko
若Makefile里CONFIG_WIFI_FRAMEWORK配置为nl80211时insmod error出现Unknown symbol in module,
请先执行modprobe cfg80211

加载驱动出现firmware open failed，请确保wifi.cfg里
“fw=/lib/firmware/ZT9101_fw_7128e8e.bin
fw1=/lib/firmware/ZT9101V30_fw_8546b3a.bin ”固件信息正确

// 驱动卸载
rmmod zt9101_ztopmac


//sdio接口
insmod 后，ifconfig -a 会有wlan 、p2p 节点；运行wifi_up.sh　即可配置网络成功；

//usb接口
配置dts中otg 为host模式；kernel-menuconfig选中USB_DWC2_FORCE_FULL_SPEED [=y]; USB_DWC2_EXT_VBUS_DETECT [=y];
启动后拔掉usb　线，此时lsusb 能识别到PID 为 9101,VID 为 350B 的设备；
insmod .ko 文件，ifconfig -a 会有wlan 、p2p 节点；运行wifi_up.sh　即可配置网络成功；

------------------------------------------------------------------------------------------------------------------
dts config:

fwang@soft:~/x2670-kernel5.10/kernel/kernel-5.10$ git diff module_drivers/dts/x2670_halley_v1.0.dts
diff --git a/module_drivers/dts/x2670_halley_v1.0.dts b/module_drivers/dts/x2670_halley_v1.0.dts
index 212aaca50..4794525f7 100644
--- a/module_drivers/dts/x2670_halley_v1.0.dts
+++ b/module_drivers/dts/x2670_halley_v1.0.dts
@@ -22,8 +22,8 @@
        wlreg_on: wlreg-on {
                  compatible = "regulator-fixed";
                  regulator-name = "wlreg_on";
-                 gpio = <&gpc 6 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>;
-                 enable-active-low;
+                 gpio = <&gpc 6 GPIO_ACTIVE_HIGH INGENIC_GPIO_NOBIAS>;
+                 enable-active-high;
        };
 #endif
        reserved-memory {
@@ -80,7 +80,7 @@
 };

 &otg {
-       dr_mode = "otg"; // host,peripheral,otg
+       dr_mode = "host"; // host,peripheral,otg
        device-using-dma = <1>;
        status = "okay";
 };
@@ -135,12 +135,21 @@
        ingenic,removal-manual; /*removal-dontcare, removal-nonremovable, removal-removable, removal-manual*/

 #ifndef CONFIG_RTL8723DS
+/*
        bcmdhd_wlan: bcmdhd_wlan {
                 compatible = "android,bcmdhd_wlan";
                 wl_vccio-supply = <&wl_vccio>;
                 ingenic,sdio-irq = <&gpd 12 IRQ_TYPE_LEVEL_HIGH INGENIC_GPIO_NOBIAS>;
                 ingenic,sdio-reset = <&gpc 0 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>;
        };
+*/
+       ZT9101: ZT9101{
+                compatible = "zt,zt9101_wlan";
+                wlreg_on-supply = <&wlreg_on>;
+                ingenic,sdio-irq = <&gpd 12 IRQ_TYPE_LEVEL_HIGH INGENIC_GPIO_NOBIAS>;
+                ingenic,sdio-reset = <&gpc 0 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>;
+       };

