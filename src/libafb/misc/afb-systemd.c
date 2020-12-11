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

#include <string.h>
#include <systemd/sd-event.h>

#include "misc/afb-systemd.h"
#include "core/afb-ev-mgr.h"
#include "sys/systemd.h"
#include "sys/x-errno.h"
#include "sys/verbose.h"

static void onprepare(struct ev_prepare *prep, void *closure)
{
	struct sd_event *ev = closure;
	int rc = sd_event_prepare(ev);
	while(rc > 0) {
		sd_event_dispatch(ev);
		rc = sd_event_prepare(ev);
	}
	if (rc < 0)
		ERROR("sd_event_prepare returned %d (state %d) %s", rc, sd_event_get_state(ev), strerror(-rc));
}

static void onevent(struct ev_fd *efd, int fd, uint32_t revents, void *closure)
{
	struct sd_event *ev = closure;
	int rc = sd_event_wait(ev, 0);
	if (rc > 0) {
		sd_event_dispatch(ev);
		rc = sd_event_wait(ev, 0);
	}
	if (rc < 0)
		ERROR("sd_event_wait returned %d (state %d) %s", rc, sd_event_get_state(ev), strerror(-rc));
}

struct sd_event *afb_systemd_get_event_loop()
{
	static char set = 0;
	struct sd_event *result;
	struct ev_fd *efd;
	struct ev_prepare *prep;
	int fd;

	result = systemd_get_event_loop();
	if (result && !set) {
		fd = sd_event_get_fd(result);
		afb_ev_mgr_add_fd(&efd, fd, EPOLLIN, onevent, result, 1, 0);
		afb_ev_mgr_add_prepare(&prep, onprepare, result);
		set = 1;
	}
	return result;
}

struct sd_bus *afb_systemd_get_user_bus()
{
	afb_systemd_get_event_loop();
	return systemd_get_user_bus();
}

struct sd_bus *afb_systemd_get_system_bus()
{
	afb_systemd_get_event_loop();
	return systemd_get_system_bus();
}

int afb_systemd_fds_init()
{
	return systemd_fds_init();
}

int afb_systemd_fds_for(const char *name)
{
	return systemd_fds_for(name);
}

#endif
