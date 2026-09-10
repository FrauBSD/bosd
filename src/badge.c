/*
 * Xft text beside the artwork: the superscript badge at the glyph's
 * upper-right, and captions above/below.  Shared outlined UTF-8
 * painter (opaque fast path, or punched ARGB layers for -A/-O).
 */
#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/Xrender.h>

#include "priv.h"

static XftFont *badge_font, *cap_font;
static int badge_font_px = -1, cap_font_px = -1;
static char badge_face[BOSD_FONT_MAX], cap_face[BOSD_FONT_MAX];

/* "face:attrs" or fallback:attrs — face may already carry Fc props. */
void
font_pattern(char *out, size_t outlen, const char *face,
    const char *fallback, const char *attrs)
{
	snprintf(out, outlen, "%s:%s",
	    (face != NULL && face[0] != '\0') ? face : fallback, attrs);
}

/* Cache the face (XftFontOpenName/fontconfig can take seconds). */
static XftFont *
open_face(int screen, int pixelsize, XftFont **slot, int *slot_px,
    char *slot_face, const char *face)
{
	char pattern[BOSD_FONT_MAX + 64];
	char attrs[64];
	XftFont *font;
	const char *want = (face != NULL) ? face : "";

	if (*slot != NULL && *slot_px == pixelsize &&
	    strcmp(slot_face, want) == 0)
		return (*slot);
	if (*slot != NULL) {
		XftFontClose(dpy, *slot);
		*slot = NULL;
		*slot_px = -1;
		slot_face[0] = '\0';
	}

	snprintf(attrs, sizeof(attrs), "pixelsize=%d:antialias=true",
	    pixelsize);
	font_pattern(pattern, sizeof(pattern), want, "DejaVu Sans", attrs);
	font = XftFontOpenName(dpy, screen, pattern);
	if (font == NULL && want[0] != '\0') {
		font_pattern(pattern, sizeof(pattern), NULL, "DejaVu Sans",
		    attrs);
		font = XftFontOpenName(dpy, screen, pattern);
	}
	if (font == NULL) {
		font_pattern(pattern, sizeof(pattern), NULL, "Sans", attrs);
		font = XftFontOpenName(dpy, screen, pattern);
	}
	*slot = font;
	*slot_px = (font != NULL) ? pixelsize : -1;
	if (font != NULL)
		strlcpy(slot_face, want, BOSD_FONT_MAX);
	return (font);
}

static void
blit_layer(Pixmap pix, int iw, int ih, int dx, int dy, double alpha)
{
	XRenderPictFormat *fmt;
	Picture src, dst, mask;
	XRenderColor mc;

	if (alpha <= 0.0)
		return;
	fmt = XRenderFindStandardFormat(dpy, PictStandardARGB32);
	if (fmt == NULL)
		return;
	src = XRenderCreatePicture(dpy, pix, fmt, 0, NULL);
	fmt = XRenderFindVisualFormat(dpy, visual);
	dst = XRenderCreatePicture(dpy, win, fmt, 0, NULL);
	mc.red = mc.green = mc.blue = 0xffff;
	mc.alpha = (unsigned short)(alpha * 65535.0 + 0.5);
	mask = XRenderCreateSolidFill(dpy, &mc);
	XRenderComposite(dpy, PictOpOver, src, mask, dst, 0, 0, 0, 0,
	    dx, dy, iw, ih);
	XRenderFreePicture(dpy, mask);
	XRenderFreePicture(dpy, src);
	XRenderFreePicture(dpy, dst);
}

static void
punch_fill_from_outline(Pixmap opix, Pixmap fpix, int iw, int ih)
{
	XRenderPictFormat *fmt;
	Picture src, dst;

	fmt = XRenderFindStandardFormat(dpy, PictStandardARGB32);
	if (fmt == NULL)
		return;
	src = XRenderCreatePicture(dpy, fpix, fmt, 0, NULL);
	dst = XRenderCreatePicture(dpy, opix, fmt, 0, NULL);
	XRenderComposite(dpy, PictOpOutReverse, src, None, dst,
	    0, 0, 0, 0, 0, 0, iw, ih);
	XRenderFreePicture(dpy, src);
	XRenderFreePicture(dpy, dst);
}

static Pixmap
stamp_xft(XftFont *f, int lx, int ly, const char *text, int len, int s,
    const char *color, int iw, int ih)
{
	XftDraw *td;
	XftColor ink;
	XRenderPictFormat *fmt;
	XRenderColor clear;
	Pixmap pix;
	Picture tp;
	int dx, dy;
	int fill = (color != NULL);

	fmt = XRenderFindStandardFormat(dpy, PictStandardARGB32);
	if (fmt == NULL)
		return (None);
	pix = XCreatePixmap(dpy, win, iw, ih, 32);
	tp = XRenderCreatePicture(dpy, pix, fmt, 0, NULL);
	clear.red = clear.green = clear.blue = clear.alpha = 0;
	XRenderFillRectangle(dpy, PictOpSrc, tp, &clear, 0, 0, iw, ih);
	XRenderFreePicture(dpy, tp);

	td = XftDrawCreate(dpy, pix, visual, cmap);
	if (td == NULL ||
	    !XftColorAllocName(dpy, visual, cmap,
	    fill ? color : "black", &ink)) {
		if (td != NULL)
			XftDrawDestroy(td);
		XFreePixmap(dpy, pix);
		return (None);
	}
	if (fill) {
		XftDrawStringUtf8(td, &ink, f, lx, ly,
		    (const FcChar8 *)text, len);
	} else {
		for (dx = -s; dx <= s; dx++) {
			for (dy = -s; dy <= s; dy++) {
				if (dx == 0 && dy == 0)
					continue;
				XftDrawStringUtf8(td, &ink, f, lx + dx,
				    ly + dy, (const FcChar8 *)text, len);
			}
		}
	}
	XftColorFree(dpy, visual, cmap, &ink);
	XftDrawDestroy(td);
	return (pix);
}

