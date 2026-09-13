/*
 * Gauge bar lifecycle: the classic tick-bar OSD on an ARGB window so
 * a gauge and a glyph coexist on one channel.  Geometry lives in
 * bar_geom.c; tick and label painters in bar_paint.c.
 *
 * bosd assigns the bar no meaning: it draws the given percentage in
 * the given color.  Above 100% the fill stays full and "N%" sits just
 * past the bar's right edge.  Alone, -p/-a captions flank that seat
 * in the same tick-sized face; -f selects the family and -F the
 * label color (else the tick color).  An optional previous percent
 * marks short ticks at or above that watermark dimmer; tall ticks
 * are never dimmed.
 */
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/Xrender.h>

#include "priv.h"

static Window	 bwin;
static Pixmap	 bpix;		/* ARGB backing; survives Expose */
static Colormap	 bcmap;
static Visual	*bvisual;
static int	 bdepth;
static int	 bmapped;

static Window
bar_window(int x, int y, int w, int h)
{
	Window swin;

	swin = argb_osd_window(x, y, w, h, &bvisual, &bcmap, &bdepth, 0);
	if (swin != 0)
		XStoreName(dpy, swin, "bosd");
	return (swin);
}

static int
pct_ticks(int pct)
{
	if (pct < 0)
		pct = 0;
	if (pct > 100)
		pct = 100;
	return (BOSD_BAR_TICKS * pct / 100);
}

static void
bar_named_rgb(const char *name, unsigned char *r, unsigned char *g,
    unsigned char *b)
{
	XColor col, exact;
	Colormap cmap = DefaultColormap(dpy, DefaultScreen(dpy));

	*r = *g = *b = 255;
	if (XAllocNamedColor(dpy, cmap, name, &col, &exact)) {
		*r = (unsigned char)(col.red >> 8);
		*g = (unsigned char)(col.green >> 8);
		*b = (unsigned char)(col.blue >> 8);
	}
}

static void
bar_resolve_colors(const struct show_req *req, unsigned char *fr,
    unsigned char *fg, unsigned char *fb, unsigned char *dr,
    unsigned char *dg, unsigned char *db)
{
	bar_named_rgb(req->color[0] != '\0' ? req->color : BOSD_GAUGE_DEF,
	    fr, fg, fb);
	*dr = *fr / 2;
	*dg = *fg / 2;
	*db = *fb / 2;
}

/* Outline (optional) then fill; same alphas as the ticks. */
static void
bar_stamp_pair(Picture pic, int w, int lineh, const char *s, int x,
    int base, unsigned char fr, unsigned char fg, unsigned char fb,
    unsigned char fa, unsigned char oa)
{
	if (oa > 0)
		bar_stamp_label(pic, w, lineh, s, x, base, 0, 0, 0, oa, 1);
	bar_stamp_label(pic, w, lineh, s, x, base, fr, fg, fb, fa, 0);
}

/*
 * Baseline so the string's ink centerline matches the tall-tick
 * centerline.  XGlyphInfo.y is the rise from baseline to ink top.
 */
static int
bar_label_base(const struct bar_geom *m, const XGlyphInfo *e)
{
	return (m->outl + m->tick_h / 2 + (int)e->y - (int)e->height / 2);
}

static void
bar_measure(const char *s, XGlyphInfo *e)
{
	XftTextExtentsUtf8(dpy, bar_xfont(), (const FcChar8 *)s,
	    (int)strlen(s), e);
}

/*
 * -p left of the bar (right-justified), overage past the right edge,
 * -a at the overage seat or left-justified just past overage when both.
 * All three share the tick-derived face and -A/-O/-o alphas; fill
 * color is -F when set on a gauge alone, else the tick color.
 */
static void
bar_draw_labels(Picture pic, const struct bar_geom *m, int w,
    const char *pfx, const char *apx, int gauge, unsigned char fr,
    unsigned char fg, unsigned char fb, unsigned char fa, unsigned char oa)
{
	XGlyphInfo e;
	char over[16];
	int bx, bar_r, seat, base, x;
	int have_over, have_pfx, have_apx;

	have_pfx = pfx != NULL && pfx[0] != '\0';
	have_apx = apx != NULL && apx[0] != '\0';
	have_over = gauge > 100;
	if (!have_pfx && !have_apx && !have_over)
		return;

	bx = (w - BOSD_BAR_TICKS * m->pitch) / 2;
	bar_r = bx + BOSD_BAR_TICKS * m->pitch;
	seat = bar_r + m->over_gap + m->text_xoff;

	if (have_pfx) {
		bar_measure(pfx, &e);
		base = bar_label_base(m, &e);
		/* Right edge of the advance lands at the left seat. */
		x = bx - m->over_gap - m->text_xoff - (int)e.xOff;
		bar_stamp_pair(pic, w, m->lineh, pfx, x, base, fr, fg, fb,
		    fa, oa);
	}
	if (have_over) {
		snprintf(over, sizeof(over), "%d%%", gauge);
		bar_measure(over, &e);
		base = bar_label_base(m, &e);
		bar_stamp_pair(pic, w, m->lineh, over, seat, base, fr, fg, fb,
		    fa, oa);
		seat += (int)e.xOff + m->over_gap;
	}
	if (have_apx) {
		bar_measure(apx, &e);
		base = bar_label_base(m, &e);
		bar_stamp_pair(pic, w, m->lineh, apx, seat, base, fr, fg, fb,
		    fa, oa);
	}
}

