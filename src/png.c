/*
 * PNG glyph loading, scaling, outlining, and the per-process icon cache.
 */
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "priv.h"

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

/*
 * Black halo behind the glyph so it reads on any wallpaper.
 *
 * Dilate the source alpha (max in a disk), then keep only the outer
 * ring (dilated - src).  Over-composite the glyph -- including its
 * soft AA fringe -- onto that ring so baked antialiasing lands on the
 * outline.  No black under the solid body: a translucent -A shows the
 * desktop through the glyph, not a filled silhouette.
 */
static unsigned char *
add_outline(const unsigned char *src, int w, int h, int stroke,
    int *w_out, int *h_out)
{
	unsigned char *dilated, *out;
	int pad, ow, oh, x, y, dx, dy, rad2;

	pad = stroke + 2;
	ow = w + pad * 2;
	oh = h + pad * 2;

	dilated = calloc((size_t)ow * (size_t)oh, 1);
	out = calloc((size_t)ow * (size_t)oh, 4);
	if (dilated == NULL || out == NULL) {
		free(dilated);
		free(out);
		return (NULL);
	}

	rad2 = stroke * stroke + stroke;

	/* Soft dilate: each coverage pixel stamps its alpha into the disk. */
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			unsigned char a =
			    src[((size_t)y * (size_t)w + (size_t)x) * 4 + 3];

			if (a == 0)
				continue;
			for (dy = -stroke; dy <= stroke; dy++) {
				for (dx = -stroke; dx <= stroke; dx++) {
					size_t di;
					int nx, ny;

					if (dx * dx + dy * dy > rad2)
						continue;
					nx = x + dx + pad;
					ny = y + dy + pad;
					di = (size_t)ny * (size_t)ow +
					    (size_t)nx;
					if (a > dilated[di])
						dilated[di] = a;
				}
			}
		}
	}

	/* Outer ring only: black with alpha = dilated - src. */
	for (y = 0; y < oh; y++) {
		for (x = 0; x < ow; x++) {
			size_t oi = ((size_t)y * (size_t)ow + (size_t)x) * 4;
			unsigned char d = dilated[(size_t)y * (size_t)ow +
			    (size_t)x];
			unsigned char sa = 0;
			int sx = x - pad, sy = y - pad;

			if (sx >= 0 && sy >= 0 && sx < w && sy < h)
				sa = src[((size_t)sy * (size_t)w +
				    (size_t)sx) * 4 + 3];
			if (d > sa) {
				out[oi + 0] = 0;
				out[oi + 1] = 0;
				out[oi + 2] = 0;
				out[oi + 3] = (unsigned char)(d - sa);
			}
		}
	}
	free(dilated);

	/* Glyph Over the ring so AA fringes sit on the outline. */
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			size_t si = ((size_t)y * (size_t)w + (size_t)x) * 4;
			size_t oi = ((size_t)(y + pad) * (size_t)ow +
			    (size_t)(x + pad)) * 4;
			unsigned sa, da, oa;
			unsigned char *d = out + oi;
			const unsigned char *s = src + si;

			sa = s[3];
			if (sa == 0)
				continue;
			da = d[3];
			oa = sa + da * (255 - sa) / 255;
			if (oa == 0)
				continue;
			d[0] = (unsigned char)
			    ((s[0] * sa + d[0] * da * (255 - sa) / 255) / oa);
			d[1] = (unsigned char)
			    ((s[1] * sa + d[1] * da * (255 - sa) / 255) / oa);
			d[2] = (unsigned char)
			    ((s[2] * sa + d[2] * da * (255 - sa) / 255) / oa);
			d[3] = (unsigned char)oa;
		}
	}

	if (w_out != NULL)
		*w_out = ow;
	if (h_out != NULL)
		*h_out = oh;
	return (out);
}

/*
 * Remap selected pixels' alpha so the selection's peak becomes
 * factor*255 (RGB unchanged).  which: 0 = all, 1 = non-black (glyph),
 * 2 = black only (outline).
 *
 * factor is absolute fill/halo opacity, not a multiplier on the file:
 * an 80% PNG with -A 0.9 peaks at 90%, with -A 1 at 100%, with no -A
 * left at 80%.  AA falloff scales with the peak, so there is no cliff
 * between 0.99 and 1.0.  factor < 0 leaves pixels alone.
 */
