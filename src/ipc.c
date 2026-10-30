/*
 * Socket naming, liveness, the show protocol, and icon-spec resolution.
 *
 * Protocol: one datagram per show, "HOLD XOFF YOFF SCALE ALPHA OALPHA
 * OUTL CNT TCOLOR FONT PFX APX SPEC [BADGE]" -- HOLD in seconds (-1 holds until
 * replaced or cleared), XOFF/YOFF signed shifts in pixels
 * (positive right/down), SCALE a multiplier on the panel-derived
 * size, ALPHA < 0 leaves PNG alpha alone and otherwise multiplies the
 * glyph's alpha (0..1) after the outline halo is drawn from native
 * coverage so the halo stays opaque even at ALPHA 0,
 * OALPHA multiplies the outline halo's alpha (0..1, default 1),
 * OUTL 1 to halo the glyph and 0 not to, CNT > 0 a countdown
 * (SPEC then a placeholder), -1 large text and -2 small text (SPEC
 * then the text).  SPEC text, BADGE, FONT, and PFX/APX accept
 * literal whitespace and bosd(1) escapes; on the wire each is one
 * token (escapes expanded, then whitespace and backslash escaped)
 * and parse_show restores the display text.  TCOLOR is the
 * text/badge fill color ("-" for the role default), FONT a
 * fontconfig family ("-" for the built-in default), PFX/APX
 * captions above/below ("-" when absent).  SPEC otherwise
 * an absolute path or a bare name resolved against BOSD_PATH / the
 * compiled share directory (".png" appended when missing).  A bare
 * "CLEAR" hides the active render(s).  A show with clear set sends
 * CLEAR first so artwork, gauge, and caption all drop before the
 * new paint.  A gauge travels separately as "BAR HOLD XOFF YOFF PCT
 * COLOR PREV ALPHA OALPHA OUTL [SCALE TCOLOR FONT PFX APX]" and
 * coexists with the artwork and with small text.  Trailing fields are
 * optional for older clients; SCALE defaults to 1, captions/font/color
 * absent when omitted.  When the client also sends artwork (or -c/-T/
 * -t) in the same show, BAR carries "-" for TCOLOR/FONT/PFX/APX so
 * those adornments stay on the artwork SHOW (bar_show only labels a
 * gauge-alone request).  Small text (CNT -2) is its own slot: only
 * another caption or CLEAR replaces it.  A datagram may be
 * prefixed with "SCR <screen>" when the client passed -S: one
 * output name, or "all".  Absent, the daemon uses the screen it
 * was started with (-S), or the primary.  PREV is a prior-percent
 * watermark (-1 disables); the daemon latches the first PREV when the
 * bar appears and ignores later ones until the bar hides.  ALPHA < 0
 * means full fill opacity on the bar (native PNG does not apply);
 * otherwise 0..1 fill opacity.  OALPHA is the black outline opacity
 * (0..1, default 1).  OUTL 1 draws the tick outline and 0 skips it
 * (-o).
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include "priv.h"

char sock_name[104];
char pid_name[104];

void
bosd_paths(const char *channel, char *sock, size_t socklen, char *pidf,
    size_t pidlen)
{
	uid_t u = getuid();

	if (channel == NULL || channel[0] == '\0')
		channel = "default";
	(void)snprintf(sock, socklen, "/tmp/bosd.%s.%u.sock", channel,
	    (unsigned)u);
	(void)snprintf(pidf, pidlen, "/tmp/bosd.%s.%u.pid", channel,
	    (unsigned)u);
}

void
resolve_ipc_names(void)
{
	bosd_paths(instance, sock_name, sizeof(sock_name), pid_name,
	    sizeof(pid_name));
}

int
daemon_alive_at(const char *sock, const char *pidf)
{
	FILE *f;
	pid_t pid;

	if (access(sock, F_OK) != 0)
		return (0);
	f = fopen(pidf, "r");
	if (f == NULL)
		return (0);
	if (fscanf(f, "%d", (int *)&pid) != 1) {
		fclose(f);
		return (0);
	}
	fclose(f);
	return (kill(pid, 0) == 0);
}

int
daemon_alive(void)
{
	return (daemon_alive_at(sock_name, pid_name));
}

int
write_pid_file(void)
{
	FILE *f;

	f = fopen(pid_name, "w");
	if (f == NULL)
		return (-1);
	fprintf(f, "%d\n", (int)getpid());
	fclose(f);
	return (0);
}

/* Body starts after an optional "SCR <screen> " prefix. */
static char *
msg_body(char *msg, size_t msgsz, const char *screen, size_t *left)
{
	size_t off = 0;

	if (screen != NULL && screen[0] != '\0') {
		int n = snprintf(msg, msgsz, "SCR %s ", screen);

		if (n < 0 || (size_t)n >= msgsz)
			return (NULL);
		off = (size_t)n;
	}
	*left = msgsz - off;
	return (msg + off);
}

