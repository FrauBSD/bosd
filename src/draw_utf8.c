/*
 * Shared outlined UTF-8 painter: opaque Xft fast path, or punched ARGB
 * layers for -A/-O.  Optional paint target (draw_set_target) so captions
 * can stamp into an off-screen pixmap instead of the main overlay.
 */
#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/Xrender.h>

#include "priv.h"

/* Optional paint target for draw_outlined_utf8 (0 = main overlay). */
static Drawable draw_dst;
static Visual *draw_vis;
static Colormap draw_cm;

void
draw_set_target(Drawable d, Visual *v, Colormap c)
{
	draw_dst = d;
	draw_vis = v;
	draw_cm = c;
}

static Drawable
paint_drawable(void)
{
	if (draw_dst != 0)
		return (draw_dst);
	return (win);
}

static Visual *
paint_visual(void)
{
	if (draw_dst != 0 && draw_vis != NULL)
		return (draw_vis);
	return (visual);
}

static Colormap
paint_cmap(void)
{
	if (draw_dst != 0 && draw_cm != None)
		return (draw_cm);
	return (cmap);
}

static void
blit_layer(Pixmap pix, int iw, int ih, int dx, int dy, double alpha)
{
	XRenderPictFormat *fmt;
	Picture src, dst, mask;
	XRenderColor mc;
	Drawable dest = paint_drawable();
	Visual *vis = paint_visual();

	if (alpha <= 0.0 || dest == 0)
		return;
	fmt = XRenderFindStandardFormat(dpy, PictStandardARGB32);
	if (fmt == NULL)
		return;
	src = XRenderCreatePicture(dpy, pix, fmt, 0, NULL);
	fmt = XRenderFindVisualFormat(dpy, vis);
	if (fmt == NULL) {
		XRenderFreePicture(dpy, src);
		return;
	}
	dst = XRenderCreatePicture(dpy, dest, fmt, 0, NULL);
	mc.red = mc.green = mc.blue = 0xffff;
	mc.alpha = (unsigned short)(alpha * 65535.0 + 0.5);
	mask = XRenderCreateSolidFill(dpy, &mc);
	XRenderComposite(dpy, PictOpOver, src, mask, dst, 0, 0, 0, 0,
	    dx, dy, iw, ih);
	XRenderFreePicture(dpy, mask);
	XRenderFreePicture(dpy, src);
	XRenderFreePicture(dpy, dst);
}

static void
punch_fill_from_outline(Pixmap opix, Pixmap fpix, int iw, int ih)
{
	XRenderPictFormat *fmt;
	Picture src, dst;

	fmt = XRenderFindStandardFormat(dpy, PictStandardARGB32);
	if (fmt == NULL)
		return;
	src = XRenderCreatePicture(dpy, fpix, fmt, 0, NULL);
	dst = XRenderCreatePicture(dpy, opix, fmt, 0, NULL);
	XRenderComposite(dpy, PictOpOutReverse, src, None, dst,
	    0, 0, 0, 0, 0, 0, iw, ih);
	XRenderFreePicture(dpy, src);
	XRenderFreePicture(dpy, dst);
}

static Pixmap
stamp_xft(XftFont *f, int lx, int ly, const char *text, int len, int s,
    const char *color, int iw, int ih)
{
	XftDraw *td;
	XftColor ink;
	XRenderPictFormat *fmt;
	XRenderColor clear;
	Pixmap pix;
	Picture tp;
	Drawable parent = paint_drawable();
	Visual *vis = paint_visual();
	Colormap cm = paint_cmap();
	int dx, dy;
	int fill = (color != NULL);

	fmt = XRenderFindStandardFormat(dpy, PictStandardARGB32);
	if (fmt == NULL || parent == 0)
		return (None);
	pix = XCreatePixmap(dpy, parent, iw, ih, 32);
	tp = XRenderCreatePicture(dpy, pix, fmt, 0, NULL);
	clear.red = clear.green = clear.blue = clear.alpha = 0;
	XRenderFillRectangle(dpy, PictOpSrc, tp, &clear, 0, 0, iw, ih);
	XRenderFreePicture(dpy, tp);

	td = XftDrawCreate(dpy, pix, vis, cm);
	if (td == NULL ||
	    !XftColorAllocName(dpy, vis, cm,
	    fill ? color : "black", &ink)) {
		if (td != NULL)
			XftDrawDestroy(td);
		XFreePixmap(dpy, pix);
		return (None);
	}
	if (fill) {
		XftDrawStringUtf8(td, &ink, f, lx, ly,
		    (const FcChar8 *)text, len);
	} else {
		for (dx = -s; dx <= s; dx++) {
			for (dy = -s; dy <= s; dy++) {
				if (dx == 0 && dy == 0)
					continue;
				XftDrawStringUtf8(td, &ink, f, lx + dx,
				    ly + dy, (const FcChar8 *)text, len);
			}
		}
	}
	XftColorFree(dpy, vis, cm, &ink);
	XftDrawDestroy(td);
	return (pix);
}

