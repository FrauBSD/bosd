/*
 * Display, panel geometry, and the ARGB32 overlay window.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>

#include "priv.h"

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

/* Per-output overlay.  Globals above are the slot overlay_select loaded. */
struct ovl {
	Window		 win;
	Visual		*visual;
	Colormap	 cmap;
	GC		 gc;
	int		 depth;
	int		 mapped;
	int		 win_x, win_y, win_w, win_h;
	int		 icon_ox, icon_oy;
};

static struct ovl ovl[BOSD_SCREEN_SLOTS];
static int ovl_i = -1;

static void
ovl_store(void)
{
	struct ovl *o;

	if (ovl_i < 0)
		return;
	o = &ovl[ovl_i];
	o->win = win;
	o->visual = visual;
	o->cmap = cmap;
	o->gc = gc;
	o->depth = scr_depth;
	o->mapped = mapped;
	o->win_x = win_x;
	o->win_y = win_y;
	o->win_w = win_w;
	o->win_h = win_h;
	o->icon_ox = icon_ox;
	o->icon_oy = icon_oy;
}

static void
ovl_load(int slot)
{
	struct ovl *o = &ovl[slot];

	win = o->win;
	visual = o->visual;
	cmap = o->cmap;
	gc = o->gc;
	scr_depth = o->depth != 0 ? o->depth : 32;
	mapped = o->mapped;
	win_x = o->win_x;
	win_y = o->win_y;
	win_w = o->win_w;
	win_h = o->win_h;
	icon_ox = o->icon_ox;
	icon_oy = o->icon_oy;
}

void
overlay_select(int slot)
{
	if (slot < 0 || slot >= BOSD_SCREEN_SLOTS || slot == ovl_i)
		return;
	ovl_store();
	ovl_i = slot;
	ovl_load(slot);
}

static void
ovl_drop(void)
{
	if (dpy == NULL)
		return;
	if (gc != None) {
		XFreeGC(dpy, gc);
		gc = None;
	}
	if (win != 0) {
		XDestroyWindow(dpy, win);
		win = 0;
	}
	if (cmap != 0) {
		XFreeColormap(dpy, cmap);
		cmap = 0;
	}
	visual = NULL;
	mapped = 0;
	win_w = 0;
	win_h = 0;
	win_x = 0;
	win_y = 0;
}

/* Drop overlays the current show does not use, so a move off "all" clears. */
void
overlay_hide_untargeted(void)
{
	int i;

	if (dpy == NULL)
		return;
	for (i = 0; i < BOSD_SCREEN_SLOTS; i++) {
		if (screen_targeted(i))
			continue;
		overlay_select(i);
		if (win != 0)
			ovl_drop();
	}
	ovl_store();
}

/* XRender ARGB32 is premultiplied; scale each color by its alpha. */
static uint32_t
pack_argb32(unsigned char r, unsigned char g, unsigned char b,
    unsigned char a)
{
	return ((uint32_t)a << 24) |
	    ((uint32_t)(r * a / 255) << 16) |
	    ((uint32_t)(g * a / 255) << 8) |
	    (uint32_t)(b * a / 255);
}

/*
 * Default output (primary, else internal panel, else the whole X
 * screen).  A show that names -S calls screen_prepare itself.
 */
void
refresh_screen_geom(void)
{
	if (screen_prepare(NULL) != 0)
		return;
	if (screen_count() > 0)
		screen_use(0);
}


/* Click-through: empty input shape, bounding shape = window rect. */
static void
apply_shape(void)
{
	shape_clickthrough(win);
	shape_bounding_rect(win, win_w, win_h);
}

static int
create_window(int x, int y)
{
	win = argb_osd_window(x, y, win_w, win_h, &visual, &cmap,
	    &scr_depth, ExposureMask);
	if (win == 0)
		return (-1);
	gc = XCreateGC(dpy, win, 0, NULL);
	mapped = 0;
	return (0);
}

