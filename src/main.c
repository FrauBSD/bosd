/*
 * bosd -- on-screen display engine.  See bosd(1).
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "priv.h"

static void
usage(void)
{
	fprintf(stderr,
	    "Usage: bosd [-hv] [-n instance] { -d | -C }\n"
	    "       bosd [-CDhov] [-n instance] [-A opacity] [-a text] "
	    "[-B seconds] \\\n"
	    "            [-b badge] [-F color] [-f font] [-G color] "
	    "[-g percent] \\\n"
	    "            [-O opacity] [-P percent] [-p text] [-s scale] "
	    "[-x offset] \\\n"
	    "            [-y offset] { icon | -c countdown | -T text } "
	    "[hold_seconds]\n"
	    "       bosd [-CDhv] [-n instance] [-B seconds] [-F color] "
	    "[-f font] \\\n"
	    "            [-G color] [-g percent] [-P percent] [-s scale] "
	    "[-x offset] \\\n"
	    "            [-y offset] -t text [hold_seconds]\n"
	    "       bosd [-CDhv] [-n instance] [-B seconds] [-G color] "
	    "[-P percent] \\\n"
	    "            [-x offset] [-y offset] -g percent\n");
	exit(1);
}

/*
 * When painting locally after -C, drop whatever the channel daemon
 * still shows so the panel is empty before this process draws.
 */
static void
clear_channel_if_asked(const struct show_req *req)
{
	if (req->clear && daemon_alive())
		(void)send_clear();
}

/* Interpret escapes in a display-text field before local render. */
static void
decode_field(char *s, size_t size)
{
	char tmp[BOSD_SPEC_MAX];

	decode_escapes(s, tmp, sizeof(tmp));
	strlcpy(s, tmp, size);
}

/* hold_seconds semantics: -1 indefinite, else clamped. */
static double
parse_hold(const char *s)
{
	char *ep;
	double hold;

	hold = strtod(s, &ep);
	if (ep == s || *ep != '\0')
		usage();
	if (hold != -1.0) {
		if (hold < BOSD_HOLD_MIN)
			hold = BOSD_HOLD_MIN;
		if (hold > BOSD_HOLD_MAX)
			hold = BOSD_HOLD_MAX;
	}
	return (hold);
}

