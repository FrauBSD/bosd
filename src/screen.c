/*
 * Which RandR output a show lands on.
 *
 * An empty selector is the primary output, else the first connected
 * internal panel (eDP/LVDS/DSI), else the whole X screen.  "primary"
 * is that primary output and does not fall back to the panel.
 * "mouse" is the output under the pointer.
 * "focus" is the output that holds the focused window.  When the
 * pointer has the focus, that is the output under the pointer.
 * "all" is one copy per connected output; identical rectangles are
 * counted once, so a mirror is not painted twice.  Any other
 * selector is that output's name (DP-4, eDP-1, ...).  The daemon
 * may remember a default from -S or from bosd.conf; a request that
 * names a screen overrides it.
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
static int	 daemon_screen_fixed;	/* -S on the daemon command */
static char	 warned[BOSD_SCREEN_MAX];

struct output_found {
	char		 name[BOSD_SCREEN_MAX];
	int		 x, y, w, h;
	int		 internal;
	RROutput	 id;
};

enum {
	SEL_EMPTY = 0,
	SEL_ALL,
	SEL_PRIMARY,
	SEL_MOUSE,
	SEL_FOCUS,
	SEL_NAME
};

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

static void
warn_sel(const char *sel, const char *msg)
{
	if (strcmp(warned, sel) == 0)
		return;
	fprintf(stderr, "bosd: %s\n", msg);
	strlcpy(warned, sel, sizeof(warned));
}

static void
target_box(const char *name, int x, int y, int w, int h)
{
	int slot;

	slot = slot_for_name(name);
	if (slot < 0)
		return;
	geom[slot].x = x;
	geom[slot].y = y;
	geom[slot].w = w;
	geom[slot].h = h;
	target_add(slot);
}

static int
output_at(const struct output_found *found, int nf, int x, int y)
{
	int i;

	for (i = 0; i < nf; i++) {
		if (x >= found[i].x && y >= found[i].y &&
		    x < found[i].x + found[i].w &&
		    y < found[i].y + found[i].h)
			return (i);
	}
	return (-1);
}

static int
overlap_area(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
	int x0, y0, x1, y1, w, h;

	x0 = ax > bx ? ax : bx;
	y0 = ay > by ? ay : by;
	x1 = (ax + aw < bx + bw) ? ax + aw : bx + bw;
	y1 = (ay + ah < by + bh) ? ay + ah : by + bh;
	w = x1 - x0;
	h = y1 - y0;
	if (w <= 0 || h <= 0)
		return (0);
	return (w * h);
}

static int
output_holding(const struct output_found *found, int nf,
    int x, int y, int w, int h)
{
	int i, area, best, best_area;

	best = -1;
	best_area = 0;
	for (i = 0; i < nf; i++) {
		area = overlap_area(x, y, w, h, found[i].x, found[i].y,
		    found[i].w, found[i].h);
		if (area > best_area) {
			best_area = area;
			best = i;
		}
	}
	return (best);
}

/*
 * 0: the focused window's rectangle on the root is filled in.
 * 1: focus follows the pointer.
 * -1: there is no focused window.
 */
static int
focus_rect(Window root, int *x, int *y, int *w, int *h)
{
	Window focus, child;
	int revert, rx, ry;
	XWindowAttributes attr;

	focus = None;
	XGetInputFocus(dpy, &focus, &revert);
	if (focus == None)
		return (-1);
	if (focus == PointerRoot)
		return (1);
	if (XGetWindowAttributes(dpy, focus, &attr) == 0)
		return (-1);
	if (attr.map_state != IsViewable)
		return (-1);
	if (!XTranslateCoordinates(dpy, focus, root, 0, 0, &rx, &ry, &child))
		return (-1);
	*x = rx;
	*y = ry;
	*w = attr.width;
	*h = attr.height;
	if (*w <= 0 || *h <= 0)
		return (-1);
	return (0);
}

