/*
 * Superscript badge: Xft text at the glyph's upper-right.
 */
#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>

#include "bosd.h"

static XftFont *badge_font;
static int badge_font_px = -1;

/* Cache the face (XftFontOpenName/fontconfig can take seconds). */
static XftFont *
open_badge_font(int screen, int pixelsize)
{
	char pattern[256];
	XftFont *font;
	const char *file = "/usr/local/share/fonts/dejavu/DejaVuSans-Bold.ttf";

	if (badge_font != NULL && badge_font_px == pixelsize)
		return (badge_font);
	if (badge_font != NULL) {
		XftFontClose(dpy, badge_font);
		badge_font = NULL;
		badge_font_px = -1;
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
	badge_font = font;
	badge_font_px = (font != NULL) ? pixelsize : -1;
	return (font);
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
	int screen, pixelsize, stroke, dx, dy, text_x, text_y;
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

	font = open_badge_font(screen, pixelsize);
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
	for (dy = -stroke; dy <= stroke; dy++) {
		for (dx = -stroke; dx <= stroke; dx++) {
			if (dx == 0 && dy == 0)
				continue;
			if (dx * dx + dy * dy > stroke * stroke + stroke)
				continue;
			XftDrawStringUtf8(draw, &bg, font, text_x + dx,
			    text_y + dy, (FcChar8 *)text, tlen);
		}
	}
	XftDrawStringUtf8(draw, &fg, font, text_x, text_y, (FcChar8 *)text,
	    tlen);

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
}
