/*
 * Xft text beside the artwork: the superscript badge at the glyph's
 * upper-right, and captions above/below.
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

/* "face:attrs" or fallback:attrs - face may already carry Fc props. */
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
