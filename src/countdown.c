/*
 * Countdown show (the screenshot timer): huge outlined Xft digits on
 * a full-panel overlay, one tick per hold.  Runs in the channel
 * daemon like any show, or in the invoking process when none does.
 */
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/Xrender.h>
#include <fontconfig/fontconfig.h>

#include "priv.h"

static XftFont	*font, *bfont, *cfont;
static XftDraw	*draw;
static XftColor	 fg, bg;
static int	 stroke, cstroke;
static int	 lock_len, lock_x, lock_bx;
static double	 fill_alpha = 1.0, outline_alpha = 1.0;
static char	 text_buf[BOSD_SPEC_MAX];

static void
on_signal(int sig __unused)
{
	stop = 1;
}

/* Sleep to a monotonic deadline despite signal wakeups. */
static void
hold_exact(double seconds)
{
	double deadline, t;

	if (seconds < 0.0) {	/* indefinite: until SIGINT/SIGTERM */
		while (!stop)
			usleep(100000);
		return;
	}
	deadline = now_monotonic() + seconds;
	while (!stop && (t = now_monotonic()) < deadline) {
		double left = deadline - t;

		if (left > 0.002)
			usleep((useconds_t)((left - 0.001) * 1e6));
	}
}

static int
stroke_for(int pointsize)
{
	int s = pointsize / 45;

	return (s < 3 ? 3 : s);
}

/* Widest digit's extents: the fixed slot every glyph centers in. */
static void
ref_digit_extents(XGlyphInfo *ref)
{
	XGlyphInfo e;
	const char *d;
	int max_w = 0;

	memset(ref, 0, sizeof(*ref));
	for (d = "0123456789"; *d != '\0'; d++) {
		XftTextExtentsUtf8(dpy, font, (const FcChar8 *)d, 1, &e);
		if ((int)e.width > max_w) {
			max_w = (int)e.width;
			*ref = e;
		}
	}
}

/*
 * Largest tabular bold face that fits the panel with outline air
 * (width-checked against fit_text when given, the widest glyph slot
 * otherwise).
 */
static XftFont *
open_fit_font(int screen, int w, int h, int *pointsize,
    const char *fit_text)
{
	char pattern[128];
	XftFont *f;
	XGlyphInfo e;
	int ps = *pointsize, s, tw;

	while (ps >= 16) {
		s = stroke_for(ps);
		snprintf(pattern, sizeof(pattern),
		    "DejaVu Sans:bold:size=%d:antialias=true:tabular=1", ps);
		f = XftFontOpenName(dpy, screen, pattern);
		if (f == NULL)
			break;
		if (fit_text != NULL) {
			XftTextExtentsUtf8(dpy, f,
			    (const FcChar8 *)fit_text,
			    (int)strlen(fit_text), &e);
			tw = (int)e.width;
		} else
			tw = (int)f->max_advance_width;
		if (f->ascent + f->descent + s * 2 + 32 <= h &&
		    tw + s * 2 + 32 <= w) {
			*pointsize = ps;
			return (f);
		}
		XftFontClose(dpy, f);
		ps -= 16;
	}
	snprintf(pattern, sizeof(pattern),
	    "Sans:bold:size=%d:antialias=true", ps);
	f = XftFontOpenName(dpy, screen, pattern);
	if (f != NULL)
		*pointsize = ps;
	return (f);
}

/*
 * Fast path: opaque Xft onto the window.  Slow path: stamp opaque
 * outline and fill onto temp ARGB pixmaps (so multipass strokes do
 * not pile translucent ink), punch the fill out of the outline so
 * translucent white is not Over black (that reads as opaque gray),
 * then PictOpOver each layer once with a solid alpha mask.
 */
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