/* Create the overlay, or move/resize it to the requested rectangle. */
static int
place_window(int x, int y, int w, int h)
{
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

/*
 * Size the window as artwork plus padding -- extra top/right room when
 * a badge is present (more for a wide label) -- and place it so the
 * artwork, not the window, is centered on the panel, shifted by the
 * requested vertical offset (positive down).  Returns 1 when the show
 * lies entirely past a panel edge (nothing to paint).
 */
static int
layout_window(const struct icon *ic, const struct show_req *req)
{
	int pad_left, pad_right, pad_top, pad_bot;
	int blen, w, h, x, y, cap_w, cw, ch;

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

	/* Captions reserve a line above/below and may widen the window. */
	cap_w = 0;
	if (req->prefix[0] != '\0') {
		caption_measure(req->prefix, caption_px(ic), &cw, &ch,
		    req->font);
		pad_top += ch + caption_gap();
		if (cw > cap_w)
			cap_w = cw;
	}
	if (req->append[0] != '\0') {
		caption_measure(req->append, caption_px(ic), &cw, &ch,
		    req->font);
		pad_bot += ch + caption_gap();
		if (cw > cap_w)
			cap_w = cw;
	}

	icon_ox = pad_left;
	icon_oy = pad_top;
	w = ic->w + pad_left + pad_right;
	h = ic->h + pad_top + pad_bot;
	if (cap_w + 16 > w) {
		icon_ox += (cap_w + 16 - w) / 2;
		w = cap_w + 16;
	}
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

	return (place_window(x, y, w, h));
}

/* Full-panel window for text shows (countdown). */
int
layout_fullscreen(void)
{
	icon_ox = 0;
	icon_oy = 0;
	return (place_window(scr_x, scr_y, scr_w, scr_h));
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
raise_overlay(void)
{
	raise_mapped(win, &mapped);
}

/* Unmap this slot only.  A clipped panel must not hide the others. */
static void
hide_current(void)
{
	if (win != 0 && mapped) {
		XUnmapWindow(dpy, win);
		mapped = 0;
	}
}

/* 0 painted or clipped, -1 if the window could not be created. */
static int
paint_icon_one(const struct icon *ic, const struct show_req *req)
{
	int vis;

	vis = layout_window(ic, req);
	if (vis < 0)
		return (-1);
	if (vis > 0) {
		hide_current();
		return (0);
	}

	raise_overlay();
	XClearWindow(dpy, win);
	paint_rgba(ic->rgba, ic->w, ic->h);
	draw_badge(ic, req->badge, req->tcolor,
	    req->alpha >= 0.0 ? req->alpha : 1.0, req->outline_alpha,
	    req->font);
	if (req->prefix[0] != '\0')
		draw_caption(req->prefix, caption_px(ic), icon_oy, 0,
		    req->font);
	if (req->append[0] != '\0')
		draw_caption(req->append, caption_px(ic),
		    icon_oy + ic->h, 1, req->font);
	XSync(dpy, False);
	return (0);
}

/*
 * Paint req on each selected output.  Returns 0 when at least one
 * panel was drawn, -1 when the icon could not be loaded, -2 when
 * the named output is not connected.
 */
int
paint_show(const struct show_req *req)
{
	int i, n, ok = 0;

	if (screen_prepare(req->screen) != 0)
		return (-2);
	n = screen_count();
	for (i = 0; i < n; i++) {
		struct icon *ic;

		screen_use(i);
		ic = icon_lookup(req->spec, req->scale, req->alpha,
		    req->outline_alpha, req->outline);
		if (ic == NULL)
			continue;
		if (paint_icon_one(ic, req) == 0)
			ok = 1;
	}
	if (!ok)
		return (-1);
	overlay_hide_untargeted();
	return (0);
}

void
hide_overlay(void)
{
	int i, any = 0;

	if (dpy == NULL)
		return;
	for (i = 0; i < BOSD_SCREEN_SLOTS; i++) {
		overlay_select(i);
		if (win != 0 && mapped) {
			XUnmapWindow(dpy, win);
			mapped = 0;
			any = 1;
		}
	}
	ovl_store();
	if (any)
		XSync(dpy, False);
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
	bar_cleanup();
	stext_cleanup();
	if (dpy != NULL) {
		int i;

		for (i = 0; i < BOSD_SCREEN_SLOTS; i++) {
			overlay_select(i);
			ovl_drop();
		}
		ovl_store();
		XCloseDisplay(dpy);
		dpy = NULL;
	}
}
