# Ingenic JPEG V4L2 Driver

## 概述

本驱动是为Ingenic X2000系列芯片开发的基于V4L2框架的JPEG编解码驱动。相比原有的基于misc设备的驱动，该驱动提供了更好的兼容性和标准性，能够与用户空间的多媒体框架（如GStreamer）无缝集成。

## 架构设计

### 目录结构
```
ingenic-jpeg-v4l2/
├── Kconfig                 # 驱动配置选项
├── Makefile                # 编译配置文件
├── README.md               # 说明文档
├── ingenic_jpeg_v4l2.c     # 核心V4L2框架实现
├── ingenic_jpeg_v4l2.h     # 公共头文件
├── decoder/                # 解码器实现
│   ├── jpeg_decoder.c      # 解码器主逻辑
│   ├── jpeg_decoder.h      # 解码器头文件
│   ├── jpeg_decoder_hw.c   # 解码器硬件操作实现
│   └── jpeg_decoder_hw.h   # 解码器硬件操作头文件
├── encoder/                # 编码器实现
│   ├── jpeg_encoder.c      # 编码器主逻辑
│   ├── jpeg_encoder.h      # 编码器头文件
│   ├── jpeg_encoder_hw.c   # 编码器硬件操作实现
│   └── jpeg_encoder_hw.h   # 编码器硬件操作头文件
└── test/                   # 测试程序
    ├── jpeg_v4l2_test.c    # V4L2测试程序
    ├── generate_test_images.py  # 测试图片生成脚本 (Python)
    ├── generate_test_images.sh  # 测试图片生成脚本 (Bash)
    └── Makefile            # 测试程序编译配置
```

### 模块化设计

1. **核心框架** (`ingenic_jpeg_v4l2.c`):
   - V4L2设备注册和管理
   - 视频队列操作实现
   - 格式管理和控制接口

2. **解码器** (`decoder/`):
   - [jpeg_decoder.c](file://e:/manhattan-all/x2000/kernel/kernel-5.10/module_drivers/drivers/video/ingenic-jpeg-v4l2/decoder/jpeg_decoder.c): JPEG硬件解码器的具体实现
   - [jpeg_decoder_hw.c](file://e:/manhattan-all/x2000/kernel/kernel-5.10/module_drivers/drivers/video/ingenic-jpeg-v4l2/decoder/jpeg_decoder_hw.c): 解码器硬件寄存器操作实现
   - 解码中断处理
   - 解码任务调度

3. **编码器** (`encoder/`):
   - [jpeg_encoder.c](file://e:/manhattan-all/x2000/kernel/kernel-5.10/module_drivers/drivers/video/ingenic-jpeg-v4l2/encoder/jpeg_encoder.c): JPEG硬件编码器的具体实现
   - [jpeg_encoder_hw.c](file://e:/manhattan-all/x2000/kernel/kernel-5.10/module_drivers/drivers/video/ingenic-jpeg-v4l2/encoder/jpeg_encoder_hw.c): 编码器硬件寄存器操作实现
   - 编码中断处理
   - 编码任务调度

## 功能特性

1. **完整的V4L2支持**:
   - 支持内存到内存的编解码操作
   - 支持标准V4L2格式（JPEG, YUYV, NV12, NV21, RGB565, RGB24）
   - 支持V4L2控制接口（JPEG压缩质量等）

2. **硬件加速**:
   - 利用Ingenic X2000芯片的JPEG硬件编解码单元
   - 零拷贝DMA传输
   - 中断驱动的操作完成通知

3. **电源管理**:
   - 时钟自动管理（启用/禁用）
   - 设备挂起/恢复支持

## 配置选项

```
CONFIG_INGENIC_JPEG_V4L2        # 主开关
CONFIG_INGENIC_JPEG_V4L2_DEC    # 解码器支持
CONFIG_INGENIC_JPEG_V4L2_ENC    # 编码器支持
```

## 编译和安装

在内核源码根目录执行:
```bash
make modules
```

或者只编译特定模块:
```bash
make M=module_drivers/drivers/video/ingenic-jpeg-v4l2
```

安装模块:
```bash
make modules_install
```

## 使用方法

### 设备节点
驱动加载后会创建以下设备节点:
- `/dev/videoX` (X为分配的设备号) 用于JPEG解码器
- `/dev/videoY` (Y为分配的设备号) 用于JPEG编码器

### 应用编程接口
用户空间程序可以通过标准的V4L2接口与驱动交互:
1. 打开设备节点
2. 查询和设置格式
3. 请求和分配缓冲区
4. 队列缓冲区并启动流处理
5. 处理完成的缓冲区

### 示例代码
```c
// 打开设备
int fd = open("/dev/video0", O_RDWR);

// 设置输入格式 (用于解码)
struct v4l2_format fmt = {0};
fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
fmt.fmt.pix.width = 640;
fmt.fmt.pix.height = 480;
fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG;
ioctl(fd, VIDIOC_S_FMT, &fmt);

// 设置输出格式 (用于解码)
fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
ioctl(fd, VIDIOC_S_FMT, &fmt);

// 其余操作遵循标准V4L2流程
```

### 测试程序

在[test](file://e:/manhattan-all/x2000/kernel/kernel-5.10/module_drivers/drivers/video/ingenic-jpeg-v4l2/test)目录中提供了一个测试程序[jpeg_v4l2_test.c](file://e:/manhattan-all/x2000/kernel/kernel-5.10/module_drivers/drivers/video/ingenic-jpeg-v4l2/test/jpeg_v4l2_test.c)，可用于测试驱动的基本功能。

#### 编译测试程序
```bash
cd test
make
```

#### 使用测试程序
```bash
# JPEG编码测试 (从NV12 raw数据编码为JPEG)
./jpeg_v4l2_test /dev/video0 input.nv12 output.jpg encode 640 480

# JPEG解码测试 (从JPEG解码为NV12 raw数据)
./jpeg_v4l2_test /dev/video1 input.jpg output.nv12 decode
```

### 测试图片生成

为了方便测试，提供了两个脚本用于生成测试图片：

#### Python脚本 (需要安装numpy和PIL)
```bash
cd test
python3 generate_test_images.py
```

#### Bash脚本 (需要安装ImageMagick)
```bash
cd test
chmod +x generate_test_images.sh
./generate_test_images.sh
```

这些脚本会在[test_images](file://e:/manhattan-all/x2000/kernel/kernel-5.10/module_drivers/drivers/video/ingenic-jpeg-v4l2/test/test_images)目录中生成多种尺寸和模式的测试图片，包括：
- PNG格式的RGB图像（可用于查看）
- NV12格式的原始数据（可用于编码测试）
- JPEG格式的压缩图像（可用于解码测试）

## 技术细节

### 中断处理
- 解码器和编码器分别有独立的中断处理函数
- 中断处理程序负责检测操作完成和错误状态
- 使用completion机制同步操作完成状态

### 缓冲区管理
- 使用videobuf2框架管理DMA缓冲区
- 支持零拷贝数据传输
- 自动处理缓冲区队列和调度

### 电源管理
- 驱动自动管理硬件时钟
- 设备空闲时自动关闭时钟以节省功耗
- 支持系统挂起和恢复操作

## 已知限制

1. 仅支持Ingenic X2000系列芯片
2. 需要设备树中正确配置硬件节点
3. 当前仅支持有限的像素格式

## 未来改进

1. 增加更多像素格式支持
2. 支持更多JPEG参数控制
3. 添加性能统计和调试功能
4. 增强错误处理和恢复机制