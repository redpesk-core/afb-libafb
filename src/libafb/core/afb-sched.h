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

#pragma once

struct afb_sched_lock;
struct ev_mgr;

/**
 * Scheduling mode for posting a job using 'afb_sched_post_job'
 */
enum afb_sched_mode
{
	Afb_Sched_Mode_Normal,	/**< don't start a new thread */
	Afb_Sched_Mode_Start	/**< enforce a thread start if needed */
};

/**
 * Enter a synchronisation point: activates the job given by 'callback'
 * and 'closure' using 'timeout' to control execution time.
 *
 * The given job callback receives 3 parameters:
 *   - 'signum': 0 on start but if a signal is caugth, its signal number
 *   - 'closure': closure data for the callback
 *   - 'lock': the lock to pass to 'afb_sched_leave' to release the synchronisation
 *
 * @param timeout the time in seconds allocated to the job
 * @param callback the callback that will handle the job.
 * @param closure the argument to the callback
 *
 * @return 0 on success or -1 in case of error
 */
extern int afb_sched_sync(
		int timeout,
		void (*callback)(int signum, void *closure, struct afb_sched_lock *lock),
		void *closure);

#undef WITH_DEPRECATED_OLDER_THAN_5_1
#define WITH_DEPRECATED_OLDER_THAN_5_1   1
#if WITH_DEPRECATED_OLDER_THAN_5_1
/**
 * Deprecated function, use 'afb_sched_sync' instead.
 * group must be NULL.
 */
extern int afb_sched_enter(
		const void *group,
		int timeout,
		void (*callback)(int signum, void *closure, struct afb_sched_lock *afb_sched_lock),
		void *closure)
	__attribute__ ((deprecated("deprecated since 5.1.0, use afb_sched_sync as replacement")));
#endif

/**
 * Unlocks the execution flow locked by 'lock'.
 * Execution flows are locked by 'afb_sched_enter'.
 *
 * @param lock indication of the execution flow to unlock
 * @return 0 in case of success of -1 on error
 */
extern int afb_sched_leave(struct afb_sched_lock *lock);

/**
 * Get the arguments of the execution flow locked by 'lock'.
 * Execution flows are locked by 'afb_sched_enter'.
 * If the execution flow is already unlocked, return NULL.
 *
 * @param lock indication of the execution flow
 * @return the argument 'closure' passed to 'afb_sched_enter' or NULL
 */
extern void *afb_sched_lock_arg(struct afb_sched_lock *lock);

/**
 * Enter the jobs processing loop.
 *
 * When entered, the job processing loop does not return until
 * the function @see afb_sched_exit is called.
 *
 * @param allowed_count  Maximum count of thread for jobs including this one
 * @param start_count    Count of thread to start now, must be lower.
 * @param max_jobs_count Maximum count of jobs that can be waiting.
 * @param start          The start routine to activate (can't be NULL)
 * @param arg            Arguments to pass to the start routine
 *
 * @return in case of success or -1 in case of error.
 */
extern int afb_sched_start(
		int allowed_count,
		int start_count,
		int max_jobs_count,
		void (*start)(int signum, void* arg),
		void *arg);

/**
 * Exit jobs threads and call handler if not NULL.
 *
 * @param force    If zero, the exit occurs when there is no more pending
 *                 jobs. Otherwise, pending jobs are no more processed.
 * @param handler  A function called when threads have stopped
 *                 and before the main thread (the one that called
 *                 'afb_sched_start') returns.
 * @param closure  The closure for the handler
 * @param exitcode Code to be returned by exited @see afb_sched_start
 */
extern void afb_sched_exit(int force, void (*handler)(void *closure), void *closure, int exitcode);

