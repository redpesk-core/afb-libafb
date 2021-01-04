/*
 * Copyright (C) 2015-2021 IoT.bzh Company
 * Author: José Bollo <jose.bollo@iot.bzh>
 *
 * $RP_BEGIN_LICENSE$
 * Commercial License Usage
 *  Licensees holding valid commercial IoT.bzh licenses may use this file in
 *  accordance with the commercial license agreement provided with the
 *  Software or, alternatively, in accordance with the terms contained in
 *  a written agreement between you and The IoT.bzh Company. For licensing terms
 *  and conditions see https://www.iot.bzh/terms-conditions. For further
 *  information use the contact form at https://www.iot.bzh/contact.
 *
 * GNU General Public License Usage
 *  Alternatively, this file may be used under the terms of the GNU General
 *  Public license version 3. This license is as published by the Free Software
 *  Foundation and appearing in the file LICENSE.GPLv3 included in the packaging
 *  of this file. Please review the following information to ensure the GNU
 *  General Public License requirements will be met
 *  https://www.gnu.org/licenses/gpl-3.0.html.
 * $RP_END_LICENSE$
 */

#include "libafb-config.h"

#if WITH_FDEV_POLL

#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include "legacy/fdev.h"
#include "legacy/fdev-provider.h"
#include "legacy/fdev-poll.h"
#include "sys/x-poll.h"
#include "sys/x-errno.h"

struct fdev_poll
{
	/** count of allocated items */
	unsigned allocated;

	/** count of enabled items */
	unsigned enabled;

	/** items */
	const struct fdev **items;
};

/*
 * disable callback for fdev
 */
static void disable(void *closure, const struct fdev *fdev)
{
	struct fdev_poll *fdev_poll = closure;
	const struct fdev **items;
	unsigned i, n;

	/* build the masks */
	items = fdev_poll->items;
	n = fdev_poll->enabled;
	for (i = 0 ; i < n && fdev != items[i] ; i++);
	if (i < n) {
		items[i] = items[--n];
		fdev_poll->enabled = n;
	}
}

/*
 * enable callback for fdev
 */
static void enable(void *closure, const struct fdev *fdev)
{
	struct fdev_poll *fdev_poll = closure;
	const struct fdev **items;
	unsigned i, n;

	/* build the masks */
	items = fdev_poll->items;
	n = fdev_poll->enabled;
	for (i = 0 ; i < n && fdev != items[i] ; i++);
	if (i == n) {
		assert(n < fdev_poll->allocated);
		items[n] = fdev;
		fdev_poll->enabled = n + 1;
	}
}

/*
 * unref
 */
static void unref(void *closure)
{
	struct fdev_poll *fdev_poll = closure;
	assert(fdev_poll->allocated);
	fdev_poll->allocated--;
}

#if POLLIN == EPOLLIN && POLLOUT == EPOLLOUT && POLLHUP == EPOLLHUP
static inline short fdev2poll(uint32_t flags)
{
	return (short)flags;
}
static inline uint32_t poll2fdev(short flags)
{
	return (uint32_t)flags;
}
#else
static inline short fdev2poll(uint32_t flags)
{
	return (flags & EPOLLIN ? POLLIN : 0)
		| (flags & EPOLLOUT ? POLLOUT : 0)
		| (flags & EPOLLHUP ? POLLHUP : 0);
}
static inline uint32_t poll2fdev(short flags)
{
	return (flags & POLLIN ? EPOLLIN : 0)
		| (flags & POLLOUT ? EPOLLOUT : 0)
		| (flags & POLLHUP ? EPOLLHUP : 0);
}
#endif

/*
 * unref is not handled here
 */
static struct fdev_itf itf =
{
	.unref = unref,
	.disable = disable,
	.enable = enable,
	.update = 0
};

/*
 * create an fdev_poll
 */
struct fdev_poll *fdev_poll_create()
{
	struct fdev_poll *fdev_poll;

	fdev_poll = malloc(sizeof *fdev_poll);
	if (fdev_poll) {
		fdev_poll->allocated = fdev_poll->enabled = 0;
		fdev_poll->items = 0;
	}
	return fdev_poll;
}

void fdev_poll_destroy(struct fdev_poll *fdev_poll)
{
	free(fdev_poll->items);
	free(fdev_poll);
}

/*
 * create an fdev linked to the 'fdev_poll' for 'fd'
 */
struct fdev *fdev_poll_add(struct fdev_poll *fdev_poll, int fd)
{
	const struct fdev **items;
	struct fdev *fdev;

	if (fd < 0 || fd >= FD_SETSIZE) {
		errno = EINVAL;
		return 0;
	}

	items = realloc(fdev_poll->items, (fdev_poll->allocated + 1) * sizeof *items);
	if (!items)
		return 0;
	fdev_poll->items = items;

	fdev = fdev_create(fd);
	if (fdev) {
		fdev_poll->allocated++;
		fdev_set_itf(fdev, &itf, fdev_poll);
	}

	return fdev;
}

/*
 * get pollable fd for the fdev_poll
 */
int fdev_poll_wait_and_dispatch(struct fdev_poll *fdev_poll, int timeout_ms)
{
	const struct fdev **items, *fdev;
	unsigned i, n;
	int rc;
	uint32_t ev;
	struct pollfd *pfds;

	/* build the polls */
	n = fdev_poll->enabled;
	items = fdev_poll->items;
	pfds = alloca(n * sizeof *pfds);
	for (i = 0 ; i < n ; i++) {
		fdev = items[i];
		pfds[i].fd = fdev_fd(fdev);
		pfds[i].events = fdev2poll(fdev_events(fdev));
		pfds[i].revents = 0;
	}

	/* build the timeval */
	if (timeout_ms < 0) {
		if (!n) {
			errno = ECANCELED;
			return -1;
		}
	}

	/* wait for an event */
	rc = poll(pfds, n, timeout_ms);
	if (rc > 0) {
		/* dispatch events */
		rc = 0;
		for (i = 0 ; i < n ; i++) {
			ev = poll2fdev(pfds[i].revents);
			if (ev) {
				fdev = items[i];
				fdev_dispatch((struct fdev*)fdev, ev);
				rc ++;
			}
		}
	}

	return rc;
}

#endif
