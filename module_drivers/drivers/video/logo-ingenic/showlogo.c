#include <linux/fb.h>
#include <linux/string.h>
#include <video/ingenic_logo.h>

void show_logo(struct fb_info *info)
{
	unsigned int i, j, di, dj;
	int w, h, bpp;
	int blank_left, blank_top;
	void *buf;
	unsigned int *p32;
	unsigned int *p_logo;
	struct fb_var_screeninfo *var = &info->var;
	unsigned char *screen_base = info->screen_base;
	struct _logo_info *pinfo = &logo_info;

	w = var->xres;
	h = var->yres;
	bpp = info->var.bits_per_pixel;
	if ((w < pinfo->width) || (h < pinfo->height)) {
		printk("\033[33mERROR: LOGO shouldn't bigger than panel! skip.\033[0m\n");
		return;
	}

	buf = screen_base;
	if (pinfo->bpp == bpp) {
		blank_left = (w - pinfo->width) / 2;
		blank_top = (h - pinfo->height) / 2;
		if (bpp == 32) {
			if (blank_top) {
				p32 = (unsigned int *)buf;
				for (i = 0; i < blank_top * w; ++i) {
					*(p32 + i) = pinfo->background_color;
				}
				p32 = (unsigned int *)buf + (blank_top + pinfo->height) * w;
				for (i = 0; i < (h - pinfo->height - blank_top) * w; ++i) {
					*(p32 + i) = pinfo->background_color;
				}
			}
			if (blank_left) {
				for (i = 0; i < pinfo->height; ++i) {
					p32 = (unsigned int *)buf + (blank_top + i) * w;
					for (j = 0; j < blank_left; ++j) {
						*(p32 + j) = pinfo->background_color;
					}
					p32 = (unsigned int *)buf + (blank_top + i) * w + pinfo->width + blank_left;
					for (j = 0; j < w - pinfo->width - blank_left; ++j) {
						*(p32 + j) = pinfo->background_color;
					}
				}
			}
			for (i = 0; i < pinfo->height; ++i) {
				p32 = (unsigned int *)buf + (blank_top + i) * w + blank_left;
				p_logo = (unsigned int*)pinfo->p8 + i * pinfo->width;
				for (j = 0; j < pinfo->width; ++j) {
					*(p32 + j) = *(p_logo + j);
				}
			}
		} else if (bpp == 16) {
			unsigned int background_color = pinfo->background_color << 16 | pinfo->background_color;

			printk("\033[31m h = %d | blank_top = %d | pinfo->height = %d |\033[0m\n", h, blank_top, pinfo->height);
			if (blank_top) {
				int tmp = blank_top * ((w + 1) / 2);
				p32 = (unsigned int *)buf;
				for (i = 0; i < tmp; i++)
					*(p32++) = background_color;

				tmp = (h - blank_top - pinfo->height) * ((w + 1) / 2);
				p32 = (unsigned int *)buf + (blank_top + pinfo->height) * ((w + 1) / 2);
				for (i = 0; i < tmp; i++)
					*(p32++) = background_color;
			}

			if (blank_left) {
				int tmp;
				for (i = 0; i < pinfo->height; ++i) {
					tmp = (blank_left + 1) / 2;
					p32 = (unsigned int *)buf + (blank_top + i) * ((w + 1) / 2);
					for (j = 0; j < tmp; ++j) {
						*(p32 + j) = background_color;
					}

					p32 = (unsigned int *)buf + (blank_top + i) * ((w + 1) / 2) + (pinfo->width + blank_left) / 2;
					for (j = 0; j < tmp; ++j) {
						*(p32 + j) = background_color;
					}
				}
			}
			if (pinfo->width%2) {
				for (i = 0; i < pinfo->height; ++i) {
					p32 = (unsigned int *)buf + (blank_top + i) * ((w + 1) / 2) + blank_left / 2;
					p_logo = (unsigned int*)pinfo->p8 + i * pinfo->width/2;
					for (j = 0; j < pinfo->width/2; ++j) {
						*(p32 + j) = *(p_logo + j);
					}
					*(unsigned short*)(p32 + j) = *(unsigned short*)(p_logo + j);
				}
			} else {
				for (i = 0; i < pinfo->height; ++i) {
					p32 = (unsigned int *)buf + (blank_top + i) * ((w + 1) / 2) + blank_left / 2;
					p_logo = (unsigned int*)pinfo->p8 + i * pinfo->width/2;
					for (j = 0; j < pinfo->width/2; ++j) {
						*(p32 + j) = *(p_logo + j);
					}
				}
			}
		}
	} else if (pinfo->bpp == 32 && bpp == 12) {
		/* BGRA logo -> NV12 framebuffer conversion */
		unsigned char *y_plane = (unsigned char *)screen_base;
		unsigned char *uv_plane;
		unsigned char *logo_pixels = pinfo->p8;
		int y_stride = w;
		int logo_w = pinfo->width;
		int logo_h = pinfo->height;
		unsigned char bg_r, bg_g, bg_b;
		int bg_y, bg_u, bg_v;
		int y_val, u_val, v_val;
		unsigned char b, g, r;
		int u_sum, v_sum, count;

		blank_left = (w - logo_w) / 2;
		blank_top = (h - logo_h) / 2;
		uv_plane = y_plane + y_stride * h;

		/* Background color (BGRA byte order) */
		bg_b = pinfo->background_color & 0xff;
		bg_g = (pinfo->background_color >> 8) & 0xff;
		bg_r = (pinfo->background_color >> 16) & 0xff;

		/* BT.601 BGRA -> YUV */
		bg_y = ((66 * bg_r + 129 * bg_g + 25 * bg_b + 128) >> 8) + 16;
		bg_u = ((-38 * bg_r - 74 * bg_g + 112 * bg_b + 128) >> 8) + 128;
		bg_v = ((112 * bg_r - 94 * bg_g - 18 * bg_b + 128) >> 8) + 128;
		bg_y = bg_y < 0 ? 0 : (bg_y > 255 ? 255 : bg_y);
		bg_u = bg_u < 0 ? 0 : (bg_u > 255 ? 255 : bg_u);
		bg_v = bg_v < 0 ? 0 : (bg_v > 255 ? 255 : bg_v);

		printk(KERN_INFO "logo: BGRA->NV12 conversion, w=%d h=%d logo=%dx%d\n",
		       w, h, pinfo->width, pinfo->height);

		/* Fill Y plane with background */
		memset(y_plane, (unsigned char)bg_y, y_stride * h);

		/* Fill UV plane with background (V,U interleaved - hardware order) */
		for (i = 0; i < h / 2; i++) {
			unsigned char *uv_line = uv_plane + i * y_stride;
			for (j = 0; j < w / 2; j++) {
				uv_line[j * 2]     = (unsigned char)bg_v;
				uv_line[j * 2 + 1] = (unsigned char)bg_u;
			}
		}

		/* Draw logo Y plane */
		for (i = 0; i < logo_h; i++) {
			int screen_y = blank_top + i;
			unsigned char *y_line = y_plane + screen_y * y_stride + blank_left;
			unsigned char *logo_row = logo_pixels + i * logo_w * 4;
			for (j = 0; j < logo_w; j++) {
				b = logo_row[j * 4 + 0]; /* B */
				g = logo_row[j * 4 + 1]; /* G */
				r = logo_row[j * 4 + 2]; /* R */
				y_val = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
				y_val = y_val < 0 ? 0 : (y_val > 255 ? 255 : y_val);
				y_line[j] = (unsigned char)y_val;
			}
		}

		/* Draw logo UV plane (2x2 subsampled, averaged) */
		for (i = 0; i < logo_h; i += 2) {
			unsigned char *uv_line = uv_plane + ((blank_top + i) / 2) * y_stride + blank_left;
			for (j = 0; j < logo_w; j += 2) {
				u_sum = 0;
				v_sum = 0;
				count = 0;
				for (di = 0; di < 2 && i + di < logo_h; di++) {
					unsigned char *block_row = logo_pixels + (i + di) * logo_w * 4 + j * 4;
					for (dj = 0; dj < 2 && j + dj < logo_w; dj++) {
						b = block_row[dj * 4 + 0];
						g = block_row[dj * 4 + 1];
						r = block_row[dj * 4 + 2];
						u_val = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
						v_val = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
						u_sum += u_val;
						v_sum += v_val;
						count++;
					}
				}
				u_val = u_sum / count;
				v_val = v_sum / count;
				u_val = u_val < 0 ? 0 : (u_val > 255 ? 255 : u_val);
				v_val = v_val < 0 ? 0 : (v_val > 255 ? 255 : v_val);
				uv_line[j + 0] = (unsigned char)v_val;
				uv_line[j + 1] = (unsigned char)u_val;
			}
		}
	} else {
		printk(KERN_WARNING "logo: format mismatch, logo bpp=%d fb bpp=%d, skip\n",
		       pinfo->bpp, bpp);
	}
}
EXPORT_SYMBOL(show_logo);
EXPORT_SYMBOL(logo_info);
