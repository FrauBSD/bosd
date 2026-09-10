/*
 * libbosd -- in-process client for a warm bosd(1) channel daemon.
 */
#include <string.h>

#include <bosd.h>

/* Defined in ipc.c; no X11. */
void	 bosd_paths(const char *, char *, size_t, char *, size_t);
int	 daemon_alive_at(const char *, const char *);
int	 send_show_to(const char *, const struct bosd_req *);
int	 send_clear_to(const char *);

char instance[BOSD_INSTANCE_MAX] = "default";

void
bosd_req_init(struct bosd_req *req)
{
	memset(req, 0, sizeof(*req));
	req->hold = BOSD_HOLD_DEF;
	req->scale = 1.0;
	req->outline = 1;
	req->gauge = -1;
	req->gauge_prev = -1;
	req->gauge_hold = BOSD_GAUGE_HOLD_DEF;
}

int
bosd_set_instance(const char *name)
{
	if (name == NULL || name[0] == '\0' ||
	    strlen(name) >= sizeof(instance) || strchr(name, '/') != NULL)
		return (-1);
	strlcpy(instance, name, sizeof(instance));
	return (0);
}

const char *
bosd_get_instance(void)
{
	return (instance);
}

int
bosd_alive(const char *channel)
{
	char sock[104], pidf[104];

	bosd_paths(channel, sock, sizeof(sock), pidf, sizeof(pidf));
	return (daemon_alive_at(sock, pidf));
}

int
bosd_show(const char *channel, const struct bosd_req *req)
{
	char sock[104], pidf[104];

	if (req == NULL)
		return (-1);
	bosd_paths(channel, sock, sizeof(sock), pidf, sizeof(pidf));
	(void)pidf;
	return (send_show_to(sock, req));
}

int
bosd_clear(const char *channel)
{
	char sock[104], pidf[104];

	bosd_paths(channel, sock, sizeof(sock), pidf, sizeof(pidf));
	(void)pidf;
	return (send_clear_to(sock));
}

const char *
bosd_version(void)
{
	return (BOSD_VERSION);
}
