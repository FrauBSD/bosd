/*
 * bosd — on-screen display engine.  See bosd(1).
 */
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
	    "Usage: bosd [-n instance] -d\n"
	    "       bosd [-n instance] [-b badge] [-x offset] [-y offset] "
	    "icon [hold_seconds]\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	struct show_req req;
	int ch, dflag = 0;

	memset(&req, 0, sizeof(req));
	req.hold = BOSD_HOLD_DEF;

	while ((ch = getopt(argc, argv, "b:dhn:x:y:")) != -1) {
		switch (ch) {
		case 'b':
			if (strlen(optarg) >= sizeof(req.badge))
				usage();
			strlcpy(req.badge, optarg, sizeof(req.badge));
			break;
		case 'd':
			dflag = 1;
			break;
		case 'n':
			if (strlen(optarg) >= sizeof(instance) ||
			    strchr(optarg, '/') != NULL)
				usage();
			strlcpy(instance, optarg, sizeof(instance));
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

	if (dflag) {
		if (argc != 0 || req.badge[0] != '\0' || req.x_off != 0 ||
		    req.y_off != 0)
			usage();
		return (run_daemon());
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

	return (show_once(&req));
}
