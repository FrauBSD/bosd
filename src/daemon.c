/*
 * Daemon event loop, show cycle, and the socketless one-shot fallback.
 */
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include "bosd.h"

volatile sig_atomic_t stop;
int sock = -1;

void
cleanup(int sig __unused)
{
	icon_cache_clear();
	x11_cleanup();
	if (sock >= 0) {
		close(sock);
		sock = -1;
	}
	unlink(sock_name);
	unlink(pid_name);
	_exit(0);
}

double
now_monotonic(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((double)ts.tv_sec + (double)ts.tv_nsec / 1e9);
}

static void
drain_pending_shows(int sockfd, double *hold_secs, char *spec,
    size_t speclen)
{
	char buf[BOSD_MSG_MAX];
	double hold;
	char next[BOSD_SPEC_MAX];
	ssize_t n;

	for (;;) {
		n = recv(sockfd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
		if (n <= 0)
			break;
		buf[n] = '\0';
		if (parse_show(buf, &hold, next, sizeof(next)) != 0)
			continue;
		*hold_secs = hold;
		strlcpy(spec, next, speclen);
	}
}

static int
wait_or_replace(int sockfd, double deadline, double *hold_secs, char *spec,
    size_t speclen, const struct icon *cur)
{
	struct pollfd pfd;
	char buf[BOSD_MSG_MAX];
	double hold;
	char next[BOSD_SPEC_MAX];
	ssize_t n;

	while (!stop && now_monotonic() < deadline) {
		double left = deadline - now_monotonic();
		int ms;

		if (left <= 0.0)
			break;
		ms = (int)(left * 1000.0);
		if (ms <= 0)
			ms = 1;

		/* Service Expose so a compositor restart cannot blank us. */
		while (dpy != NULL && XPending(dpy) > 0) {
			XEvent ev;
			XNextEvent(dpy, &ev);
			if (ev.type == Expose && ev.xexpose.count == 0 &&
			    mapped && cur != NULL)
				paint_icon(cur);
		}

		pfd.fd = sockfd;
		pfd.events = POLLIN;
		if (poll(&pfd, 1, ms) <= 0)
			continue;

		n = recv(sockfd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
		if (n <= 0) {
			if (n < 0 && (errno == EINTR || errno == EAGAIN))
				continue;
			break;
		}
		buf[n] = '\0';
		if (parse_show(buf, &hold, next, sizeof(next)) != 0)
			continue;
		*hold_secs = hold;
		strlcpy(spec, next, speclen);
		drain_pending_shows(sockfd, hold_secs, spec, speclen);
		return (1);
	}

	return (0);
}

static void
show_cycle(char *spec, double hold, int sockfd)
{
	struct icon *ic;

	drain_pending_shows(sockfd, &hold, spec, BOSD_SPEC_MAX);
	ic = icon_lookup(spec);
	if (ic == NULL)
		return;
	paint_icon(ic);

	for (;;) {
		struct icon *next;
		int replaced;

		replaced = wait_or_replace(sockfd, now_monotonic() + hold,
		    &hold, spec, BOSD_SPEC_MAX, ic);
		if (!replaced)
			break;
		/* Unresolvable replacement: keep the current glyph up. */
		next = icon_lookup(spec);
		if (next != NULL) {
			ic = next;
			paint_icon(ic);
		}
	}

	hide_overlay();
}

int
run_daemon(void)
{
	struct sockaddr_un addr;
	char buf[BOSD_MSG_MAX];
	char spec[BOSD_SPEC_MAX];
	double hold;

	signal(SIGTERM, cleanup);
	signal(SIGINT, cleanup);
	signal(SIGHUP, cleanup);

	if (init_display() != 0)
		return (1);

	sock = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (sock < 0)
		return (1);

	unlink(sock_name);
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strlcpy(addr.sun_path, sock_name, sizeof(addr.sun_path));
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		close(sock);
		sock = -1;
		return (1);
	}
	(void)chmod(sock_name, 0666);
	if (write_pid_file() != 0) {
		close(sock);
		sock = -1;
		return (1);
	}

	for (;;) {
		struct pollfd pfd;
		ssize_t n;

		pfd.fd = sock;
		pfd.events = POLLIN;
		if (poll(&pfd, 1, 1000) <= 0)
			continue;

		n = recv(sock, buf, sizeof(buf) - 1, 0);
		if (n <= 0) {
			if (n < 0 && errno == EINTR)
				continue;
			break;
		}
		buf[n] = '\0';
		if (parse_show(buf, &hold, spec, sizeof(spec)) != 0)
			continue;
		show_cycle(spec, hold, sock);
	}

	return (0);
}

int
show_once(const char *spec, double hold_secs)
{
	struct icon *ic;
	double deadline;

	if (hold_secs <= 0.0)
		hold_secs = BOSD_HOLD_DEF;
	if (init_display() != 0)
		return (1);
	signal(SIGTERM, cleanup);
	signal(SIGINT, cleanup);

	ic = icon_lookup(spec);
	if (ic == NULL) {
		fprintf(stderr, "bosd: cannot load icon '%s'\n", spec);
		return (1);
	}
	paint_icon(ic);

	deadline = now_monotonic() + hold_secs;
	while (now_monotonic() < deadline) {
		while (XPending(dpy) > 0) {
			XEvent ev;
			XNextEvent(dpy, &ev);
			if (ev.type == Expose && ev.xexpose.count == 0)
				paint_icon(ic);
		}
		poll(NULL, 0, 50);
	}
	hide_overlay();
	cleanup(0);
	return (0);
}