static int
sel_kind(const char *sel)
{
	if (sel == NULL || sel[0] == '\0')
		return (SEL_EMPTY);
	if (strcmp(sel, "all") == 0)
		return (SEL_ALL);
	if (strcmp(sel, "primary") == 0)
		return (SEL_PRIMARY);
	if (strcmp(sel, "mouse") == 0)
		return (SEL_MOUSE);
	if (strcmp(sel, "focus") == 0)
		return (SEL_FOCUS);
	return (SEL_NAME);
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
screen_fix_default(void)
{
	daemon_screen_fixed = 1;
}

int
screen_default_fixed(void)
{
	return (daemon_screen_fixed);
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
 * Fill the target list.  Returns 0, or -1 when the selector does
 * not name a connected output.  sel NULL or "" is the primary,
 * else the built-in panel, else the whole X screen.  "primary"
 * is the primary output only.  "mouse" is the output under the
 * pointer.  "focus" is the output that holds the focused window.
 */
int
screen_prepare(const char *sel)
{
	struct output_found found[BOSD_OUTPUT_MAX];
	Window root;
	XRRScreenResources *res;
	XRROutputInfo *oi;
	XRRCrtcInfo *ci;
	RROutput primary;
	int screen, i, nf, pick, kind;

	targets_reset();
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	nf = 0;
	kind = sel_kind(sel);

	res = XRRGetScreenResourcesCurrent(dpy, root);
	if (res != NULL) {
		for (i = 0; i < res->noutput && nf < BOSD_OUTPUT_MAX; i++) {
			size_t nlen;
			struct output_found *f;

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

		if (kind == SEL_EMPTY || kind == SEL_PRIMARY) {
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
			if (pick < 0 && kind == SEL_EMPTY) {
				for (i = 0; i < nf; i++) {
					if (found[i].internal) {
						pick = i;
						break;
					}
				}
			}
			if (pick >= 0) {
				target_box(found[pick].name, found[pick].x,
				    found[pick].y, found[pick].w,
				    found[pick].h);
			} else if (kind == SEL_PRIMARY) {
				XRRFreeScreenResources(res);
				warn_sel("primary", "no primary output");
				targets_reset();
				return (-1);
			}
		} else if (kind == SEL_ALL) {
			for (i = 0; i < nf; i++) {
				if (rect_taken(found[i].x, found[i].y,
				    found[i].w, found[i].h))
					continue;
				target_box(found[i].name, found[i].x,
				    found[i].y, found[i].w, found[i].h);
			}
		} else if (kind == SEL_MOUSE) {
			Window root_ret, child;
			int rx, ry, wx, wy, hit;
			unsigned int mask;

			hit = -1;
			if (XQueryPointer(dpy, root, &root_ret, &child,
			    &rx, &ry, &wx, &wy, &mask))
				hit = output_at(found, nf, rx, ry);
			if (hit < 0) {
				XRRFreeScreenResources(res);
				warn_sel("mouse",
				    "pointer is not on a connected output");
				targets_reset();
				return (-1);
			}
			target_box(found[hit].name, found[hit].x,
			    found[hit].y, found[hit].w, found[hit].h);
		} else if (kind == SEL_FOCUS) {
			Window root_ret, child;
			int fx, fy, fw, fh, rx, ry, wx, wy, hit, rc;
			unsigned int mask;

			rc = focus_rect(root, &fx, &fy, &fw, &fh);
			hit = -1;
			if (rc == 0) {
				hit = output_holding(found, nf, fx, fy, fw, fh);
			} else if (rc == 1 && XQueryPointer(dpy, root,
			    &root_ret, &child, &rx, &ry, &wx, &wy, &mask)) {
				hit = output_at(found, nf, rx, ry);
			}
			if (hit < 0) {
				XRRFreeScreenResources(res);
				if (rc < 0)
					warn_sel("focus", "no focused window");
				else if (rc == 1)
					warn_sel("focus",
					    "pointer is not on a connected output");
				else
					warn_sel("focus",
					    "focused window is not on a "
					    "connected output");
				targets_reset();
				return (-1);
			}
			target_box(found[hit].name, found[hit].x,
			    found[hit].y, found[hit].w, found[hit].h);
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
			target_box(found[pick].name, found[pick].x,
			    found[pick].y, found[pick].w, found[pick].h);
		}
		XRRFreeScreenResources(res);
	} else if (kind == SEL_NAME) {
		warn_once(sel);
		return (-1);
	} else if (kind == SEL_PRIMARY) {
		warn_sel("primary", "no primary output");
		return (-1);
	} else if (kind == SEL_MOUSE) {
		warn_sel("mouse", "pointer is not on a connected output");
		return (-1);
	} else if (kind == SEL_FOCUS) {
		warn_sel("focus", "no focused window");
		return (-1);
	}

	if (ntargets == 0) {
		if (kind == SEL_NAME) {
			warn_once(sel);
			return (-1);
		}
		if (kind == SEL_PRIMARY) {
			warn_sel("primary", "no primary output");
			return (-1);
		}
		if (kind == SEL_MOUSE) {
			warn_sel("mouse",
			    "pointer is not on a connected output");
			return (-1);
		}
		if (kind == SEL_FOCUS) {
			warn_sel("focus", "no focused window");
			return (-1);
		}
		target_whole();
	}
	warned[0] = '\0';
	return (0);
}