/**
 * Schedule a new asynchronous job represented by 'callback' and 'arg'
 * for the 'group' and the 'timeout'.
 *
 * Jobs are queued FIFO and are possibly executed in parallel
 * concurrently except for job of the same group that are
 * executed sequentially in FIFO order.
 *
 * @param group    The group of the job or NULL when no group.
 * @param delayms  minimal time in ms to wait before starting the job
 * @param timeout  The maximum execution time in seconds of the job
 *                 or 0 for unlimited time.
 * @param callback The function to execute for achieving the job.
 *                 Its first parameter is either 0 on normal flow
 *                 or the signal number that broke the normal flow.
 *                 The remaining parameter is the parameter 'arg1'
 *                 given here.
 * @param arg      The second argument for 'callback'
 * @param mode     The mode
 *
 * @return the job id on success (greater than 0) or
 *         in case of error a negative number in -errno like form
 */
extern int afb_sched_post_job(
		const void *group,
		long delayms,
		int timeout,
		void (*callback)(int, void*),
		void *arg,
		enum afb_sched_mode mode);

/**
 * Schedule a new asynchronous job represented by 'callback' and 'arg'
 * for the 'group' and the 'timeout'.
 *
 * Jobs are queued FIFO and are possibly executed in parallel
 * concurrently except for job of the same group that are
 * executed sequentially in FIFO order.
 *
 * @param group    The group of the job or NULL when no group.
 * @param delayms  minimal time in ms to wait before starting the job
 * @param timeout  The maximum execution time in seconds of the job
 *                 or 0 for unlimited time.
 * @param callback The function to execute for achieving the job.
 *                 Its first parameter is either 0 on normal flow
 *                 or the signal number that broke the normal flow.
 *                 The remaining parameters are the parameters 'arg1'
 *                 and 'arg2' given here.
 * @param arg1     The second argument for 'callback'
 * @param arg2     The third argument for 'callback'
 * @param mode     The mode
 *
 * @return the job id on success (greater than 0) or
 *         in case of error a negative number in -errno like form
 */
extern int afb_sched_post_job2(
		const void *group,
		long delayms,
		int timeout,
		void (*callback)(int, void*, void*),
		void *arg1,
		void *arg2,
		enum afb_sched_mode mode);

/**
 * Aborts the job of given id, if not started, the job receives SIGABORT
 *
 * @param jobid the jobid to abort
 *
 * @return 0 on success or a negative error code
 */
extern int afb_sched_abort_job(int jobid);

/**
 * Calls synchronously in the current thread the job represented
 * by 'callback' and 'arg'.
 * The advantage of calling this function intead of calling
 *       afb_sig_monitor_run(0, callback, arg)
 * directly is that this function takes care of the fact that
 * it can lock the thread for a while. Consequently, this
 * function release the event loop if it held it and remove
 * the current thread from the thread of threads handling
 * asynchronous jobs.
 *
 * @param timeout  The timeout of achievment in second (0 no timeout)
 * @param callback The function to execute for achieving the job.
 *                 Its first parameter is either 0 on normal flow
 *                 or the signal number that broke the normal flow.
 *                 The remaining parameter is the parameter 'arg'
 *                 given here.
 * @param arg      The second argument for 'callback'
 * @param mode     Scheduling policy
 */
extern void afb_sched_call(
		int timeout,
		void (*callback)(int, void*),
		void *arg,
		enum afb_sched_mode mode
);

/**
 * Wait that every running thread are in waiting state.
 * One of the thread can be in event loop, waiting for some
 * event, while the other threads are just idled.
 *
 * This means that if the scheduler is waiting for a timeout
 * to start a job, the job queue might be not empty. To ensure
 * that the job queue is empty set 'wait_jobs' to 1
 *
 * If the scheduler is not started, this function can be used
 * to wait achievement of all automatically started threads.
 *
 * If wait_jobs is not zero and not thread is started, the
 * routine starts at least one thread to process the jobs.
 *
 * @param wait_jobs if not zero, wait completion of all jobs
 * @param timeout the timeout in seconds (negative for infinite)
 *
 * @return -1 if timeout or the count of pending jobs
 */
extern int afb_sched_wait_idle(int wait_jobs, int timeout);