static int
send_dgram(const char *sock, const char *msg)
{
	struct sockaddr_un addr;
	int fd;
	ssize_t n;

	fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0)
		return (-1);

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strlcpy(addr.sun_path, sock, sizeof(addr.sun_path));
	n = sendto(fd, msg, strlen(msg), MSG_DONTWAIT,
	    (struct sockaddr *)&addr, sizeof(addr));
	close(fd);
	return (n < 0 ? -1 : 0);
}

/*
 * Escape whitespace so a field travels as one datagram token.
 * Leaves backslash escapes alone so a single decode_escapes on the
 * far side restores both literal spaces and caller \xNN / \u / \U.
 */
static void
encode_ws(const char *in, char *out, size_t outlen)
{
	size_t o = 0;

	if (*in == '\0')
		in = "-";	/* absent-field placeholder */
	while (*in != '\0' && o + 5 < outlen) {
		if (*in == ' ' || *in == '\t' || *in == '\n') {
			snprintf(out + o, outlen - o, "\\x%02x",
			    (unsigned char)*in);
			o += 4;
			in++;
		} else
			out[o++] = *in++;
	}
	out[o] = '\0';
}

int
send_show_to(const char *sock, const struct show_req *req)
{
	char pfx[BOSD_CAPTION_MAX * 4], apx[BOSD_CAPTION_MAX * 4];
	char font[BOSD_FONT_MAX * 4];

	/*
	 * Drop every slot first when asked, so a follow-up gauge,
	 * glyph, or caption does not share the panel with a leftover
	 * from another slot (bosd -C -g …, bosd -C -t …, …).
	 */
	if (req->clear && send_dgram(sock, "CLEAR") != 0)
		return (-1);

	if (req->gauge >= 0) {
		int bar_alone;

		/*
		 * BAR is its own datagram, so after parse the gauge
		 * looks alone.  -p/-a/-f/-F belong on the artwork
		 * SHOW when co-displaying; only a gauge-alone BAR
		 * carries label fields (matches bar_show()).
		 */
		bar_alone = req->spec[0] == '\0' && req->count <= 0 &&
		    !req->text && !req->small;
		if (bar_alone) {
			encode_ws(req->prefix, pfx, sizeof(pfx));
			encode_ws(req->append, apx, sizeof(apx));
			encode_ws(req->font, font, sizeof(font));
		} else {
			strlcpy(pfx, "-", sizeof(pfx));
			strlcpy(apx, "-", sizeof(apx));
			strlcpy(font, "-", sizeof(font));
		}
		{
			char msg[BOSD_MSG_MAX];
			char *bp;
			size_t left;
			int nw;

			bp = msg_body(msg, sizeof(msg), req->screen, &left);
			if (bp == NULL)
				return (-1);
			nw = snprintf(bp, left,
			    "BAR %.2f %d %d %d %s %d %.3f %.3f %d %.3f %s %s %s %s",
			    req->gauge_hold, req->x_off, req->y_off, req->gauge,
			    req->color[0] != '\0' ? req->color : BOSD_GAUGE_DEF,
			    req->gauge_prev, req->alpha, req->outline_alpha,
			    req->outline, req->scale > 0.0 ? req->scale : 1.0,
			    bar_alone && req->tcolor[0] != '\0' ?
			    req->tcolor : "-",
			    bar_alone && req->font[0] != '\0' ? font : "-",
			    pfx, apx);
			if (nw < 0 || (size_t)nw >= left)
				return (-1);
			if (send_dgram(sock, msg) != 0)
				return (-1);
		}
		if (bar_alone)
			return (0);
	}
	{
		char msg[BOSD_MSG_MAX];
		char spec[BOSD_SPEC_MAX * 4];
		char badge[BOSD_BADGE_MAX * 4];
		char *bp;
		size_t left;
		int nw;

		encode_ws(req->prefix, pfx, sizeof(pfx));
		encode_ws(req->append, apx, sizeof(apx));
		encode_ws(req->font, font, sizeof(font));
		encode_ws(req->spec, spec, sizeof(spec));
		if (req->badge[0] != '\0')
			encode_ws(req->badge, badge, sizeof(badge));
		else
			badge[0] = '\0';
		bp = msg_body(msg, sizeof(msg), req->screen, &left);
		if (bp == NULL)
			return (-1);
		nw = snprintf(bp, left,
		    "%.2f %d %d %.3f %.3f %.3f %d %d %s %s %s %s %s%s%s",
		    req->hold, req->x_off, req->y_off, req->scale, req->alpha,
		    req->outline_alpha, req->outline,
		    req->text ? -1 : req->small ? -2 : req->count,
		    req->tcolor[0] != '\0' ? req->tcolor : "-",
		    req->font[0] != '\0' ? font : "-",
		    pfx, apx, spec,
		    badge[0] != '\0' ? " " : "", badge);
		if (nw < 0 || (size_t)nw >= left)
			return (-1);
		return (send_dgram(sock, msg));
	}
}

