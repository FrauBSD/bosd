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
int	 icon_px = 200;
int	 mapped;
int	 win_w, win_h;

static Colormap cmap;
static Visual *visual;
static GC gc;
static int scr_depth = 32;

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
init_window(int w, int h)
{
	XSetWindowAttributes wa;
	Atom net_wm_state, states[3];
	int screen, x, y;

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

	win_w = w;
	win_h = h;
	x = scr_x + (scr_w - win_w) / 2;
	y = scr_y + (scr_h - win_h) / 2;
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

static void
resize_window(int w, int h)
{
	win_w = w;
	win_h = h;
	XMoveResizeWindow(dpy, win,
	    scr_x + (scr_w - win_w) / 2, scr_y + (scr_h - win_h) / 2,
	    (unsigned)win_w, (unsigned)win_h);
	apply_shape();
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
	XRenderComposite(dpy, PictOpOver, src, None, dst, 0, 0, 0, 0, 0, 0,
	    iw, ih);
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
paint_icon(const struct icon *ic)
{
	if (win == 0) {
		if (init_window(ic->w, ic->h) != 0)
			return;
	} else if (ic->w != win_w || ic->h != win_h) {
		resize_window(ic->w, ic->h);
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
