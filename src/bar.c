/*
 * Gauge bar: the classic tick-bar OSD.  56 ticks bottom-centered
 * on the panel, 64px up, sized from 52px fixed-font metrics; tall
 * ticks fill to the given percentage, the rest stay short, all
 * black-outlined.  bosd assigns the bar no meaning: it draws the
 * given percentage in the given color.  Above 100% the fill stays
 * full and "N%" sits just past the bar's right edge in the same
 * color.
 *
 * The bar owns its window, so a gauge and a glyph coexist on one
 * channel; drawn pixels become the XShape bounding mask.
 */
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>

#include "bosd.h"

#define BAR_FONT   "-misc-fixed-medium-r-normal--52-*-*-*-*-*-*"
#define BAR_TICKS  56	/* tick count across the bar */
#define BAR_VOFF   64	/* pixels up from the panel bottom */
#define BAR_OUTL   2	/* black outline thickness */
#define OVER_GAP   20	/* label gap past the bar's right edge */
#define TEXT_XOFF  10	/* label inset within its slot */

static Window	 bwin;
static Pixmap	 pix, mask;
static GC	 pgc, mgc;
static XFontSet	 fset;
static int	 ascent, lineh;	/* -extent.y and drawn line height */
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

static int
bar_window(int x, int y, int w, int h)
{
	XSetWindowAttributes wa;
	Atom net_wm_state, states[3];
	int screen = DefaultScreen(dpy);

	if (bwin != 0)
		return (0);
	wa.override_redirect = True;
	wa.background_pixel = BlackPixel(dpy, screen);
	bwin = XCreateWindow(dpy, RootWindow(dpy, screen), x, y,
	    (unsigned)w, (unsigned)h, 0, CopyFromParent, InputOutput,
	    CopyFromParent, CWOverrideRedirect | CWBackPixel, &wa);
	if (bwin == 0)
		return (-1);
	net_wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
	states[0] = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
	states[1] = XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False);
	states[2] = XInternAtom(dpy, "_NET_WM_STATE_SKIP_PAGER", False);
	XChangeProperty(dpy, bwin, net_wm_state, XA_ATOM, 32,
	    PropModeReplace, (unsigned char *)states, 3);
	/* Click-through. */
	XShapeCombineRectangles(dpy, bwin, ShapeInput, 0, 0, NULL, 0,
	    ShapeSet, Unsorted);
	return (0);
}

/* One tick pass: rectangles into the pixmap and the shape mask. */
static void
bar_rects(int on, int bx, int grow)
{
	XRectangle r;
	int i, x;

	for (i = 0, x = bx; i < BAR_TICKS; i++, x += ascent / 2) {
		if (i < on) {	/* tall tick: 0.7 pitch wide, full height */
			r.x = (short)(x - grow);
			r.y = (short)(0 - grow + BAR_OUTL);
			r.width = (unsigned short)
			    ((ascent / 2) * 7 / 10 + 2 * grow);
			r.height = (unsigned short)(ascent + 2 * grow);
		} else {	/* short tick: 0.8 pitch wide, 1/3 height */
			r.x = (short)(x - grow);
			r.y = (short)(ascent / 3 - grow + BAR_OUTL);
			r.width = (unsigned short)
			    ((ascent / 2) * 8 / 10 + 2 * grow);
			r.height = (unsigned short)(ascent / 3 + 2 * grow);
		}
		XFillRectangle(dpy, pix, pgc, r.x, r.y, r.width, r.height);
		XFillRectangle(dpy, mask, mgc, r.x, r.y, r.width, r.height);
	}
}

/* Overage label: square black outline passes, then the fill. */
static void
bar_label(const char *s, int x, int grow_pass, unsigned long pixel)
{
	int len = (int)strlen(s), dx, dy;

	XSetForeground(dpy, pgc, pixel);
	if (grow_pass) {
		for (dx = -BAR_OUTL; dx <= BAR_OUTL; dx++)
			for (dy = -BAR_OUTL; dy <= BAR_OUTL; dy++) {
				if (dx == 0 && dy == 0)
					continue;
				XmbDrawString(dpy, pix, fset, pgc, x + dx,
				    ascent + BAR_OUTL + dy, s, len);
				XmbDrawString(dpy, mask, fset, mgc, x + dx,
				    ascent + BAR_OUTL + dy, s, len);
			}
	} else {
		XmbDrawString(dpy, pix, fset, pgc, x, ascent + BAR_OUTL,
		    s, len);
		XmbDrawString(dpy, mask, fset, mgc, x, ascent + BAR_OUTL,
		    s, len);
	}
}

