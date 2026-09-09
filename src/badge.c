/*
 * Xft text riding an icon show: the superscript badge at the glyph's
 * upper-right, and captions above/below the artwork.
 */
#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>

#include "bosd.h"

static XftFont *badge_font, *cap_font;
static int badge_font_px = -1, cap_font_px = -1;

/* Cache the face (XftFontOpenName/fontconfig can take seconds). */
static XftFont *
open_face(int screen, int pixelsize, XftFont **slot, int *slot_px)
{
	char pattern[256];
	XftFont *font;
	const char *file = "/usr/local/share/fonts/dejavu/DejaVuSans-Bold.ttf";

	if (*slot != NULL && *slot_px == pixelsize)
		return (*slot);
	if (*slot != NULL) {
		XftFontClose(dpy, *slot);
		*slot = NULL;
		*slot_px = -1;
	}

	snprintf(pattern, sizeof(pattern),
	    "file=%s:pixelsize=%d:antialias=true", file, pixelsize);
	font = XftFontOpenName(dpy, screen, pattern);
	if (font == NULL) {
		snprintf(pattern, sizeof(pattern),
		    "DejaVu Sans:bold:pixelsize=%d:antialias=true", pixelsize);
		font = XftFontOpenName(dpy, screen, pattern);
	}
	if (font == NULL) {
		snprintf(pattern, sizeof(pattern),
		    "Sans:bold:pixelsize=%d:antialias=true", pixelsize);
		font = XftFontOpenName(dpy, screen, pattern);
	}
	*slot = font;
	*slot_px = (font != NULL) ? pixelsize : -1;
	return (font);
}

/* White fill ringed by a black outline. */
static void
outlined_string(XftDraw *draw, XftFont *font, int x, int y,
    const char *text, int tlen, int stroke, XftColor *fg, XftColor *bg)
{
	int dx, dy;

	for (dy = -stroke; dy <= stroke; dy++) {
		for (dx = -stroke; dx <= stroke; dx++) {
			if (dx == 0 && dy == 0)
				continue;
			if (dx * dx + dy * dy > stroke * stroke + stroke)
				continue;
			XftDrawStringUtf8(draw, bg, font, x + dx, y + dy,
			    (FcChar8 *)text, tlen);
		}
	}
	XftDrawStringUtf8(draw, fg, font, x, y, (FcChar8 *)text, tlen);
}

/*
 * White fill with a black outline (no surrounding disc).  Reads as an
 * index/label; artwork stays centered.
 */
void
draw_badge(const struct icon *ic, const char *text)
{
	XftFont *font;
	XftDraw *draw;
	XftColor fg, bg;
	XGlyphInfo ext;
	int screen, pixelsize, stroke, text_x, text_y;
	int target_h, tlen;

	if (text == NULL || text[0] == '\0')
		return;
	tlen = (int)strlen(text);

	screen = DefaultScreen(dpy);

	/* ~26% of the glyph height: an index, not a second hero icon. */
	target_h = (ic->h * 26) / 100;
	if (target_h < 49)
		target_h = 49;
	if (target_h > 109)
		target_h = 109;
	pixelsize = target_h;

	font = open_face(screen, pixelsize, &badge_font, &badge_font_px);
	if (font == NULL)
		return;

	XftTextExtentsUtf8(dpy, font, (FcChar8 *)text, tlen, &ext);

	/* Nestled against the glyph's upper-right, slightly overlapping. */
	text_x = icon_ox + (ic->w * 70) / 100 - ext.x;
	text_y = icon_oy + (ic->h * 12) / 100 + font->ascent;
	if (text_x + ext.width + 6 > win_w)
		text_x = win_w - ext.width - 6;
	if (text_x < 4)
		text_x = 4;
	if (text_y < font->ascent + 4)
		text_y = font->ascent + 4;
	if (text_y + font->descent + 4 > win_h)
		text_y = win_h - font->descent - 4;

	draw = XftDrawCreate(dpy, win, visual, cmap);
	if (draw == NULL)
		return;
	if (!XftColorAllocName(dpy, visual, cmap, "black", &bg) ||
	    !XftColorAllocName(dpy, visual, cmap, "white", &fg)) {
		XftDrawDestroy(draw);
		return;
	}

	stroke = pixelsize / 14;
	if (stroke < 3)
		stroke = 3;
	if (stroke > 8)
		stroke = 8;
	outlined_string(draw, font, text_x, text_y, text, tlen, stroke,
	    &fg, &bg);

	XftColorFree(dpy, visual, cmap, &fg);
	XftColorFree(dpy, visual, cmap, &bg);
	XftDrawDestroy(draw);
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
caption_measure(const char *text, int px, int *w, int *h)
{
	XftFont *font;
	XGlyphInfo ext;

	*w = 0;
	*h = 0;
	font = open_face(DefaultScreen(dpy), px, &cap_font, &cap_font_px);
	if (font == NULL)
		return;
	XftTextExtentsUtf8(dpy, font, (FcChar8 *)text, (int)strlen(text),
	    &ext);
	*w = (int)ext.width;
	*h = font->ascent + font->descent;
}

/* Centered caption; anchor_y is the artwork edge it hangs off. */
void
draw_caption(const char *text, int px, int anchor_y, int below)
{
	XftFont *font;
	XftDraw *draw;
	XftColor fg, bg;
	XGlyphInfo ext;
	int tlen, x, y, stroke;

	if (text == NULL || text[0] == '\0')
		return;
	tlen = (int)strlen(text);
	font = open_face(DefaultScreen(dpy), px, &cap_font, &cap_font_px);
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

	draw = XftDrawCreate(dpy, win, visual, cmap);
	if (draw == NULL)
		return;
	if (!XftColorAllocName(dpy, visual, cmap, "black", &bg) ||
	    !XftColorAllocName(dpy, visual, cmap, "white", &fg)) {
		XftDrawDestroy(draw);
		return;
	}
	stroke = px / 14;
	if (stroke < 2)
		stroke = 2;
	if (stroke > 6)
		stroke = 6;
	outlined_string(draw, font, x, y, text, tlen, stroke, &fg, &bg);
	XftColorFree(dpy, visual, cmap, &fg);
	XftColorFree(dpy, visual, cmap, &bg);
	XftDrawDestroy(draw);
}

void
badge_cleanup(void)
{
	if (dpy != NULL && badge_font != NULL) {
		XftFontClose(dpy, badge_font);
		badge_font = NULL;
		badge_font_px = -1;
	}
	if (dpy != NULL && cap_font != NULL) {
		XftFontClose(dpy, cap_font);
		cap_font = NULL;
		cap_font_px = -1;
	}
}
