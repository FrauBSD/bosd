/*
 * Shared OSD window chrome: ARGB/shaped create, NET_WM state, Shape
 * click-through, map/raise, and non-PNG alpha clamp.
 */
#include <stdio.h>
#include <string.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>

#include "priv.h"

Visual *
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

void
net_wm_osd_props(Window w)
{
	Atom net_wm_state, states[3];

	net_wm_state = XInternAtom(dpy, "_NET_WM_STATE", False);
	states[0] = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
	states[1] = XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False);
	states[2] = XInternAtom(dpy, "_NET_WM_STATE_SKIP_PAGER", False);
	XChangeProperty(dpy, w, net_wm_state, XA_ATOM, 32, PropModeReplace,
	    (unsigned char *)states, 3);
}

void
shape_clickthrough(Window w)
{
	int se, serr;

	if (!XShapeQueryExtension(dpy, &se, &serr))
		return;
	XShapeCombineRectangles(dpy, w, ShapeInput, 0, 0, NULL, 0,
	    ShapeSet, Unsorted);
}

void
shape_bounding_rect(Window w, int width, int height)
{
	XRectangle rect;
	int se, serr;

	if (!XShapeQueryExtension(dpy, &se, &serr))
		return;
	rect.x = 0;
	rect.y = 0;
	rect.width = (unsigned short)width;
	rect.height = (unsigned short)height;
	XShapeCombineRectangles(dpy, w, ShapeBounding, 0, 0, &rect, 1,
	    ShapeSet, Unsorted);
}

/*
 * ARGB override-redirect OSD window.  Caller owns *cmap_inout across
 * recreates (old colormap freed here when non-None).  event_mask 0
 * omits CWEventMask.
 */
Window
argb_osd_window(int x, int y, int w, int h, Visual **vis_out,
    Colormap *cmap_inout, int *depth_out, unsigned event_mask)
{
	XSetWindowAttributes wa;
	Visual *vis;
	Colormap cm;
	Window swin;
	unsigned long valuemask;
	int screen = DefaultScreen(dpy);
	int depth;

	vis = find_argb_visual(&depth);
	if (vis == NULL)
		return (0);
	if (cmap_inout != NULL && *cmap_inout != None)
		XFreeColormap(dpy, *cmap_inout);
	cm = XCreateColormap(dpy, RootWindow(dpy, screen), vis, AllocNone);
	wa.colormap = cm;
	wa.border_pixel = 0;
	wa.background_pixel = 0;
	wa.override_redirect = True;
	wa.event_mask = (long)event_mask;
	valuemask = CWColormap | CWBorderPixel | CWBackPixel |
	    CWOverrideRedirect;
	if (event_mask != 0)
		valuemask |= CWEventMask;
	swin = XCreateWindow(dpy, RootWindow(dpy, screen), x, y,
	    (unsigned)w, (unsigned)h, 0, depth, InputOutput, vis,
	    valuemask, &wa);
	if (swin == 0) {
		XFreeColormap(dpy, cm);
		if (cmap_inout != NULL)
			*cmap_inout = None;
		return (0);
	}
	if (vis_out != NULL)
		*vis_out = vis;
	if (cmap_inout != NULL)
		*cmap_inout = cm;
	if (depth_out != NULL)
		*depth_out = depth;
	net_wm_osd_props(swin);
	shape_clickthrough(swin);
	shape_bounding_rect(swin, w, h);
	return (swin);
}

/* Borderless click-through window whose shape the caller sets. */
Window
shaped_window(int x, int y, int w, int h)
{
	XSetWindowAttributes wa;
	Window swin;
	int screen = DefaultScreen(dpy);

	wa.override_redirect = True;
	wa.background_pixel = BlackPixel(dpy, screen);
	swin = XCreateWindow(dpy, RootWindow(dpy, screen), x, y,
	    (unsigned)w, (unsigned)h, 0, CopyFromParent, InputOutput,
	    CopyFromParent, CWOverrideRedirect | CWBackPixel, &wa);
	if (swin == 0)
		return (0);
	net_wm_osd_props(swin);
	shape_clickthrough(swin);
	return (swin);
}

void
raise_mapped(Window w, int *mapped_flag)
{
	if (!*mapped_flag) {
		XMapRaised(dpy, w);
		*mapped_flag = 1;
		XSync(dpy, False);
	} else
		XRaiseWindow(dpy, w);
}

/*
 * Non-PNG paints: unset fill (alpha < 0) means full; -o zeroes outline.
 * Do not use for icon_lookup (native PNG alpha must stay < 0).
 */
void
clamp_paint_alphas(const struct show_req *req, double *fill,
    double *outline)
{
	double f, o;

	f = req->alpha >= 0.0 ? req->alpha : 1.0;
	if (f < BOSD_ALPHA_MIN)
		f = BOSD_ALPHA_MIN;
	if (f > BOSD_ALPHA_MAX)
		f = BOSD_ALPHA_MAX;
	if (!req->outline)
		o = 0.0;
	else {
		o = req->outline_alpha;
		if (o < BOSD_ALPHA_MIN)
			o = BOSD_ALPHA_MIN;
		if (o > BOSD_ALPHA_MAX)
			o = BOSD_ALPHA_MAX;
	}
	*fill = f;
	*outline = o;
}

/* Premultiplied XRenderColor for PictOpSrc into ARGB32. */
void
xrender_color_premul(unsigned char r, unsigned char g, unsigned char b,
    unsigned char a, XRenderColor *out)
{
	out->red = (unsigned short)((r * 257 * (unsigned)a) / 255);
	out->green = (unsigned short)((g * 257 * (unsigned)a) / 255);
	out->blue = (unsigned short)((b * 257 * (unsigned)a) / 255);
	out->alpha = (unsigned short)(a * 257);
}
