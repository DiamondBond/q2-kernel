obj-$(CONFIG_INGENIC_JPEG_V4L2_DEC) += ingenic-jpeg-dec.o
obj-$(CONFIG_INGENIC_JPEG_V4L2_ENC) += ingenic-jpeg-enc.o

ingenic-jpeg-dec-objs := ingenic_jpeg_v4l2.o decoder/jpeg_decoder.o decoder/jpeg_decoder_hw.o
ingenic-jpeg-enc-objs := ingenic_jpeg_v4l2.o encoder/jpeg_encoder.o encoder/jpeg_encoder_hw.o

ccflags-y += -I$(src)/../ingenic-jpeg