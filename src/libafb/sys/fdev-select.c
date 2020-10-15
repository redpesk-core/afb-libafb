/*
 * Copyright (C) 2015-2020 IoT.bzh Company
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

#if WITH_FDEV_SELECT

#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <sys/select.h>

#define FDEV_PROVIDER
#include "sys/fdev.h"
#include "sys/fdev-select.h"
#include "sys/x-errno.h"

struct fdev_select
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
	struct fdev_select *fdev_select = closure;
	const struct fdev **items;
	unsigned i, n;

	/* build the masks */
	items = fdev_select->items;
	n = fdev_select->enabled;
	for (i = 0 ; i < n && fdev != items[i] ; i++);
	if (i < n) {
		items[i] = items[--n];
		fdev_select->enabled = n;
	}
}

/*
 * enable callback for fdev
 */
static void enable(void *closure, const struct fdev *fdev)
{
	struct fdev_select *fdev_select = closure;
	const struct fdev **items;
	unsigned i, n;

	/* build the masks */
	items = fdev_select->items;
	n = fdev_select->enabled;
	for (i = 0 ; i < n && fdev != items[i] ; i++);
	if (i == n) {
		assert(n < fdev_select->allocated);
		items[n] = fdev;
		fdev_select->enabled = n + 1;
	}
}

/*
 * unref
 */
static void unref(void *closure)
{
	struct fdev_select *fdev_select = closure;
	assert(fdev_select->allocated);
	fdev_select->allocated--;
}

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
 * create an fdev_select
 */
struct fdev_select *fdev_select_create()
{
	struct fdev_select *fdev_select;

	fdev_select = malloc(sizeof *fdev_select);
	if (fdev_select) {
		fdev_select->allocated = fdev_select->enabled = 0;
		fdev_select->items = 0;
	}
	return fdev_select;
}
void fdev_select_destroy(struct fdev_select *fdev_select)
{
	free(fdev_select->items);
	free(fdev_select);
}

/*
 * create an fdev linked to the 'fdev_select' for 'fd'
 */
struct fdev *fdev_select_add(struct fdev_select *fdev_select, int fd)
{
	const struct fdev **items;
	struct fdev *fdev;

	if (fd < 0 || fd >= FD_SETSIZE) {
		errno = EINVAL;
		return 0;
	}

	items = realloc(fdev_select->items, (fdev_select->allocated + 1) * sizeof *items);
	if (!items)
		return 0;
	fdev_select->items = items;

	fdev = fdev_create(fd);
	if (fdev) {
		fdev_select->allocated++;
		fdev_set_itf(fdev, &itf, fdev_select);
	}

	return fdev;
}

/*
 * get pollable fd for the fdev_select
 */
int fdev_select_wait_and_dispatch(struct fdev_select *fdev_select, int timeout_ms)
{
	fd_set wfds, rfds, efds;
	const struct fdev **items, *fdev;
	unsigned i, n;
	int rc, fd, nfds;
	uint32_t ev;
	struct timeval tv, *ptv;

	/* build the masks */
	FD_ZERO(&wfds);
	FD_ZERO(&rfds);
	FD_ZERO(&efds);
	nfds = -1;
	n = fdev_select->enabled;
	items = fdev_select->items;
	for (i = 0 ; i < n ; i++) {
		fdev = items[i];
		fd = fdev_fd(fdev);
		if (fd > nfds)
			nfds = fd;
		ev = fdev_events(fdev);
		if (ev & EPOLLIN)
			FD_SET(fd, &rfds);
		if (ev & EPOLLOUT)
			FD_SET(fd, &wfds);
		if (ev & EPOLLPRI)
			FD_SET(fd, &efds);
	}
	nfds++;

	/* build the timeval */
	if (timeout_ms < 0) {
		if (!nfds)
			return X_ECANCELED;
		ptv = 0;
	} else {
		tv.tv_sec = timeout_ms / 1000;
		tv.tv_usec = (timeout_ms % 1000) * 1000;
		ptv = &tv;
	}

	/* wait for an event */
	rc = select(nfds, &rfds, &wfds, &efds, ptv);
	if (rc < 0)
		rc = -errno;
	else if (rc > 0) {
		/* dispatch events */
		rc = 0;
		for (i = 0 ; i < n ; i++) {
			fdev = items[i];
			fd = fdev_fd(fdev);
			ev = 0;
			if(FD_ISSET(fd, &rfds))
				ev |= EPOLLIN;
			if(FD_ISSET(fd, &wfds))
				ev |= EPOLLOUT;
			if(FD_ISSET(fd, &efds))
				ev |= EPOLLPRI;
			if (ev) {
				fdev_dispatch((struct fdev*)fdev, ev);
				rc++;
			}
		}
	}

	return rc;
}

#endif
