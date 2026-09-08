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
	    "       bosd [-n instance] icon [hold_seconds]\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	double hold = BOSD_HOLD_DEF;
	int ch, dflag = 0;

	while ((ch = getopt(argc, argv, "dn:")) != -1) {
		switch (ch) {
		case 'd':
			dflag = 1;
			break;
		case 'n':
			if (strlen(optarg) >= sizeof(instance) ||
			    strchr(optarg, '/') != NULL)
				usage();
			strlcpy(instance, optarg, sizeof(instance));
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	resolve_ipc_names();

	if (dflag) {
		if (argc != 0)
			usage();
		return (run_daemon());
	}

	if (argc < 1 || argc > 2)
		usage();
	if (argc == 2)
		hold = atof(argv[1]);
	if (hold < BOSD_HOLD_MIN)
		hold = BOSD_HOLD_MIN;
	if (hold > BOSD_HOLD_MAX)
		hold = BOSD_HOLD_MAX;

	if (daemon_alive() && send_show(argv[0], hold) == 0)
		return (0);

	return (show_once(argv[0], hold));
}
