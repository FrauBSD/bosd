/*
 * bosd -- on-screen display engine.
 *
 * One daemon instance per OSD channel (per-UID datagram socket); clients
 * send "hold icon-spec" show requests.  Glyphs are PNG files, pre-scaled
 * to the internal panel and outlined, painted into a panel-centered
 * ARGB32 override-redirect window.  Replacing a mapped glyph overwrites
 * pixels in place (no unmap/remap flash).
 */
#ifndef BOSD_H
#define BOSD_H

#include <signal.h>
#include <stddef.h>

#include <X11/Xlib.h>

#define BOSD_VERSION	"1.0"

#define BOSD_SPEC_MAX	1024	/* icon spec (path or bare name) */
#define BOSD_BADGE_MAX	32	/* superscript label */
#define BOSD_CAPTION_MAX 64	/* caption above/below the artwork */
#define BOSD_MSG_MAX	1664	/* show datagram, captions escaped */
#define BOSD_HOLD_DEF	2.0
#define BOSD_HOLD_MIN	0.01
#define BOSD_HOLD_MAX	30.0
#define BOSD_SCALE_MIN	0.1
#define BOSD_SCALE_MAX	8.0

#ifndef BOSD_ICONDIR
#define BOSD_ICONDIR	"/usr/local/share/bosd"
#endif

struct icon {
	struct icon	*next;
	char		 path[BOSD_SPEC_MAX];
	unsigned char	*rgba;		/* scaled, optionally outlined */
	double		 scale;
	int		 outline;
	int		 w, h;
};

struct show_req {
	double	 hold;
	double	 scale;		/* multiplies the panel-derived size */
	int	 outline;	/* black halo behind the glyph */
	int	 count;		/* > 0: countdown show, not an icon */
	int	 text;		/* spec is text to render, not an icon */
	int	 clear;		/* hide the active render, show nothing */
	int	 x_off;		/* horizontal shift: positive right */
	int	 y_off;		/* vertical shift: positive down */
	char	 spec[BOSD_SPEC_MAX];
	char	 badge[BOSD_BADGE_MAX];
	char	 prefix[BOSD_CAPTION_MAX];	/* caption above */
	char	 append[BOSD_CAPTION_MAX];	/* caption below */
};

/* main.c */
extern char	 instance[64];

/* x11.c */
extern Display	*dpy;
extern Window	 win;
extern Visual	*visual;
extern Colormap	 cmap;
extern int	 scr_x, scr_y, scr_w, scr_h;
extern int	 icon_px, icon_pad;
extern int	 mapped;
extern int	 win_w, win_h;
extern int	 icon_ox, icon_oy;

int	 init_display(void);
Visual	*find_argb_visual(int *depth_out);
int	 layout_fullscreen(void);
void	 raise_overlay(void);
void	 paint_icon(const struct icon *, const struct show_req *);
void	 hide_overlay(void);
void	 x11_cleanup(void);

/* badge.c */
void	 draw_badge(const struct icon *, const char *text);
int	 caption_px(const struct icon *);
int	 caption_gap(void);
void	 caption_measure(const char *text, int px, int *w, int *h);
void	 draw_caption(const char *text, int px, int anchor_y, int below);
void	 badge_cleanup(void);

/* png.c */
struct icon	*icon_lookup(const char *spec, double scale, int outline);
void		 icon_cache_clear(void);

/* ipc.c */
extern char	 sock_name[104];
extern char	 pid_name[104];

void	 resolve_ipc_names(void);
int	 daemon_alive(void);
int	 write_pid_file(void);
int	 send_show(const struct show_req *);
int	 send_clear(void);
int	 parse_show(const char *buf, struct show_req *);
int	 icon_resolve(const char *spec, char *path, size_t pathlen);

/* countdown.c */
void	 decode_escapes(const char *in, char *out, size_t outlen);
int	 countdown_begin(const struct show_req *);
void	 countdown_tick(const struct show_req *, int digit);
void	 text_tick(const struct show_req *);
void	 countdown_end(void);
int	 run_countdown(const struct show_req *);
int	 run_text(const struct show_req *);

/* daemon.c */
extern volatile sig_atomic_t stop;
extern int	 sock;

void	 cleanup(int);
double	 now_monotonic(void);
int	 run_daemon(void);
int	 show_once(const struct show_req *);

#endif /* !BOSD_H */
