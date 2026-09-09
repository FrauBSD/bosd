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
#include <fontconfig/fontconfig.h>

#include "bosd.h"

static XftFont	*font, *bfont;
static XftDraw	*draw;
static XftColor	 fg, bg;
static int	 stroke;
static int	 lock_len, lock_x, lock_bx;
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

static int
xdigit(int c)
{
	if (c >= '0' && c <= '9')
		return (c - '0');
	if (c >= 'a' && c <= 'f')
		return (c - 'a' + 10);
	if (c >= 'A' && c <= 'F')
		return (c - 'A' + 10);
	return (-1);
}

static size_t
utf8_put(char *out, unsigned long cp)
{
	if (cp < 0x80) {
		out[0] = (char)cp;
		return (1);
	}
	if (cp < 0x800) {
		out[0] = (char)(0xc0 | (cp >> 6));
		out[1] = (char)(0x80 | (cp & 0x3f));
		return (2);
	}
	if (cp < 0x10000) {
		out[0] = (char)(0xe0 | (cp >> 12));
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
		out[2] = (char)(0x80 | (cp & 0x3f));
		return (3);
	}
	if (cp < 0x110000) {
		out[0] = (char)(0xf0 | (cp >> 18));
		out[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
		out[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
		out[3] = (char)(0x80 | (cp & 0x3f));
		return (4);
	}
	return (0);
}

/*
 * Decode \xNN (raw byte), \uNNNN and \UNNNNNNNN (codepoint, UTF-8
 * encoded), and \\.  Malformed escapes pass through literally.
 */
void
decode_escapes(const char *in, char *out, size_t outlen)
{
	size_t o = 0;

	while (*in != '\0' && o + 5 < outlen) {
		unsigned long cp = 0;
		int i, n, v;

		if (in[0] != '\\') {
			out[o++] = *in++;
			continue;
		}
		switch (in[1]) {
		case '\\':
			out[o++] = '\\';
			in += 2;
			continue;
		case 'x':
			n = 2;
			break;
		case 'u':
			n = 4;
			break;
		case 'U':
			n = 8;
			break;
		default:
			out[o++] = *in++;
			continue;
		}
		for (i = 0; i < n; i++) {
			v = xdigit((unsigned char)in[2 + i]);
			if (v < 0)
				break;
			cp = cp * 16 + (unsigned long)v;
		}
		if (i < n) {
			out[o++] = *in++;
			continue;
		}
		if (in[1] == 'x')
			out[o++] = (char)cp;
		else
			o += utf8_put(out + o, cp);
		in += 2 + n;
	}
	out[o] = '\0';
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
		bx = x - (int)extents.x + (int)extents.width + stroke + 8;
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

/* One-shot fallback for a text show. */
int
run_text(const struct show_req *req)
{
	if (init_display() != 0)
		return (1);
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);

	if (countdown_begin(req) != 0)
		return (1);
	text_tick(req);
	hold_exact(req->hold);
	countdown_end();
	hide_overlay();
	x11_cleanup();
	return (0);
}
