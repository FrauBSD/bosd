/*
 * PNG glyph loading, scaling, outlining, and the per-process icon cache.
 */
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bosd.h"

#define CACHE_MAX 16

static struct icon *cache_head;
static int cache_len;

static unsigned char *
load_png_rgba(const char *path, int *w_out, int *h_out)
{
	FILE *fp;
	png_structp png;
	png_infop info, end;
	png_bytep *rows;
	unsigned char *rgba = NULL;
	unsigned char sig[8];
	int w, h, y, bit_depth, color_type;

	fp = fopen(path, "rb");
	if (fp == NULL)
		return (NULL);
	if (fread(sig, 1, 8, fp) != 8 || png_sig_cmp(sig, 0, 8)) {
		fclose(fp);
		return (NULL);
	}
	png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	info = png_create_info_struct(png);
	end = png_create_info_struct(png);
	if (png == NULL || info == NULL || end == NULL ||
	    setjmp(png_jmpbuf(png))) {
		fclose(fp);
		png_destroy_read_struct(&png, &info, &end);
		return (NULL);
	}
	png_init_io(png, fp);
	png_set_sig_bytes(png, 8);
	png_read_info(png, info);
	w = png_get_image_width(png, info);
	h = png_get_image_height(png, info);
	bit_depth = png_get_bit_depth(png, info);
	color_type = png_get_color_type(png, info);
	if (bit_depth == 16)
		png_set_strip_16(png);
	if (color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png);
	if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
		png_set_expand_gray_1_2_4_to_8(png);
	if (png_get_valid(png, info, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png);
	if (color_type == PNG_COLOR_TYPE_RGB ||
	    color_type == PNG_COLOR_TYPE_GRAY ||
	    color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_filler(png, 0xff, PNG_FILLER_AFTER);
	if (color_type == PNG_COLOR_TYPE_GRAY ||
	    color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png);
	png_read_update_info(png, info);
	rows = png_malloc(png, (png_alloc_size_t)h * sizeof(png_bytep));
	rgba = malloc((size_t)w * (size_t)h * 4);
	if (rows == NULL || rgba == NULL) {
		free(rgba);
		fclose(fp);
		png_destroy_read_struct(&png, &info, &end);
		return (NULL);
	}
	for (y = 0; y < h; y++)
		rows[y] = rgba + (size_t)y * (size_t)w * 4;
	png_read_image(png, rows);
	png_read_end(png, end);
	png_free(png, rows);
	png_destroy_read_struct(&png, &info, &end);
	fclose(fp);
	*w_out = w;
	*h_out = h;
	return (rgba);
}

static unsigned char *
scale_rgba(const unsigned char *src, int sw, int sh, int dw, int dh)
{
	unsigned char *dst;
	int x, y, sx, sy;

	dst = malloc((size_t)dw * (size_t)dh * 4);
	if (dst == NULL)
		return (NULL);
	for (y = 0; y < dh; y++) {
		sy = y * sh / dh;
		for (x = 0; x < dw; x++) {
			sx = x * sw / dw;
			memcpy(dst + ((size_t)y * dw + x) * 4,
			    src + ((size_t)sy * sw + sx) * 4, 4);
		}
	}
	return (dst);
}

/* Black halo behind the glyph so it reads on any wallpaper. */
static unsigned char *
add_outline(const unsigned char *src, int w, int h, int stroke,
    int *w_out, int *h_out)
{
	unsigned char *mask, *out;
	int pad, ow, oh, x, y, dx, dy, rad2;

	pad = stroke + 2;
	ow = w + pad * 2;
	oh = h + pad * 2;

	mask = calloc((size_t)w * (size_t)h, 1);
	out = calloc((size_t)ow * (size_t)oh, 4);
	if (mask == NULL || out == NULL) {
		free(mask);
		free(out);
		return (NULL);
	}

	rad2 = stroke * stroke + stroke;

	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			if (src[((size_t)y * (size_t)w + (size_t)x) * 4 + 3] >=
			    32)
				mask[(size_t)y * (size_t)w + (size_t)x] = 1;
		}
	}

	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			if (!mask[(size_t)y * (size_t)w + (size_t)x])
				continue;
			for (dy = -stroke; dy <= stroke; dy++) {
				for (dx = -stroke; dx <= stroke; dx++) {
					int nx, ny;
					size_t oi;

					if (dx * dx + dy * dy > rad2)
						continue;
					nx = x + dx + pad;
					ny = y + dy + pad;
					oi = ((size_t)ny * (size_t)ow +
					    (size_t)nx) * 4;
					out[oi + 0] = 0;
					out[oi + 1] = 0;
					out[oi + 2] = 0;
					out[oi + 3] = 255;
				}
			}
		}
	}

	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			size_t si = ((size_t)y * (size_t)w + (size_t)x) * 4;
			size_t oi = ((size_t)(y + pad) * (size_t)ow +
			    (size_t)(x + pad)) * 4;
			unsigned char a;

			a = src[si + 3];
			if (a < 32)
				continue;
			out[oi + 0] = src[si + 0];
			out[oi + 1] = src[si + 1];
			out[oi + 2] = src[si + 2];
			out[oi + 3] = a;
		}
	}

	free(mask);
	if (w_out != NULL)
		*w_out = ow;
	if (h_out != NULL)
		*h_out = oh;
	return (out);
}

