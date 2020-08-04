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

/**
 * Enter a synchronisation point: activates the job given by 'callback'
 * and 'closure' using 'group' and 'timeout' to control sequencing and
 * execution time.
 * @param group the group for sequencing jobs
 * @param timeout the time in seconds allocated to the job
 * @param callback the callback that will handle the job.
 *                 it receives 3 parameters: 'signum' that will be 0
 *                 on normal flow or the catched signal number in case
 *                 of interrupted flow, the context 'closure' as given and
 *                 a 'jobloop' reference that must be used when the job is
 *                 terminated to unlock the current execution flow.
 * @param closure the argument to the callback
 * @return 0 on success or -1 in case of error
 */
extern int afb_sched_enter(
		const void *group,
		int timeout,
		void (*callback)(int signum, void *closure, struct afb_sched_lock *afb_sched_lock),
		void *closure);

/**
 * Unlocks the execution flow designed by 'jobloop'.
 * @param jobloop indication of the flow to unlock
 * @return 0 in case of success of -1 on error
 */
extern int afb_sched_leave(struct afb_sched_lock *afb_sched_lock);

/**
 * Calls synchronously the job represented by 'callback' and 'arg1'
 * for the 'group' and the 'timeout' and waits for its completion.
 * @param group    The group of the job or NULL when no group.
 * @param timeout  The maximum execution time in seconds of the job
 *                 or 0 for unlimited time.
 * @param callback The function to execute for achieving the job.
 *                 Its first parameter is either 0 on normal flow
 *                 or the signal number that broke the normal flow.
 *                 The remaining parameter is the parameter 'arg1'
 *                 given here.
 * @param arg      The second argument for 'callback'
 * @return 0 in case of success or -1 in case of error
 */
extern int afb_sched_call_job_sync(
		const void *group,
		int timeout,
		void (*callback)(int, void*),
		void *arg);

/**
 * Enter the jobs processing loop.
 * @param allowed_count Maximum count of thread for jobs including this one
 * @param start_count   Count of thread to start now, must be lower.
 * @param waiter_count  Maximum count of jobs that can be waiting.
 * @param start         The start routine to activate (can't be NULL)
 * @param arg           Arguments to pass to the start routine
 * @return 0 in case of success or -1 in case of error.
 */
extern int afb_sched_start(
		int allowed_count,
		int start_count,
		int waiter_count,
		void (*start)(int signum, void* arg),
		void *arg);

/**
 * Exit jobs threads and call handler if not NULL.
 */
extern void afb_sched_exit(void (*handler)());

/**
 * Ensure that the current running thread can control the event loop.
 */
extern struct evmgr *afb_sched_acquire_event_manager();

/**
 * adapt the count of thread to the given count of pending jobs
 */
extern void afb_sched_adapt(int pending_job_count);
