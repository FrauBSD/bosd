/*
 * Gauge bar ARGB painters: ticks, overage label stamp, and blit to
 * the mapped window.  Geometry and font come from bar_geom.c.
 */
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/Xrender.h>

#include "priv.h"

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
bar_tick(Picture pic, const struct bar_geom *m, int i, int bx, int grow,
    int tall, unsigned char r, unsigned char g, unsigned char b,
    unsigned char a)
{
	int x = bx + i * m->pitch;
	int rx, ry, rwi, rhi;

	if (tall) {
		rx = x - grow;
		ry = 0 - grow + m->outl;
		rwi = m->pitch * 7 / 10 + 2 * grow;
		rhi = m->tick_h + 2 * grow;
	} else {
		rx = x - grow;
		ry = m->tick_h / 3 - grow + m->outl;
		rwi = m->pitch * 8 / 10 + 2 * grow;
		rhi = m->tick_h / 3 + 2 * grow;
	}
	fill_rect_pic(pic, rx, ry, rwi, rhi, r, g, b, a);
}

/*
 * Outline every tick (when out_a > 0), then color: tall always fill;
 * short ticks at or past the previous watermark use dim, the rest
 * (return zone) fill.  prev_on < 0 disables the watermark.
 */
void
bar_paint_ticks(Picture pic, int on, int prev_on, int bx, unsigned char fr,
    unsigned char fg, unsigned char fb, unsigned char fa, unsigned char dr,
    unsigned char dg, unsigned char db, unsigned char out_a)
{
	const struct bar_geom *m = bar_geom_get();
	int i;

	if (out_a > 0) {
		for (i = 0; i < BOSD_BAR_TICKS; i++)
			bar_tick(pic, m, i, bx, m->outl, i < on, 0, 0, 0,
			    out_a);
	}
	for (i = 0; i < on; i++)
		bar_tick(pic, m, i, bx, 0, 1, fr, fg, fb, fa);
	for (; i < BOSD_BAR_TICKS; i++) {
		if (prev_on >= 0 && i >= prev_on)
			bar_tick(pic, m, i, bx, 0, 0, dr, dg, db, fa);
		else
			bar_tick(pic, m, i, bx, 0, 0, fr, fg, fb, fa);
	}
}

/* Draw black label ink onto a white scratch pixmap. */
static int
stamp_label_ink(Pixmap tmp, Visual *vis, Colormap cm, const char *s,
    int x, int base, int grow_pass, int outl)
{
	XftDraw *td;
	XftColor ink;
	XftFont *font = bar_xfont();
	int len = (int)strlen(s), dx, dy;

	if (font == NULL)
		return (-1);
	td = XftDrawCreate(dpy, tmp, vis, cm);
	if (td == NULL ||
	    !XftColorAllocName(dpy, vis, cm, "black", &ink)) {
		if (td != NULL)
			XftDrawDestroy(td);
		return (-1);
	}
	if (grow_pass) {
		for (dx = -outl; dx <= outl; dx++)
			for (dy = -outl; dy <= outl; dy++) {
				if (dx == 0 && dy == 0)
					continue;
				XftDrawStringUtf8(td, &ink, font, x + dx,
				    base + dy, (const FcChar8 *)s, len);
			}
	} else
		XftDrawStringUtf8(td, &ink, font, x, base,
		    (const FcChar8 *)s, len);
	XftColorFree(dpy, vis, cm, &ink);
	XftDrawDestroy(td);
	return (0);
}

/* Copy non-background pixels from scratch into the ARGB picture. */
static void
stamp_label_blit(Picture pic, XImage *img, int rw, int rh, unsigned long bg,
    unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
	int xi, yi;

	for (yi = 0; yi < rh; yi++) {
		for (xi = 0; xi < rw; xi++) {
			if (XGetPixel(img, xi, yi) == bg)
				continue;
			fill_rect_pic(pic, xi, yi, 1, 1, r, g, b, a);
		}
	}
}

/*
 * Fixed-face label: ink from a scratch pixmap, stamped with
 * XRenderFillRectangle so alpha stays intact (XPutImage drops it).
 */
void
bar_stamp_label(Picture pic, int rw, int rh, const char *s, int x, int base,
    unsigned char r, unsigned char g, unsigned char b, unsigned char a,
    int grow_pass)
{
	const struct bar_geom *m = bar_geom_get();
	Pixmap tmp;
	GC tgc;
	XImage *img;
	Visual *vis;
	Colormap cm;
	int screen = DefaultScreen(dpy);
	int depth = DefaultDepth(dpy, screen);
	unsigned long bg = WhitePixel(dpy, screen);

	if (a == 0 || bar_xfont() == NULL)
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

	if (stamp_label_ink(tmp, vis, cm, s, x, base, grow_pass, m->outl)
	    != 0) {
		XFreePixmap(dpy, tmp);
		return;
	}

	img = XGetImage(dpy, tmp, 0, 0, (unsigned)rw, (unsigned)rh,
	    AllPlanes, ZPixmap);
	XFreePixmap(dpy, tmp);
	if (img == NULL)
		return;
	stamp_label_blit(pic, img, rw, rh, bg, r, g, b, a);
	XDestroyImage(img);
}

/*
 * Blit an ARGB32 picture to the mapped window and keep pix as the
 * Expose background.  *bpix_inout tracks the retained pixmap.
 */
int
bar_blit_window(Window win, Visual *vis, Pixmap *bpix_inout, Picture src_pic,
    Pixmap pix, int w, int h)
{
	XRenderPictFormat *fmt;
	Picture dst;
	XRenderColor clear;
	Pixmap bpix = *bpix_inout;

	fmt = XRenderFindVisualFormat(dpy, vis);
	if (fmt == NULL)
		return (-1);
	dst = XRenderCreatePicture(dpy, win, fmt, 0, NULL);
	clear.red = clear.green = clear.blue = clear.alpha = 0;
	XRenderFillRectangle(dpy, PictOpSrc, dst, &clear, 0, 0, w, h);
	XRenderComposite(dpy, PictOpSrc, src_pic, None, dst, 0, 0, 0, 0, 0, 0,
	    w, h);
	XRenderFreePicture(dpy, dst);

	if (bpix != 0 && bpix != pix) {
		XSetWindowBackgroundPixmap(dpy, win, None);
		XFreePixmap(dpy, bpix);
	}
	*bpix_inout = pix;
	XSetWindowBackgroundPixmap(dpy, win, pix);

	shape_bounding_rect(win, w, h);
	XFlush(dpy);
	return (0);
}
