/*
 * Small-text fonts and shaped-line painters used by stext.c.
 */
#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>

#include "priv.h"

#define STEXT_XLFD "-misc-fixed-medium-r-normal--48-*-*-*-*-*-*"
#define STEXT_PX   48	/* base pixel size before -s */
#define STEXT_OUTL 4	/* base outline thickness before -s */

static XFontSet	 fset;
static XftFont	*xfont;
static char	 xface[BOSD_FONT_MAX];
static int	 ascent, lineh, outl, xpx;
static int	 use_xft;

void
stext_close_fonts(void)
{
	if (fset != NULL) {
		XFreeFontSet(dpy, fset);
		fset = NULL;
	}
	if (xfont != NULL) {
		XftFontClose(dpy, xfont);
		xfont = NULL;
	}
	xface[0] = '\0';
	xpx = 0;
	use_xft = 0;
}

int
stext_metrics(const char *face, double scale)
{
	XFontSetExtents *ex;
	char **missing;
	int nmissing;
	char *def;
	char pattern[BOSD_FONT_MAX + 64];
	char attrs[64];
	const char *want = (face != NULL) ? face : "";
	int px, ol;

	if (scale < BOSD_SCALE_MIN)
		scale = BOSD_SCALE_MIN;
	if (scale > BOSD_SCALE_MAX)
		scale = BOSD_SCALE_MAX;
	px = (int)(STEXT_PX * scale + 0.5);
	if (px < 8)
		px = 8;
	ol = (int)(STEXT_OUTL * scale + 0.5);
	if (ol < 1)
		ol = 1;

	/*
	 * Unscaled default: try classic misc-fixed XLFD at STEXT_PX;
	 * if no bitmap exists (common past 20px), fall through to Xft.
	 */
	if (want[0] == '\0' && scale >= 0.999 && scale <= 1.001) {
		if (!use_xft && fset != NULL) {
			outl = STEXT_OUTL;
			return (0);
		}
		stext_close_fonts();
		fset = XCreateFontSet(dpy, STEXT_XLFD, &missing, &nmissing,
		    &def);
		if (missing != NULL)
			XFreeStringList(missing);
		if (fset != NULL) {
			ex = XExtentsOfFontSet(fset);
			ascent = -ex->max_logical_extent.y;
			if (ascent < 2)
				ascent = 20;
			outl = STEXT_OUTL;
			lineh = ex->max_logical_extent.height + 2 * outl;
			return (0);
		}
	}

	if (use_xft && xfont != NULL && xpx == px &&
	    strcmp(xface, want) == 0) {
		outl = ol;
		return (0);
	}
	stext_close_fonts();
	snprintf(attrs, sizeof(attrs), "pixelsize=%d:antialias=true", px);
	font_pattern(pattern, sizeof(pattern), want,
	    want[0] != '\0' ? "Sans" : "Fixed", attrs);
	xfont = XftFontOpenName(dpy, DefaultScreen(dpy), pattern);
	if (xfont == NULL && want[0] == '\0') {
		font_pattern(pattern, sizeof(pattern), NULL, "Sans", attrs);
		xfont = XftFontOpenName(dpy, DefaultScreen(dpy), pattern);
	}
	if (xfont == NULL)
		return (-1);
	strlcpy(xface, want, sizeof(xface));
	xpx = px;
	use_xft = 1;
	ascent = xfont->ascent;
	if (ascent < 2)
		ascent = 20;
	outl = ol;
	lineh = xfont->ascent + xfont->descent + 2 * outl;
	return (0);
}

int
stext_ascent(void)
{
	return (ascent);
}

int
stext_lineh(void)
{
	return (lineh);
}

int
stext_outl(void)
{
	return (outl);
}

int
stext_use_xft(void)
{
	return (use_xft);
}

XFontSet
stext_fset(void)
{
	return (fset);
}

XftFont *
stext_xfont(void)
{
	return (xfont);
}

