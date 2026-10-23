#
# linux/arch/mips/boot/compressed/Makefile
#
# create a compressed zImage from the original vmlinux
#

targets		:= zImage uImage vmlinuz vmlinux.bin.gz vmlinux.bin.zst head.o misc.o piggy.o dummy.o

OBJS 		:= $(obj)/head.o $(obj)/misc.o

ifdef CONFIG_LOADADDR
LOADADDR=$(CONFIG_LOADADDR)
else
    LOADADDR=0x80010000
endif
ifneq ($(CONFIG_XIMAGE_LDADDR),0)
    XIMAGE_LOADADDR=$(CONFIG_XIMAGE_LDADDR)
else
    XIMAGE_LOADADDR=0x80F00000
endif


LD_ARGS 	:= -m elf32ltsmip -T $(srctree)/$(src)/ld.script -Ttext $(XIMAGE_LOADADDR) -Bstatic -EL
OBJCOPY_ARGS 	:= -O elf32-tradlittlemips

ENTRY 		:= $(srctree)/$(src)/entry
FILESIZE 	:= $(srctree)/$(src)/filesize

KBUILD_CFLAGS := $(shell echo $(KBUILD_CFLAGS) | sed -e "s/-pg//")
KBUILD_CFLAGS := $(filter-out -fstack-protector -fno-stack-check -fstack-protector-strong, $(KBUILD_CFLAGS))
KBUILD_CFLAGS := $(LINUXINCLUDE) $(KBUILD_CFLAGS) -D__KERNEL__ \
       -D__DISABLE_EXPORTS \
       -DBOOT_HEAP_SIZE=$(BOOT_HEAP_SIZE) -D"VMLINUX_LOAD_ADDRESS_ULL=$(VMLINUX_LOAD_ADDRESS)ull"

suffix-y			:= gz
suffix-$(CONFIG_KERNEL_ZSTD)	:= zst
uimage-comp-y			:= gzip
uimage-comp-$(CONFIG_KERNEL_ZSTD)	:= zstd
zcompressed-image		:= $(obj)/vmlinux.bin.$(suffix-y)

drop-sections	= .reginfo .mdebug .comment .note .pdr .options .MIPS.options
strip-flags	= $(addprefix --remove-section=,$(drop-sections))


$(obj)/vmlinux.bin: vmlinux
	$(OBJCOPY) -O binary $(strip-flags) vmlinux $(obj)/vmlinux.bin

$(obj)/vmlinux.bin.gz: $(obj)/vmlinux.bin FORCE
	rm -f $@
	$(KGZIP) -n -f -9 -c $< > $@

$(obj)/vmlinux.bin.zst: $(obj)/vmlinux.bin FORCE
	rm -f $@
	$(ZSTD) -16 --no-check -q -f $< -o $@

$(obj)/head.o: $(obj)/head.S $(zcompressed-image) vmlinux
	echo $(CC)
	$(CC) $(KBUILD_AFLAGS) $(a_flags) \
	-DIMAGESIZE=$(shell sh $(FILESIZE) $(zcompressed-image)) \
	-DKERNEL_ENTRY=$(shell sh $(ENTRY) $(NM) vmlinux ) \
	-DLOADADDR=$(LOADADDR) \
	-I./include \
	-c -o $(obj)/head.o $<

$(obj)/vmlinuz: $(OBJS) $(srctree)/$(src)/ld.script $(zcompressed-image) $(obj)/dummy.o
	$(OBJCOPY) \
		--add-section=.image=$(zcompressed-image) \
		--set-section-flags=.image=contents,alloc,load,readonly,data \
		$(obj)/dummy.o $(obj)/piggy.o
	$(LD) $(LD_ARGS) -o $@ $(OBJS) $(obj)/piggy.o
	$(OBJCOPY) $(OBJCOPY_ARGS) $@ $@ -R .comment -R .stab -R .stabstr -R .initrd -R .sysmap

zImage: $(obj)/vmlinuz
	$(OBJCOPY) -O binary $(obj)/vmlinuz $(obj)/zImage
	@rm -f $(obj)/vmlinuz

vmlinux.bin: $(obj)/vmlinux.bin

uImage: $(VMLINUX) $(zcompressed-image)
	mkimage -A mips -O linux -T kernel -C $(uimage-comp-y) \
		-a $(LOADADDR) -e $(shell sh $(srctree)/$(src)/tools/entry $(NM) $(VMLINUX) ) \
		-n 'Linux-$(KERNELRELEASE)' \
		-d $(zcompressed-image) $(obj)/uImage
xImage: zImage
	mkimage -A mips -O linux -T kernel -C none \
		-a $(XIMAGE_LOADADDR) -e $(XIMAGE_LOADADDR) \
		-n 'Linux-$(KERNELRELEASE)' \
		-d $(obj)/zImage $(obj)/xImage
	@rm -f $(obj)/zImage