int
send_show(const struct show_req *req)
{
	return (send_show_to(sock_name, req));
}

int
send_clear_to(const char *sock)
{
	return (send_dgram(sock, "CLEAR"));
}

int
send_clear(void)
{
	return (send_clear_to(sock_name));
}

int
parse_show(const char *buf, struct show_req *req)
{
	const char *msg = buf;
	double hold, scale, alpha, oalpha;
	char name[BOSD_SPEC_MAX * 4];
	char badge[BOSD_BADGE_MAX * 4];
	char tcolor[BOSD_COLOR_MAX];
	char font[BOSD_FONT_MAX * 4];
	char pfx[BOSD_CAPTION_MAX * 4], apx[BOSD_CAPTION_MAX * 4];
	char screen[BOSD_SCREEN_MAX];
	int n, x_off, y_off, outline, count;

	screen[0] = '\0';
	if (strncmp(msg, "SCR ", 4) == 0) {
		const char *sp;
		size_t nlen;

		msg += 4;
		sp = strchr(msg, ' ');
		if (sp == NULL)
			return (-1);
		nlen = (size_t)(sp - msg);
		if (nlen == 0 || nlen >= sizeof(screen))
			return (-1);
		memcpy(screen, msg, nlen);
		screen[nlen] = '\0';
		msg = sp + 1;
	}

	if (strcmp(msg, "CLEAR") == 0) {
		memset(req, 0, sizeof(*req));
		req->clear = 1;
		req->gauge = -1;
		req->gauge_prev = -1;
		req->alpha = BOSD_ALPHA_NATIVE;
		req->outline_alpha = BOSD_OUTLINE_ALPHA_DEF;
		return (0);
	}
	if (strncmp(msg, "BAR ", 4) == 0) {
		double alpha, oalpha, scale;
		char tcolor[BOSD_COLOR_MAX];
		char font[BOSD_FONT_MAX * 4];
		char pfx[BOSD_CAPTION_MAX * 4], apx[BOSD_CAPTION_MAX * 4];
		int outline;

		memset(req, 0, sizeof(*req));
		req->gauge_prev = -1;
		req->scale = 1.0;
		alpha = BOSD_ALPHA_NATIVE;
		oalpha = BOSD_OUTLINE_ALPHA_DEF;
		outline = 1;
		scale = 1.0;
		tcolor[0] = '\0';
		font[0] = '\0';
		pfx[0] = '\0';
		apx[0] = '\0';
		n = sscanf(msg + 4,
		    "%lf %d %d %d %31s %d %lf %lf %d %lf %31s %511s %255s %255s",
		    &hold, &x_off, &y_off, &req->gauge, req->color,
		    &req->gauge_prev, &alpha, &oalpha, &outline, &scale,
		    tcolor, font, pfx, apx);
		if (n < 5 || req->gauge < 0)
			return (-1);
		if (n < 6)
			req->gauge_prev = -1;
		if (n < 7)
			alpha = BOSD_ALPHA_NATIVE;
		if (n < 8)
			oalpha = BOSD_OUTLINE_ALPHA_DEF;
		if (n < 9)
			outline = 1;
		if (n < 10 || scale <= 0.0)
			scale = 1.0;
		if (hold != -1.0 && hold <= 0.0)
			hold = BOSD_GAUGE_HOLD_DEF;
		if (alpha >= 0.0) {
			if (alpha < BOSD_ALPHA_MIN)
				alpha = BOSD_ALPHA_MIN;
			if (alpha > BOSD_ALPHA_MAX)
				alpha = BOSD_ALPHA_MAX;
		} else
			alpha = BOSD_ALPHA_NATIVE;
		if (oalpha < BOSD_ALPHA_MIN)
			oalpha = BOSD_ALPHA_MIN;
		if (oalpha > BOSD_ALPHA_MAX)
			oalpha = BOSD_ALPHA_MAX;
		req->hold = hold;
		req->gauge_hold = hold;
		req->x_off = x_off;
		req->y_off = y_off;
		req->scale = scale;
		req->alpha = alpha;
		req->outline_alpha = oalpha;
		req->outline = outline != 0;
		if (n >= 11 && strcmp(tcolor, "-") != 0)
			strlcpy(req->tcolor, tcolor, sizeof(req->tcolor));
		if (n >= 12 && strcmp(font, "-") != 0)
			decode_escapes(font, req->font, sizeof(req->font));
		if (n >= 13 && strcmp(pfx, "-") != 0)
			decode_escapes(pfx, req->prefix, sizeof(req->prefix));
		if (n >= 14 && strcmp(apx, "-") != 0)
			decode_escapes(apx, req->append, sizeof(req->append));
		strlcpy(req->screen, screen, sizeof(req->screen));
		return (0);
	}

	badge[0] = '\0';
	font[0] = '\0';
	alpha = BOSD_ALPHA_NATIVE;
	oalpha = BOSD_OUTLINE_ALPHA_DEF;
	/* Field widths track BOSD_*_MAX after encode_ws (x4). */
	n = sscanf(msg,
	    "%lf %d %d %lf %lf %lf %d %d %31s %511s %255s %255s %4095s %127s",
	    &hold, &x_off, &y_off, &scale, &alpha, &oalpha, &outline,
	    &count, tcolor, font, pfx, apx, name, badge);
	if (n < 13)
		return (-1);
	if (hold != -1.0 && hold <= 0.0)
		hold = BOSD_HOLD_DEF;
	if (count > 0 && hold < 0.0)	/* a countdown must advance */
		hold = 1.0;
	if (scale <= 0.0)
		scale = 1.0;
	if (alpha >= 0.0) {
		if (alpha < BOSD_ALPHA_MIN)
			alpha = BOSD_ALPHA_MIN;
		if (alpha > BOSD_ALPHA_MAX)
			alpha = BOSD_ALPHA_MAX;
	} else
		alpha = BOSD_ALPHA_NATIVE;
	if (oalpha < BOSD_ALPHA_MIN)
		oalpha = BOSD_ALPHA_MIN;
	if (oalpha > BOSD_ALPHA_MAX)
		oalpha = BOSD_ALPHA_MAX;
	req->hold = hold;
	req->scale = scale;
	req->alpha = alpha;
	req->outline_alpha = oalpha;
	req->outline = outline != 0;
	req->count = count > 0 ? count : 0;
	req->text = count == -1;
	req->small = count == -2;
	req->clear = 0;
	req->gauge = -1;
	req->gauge_prev = -1;
	req->x_off = x_off;
	req->y_off = y_off;
	if (strcmp(tcolor, "-") == 0)
		req->tcolor[0] = '\0';
	else
		strlcpy(req->tcolor, tcolor, sizeof(req->tcolor));
	if (strcmp(font, "-") == 0)
		req->font[0] = '\0';
	else
		decode_escapes(font, req->font, sizeof(req->font));
	/* SPEC/BADGE: decode on arrival (wire may carry \xNN for spaces). */
	if (strcmp(name, "-") == 0)
		req->spec[0] = '\0';
	else
		decode_escapes(name, req->spec, sizeof(req->spec));
	decode_escapes(badge, req->badge, sizeof(req->badge));
	if (strcmp(pfx, "-") == 0)
		req->prefix[0] = '\0';
	else
		decode_escapes(pfx, req->prefix, sizeof(req->prefix));
	if (strcmp(apx, "-") == 0)
		req->append[0] = '\0';
	else
		decode_escapes(apx, req->append, sizeof(req->append));
	strlcpy(req->screen, screen, sizeof(req->screen));
	return (0);
}