/*
 * Fast path: opaque Xft.  Slow path: stamp outline/fill, punch, then
 * PictOpOver each layer once with a solid alpha mask.
 */
void
draw_outlined_utf8(XftDraw *xd, XftFont *font, int x, int y,
    const char *text, int stroke, const char *fill_color,
    double fill_alpha, double outline_alpha)
{
	XGlyphInfo e;
	XftColor fg, bg;
	XftDraw *own = NULL;
	Pixmap opix = None, fpix = None;
	Visual *vis = paint_visual();
	Colormap cm = paint_cmap();
	Drawable dest = paint_drawable();
	int len, dx, dy, ox, oy, iw, ih, lx, ly;

	if (text == NULL || text[0] == '\0' || font == NULL)
		return;
	len = (int)strlen(text);
	if (fill_color == NULL || fill_color[0] == '\0')
		fill_color = "white";
	if (fill_alpha >= 1.0 && outline_alpha >= 1.0) {
		if (xd == NULL) {
			if (dest == 0)
				return;
			own = XftDrawCreate(dpy, dest, vis, cm);
			xd = own;
		}
		if (xd == NULL)
			return;
		if (!XftColorAllocName(dpy, vis, cm, fill_color, &fg) ||
		    !XftColorAllocName(dpy, vis, cm, "black", &bg)) {
			if (own != NULL)
				XftDrawDestroy(own);
			return;
		}
		if (stroke > 0) {
			for (dx = -stroke; dx <= stroke; dx++) {
				for (dy = -stroke; dy <= stroke; dy++) {
					if (dx == 0 && dy == 0)
						continue;
					XftDrawStringUtf8(xd, &bg, font,
					    x + dx, y + dy,
					    (const FcChar8 *)text, len);
				}
			}
		}
		XftDrawStringUtf8(xd, &fg, font, x, y,
		    (const FcChar8 *)text, len);
		XftColorFree(dpy, vis, cm, &fg);
		XftColorFree(dpy, vis, cm, &bg);
		if (own != NULL)
			XftDrawDestroy(own);
		return;
	}

	XftTextExtentsUtf8(dpy, font, (const FcChar8 *)text, len, &e);
	ox = x - (int)e.x - stroke;
	oy = y - (int)e.y - stroke;
	iw = (int)e.width + 2 * stroke;
	ih = (int)e.height + 2 * stroke;
	if (iw < 1)
		iw = 1;
	if (ih < 1)
		ih = 1;
	lx = x - ox;
	ly = y - oy;

	if (stroke > 0 && outline_alpha > 0.0)
		opix = stamp_xft(font, lx, ly, text, len, stroke, NULL,
		    iw, ih);
	if (fill_alpha > 0.0 || opix != None)
		fpix = stamp_xft(font, lx, ly, text, len, stroke,
		    fill_color, iw, ih);
	if (opix != None && fpix != None)
		punch_fill_from_outline(opix, fpix, iw, ih);
	if (opix != None) {
		blit_layer(opix, iw, ih, ox, oy, outline_alpha);
		XFreePixmap(dpy, opix);
	}
	if (fpix != None) {
		if (fill_alpha > 0.0)
			blit_layer(fpix, iw, ih, ox, oy, fill_alpha);
		XFreePixmap(dpy, fpix);
	}
}
