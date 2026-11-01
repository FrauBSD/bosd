/*
 * Read bosd.conf. Name=value, '#' comments, quotes only when the
 * value needs them: the bytes `sysconf generic' writes. No libbsdconf.
 */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "conf.h"

#ifndef BOSD_CONF
#define BOSD_CONF	"/usr/local/etc/bosd.conf"
#endif

/* BOSD_CONF in the environment, else the compiled path. */
static const char *
conf_path(void)
{
	const char *env;

	env = getenv("BOSD_CONF");
	if (env != NULL && env[0] != '\0')
		return (env);
	return (BOSD_CONF);
}

static int
fail(int line, const char *what)
{
	const char *path;

	path = conf_path();
	if (line > 0)
		fprintf(stderr, "bosd: %s:%d: %s\n", path, line, what);
	else
		fprintf(stderr, "bosd: %s: %s\n", path, what);
	return (-1);
}

/*
 * Collapse the escapes bsdconf_strunexpand accepts, in place. The
 * output never runs ahead of the input.
 */
static void
unescape(char *s)
{
	char *d;
	unsigned int n, v;
	unsigned char c;

	d = s;
	while (*s != '\0') {
		if (*s != '\\') {
			*d++ = *s++;
			continue;
		}
		if (s[1] == '\0') {
			*d++ = *s;
			break;
		}
		s++;
		switch (*s) {
		case 'a': *d = '\a'; break;
		case 'b': *d = '\b'; break;
		case 'f': *d = '\f'; break;
		case 'n': *d = '\n'; break;
		case 'r': *d = '\r'; break;
		case 't': *d = '\t'; break;
		case 'v': *d = '\v'; break;
		case 'x':
			v = 0;
			n = 0;
			while (n < 2) {
				c = (unsigned char)s[1];
				if (c >= '0' && c <= '9')
					v = (v << 4) + (c - '0');
				else if (c >= 'A' && c <= 'F')
					v = (v << 4) + (c - 'A' + 10);
				else if (c >= 'a' && c <= 'f')
					v = (v << 4) + (c - 'a' + 10);
				else
					break;
				s++;
				n++;
			}
			*d = (n == 0) ? 'x' : (char)v;
			break;
		default:
			if (*s >= '0' && *s <= '7') {
				v = (unsigned int)(*s - '0');
				n = 1;
				while (n < 3 && s[1] >= '0' && s[1] <= '7') {
					s++;
					v = (v << 3) +
					    (unsigned int)(*s - '0');
					n++;
				}
				*d = (char)v;
			} else
				*d = *s;
			break;
		}
		d++;
		s++;
	}
	*d = '\0';
}

/* Value after '=': quoted or bare. *sp is left on the trailer. */
static int
take_value(const char **sp, char *out, size_t outlen, int line)
{
	const char *s = *sp;
	size_t n = 0;

	if (*s == '"') {
		s++;
		while (*s != '\0') {
			if (*s == '\\' && s[1] != '\0') {
				if (n + 2 >= outlen)
					return (fail(line, "value too long"));
				out[n++] = *s++;
				out[n++] = *s++;
				continue;
			}
			if (*s == '"')
				break;
			if (n + 1 >= outlen)
				return (fail(line, "value too long"));
			out[n++] = *s++;
		}
		if (*s != '"')
			return (fail(line, "unterminated quote"));
		s++;
		out[n] = '\0';
		unescape(out);
	} else {
		const char *e = s;

		while (*e != '\0' && *e != '#')
			e++;
		while (e > s && isspace((unsigned char)e[-1]))
			e--;
		if ((size_t)(e - s) >= outlen)
			return (fail(line, "value too long"));
		memcpy(out, s, (size_t)(e - s));
		out[e - s] = '\0';
		unescape(out);
		s = e;
	}
	s += strspn(s, " \t");
	if (*s != '\0' && *s != '#')
		return (fail(line, "trailing junk after value"));
	*sp = s;
	return (0);
}

static int
num_d(const char *s, double *out)
{
	char *ep;

	errno = 0;
	*out = strtod(s, &ep);
	if (ep == s || *ep != '\0' || errno != 0)
		return (-1);
	return (0);
}

static int
num_i(const char *s, int *out)
{
	char *ep;
	long v;

	errno = 0;
	v = strtol(s, &ep, 10);
	if (ep == s || *ep != '\0' || errno != 0 ||
	    v < INT_MIN || v > INT_MAX)
		return (-1);
	*out = (int)v;
	return (0);
}

