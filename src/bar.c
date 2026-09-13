/*
 * Gauge bar: the classic tick-bar OSD.  56 ticks bottom-centered
 * on the panel, 64px up, sized from 52px fixed-font metrics; tall
 * ticks fill to the given percentage, the rest stay short.  By
 * default ticks (and the overage label) carry a black outline; -A
 * sets fill opacity, -O the outline's, and -o skips the outline.
 * bosd assigns the bar no meaning: it draws the given percentage
 * in the given color.  Above 100% the fill stays full and "N%"
 * sits just past the bar's right edge in the same color,
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
#include <X11/extensions/Xrender.h>

#include "priv.h"

#define BAR_FONT   "-misc-fixed-medium-r-normal--52-*-*-*-*-*-*"
#define BAR_TICKS  56	/* tick count across the bar */
#define BAR_VOFF   64	/* pixels up from the panel bottom */
#define BAR_OUTL   2	/* black outline thickness */
#define OVER_GAP   20	/* label gap past the bar's right edge */
#define TEXT_XOFF  10	/* label inset within its slot */

static Window	 bwin;
static Pixmap	 bpix;		/* ARGB backing; survives Expose */
static Colormap	 bcmap;
static Visual	*bvisual;
static XFontSet	 fset;
static int	 ascent, lineh;	/* -extent.y and drawn line height */
static int	 bdepth;
static int	 bmapped;

/* The classic bar's metrics: tick pitch is half the font ascent. */
static int
bar_metrics(void)
{
	XFontSetExtents *ex;
	char **missing;
	int nmissing;
	char *def;

	if (fset != NULL)
		return (0);
	fset = XCreateFontSet(dpy, BAR_FONT, &missing, &nmissing, &def);
	if (missing != NULL)
		XFreeStringList(missing);
	if (fset == NULL)
		return (-1);
	ex = XExtentsOfFontSet(fset);
	ascent = -ex->max_logical_extent.y;
	if (ascent < 2)
		ascent = 40;
	lineh = ex->max_logical_extent.height + 2 * BAR_OUTL;
	return (0);
}

/*
 * How far up from the panel bottom the gauge band reaches (BAR_VOFF
 * plus the tick window).  Used so -t captions sit entirely above it.
 */
int
bar_band_height(void)
{
	if (bar_metrics() != 0)
		return (BAR_VOFF + 56);
	return (BAR_VOFF + lineh);
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
	int x = bx + i * (ascent / 2);
	int rx, ry, rwi, rhi;

	if (tall) {
		rx = x - grow;
		ry = 0 - grow + BAR_OUTL;
		rwi = (ascent / 2) * 7 / 10 + 2 * grow;
		rhi = ascent + 2 * grow;
	} else {
		rx = x - grow;
		ry = ascent / 3 - grow + BAR_OUTL;
		rwi = (ascent / 2) * 8 / 10 + 2 * grow;
		rhi = ascent / 3 + 2 * grow;
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
			bar_tick(pic, i, bx, BAR_OUTL, i < on, 0, 0, 0, out_a);
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
 * Fixed-font label: ink from a scratch pixmap, stamped with
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
	int screen = DefaultScreen(dpy);
	int depth = DefaultDepth(dpy, screen);
	unsigned long bg = WhitePixel(dpy, screen);
	unsigned long fg = BlackPixel(dpy, screen);
	int len = (int)strlen(s), dx, dy, xi, yi;

	if (a == 0)
		return;
	tmp = XCreatePixmap(dpy, RootWindow(dpy, screen), (unsigned)rw,
	    (unsigned)rh, depth);
	if (tmp == 0)
		return;
	tgc = XCreateGC(dpy, tmp, 0, NULL);
	XSetForeground(dpy, tgc, bg);
	XFillRectangle(dpy, tmp, tgc, 0, 0, (unsigned)rw, (unsigned)rh);
	XSetForeground(dpy, tgc, fg);
	if (grow_pass) {
		for (dx = -BAR_OUTL; dx <= BAR_OUTL; dx++)
			for (dy = -BAR_OUTL; dy <= BAR_OUTL; dy++) {
				if (dx == 0 && dy == 0)
					continue;
				XmbDrawString(dpy, tmp, fset, tgc, x + dx,
				    base + dy, s, len);
			}
	} else
		XmbDrawString(dpy, tmp, fset, tgc, x, base, s, len);

	img = XGetImage(dpy, tmp, 0, 0, (unsigned)rw, (unsigned)rh,
	    AllPlanes, ZPixmap);
	XFreeGC(dpy, tgc);
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

	if (bar_metrics() != 0)
		return (-1);
	w = scr_w;
	x = scr_x + req->x_off;
	y = scr_y + scr_h - lineh - BAR_VOFF + req->y_off;
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
	bx = (w - BAR_TICKS * (ascent / 2)) / 2;

	bar_paint(pic, on, prev_on, bx, fr, fg, fb, fa, dr, dg, db, oa);

	if (req->gauge > 100) {
		XRectangle ink, logical;
		int base, len;

		snprintf(label, sizeof(label), "%d%%", req->gauge);
		len = (int)strlen(label);
		XmbTextExtents(fset, label, len, &ink, &logical);
		/*
		 * Tall ticks span [BAR_OUTL, BAR_OUTL+ascent).  Place the
		 * baseline so the label's ink centerline matches the bar's,
		 * not the old bottom-aligned baseline (ascent+BAR_OUTL).
		 */
		base = BAR_OUTL + ascent / 2 - (ink.y + ink.height / 2);
		x = (w + BAR_TICKS * (ascent / 2)) / 2 + OVER_GAP +
		    TEXT_XOFF;
		if (oa > 0)
			stamp_label(pic, w, lineh, label, x, base, 0, 0, 0,
			    oa, 1);
		stamp_label(pic, w, lineh, label, x, base, fr, fg, fb, fa, 0);
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
	if (fset != NULL) {
		XFreeFontSet(dpy, fset);
		fset = NULL;
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