static int
icon_file_ok(const char *path)
{
	struct stat st;

	return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

static int
try_dir(const char *dir, size_t dirlen, const char *name, const char *sfx,
    char *path, size_t pathlen)
{
	if (dirlen == 0)
		return (0);
	if (snprintf(path, pathlen, "%.*s/%s%s", (int)dirlen, dir, name,
	    sfx) >= (int)pathlen)
		return (0);
	return (icon_file_ok(path));
}

int
icon_resolve(const char *spec, char *path, size_t pathlen)
{
	const char *search, *p, *sep;
	const char *sfx;
	size_t len;

	if (spec[0] == '\0')
		return (-1);

	len = strlen(spec);
	sfx = (len > 4 && strcmp(spec + len - 4, ".png") == 0) ? "" : ".png";

	if (strchr(spec, '/') != NULL) {
		if (snprintf(path, pathlen, "%s%s", spec, sfx) >=
		    (int)pathlen)
			return (-1);
		return (icon_file_ok(path) ? 0 : -1);
	}

	search = getenv("BOSD_PATH");
	if (search != NULL) {
		for (p = search; *p != '\0'; p = (*sep == '\0') ? sep :
		    sep + 1) {
			sep = strchr(p, ':');
			if (sep == NULL)
				sep = p + strlen(p);
			if (try_dir(p, (size_t)(sep - p), spec, sfx, path,
			    pathlen))
				return (0);
		}
	}
	if (try_dir(BOSD_ICONDIR, strlen(BOSD_ICONDIR), spec, sfx, path,
	    pathlen))
		return (0);
	return (-1);
}
