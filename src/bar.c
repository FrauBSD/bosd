/*
 * Gauge bar: the classic tick-bar OSD.  56 ticks bottom-centered on
 * the panel; tall ticks fill to the given percentage, the rest stay
 * short.  Geometry scales with panel height from curated pixel sizes
 * at BAR_REF_H (40px ticks, 64px bottom clearance on a 1200-tall
 * panel); the overage label face is sized to the tick height, not the
 * other way around.  By default ticks (and the overage label) carry a
 * black outline; -A sets fill opacity, -O the outline's, and -o skips
 * the outline.  bosd assigns the bar no meaning: it draws the given
 * percentage in the given color.  Above 100% the fill stays full and
 * "N%" sits just past the bar's right edge in the same color,
 * vertically centered on the gauge bar.
 *
 * An optional previous percent (daemon-latched for a bar session)
 * marks short ticks at or above that watermark in a 50% dimmer
 * shade of the fill; short ticks between the current fill and the
 * watermark stay full color.  Tall ticks are never dimmed.
 *
 * The bar owns an ARGB window so a gauge and a glyph coexist on
 * one channel; translucency comes from the pixel alpha, not a
 * 1-bit shape mask.
 */
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/Xrender.h>

#include "priv.h"

#define BAR_TICKS	56	/* tick count across the bar */
/* Curated sizes below apply when scr_h == BAR_REF_H; elsewhere scale. */
#define BAR_REF_H	1200	/* panel height those pixels were tuned for */
#define BAR_REF_TICK	40	/* tall tick height at BAR_REF_H */
#define BAR_REF_VOFF	64	/* bottom clearance at BAR_REF_H */
#define BAR_REF_EXTRA	10	/* lineh pad beyond tick+outline at ref */
#define BAR_REF_OUTL	2	/* outline thickness at BAR_REF_H */
#define BAR_REF_OGAP	20	/* overage label gap at ref */
#define BAR_REF_XOFF	10	/* label inset at ref */
#define BAR_Y_NUDGE	(-2)	/* raise the band this many px at BAR_REF_H */

static Window	 bwin;
static Pixmap	 bpix;		/* ARGB backing; survives Expose */
static Colormap	 bcmap;
static Visual	*bvisual;
static XftFont	*bar_font;	/* overage label; fit to tick_h */
static int	 bdepth;
static int	 bmapped;
static int	 tick_h, pitch, lineh, voff, outl, over_gap, text_xoff;
static int	 y_nudge;
static int	 font_for_tick;	/* tick_h used to open bar_font */

/*
 * Panel-proportional geometry from the primary output's height
 * (scr_h after refresh_screen_geom; never the X virtual desktop).
 * At scr_h == BAR_REF_H the curated pixel sizes are unchanged;
 * other primary heights scale from that baseline.
 */
static void
bar_geom(void)
{
	int h = scr_h > 0 ? scr_h : BAR_REF_H;

	tick_h = BAR_REF_TICK * h / BAR_REF_H;
	if (tick_h < 8)
		tick_h = 8;
	pitch = tick_h / 2;
	if (pitch < 1)
		pitch = 1;
	outl = BAR_REF_OUTL * h / BAR_REF_H;
	if (outl < 1)
		outl = 1;
	lineh = tick_h + BAR_REF_EXTRA * h / BAR_REF_H + 2 * outl;
	if (lineh < tick_h + 2 * outl)
		lineh = tick_h + 2 * outl;
	voff = BAR_REF_VOFF * h / BAR_REF_H;
	if (voff < 1)
		voff = 1;
	over_gap = BAR_REF_OGAP * h / BAR_REF_H;
	if (over_gap < 1)
		over_gap = 1;
	text_xoff = BAR_REF_XOFF * h / BAR_REF_H;
	if (text_xoff < 1)
		text_xoff = 1;
	y_nudge = BAR_Y_NUDGE * h / BAR_REF_H;
}

/*
 * Open BOSD_FIXED_FACE so its ascent fits the panel-derived tick
 * height.  Largest pixelsize with ascent <= tick_h wins.
 */
