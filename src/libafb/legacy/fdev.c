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

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include "sys/x-errno.h"

#include "legacy/fdev.h"
#include "legacy/fdev-provider.h"

struct fdev
{
	int fd;
	uint32_t events;
	unsigned refcount;
	struct fdev_itf *itf;
	void *closure_itf;
	void (*callback)(void*,uint32_t,struct fdev*);
	void *closure_callback;
};

struct fdev *fdev_create(int fd)
{
	struct fdev *fdev;

	fdev = calloc(1, sizeof *fdev);
	if (fdev) {
		fdev->fd = fd;
		fdev->refcount = 3; /* set autoclose by default */
	}
	return fdev;
}

void fdev_set_itf(struct fdev *fdev, struct fdev_itf *itf, void *closure_itf)
{
	fdev->itf = itf;
	fdev->closure_itf = closure_itf;
}

void fdev_dispatch(struct fdev *fdev, uint32_t events)
{
	if (fdev->callback)
		fdev->callback(fdev->closure_callback, events, fdev);
}

struct fdev *fdev_addref(struct fdev *fdev)
{
	if (fdev)
		__atomic_add_fetch(&fdev->refcount, 2, __ATOMIC_RELAXED);
	return fdev;
}

void fdev_unref(struct fdev *fdev)
{
	if (fdev && __atomic_sub_fetch(&fdev->refcount, 2, __ATOMIC_RELAXED) <= 1) {
		if (fdev->itf) {
			fdev->itf->disable(fdev->closure_itf, fdev);
			if (fdev->itf->unref)
				fdev->itf->unref(fdev->closure_itf);
		}
		if (fdev->refcount)
			close(fdev->fd);
		free(fdev);
	}
}

int fdev_fd(const struct fdev *fdev)
{
	return fdev->fd;
}

uint32_t fdev_events(const struct fdev *fdev)
{
	return fdev->events;
}

int fdev_autoclose(const struct fdev *fdev)
{
	return 1 & fdev->refcount;
}

static inline int is_active(struct fdev *fdev)
{
	return !!fdev->callback;
}

static inline void update_activity(struct fdev *fdev, int old_active)
{
	if (fdev->itf) {
		if (is_active(fdev)) {
			if (!old_active && fdev->itf->enable)
				fdev->itf->enable(fdev->closure_itf, fdev);
		} else {
			if (old_active && fdev->itf->disable)
				fdev->itf->disable(fdev->closure_itf, fdev);
		}
	}
}

void fdev_set_callback(struct fdev *fdev, void (*callback)(void*,uint32_t,struct fdev*), void *closure)
{
	int oa;

	oa = is_active(fdev);
	fdev->callback = callback;
	fdev->closure_callback = closure;
	update_activity(fdev, oa);
}

void fdev_set_events(struct fdev *fdev, uint32_t events)
{
	if (events != fdev->events) {
		fdev->events = events;
		if (is_active(fdev) && fdev->itf && fdev->itf->update)
			fdev->itf->update(fdev->closure_itf, fdev);
	}
}

void fdev_set_autoclose(struct fdev *fdev, int autoclose)
{
	if (autoclose)
		fdev->refcount |= (unsigned)1;
	else
		fdev->refcount &= ~(unsigned)1;
}

