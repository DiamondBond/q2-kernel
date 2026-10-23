# emu_common

## Overview

firmware和host driver有很多公用的结构和常量定义。

原先各自定义了一个副本，很容易出现不一致的问题，且难于维护。

现在将这些公共的定义，移到当前repo: emu_common。并且将其作为firmware或者host driver的submodule, 分别放置在如下目录：
- [emu_host_drv](http://192.168.100.52/wifi/emu_host_drv)/common
- [emu_dtop](http://192.168.3.36/gitlab/wifi/emu_dtop)/common
- [emu_wifi](http://192.168.3.36/gitlab/wifi/emu_wifi)/common
- [emu_bt](http://192.168.3.36/gitlab/wifi/emu_bt)/common

即原先emu_soc相同的位置。

## File Structure

目录结构按照模块及层次命名如下
- fw_api
  - non_wifi
    - bmi
    - hif
      - pcie
      - sdio
      - usb
    - mp
  - wifi
    - mac
    - phy

按照模块的功能主要分为两类：Wi-Fi和非Wi-Fi的。非Wi-Fi应该按照其类属，分布在bmi，hif和mp。

每个目录下均有一个默认的api.h。子目录首先包含上层目录（更大层级模块）的api.h。 以"fw_api/wifi/mac/api.h"为例。
```
#ifndef WQ_FW_WIFI_MAC_API_H_
#define WQ_FW_WIFI_MAC_API_H_

#include "fw_api/wifi/api.h"

/* WiFi MAC definitions */

#endif /* WQ_FW_WIFI_MAC_API_H_ */

```

也就"fw_api/wifi/api.h"，应该只包括MAC和PHY公共的定义。

## Principle

一个常量或structure放置在哪个文件，需要遵循下面的原则：

- 不应该出现重复的定义。即，在emu_common里，每个定义只会出现一次。
- **能放在子模块的定义，一定不要放到上一级**。
- 需要特别指出的是mp下应该只定义与产测相关的常量和结构。**不应该将主业务相关的定义，放到mp下**。

所有的project的include path仅仅到common这个层级。因此include要带相对路径。如： #include "fw_api/wifi/mac/api.h"。

在include哪个api文件，遵循相反的原则，**如果include上层api可以满足需要，绝不include下层的api文件**。

## SDK release

在SDK发布的时候，应该由SDK release script将不相干的目录删除。如：发布WiFi host driver应该将fw_api/bt删除。反之亦然，对于BT。

## Others
原则上，emu_common下的结构**不应该因为MACRO的定义不同而改变**，也就是要保持interface的稳定。

因此，我们对firmware中的一些宏定义进行了清理：
- NX_UMAC_PRESENT
  - 考虑到该宏为**始终定义**，删除无用的条件编译分支。
  - 对应的修改在emu_wifi的tag: [clean-NX_UMAC_PRESENT](http://192.168.3.36/gitlab/wifi/emu_wifi/-/tags/clean-NX_UMAC_PRESENT)
- NX_FULLY_HOSTED
  - 考虑到该宏为**始终未定义**，删除无用的条件编译分支。
  - 对应的修改在emu_wifi的tag：[clean-NX_FULLY_HOSTED](http://192.168.3.36/gitlab/wifi/emu_wifi/-/tags/clean-NX_FULLY_HOSTED)

