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
drain_pending_shows(int sockfd, struct show_req *req)
{
	char buf[BOSD_MSG_MAX];
	struct show_req next;
	ssize_t n;

	for (;;) {
		n = recv(sockfd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
		if (n <= 0)
			break;
		buf[n] = '\0';
		if (parse_show(buf, &next) != 0)
			continue;
		*req = next;
	}
}

static int
wait_or_replace(int sockfd, double deadline, struct show_req *req,
    const struct icon *cur, const struct show_req *shown)
{
	struct pollfd pfd;
	char buf[BOSD_MSG_MAX];
	struct show_req next;
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
				paint_icon(cur, shown);
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
		if (parse_show(buf, &next) != 0)
			continue;
		*req = next;
		drain_pending_shows(sockfd, req);
		return (1);
	}

	return (0);
}

/* Tick the digits down; returns 1 when a new show preempted us. */
static int
countdown_cycle(struct show_req *req, int sockfd)
{
	struct show_req cur = *req;
	int i;

	if (countdown_begin(&cur) != 0)
		return (0);
	for (i = cur.count; i >= 1 && !stop; i--) {
		countdown_tick(&cur, i);
		if (wait_or_replace(sockfd, now_monotonic() + cur.hold,
		    req, NULL, NULL)) {
			countdown_end();
			return (1);
		}
	}
	countdown_end();
	return (0);
}

static void
show_cycle(struct show_req *req, int sockfd)
{
	struct show_req shown;
	struct icon *ic;

	drain_pending_shows(sockfd, req);
restart:
	if (req->clear) {
		hide_overlay();
		return;
	}
	if (req->count > 0) {
		if (countdown_cycle(req, sockfd))
			goto restart;
		hide_overlay();
		return;
	}
	ic = icon_lookup(req->spec, req->scale, req->outline);
	if (ic == NULL) {
		hide_overlay();
		return;
	}
	paint_icon(ic, req);
	shown = *req;

	for (;;) {
		struct icon *next;
		int replaced;

		replaced = wait_or_replace(sockfd,
		    now_monotonic() + req->hold, req, ic, &shown);
		if (!replaced)
			break;
		if (req->count > 0 || req->clear)
			goto restart;
		/* Unresolvable replacement: keep the current show up. */
		next = icon_lookup(req->spec, req->scale, req->outline);
		if (next != NULL) {
			ic = next;
			paint_icon(ic, req);
			shown = *req;
		}
	}

	hide_overlay();
}

int
run_daemon(void)
{
	struct sockaddr_un addr;
	char buf[BOSD_MSG_MAX];
	struct show_req req;

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
		if (parse_show(buf, &req) != 0)
			continue;
		show_cycle(&req, sock);
	}

	return (0);
}

int
show_once(const struct show_req *req)
{
	struct icon *ic;
	double deadline, hold;

	hold = req->hold;
	if (hold <= 0.0)
		hold = BOSD_HOLD_DEF;
	if (init_display() != 0)
		return (1);
	signal(SIGTERM, cleanup);
	signal(SIGINT, cleanup);

	ic = icon_lookup(req->spec, req->scale, req->outline);
	if (ic == NULL) {
		fprintf(stderr, "bosd: cannot load icon '%s'\n", req->spec);
		return (1);
	}
	paint_icon(ic, req);

	deadline = now_monotonic() + hold;
	while (now_monotonic() < deadline) {
		while (XPending(dpy) > 0) {
			XEvent ev;
			XNextEvent(dpy, &ev);
			if (ev.type == Expose && ev.xexpose.count == 0)
				paint_icon(ic, req);
		}
		poll(NULL, 0, 50);
	}
	hide_overlay();
	cleanup(0);
	return (0);
}
