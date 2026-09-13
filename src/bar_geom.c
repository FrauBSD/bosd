/*
 * Gauge bar geometry and overage-label font.
 *
 * Panel-proportional sizes from curated pixels at BOSD_PANEL_REF_H
 * (primary scr_h only; never the X virtual desktop).  The overage
 * label face is sized to the tick height, not the reverse.
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

static int
scale_ref(int ref, int h)
{
	int v = ref * h / BOSD_PANEL_REF_H;

	return (v);
}

static void
bar_geom_clamp(void)
{
	if (g.tick_h < 8)
		g.tick_h = 8;
	g.pitch = g.tick_h / 2;
	if (g.pitch < 1)
		g.pitch = 1;
	if (g.outl < 1)
		g.outl = 1;
	if (g.lineh < g.tick_h + 2 * g.outl)
		g.lineh = g.tick_h + 2 * g.outl;
	if (g.voff < 1)
		g.voff = 1;
	if (g.over_gap < 1)
		g.over_gap = 1;
	if (g.text_xoff < 1)
		g.text_xoff = 1;
}

void
bar_geom_refresh(void)
{
	int h = scr_h > 0 ? scr_h : BOSD_PANEL_REF_H;

	g.tick_h = scale_ref(BAR_REF_TICK, h);
	g.outl = scale_ref(BAR_REF_OUTL, h);
	g.lineh = g.tick_h + scale_ref(BAR_REF_EXTRA, h) + 2 * g.outl;
	g.voff = scale_ref(BAR_REF_VOFF, h);
	g.over_gap = scale_ref(BAR_REF_OGAP, h);
	g.text_xoff = scale_ref(BAR_REF_XOFF, h);
	g.y_nudge = scale_ref(BAR_Y_NUDGE, h);
	bar_geom_clamp();
}

const struct bar_geom *
bar_geom_get(void)
{
	bar_geom_refresh();
	return (&g);
}

int
bar_band_height(void)
{
	bar_geom_refresh();
	return (g.voff + g.lineh - g.y_nudge);
}

static XftFont *
bar_open_face(int px)
{
	char pattern[BOSD_FONT_MAX + 64];
	char attrs[64];
	XftFont *f;
	int screen = DefaultScreen(dpy);

	snprintf(attrs, sizeof(attrs), "pixelsize=%d:antialias=true", px);
	font_pattern(pattern, sizeof(pattern), NULL, BOSD_FIXED_FACE, attrs);
	f = XftFontOpenName(dpy, screen, pattern);
	if (f != NULL)
		return (f);
	font_pattern(pattern, sizeof(pattern), NULL, "Sans", attrs);
	return (XftFontOpenName(dpy, screen, pattern));
}

/*
 * Open BOSD_FIXED_FACE so its ascent fits the panel-derived tick
 * height.  Largest pixelsize with ascent <= tick_h wins.
 */
int
bar_ensure_font(void)
{
	XftFont *f;
	int px;

	bar_geom_refresh();
	if (bar_font != NULL && font_for_tick == g.tick_h)
		return (0);
	bar_font_cleanup();
	for (px = g.tick_h + 12; px >= 8; px--) {
		f = bar_open_face(px);
		if (f == NULL)
			continue;
		if (f->ascent <= g.tick_h) {
			bar_font = f;
			font_for_tick = g.tick_h;
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
}
