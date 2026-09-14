/*
 * Gauge bar geometry and overage-label font.
 *
 * Panel-proportional sizes from curated pixels at BOSD_PANEL_REF_H
 * (primary scr_h only; never the X virtual desktop), times -s.
 * Bottom clearance (voff) stays panel-only so the seat does not
 * float.  The overage label face is sized to the tick height.
 */
#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>

#include "priv.h"

#define BAR_REF_TICK	40	/* tall tick height at BOSD_PANEL_REF_H */
#define BAR_REF_VOFF	64	/* bottom clearance at BOSD_PANEL_REF_H */
#define BAR_REF_EXTRA	10	/* lineh pad beyond tick+outline at ref */
#define BAR_REF_OUTL	2	/* outline thickness at BOSD_PANEL_REF_H */
#define BAR_REF_OGAP	20	/* overage label gap at ref */
#define BAR_REF_XOFF	10	/* label inset at ref */
#define BAR_Y_NUDGE	(-2)	/* raise the band this many px at ref */

static struct bar_geom g;
static XftFont	*bar_font;
static int	 font_for_tick;
static char	 font_face[BOSD_FONT_MAX];
static double	 g_scale = 1.0;

static int
scale_ref(int ref, int h)
{
	int v = ref * h / BOSD_PANEL_REF_H;

	return (v);
}

static int
scale_user(int panel_px, double s)
{
	return ((int)(panel_px * s + (panel_px >= 0 ? 0.5 : -0.5)));
}

static void
bar_geom_clamp(struct bar_geom *m)
{
	if (m->tick_h < 8)
		m->tick_h = 8;
	m->pitch = m->tick_h / 2;
	if (m->pitch < 1)
		m->pitch = 1;
	if (m->outl < 1)
		m->outl = 1;
	if (m->lineh < m->tick_h + 2 * m->outl)
		m->lineh = m->tick_h + 2 * m->outl;
	if (m->voff < 1)
		m->voff = 1;
	if (m->over_gap < 1)
		m->over_gap = 1;
	if (m->text_xoff < 1)
		m->text_xoff = 1;
}

static double
clamp_scale(double scale)
{
	/* Unset/invalid (IPC memset 0) means default, not BOSD_SCALE_MIN. */
	if (scale <= 0.0)
		return (1.0);
	if (scale < BOSD_SCALE_MIN)
		return (BOSD_SCALE_MIN);
	if (scale > BOSD_SCALE_MAX)
		return (BOSD_SCALE_MAX);
	return (scale);
}

static void
bar_geom_compute(struct bar_geom *m, double scale)
{
	int h = scr_h > 0 ? scr_h : BOSD_PANEL_REF_H;
	double s = clamp_scale(scale);

	m->tick_h = scale_user(scale_ref(BAR_REF_TICK, h), s);
	m->outl = scale_user(scale_ref(BAR_REF_OUTL, h), s);
	m->lineh = m->tick_h + scale_user(scale_ref(BAR_REF_EXTRA, h), s) +
	    2 * m->outl;
	/* Bottom seat stays panel-proportional; -s does not lift it. */
	m->voff = scale_ref(BAR_REF_VOFF, h);
	m->over_gap = scale_user(scale_ref(BAR_REF_OGAP, h), s);
	m->text_xoff = scale_user(scale_ref(BAR_REF_XOFF, h), s);
	m->y_nudge = scale_user(scale_ref(BAR_Y_NUDGE, h), s);
	bar_geom_clamp(m);
}

void
bar_geom_set_scale(double scale)
{
	g_scale = clamp_scale(scale);
}

void
bar_geom_refresh(void)
{
	bar_geom_compute(&g, g_scale);
}

const struct bar_geom *
bar_geom_get(void)
{
	bar_geom_refresh();
	return (&g);
}

int
bar_band_height(double scale)
{
	struct bar_geom m;

	bar_geom_compute(&m, scale);
	return (m.voff + m.lineh - m.y_nudge);
}

static XftFont *
bar_open_face(int px, const char *face)
{
	char pattern[BOSD_FONT_MAX + 64];
	char attrs[64];
	XftFont *f;
	int screen = DefaultScreen(dpy);
	const char *want = (face != NULL) ? face : "";

	snprintf(attrs, sizeof(attrs), "pixelsize=%d:antialias=true", px);
	font_pattern(pattern, sizeof(pattern), want, BOSD_FIXED_FACE, attrs);
	f = XftFontOpenName(dpy, screen, pattern);
	if (f != NULL)
		return (f);
	if (want[0] != '\0') {
		font_pattern(pattern, sizeof(pattern), NULL, BOSD_FIXED_FACE,
		    attrs);
		f = XftFontOpenName(dpy, screen, pattern);
		if (f != NULL)
			return (f);
	}
	font_pattern(pattern, sizeof(pattern), NULL, "Sans", attrs);
	return (XftFontOpenName(dpy, screen, pattern));
}

/*
 * Open a face so its ascent fits the panel-derived tick height.
 * Largest pixelsize with ascent <= tick_h wins.  Empty face uses
 * BOSD_FIXED_FACE.
 */
int
bar_ensure_font(const char *face)
{
	XftFont *f;
	const char *want = (face != NULL) ? face : "";
	int px;

	bar_geom_refresh();
	if (bar_font != NULL && font_for_tick == g.tick_h &&
	    strcmp(font_face, want) == 0)
		return (0);
	bar_font_cleanup();
	for (px = g.tick_h + 12; px >= 8; px--) {
		f = bar_open_face(px, want);
		if (f == NULL)
			continue;
		if (f->ascent <= g.tick_h) {
			bar_font = f;
			font_for_tick = g.tick_h;
			strlcpy(font_face, want, sizeof(font_face));
			return (0);
		}
		XftFontClose(dpy, f);
	}
	return (-1);
}

XftFont *
bar_xfont(void)
{
	return (bar_font);
}

void
bar_font_cleanup(void)
{
	if (bar_font != NULL) {
		XftFontClose(dpy, bar_font);
		bar_font = NULL;
	}
	font_for_tick = 0;
	font_face[0] = '\0';
}
