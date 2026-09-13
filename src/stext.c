/*
 * Small text: caption-sized lines (a screenshot's filename and the
 * like) in panel-scaled type (times -s; 36px on a 1200-tall primary),
 * black-outlined and filled in a caller-given color (default green),
 * centered near the panel bottom with the block sitting above the
 * gauge bar's band so a concurrent -g never overlaps; lines grow
 * downward.  Default face is Xft BOSD_FIXED_FACE; -f selects another
 * family.  -A/-O need real alpha so those paints use an ARGB window;
 * -o simply skips the outline.  Face and size do not depend on -A/-O.
 */
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>

#include "priv.h"

#define STEXT_REF_GAP 16	/* air above gauge band at BOSD_PANEL_REF_H */
#define STEXT_MAXLINES 8

static int
stext_gap(void)
{
	int h = scr_h > 0 ? scr_h : BOSD_PANEL_REF_H;
	int g = STEXT_REF_GAP * h / BOSD_PANEL_REF_H;

	if (g < 4)
		g = 4;
	return (g);
}

static Window	 twin;
static Pixmap	 pix, mask;
static Colormap	 tcmap;
static Visual	*tvisual;
static GC	 pgc, mgc;
static XftDraw	*xdraw;
static int	 tmapped;
static int	 t_argb;	/* twin is an ARGB window */
static int	 tdepth;

static void
stext_kill_xdraw(void)
{
	if (xdraw != NULL) {
		XftDrawDestroy(xdraw);
		xdraw = NULL;
	}
}

