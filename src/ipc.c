/*
 * Socket naming, liveness, the show protocol, and icon-spec resolution.
 *
 * Protocol: one datagram per show, "HOLD XOFF YOFF SCALE OUTL CNT
 * TCOLOR PFX APX SPEC [BADGE]" -- HOLD in seconds (-1 holds until
 * replaced or cleared), XOFF/YOFF signed shifts in pixels
 * (positive right/down), SCALE a multiplier on the panel-derived
 * size, OUTL 1 to halo the glyph and 0 not to, CNT > 0 a countdown
 * (SPEC then a placeholder), -1 large text and -2 small text (SPEC
 * then the text, escapes still encoded), TCOLOR the small-text
 * color ("-" for the default green), PFX/APX captions above/below
 * ("-" when absent, whitespace escaped in flight), SPEC otherwise
 * an absolute path or a bare name resolved against BOSD_PATH / the
 * compiled share directory (".png" appended when missing).  A bare
 * "CLEAR" hides the active render(s).  A gauge travels separately
 * as "BAR HOLD XOFF YOFF PCT COLOR" and coexists with the artwork.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include "bosd.h"

char sock_name[104];
char pid_name[104];

void
resolve_ipc_names(void)
{
	uid_t u = getuid();

	(void)snprintf(sock_name, sizeof(sock_name),
	    "/tmp/bosd.%s.%u.sock", instance, (unsigned)u);
	(void)snprintf(pid_name, sizeof(pid_name),
	    "/tmp/bosd.%s.%u.pid", instance, (unsigned)u);
}

int
daemon_alive(void)
{
	FILE *f;
	pid_t pid;

	if (access(sock_name, F_OK) != 0)
		return (0);
	f = fopen(pid_name, "r");
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

static int
send_dgram(const char *msg)
{
	struct sockaddr_un addr;
	int fd;
	ssize_t n;

	fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0)
		return (-1);

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strlcpy(addr.sun_path, sock_name, sizeof(addr.sun_path));
	n = sendto(fd, msg, strlen(msg), MSG_DONTWAIT,
	    (struct sockaddr *)&addr, sizeof(addr));
	close(fd);
	return (n < 0 ? -1 : 0);
}

/* Escape whitespace so a caption travels as one datagram token. */
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
send_show(const struct show_req *req)
{
	char msg[BOSD_MSG_MAX];
	char pfx[BOSD_CAPTION_MAX * 4], apx[BOSD_CAPTION_MAX * 4];

	if (req->gauge >= 0) {
		snprintf(msg, sizeof(msg), "BAR %.2f %d %d %d %s",
		    req->gauge_hold, req->x_off, req->y_off, req->gauge,
		    req->color[0] != '\0' ? req->color : BOSD_GAUGE_DEF);
		if (send_dgram(msg) != 0)
			return (-1);
		/* Gauge alone, or artwork too? */
		if (req->spec[0] == '\0' && req->count <= 0 &&
		    !req->text && !req->small)
			return (0);
	}
	encode_ws(req->prefix, pfx, sizeof(pfx));
	encode_ws(req->append, apx, sizeof(apx));
	snprintf(msg, sizeof(msg), "%.2f %d %d %.3f %d %d %s %s %s %s%s%s",
	    req->hold, req->x_off, req->y_off, req->scale, req->outline,
	    req->text ? -1 : req->small ? -2 : req->count,
	    req->tcolor[0] != '\0' ? req->tcolor : "-", pfx, apx,
	    req->spec[0] != '\0' ? req->spec : "-",
	    req->badge[0] != '\0' ? " " : "", req->badge);
	return (send_dgram(msg));
}

int
send_clear(void)
{
	return (send_dgram("CLEAR"));
}

int
parse_show(const char *buf, struct show_req *req)
{
	double hold, scale;
	char name[BOSD_SPEC_MAX];
	char badge[BOSD_BADGE_MAX];
	char tcolor[BOSD_COLOR_MAX];
	char pfx[BOSD_CAPTION_MAX * 4], apx[BOSD_CAPTION_MAX * 4];
	int n, x_off, y_off, outline, count;

	if (strcmp(buf, "CLEAR") == 0) {
		memset(req, 0, sizeof(*req));
		req->clear = 1;
		req->gauge = -1;
		return (0);
	}
	if (strncmp(buf, "BAR ", 4) == 0) {
		memset(req, 0, sizeof(*req));
		n = sscanf(buf + 4, "%lf %d %d %d %31s", &hold, &x_off,
		    &y_off, &req->gauge, req->color);
		if (n < 5 || req->gauge < 0)
			return (-1);
		if (hold != -1.0 && hold <= 0.0)
			hold = BOSD_GAUGE_HOLD_DEF;
		req->hold = hold;
		req->gauge_hold = hold;
		req->x_off = x_off;
		req->y_off = y_off;
		return (0);
	}

	badge[0] = '\0';
	/* Field widths track BOSD_SPEC/BADGE/COLOR/CAPTION_MAX (x4). */
	n = sscanf(buf, "%lf %d %d %lf %d %d %31s %255s %255s %1023s %31s",
	    &hold, &x_off, &y_off, &scale, &outline, &count, tcolor,
	    pfx, apx, name, badge);
	if (n < 10)
		return (-1);
	if (hold != -1.0 && hold <= 0.0)
		hold = BOSD_HOLD_DEF;
	if (count > 0 && hold < 0.0)	/* a countdown must advance */
		hold = 1.0;
	if (scale <= 0.0)
		scale = 1.0;
	req->hold = hold;
	req->scale = scale;
	req->outline = outline != 0;
	req->count = count > 0 ? count : 0;
	req->text = count == -1;
	req->small = count == -2;
	req->clear = 0;
	req->gauge = -1;
	req->x_off = x_off;
	req->y_off = y_off;
	if (strcmp(tcolor, "-") == 0)
		req->tcolor[0] = '\0';
	else
		strlcpy(req->tcolor, tcolor, sizeof(req->tcolor));
	strlcpy(req->spec, name, sizeof(req->spec));
	/* Display text: decode escapes on arrival; "-" means absent. */
	decode_escapes(badge, req->badge, sizeof(req->badge));
	if (strcmp(pfx, "-") == 0)
		req->prefix[0] = '\0';
	else
		decode_escapes(pfx, req->prefix, sizeof(req->prefix));
	if (strcmp(apx, "-") == 0)
		req->append[0] = '\0';
	else
		decode_escapes(apx, req->append, sizeof(req->append));
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
