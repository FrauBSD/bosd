/*
 * Small text: caption-sized lines (a screenshot's filename and the
 * like) in 24px fixed type, black-outlined and filled in a caller-
 * given color (default green), centered near the panel bottom with
 * the first line's top fixed 205 pixels up; lines grow downward.
 * Drawn like the gauge bar: a shaped click-through window whose
 * drawn pixels form the bounding mask.  Lines split on newline
 * (escape \x0a) and center individually.
 */
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>

#include "priv.h"

#define STEXT_FONT "-misc-fixed-medium-r-normal--24-*-*-*-*-*-*"
#define STEXT_TOP  205	/* text top, pixels up from the panel bottom */
#define STEXT_OUTL 2	/* black outline thickness */
#define STEXT_MAXLINES 8

static Window	 twin;
static Pixmap	 pix, mask;
static GC	 pgc, mgc;
static XFontSet	 fset;
static int	 ascent, lineh;
static int	 tmapped;

static int
stext_metrics(void)
{
	XFontSetExtents *ex;
	char **missing;
	int nmissing;
	char *def;

	if (fset != NULL)
		return (0);
	fset = XCreateFontSet(dpy, STEXT_FONT, &missing, &nmissing, &def);
	if (missing != NULL)
		XFreeStringList(missing);
	if (fset == NULL)
		return (-1);
	ex = XExtentsOfFontSet(fset);
	ascent = -ex->max_logical_extent.y;
	if (ascent < 2)
		ascent = 20;
	lineh = ex->max_logical_extent.height + 2 * STEXT_OUTL;
	return (0);
}

/* Square outline passes, then one fill pass, into pixmap and mask. */
static void
line_pass(const char *s, int x, int base, int grow_pass)
{
	int len = (int)strlen(s), dx, dy;

	if (grow_pass) {
		for (dx = -STEXT_OUTL; dx <= STEXT_OUTL; dx++)
			for (dy = -STEXT_OUTL; dy <= STEXT_OUTL; dy++) {
				if (dx == 0 && dy == 0)
					continue;
				XmbDrawString(dpy, pix, fset, pgc, x + dx,
				    base + dy, s, len);
				XmbDrawString(dpy, mask, fset, mgc, x + dx,
				    base + dy, s, len);
			}
	} else {
		XmbDrawString(dpy, pix, fset, pgc, x, base, s, len);
		XmbDrawString(dpy, mask, fset, mgc, x, base, s, len);
	}
}

int
stext_show(const struct show_req *req)
{
	XColor col, exact;
	char text[BOSD_SPEC_MAX];
	char *lines[STEXT_MAXLINES];
	char *p;
	int screen = DefaultScreen(dpy), depth;
	int nl = 0, i, lw, lx, base;
	int x, y, w, h;
	unsigned long fill_px, black;

	if (stext_metrics() != 0)
		return (-1);
	decode_escapes(req->spec, text, sizeof(text));
	for (p = text; nl < STEXT_MAXLINES && *p != '\0';) {
		lines[nl++] = p;
		p = strchr(p, '\n');
		if (p == NULL)
			break;
		*p++ = '\0';
	}
	if (nl == 0)
		return (-1);

	w = scr_w;
	h = nl * lineh;
	x = scr_x + req->x_off;
	y = scr_y + scr_h - STEXT_TOP + req->y_off;
	if (twin == 0 && (twin = shaped_window(x, y, w, h)) == 0)
		return (-1);
	XMoveResizeWindow(dpy, twin, x, y, (unsigned)w, (unsigned)h);

	depth = DefaultDepth(dpy, screen);
	if (pix != 0)
		XFreePixmap(dpy, pix);
	if (mask != 0)
		XFreePixmap(dpy, mask);
	pix = XCreatePixmap(dpy, twin, (unsigned)w, (unsigned)h, depth);
	mask = XCreatePixmap(dpy, twin, (unsigned)w, (unsigned)h, 1);
	if (pgc == None)
		pgc = XCreateGC(dpy, pix, 0, NULL);
	if (mgc == None)
		mgc = XCreateGC(dpy, mask, 0, NULL);

	XSetForeground(dpy, mgc, 0);
	XFillRectangle(dpy, mask, mgc, 0, 0, (unsigned)w, (unsigned)h);
	XSetForeground(dpy, mgc, 1);

	black = BlackPixel(dpy, screen);
	if (XAllocNamedColor(dpy, DefaultColormap(dpy, screen),
	    req->tcolor[0] != '\0' ? req->tcolor : BOSD_STEXT_DEF,
	    &col, &exact))
		fill_px = col.pixel;
	else
		fill_px = WhitePixel(dpy, screen);

	for (i = 0; i < nl; i++) {
		lw = XmbTextEscapement(fset, lines[i],
		    (int)strlen(lines[i]));
		lx = (w - lw) / 2;
		base = i * lineh + ascent + STEXT_OUTL;
		XSetForeground(dpy, pgc, black);
		line_pass(lines[i], lx, base, 1);
		XSetForeground(dpy, pgc, fill_px);
		line_pass(lines[i], lx, base, 0);
	}

	XShapeCombineMask(dpy, twin, ShapeBounding, 0, 0, mask, ShapeSet);
	XSetWindowBackgroundPixmap(dpy, twin, pix);
	XClearWindow(dpy, twin);
	if (!tmapped) {
		XMapRaised(dpy, twin);
		tmapped = 1;
	} else
		XRaiseWindow(dpy, twin);
	XSync(dpy, False);
	return (0);
}

void
stext_hide(void)
{
	if (tmapped) {
		XUnmapWindow(dpy, twin);
		tmapped = 0;
		XSync(dpy, False);
	}
}

void
stext_cleanup(void)
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
	if (twin != 0) {
		XDestroyWindow(dpy, twin);
		twin = 0;
	}
	if (fset != NULL) {
		XFreeFontSet(dpy, fset);
		fset = NULL;
	}
	tmapped = 0;
}

/* One-shot fallback: no daemon on the channel, draw it ourselves. */
int
run_stext(const struct show_req *req)
{
	double mdl, bdl = 0.0, hold;

	hold = req->hold;
	if (hold != -1.0 && hold <= 0.0)
		hold = BOSD_HOLD_DEF;
	if (init_display() != 0)
		return (1);
	signal(SIGTERM, cleanup);
	signal(SIGINT, cleanup);
	if (stext_show(req) != 0)
		return (1);
	if (req->gauge >= 0) {
		bar_show(req);
		bdl = req->gauge_hold < 0.0 ? -1.0 :
		    now_monotonic() + req->gauge_hold;
	}

	/* Text and gauge expire on their own holds; 0 marks one down. */
	mdl = hold < 0.0 ? -1.0 : now_monotonic() + hold;
	while (mdl != 0.0 || bdl != 0.0) {
		double now = now_monotonic();

		if (mdl != 0.0 && mdl >= 0.0 && now >= mdl) {
			stext_hide();
			mdl = 0.0;
		}
		if (bdl != 0.0 && bdl >= 0.0 && now >= bdl) {
			bar_hide();
			bdl = 0.0;
		}
		if (mdl == 0.0 && bdl == 0.0)
			break;
		poll(NULL, 0, 50);
	}
	cleanup(0);
	return (0);
}