static int
bar_ensure_font(void)
{
	char pattern[BOSD_FONT_MAX + 64];
	char attrs[64];
	XftFont *f;
	int px, screen;

	bar_geom();
	if (bar_font != NULL && font_for_tick == tick_h)
		return (0);
	if (bar_font != NULL) {
		XftFontClose(dpy, bar_font);
		bar_font = NULL;
	}
	screen = DefaultScreen(dpy);
	for (px = tick_h + 12; px >= 8; px--) {
		snprintf(attrs, sizeof(attrs),
		    "pixelsize=%d:antialias=true", px);
		font_pattern(pattern, sizeof(pattern), NULL,
		    BOSD_FIXED_FACE, attrs);
		f = XftFontOpenName(dpy, screen, pattern);
		if (f == NULL) {
			font_pattern(pattern, sizeof(pattern), NULL, "Sans",
			    attrs);
			f = XftFontOpenName(dpy, screen, pattern);
		}
		if (f == NULL)
			continue;
		if (f->ascent <= tick_h) {
			bar_font = f;
			font_for_tick = tick_h;
			return (0);
		}
		XftFontClose(dpy, f);
	}
	return (-1);
}

/*
 * How far up from the panel bottom the gauge band reaches (voff
 * plus the tick window).  Used so -t captions sit entirely above it.
 */
int
bar_band_height(void)
{
	bar_geom();
	return (voff + lineh);
}

/* ARGB click-through window for translucent ticks. */
static Window
bar_window(int x, int y, int w, int h)
{
	Window swin;

	swin = argb_osd_window(x, y, w, h, &bvisual, &bcmap, &bdepth, 0);
	if (swin != 0)
		XStoreName(dpy, swin, "bosd");
	return (swin);
}

/* Replace a rectangle in an ARGB picture (outline then fill). */
static void
fill_rect_pic(Picture pic, int x, int y, int fw, int fh, unsigned char r,
    unsigned char g, unsigned char b, unsigned char a)
{
	XRenderColor c;

	if (a == 0 || fw <= 0 || fh <= 0)
		return;
	xrender_color_premul(r, g, b, a, &c);
	XRenderFillRectangle(dpy, PictOpSrc, pic, &c, x, y, fw, fh);
}

/* One tick into the ARGB picture. */
static void
bar_tick(Picture pic, int i, int bx, int grow, int tall, unsigned char r,
    unsigned char g, unsigned char b, unsigned char a)
{
	int x = bx + i * pitch;
	int rx, ry, rwi, rhi;

	if (tall) {
		rx = x - grow;
		ry = 0 - grow + outl;
		rwi = pitch * 7 / 10 + 2 * grow;
		rhi = tick_h + 2 * grow;
	} else {
		rx = x - grow;
		ry = tick_h / 3 - grow + outl;
		rwi = pitch * 8 / 10 + 2 * grow;
		rhi = tick_h / 3 + 2 * grow;
	}
	fill_rect_pic(pic, rx, ry, rwi, rhi, r, g, b, a);
}

/*
 * Outline every tick (when out_a > 0), then color: tall always fill;
 * short ticks at or past the previous watermark use dim, the rest
 * (return zone) fill.  prev_on < 0 disables the watermark.
 */
static void
bar_paint(Picture pic, int on, int prev_on, int bx, unsigned char fr,
    unsigned char fg, unsigned char fb, unsigned char fa, unsigned char dr,
    unsigned char dg, unsigned char db, unsigned char out_a)
{
	int i;

	if (out_a > 0) {
		for (i = 0; i < BAR_TICKS; i++)
			bar_tick(pic, i, bx, outl, i < on, 0, 0, 0, out_a);
	}
	for (i = 0; i < on; i++)
		bar_tick(pic, i, bx, 0, 1, fr, fg, fb, fa);
	for (; i < BAR_TICKS; i++) {
		if (prev_on >= 0 && i >= prev_on)
			bar_tick(pic, i, bx, 0, 0, dr, dg, db, fa);
		else
			bar_tick(pic, i, bx, 0, 0, fr, fg, fb, fa);
	}
}

/*
 * Fixed-face label: ink from a scratch pixmap, stamped with
 * XRenderFillRectangle so alpha stays intact (XPutImage drops it).
 */
