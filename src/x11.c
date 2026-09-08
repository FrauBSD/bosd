/*
 * Display, panel geometry, and the ARGB32 overlay window.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>

#include "bosd.h"

Display	*dpy;
Window	 win;
int	 scr_x, scr_y, scr_w, scr_h;
int	 icon_px = 200, icon_pad;
int	 mapped;
int	 win_w, win_h;
int	 icon_ox, icon_oy;	/* artwork origin within the window */
Visual	*visual;
Colormap cmap;

static GC gc;
static int scr_depth = 32;
static int win_x, win_y;

static uint32_t
pack_argb32(unsigned char r, unsigned char g, unsigned char b,
    unsigned char a)
{
	return ((uint32_t)a << 24) | ((uint32_t)r << 16) |
	    ((uint32_t)g << 8) | (uint32_t)b;
}

/*
 * Center on the primary output; fall back to the first connected
 * internal panel (eDP/LVDS/DSI), then the whole X screen.
 */
static void
refresh_screen_geom(void)
{
	Window root;
	XRRScreenResources *res;
	RROutput primary;
	XRROutputInfo *oi;
	XRRCrtcInfo *ci;
	int screen, i;

	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	scr_x = 0;
	scr_y = 0;
	scr_w = DisplayWidth(dpy, screen);
	scr_h = DisplayHeight(dpy, screen);

	res = XRRGetScreenResourcesCurrent(dpy, root);
	if (res == NULL)
		return;

	primary = XRRGetOutputPrimary(dpy, root);
	if (primary == None) {
		for (i = 0; i < res->noutput; i++) {
			oi = XRRGetOutputInfo(dpy, res, res->outputs[i]);
			if (oi == NULL)
				continue;
			if (oi->connection == RR_Connected &&
			    oi->crtc != None &&
			    (strncmp(oi->name, "eDP", 3) == 0 ||
			     strncmp(oi->name, "LVDS", 4) == 0 ||
			     strncmp(oi->name, "DSI", 3) == 0)) {
				primary = res->outputs[i];
				XRRFreeOutputInfo(oi);
				break;
			}
			XRRFreeOutputInfo(oi);
		}
	}
	if (primary != None) {
		oi = XRRGetOutputInfo(dpy, res, primary);
		if (oi != NULL && oi->crtc != None) {
			ci = XRRGetCrtcInfo(dpy, res, oi->crtc);
			if (ci != NULL) {
				scr_x = (int)ci->x;
				scr_y = (int)ci->y;
				scr_w = (int)ci->width;
				scr_h = (int)ci->height;
				XRRFreeCrtcInfo(ci);
			}
		}
		if (oi != NULL)
			XRRFreeOutputInfo(oi);
	}
	XRRFreeScreenResources(res);

	icon_px = scr_h / 4;
	if (icon_px > 320)
		icon_px = 320;
	if (icon_px < 160)
		icon_px = 160;
	icon_pad = icon_px / 22;
	if (icon_pad < 12)
		icon_pad = 12;
	if (icon_pad > 24)
		icon_pad = 24;
}

static Visual *
find_argb_visual(int *depth_out)
{
	XVisualInfo template, *vi;
	int n;

	template.screen = DefaultScreen(dpy);
	template.depth = 32;
	template.class = TrueColor;
	vi = XGetVisualInfo(dpy, VisualScreenMask | VisualDepthMask |
	    VisualClassMask, &template, &n);
	if (vi == NULL)
		return (NULL);
	*depth_out = vi->depth;
	return (vi[0].visual);
}

/* Click-through: empty input shape, bounding shape = window rect. */
static void
apply_shape(void)
{
	XRectangle rect;
	int se, serr;

	if (!XShapeQueryExtension(dpy, &se, &serr))
		return;
	XShapeCombineRectangles(dpy, win, ShapeInput, 0, 0, NULL, 0,
	    ShapeSet, Unsorted);
	rect.x = 0;
	rect.y = 0;
	rect.width = (unsigned short)win_w;
	rect.height = (unsigned short)win_h;
	XShapeCombineRectangles(dpy, win, ShapeBounding, 0, 0, &rect, 1,
	    ShapeSet, Unsorted);
}

