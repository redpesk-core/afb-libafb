/*
 * Copyright (C) 2015-2025 IoT.bzh Company
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

#include "../libafb-config.h"

#include "sys/x-errno.h"

#if WITH_SYSTEMD

#include <unistd.h>

#include <systemd/sd-event.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-daemon.h>

#include "sys/systemd.h"

static int sdbusopen(struct sd_bus **p, int (*f)(struct sd_bus **))
{
	int rc;
	struct sd_bus *r;

	r = *p;
	if (r)
		rc = 0;
	else {
		rc = f(&r);
		if (rc >= 0) {
			rc = sd_bus_attach_event(r, systemd_get_event_loop(), 0);
			if (rc < 0)
				sd_bus_unref(r);
			else
				*p = r;
		}
	}
	return rc;
}

struct sd_event *systemd_get_event_loop()
{
	static struct sd_event *result = 0;
	int rc;

	if (!result) {
		rc = sd_event_new(&result);
		if (rc < 0)
			result = NULL;
	}
	return result;
}

struct sd_bus *systemd_get_user_bus()
{
	static struct sd_bus *result = 0;
	sdbusopen((void*)&result, (void*)sd_bus_open_user);
	return result;
}

struct sd_bus *systemd_get_system_bus()
{
	static struct sd_bus *result = 0;
	sdbusopen((void*)&result, (void*)sd_bus_open_system);
	return result;
}

#endif

#include <stdlib.h>

#if !defined(SD_LISTEN_FDS_START)
# define SD_LISTEN_FDS_START 3
#endif

int systemd_fds_for(const char *name)
{
	int idx, fd;
	const char *fdnames = getenv("LISTEN_FDNAMES");
	if (fdnames != NULL) {
		for (fd = SD_LISTEN_FDS_START ; *fdnames != '\0' ; fd++) {
			idx = 0;
			while (fdnames[idx] != ':' && name[idx] != '\0' && fdnames[idx] == name[idx])
				idx++;
			if (name[idx] == '\0' && (fdnames[idx] == ':' || fdnames[idx] == '\0'))
				return fd;
			while (fdnames[idx] != ':' && fdnames[idx] != '\0')
				idx++;
			while (fdnames[idx] == ':')
				idx++;
			fdnames = &fdnames[idx];
		}
	}
	return X_ENOENT;
}
