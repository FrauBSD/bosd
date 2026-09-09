/*
 * Daemon event loop and the socketless one-shot fallback.
 *
 * Two independent slots: the main show (icon, countdown, text) and
 * the gauge bar, each with its own window and its own expiry.  A bar
 * replaces a bar and a main show replaces a main show; neither
 * touches the other, so a gauge and a glyph can be up at once.  CLEAR
 * drops both.
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

/* Main-show slot. */
enum { M_NONE, M_ICON, M_COUNT, M_TEXT };
static int m_kind;
static struct show_req m_req;
static double m_deadline;	/* next tick or expiry; < 0 never */
static int m_digit;
static struct icon *m_icon;

/* Gauge slot. */
static int b_active;
static double b_deadline;

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

static double
expiry(double hold)
{
	return (hold < 0.0 ? -1.0 : now_monotonic() + hold);
}

static void
main_stop(void)
{
	if (m_kind == M_COUNT || m_kind == M_TEXT)
		countdown_end();
	if (m_kind != M_NONE)
		hide_overlay();
	m_kind = M_NONE;
	m_icon = NULL;
}

static void
main_start(const struct show_req *req)
{
	if (m_kind == M_COUNT || m_kind == M_TEXT)
		countdown_end();

	if (req->count > 0) {
		m_req = *req;
		if (countdown_begin(&m_req) != 0) {
			main_stop();
			return;
		}
		m_kind = M_COUNT;
		m_digit = m_req.count;
		countdown_tick(&m_req, m_digit);
		m_deadline = expiry(m_req.hold);
		return;
	}
	if (req->text) {
		m_req = *req;
		if (countdown_begin(&m_req) != 0) {
			main_stop();
			return;
		}
		m_kind = M_TEXT;
		text_tick(&m_req);
		m_deadline = expiry(m_req.hold);
		return;
	}

	/* Icon: an unresolvable replacement keeps the current show. */
	{
		struct icon *ic;

		ic = icon_lookup(req->spec, req->scale, req->outline);
		if (ic == NULL) {
			if (m_kind == M_NONE)
				hide_overlay();
			return;
		}
		m_req = *req;
		m_kind = M_ICON;
		m_icon = ic;
		paint_icon(m_icon, &m_req);
		m_deadline = expiry(m_req.hold);
	}
}

/* Deadline passed: advance a countdown, or take the show down. */
static void
main_expire(void)
{
	if (m_kind == M_COUNT && m_digit > 1) {
		m_digit--;
		countdown_tick(&m_req, m_digit);
		m_deadline = expiry(m_req.hold);
		return;
	}
	main_stop();
}

/* Nearest pending deadline as a poll timeout, capped for X service. */
static int
next_timeout(void)
{
	double next = -1.0, now;
	int ms;

	if (m_kind != M_NONE && m_deadline >= 0.0)
		next = m_deadline;
	if (b_active && b_deadline >= 0.0 &&
	    (next < 0.0 || b_deadline < next))
		next = b_deadline;
	if (next < 0.0)
		return (1000);
	now = now_monotonic();
	if (next <= now)
		return (0);
	ms = (int)((next - now) * 1000.0) + 1;
	return (ms > 1000 ? 1000 : ms);
}

/* Drain pending datagrams; the latest of each slot's kind wins. */
static void
drain_socket(int sockfd)
{
	char buf[BOSD_MSG_MAX];
	struct show_req req;
	int have_main = 0, have_bar = 0;
	struct show_req mreq, breq;
	ssize_t n;

	for (;;) {
		n = recv(sockfd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
		if (n <= 0)
			break;
		buf[n] = '\0';
		if (parse_show(buf, &req) != 0)
			continue;
		if (req.clear) {
			main_stop();
			bar_hide();
			b_active = 0;
			have_main = 0;
			have_bar = 0;
			continue;
		}
		if (req.gauge >= 0) {
			breq = req;
			have_bar = 1;
		} else {
			mreq = req;
			have_main = 1;
		}
	}
	if (have_bar) {
		if (bar_show(&breq) == 0) {
			b_active = 1;
			b_deadline = expiry(breq.hold);
		}
	}
	if (have_main)
		main_start(&mreq);
}

static void
service_x(void)
{
	while (dpy != NULL && XPending(dpy) > 0) {
		XEvent ev;

		XNextEvent(dpy, &ev);
		if (ev.type == Expose && ev.xexpose.count == 0 &&
		    m_kind == M_ICON && mapped && m_icon != NULL)
			paint_icon(m_icon, &m_req);
	}
}

int
run_daemon(void)
{
	struct sockaddr_un addr;

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

	while (!stop) {
		struct pollfd pfd;
		double now;

		pfd.fd = sock;
		pfd.events = POLLIN;
		if (poll(&pfd, 1, next_timeout()) < 0 && errno != EINTR)
			break;
		if (pfd.revents & POLLIN)
			drain_socket(sock);

		now = now_monotonic();
		if (m_kind != M_NONE && m_deadline >= 0.0 &&
		    now >= m_deadline)
			main_expire();
		if (b_active && b_deadline >= 0.0 && now >= b_deadline) {
			bar_hide();
			b_active = 0;
		}
		service_x();
	}

	return (0);
}

int
show_once(const struct show_req *req)
{
	struct icon *ic;
	double mdl, bdl = 0.0, hold;

	hold = req->hold;
	if (hold != -1.0 && hold <= 0.0)
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
	if (req->gauge >= 0) {
		bar_show(req);
		bdl = req->gauge_hold < 0.0 ? -1.0 :
		    now_monotonic() + req->gauge_hold;
	}

	/*
	 * Glyph and gauge expire on their own holds; indefinite (-1)
	 * ends via SIGINT/SIGTERM -> cleanup().  A deadline of 0
	 * marks a slot already down.
	 */
	mdl = hold < 0.0 ? -1.0 : now_monotonic() + hold;
	while (mdl != 0.0 || bdl != 0.0) {
		double now = now_monotonic();

		if (mdl != 0.0 && mdl >= 0.0 && now >= mdl) {
			hide_overlay();
			mdl = 0.0;
		}
		if (bdl != 0.0 && bdl >= 0.0 && now >= bdl) {
			bar_hide();
			bdl = 0.0;
		}
		if (mdl == 0.0 && bdl == 0.0)
			break;
		while (XPending(dpy) > 0) {
			XEvent ev;
			XNextEvent(dpy, &ev);
			if (ev.type == Expose && ev.xexpose.count == 0 &&
			    mdl != 0.0)
				paint_icon(ic, req);
		}
		poll(NULL, 0, 50);
	}
	cleanup(0);
	return (0);
}