int
bar_show(const struct show_req *req)
{
	XColor col, exact;
	char label[16];
	int screen = DefaultScreen(dpy), depth;
	int fill, on, bx, x, y, w;
	unsigned long fill_px, black;

	if (bar_metrics() != 0)
		return (-1);
	w = scr_w;
	x = scr_x + req->x_off;
	y = scr_y + scr_h - lineh - BAR_VOFF + req->y_off;
	if (bar_window(x, y, w, lineh) != 0)
		return (-1);
	XMoveResizeWindow(dpy, bwin, x, y, (unsigned)w, (unsigned)lineh);

	depth = DefaultDepth(dpy, screen);
	if (pix != 0)
		XFreePixmap(dpy, pix);
	if (mask != 0)
		XFreePixmap(dpy, mask);
	pix = XCreatePixmap(dpy, bwin, (unsigned)w, (unsigned)lineh, depth);
	mask = XCreatePixmap(dpy, bwin, (unsigned)w, (unsigned)lineh, 1);
	if (pgc == None)
		pgc = XCreateGC(dpy, pix, 0, NULL);
	if (mgc == None)
		mgc = XCreateGC(dpy, mask, 0, NULL);

	XSetForeground(dpy, mgc, 0);
	XFillRectangle(dpy, mask, mgc, 0, 0, (unsigned)w, (unsigned)lineh);
	XSetForeground(dpy, mgc, 1);

	black = BlackPixel(dpy, screen);
	if (XAllocNamedColor(dpy, DefaultColormap(dpy, screen),
	    req->color[0] != '\0' ? req->color : BOSD_GAUGE_DEF,
	    &col, &exact))
		fill_px = col.pixel;
	else
		fill_px = WhitePixel(dpy, screen);

	fill = req->gauge > 100 ? 100 : (req->gauge < 0 ? 0 : req->gauge);
	on = BAR_TICKS * fill / 100;
	bx = (w - BAR_TICKS * (ascent / 2)) / 2;

	/* Outline pass (rects grown by 2), then the color pass. */
	XSetForeground(dpy, pgc, black);
	bar_rects(on, bx, BAR_OUTL);
	XSetForeground(dpy, pgc, fill_px);
	bar_rects(on, bx, 0);

	if (req->gauge > 100) {
		snprintf(label, sizeof(label), "%d%%", req->gauge);
		x = (w + BAR_TICKS * (ascent / 2)) / 2 + OVER_GAP +
		    TEXT_XOFF;
		bar_label(label, x, 1, black);
		bar_label(label, x, 0, fill_px);
	}

	XShapeCombineMask(dpy, bwin, ShapeBounding, 0, 0, mask, ShapeSet);
	XSetWindowBackgroundPixmap(dpy, bwin, pix);
	XClearWindow(dpy, bwin);
	if (!bmapped) {
		XMapRaised(dpy, bwin);
		bmapped = 1;
	} else
		XRaiseWindow(dpy, bwin);
	XSync(dpy, False);
	return (0);
}

void
bar_hide(void)
{
	if (bmapped) {
		XUnmapWindow(dpy, bwin);
		bmapped = 0;
		XSync(dpy, False);
	}
}

void
bar_cleanup(void)
{
	if (dpy == NULL)
		return;
	if (pgc != None) {
		XFreeGC(dpy, pgc);
		pgc = None;
	}
	if (mgc != None) {
		XFreeGC(dpy, mgc);
		mgc = None;
	}
	if (pix != 0) {
		XFreePixmap(dpy, pix);
		pix = 0;
	}
	if (mask != 0) {
		XFreePixmap(dpy, mask);
		mask = 0;
	}
	if (bwin != 0) {
		XDestroyWindow(dpy, bwin);
		bwin = 0;
	}
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