static void
stamp_label(Picture pic, int rw, int rh, const char *s, int x, int base,
    unsigned char r, unsigned char g, unsigned char b, unsigned char a,
    int grow_pass)
{
	Pixmap tmp;
	GC tgc;
	XImage *img;
	XftDraw *td;
	XftColor ink;
	Visual *vis;
	Colormap cm;
	int screen = DefaultScreen(dpy);
	int depth = DefaultDepth(dpy, screen);
	unsigned long bg = WhitePixel(dpy, screen);
	int len = (int)strlen(s), dx, dy, xi, yi;

	if (a == 0 || bar_font == NULL)
		return;
	vis = DefaultVisual(dpy, screen);
	cm = DefaultColormap(dpy, screen);
	tmp = XCreatePixmap(dpy, RootWindow(dpy, screen), (unsigned)rw,
	    (unsigned)rh, depth);
	if (tmp == 0)
		return;
	tgc = XCreateGC(dpy, tmp, 0, NULL);
	XSetForeground(dpy, tgc, bg);
	XFillRectangle(dpy, tmp, tgc, 0, 0, (unsigned)rw, (unsigned)rh);
	XFreeGC(dpy, tgc);

	td = XftDrawCreate(dpy, tmp, vis, cm);
	if (td == NULL ||
	    !XftColorAllocName(dpy, vis, cm, "black", &ink)) {
		if (td != NULL)
			XftDrawDestroy(td);
		XFreePixmap(dpy, tmp);
		return;
	}
	if (grow_pass) {
		for (dx = -outl; dx <= outl; dx++)
			for (dy = -outl; dy <= outl; dy++) {
				if (dx == 0 && dy == 0)
					continue;
				XftDrawStringUtf8(td, &ink, bar_font,
				    x + dx, base + dy,
				    (const FcChar8 *)s, len);
			}
	} else
		XftDrawStringUtf8(td, &ink, bar_font, x, base,
		    (const FcChar8 *)s, len);
	XftColorFree(dpy, vis, cm, &ink);
	XftDrawDestroy(td);

	img = XGetImage(dpy, tmp, 0, 0, (unsigned)rw, (unsigned)rh,
	    AllPlanes, ZPixmap);
	XFreePixmap(dpy, tmp);
	if (img == NULL)
		return;
	for (yi = 0; yi < rh; yi++) {
		for (xi = 0; xi < rw; xi++) {
			if (XGetPixel(img, xi, yi) == bg)
				continue;
			fill_rect_pic(pic, xi, yi, 1, 1, r, g, b, a);
		}
	}
	XDestroyImage(img);
}

/*
 * Build the bar in an ARGB32 picture (XRender fills keep alpha), blit
 * to the mapped window, and keep the pixmap as background for Expose.
 */
static int
paint_bar(Picture src_pic, Pixmap pix, int w, int h)
{
	XRenderPictFormat *fmt;
	Picture dst;
	XRenderColor clear;

	fmt = XRenderFindVisualFormat(dpy, bvisual);
	if (fmt == NULL)
		return (-1);
	dst = XRenderCreatePicture(dpy, bwin, fmt, 0, NULL);
	clear.red = clear.green = clear.blue = clear.alpha = 0;
	XRenderFillRectangle(dpy, PictOpSrc, dst, &clear, 0, 0, w, h);
	XRenderComposite(dpy, PictOpSrc, src_pic, None, dst, 0, 0, 0, 0, 0, 0,
	    w, h);
	XRenderFreePicture(dpy, dst);

	if (bpix != 0 && bpix != pix) {
		XSetWindowBackgroundPixmap(dpy, bwin, None);
		XFreePixmap(dpy, bpix);
	}
	bpix = pix;
	XSetWindowBackgroundPixmap(dpy, bwin, bpix);

	shape_bounding_rect(bwin, w, h);
	XFlush(dpy);
	return (0);
}

static int
pct_ticks(int pct)
{
	if (pct < 0)
		pct = 0;
	if (pct > 100)
		pct = 100;
	return (BAR_TICKS * pct / 100);
}

