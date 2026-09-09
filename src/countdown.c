/*
 * Countdown show — the screenshot timer: huge outlined Xft digits on
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
#include <fontconfig/fontconfig.h>

#include "bosd.h"

static XftFont	*font, *bfont;
static XftDraw	*draw;
static XftColor	 fg, bg;
static int	 stroke;
static int	 lock_len, lock_x, lock_bx;

static void
on_signal(int sig __unused)
{
	stop = 1;
}

/* Sleep to a monotonic deadline despite signal wakeups. */
static void
hold_exact(double seconds)
{
	double deadline = now_monotonic() + seconds;
	double t;

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

/* Largest tabular bold face that fits the panel with outline air. */
static XftFont *
open_fit_font(int screen, int w, int h, int *pointsize)
{
	char pattern[128];
	XftFont *f;
	int ps = *pointsize, s;

	while (ps >= 16) {
		s = stroke_for(ps);
		snprintf(pattern, sizeof(pattern),
		    "DejaVu Sans:bold:size=%d:antialias=true:tabular=1", ps);
		f = XftFontOpenName(dpy, screen, pattern);
		if (f == NULL)
			break;
		if (f->ascent + f->descent + s * 2 + 32 <= h &&
		    (int)f->max_advance_width + s * 2 + 32 <= w) {
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

static void
draw_outlined(XftFont *f, int x, int y, const char *text, int s)
{
	int dx, dy, len = (int)strlen(text);

	for (dx = -s; dx <= s; dx++) {
		for (dy = -s; dy <= s; dy++) {
			if (dx == 0 && dy == 0)
				continue;
			XftDrawStringUtf8(draw, &bg, f, x + dx, y + dy,
			    (const FcChar8 *)text, len);
		}
	}
	XftDrawStringUtf8(draw, &fg, f, x, y, (const FcChar8 *)text, len);
}

/* Size the overlay to the panel and stand up fonts, draw, colors. */
int
countdown_begin(const struct show_req *req)
{
	int screen = DefaultScreen(dpy), pointsize;

	FcInit();
	lock_len = 0;
	if (layout_fullscreen() != 0)
		return (-1);

	pointsize = (int)((scr_h / 2) * 85 / 100 * req->scale + 0.5);
	if (pointsize < 16)
		pointsize = 16;
	if (req->scale <= 1.0 && pointsize > 612)
		pointsize = 612;
	font = open_fit_font(screen, scr_w, scr_h, &pointsize);
	if (font == NULL)
		return (-1);
	stroke = req->outline ? stroke_for(pointsize) : 0;
	if (req->badge[0] != '\0') {
		char pattern[128];
		int bps = pointsize * 26 / 100;

		snprintf(pattern, sizeof(pattern),
		    "DejaVu Sans:bold:size=%d:antialias=true",
		    bps < 16 ? 16 : bps);
		bfont = XftFontOpenName(dpy, screen, pattern);
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
	int x, y, bx, by, bstroke, len;

	raise_overlay();
	memset(&clear, 0, sizeof(clear));
	XftDrawRect(draw, &clear, 0, 0, (unsigned)win_w, (unsigned)win_h);

	snprintf(buf, sizeof(buf), "%d", digit);
	len = (int)strlen(buf);
	XftTextExtentsUtf8(dpy, font, (const FcChar8 *)buf, len, &extents);
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
			    (int)extents.width + stroke + 8;
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
		bx = win_w / 2 - stroke / 2 + (int)ref.width / 2 + stroke +
		    8 + req->x_off;
	}
	y = (win_h + font->ascent - font->descent) / 2 + req->y_off;
	draw_outlined(font, x, y, buf, stroke);

	/* Badge superscript off the digits' upper right. */
	if (bfont != NULL) {
		by = y - font->ascent + bfont->ascent;
		bstroke = stroke > 0 ?
		    (stroke / 3 < 3 ? 3 : stroke / 3) : 0;
		draw_outlined(bfont, bx, by, req->badge, bstroke);
	}
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
	if (font != NULL) {
		XftFontClose(dpy, font);
		font = NULL;
	}
}

/* One-shot fallback: no daemon on the channel, draw it ourselves. */
int
run_countdown(const struct show_req *req)
{
	int i;

	if (init_display() != 0)
		return (1);
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);

	if (countdown_begin(req) != 0)
		return (1);
	for (i = req->count; i >= 1 && !stop; i--) {
		countdown_tick(req, i);
		hold_exact(req->hold);
	}
	countdown_end();
	hide_overlay();
	x11_cleanup();
	return (0);
}
