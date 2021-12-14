/*
 * Copyright (C) 2015-2022 IoT.bzh Company
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

#pragma once

#include "../sys/x-thread.h"
#include "../sys/ev-mgr.h"

extern
int afb_ev_mgr_release(x_thread_t tid);

extern
struct ev_mgr *afb_ev_mgr_try_get(x_thread_t tid);

extern
struct ev_mgr *afb_ev_mgr_get(x_thread_t tid);

extern
void afb_ev_mgr_wakeup();

extern
int afb_ev_mgr_release_for_me();

extern
struct ev_mgr *afb_ev_mgr_try_get_for_me();

extern
struct ev_mgr *afb_ev_mgr_get_for_me();

extern
int afb_ev_mgr_get_fd();

extern
int afb_ev_mgr_prepare();

extern
int afb_ev_mgr_wait(int ms);

extern
void afb_ev_mgr_dispatch();

extern
int afb_ev_mgr_wait_and_dispatch(int ms);

extern
int afb_ev_mgr_add_fd(
	struct ev_fd **efd,
	int fd,
	uint32_t events,
	ev_fd_cb_t handler,
	void *closure,
	int autounref,
	int autoclose
);

extern
int afb_ev_mgr_add_prepare(
	struct ev_prepare **prep,
	ev_prepare_cb_t handler,
	void *closure
);

extern
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
);
