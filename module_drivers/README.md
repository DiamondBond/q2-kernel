# 君正(Ingenic)平台私有驱动

本目录包含君正XBurst系列芯片的专属外设驱动及设备树配置，按照每个驱动基于的框架，以内核的目录结构展开，独立存放在该module_drivers目录，每个外设驱动独立成模块，按内核标准框架（如IIO、Input、PWM等）实现，通用功能直接复用内核原生驱动.



## 目录结构说明

| 文件名称  | 文件说明                             |
| --------- | ------------------------------------ |
| Kconfig   | 内核模块配置选项文件                 |
| Makefile  | 模块编译的Makefile规则               |
| drivers   | 君正核心设备驱动代码目录             |
| dts       | 旧版设备树源文件(历史遗留，逐步迁移) |
| dts-stage | 新版规范化设备树(主维护版本)         |
| include   | 驱动公共头文件                       |
| sound     | 君正音频子系统驱动                   |



## 设备树(dts/dts-stage)说明

### 历史背景

- `dts/` 目录是长期维护的设备树文件集合
- 随着芯片平台增多，出现文件命名不规范、结构混乱的问题

### 新旧版本对比

| 特性         | dts-stage (新版)   | dts (旧版)     |
| ------------ | ------------------ | -------------- |
| **组织结构** | 按芯片系列型号分级 | 扁平化混合存放 |
| **文件命名** | 标准化命名         | 历史遗留命名   |
| **维护策略** | 主维护分支         | 仅关键补丁维护 |
| **使用场景** | 新项目强制使用     | 存量项目兼容   |

### 迁移计划

1. **过渡期**：
   - 保持两个目录并行维护
   - 对旧版dts只做关键性修复
2. **长期规划**：
   - 新项目必须使用dts-stage
   - 逐步迁移旧项目到新版设备树



### 新版设备树文件命名规范

- **芯片基础定义**:`dts-stage/<SOC>`

  (示例: `dts-stage/x2000`)

- **芯片核心配置**：`<SOC>.dtsi`
  (示例：`dts-stage/x2000/x2000.dtsi`)

- **板级配置**：`<BOARD>_<VERSION>.dts`
  (示例：`dts-stage/x2000/halley5_v30.dts`)

- **外设模块**：`<BOARD>_<FUNCTION>_<MODEL>.dtsi`
  (示例：`dts-stage/x2000/halley5_lcd/HALLEY5_MIPI_LCD_FW050.dtsi`)

- **dts编译配置项**:`DT_<SOC>_<BOARD>_<VERSION>`

  (示例：`CONFIG_DT_X2000_HALLEY5_V30`)

## 如何嵌入到内核源码编译

1. 内核源码顶层Kconfig添加

```
config OUTSIDE_DRIVERS
	string
	option env="OUTSIDE_DRIVERS"
	default "module_drivers"


source "$OUTSIDE_DRIVERS/Kconfig"
```

**注**：Kconfig 定义尽量清晰，贴近SOC本身，只显示SOC支持的控制器驱动.

2. 内核Makefile添加

```
drivers-y	+= module_drivers/
```