/* Square outline passes, then one fill pass, into pixmap and mask. */
void
stext_line_xlfd(Pixmap pix, Pixmap mask, GC pgc, GC mgc, const char *s,
    int x, int base, int grow_pass)
{
	int len = (int)strlen(s), dx, dy;

	if (grow_pass) {
		for (dx = -outl; dx <= outl; dx++)
			for (dy = -outl; dy <= outl; dy++) {
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

void
stext_line_xft(XftDraw *xd, const char *s, int x, int base, XftColor *ink,
    int grow_pass)
{
	int len = (int)strlen(s), dx, dy;

	if (grow_pass) {
		for (dx = -outl; dx <= outl; dx++)
			for (dy = -outl; dy <= outl; dy++) {
				if (dx == 0 && dy == 0)
					continue;
				XftDrawStringUtf8(xd, ink, xfont, x + dx,
				    base + dy, (const FcChar8 *)s, len);
			}
	} else {
		XftDrawStringUtf8(xd, ink, xfont, x, base,
		    (const FcChar8 *)s, len);
	}
}

/*
 * Xft cannot paint a 1-bit mask directly.  Stamp white outline+fill on a
 * temp pixmap and copy every non-black pixel into the shape mask so the
 * window stays letter-shaped (same idea as the XLFD path).
 * ol_override < 0 uses the metrics outline; else that thickness (0 =
 * fill-only mask).
 */
void
stext_mask_line_xft(Window twin, Pixmap mask, GC mgc, const char *s,
    int x, int base, Visual *vis, Colormap cm, int ol_override)
{
	XGlyphInfo e;
	XftDraw *td;
	XftColor white;
	Pixmap tmp;
	XImage *img;
	GC tgc;
	int len, bx, by, bw, bh, ix, iy, depth, ol;
	unsigned long blk;

	ol = ol_override < 0 ? outl : ol_override;
	len = (int)strlen(s);
	XftTextExtentsUtf8(dpy, xfont, (const FcChar8 *)s, len, &e);
	bx = x - (int)e.x - ol;
	by = base - (int)e.y - ol;
	bw = (int)e.width + 2 * ol;
	bh = (int)e.height + 2 * ol;
	if (bw < 1)
		bw = 1;
	if (bh < 1)
		bh = 1;

	depth = DefaultDepth(dpy, DefaultScreen(dpy));
	tmp = XCreatePixmap(dpy, twin, (unsigned)bw, (unsigned)bh, depth);
	tgc = XCreateGC(dpy, tmp, 0, NULL);
	blk = BlackPixel(dpy, DefaultScreen(dpy));
	XSetForeground(dpy, tgc, blk);
	XFillRectangle(dpy, tmp, tgc, 0, 0, (unsigned)bw, (unsigned)bh);
	XFreeGC(dpy, tgc);

	td = XftDrawCreate(dpy, tmp, vis, cm);
	if (td == NULL ||
	    !XftColorAllocName(dpy, vis, cm, "white", &white)) {
		if (td != NULL)
			XftDrawDestroy(td);
		XFreePixmap(dpy, tmp);
		return;
	}
	/* Grow with ol, then fill; temporarily swap outl for line helper. */
	{
		int save = outl;

		outl = ol;
		stext_line_xft(td, s, x - bx, base - by, &white, 1);
		stext_line_xft(td, s, x - bx, base - by, &white, 0);
		outl = save;
	}
	XftColorFree(dpy, vis, cm, &white);
	XftDrawDestroy(td);

	img = XGetImage(dpy, tmp, 0, 0, (unsigned)bw, (unsigned)bh,
	    AllPlanes, ZPixmap);
	XFreePixmap(dpy, tmp);
	if (img == NULL)
		return;
	XSetForeground(dpy, mgc, 1);
	for (iy = 0; iy < bh; iy++) {
		for (ix = 0; ix < bw; ix++) {
			if (XGetPixel(img, ix, iy) != blk)
				XDrawPoint(dpy, mask, mgc, bx + ix, by + iy);
		}
	}
	XDestroyImage(img);
}