static int
bar_prepare_surface(const struct bar_geom *m, int x, int y, int w,
    Pixmap *pix_out, Picture *pic_out)
{
	XRenderPictFormat *fmt;
	XRenderColor clear;
	Pixmap pix;
	Picture pic;

	if (bwin == 0 && (bwin = bar_window(x, y, w, m->lineh)) == 0)
		return (-1);
	XMoveResizeWindow(dpy, bwin, x, y, (unsigned)w, (unsigned)m->lineh);

	fmt = XRenderFindStandardFormat(dpy, PictStandardARGB32);
	if (fmt == NULL)
		return (-1);
	pix = XCreatePixmap(dpy, bwin, (unsigned)w, (unsigned)m->lineh, 32);
	pic = XRenderCreatePicture(dpy, pix, fmt, 0, NULL);
	clear.red = clear.green = clear.blue = clear.alpha = 0;
	XRenderFillRectangle(dpy, PictOpSrc, pic, &clear, 0, 0, w, m->lineh);
	*pix_out = pix;
	*pic_out = pic;
	return (0);
}

int
bar_show(const struct show_req *req)
{
	const struct bar_geom *m;
	const char *face, *pfx, *apx;
	Picture pic;
	Pixmap pix;
	int on, prev_on, bx, x, y, w, alone, need_font;
	unsigned char fr, fg, fb, fa, dr, dg, db, oa;
	unsigned char lr, lg, lb;
	double fill_a, out_a;

	/*
	 * -p/-a/-f/-F adorn the bar only when it is alone.  With an
	 * icon, -c/-T, or -t they belong to that artwork (and -f/-F
	 * size that face, not the gauge labels).  -s always scales
	 * the bar's panel-derived tick geometry.
	 */
	alone = req->spec[0] == '\0' && req->count == 0 && !req->text &&
	    !req->small;
	bar_geom_set_scale(req->scale);
	m = bar_geom_get();
	w = scr_w;
	x = scr_x + req->x_off;
	/* y_nudge shifts the band (positive down) from the curated seat. */
	y = scr_y + scr_h - m->lineh - m->voff + m->y_nudge + req->y_off;
	face = alone ? req->font : "";
	pfx = alone ? req->prefix : "";
	apx = alone ? req->append : "";
	need_font = pfx[0] != '\0' || apx[0] != '\0' || req->gauge > 100;
	if (need_font && bar_ensure_font(face) != 0)
		return (-1);
	if (bar_prepare_surface(m, x, y, w, &pix, &pic) != 0)
		return (-1);

	clamp_paint_alphas(req, &fill_a, &out_a);
	fa = (unsigned char)(fill_a * 255.0 + 0.5);
	oa = (unsigned char)(out_a * 255.0 + 0.5);
	bar_resolve_colors(req, &fr, &fg, &fb, &dr, &dg, &db);
	lr = fr;
	lg = fg;
	lb = fb;
	if (alone && req->tcolor[0] != '\0')
		bar_named_rgb(req->tcolor, &lr, &lg, &lb);

	on = pct_ticks(req->gauge);
	prev_on = req->gauge_prev < 0 ? -1 : pct_ticks(req->gauge_prev);
	bx = (w - BOSD_BAR_TICKS * m->pitch) / 2;
	bar_paint_ticks(pic, on, prev_on, bx, fr, fg, fb, fa, dr, dg, db, oa);
	if (need_font)
		bar_draw_labels(pic, m, w, pfx, apx, req->gauge, lr, lg, lb,
		    fa, oa);

	raise_mapped(bwin, &bmapped);
	if (bar_blit_window(bwin, bvisual, &bpix, pic, pix, w, m->lineh)
	    != 0) {
		XRenderFreePicture(dpy, pic);
		XFreePixmap(dpy, pix);
		return (-1);
	}
	XRenderFreePicture(dpy, pic);
	/* pix retained as bpix inside bar_blit_window */
	XSync(dpy, False);
	return (0);
}

static void
bar_drop_window(void)
{
	if (bpix != 0) {
		if (bwin != 0)
			XSetWindowBackgroundPixmap(dpy, bwin, None);
		XFreePixmap(dpy, bpix);
		bpix = 0;
	}
	if (bwin != 0) {
		XDestroyWindow(dpy, bwin);
		bwin = 0;
	}
	if (bcmap != None) {
		XFreeColormap(dpy, bcmap);
		bcmap = None;
	}
	bvisual = NULL;
}

void
bar_hide(void)
{
	if (bmapped) {
		XUnmapWindow(dpy, bwin);
		bmapped = 0;
	}
	/*
	 * Destroy the window so a later show cannot remap a compositor-
	 * cached frame of the previous fill (seen as the last percent
	 * after the bar had already timed out).
	 */
	bar_drop_window();
	XSync(dpy, False);
}

void
bar_cleanup(void)
{
	if (dpy == NULL)
		return;
	bar_drop_window();
	bar_font_cleanup();
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
