# Stock Q2 device tree (firmware V9.1S)

The source tree in this repository is Ingenic's generic 5.10.186 SDK with an
X2600 `.config`. It is not the kernel the Q2 runs. These files come from
the kernel that actually ships on the Q2:

- `q2-v9.1S.dtb`: the built-in device tree, carved from
  `recovery-update/xImage` in the stock V9.1S `update.tar` (gzip payload at
  offset 0x4260, FDT at 0xb6cba0 of the decompressed image).
- `q2-v9.1S.dts`: the same blob decompiled with `dtc -I dtb -O dts`.

Stock kernel: `Linux version 5.10.21+ (chenyou@server1-dell)
(mips-linux-gnu-gcc (Codescape GNU Tools 2020.06-01 for MIPS MTI Linux) 9.3.0)
#251 SMP PREEMPT Mon Aug 10 18:06:05 CST 2026`, Ingenic X2000.

The device tree has no nodes for the screen, wheel, buttons or DAC. Those
are set up by loadable modules in the rootfs `/module_driver/`, and their
source is not in this repository:

codec_cs43131, x2000_510_cs4308p_sound_card, hyn_slider, hyn_touch,
keyboard_gpio_add, lcd_h0209s, cw2015_driver, rtl8733bs, hci_uart_h5,
rmem_manager, utils, soc_adc, soc_aic, soc_audio_dma, soc_fb,
soc_fb_layer_mixer, soc_gpio, soc_i2c, soc_mcu, soc_msc, soc_pwm,
soc_rotator, soc_utils, soc_watchdog.

The AXP2101 PMIC driver is built into the stock kernel and is also missing.