int
main(int argc, char **argv)
{
	struct show_req req;
	int ch, count = 0, Bflag = 0, Cflag = 0, Dflag = 0, Tflag = 0;
	int dflag = 0, tflag = 0, Pflag = 0;

	memset(&req, 0, sizeof(req));
	req.hold = BOSD_HOLD_DEF;
	req.scale = 1.0;
	req.alpha = BOSD_ALPHA_NATIVE;
	req.outline_alpha = BOSD_OUTLINE_ALPHA_DEF;
	req.outline = 1;
	req.gauge = -1;
	req.gauge_prev = -1;
	req.gauge_hold = BOSD_GAUGE_HOLD_DEF;

	while ((ch = getopt(argc, argv,
	    "A:B:CDF:G:O:P:Ta:b:c:df:g:hn:op:s:tvx:y:")) != -1) {
		switch (ch) {
		case 'A': {
			char *ep;

			errno = 0;
			req.alpha = strtod(optarg, &ep);
			if (ep == optarg || *ep != '\0' || errno != 0 ||
			    req.alpha < BOSD_ALPHA_MIN ||
			    req.alpha > BOSD_ALPHA_MAX) {
				fprintf(stderr, "bosd: -A opacity must "
				    "be %.1f to %.1f\n",
				    BOSD_ALPHA_MIN, BOSD_ALPHA_MAX);
				usage();
			}
			break;
		}
		case 'B':
			Bflag = 1;
			req.gauge_hold = parse_hold(optarg);
			break;
		case 'O': {
			char *ep;

			errno = 0;
			req.outline_alpha = strtod(optarg, &ep);
			if (ep == optarg || *ep != '\0' || errno != 0 ||
			    req.outline_alpha < BOSD_ALPHA_MIN ||
			    req.outline_alpha > BOSD_ALPHA_MAX) {
				fprintf(stderr, "bosd: -O opacity must "
				    "be %.1f to %.1f\n",
				    BOSD_ALPHA_MIN, BOSD_ALPHA_MAX);
				usage();
			}
			break;
		}
		case 'C':
			Cflag = 1;
			break;
		case 'P': {
			char *ep;
			long v;

			errno = 0;
			v = strtol(optarg, &ep, 10);
			if (ep == optarg || *ep != '\0' || errno != 0 ||
			    v < -1 || v > 9999) {
				fprintf(stderr, "bosd: -P percent must "
				    "be -1 or 0 to 9999\n");
				usage();
			}
			Pflag = 1;
			req.gauge_prev = (int)v;
			break;
		}
		case 'a':
			if (strlen(optarg) >= sizeof(req.append)) {
				fprintf(stderr, "bosd: -a text must be "
				    "at most %zu characters\n",
				    sizeof(req.append) - 1);
				usage();
			}
			strlcpy(req.append, optarg, sizeof(req.append));
			break;
		case 'D':
			Dflag = 1;	/* render directly, skip daemon */
			break;
		case 'F':
			if (strlen(optarg) >= sizeof(req.tcolor)) {
				fprintf(stderr, "bosd: -F color must be "
				    "at most %zu characters\n",
				    sizeof(req.tcolor) - 1);
				usage();
			}
			strlcpy(req.tcolor, optarg, sizeof(req.tcolor));
			break;
		case 'f':
			if (optarg[0] == '\0' ||
			    strlen(optarg) >= sizeof(req.font)) {
				fprintf(stderr, "bosd: -f font must be "
				    "1 to %zu characters\n",
				    sizeof(req.font) - 1);
				usage();
			}
			strlcpy(req.font, optarg, sizeof(req.font));
			break;
		case 'G':
			if (strlen(optarg) >= sizeof(req.color)) {
				fprintf(stderr, "bosd: -G color must be "
				    "at most %zu characters\n",
				    sizeof(req.color) - 1);
				usage();
			}
			strlcpy(req.color, optarg, sizeof(req.color));
			break;
		case 'b':
			if (strlen(optarg) >= sizeof(req.badge) ||
			    optarg[strcspn(optarg, " \t\n")] != '\0') {
				fprintf(stderr, "bosd: -b badge must be "
				    "1 to %zu characters, no whitespace\n",
				    sizeof(req.badge) - 1);
				usage();
			}
			strlcpy(req.badge, optarg, sizeof(req.badge));
			break;
		case 'c': {
			char *ep;
			long v;

			errno = 0;
			v = strtol(optarg, &ep, 10);
			if (ep == optarg || *ep != '\0' || errno != 0 ||
			    v < 1 || v > INT_MAX) {
				fprintf(stderr, "bosd: -c countdown must "
				    "be 1 to %d\n", INT_MAX);
				usage();
			}
			count = (int)v;
			break;
		}
		case 'd':
			dflag = 1;
			break;
		case 'g': {
			char *ep;
			long v;

			errno = 0;
			v = strtol(optarg, &ep, 10);
			if (ep == optarg || *ep != '\0' || errno != 0 ||
			    v < 0 || v > 9999) {
				fprintf(stderr, "bosd: -g percent must "
				    "be 0 to 9999\n");
				usage();
			}
			req.gauge = (int)v;
			break;
		}
		case 'n':
			if (bosd_set_instance(optarg) != 0)
				usage();
			break;
		case 'o':
			req.outline = 0;
			break;
		case 'p':
			if (strlen(optarg) >= sizeof(req.prefix)) {
				fprintf(stderr, "bosd: -p text must be "
				    "at most %zu characters\n",
				    sizeof(req.prefix) - 1);
				usage();
			}
			strlcpy(req.prefix, optarg, sizeof(req.prefix));
			break;
		case 's':
			req.scale = atof(optarg);
			if (req.scale < BOSD_SCALE_MIN ||
			    req.scale > BOSD_SCALE_MAX) {
				fprintf(stderr, "bosd: -s scale must be "
				    "%.1f to %.1f\n", BOSD_SCALE_MIN,
				    BOSD_SCALE_MAX);
				usage();
			}
			break;
		case 'T':
			/*
			 * Flag, not an argument: the text is the first
			 * operand, so getopt stops there and a negative
			 * hold_seconds after it needs no "--".
			 */
			Tflag = 1;	/* large outlined text */
			break;
		case 't':
			tflag = 1;	/* small caption text */
			break;
		case 'v':
			printf("%s\n", BOSD_VERSION);
			return (0);
		case 'x':
			req.x_off = atoi(optarg);
			break;
		case 'y':
			req.y_off = atoi(optarg);
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	resolve_ipc_names();

	if (!req.outline &&
	    req.outline_alpha != BOSD_OUTLINE_ALPHA_DEF) {
		fprintf(stderr,
		    "bosd: -O sets outline opacity; it conflicts with -o\n");
		usage();
	}

	if (dflag) {
		if (Cflag || Dflag || argc != 0 || count != 0 || tflag ||
		    Tflag || req.gauge >= 0 || req.badge[0] != '\0' ||
		    req.tcolor[0] != '\0' || req.font[0] != '\0' ||
		    req.prefix[0] != '\0' || req.append[0] != '\0' ||
		    req.scale != 1.0 || req.alpha >= 0.0 ||
		    req.outline_alpha != BOSD_OUTLINE_ALPHA_DEF ||
		    !req.outline || req.x_off != 0 || req.y_off != 0)
			usage();
		return (run_daemon());
	}

	/*
	 * -C alone clears.  With a show (-g/-t/-c/-T/icon) it means
	 * clear every slot first, then paint the request.
	 */
	if (Cflag) {
		int showing = (argc > 0 || count > 0 || tflag || Tflag ||
		    req.gauge >= 0);

		if (!showing) {
			if (Dflag) {
				fprintf(stderr, "bosd: -D renders directly "
				    "and cannot clear a daemon's display "
				    "(-C)\n");
				usage();
			}
			if (req.badge[0] != '\0' ||
			    req.tcolor[0] != '\0' || req.font[0] != '\0' ||
			    req.prefix[0] != '\0' ||
			    req.append[0] != '\0' || req.scale != 1.0 ||
			    req.alpha >= 0.0 ||
			    req.outline_alpha != BOSD_OUTLINE_ALPHA_DEF ||
			    !req.outline || req.x_off != 0 ||
			    req.y_off != 0 || Bflag || Pflag ||
			    req.color[0] != '\0')
				usage();
			if (daemon_alive())
				return (send_clear() != 0);
			return (0);
		}
		req.clear = 1;
	}

	if (req.gauge < 0 && (Bflag || Pflag || req.color[0] != '\0')) {
		fprintf(stderr,
		    "bosd: -B, -G, and -P describe the bar; they require -g\n");
		usage();
	}
	if (req.tcolor[0] != '\0' && !tflag && !Tflag && count == 0 &&
	    req.badge[0] == '\0') {
		fprintf(stderr,
		    "bosd: -F colors -c/-t/-T text or a -b badge\n");
		usage();
	}
	if (req.font[0] != '\0' && !tflag && !Tflag && count == 0 &&
	    req.badge[0] == '\0') {
		fprintf(stderr,
		    "bosd: -f sets the face for -c/-t/-T text or a "
		    "-b badge\n");
		usage();
	}

	/* Gauge alone: no icon operand, no -c, no -T, no -t. */
	if (req.gauge >= 0 && count == 0 && !tflag && !Tflag &&
	    argc == 0) {
		if (req.badge[0] != '\0' || req.prefix[0] != '\0' ||
		    req.append[0] != '\0' || req.font[0] != '\0' ||
		    req.scale != 1.0 ||
		    req.alpha >= 0.0 ||
		    req.outline_alpha != BOSD_OUTLINE_ALPHA_DEF ||
		    !req.outline) {
			fprintf(stderr,
			    "bosd: -A, -O, -b, -f, -o, -s, -a, -p adorn "
			    "the artwork, not the bar\n");
			usage();
		}
		if (!Dflag && daemon_alive() && send_show(&req) == 0)
			return (0);
		clear_channel_if_asked(&req);
		return (run_bar(&req));
	}

	if (count > 0 || tflag || Tflag) {
		const char *holdarg = NULL;

		if ((count > 0 && (tflag || Tflag)) || (tflag && Tflag))
			usage();
		if (tflag && (req.badge[0] != '\0' ||
		    req.prefix[0] != '\0' || req.append[0] != '\0' ||
		    req.alpha >= 0.0 ||
		    req.outline_alpha != BOSD_OUTLINE_ALPHA_DEF ||
		    !req.outline)) {
			fprintf(stderr,
			    "bosd: -A, -O, -b, -o, -a, -p adorn "
			    "the large artwork; they do not apply to -t\n");
			usage();
		}
		if (tflag || Tflag) {
			if (argc < 1 || argc > 2)
				usage();
			if (argv[0][0] == '\0' ||
			    strlen(argv[0]) >= sizeof(req.spec) ||
			    argv[0][strcspn(argv[0], " \t\n")] != '\0') {
				fprintf(stderr, "bosd: -%c text must be "
				    "1 to %zu characters, no whitespace "
				    "(escape it: \\x20)\n",
				    tflag ? 't' : 'T',
				    sizeof(req.spec) - 1);
				usage();
			}
			strlcpy(req.spec, argv[0], sizeof(req.spec));
			if (argc == 2)
				holdarg = argv[1];
		} else {
			if (argc > 1)
				usage();
			req.hold = 1.0;	/* one second per digit */
			if (argc == 1)
				holdarg = argv[0];
		}
		req.count = count;
		req.text = Tflag;
		req.small = tflag;
		if (holdarg != NULL) {
			char *ep;

			req.hold = strtod(holdarg, &ep);
			if (ep == holdarg || *ep != '\0')
				usage();
			if (req.hold == -1.0) {
				/* Indefinite: a countdown must advance. */
				if (count > 0) {
					fprintf(stderr, "bosd: -c cannot "
					    "hold indefinitely (-1)\n");
					usage();
				}
			} else {
				if (req.hold < BOSD_HOLD_MIN)
					req.hold = BOSD_HOLD_MIN;
				if (req.hold > BOSD_HOLD_MAX)
					req.hold = BOSD_HOLD_MAX;
			}
		}
		if (!Dflag && daemon_alive() && send_show(&req) == 0)
			return (0);
		clear_channel_if_asked(&req);
		decode_field(req.badge, sizeof(req.badge));
		decode_field(req.prefix, sizeof(req.prefix));
		decode_field(req.append, sizeof(req.append));
		if (tflag)
			return (run_stext(&req));
		return (Tflag ? run_text(&req) : run_countdown(&req));
	}

	if (argc < 1 || argc > 2)
		usage();
	strlcpy(req.spec, argv[0], sizeof(req.spec));
	if (argc == 2)
		req.hold = atof(argv[1]);
	if (req.hold != -1.0) {	/* -1: hold until replaced or cleared */
		if (req.hold < BOSD_HOLD_MIN)
			req.hold = BOSD_HOLD_MIN;
		if (req.hold > BOSD_HOLD_MAX)
			req.hold = BOSD_HOLD_MAX;
	}

	if (!Dflag && daemon_alive() && send_show(&req) == 0)
		return (0);

	clear_channel_if_asked(&req);
	decode_field(req.badge, sizeof(req.badge));
	decode_field(req.prefix, sizeof(req.prefix));
	decode_field(req.append, sizeof(req.append));
	return (show_once(&req));
}
