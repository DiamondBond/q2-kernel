/****
   How to use
***/
# Wifi:
# insmod skw_sdio.ko
# insmod skw.ko

# BT:
# insmod skwbt.ko

Load interface:
# ls /sys/class/rfkill/
rfkill0  rfkill1

# cat /sys/class/rfkill/rfkill0/name
phy0
# cat /sys/class/rfkill/rfkill1/name
hci0

rfkill Power on interface:
# echo 1 >/sys/class/rfkill/rfkill1/state

FW Path:
# ls /lib/firmware/
EA6521QF_SEEKWAVE_R00005.bin  EA6621Q_SEEKWAVE_R00005.bin
EA6521QF_SEEKWAVE_R00005.ini  EA6621Q_SEEKWAVE_R00005.ini
EA6521QT_SEEKWAVE_R00005.bin  SWT6621_DRAM_SDIO.bin
EA6521QT_SEEKWAVE_R00005.ini  SWT6621_IRAM_SDIO.bin
EA6621QT_SEEKWAVE_R00005.bin  md5.txt
EA6621QT_SEEKWAVE_R00005.ini  sv6160.nvbin

BT Address:
/dev/skwbdaddr
Debug interface:
# ls
btsnoop-hci.cfa

/******* insmod log *****/
# insmod skw_sdio.ko
[   21.167939] ------->file=module_drivers/drivers/misc/seekwaveplatform/./sdio/skw_sdio_main.c.func=skw_sdio_io_init.line=1663
[   21.179645] =========== WLAN placed in NOMAL ON========
[   21.185055] reset 36 wlan power on:1
[   21.189285] =========== WLAN placed in RESET ON  ========
[   21.194891] reset 36 wlan power on:0
[   21.265197] ------->file=module_drivers/drivers/misc/seekwaveplatform/./sdio/skw_sdio_main.c.func=skw_sdio_io_init.line=1666
[   21.277800] [SKWSDIO INFO] skw_sdio_scan_card: sdio_scan_card
[   21.305417] mmc0: new high speed SDIO card at address 8800
[   21.326561] skw_sdio_probe: func->class=0, vendor=0x0000, device=0x0000, func_num=0x0001, clock=50000000 blksize=0x200 max_blkcnt 4096
[   21.375831] [SKWSDIO INFO] skw_sdio_probe: sdio_enable_func ret=0 type 0
[   21.382783] ****host_oob_irq_flags = 1,gpio_wl_host_wake =  -2
[   21.382789] ****wlan_regon_flags = 1,wl_regon =  36
[   21.435304] [SKWSDIO INFO] skw_sdio_probe: enable func1 done
[   21.446271] [SKWSDIO INFO] skw_sdio_scan_card: scan end!
[   21.451773] [SKWSDIO INFO] skw_sdio_io_init:  OK
[   21.458531] [SKWSDIO INFO] check_chipid: Chip id:SV6160 used SDIO10
[   21.525658] register char device:ATC 245:0
[   21.621531] register char device:LOG 245:1
[   21.645870] register char device:LOOPCHECK 245:7
[   21.689813] [SKWBOOT_ERR]:seekwave_boot_parse_dt:iram path fail ret=-22
[   21.744883] [SKWBOOT_ERR]:seekwave_boot_parse_dt: dram path fail ret=-22
[   21.778730] [SKWBOOT]:seekwave_boot_parse_dt, chipen:73 gpio_out:35 gpio_in:37 ret=-16
[   21.787315] [SKWBOOT]:firmware: SWT6621_DRAM_SDIO.bin SWT6621_IRAM_SDIO.bin
[   21.813113] [SKWBOOT]:boot data dram_img_data 953c7810
[   21.943850] [SKWBOOT]:image_size=498748,237540, ret=0
[   21.949160] [SKWBOOT]:skw_boot_init line:811,the tail_offset ---0x128, the head_offset --0xd4 ,iram_addr=0x100000,dram_addr=0x20200000,
[   21.962175] [SKWBOOT]:skw_boot_init line:815 analysis the img module
[   21.969003] [SKWBOOT]:skw_boot_init line:824 dl_addr=0x110000, write_addr=0x110000, index=0x1,data_size=0x2540
[   21.979701] [SKWBOOT]:skw_boot_init line:824 dl_addr=0x20200000, write_addr=0x20200000, index=0x1,data_size=0x8880
[   21.990669] [SKWBOOT]:skw_boot_init line:824 dl_addr=0x112600, write_addr=0x112600, index=0x2,data_size=0x3075c
[   22.001449] [SKWBOOT]:skw_boot_init line:824 dl_addr=0x2020a800, write_addr=0x2020a800, index=0x2,data_size=0x13958
[   22.012534] [SKWBOOT]:skw_boot_init line:824 dl_addr=0x143000, write_addr=0x143000, index=0x3,data_size=0x36c3c
[   22.023342] [SKWBOOT]:skw_boot_init line:824 dl_addr=0x20231e80, write_addr=0x20231e80, index=0x3,data_size=0x59a8
[   22.034317] [SKWBOOT]:skw_boot_init line:824 dl_addr=0x20239bdc, write_addr=0x2023fb00, index=0x3,data_size=0x408
[   22.045308] bus type: sdio
[   22.049156] register char device:BTBOOT 245:12
[   22.075734] [SKWSDIO INFO] skw_boot_loader: boot chip id 0x6160
[   22.105408] [SKWSDIO INFO] skw_sdio_set_dma_type: dma_type=1,adma_rx_enable:1
[   22.105408]
[   22.105460] [SKWSDIO INFO] skw_sdio_slp_feature_en: no-sleep_support_enable:0
[   22.105460]
[   22.114475] [SKWSDIO INFO] skw_sdio_boot_cp: DOWNLOAD BIN TO CP
[   22.295988] [SKWSDIO INFO] skw_sdio_boot_cp: line:1112 write the download done flag
[   22.725525] [SKWSDIO INFO] skw_sdio_handle_packet: LOOPCHECK channel received: trunk_W23.20.2-rev31536-rev31538-rev31359 20240807-01:06:38, slp=2
[   22.725525]
[   22.740612] [SKWSDIO INFO] skw_sdio_gpio_irq_pre_ops: cp_detect_sleep_mode is 1 or 2
[   22.748799] [SKWSDIO INFO] skw_sdio_host_irq_init: gpio_In:37,gpio_out:35 irq 77
[   22.756633] [SKWSDIO INFO] skw_sdio_bind_WIFI_driver:  wifi_pdata bus_type:0x0
[   22.766573] [SKWSDIO ERROR] skw_sdio_cp_service_ops: service not support!
[   22.773616] [SKWSDIO INFO] skw_boot_loader: boot loader ops end!!!
[   22.805382] [SKWBOOT]:skw_first_boot first boot pass
#
#
# insmod skw.ko
[   29.632023] [SKWIFI INFO] VERSION: 2.0.240805.6e0fb88 (5.10.186+)
[   29.638612] [SKWIFI INFO] skw_drv_probe: chip: 1, MAC: 00:00:00:00:00:00
[   29.646273] [SKWIFI DBG] skw_register_rx_callback: register cmd port: 5, data port bitmap: 0x40
[   29.655891] [SKWIFI DBG] skw_hw_xmit_init: dma: 0
[   29.661702] [SKWBOOT]:skw_start_wifi_service Enter cp_state =0
[   29.675302] [SKWBOOT]:skw_download_signal_ops line:718 download data ops done
[   29.682795] [SKWSDIO INFO] skw_WIFI_service_start: Enter STARTWIFI cp_state:0
[   29.697757] [SKWSDIO INFO] skw_sdio_cp_log: line:1179 disable the CP log !!
[   29.705010] [SKWSDIO INFO] send_modem_service_command: ret = 0 command 0
[   29.712253] [SKWSDIO INFO] skw_sdio_handle_packet: LOOPCHECK channel received: WIFIREADY
[   29.721036] [SKWSDIO INFO] skw_boot_loader: boot loader ops end!!!
[   29.727752] [SKWBOOT]:skw_start_wifi_service wifi boot sucessfull
[   29.735347] [SKWIFI WARN] skw_event_thermal_warn: active: 0
[   29.743366] [SKWIFI DBG] skw_sync_chip_info: efuse mac: 04:f4:d8:15:57:89, valid
[   29.751357] [SKWIFI DBG] skw_setup_mac_address: addr[0]: 04:f4:d8:15:57:89
[   29.758799] [SKWIFI DBG] skw_setup_mac_address: addr[1]: 06:f4:d8:17:57:89
[   29.766195] [SKWIFI DBG] skw_setup_mac_address: addr[2]: 06:f4:d8:11:57:89
[   29.773324] [SKWIFI DBG] skw_setup_mac_address: addr[3]: 06:f4:d8:1d:57:89
[   29.780765] [SKWIFI DBG] skw_calib_download: fw file: EA6521QF_SEEKWAVE_R00005.bin
[   29.789618] [SKWIFI DBG] skw_calib_bin_download: file size: 428
[   29.796099] [SKWIFI DBG] skw_calib_bin_download: bb_file remain: 0, seq: 0 len: 428 end: 1
[   29.806423] [SKWIFI DBG] skw_sync_band_capa: BW capa: 0xf, flags: 0x180
[   29.813286] [SKWIFI DBG] skw_sync_band_capa: BW capa: 0xf, flags: 0x180
[   29.822661] [SKWIFI INFO] skw_add_iface: STA, inst: 0, mac: 00:00:00:00:00:00, bitmap: 0x1
[   29.855347] [SKWIFI DBG] skw_iface_setup: STA, addr: 04:f4:d8:15:57:89
[   29.862142] [SKWIFI DBG] skw_cmd_open_dev: STA, inst: 0, mac: 04:f4:d8:15:57:89, flags: 0x0
[   29.879192] [SKWIFI DBG] skw_add_vif: iface: 0x0, bitmap: 0x1
#
#
# insmod skwbt.ko
[   33.573317] [SKWBT_INFO] Seekwave Bluetooth driver ver 0.1
[   33.579289] [SKWBT_INFO] btseekwave_probe pdev name LOOPCHECK
[   33.589920] [SKWBT_INFO] btseekwave_open enter...
[   33.594813] [SKWSDIO INFO] open_sdio_port: kworker/u3:1(873) open_sdio_port portno = 3
[   33.603423] [SKWBT_INFO] btseekwave_open mode data_port:2, audio_port:4
[   33.610557] [SKWSDIO INFO] open_sdio_port: kworker/u3:1(873) open_sdio_port portno = 2
[   33.619105] [SKWSDIO INFO] open_sdio_port: kworker/u3:1(873) open_sdio_port portno = 4
[   33.627599] [SKWBOOT]:skw_start_bt_service Enter cp_state =0
[   33.633559] [SKWBOOT]:skw_download_signal_ops line:718 download data ops done
[   33.641370] [SKWSDIO INFO] skw_BT_service_start: Enter cpstate=0
[   33.648819] [SKWBT_INFO] skwbdaddr init path:/dev//skwbdaddr
[   33.654890] [SKWSDIO INFO] skw_sdio_cp_log: line:1179 disable the CP log !!
[   33.662560] [SKWSDIO INFO] send_modem_service_command: ret = 0 command 0
[   33.679448] [SKWSDIO INFO] skw_sdio_handle_packet: LOOPCHECK channel received: BTREADY
[   33.687773] [SKWSDIO INFO] skw_boot_loader: boot loader ops end!!!
[   33.694177] [SKWBOOT]:skw_start_bt_service line:923 , boot bt sucessfully!
[   33.701719] [SKWBT_INFO] btseekwave_download_nv
[   33.703587] [SKWBT_INFO] btseekwave_rx_complete, chip version:0x17
[   33.708410] [SKWBT_INFO] init cmd response: 0x100101
[   33.726561] [SKWBT_INFO] init cmd response: 0xfc8001
[   33.733903] [SKWBT_INFO] init cmd response: 0xfc8001
[   33.739293] [SKWBT_INFO] btseekwave_setup
[   33.758761] [SKWBT_INFO] skwbd addr:22b3e6
# [   33.891003] [SKWBT_INFO] hci0
[   33.891025] [SKWBT_INFO] btseekwave_close enter...
[   33.899499] [SKWSDIO INFO] close_sdio_port: kworker/u3:1(state=1) portno = 3
[   33.907094] [SKWSDIO INFO] close_sdio_port: kworker/u3:1(state=1) portno = 2
[   33.914403] [SKWSDIO INFO] close_sdio_port: kworker/u3:1(state=1) portno = 4
[   33.922079] [SKWBOOT]:skw_stop_bt_service Enter cp_state =0
[   33.928191] [SKWSDIO INFO] skw_BT_service_stop: Enter cpstate=0
[   33.934412] [SKWSDIO INFO] send_modem_service_command: ret = 0 command 1
[   34.053997] [SKWSDIO INFO] skw_sdio_handle_packet: LOOPCHECK channel received: BTEXIT
[   34.062185] [SKWSDIO INFO] skw_boot_loader: boot loader ops end!!!
[   34.068615] [SKWBOOT]:seekwave boot stop done:skw_stop_bt_service

