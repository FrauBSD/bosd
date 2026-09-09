/*
 * bosd -- on-screen display engine.  See bosd(1).
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bosd.h"

char instance[64] = "default";

static void
usage(void)
{
	fprintf(stderr,
	    "Usage: bosd [-h] [-n instance] { -d | -C }\n"
	    "       bosd [-Dho] [-n instance] [-a text] [-b badge] "
	    "[-p text] \\\n"
	    "            [-s scale] [-x offset] [-y offset] \\\n"
	    "            { icon | -c countdown | -t text } "
	    "[hold_seconds]\n");
	exit(1);
}

/* Interpret escapes in a display-text field before local render. */
static void
decode_field(char *s, size_t size)
{
	char tmp[BOSD_SPEC_MAX];

	decode_escapes(s, tmp, sizeof(tmp));
	strlcpy(s, tmp, size);
}

int
main(int argc, char **argv)
{
	struct show_req req;
	int ch, count = 0, Cflag = 0, Dflag = 0, dflag = 0, tflag = 0;

	memset(&req, 0, sizeof(req));
	req.hold = BOSD_HOLD_DEF;
	req.scale = 1.0;
	req.outline = 1;

	while ((ch = getopt(argc, argv, "CDa:b:c:dhn:op:s:tx:y:")) != -1) {
		switch (ch) {
		case 'C':
			Cflag = 1;
			break;
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
		case 'n':
			if (strlen(optarg) >= sizeof(instance) ||
			    strchr(optarg, '/') != NULL)
				usage();
			strlcpy(instance, optarg, sizeof(instance));
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
		case 't':
			/*
			 * Flag, not an argument: the text is the first
			 * operand, so getopt stops there and a negative
			 * hold_seconds after it needs no "--".
			 */
			tflag = 1;
			break;
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

	if (dflag || Cflag) {
		if (dflag && Cflag)
			usage();
		if (Dflag && Cflag) {
			fprintf(stderr, "bosd: -D renders directly and "
			    "cannot clear a daemon's show (-C)\n");
			usage();
		}
		if (Dflag || argc != 0 || count != 0 || tflag ||
		    req.badge[0] != '\0' || req.prefix[0] != '\0' ||
		    req.append[0] != '\0' || req.scale != 1.0 ||
		    !req.outline || req.x_off != 0 || req.y_off != 0)
			usage();
		if (dflag)
			return (run_daemon());
		/* Clear: nothing to do when no daemon owns the channel. */
		if (daemon_alive())
			return (send_clear() != 0);
		return (0);
	}

	if (count > 0 || tflag) {
		const char *holdarg = NULL;

		if (count > 0 && tflag)
			usage();
		if (tflag) {
			if (argc < 1 || argc > 2)
				usage();
			if (argv[0][0] == '\0' ||
			    strlen(argv[0]) >= sizeof(req.spec) ||
			    argv[0][strcspn(argv[0], " \t\n")] != '\0') {
				fprintf(stderr, "bosd: -t text must be "
				    "1 to %zu characters, no whitespace "
				    "(escape it: \\x20)\n",
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
		req.text = tflag;
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
		decode_field(req.badge, sizeof(req.badge));
		decode_field(req.prefix, sizeof(req.prefix));
		decode_field(req.append, sizeof(req.append));
		return (tflag ? run_text(&req) : run_countdown(&req));
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

	decode_field(req.badge, sizeof(req.badge));
	decode_field(req.prefix, sizeof(req.prefix));
	decode_field(req.append, sizeof(req.append));
	return (show_once(&req));
}