static void
stext_drop_window(void)
{
	if (xdraw != NULL) {
		XftDrawDestroy(xdraw);
		xdraw = NULL;
	}
	if (pix != 0) {
		if (twin != 0)
			XSetWindowBackgroundPixmap(dpy, twin, None);
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
	if (tcmap != None) {
		XFreeColormap(dpy, tcmap);
		tcmap = None;
	}
	tvisual = NULL;
	tmapped = 0;
	t_argb = 0;
}

/* ARGB click-through window for translucent -t. */
static Window
stext_argb_window(int x, int y, int w, int h)
{
	Window swin;

	swin = argb_osd_window(x, y, w, h, &tvisual, &tcmap, &tdepth, 0);
	if (swin != 0)
		XStoreName(dpy, swin, "bosd");
	return (swin);
}

static int
stext_show_argb(const struct show_req *req, char **lines, int nl,
    const char *fill_name, double fill_a, double out_a, int stroke)
{
	XRenderPictFormat *fmt;
	XRenderColor clear;
	Picture pic;
	int i, lw, lx, base;
	int x, y, w, h;

	w = scr_w;
	h = nl * stext_lineh();
	x = scr_x + req->x_off;
	y = scr_y + scr_h - (bar_band_height() + stext_gap() + h) +
	    req->y_off;

	if (twin != 0 && !t_argb)
		stext_drop_window();
	if (twin == 0 && (twin = stext_argb_window(x, y, w, h)) == 0)
		return (-1);
	t_argb = 1;
	XMoveResizeWindow(dpy, twin, x, y, (unsigned)w, (unsigned)h);

	fmt = XRenderFindStandardFormat(dpy, PictStandardARGB32);
	if (fmt == NULL)
		return (-1);
	if (pix != 0) {
		XSetWindowBackgroundPixmap(dpy, twin, None);
		XFreePixmap(dpy, pix);
	}
	pix = XCreatePixmap(dpy, twin, (unsigned)w, (unsigned)h, 32);
	pic = XRenderCreatePicture(dpy, pix, fmt, 0, NULL);
	clear.red = clear.green = clear.blue = clear.alpha = 0;
	XRenderFillRectangle(dpy, PictOpSrc, pic, &clear, 0, 0, w, h);
	XRenderFreePicture(dpy, pic);

	draw_set_target(pix, tvisual, tcmap);
	for (i = 0; i < nl; i++) {
		XGlyphInfo e;

		XftTextExtentsUtf8(dpy, stext_xfont(), (const FcChar8 *)lines[i],
		    (int)strlen(lines[i]), &e);
		lw = (int)e.width;
		lx = (w - lw) / 2;
		base = i * stext_lineh() + stext_ascent() + stext_outl();
		draw_outlined_utf8(NULL, stext_xfont(), lx, base, lines[i], stroke,
		    fill_name, fill_a, out_a);
	}
	draw_set_target(0, NULL, None);

	XSetWindowBackgroundPixmap(dpy, twin, pix);
	shape_bounding_rect(twin, w, h);

	raise_mapped(twin, &tmapped);
	XClearWindow(dpy, twin);
	XSync(dpy, False);
	return (0);
}

int
stext_show(const struct show_req *req)
{
	XftColor xink, xblack;
	char text[BOSD_SPEC_MAX];
	char *lines[STEXT_MAXLINES];
	char *p;
	const char *fill_name;
	int screen = DefaultScreen(dpy), depth;
	int nl = 0, i, lw, lx, base;
	int x, y, w, h;
	int need_argb, stroke;
	double fill_a, out_a;
	Visual *vis;
	Colormap cm;

	clamp_paint_alphas(req, &fill_a, &out_a);
	/*
	 * Real translucency needs ARGB.  -o alone stays on the shaped
	 * path (just skip the outline).  -A / -O take the ARGB path.
	 */
	need_argb = (req->alpha >= 0.0 ||
	    req->outline_alpha != BOSD_OUTLINE_ALPHA_DEF);

	stext_kill_xdraw();
	if (stext_metrics(req->font, req->scale) != 0)
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

	fill_name = req->tcolor[0] != '\0' ? req->tcolor : BOSD_STEXT_DEF;
	stroke = req->outline ? stext_outl() : 0;

	if (need_argb)
		return (stext_show_argb(req, lines, nl, fill_name, fill_a,
		    out_a, stroke));

	w = scr_w;
	h = nl * stext_lineh();
	x = scr_x + req->x_off;
	/*
	 * Park the whole caption block above the gauge band so a bar
	 * that appears later cannot cover the text.  First-line baseline
	 * is then ascent+outl below the window top, still clear of the bar.
	 */
	y = scr_y + scr_h - (bar_band_height() + stext_gap() + h) +
	    req->y_off;
	if (twin != 0 && t_argb)
		stext_drop_window();
	if (twin == 0 && (twin = shaped_window(x, y, w, h)) == 0)
		return (-1);
	t_argb = 0;
	XMoveResizeWindow(dpy, twin, x, y, (unsigned)w, (unsigned)h);

	depth = DefaultDepth(dpy, screen);
	vis = DefaultVisual(dpy, screen);
	cm = DefaultColormap(dpy, screen);
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
	XSetForeground(dpy, pgc, BlackPixel(dpy, screen));
	XFillRectangle(dpy, pix, pgc, 0, 0, (unsigned)w, (unsigned)h);

	if (xdraw != NULL)
		XftDrawDestroy(xdraw);
	xdraw = XftDrawCreate(dpy, pix, vis, cm);
	if (xdraw == NULL)
		return (-1);
	if (!XftColorAllocName(dpy, vis, cm, "black", &xblack) ||
	    !XftColorAllocName(dpy, vis, cm, fill_name, &xink)) {
		if (xdraw != NULL) {
			XftDrawDestroy(xdraw);
			xdraw = NULL;
		}
		return (-1);
	}
	for (i = 0; i < nl; i++) {
		XGlyphInfo e;

		XftTextExtentsUtf8(dpy, stext_xfont(),
		    (const FcChar8 *)lines[i],
		    (int)strlen(lines[i]), &e);
		lw = (int)e.width;
		lx = (w - lw) / 2;
		base = i * stext_lineh() + stext_ascent() + stext_outl();
		if (stroke > 0) {
			stext_mask_line_xft(twin, mask, mgc,
			    lines[i], lx, base, vis, cm, -1);
			stext_line_xft(xdraw, lines[i], lx, base,
			    &xblack, 1);
		} else {
			/* Fill-only mask: reuse white stamp path. */
			stext_mask_line_xft(twin, mask, mgc,
			    lines[i], lx, base, vis, cm, 0);
		}
		stext_line_xft(xdraw, lines[i], lx, base, &xink, 0);
	}
	XftColorFree(dpy, vis, cm, &xink);
	XftColorFree(dpy, vis, cm, &xblack);

	XShapeCombineMask(dpy, twin, ShapeBounding, 0, 0, mask, ShapeSet);
	XSetWindowBackgroundPixmap(dpy, twin, pix);
	XClearWindow(dpy, twin);
	raise_mapped(twin, &tmapped);
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
	stext_kill_xdraw();
	stext_close_fonts();
	if (pgc != None) {
		XFreeGC(dpy, pgc);
		pgc = None;
	}
	if (mgc != None) {
		XFreeGC(dpy, mgc);
		mgc = None;
	}
	stext_drop_window();
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
