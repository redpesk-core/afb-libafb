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


#include <stdint.h>

#include "sys/ev-mgr.h"
#include "core/afb-sched.h"


/******************************************************************************/
/******************************************************************************/
/******  FDEV COMPATIBILITY                                              ******/
/******************************************************************************/
/******************************************************************************/

#include "legacy/fdev.h"
#include "legacy/fdev-provider.h"

static void unref(void *closure)
{
	struct ev_fd *efd = closure;
	ev_fd_unref(efd);
}

static void clear(void *closure, const struct fdev *fdev)
{
	struct ev_fd *efd = closure;
	ev_fd_set_events(efd, 0);
}

static void set(void *closure, const struct fdev *fdev)
{
	struct ev_fd *efd = closure;
	ev_fd_set_events(efd, fdev_events(fdev));
}

static void handler(struct ev_fd *efd, int fd, uint32_t revents, void *closure)
{
	struct fdev *fdev = closure;
	fdev_dispatch(fdev, revents);
}

static struct fdev_itf itf =
{
	.unref = unref,
	.disable = clear,
	.enable = set,
	.update = set
};

struct fdev *afb_fdev_create(int fd)
{
	struct ev_mgr *evmgr;
	struct ev_fd *efd;
	struct fdev *fdev;
	int rc;

	evmgr = afb_sched_acquire_event_manager();
	fdev = fdev_create(fd);
	if (fdev) {
		rc = ev_mgr_add_fd(evmgr, &efd, fd, 0, handler, fdev, 0, 0);
		if (rc >= 0)
			fdev_set_itf(fdev, &itf, efd);
		else {
			fdev_unref(fdev);
			fdev = 0;
		}
	}
	return fdev;
}

