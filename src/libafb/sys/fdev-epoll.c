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

#if WITH_FDEV_EPOLL && WITH_EPOLL

#include <unistd.h>
#include <sys/epoll.h>

#define FDEV_PROVIDER
#include "sys/fdev.h"
#include "sys/fdev-epoll.h"
#include "sys/x-errno.h"

/*
 * For sake of simplicity there is no struct fdev_epoll.
 * Instead, the file descriptor of the internal epoll is used
 * and wrapped in a pseudo pointer to a pseudo struct.
 */
#define epollfd(fdev_epoll)  ((int)(intptr_t)fdev_epoll)

/*
 * disable callback for fdev
 *
 * refs to fdev must not be counted here
 */
static void disable(void *closure, const struct fdev *fdev)
{
	struct fdev_epoll *fdev_epoll = closure;
	epoll_ctl(epollfd(fdev_epoll), EPOLL_CTL_DEL, fdev_fd(fdev), 0);
}

/*
 * enable callback for fdev
 *
 * refs to fdev must not be counted here
 */
static void enable_or_update(void *closure, const struct fdev *fdev, int op, int err)
{
	struct fdev_epoll *fdev_epoll = closure;
	struct epoll_event event;
	int rc, fd;

	fd = fdev_fd(fdev);
	event.events = fdev_events(fdev);
	event.data.ptr = (void*)fdev;
	rc = epoll_ctl(epollfd(fdev_epoll), op, fd, &event);
	if (rc < 0 && errno == err)
		epoll_ctl(epollfd(fdev_epoll), (EPOLL_CTL_MOD + EPOLL_CTL_ADD) - op, fd, &event);
}

/*
 * enable callback for fdev
 *
 * refs to fdev must not be counted here
 */
static void enable(void *closure, const struct fdev *fdev)
{
	enable_or_update(closure, fdev, EPOLL_CTL_ADD, EEXIST);
}

/*
 * update callback for fdev
 *
 * refs to fdev must not be counted here
 */
static void update(void *closure, const struct fdev *fdev)
{
	enable_or_update(closure, fdev, EPOLL_CTL_MOD, ENOENT);
}

/*
 * unref is not handled here
 */
static struct fdev_itf itf =
{
	.unref = 0,
	.disable = disable,
	.enable = enable,
	.update = update
};

/*
 * create an fdev_epoll
 */
struct fdev_epoll *fdev_epoll_create()
{
	int fd = epoll_create1(EPOLL_CLOEXEC);
	if (!fd) {
		fd = dup(fd);
		close(0);
	}
	return fd < 0 ? 0 : (struct fdev_epoll*)(intptr_t)fd;
}

/*
 * destroy the fdev_epoll
 */
void fdev_epoll_destroy(struct fdev_epoll *fdev_epoll)
{
	close(epollfd(fdev_epoll));
}

/*
 * get pollable fd for the fdev_epoll
 */
int fdev_epoll_fd(struct fdev_epoll *fdev_epoll)
{
	return epollfd(fdev_epoll);
}

/*
 * create an fdev linked to the 'fdev_epoll' for 'fd'
 */
struct fdev *fdev_epoll_add(struct fdev_epoll *fdev_epoll, int fd)
{
	struct fdev *fdev;

	fdev = fdev_create(fd);
	if (fdev)
		fdev_set_itf(fdev, &itf, fdev_epoll);
	return fdev;
}

/*
 * get pollable fd for the fdev_epoll
 */
int fdev_epoll_wait_and_dispatch(struct fdev_epoll *fdev_epoll, int timeout_ms)
{
	struct fdev *fdev;
	struct epoll_event events;
	int rc;

	rc = epoll_wait(epollfd(fdev_epoll), &events, 1, timeout_ms < 0 ? -1 : timeout_ms);
	if (rc == 1) {
		fdev = events.data.ptr;
		fdev_dispatch(fdev, events.events);
	}
	return rc;
}

int fdev_epoll_get_epoll_fd(struct fdev_epoll *fdev_epoll)
{
	return epollfd(fdev_epoll);
}

#endif