static int
parse_bool(const char *s, int *out)
{
	if (strcasecmp(s, "yes") == 0 || strcasecmp(s, "on") == 0 ||
	    strcasecmp(s, "true") == 0 || strcmp(s, "1") == 0) {
		*out = 1;
		return (0);
	}
	if (strcasecmp(s, "no") == 0 || strcasecmp(s, "off") == 0 ||
	    strcasecmp(s, "false") == 0 || strcmp(s, "0") == 0) {
		*out = 0;
		return (0);
	}
	return (-1);
}

static int
set_text(char *dst, size_t dstlen, const char *val, int line,
    const char *what)
{
	if (strlen(val) >= dstlen)
		return (fail(line, what));
	strlcpy(dst, val, dstlen);
	return (0);
}

static int
assign(struct bosd_conf *cf, const char *name, const char *val, int line)
{
	double d;
	int i;

	if (strcmp(name, "screen") == 0) {
		if (val[0] != '\0' && (strlen(val) >= sizeof(cf->screen) ||
		    val[strcspn(val, " \t")] != '\0'))
			return (fail(line,
			    "screen must be one word, at most 63 characters"));
		cf->screen_set = 1;
		strlcpy(cf->screen, val, sizeof(cf->screen));
		return (0);
	}
	if (strcmp(name, "alpha") == 0) {
		if (num_d(val, &d) != 0 ||
		    (d < 0.0 && d != BOSD_ALPHA_NATIVE) ||
		    d > BOSD_ALPHA_MAX)
			return (fail(line, "alpha must be -1 or 0 to 1"));
		cf->alpha_set = 1;
		cf->alpha = d;
		return (0);
	}
	if (strcmp(name, "outline_alpha") == 0) {
		if (num_d(val, &d) != 0 || d < BOSD_ALPHA_MIN ||
		    d > BOSD_ALPHA_MAX)
			return (fail(line,
			    "outline_alpha must be 0 to 1"));
		cf->outline_alpha_set = 1;
		cf->outline_alpha = d;
		return (0);
	}
	if (strcmp(name, "outline") == 0) {
		if (parse_bool(val, &i) != 0)
			return (fail(line, "outline must be yes or no"));
		cf->outline_set = 1;
		cf->outline = i;
		return (0);
	}
	if (strcmp(name, "font") == 0) {
		if (set_text(cf->font, sizeof(cf->font), val, line,
		    "font must be at most 127 characters") != 0)
			return (-1);
		cf->font_set = 1;
		return (0);
	}
	if (strcmp(name, "text_color") == 0) {
		if (set_text(cf->text_color, sizeof(cf->text_color), val,
		    line, "text_color must be at most 31 characters") != 0)
			return (-1);
		cf->text_color_set = 1;
		return (0);
	}
	if (strcmp(name, "gauge_color") == 0) {
		if (set_text(cf->gauge_color, sizeof(cf->gauge_color), val,
		    line, "gauge_color must be at most 31 characters") != 0)
			return (-1);
		cf->gauge_color_set = 1;
		return (0);
	}
	if (strcmp(name, "scale") == 0) {
		if (num_d(val, &d) != 0 || d < BOSD_SCALE_MIN ||
		    d > BOSD_SCALE_MAX)
			return (fail(line, "scale must be 0.1 to 8"));
		cf->scale_set = 1;
		cf->scale = d;
		return (0);
	}
	if (strcmp(name, "x") == 0) {
		if (num_i(val, &i) != 0)
			return (fail(line, "x must be an integer"));
		cf->x_set = 1;
		cf->x_off = i;
		return (0);
	}
	if (strcmp(name, "y") == 0) {
		if (num_i(val, &i) != 0)
			return (fail(line, "y must be an integer"));
		cf->y_set = 1;
		cf->y_off = i;
		return (0);
	}
	if (strcmp(name, "hold") == 0) {
		if (num_d(val, &d) != 0 ||
		    (d != -1.0 && (d < BOSD_HOLD_MIN || d > BOSD_HOLD_MAX)))
			return (fail(line, "hold must be -1 or 0.01 to 30"));
		cf->hold_set = 1;
		cf->hold = d;
		return (0);
	}
	if (strcmp(name, "gauge_hold") == 0) {
		if (num_d(val, &d) != 0 ||
		    (d != -1.0 && (d < BOSD_HOLD_MIN || d > BOSD_HOLD_MAX)))
			return (fail(line,
			    "gauge_hold must be -1 or 0.01 to 30"));
		cf->gauge_hold_set = 1;
		cf->gauge_hold = d;
		return (0);
	}
	if (strcmp(name, "gauge_prev") == 0) {
		if (num_i(val, &i) != 0 || i < -1 || i > 9999)
			return (fail(line,
			    "gauge_prev must be -1 or 0 to 9999"));
		cf->gauge_prev_set = 1;
		cf->gauge_prev = i;
		return (0);
	}
	return (fail(line, "unknown setting"));
}

