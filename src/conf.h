/*
 * bosd.conf -- defaults for optional bosd(1) flags.
 *
 * The file is name=value, the form `sysconf generic' reads and
 * writes. Missing is valid. See bosd.conf(5).
 */
#ifndef BOSD_CONF_H
#define BOSD_CONF_H

#include <bosd.h>

/* Which optional flags the command line actually passed. */
struct bosd_optset {
	int	alpha;		/* -A */
	int	outline_alpha;	/* -O */
	int	outline;	/* -o */
	int	font;		/* -f */
	int	text_color;	/* -F */
	int	gauge_color;	/* -G */
	int	scale;		/* -s */
	int	x_off;		/* -x */
	int	y_off;		/* -y */
	int	gauge_hold;	/* -B */
	int	gauge_prev;	/* -P */
	int	screen;		/* -S */
};

struct bosd_conf {
	int	screen_set;
	char	screen[BOSD_SCREEN_MAX];
	int	alpha_set;
	double	alpha;
	int	outline_alpha_set;
	double	outline_alpha;
	int	outline_set;
	int	outline;
	int	font_set;
	char	font[BOSD_FONT_MAX];
	int	text_color_set;
	char	text_color[BOSD_COLOR_MAX];
	int	gauge_color_set;
	char	gauge_color[BOSD_COLOR_MAX];
	int	scale_set;
	double	scale;
	int	x_set;
	int	x_off;
	int	y_set;
	int	y_off;
	int	gauge_hold_set;
	double	gauge_hold;
	int	hold_set;
	double	hold;
	int	gauge_prev_set;
	int	gauge_prev;
};

/* 0 on success, including a missing file. -1 after a message. */
int	bosd_conf_load(struct bosd_conf *);

/* Copy presentation defaults the command line did not set. */
void	bosd_conf_apply_show(const struct bosd_conf *, struct bosd_req *,
	    const struct bosd_optset *);

/* Copy screen when that key is set. */
void	bosd_conf_apply_screen(const struct bosd_conf *, struct bosd_req *);

#endif /* !BOSD_CONF_H */
