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

#if WITH_SYSTEMD

#include <systemd/sd-event.h>

#include "sys/fdev.h"
#include "sys/fdev-provider.h"
#include "sys/fdev-systemd.h"
#include "sys/x-errno.h"

static int handler(sd_event_source *s, int fd, uint32_t revents, void *userdata)
{
	struct fdev *fdev = userdata;
	fdev_dispatch(fdev, revents);
	return 0;
}

static void unref(void *closure)
{
	sd_event_source *source = closure;
	sd_event_source_unref(source);
}

static void disable(void *closure, const struct fdev *fdev)
{
	sd_event_source *source = closure;
	sd_event_source_set_enabled(source, SD_EVENT_OFF);
}

static void enable(void *closure, const struct fdev *fdev)
{
	sd_event_source *source = closure;
	sd_event_source_set_io_events(source, fdev_events(fdev));
	sd_event_source_set_enabled(source, SD_EVENT_ON);
}

static struct fdev_itf itf =
{
	.unref = unref,
	.disable = disable,
	.enable = enable,
	.update = enable
};

struct fdev *fdev_systemd_create(struct sd_event *eloop, int fd)
{
	int rc;
	sd_event_source *source;
	struct fdev *fdev;

	fdev = fdev_create(fd);
	if (fdev) {
		rc = sd_event_add_io(eloop, &source, fd, 0, handler, fdev);
		if (rc < 0) {
			fdev_unref(fdev);
			fdev = 0;
			errno = -rc;
		} else {
			sd_event_source_set_enabled(source, SD_EVENT_OFF);
			fdev_set_itf(fdev, &itf, source);
		}
	}
	return fdev;
}

#endif

