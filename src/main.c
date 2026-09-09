/*
 * bosd — on-screen display engine.  See bosd(1).
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
	    "       bosd [-ho] [-n instance] [-b badge] [-s scale] "
	    "[-x offset] \\\n"
	    "            [-y offset] { icon | -c countdown | -t text } "
	    "[hold_seconds]\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	struct show_req req;
	char badge_dec[BOSD_BADGE_MAX];
	int ch, count = 0, Cflag = 0, dflag = 0, tflag = 0;

	memset(&req, 0, sizeof(req));
	req.hold = BOSD_HOLD_DEF;
	req.scale = 1.0;
	req.outline = 1;

	while ((ch = getopt(argc, argv, "Cb:c:dhn:os:t:x:y:")) != -1) {
		switch (ch) {
		case 'C':
			Cflag = 1;
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
			if (optarg[0] == '\0' ||
			    strlen(optarg) >= sizeof(req.spec) ||
			    optarg[strcspn(optarg, " \t\n")] != '\0') {
				fprintf(stderr, "bosd: -t text must be "
				    "1 to %zu characters, no whitespace "
				    "(escape it: \\x20)\n",
				    sizeof(req.spec) - 1);
				usage();
			}
			tflag = 1;
			strlcpy(req.spec, optarg, sizeof(req.spec));
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
		if (argc != 0 || count != 0 || tflag ||
		    req.badge[0] != '\0' || req.scale != 1.0 ||
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
		if (argc > 1 || (count > 0 && tflag))
			usage();
		req.count = count;
		req.text = tflag;
		if (count > 0)
			req.hold = 1.0;	/* one second per digit */
		if (argc == 1) {
			char *ep;

			req.hold = strtod(argv[0], &ep);
			if (ep == argv[0] || *ep != '\0')
				usage();
			if (req.hold < BOSD_HOLD_MIN)
				req.hold = BOSD_HOLD_MIN;
			if (req.hold > BOSD_HOLD_MAX)
				req.hold = BOSD_HOLD_MAX;
		}
		if (daemon_alive() && send_show(&req) == 0)
			return (0);
		decode_escapes(req.badge, badge_dec, sizeof(badge_dec));
		strlcpy(req.badge, badge_dec, sizeof(req.badge));
		return (tflag ? run_text(&req) : run_countdown(&req));
	}

	if (argc < 1 || argc > 2)
		usage();
	strlcpy(req.spec, argv[0], sizeof(req.spec));
	if (argc == 2)
		req.hold = atof(argv[1]);
	if (req.hold < BOSD_HOLD_MIN)
		req.hold = BOSD_HOLD_MIN;
	if (req.hold > BOSD_HOLD_MAX)
		req.hold = BOSD_HOLD_MAX;

	if (daemon_alive() && send_show(&req) == 0)
		return (0);

	decode_escapes(req.badge, badge_dec, sizeof(badge_dec));
	strlcpy(req.badge, badge_dec, sizeof(req.badge));
	return (show_once(&req));
}
