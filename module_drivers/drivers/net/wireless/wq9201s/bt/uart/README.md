## The example of platform adaption

* **Prerequisite**: Make sure CONFIG_SERIAL_DEV_BUS is enabled.
```
CONFIG_SERIAL_DEV_BUS:                                                                                                         
  │ Core support for devices connected via a serial port.                                                                       
  │ Note that you typically also want to enable TTY port controller support.                                                     
  │ Symbol: SERIAL_DEV_BUS [=y]                                                                                                 
  │ Type  : tristate                                                                                                             
  │ Defined at drivers/tty/serdev/Kconfig:5                                                                                     
  │   Prompt: Serial device bus                                                                                                 
  │   Location:                                                                                                                 
  │     -> Device Drivers                                                                                                       
  │       -> Character devices                                                                                                   
  │         -> Serial device bus (SERIAL_DEV_BUS [=y]) 

``` 

- **Platform**: Raspberry Pi Compute Module 4



1. modify the `arch/arm/boot/dts/overlays/uart2-overlay.dts` and add the "bluetooth" item to it as follows:

```
/{
        compatible = "brcm,bcm2711";

        fragment@0 {
                target = <&uart2>;
                __overlay__ {
                        status = "okay";
                        bluetooth {
                                compatible = "wuqi,my-bt-chip"; // This string must match the driver's ID table!
                                // Optional properties:
                                max-speed = <921600>; // Maximum supported baud rate
                                // If your chip requires hardware flow control (RTS/CTS)
                                // uart-has-rtscts; 
                        };
                };
        };

        fragment@1 {
                target = <&uart2_pins>;
                __dormant__ {
                        brcm,pins = <0 1 2 3>;
                        brcm,pull = <0 2 2 0>;
                };
        };

        __overrides__ {
                ctsrts = <0>,"=1";
        };
};

```

2. add `dtoverlay=uart2` to the end of /boot/firmware/config.txt