static int
create_window(int x, int y)
{
	XSetWindowAttributes wa;
	Atom net_wm_state, states[3];
	int screen;

	visual = find_argb_visual(&scr_depth);
	if (visual == NULL)
		return (-1);
	screen = DefaultScreen(dpy);
	cmap = XCreateColormap(dpy, RootWindow(dpy, screen), visual,
	    AllocNone);
	wa.colormap = cmap;
	wa.border_pixel = 0;
	wa.background_pixel = 0;
	wa.override_redirect = True;
	wa.event_mask = ExposureMask;

	win = XCreateWindow(dpy, RootWindow(dpy, screen), x, y, win_w, win_h,
	    0, scr_depth, InputOutput, visual,
	    CWColormap | CWBorderPixel | CWBackPixel | CWOverrideRedirect |
	    CWEventMask, &wa);
	if (win == 0)
		return (-1);

	net_wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
	states[0] = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
	states[1] = XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False);
	states[2] = XInternAtom(dpy, "_NET_WM_STATE_SKIP_PAGER", False);
	XChangeProperty(dpy, win, net_wm_state, XA_ATOM, 32, PropModeReplace,
	    (unsigned char *)states, 3);

	apply_shape();
	gc = XCreateGC(dpy, win, 0, NULL);
	mapped = 0;
	return (0);
}

/*
 * Size the window as artwork plus padding — extra top/right room when a
 * badge rides along (more for a wide label) — and place it so the
 * artwork, not the window, is centered on the panel, shifted by the
 * requested vertical offset (positive down).  Returns 1 when the show
 * lies entirely past a panel edge (nothing to paint).
 */
static int
layout_window(const struct icon *ic, const struct show_req *req)
{
	int pad_left, pad_right, pad_top, pad_bot;
	int blen, w, h, x, y;

	blen = (int)strlen(req->badge);
	pad_left = icon_px / 12;
	pad_bot = icon_px / 12;
	if (blen > 1) {
		pad_right = (ic->h * 42) / 100;
		if (pad_right < 88)
			pad_right = 88;
		pad_top = (ic->h * 18) / 100;
		if (pad_top < 48)
			pad_top = 48;
	} else if (blen == 1) {
		pad_right = (ic->h * 20) / 100;
		if (pad_right < 48)
			pad_right = 48;
		pad_top = (ic->h * 15) / 100;
		if (pad_top < 41)
			pad_top = 41;
	} else {
		pad_right = icon_px / 12;
		pad_top = icon_px / 12;
	}

	icon_ox = pad_left;
	icon_oy = pad_top;
	w = ic->w + pad_left + pad_right;
	h = ic->h + pad_top + pad_bot;
	x = scr_x + scr_w / 2 - (icon_ox + ic->w / 2) + req->x_off;
	y = scr_y + scr_h / 2 - (icon_oy + ic->h / 2) + req->y_off;
	/* Offset shows are deliberate: only clamp plain centering. */
	if (req->y_off == 0) {
		if (h < scr_h && y + h > scr_y + scr_h)
			y = scr_y + scr_h - h;
		if (y < scr_y)
			y = scr_y;
	}

	/*
	 * Confine the window to the panel and clip the artwork by
	 * shifting its paint origin, so an offset show emerges from
	 * the panel edge instead of straying onto a neighbor output.
	 */
	if (x < scr_x) {
		icon_ox -= scr_x - x;
		w -= scr_x - x;
		x = scr_x;
	}
	if (x + w > scr_x + scr_w)
		w = scr_x + scr_w - x;
	if (y < scr_y) {
		icon_oy -= scr_y - y;
		h -= scr_y - y;
		y = scr_y;
	}
	if (y + h > scr_y + scr_h)
		h = scr_y + scr_h - y;
	if (w <= 0 || h <= 0)
		return (1);	/* fully past an edge: nothing visible */

	if (win == 0) {
		win_w = w;
		win_h = h;
		win_x = x;
		win_y = y;
		return (create_window(x, y));
	}
	if (w != win_w || h != win_h) {
		win_w = w;
		win_h = h;
		win_x = x;
		win_y = y;
		XMoveResizeWindow(dpy, win, x, y, (unsigned)win_w,
		    (unsigned)win_h);
		apply_shape();
	} else if (x != win_x || y != win_y) {
		win_x = x;
		win_y = y;
		XMoveWindow(dpy, win, x, y);
	}
	return (0);
}

