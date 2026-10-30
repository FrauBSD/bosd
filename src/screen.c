/*
 * Which RandR output a show lands on.
 *
 * An empty selector is the primary output, else the first connected
 * internal panel (eDP/LVDS/DSI), else the whole X screen.  "all" is
 * one copy per connected output; identical rectangles are counted
 * once, so a mirror is not painted twice.  Any other selector is
 * that output's name (DP-4, eDP-1, ...).  The daemon may remember a
 * default from -S; a request that names a screen overrides it.
 */
#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>

#include "priv.h"

/* Last slot is the whole-X-screen fallback, not a named output. */
#define BOSD_SCREEN_WHOLE	BOSD_OUTPUT_MAX

struct screen_geom {
	char	 name[BOSD_SCREEN_MAX];
	int	 x, y, w, h;
};

static struct screen_geom geom[BOSD_SCREEN_SLOTS];
static int	 targets[BOSD_SCREEN_SLOTS];
static int	 ntargets;
static unsigned char targeted[BOSD_SCREEN_SLOTS];
static char	 daemon_screen[BOSD_SCREEN_MAX];
static char	 warned[BOSD_SCREEN_MAX];

static void
apply_icon_px(void)
{
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

static void
targets_reset(void)
{
	memset(targeted, 0, sizeof(targeted));
	ntargets = 0;
}

static void
target_add(int slot)
{
	int i;

	if (slot < 0 || slot >= BOSD_SCREEN_SLOTS)
		return;
	for (i = 0; i < ntargets; i++) {
		if (targets[i] == slot)
			return;
	}
	if (ntargets >= BOSD_SCREEN_SLOTS)
		return;
	targets[ntargets++] = slot;
	targeted[slot] = 1;
}

static int
rect_taken(int x, int y, int w, int h)
{
	int i, slot;

	for (i = 0; i < ntargets; i++) {
		slot = targets[i];
		if (geom[slot].x == x && geom[slot].y == y &&
		    geom[slot].w == w && geom[slot].h == h)
			return (1);
	}
	return (0);
}

/* Reuse a slot for an output name so its window survives a refresh. */
static int
slot_for_name(const char *name)
{
	int i, free_slot = -1;

	for (i = 0; i < BOSD_OUTPUT_MAX; i++) {
		if (geom[i].name[0] != '\0' &&
		    strcmp(geom[i].name, name) == 0)
			return (i);
		if (free_slot < 0 && geom[i].name[0] == '\0')
			free_slot = i;
	}
	if (free_slot < 0)
		return (-1);
	strlcpy(geom[free_slot].name, name, sizeof(geom[free_slot].name));
	return (free_slot);
}

static void
target_whole(void)
{
	int screen = DefaultScreen(dpy);

	geom[BOSD_SCREEN_WHOLE].name[0] = '\0';
	geom[BOSD_SCREEN_WHOLE].x = 0;
	geom[BOSD_SCREEN_WHOLE].y = 0;
	geom[BOSD_SCREEN_WHOLE].w = DisplayWidth(dpy, screen);
	geom[BOSD_SCREEN_WHOLE].h = DisplayHeight(dpy, screen);
	target_add(BOSD_SCREEN_WHOLE);
}

static int
is_internal(const char *name)
{
	return (strncmp(name, "eDP", 3) == 0 ||
	    strncmp(name, "LVDS", 4) == 0 ||
	    strncmp(name, "DSI", 3) == 0);
}

static void
warn_once(const char *sel)
{
	if (strcmp(warned, sel) == 0)
		return;
	fprintf(stderr, "bosd: no connected output '%s'\n", sel);
	strlcpy(warned, sel, sizeof(warned));
}

void
screen_set_default(const char *name)
{
	if (name == NULL || name[0] == '\0')
		daemon_screen[0] = '\0';
	else
		strlcpy(daemon_screen, name, sizeof(daemon_screen));
}

void
screen_apply_default(struct show_req *req)
{
	if (req == NULL)
		return;
	if (req->screen[0] == '\0' && daemon_screen[0] != '\0')
		strlcpy(req->screen, daemon_screen, sizeof(req->screen));
}

void
screen_check_default(void)
{
	if (daemon_screen[0] != '\0')
		(void)screen_prepare(daemon_screen);
}

int
screen_count(void)
{
	return (ntargets);
}

int
screen_slot(int i)
{
	if (i < 0 || i >= ntargets)
		return (0);
	return (targets[i]);
}

int
screen_targeted(int slot)
{
	if (slot < 0 || slot >= BOSD_SCREEN_SLOTS)
		return (0);
	return (targeted[slot] != 0);
}

void
screen_use(int i)
{
	int slot;

	if (i < 0 || i >= ntargets)
		return;
	slot = targets[i];
	scr_x = geom[slot].x;
	scr_y = geom[slot].y;
	scr_w = geom[slot].w;
	scr_h = geom[slot].h;
	apply_icon_px();
	overlay_select(slot);
}

/*
 * Fill the target list.  Returns 0, or -1 when a named output is
 * not connected.  sel NULL or "" is the primary/internal default.
 */
int
screen_prepare(const char *sel)
{
	struct found {
		char		 name[BOSD_SCREEN_MAX];
		int		 x, y, w, h;
		int		 internal;
		RROutput	 id;
	} found[BOSD_OUTPUT_MAX];
	Window root;
	XRRScreenResources *res;
	XRROutputInfo *oi;
	XRRCrtcInfo *ci;
	RROutput primary;
	int screen, i, nf, pick, named;

	targets_reset();
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	nf = 0;
	named = (sel != NULL && sel[0] != '\0' && strcmp(sel, "all") != 0);

	res = XRRGetScreenResourcesCurrent(dpy, root);
	if (res != NULL) {
		for (i = 0; i < res->noutput && nf < BOSD_OUTPUT_MAX; i++) {
			size_t nlen;
			struct found *f;

			oi = XRRGetOutputInfo(dpy, res, res->outputs[i]);
			if (oi == NULL)
				continue;
			if (oi->connection != RR_Connected ||
			    oi->crtc == None || oi->nameLen <= 0) {
				XRRFreeOutputInfo(oi);
				continue;
			}
			ci = XRRGetCrtcInfo(dpy, res, oi->crtc);
			if (ci == NULL) {
				XRRFreeOutputInfo(oi);
				continue;
			}
			f = &found[nf];
			nlen = (size_t)oi->nameLen;
			if (nlen >= sizeof(f->name))
				nlen = sizeof(f->name) - 1;
			memcpy(f->name, oi->name, nlen);
			f->name[nlen] = '\0';
			f->x = (int)ci->x;
			f->y = (int)ci->y;
			f->w = (int)ci->width;
			f->h = (int)ci->height;
			f->internal = is_internal(f->name);
			f->id = res->outputs[i];
			nf++;
			XRRFreeCrtcInfo(ci);
			XRRFreeOutputInfo(oi);
		}

		if (!named && (sel == NULL || sel[0] == '\0')) {
			pick = -1;
			primary = XRRGetOutputPrimary(dpy, root);
			if (primary != None) {
				for (i = 0; i < nf; i++) {
					if (found[i].id == primary) {
						pick = i;
						break;
					}
				}
			}
			if (pick < 0) {
				for (i = 0; i < nf; i++) {
					if (found[i].internal) {
						pick = i;
						break;
					}
				}
			}
			if (pick >= 0) {
				int slot = slot_for_name(found[pick].name);

				if (slot >= 0) {
					geom[slot].x = found[pick].x;
					geom[slot].y = found[pick].y;
					geom[slot].w = found[pick].w;
					geom[slot].h = found[pick].h;
					target_add(slot);
				}
			}
		} else if (!named) {
			/* "all" */
			for (i = 0; i < nf; i++) {
				int slot;

				if (rect_taken(found[i].x, found[i].y,
				    found[i].w, found[i].h))
					continue;
				slot = slot_for_name(found[i].name);
				if (slot < 0)
					continue;
				geom[slot].x = found[i].x;
				geom[slot].y = found[i].y;
				geom[slot].w = found[i].w;
				geom[slot].h = found[i].h;
				target_add(slot);
			}
		} else {
			pick = -1;
			for (i = 0; i < nf; i++) {
				if (strcmp(found[i].name, sel) == 0) {
					pick = i;
					break;
				}
			}
			if (pick < 0) {
				XRRFreeScreenResources(res);
				warn_once(sel);
				targets_reset();
				return (-1);
			}
			{
				int slot = slot_for_name(found[pick].name);

				if (slot >= 0) {
					geom[slot].x = found[pick].x;
					geom[slot].y = found[pick].y;
					geom[slot].w = found[pick].w;
					geom[slot].h = found[pick].h;
					target_add(slot);
				}
			}
		}
		XRRFreeScreenResources(res);
	} else if (named) {
		warn_once(sel);
		return (-1);
	}

	if (ntargets == 0) {
		if (named) {
			warn_once(sel);
			return (-1);
		}
		target_whole();
	}
	warned[0] = '\0';
	return (0);
}