int
bar_show(const struct show_req *req)
{
	XRenderPictFormat *fmt;
	XRenderColor clear;
	XColor col, exact;
	Colormap cmap;
	Picture pic;
	Pixmap pix;
	char label[16];
	int screen = DefaultScreen(dpy);
	int on, prev_on, bx, x, y, w;
	unsigned char fr, fg, fb, fa, dr, dg, db, oa;
	double fill_a, out_a;

	bar_geom();
	if (req->gauge > 100 && bar_ensure_font() != 0)
		return (-1);
	w = scr_w;
	x = scr_x + req->x_off;
	/* y_nudge shifts the band (positive down) from the curated seat. */
	y = scr_y + scr_h - lineh - voff + y_nudge + req->y_off;
	if (bwin == 0 && (bwin = bar_window(x, y, w, lineh)) == 0)
		return (-1);
	XMoveResizeWindow(dpy, bwin, x, y, (unsigned)w, (unsigned)lineh);

	fmt = XRenderFindStandardFormat(dpy, PictStandardARGB32);
	if (fmt == NULL)
		return (-1);
	pix = XCreatePixmap(dpy, bwin, (unsigned)w, (unsigned)lineh, 32);
	pic = XRenderCreatePicture(dpy, pix, fmt, 0, NULL);
	clear.red = clear.green = clear.blue = clear.alpha = 0;
	XRenderFillRectangle(dpy, PictOpSrc, pic, &clear, 0, 0, w, lineh);

	clamp_paint_alphas(req, &fill_a, &out_a);
	fa = (unsigned char)(fill_a * 255.0 + 0.5);
	oa = (unsigned char)(out_a * 255.0 + 0.5);

	cmap = DefaultColormap(dpy, screen);
	fr = fg = fb = 255;
	if (XAllocNamedColor(dpy, cmap,
	    req->color[0] != '\0' ? req->color : BOSD_GAUGE_DEF,
	    &col, &exact)) {
		fr = (unsigned char)(col.red >> 8);
		fg = (unsigned char)(col.green >> 8);
		fb = (unsigned char)(col.blue >> 8);
	}
	dr = fr / 2;
	dg = fg / 2;
	db = fb / 2;

	on = pct_ticks(req->gauge);
	prev_on = req->gauge_prev < 0 ? -1 : pct_ticks(req->gauge_prev);
	bx = (w - BAR_TICKS * pitch) / 2;

	bar_paint(pic, on, prev_on, bx, fr, fg, fb, fa, dr, dg, db, oa);

	if (req->gauge > 100) {
		XGlyphInfo e;
		int base, len;

		snprintf(label, sizeof(label), "%d%%", req->gauge);
		len = (int)strlen(label);
		XftTextExtentsUtf8(dpy, bar_font, (const FcChar8 *)label,
		    len, &e);
		/*
		 * Tall ticks span [outl, outl+tick_h).  Place the baseline
		 * so the label's ink centerline matches the bar's.
		 * XGlyphInfo.y is the (positive) rise from baseline to
		 * ink top.
		 */
		base = outl + tick_h / 2 + (int)e.y - (int)e.height / 2;
		x = (w + BAR_TICKS * pitch) / 2 + over_gap + text_xoff;
		if (oa > 0)
			stamp_label(pic, w, lineh, label, x, base, 0, 0, 0,
			    oa, 1);
		stamp_label(pic, w, lineh, label, x, base, fr, fg, fb, fa,
		    0);
	}

	raise_mapped(bwin, &bmapped);

	if (paint_bar(pic, pix, w, lineh) != 0) {
		XRenderFreePicture(dpy, pic);
		XFreePixmap(dpy, pix);
		return (-1);
	}
	XRenderFreePicture(dpy, pic);
	/* pix retained as bpix inside paint_bar */
	XSync(dpy, False);
	return (0);
}

void
bar_hide(void)
{
	if (bmapped) {
		XUnmapWindow(dpy, bwin);
		bmapped = 0;
	}
	/*
	 * Destroy the window so a later show cannot remap a compositor-
	 * cached frame of the previous fill (seen as the last percent
	 * after the bar had already timed out).
	 */
	if (bpix != 0) {
		if (bwin != 0)
			XSetWindowBackgroundPixmap(dpy, bwin, None);
		XFreePixmap(dpy, bpix);
		bpix = 0;
	}
	if (bwin != 0) {
		XDestroyWindow(dpy, bwin);
		bwin = 0;
	}
	if (bcmap != None) {
		XFreeColormap(dpy, bcmap);
		bcmap = None;
	}
	bvisual = NULL;
	XSync(dpy, False);
}

void
bar_cleanup(void)
{
	if (dpy == NULL)
		return;
	if (bpix != 0) {
		if (bwin != 0)
			XSetWindowBackgroundPixmap(dpy, bwin, None);
		XFreePixmap(dpy, bpix);
		bpix = 0;
	}
	if (bwin != 0) {
		XDestroyWindow(dpy, bwin);
		bwin = 0;
	}
	if (bcmap != None) {
		XFreeColormap(dpy, bcmap);
		bcmap = None;
	}
	bvisual = NULL;
	if (bar_font != NULL) {
		XftFontClose(dpy, bar_font);
		bar_font = NULL;
		font_for_tick = 0;
	}
	bmapped = 0;
}

/* One-shot fallback: no daemon on the channel, draw it ourselves. */
int
run_bar(const struct show_req *req)
{
	double deadline, hold;

	hold = req->gauge_hold;
	if (hold != -1.0 && hold <= 0.0)
		hold = BOSD_GAUGE_HOLD_DEF;
	if (init_display() != 0)
		return (1);
	signal(SIGTERM, cleanup);
	signal(SIGINT, cleanup);
	if (bar_show(req) != 0)
		return (1);
	deadline = hold < 0.0 ? -1.0 : now_monotonic() + hold;
	while (deadline < 0.0 || now_monotonic() < deadline)
		poll(NULL, 0, 50);
	bar_hide();
	cleanup(0);
	return (0);
}
