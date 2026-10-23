KERNELDIR ?= /lib/modules/$(shell uname -r)/build

# default
ifdef h5
MODULE_NAME := wq_uart_h5
SRC_FILES := wq_h5_core.c wq_hci_serdev.c
OBJ_FILES := $(SRC_FILES:.c=.o)
endif

# 检查是否有 h4 参数
#ifdef h4
  MODULE_NAME := wq_uart_h4
  SRC_FILES := wq_h4_core.c
  OBJ_FILES := $(SRC_FILES:.c=.o)
#endif

# 目标模块文件名
TARGET = $(MODULE_NAME).ko

# 头文件路径
EXTRA_CFLAGS += -I$(PWD)/include

# 内核模块构建目标
obj-m := $(MODULE_NAME).o
$(MODULE_NAME)-y := $(OBJ_FILES)

# 默认构建目标
all:
	-@echo "\n===========BT UART driver building============\n";
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

# 清理构建文件
clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

# 安装模块
install: all
	sudo insmod $(TARGET)

# 卸载模块
uninstall:
	sudo rmmod $(MODULE_NAME) || true

# 重新加载模块
reload: uninstall install

# 显示帮助信息
help:
	@echo "可用目标:"
	@echo "  all        - 编译模块 (默认: wq_uart_h5.ko)"
	@echo "  clean      - 清理构建文件"
	@echo "  install    - 安装模块"
	@echo "  uninstall  - 卸载模块"
	@echo "  reload     - 重新加载模块"
	@echo ""
	@echo "选项:"
	@echo "  h4=1       - 编译 H4 版本模块 (wq_uart_h4.ko)"
	@echo ""
	@echo "示例:"
	@echo "  make              # 编译 H5 版本"
	@echo "  make h4=1         # 编译 H4 版本"
	@echo "  make h4=1 install # 编译并安装 H4 版本"

.PHONY: all clean install uninstall reload help

distclean:
	-find . -name "*.o" -exec rm {} \;
	-find . -name "*.orig" -exec rm {} \;
	-find . -name "*.swp" -exec rm {} \;
	-find . -name "*.*~" -exec rm {} \;
	-find . -name "*~" -exec rm {} \;
	-find . -name "*.d" -exec rm {} \;
	-find . -name "*.a" -exec rm {} \;
	-find . -name "tags" -exec rm {} \;
	-find . -name ".*" -exec rm -rf 2> /dev/null \;
	-find . -name "*.ko" -exec rm {} \;
	-find . -name ".*.cmd" -exec rm {} \;
	-find . -name "*.mod.c" -exec rm {} \;
	-find . -name ".*.dwo" -exec rm {} \;
	-find . -name "*dwo" -exec rm {} \;
	-rm -rf .tmp_versions