/* Transparent margin around the artwork (badge headroom, halo air). */
static unsigned char *
pad_icon_rgba(const unsigned char *src, int w, int h, int margin)
{
	unsigned char *dst;
	int pw, ph, y;

	pw = w + margin * 2;
	ph = h + margin * 2;
	dst = calloc((size_t)pw * (size_t)ph, 4);
	if (dst == NULL)
		return (NULL);
	for (y = 0; y < h; y++)
		memcpy(dst + (((size_t)(y + margin) * pw) + margin) * 4,
		    src + (size_t)y * w * 4, (size_t)w * 4);
	return (dst);
}

static unsigned char *
prep_icon(const char *path, int *w_out, int *h_out)
{
	unsigned char *src, *scaled, *padded, *outlined;
	int sw, sh, dw, dh, stroke, margin, ow, oh;

	src = load_png_rgba(path, &sw, &sh);
	if (src == NULL)
		return (NULL);
	dh = icon_px;
	dw = (int)((long)icon_px * (long)sw / (long)sh);
	if (dw < 1)
		dw = 1;
	scaled = scale_rgba(src, sw, sh, dw, dh);
	free(src);
	if (scaled == NULL)
		return (NULL);

	stroke = icon_px / 25;
	if (stroke < 5)
		stroke = 5;
	if (stroke > 13)
		stroke = 13;
	margin = icon_pad + stroke + 6;
	padded = pad_icon_rgba(scaled, dw, dh, margin);
	free(scaled);
	if (padded == NULL)
		return (NULL);
	outlined = add_outline(padded, dw + margin * 2, dh + margin * 2,
	    stroke, &ow, &oh);
	free(padded);
	if (outlined == NULL)
		return (NULL);
	*w_out = ow;
	*h_out = oh;
	return (outlined);
}

static void
icon_free(struct icon *ic)
{
	free(ic->rgba);
	free(ic);
}

/*
 * Resolve, load, and cache an icon.  Most-recently-shown stays at the
 * head; the list is capped so a chatty session cannot grow unbounded.
 */
struct icon *
icon_lookup(const char *spec)
{
	struct icon *ic, **pp;
	char path[BOSD_SPEC_MAX];
	unsigned char *rgba;
	int w, h;

	if (icon_resolve(spec, path, sizeof(path)) != 0)
		return (NULL);

	for (pp = &cache_head; (ic = *pp) != NULL; pp = &ic->next) {
		if (strcmp(ic->path, path) == 0) {
			*pp = ic->next;
			ic->next = cache_head;
			cache_head = ic;
			return (ic);
		}
	}

	rgba = prep_icon(path, &w, &h);
	if (rgba == NULL)
		return (NULL);
	ic = calloc(1, sizeof(*ic));
	if (ic == NULL) {
		free(rgba);
		return (NULL);
	}
	strlcpy(ic->path, path, sizeof(ic->path));
	ic->rgba = rgba;
	ic->w = w;
	ic->h = h;
	ic->next = cache_head;
	cache_head = ic;

	if (++cache_len > CACHE_MAX) {
		for (pp = &cache_head; (*pp)->next != NULL;
		    pp = &(*pp)->next)
			;
		icon_free(*pp);
		*pp = NULL;
		cache_len--;
	}
	return (ic);
}

void
icon_cache_clear(void)
{
	struct icon *ic;

	while ((ic = cache_head) != NULL) {
		cache_head = ic->next;
		icon_free(ic);
	}
	cache_len = 0;
}