/*
 * Fast path: opaque Xft.  Slow path: stamp outline/fill, punch, then
 * PictOpOver each layer once with a solid alpha mask.
 */
void
draw_outlined_utf8(XftDraw *xd, XftFont *font, int x, int y,
    const char *text, int stroke, const char *fill_color,
    double fill_alpha, double outline_alpha)
{
	XGlyphInfo e;
	XftColor fg, bg;
	XftDraw *own = NULL;
	Pixmap opix = None, fpix = None;
	int len, dx, dy, ox, oy, iw, ih, lx, ly;

	if (text == NULL || text[0] == '\0' || font == NULL)
		return;
	len = (int)strlen(text);
	if (fill_color == NULL || fill_color[0] == '\0')
		fill_color = "white";
	if (fill_alpha >= 1.0 && outline_alpha >= 1.0) {
		if (xd == NULL) {
			own = XftDrawCreate(dpy, win, visual, cmap);
			xd = own;
		}
		if (xd == NULL)
			return;
		if (!XftColorAllocName(dpy, visual, cmap, fill_color, &fg) ||
		    !XftColorAllocName(dpy, visual, cmap, "black", &bg)) {
			if (own != NULL)
				XftDrawDestroy(own);
			return;
		}
		for (dx = -stroke; dx <= stroke; dx++) {
			for (dy = -stroke; dy <= stroke; dy++) {
				if (dx == 0 && dy == 0)
					continue;
				XftDrawStringUtf8(xd, &bg, font, x + dx,
				    y + dy, (const FcChar8 *)text, len);
			}
		}
		XftDrawStringUtf8(xd, &fg, font, x, y, (const FcChar8 *)text,
		    len);
		XftColorFree(dpy, visual, cmap, &fg);
		XftColorFree(dpy, visual, cmap, &bg);
		if (own != NULL)
			XftDrawDestroy(own);
		return;
	}

	XftTextExtentsUtf8(dpy, font, (const FcChar8 *)text, len, &e);
	ox = x - (int)e.x - stroke;
	oy = y - (int)e.y - stroke;
	iw = (int)e.width + 2 * stroke;
	ih = (int)e.height + 2 * stroke;
	if (iw < 1)
		iw = 1;
	if (ih < 1)
		ih = 1;
	lx = x - ox;
	ly = y - oy;

	if (stroke > 0 && outline_alpha > 0.0)
		opix = stamp_xft(font, lx, ly, text, len, stroke, NULL,
		    iw, ih);
	if (fill_alpha > 0.0 || opix != None)
		fpix = stamp_xft(font, lx, ly, text, len, stroke,
		    fill_color, iw, ih);
	if (opix != None && fpix != None)
		punch_fill_from_outline(opix, fpix, iw, ih);
	if (opix != None) {
		blit_layer(opix, iw, ih, ox, oy, outline_alpha);
		XFreePixmap(dpy, opix);
	}
	if (fpix != None) {
		if (fill_alpha > 0.0)
			blit_layer(fpix, iw, ih, ox, oy, fill_alpha);
		XFreePixmap(dpy, fpix);
	}
}

/*
 * Inclusive ink box of pixels with alpha >= 32 (same threshold as the
 * PNG outline mask).  Returns 0 if the canvas is empty.
 */
static int
icon_ink_bounds(const struct icon *ic, int *left, int *top, int *right,
    int *bottom)
{
	int x, y, l, t, r, b, found;
	const unsigned char *p;

	if (ic == NULL || ic->rgba == NULL || ic->w < 1 || ic->h < 1)
		return (0);
	l = ic->w;
	t = ic->h;
	r = -1;
	b = -1;
	found = 0;
	for (y = 0; y < ic->h; y++) {
		p = ic->rgba + (size_t)y * (size_t)ic->w * 4;
		for (x = 0; x < ic->w; x++, p += 4) {
			if (p[3] < 32)
				continue;
			if (!found) {
				l = r = x;
				t = b = y;
				found = 1;
			} else {
				if (x < l)
					l = x;
				if (x > r)
					r = x;
				if (y < t)
					t = y;
				if (y > b)
					b = y;
			}
		}
	}
	if (!found)
		return (0);
	*left = l;
	*top = t;
	*right = r;
	*bottom = b;
	return (1);
}