static void
apply_alpha(unsigned char *rgba, int w, int h, double alpha, int which)
{
	size_t n, i;
	unsigned peak, target;

	if (alpha < 0.0)
		return;
	n = (size_t)w * (size_t)h;

	peak = 0;
	for (i = 0; i < n; i++) {
		unsigned char *p = rgba + i * 4;
		int black = (p[0] == 0 && p[1] == 0 && p[2] == 0);

		if (which == 1 && black)
			continue;
		if (which == 2 && !black)
			continue;
		if (p[3] > peak)
			peak = p[3];
	}
	if (peak == 0)
		return;

	if (alpha <= 0.0) {
		for (i = 0; i < n; i++) {
			unsigned char *p = rgba + i * 4;
			int black = (p[0] == 0 && p[1] == 0 && p[2] == 0);

			if (which == 1 && black)
				continue;
			if (which == 2 && !black)
				continue;
			p[3] = 0;
		}
		return;
	}

	target = (unsigned)(alpha * 255.0 + 0.5);
	if (target > 255)
		target = 255;
	if (target == peak)
		return;
	for (i = 0; i < n; i++) {
		unsigned char *p = rgba + i * 4;
		unsigned a;
		int black = (p[0] == 0 && p[1] == 0 && p[2] == 0);

		if (which == 1 && black)
			continue;
		if (which == 2 && !black)
			continue;
		a = p[3];
		p[3] = (unsigned char)((a * target + peak / 2) / peak);
	}
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
prep_icon(const char *path, double scale, double alpha, double oalpha,
    int outline, int *w_out, int *h_out)
{
	unsigned char *src, *scaled, *padded, *outlined;
	int sw, sh, dw, dh, px, stroke, margin, ow, oh;

	src = load_png_rgba(path, &sw, &sh);
	if (src == NULL)
		return (NULL);
	px = (int)((double)icon_px * scale + 0.5);
	if (px < 8)
		px = 8;
	dh = px;
	dw = (int)((long)px * (long)sw / (long)sh);
	if (dw < 1)
		dw = 1;
	scaled = scale_rgba(src, sw, sh, dw, dh);
	free(src);
	if (scaled == NULL)
		return (NULL);

	stroke = px / 25;
	if (stroke < 5)
		stroke = 5;
	if (stroke > 13)
		stroke = 13;
	margin = icon_pad + stroke + 6;
	padded = pad_icon_rgba(scaled, dw, dh, margin);
	free(scaled);
	if (padded == NULL)
		return (NULL);
	/*
	 * Build the halo from the file's native coverage first so a low
	 * or zero -A cannot erase the mask (add_outline dilates alpha).
	 * Then -A / -O remap glyph and outline peaks to the requested
	 * opacity (AA scales with the peak).  alpha < 0 leaves the
	 * file's glyph alpha alone; outline still takes oalpha.
	 */
	if (!outline) {
		if (alpha >= 0.0)
			apply_alpha(padded, dw + margin * 2, dh + margin * 2,
			    alpha, 0);
		*w_out = dw + margin * 2;
		*h_out = dh + margin * 2;
		return (padded);
	}
	outlined = add_outline(padded, dw + margin * 2, dh + margin * 2,
	    stroke, &ow, &oh);
	free(padded);
	if (outlined == NULL)
		return (NULL);
	if (alpha >= 0.0)
		apply_alpha(outlined, ow, oh, alpha, 1);
	apply_alpha(outlined, ow, oh, oalpha, 2);
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
icon_lookup(const char *spec, double scale, double alpha, double oalpha,
    int outline)
{
	struct icon *ic, **pp;
	char path[BOSD_SPEC_MAX];
	unsigned char *rgba;
	int w, h;

	if (icon_resolve(spec, path, sizeof(path)) != 0)
		return (NULL);
	if (scale < BOSD_SCALE_MIN)
		scale = BOSD_SCALE_MIN;
	if (scale > BOSD_SCALE_MAX)
		scale = BOSD_SCALE_MAX;
	if (alpha >= 0.0) {
		if (alpha < BOSD_ALPHA_MIN)
			alpha = BOSD_ALPHA_MIN;
		if (alpha > BOSD_ALPHA_MAX)
			alpha = BOSD_ALPHA_MAX;
	} else
		alpha = BOSD_ALPHA_NATIVE;
	if (oalpha < BOSD_ALPHA_MIN)
		oalpha = BOSD_ALPHA_MIN;
	if (oalpha > BOSD_ALPHA_MAX)
		oalpha = BOSD_ALPHA_MAX;

	for (pp = &cache_head; (ic = *pp) != NULL; pp = &ic->next) {
		if (strcmp(ic->path, path) == 0 && ic->scale == scale &&
		    ic->alpha == alpha && ic->outline_alpha == oalpha &&
		    ic->outline == outline) {
			*pp = ic->next;
			ic->next = cache_head;
			cache_head = ic;
			return (ic);
		}
	}

	rgba = prep_icon(path, scale, alpha, oalpha, outline, &w, &h);
	if (rgba == NULL)
		return (NULL);
	ic = calloc(1, sizeof(*ic));
	if (ic == NULL) {
		free(rgba);
		return (NULL);
	}
	strlcpy(ic->path, path, sizeof(ic->path));
	ic->scale = scale;
	ic->alpha = alpha;
	ic->outline_alpha = oalpha;
	ic->outline = outline;
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
