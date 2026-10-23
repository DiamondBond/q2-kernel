gt1x 设备树配置示例
=========

```dtsi
&i2c0{
    status = "okay";
    clock-frequency = <100000>;
    pinctrl-names = "default";
    pinctrl-0 = <&i2c0_pc>;

    goodix_ts@5d {
        compatible = "goodix,gt1x";
        reg = <0x5d>;
        interrupt-parent = <&gpc>;
        interrupts = <IRQ_SSI0>;
        goodix,reset-gpio = <&gpc 7 GPIO_ACTIVE_LOW INGENIC_GPIO_NOBIAS>;
        goodix,irq-gpio = <&gpc 8 IRQ_TYPE_EDGE_FALLING INGENIC_GPIO_NOBIAS>;
    };
};

```

