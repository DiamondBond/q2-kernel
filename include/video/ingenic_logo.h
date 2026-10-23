#ifndef __LOGO_H__
#define __LOGO_H__

/*
 * External logo blobs reuse the serialized p8 field to carry a magic
 * signature. The runtime logo_info object still repurposes p8 as a pointer.
 */
#define INGENIC_LOGO_MAGIC 0x4c4f474fUL

struct _logo_info {
	int width;
	int height;
	int bpp;
	unsigned char *p8;
	unsigned int background_color;
} __attribute__ ((packed));

extern struct _logo_info logo_info;
extern unsigned char logo_buf_initdata[];

#endif /* __LOGO_H__ */