static int
parse_line(char *line, int lineno, struct bosd_conf *cf)
{
	char *name, *p;
	char val[1024];
	const char *vp;

	p = line + strspn(line, " \t");
	if (*p == '\0' || *p == '#')
		return (0);
	name = p;
	while (*p != '\0' && !isspace((unsigned char)*p) && *p != '=') {
		if (!isalnum((unsigned char)*p) && *p != '_')
			return (fail(lineno, "expected name=value"));
		p++;
	}
	if (p == name)
		return (fail(lineno, "expected name=value"));
	{
		char *end = p;

		if (*p != '=') {
			p += strspn(p, " \t");
			if (*p != '=')
				return (fail(lineno, "expected '='"));
		}
		*end = '\0';
		p++;
	}
	p += strspn(p, " \t");
	vp = p;
	if (take_value(&vp, val, sizeof(val), lineno) != 0)
		return (-1);
	return (assign(cf, name, val, lineno));
}

int
bosd_conf_load(struct bosd_conf *cf)
{
	FILE *fp;
	const char *path;
	char buf[2048];
	int line = 0;

	memset(cf, 0, sizeof(*cf));
	path = conf_path();
	fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT)
			return (0);
		fprintf(stderr, "bosd: %s: %s\n", path, strerror(errno));
		return (-1);
	}
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		size_t n;

		line++;
		n = strlen(buf);
		if (n > 0 && buf[n - 1] == '\n')
			buf[n - 1] = '\0';
		else if (n == sizeof(buf) - 1) {
			int c = fgetc(fp);

			if (c != EOF) {
				fclose(fp);
				return (fail(line, "line too long"));
			}
		}
		if (parse_line(buf, line, cf) != 0) {
			fclose(fp);
			return (-1);
		}
	}
	if (ferror(fp)) {
		fprintf(stderr, "bosd: %s: %s\n", path, strerror(errno));
		fclose(fp);
		return (-1);
	}
	fclose(fp);
	return (0);
}

void
bosd_conf_apply_show(const struct bosd_conf *cf, struct bosd_req *req,
    const struct bosd_optset *set)
{
	if (!set->alpha && cf->alpha_set)
		req->alpha = cf->alpha;
	if (!set->outline_alpha && cf->outline_alpha_set)
		req->outline_alpha = cf->outline_alpha;
	if (!set->outline && cf->outline_set)
		req->outline = cf->outline;
	if (!set->font && cf->font_set)
		strlcpy(req->font, cf->font, sizeof(req->font));
	if (!set->text_color && cf->text_color_set)
		strlcpy(req->tcolor, cf->text_color, sizeof(req->tcolor));
	if (!set->gauge_color && cf->gauge_color_set)
		strlcpy(req->color, cf->gauge_color, sizeof(req->color));
	if (!set->scale && cf->scale_set)
		req->scale = cf->scale;
	if (!set->x_off && cf->x_set)
		req->x_off = cf->x_off;
	if (!set->y_off && cf->y_set)
		req->y_off = cf->y_off;
	if (!set->gauge_hold && cf->gauge_hold_set)
		req->gauge_hold = cf->gauge_hold;
	/*
	 * An explicit hold_seconds operand is parsed later and
	 * replaces this. -c then sets its own per-digit default.
	 */
	if (cf->hold_set)
		req->hold = cf->hold;
	if (!set->gauge_prev && cf->gauge_prev_set)
		req->gauge_prev = cf->gauge_prev;
}

void
bosd_conf_apply_screen(const struct bosd_conf *cf, struct bosd_req *req)
{
	if (!cf->screen_set)
		return;
	strlcpy(req->screen, cf->screen, sizeof(req->screen));
}