/*
 * Fill (or caller color) with a black outline.  Honors -A/-O like
 * large text so icon badges match -T badges.  Anchored just past the
 * artwork's rightmost ink so a sparse canvas does not shove the badge
 * over empty padding, and a full canvas does not park it mid-glyph.
 */
void
draw_badge(const struct icon *ic, const char *text, const char *color,
    double fill_alpha, double outline_alpha, const char *face)
{
	XftFont *font;
	XGlyphInfo ext;
	int screen, pixelsize, stroke, text_x, text_y;
	int target_h, tlen, hgap, vgap;
	int ink_l, ink_t, ink_r, ink_b;
	const char *fill;

	if (text == NULL || text[0] == '\0')
		return;
	tlen = (int)strlen(text);
	fill = (color != NULL && color[0] != '\0') ? color : "white";

	screen = DefaultScreen(dpy);

	/* ~26% of the glyph height: an index, not a second hero icon. */
	target_h = (ic->h * 26) / 100;
	if (target_h < 49)
		target_h = 49;
	if (target_h > 109)
		target_h = 109;
	pixelsize = target_h;

	font = open_face(screen, pixelsize, &badge_font, &badge_font_px,
	    badge_face, face);
	if (font == NULL)
		return;

	XftTextExtentsUtf8(dpy, font, (FcChar8 *)text, tlen, &ext);

	stroke = pixelsize / 14;
	if (stroke < 3)
		stroke = 3;
	if (stroke > 8)
		stroke = 8;
	/* Outline may kiss the ink; keep a hairline for the fill. */
	hgap = 2;
	vgap = stroke < 4 ? 4 : stroke;

	if (icon_ink_bounds(ic, &ink_l, &ink_t, &ink_r, &ink_b)) {
		/* Right of the ink; baseline just above the content top. */
		text_x = icon_ox + ink_r + hgap - ext.x;
		text_y = icon_oy + ink_t - vgap;
	} else {
		/* Empty canvas fallback: old fraction of the pixmap. */
		text_x = icon_ox + (ic->w * 70) / 100 - ext.x;
		text_y = icon_oy + (ic->h * 12) / 100 + font->ascent;
	}
	if (text_x + ext.width + 6 > win_w)
		text_x = win_w - ext.width - 6;
	if (text_x < 4)
		text_x = 4;
	if (text_y < font->ascent + 4)
		text_y = font->ascent + 4;
	if (text_y + font->descent + 4 > win_h)
		text_y = win_h - font->descent - 4;

	draw_outlined_utf8(NULL, font, text_x, text_y, text, stroke, fill,
	    fill_alpha, outline_alpha);
}

/* Caption sizing shared by layout (reserve room) and paint. */
int
caption_px(const struct icon *ic)
{
	int px = (ic->h * 14) / 100;

	if (px < 28)
		px = 28;
	if (px > 64)
		px = 64;
	return (px);
}

int
caption_gap(void)
{
	int gap = icon_px / 24;

	return (gap < 8 ? 8 : gap);
}

void
caption_measure(const char *text, int px, int *w, int *h,
    const char *face)
{
	XftFont *font;
	XGlyphInfo ext;

	*w = 0;
	*h = 0;
	font = open_face(DefaultScreen(dpy), px, &cap_font, &cap_font_px,
	    cap_face, face);
	if (font == NULL)
		return;
	XftTextExtentsUtf8(dpy, font, (FcChar8 *)text, (int)strlen(text),
	    &ext);
	*w = (int)ext.width;
	*h = font->ascent + font->descent;
}

/* Centered caption; anchor_y is the artwork edge it hangs off. */
void
draw_caption(const char *text, int px, int anchor_y, int below,
    const char *face)
{
	XftFont *font;
	XGlyphInfo ext;
	int tlen, x, y, stroke;

	if (text == NULL || text[0] == '\0')
		return;
	tlen = (int)strlen(text);
	font = open_face(DefaultScreen(dpy), px, &cap_font, &cap_font_px,
	    cap_face, face);
	if (font == NULL)
		return;

	XftTextExtentsUtf8(dpy, font, (FcChar8 *)text, tlen, &ext);
	x = (win_w - (int)ext.width) / 2 + ext.x;
	if (x < 4)
		x = 4;
	if (below)
		y = anchor_y + caption_gap() + font->ascent;
	else
		y = anchor_y - caption_gap() - font->descent;

	stroke = px / 14;
	if (stroke < 2)
		stroke = 2;
	if (stroke > 6)
		stroke = 6;
	/* Captions stay fully opaque; -A/-O target artwork and badges. */
	draw_outlined_utf8(NULL, font, x, y, text, stroke, "white",
	    1.0, 1.0);
}

void
badge_cleanup(void)
{
	if (dpy != NULL && badge_font != NULL) {
		XftFontClose(dpy, badge_font);
		badge_font = NULL;
		badge_font_px = -1;
		badge_face[0] = '\0';
	}
	if (dpy != NULL && cap_font != NULL) {
		XftFontClose(dpy, cap_font);
		cap_font = NULL;
		cap_font_px = -1;
		cap_face[0] = '\0';
	}
}
