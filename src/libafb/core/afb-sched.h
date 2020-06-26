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

#pragma once

struct afb_sched_lock;
struct evmgr;

extern int afb_sched_enter(
		const void *group,
		int timeout,
		void (*callback)(int signum, void *closure, struct afb_sched_lock *afb_sched_lock),
		void *closure);

extern int afb_sched_leave(struct afb_sched_lock *afb_sched_lock);

extern int afb_sched_call_job_sync(
		const void *group,
		int timeout,
		void (*callback)(int, void*),
		void *arg);

extern int afb_sched_start(
		int allowed_count,
		int start_count,
		int waiter_count,
		void (*start)(int signum, void* arg),
		void *arg);

extern void afb_sched_exit(void (*handler)());

extern struct evmgr *afb_sched_acquire_event_manager();

extern void afb_sched_adapt(int pending_job_count);