# echo 1 >/sys/class/rfkill/rfkill1/state 
# [   34.074925] [SKWBT_INFO] func btseekwave_close, atomic_read:0
[  298.437521] [SKWBT_INFO] btseekwave_open enter...
[  298.448457] [SKWSDIO INFO] open_sdio_port: kworker/u3:1(873) open_sdio_port portno = 3
[  298.457159] [SKWBT_INFO] btseekwave_open mode data_port:2, audio_port:4
[  298.464025] [SKWSDIO INFO] open_sdio_port: kworker/u3:1(873) open_sdio_port portno = 2
[  298.472600] [SKWSDIO INFO] open_sdio_port: kworker/u3:1(873) open_sdio_port portno = 4
[  298.481074] [SKWBOOT]:skw_start_bt_service Enter cp_state =0 
[  298.487267] [SKWBOOT]:skw_download_signal_ops line:718 download data ops done 
[  298.494755] [SKWSDIO INFO] skw_BT_service_start: Enter cpstate=0
[  298.503010] [SKWSDIO INFO] skw_sdio_cp_log: line:1179 disable the CP log !!
[  298.510635] [SKWSDIO INFO] send_modem_service_command: ret = 0 command 0
[  298.527145] [SKWSDIO INFO] skw_sdio_handle_packet: LOOPCHECK channel received: BTREADY
[  298.535476] [SKWSDIO INFO] skw_boot_loader: boot loader ops end!!!
[  298.541879] [SKWBOOT]:skw_start_bt_service line:923 , boot bt sucessfully!
[  298.549439] [SKWBT_INFO] btseekwave_download_nv
[  298.550257] [SKWBT_INFO] btseekwave_rx_complete, chip version:0x17
[  298.554972] [SKWBT_INFO] init cmd response: 0x100101 
[  298.572723] [SKWBT_INFO] init cmd response: 0xfc8001 
[  298.579933] [SKWBT_INFO] init cmd response: 0xfc8001 
[  298.587150] [SKWBT_INFO] init cmd response: 0xfc8201 

# hciconfig -a
hci0:   Type: Primary  Bus: SDIO
        BD Address: 56:6A:ED:22:B3:E6  ACL MTU: 1021:9  SCO MTU: 255:4
        UP RUNNING PSCAN 
        RX bytes:0 acl:0 sco:0 events:93 errors:0
        TX bytes:2023 acl:0 sco:0 commands:100 errors:0
        Features: 0xbf 0xee 0x0d 0xfe 0xdb 0xfd 0x7b 0x87
        Packet type: DM1 DM3 DM5 DH1 DH3 DH5 HV1 HV3 
        Link policy: RSWITCH SNIFF 
        Link mode: PERIPHERAL ACCEPT 
        Name: 'BlueZ 5.68'
        Class: 0x000000
        Service Classes: Unspecified
        Device Class: Miscellaneous, 
        HCI Version: 5.0 (0x9)  Revision: 0x17
        LMP Version: 5.0 (0x9)  Subversion: 0x17
        Manufacturer: not assigned (3149)


