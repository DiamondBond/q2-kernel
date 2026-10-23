
固件位置：sdk/external/wifi-ble/wifi/wifi_88W8987/
顶层编译：install_wifi_88w8987_firmware　将固件全部install  /out/product/halley6.v10_nand_5.10-eng/system/lib/firmware/下；
顶层make post-image 打包到文件系统中，烧录文件系统即可；


  加载wifi模块；
make kernel-menuconfig 选中

Symbol: WIRELESS_88W8987 [=m]                                                                                                                                                    │
  │ Type  : tristate                                                                                                                                                                 │
  │ Defined at module_drivers/drivers/net/wireless/88W8987/Kconfig:1                                                                                                                 │
  │   Prompt: nxp 88W8987 SDIO WiFi                                                                                                                                                  │
  │   Location:                                                                                                                                                                      │
  │     -> Ingenic device-drivers Configurations                                                                                                                                     │
  │       -> [WIFI]  Drivers

  模块化编译：
make kernel-modules

生成：	out/product/halley6.v10_nand_5.10-eng/obj/kernel-intermediate/module_drivers/drivers/net/wireless/88W8987/moal.ko
	out/product/halley6.v10_nand_5.10-eng/obj/kernel-intermediate/module_drivers/drivers/net/wireless/88W8987/mlan.ko
把两个.ko文件拷贝到串口的/lib/firmware/下；

cd /lib/firmware
insmod mlan.ko
insmod moal.ko mod_para=nxp/wifi_mod_para.conf

　加载成功后，生成节点：
# ifconfig -a
lo        Link encap:Local Loopback
          inet addr:127.0.0.1  Mask:255.0.0.0
          UP LOOPBACK RUNNING  MTU:65536  Metric:1
          RX packets:12 errors:0 dropped:0 overruns:0 frame:0
          TX packets:12 errors:0 dropped:0 overruns:0 carrier:0
          collisions:0 txqueuelen:1000
          RX bytes:1344 (1.3 KiB)  TX bytes:1344 (1.3 KiB)

mlan0     Link encap:Ethernet  HWaddr D4:53:83:C4:D9:14
          UP BROADCAST MULTICAST  MTU:1500  Metric:1
          RX packets:152 errors:0 dropped:0 overruns:0 frame:0
          TX packets:140 errors:0 dropped:0 overruns:0 carrier:0
          collisions:0 txqueuelen:1000
          RX bytes:17015 (16.6 KiB)  TX bytes:13594 (13.2 KiB)

uap0      Link encap:Ethernet  HWaddr D6:53:83:C4:DA:14
          BROADCAST MULTICAST  MTU:1500  Metric:1
          RX packets:0 errors:0 dropped:0 overruns:0 frame:0
          TX packets:0 errors:0 dropped:0 overruns:0 carrier:0
          collisions:0 txqueuelen:1000
	  RX bytes:0 (0.0 B)  TX bytes:0 (0.0 B)

wfd0      Link encap:Ethernet  HWaddr D6:53:83:C4:D9:14
          BROADCAST MULTICAST  MTU:1500  Metric:1
          RX packets:0 errors:0 dropped:0 overruns:0 frame:0
          TX packets:0 errors:0 dropped:0 overruns:0 carrier:0
          collisions:0 txqueuelen:1000
          RX bytes:0 (0.0 B)  TX bytes:0 (0.0 B)

在etc/wpa_supplicant.conf 配置wifi名和密码；
eg:
# cat etc/wpa_supplicant.conf
#ctrl_interface=/var/run/wpa_supplicant
#ap_scan=1

#network={
#  key_mgmt=NONE
ctrl_interface=/var/run/wpa_supplicant
update_config=1
country=GB"

network={
    ssid="WF"
    psk="asdfghjkl"
    bssid=
    priority=1

}
执行wifi_up.sh
------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------

dts:

&msc1 {
        status = "okay";
        pinctrl-names ="default", "enable", "disable";
        pinctrl-0 = <&msc1_4bit> , <&rtc32k_disable>;
        pinctrl-1 = <&rtc32k_enable>;
        pinctrl-2 = <&rtc32k_disable>;

        cap-sd-highspeed;
        max-frequency = <50000000>;
        bus-width = <4>;
        voltage-ranges = <1800 3300>;
        non-removable;

        ingenic,sdio_clk = <1>;
        keep-power-in-suspend;

        /* special property */
        ingenic,wp-gpios = <0>;
        ingenic,cd-gpios = <0>;
        ingenic,rst-gpios = <0>;
        ingenic,removal-manual; /*removal-dontcare, removal-nonremovable, removal-removable, removal-manual*/
        wlan: wlan {
                 status = "okay";
                 compatible = "nxp,wlan";
                 ingenic,sdio-irq = <&gpb 3 IRQ_TYPE_LEVEL_HIGH INGENIC_GPIO_NOBIAS>;
                 ingenic,sdio-reset = <&gpb 4 GPIO_ACTIVE_HIGH INGENIC_GPIO_NOBIAS>;
         };
};


