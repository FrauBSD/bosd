/*
 * bosd -- public client API.
 *
 * Link with -lbosd.  A warm bosd(1) daemon per channel owns the X11
 * overlay; these calls hand it a datagram and return.  No X11 is
 * pulled into the caller.
 */
#ifndef BOSD_H
#define BOSD_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOSD_VERSION	"4.1"

#define BOSD_SPEC_MAX	1024
#define BOSD_BADGE_MAX	32
#define BOSD_CAPTION_MAX 64
#define BOSD_COLOR_MAX	32
#define BOSD_INSTANCE_MAX 64

#define BOSD_GAUGE_DEF	"#2AC12A"
#define BOSD_GAUGE_HOLD_DEF 3.0
#define BOSD_STEXT_DEF	"green"
#define BOSD_HOLD_DEF	2.0
#define BOSD_HOLD_MIN	0.01
#define BOSD_HOLD_MAX	30.0
#define BOSD_SCALE_MIN	0.1
#define BOSD_SCALE_MAX	8.0

/*
 * One display request.  Zero the struct (or call bosd_req_init) then
 * fill the fields that apply.  String fields are NUL-terminated and
 * may use the same escapes as bosd(1): \xNN, \uNNNN, \UNNNNNNNN, \\.
 */
struct bosd_req {
	double	 hold;		/* seconds; -1 until clear/replace */
	double	 scale;		/* multiplies panel-derived glyph size */
	int	 outline;	/* 1 = black halo on the glyph */
	int	 count;		/* >0: countdown digits */
	int	 text;		/* 1: large outlined text in spec */
	int	 small;		/* 1: small caption text in spec */
	int	 clear;		/* internal: set by the daemon parser */
	int	 gauge;		/* >=0: bar percent; -1 = no bar */
	double	 gauge_hold;	/* bar's own hold */
	char	 color[BOSD_COLOR_MAX];	/* bar fill */
	char	 tcolor[BOSD_COLOR_MAX];	/* small-text color */
	int	 x_off;		/* pixels; positive right */
	int	 y_off;		/* pixels; positive down */
	char	 spec[BOSD_SPEC_MAX];	/* icon path/name or text */
	char	 badge[BOSD_BADGE_MAX];
	char	 prefix[BOSD_CAPTION_MAX];
	char	 append[BOSD_CAPTION_MAX];
	int	 gauge_prev;	/* >=0: prior percent watermark; -1 off */
};

/* Fill defaults: hold 2s, scale 1, outline on, gauge -1,
 * gauge_prev -1, gauge_hold 3s. */
void		 bosd_req_init(struct bosd_req *);

/* Channel name for the CLI's global instance (bosd -n); default "default". */
int		 bosd_set_instance(const char *name);
const char	*bosd_get_instance(void);

/*
 * channel NULL or "" means "default".  Returns 1 if a daemon owns the
 * channel, 0 otherwise.
 */
int		 bosd_alive(const char *channel);

/*
 * Hand the request to a warm daemon.  Returns 0 on success, -1 if the
 * send fails (no daemon, full socket, ...).  Does not fall back to a
 * local paint -- start bosd -d for the channel first.
 */
int		 bosd_show(const char *channel, const struct bosd_req *);

/* Hide artwork and bar on the channel.  Same return convention. */
int		 bosd_clear(const char *channel);

const char	*bosd_version(void);

#ifdef __cplusplus
}
#endif

#endif /* !BOSD_H */
