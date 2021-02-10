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
#include "core/afb-ev-mgr.h"

int afb_ev_mgr_get_fd()
{
	struct ev_mgr *mgr = afb_sched_acquire_event_manager();
	return ev_mgr_get_fd(mgr);
}

int afb_ev_mgr_prepare()
{
	struct ev_mgr *mgr = afb_sched_acquire_event_manager();
	return ev_mgr_prepare(mgr);
}

int afb_ev_mgr_wait(int ms)
{
	struct ev_mgr *mgr = afb_sched_acquire_event_manager();
	return ev_mgr_wait(mgr, ms);
}

void afb_ev_mgr_dispatch()
{
	struct ev_mgr *mgr = afb_sched_acquire_event_manager();
	ev_mgr_dispatch(mgr);
}

int afb_ev_mgr_wait_and_dispatch(int ms)
{
	int rc;
	struct ev_mgr *mgr = afb_sched_acquire_event_manager();
	rc = ev_mgr_wait(mgr, ms);
	if (rc >= 0)
		ev_mgr_dispatch(mgr);
	return rc;
}

int afb_ev_mgr_add_fd(
	struct ev_fd **efd,
	int fd,
	uint32_t events,
	ev_fd_cb_t handler,
	void *closure,
	int autounref,
	int autoclose
) {
	struct ev_mgr *mgr = afb_sched_acquire_event_manager();
	return ev_mgr_add_fd(mgr, efd, fd, events, handler, closure, autounref, autoclose);
}

int afb_ev_mgr_add_prepare(
	struct ev_prepare **prep,
	ev_prepare_cb_t handler,
	void *closure
) {
	struct ev_mgr *mgr = afb_sched_acquire_event_manager();
	return ev_mgr_add_prepare(mgr, prep, handler, closure);
}

int afb_ev_mgr_add_timer(
	struct ev_timer **timer,
	int absolute,
	time_t start_sec,
	unsigned start_ms,
	unsigned count,
	unsigned period_ms,
	unsigned accuracy_ms,
	ev_timer_cb_t handler,
	void *closure,
	int autounref
) {
	struct ev_mgr *mgr = afb_sched_acquire_event_manager();
	return ev_mgr_add_timer(mgr, timer, absolute, start_sec, start_ms, count, period_ms, accuracy_ms, handler, closure, autounref);
}
