/*
 * bosd -- private declarations for the daemon and CLI.
 */
#ifndef BOSD_PRIV_H
#define BOSD_PRIV_H

#include <signal.h>
#include <stddef.h>

#include <X11/Xlib.h>

#include <bosd.h>

#define BOSD_MSG_MAX	1696

#ifndef BOSD_ICONDIR
#define BOSD_ICONDIR	"/usr/local/share/bosd"
#endif

/* Internal name kept for the existing call sites. */
#define show_req bosd_req

struct icon {
	struct icon	*next;
	char		 path[BOSD_SPEC_MAX];
	unsigned char	*rgba;
	double		 scale;
	int		 outline;
	int		 w, h;
};

/* Channel name shared with libbosd (bosd -n / bosd_set_instance). */
extern char	 instance[BOSD_INSTANCE_MAX];

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

/* bar.c */
Window	 shaped_window(int x, int y, int w, int h);
int	 bar_show(const struct show_req *);
void	 bar_hide(void);
void	 bar_cleanup(void);
int	 run_bar(const struct show_req *);

/* stext.c */
int	 stext_show(const struct show_req *);
void	 stext_hide(void);
void	 stext_cleanup(void);
int	 run_stext(const struct show_req *);

/* ipc.c */
extern char	 sock_name[104];
extern char	 pid_name[104];

void	 bosd_paths(const char *channel, char *sock, size_t socklen,
	    char *pidf, size_t pidlen);
void	 resolve_ipc_names(void);
int	 daemon_alive(void);
int	 daemon_alive_at(const char *sock, const char *pidf);
int	 write_pid_file(void);
int	 send_show(const struct show_req *);
int	 send_show_to(const char *sock, const struct show_req *);
int	 send_clear(void);
int	 send_clear_to(const char *sock);
int	 parse_show(const char *buf, struct show_req *);
int	 icon_resolve(const char *spec, char *path, size_t pathlen);

/* escape.c */
void	 decode_escapes(const char *in, char *out, size_t outlen);

/* countdown.c */
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

#endif /* !BOSD_PRIV_H */