static void
paint_rgba(const unsigned char *rgba, int iw, int ih)
{
	XRenderPictFormat *fmt;
	Pixmap pix;
	Picture src, dst;
	XImage *img;
	uint32_t *pixels;
	XRenderColor clear;
	int x, y;

	fmt = XRenderFindStandardFormat(dpy, PictStandardARGB32);
	if (fmt == NULL)
		return;
	pixels = malloc((size_t)iw * (size_t)ih * sizeof(uint32_t));
	if (pixels == NULL)
		return;
	for (y = 0; y < ih; y++) {
		for (x = 0; x < iw; x++) {
			const unsigned char *p =
			    rgba + ((size_t)y * iw + x) * 4;
			pixels[(size_t)y * iw + x] =
			    pack_argb32(p[0], p[1], p[2], p[3]);
		}
	}
	img = XCreateImage(dpy, visual, 32, ZPixmap, 0, (char *)pixels, iw,
	    ih, 32, 0);
	if (img == NULL) {
		free(pixels);
		return;
	}
	if (gc == None)
		gc = XCreateGC(dpy, win, 0, NULL);
	pix = XCreatePixmap(dpy, win, iw, ih, 32);
	XPutImage(dpy, pix, gc, img, 0, 0, 0, 0, iw, ih);
	img->data = NULL;
	XDestroyImage(img);
	free(pixels);

	src = XRenderCreatePicture(dpy, pix, fmt, 0, NULL);
	fmt = XRenderFindVisualFormat(dpy, visual);
	dst = XRenderCreatePicture(dpy, win, fmt, 0, NULL);
	clear.red = clear.green = clear.blue = clear.alpha = 0;
	XRenderFillRectangle(dpy, PictOpSrc, dst, &clear, 0, 0, win_w,
	    win_h);
	XRenderComposite(dpy, PictOpOver, src, None, dst, 0, 0, 0, 0,
	    icon_ox, icon_oy, iw, ih);
	XRenderFreePicture(dpy, src);
	XRenderFreePicture(dpy, dst);
	XFreePixmap(dpy, pix);
	XFlush(dpy);
}

/*
 * Keep the window mapped and overwrite pixels; unmap/remap (or
 * kill/respawn) flashes between glyphs.
 */
void
paint_icon(const struct icon *ic, const struct show_req *req)
{
	int vis;

	vis = layout_window(ic, req);
	if (vis < 0)
		return;
	if (vis > 0) {
		hide_overlay();
		return;
	}

	if (!mapped) {
		XMapRaised(dpy, win);
		mapped = 1;
		XSync(dpy, False);
	} else {
		XRaiseWindow(dpy, win);
	}
	XClearWindow(dpy, win);
	paint_rgba(ic->rgba, ic->w, ic->h);
	draw_badge(ic, req->badge);
	XSync(dpy, False);
}

void
hide_overlay(void)
{
	if (mapped) {
		XUnmapWindow(dpy, win);
		mapped = 0;
		XSync(dpy, False);
	}
}

static int
x_error_ignore(Display *d __unused, XErrorEvent *e __unused)
{
	return (0);
}

int
init_display(void)
{
	dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		fprintf(stderr, "bosd: cannot open DISPLAY\n");
		return (-1);
	}
	XSetErrorHandler(x_error_ignore);
	refresh_screen_geom();
	return (0);
}

void
x11_cleanup(void)
{
	badge_cleanup();
	if (dpy != NULL && gc != None) {
		XFreeGC(dpy, gc);
		gc = None;
	}
	if (dpy != NULL && win != 0) {
		XDestroyWindow(dpy, win);
		win = 0;
	}
	if (dpy != NULL && cmap != 0) {
		XFreeColormap(dpy, cmap);
		cmap = 0;
	}
	if (dpy != NULL) {
		XCloseDisplay(dpy);
		dpy = NULL;
	}
}