/* dst := dst outside src — clear outline ink under the glyph body. */
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
    int fill, int iw, int ih)
{
	XftDraw *td;
	XftColor ink;
	XRenderPictFormat *fmt;
	XRenderColor clear;
	Pixmap pix;
	Picture tp;
	int dx, dy;

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
	    fill ? "white" : "black", &ink)) {
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

static void
draw_outlined(XftFont *f, int x, int y, const char *text, int s)
{
	XGlyphInfo e;
	Pixmap opix = None, fpix = None;
	int len, dx, dy, ox, oy, iw, ih, lx, ly;

	len = (int)strlen(text);
	if (fill_alpha >= 1.0 && outline_alpha >= 1.0) {
		for (dx = -s; dx <= s; dx++) {
			for (dy = -s; dy <= s; dy++) {
				if (dx == 0 && dy == 0)
					continue;
				XftDrawStringUtf8(draw, &bg, f, x + dx,
				    y + dy, (const FcChar8 *)text, len);
			}
		}
		XftDrawStringUtf8(draw, &fg, f, x, y, (const FcChar8 *)text,
		    len);
		return;
	}

	XftTextExtentsUtf8(dpy, f, (const FcChar8 *)text, len, &e);
	ox = x - (int)e.x - s;
	oy = y - (int)e.y - s;
	iw = (int)e.width + 2 * s;
	ih = (int)e.height + 2 * s;
	if (iw < 1)
		iw = 1;
	if (ih < 1)
		ih = 1;
	lx = x - ox;
	ly = y - oy;

	if (s > 0 && outline_alpha > 0.0)
		opix = stamp_xft(f, lx, ly, text, len, s, 0, iw, ih);
	if (fill_alpha > 0.0 || opix != None)
		fpix = stamp_xft(f, lx, ly, text, len, s, 1, iw, ih);
	/* Even when -A is 0 we need the fill mask to hollow the halo. */
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

/* Captions above/below the main content; y is its baseline. */
static void
draw_caps(const struct show_req *req, int y)
{
	XGlyphInfo e;
	int cx, cy, gap;

	if (cfont == NULL)
		return;
	gap = stroke * 2 + 16;
	if (req->prefix[0] != '\0') {
		XftTextExtentsUtf8(dpy, cfont, (const FcChar8 *)req->prefix,
		    (int)strlen(req->prefix), &e);
		cx = (win_w - (int)e.width) / 2 + e.x + req->x_off;
		cy = y - font->ascent - gap - cfont->descent;
		draw_outlined(cfont, cx, cy, req->prefix, cstroke);
	}
	if (req->append[0] != '\0') {
		XftTextExtentsUtf8(dpy, cfont, (const FcChar8 *)req->append,
		    (int)strlen(req->append), &e);
		cx = (win_w - (int)e.width) / 2 + e.x + req->x_off;
		cy = y + font->descent + gap + cfont->ascent;
		draw_outlined(cfont, cx, cy, req->append, cstroke);
	}
}

/* Size the overlay to the panel and stand up fonts, draw, colors. */
int
countdown_begin(const struct show_req *req)
{
	int screen = DefaultScreen(dpy), pointsize;

	FcInit();
	lock_len = 0;
	if (req->text)
		decode_escapes(req->spec, text_buf, sizeof(text_buf));
	if (layout_fullscreen() != 0)
		return (-1);

	pointsize = (int)((scr_h / 2) * 85 / 100 * req->scale + 0.5);
	if (pointsize < 16)
		pointsize = 16;
	if (req->scale <= 1.0 && pointsize > 612)
		pointsize = 612;
	font = open_fit_font(screen, scr_w, scr_h, &pointsize,
	    req->text ? text_buf : NULL);
	if (font == NULL)
		return (-1);
	stroke = req->outline ? stroke_for(pointsize) : 0;
	fill_alpha = req->alpha >= 0.0 ? req->alpha : 1.0;
	outline_alpha = req->outline_alpha;
	if (req->badge[0] != '\0') {
		char pattern[128];
		int bps = pointsize * 26 / 100;

		snprintf(pattern, sizeof(pattern),
		    "DejaVu Sans:size=%d:antialias=true",
		    bps < 16 ? 16 : bps);
		bfont = XftFontOpenName(dpy, screen, pattern);
	}
	if (req->prefix[0] != '\0' || req->append[0] != '\0') {
		char pattern[128];
		int cps = pointsize * 12 / 100;

		snprintf(pattern, sizeof(pattern),
		    "DejaVu Sans:bold:size=%d:antialias=true",
		    cps < 20 ? 20 : cps);
		cfont = XftFontOpenName(dpy, screen, pattern);
		cstroke = stroke > 0 ?
		    (stroke / 4 < 2 ? 2 : stroke / 4) : 0;
	}

	draw = XftDrawCreate(dpy, win, visual, cmap);
	if (draw == NULL ||
	    !XftColorAllocName(dpy, visual, cmap, "white", &fg) ||
	    !XftColorAllocName(dpy, visual, cmap, "black", &bg)) {
		countdown_end();
		return (-1);
	}
	return (0);
}

void
countdown_tick(const struct show_req *req, int digit)
{
	XGlyphInfo extents, ref;
	XftColor clear;
	char buf[16];
	int x, y, bx, by, bstroke, gap, len;

	raise_overlay();
	memset(&clear, 0, sizeof(clear));
	XftDrawRect(draw, &clear, 0, 0, (unsigned)win_w, (unsigned)win_h);

	snprintf(buf, sizeof(buf), "%d", digit);
	len = (int)strlen(buf);
	XftTextExtentsUtf8(dpy, font, (const FcChar8 *)buf, len, &extents);
	bstroke = 0;
	if (bfont != NULL && stroke > 0)
		bstroke = stroke / 3 < 3 ? 3 : stroke / 3;
	/* Same idea as caption clearance: past both outlines, plus air. */
	gap = stroke * 2 + bstroke + 28;
	if (len > 1) {
		/*
		 * Variable-width digits jitter if recentered per tick:
		 * lock a position per string length, recentering only
		 * as each digit is lost.
		 */
		if (len != lock_len) {
			lock_len = len;
			lock_x = win_w / 2 - stroke / 2 -
			    (int)extents.width / 2 + (int)extents.x;
			lock_bx = lock_x - (int)extents.x +
			    (int)extents.width + gap;
		}
		x = lock_x + req->x_off;
		bx = lock_bx + req->x_off;
	} else {
		/* Last digits: recenter each in the widest-digit slot. */
		ref_digit_extents(&ref);
		/* Outline adds visual weight; nudge left by half stroke. */
		x = win_w / 2 - stroke / 2 - (int)ref.width / 2 -
		    (int)ref.x + ((int)ref.width - (int)extents.width) / 2 +
		    ((int)extents.x - (int)ref.x) + req->x_off;
		/* Badge stays on the slot's right, not the current digit. */
		bx = win_w / 2 - stroke / 2 + (int)ref.width / 2 + gap +
		    req->x_off;
	}
	y = (win_h + font->ascent - font->descent) / 2 + req->y_off;
	draw_outlined(font, x, y, buf, stroke);

	/* Badge superscript off the digits' upper right (slot-stable). */
	if (bfont != NULL) {
		by = y - font->ascent + bfont->ascent - bfont->ascent / 5;
		draw_outlined(bfont, bx, by, req->badge, bstroke);
	}
	draw_caps(req, y);
	XFlush(dpy);
}

/* One centered render of the decoded text (e.g. the checkmark). */
void
text_tick(const struct show_req *req)
{
	XGlyphInfo extents;
	XftColor clear;
	int x, y, bx, by, bstroke;

	raise_overlay();
	memset(&clear, 0, sizeof(clear));
	XftDrawRect(draw, &clear, 0, 0, (unsigned)win_w, (unsigned)win_h);

	XftTextExtentsUtf8(dpy, font, (const FcChar8 *)text_buf,
	    (int)strlen(text_buf), &extents);
	/* Outline adds visual weight; nudge left by half stroke. */
	x = win_w / 2 - stroke / 2 - (int)extents.width / 2 +
	    (int)extents.x + req->x_off;
	y = (win_h + font->ascent - font->descent) / 2 + req->y_off;
	draw_outlined(font, x, y, text_buf, stroke);

	if (bfont != NULL) {
		bstroke = stroke > 0 ?
		    (stroke / 3 < 3 ? 3 : stroke / 3) : 0;
		bx = x - (int)extents.x + (int)extents.width +
		    stroke * 2 + bstroke + 28;
		by = y - font->ascent + bfont->ascent - bfont->ascent / 5;
		draw_outlined(bfont, bx, by, req->badge, bstroke);
	}
	draw_caps(req, y);
	XFlush(dpy);
}

void
countdown_end(void)
{
	if (draw != NULL) {
		XftColorFree(dpy, visual, cmap, &fg);
		XftColorFree(dpy, visual, cmap, &bg);
		XftDrawDestroy(draw);
		draw = NULL;
	}
	if (bfont != NULL) {
		XftFontClose(dpy, bfont);
		bfont = NULL;
	}
	if (cfont != NULL) {
		XftFontClose(dpy, cfont);
		cfont = NULL;
	}
	if (font != NULL) {
		XftFontClose(dpy, font);
		font = NULL;
	}
}

/* After the artwork comes down, let a gauge finish its own hold. */
static void
bar_linger(const struct show_req *req, double bstart)
{
	double left;

	if (req->gauge < 0 || stop)
		return;
	if (req->gauge_hold < 0.0) {	/* until SIGINT/SIGTERM */
		while (!stop)
			usleep(100000);
	} else {
		left = bstart + req->gauge_hold - now_monotonic();
		if (left > 0.0)
			hold_exact(left);
	}
	bar_hide();
}

/* One-shot fallback: no daemon on the channel, draw it ourselves. */
int
run_countdown(const struct show_req *req)
{
	double bstart;
	int i;

	if (init_display() != 0)
		return (1);
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);

	if (countdown_begin(req) != 0)
		return (1);
	bstart = now_monotonic();
	if (req->gauge >= 0)
		bar_show(req);
	for (i = req->count; i >= 1 && !stop; i--) {
		countdown_tick(req, i);
		hold_exact(req->hold);
	}
	countdown_end();
	hide_overlay();
	bar_linger(req, bstart);
	x11_cleanup();
	return (0);
}

/* One-shot fallback for a text show. */
int
run_text(const struct show_req *req)
{
	double bstart;

	if (init_display() != 0)
		return (1);
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);

	if (countdown_begin(req) != 0)
		return (1);
	bstart = now_monotonic();
	if (req->gauge >= 0)
		bar_show(req);
	text_tick(req);
	hold_exact(req->hold);
	countdown_end();
	hide_overlay();
	bar_linger(req, bstart);
	x11_cleanup();
	return (0);
}
