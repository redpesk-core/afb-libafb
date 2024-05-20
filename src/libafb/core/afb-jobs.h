/*
 * Copyright (C) 2015-2024 IoT.bzh Company
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

struct afb_job;

/**
 * Queues a new asynchronous job represented by 'callback' and 'arg'
 * for the 'group' and the 'timeout'.
 * Jobs are queued in a FIFO (first in first out) structure.
 * They are dequeued by arrival order.
 * The group if not NULL is used to group jobs of that same group
 * sequentially. This is of importance if jobs are executed in
 * parallel concurrently.
 *
 * @param group    The group of the job or NULL when no group.
 * @param delayms  Minimal delay in ms before starting the job
 * @param timeout  The maximum execution time in seconds of the job
 *                 or 0 for unlimited time.
 * @param callback The function to execute for achieving the job.
 *                 Its first parameter is either 0 on normal flow
 *                 or the signal number that broke the normal flow.
 *                 The remaining parameter is the parameter 'arg'
 *                 given here.
 * @param arg      The second argument for 'callback'
 *
 * @return the id of the job, greater than zero, or in case
 *         of error a negative number in -errno like form
 */
extern int afb_jobs_post(
		const void *group,
		long delayms,
		int timeout,
		void (*callback)(int, void*),
		void *arg);

/**
 * Queues a new asynchronous job represented by 'callback' and 'arg'
 * for the 'group' and the 'timeout'.
 * Jobs are queued in a FIFO (first in first out) structure.
 * They are dequeued by arrival order.
 * The group if not NULL is used to group jobs of that same group
 * sequentially. This is of importance if jobs are executed in
 * parallel concurrently.
 *
 * @param group    The group of the job or NULL when no group.
 * @param delayms  Minimal delay in ms before starting the job
 * @param timeout  The maximum execution time in seconds of the job
 *                 or 0 for unlimited time.
 * @param callback The function to execute for achieving the job.
 *                 Its first parameter is either 0 on normal flow
 *                 or the signal number that broke the normal flow.
 *                 The remaining parameters are the parameters 'arg1'
 *                 and 'arg2' given here.
 * @param arg1     The second argument for 'callback'
 * @param arg2     The third argument for 'callback'
 *
 * @return the id of the job, greater than zero, or in case
 *         of error a negative number in -errno like form
 */
extern int afb_jobs_post2(
		const void *group,
		long delayms,
		int timeout,
		void (*callback)(int, void*, void*),
		void *arg1,
		void *arg2);

/**
 * Get the next job to process or NULL if none, i.e.
 * if all jobs are blocked or if no job exists.
 *
 * Once gotten, the job must be either run or cancelled.
 *
 * @param delayms if the result is 0 (no job), the long pointed
 *                receives a timeout in ms that have to be waiten.
 *
 * @return the first job that isn't blocked or NULL
 */
extern struct afb_job *afb_jobs_dequeue(long *delayms);

/**
 * Get in 'jobs' at most 'njobs' available to start immediately.
 * Returns the count of jobs that can be started immediately.
 * The returned value can be greater than the given count 'njobs',
 * Allowing to pass a value of zero njobs and get in return the count
 * of available jobs.
 *
 * If not NULL, the value pointed by delayms is filled with the delay
 * in milliseconds of the first delayed job to be run.
 *
 * Once gotten, the returned jobs must be either run or cancelled.
 *
 * @param jobs an array for storing jobs found to be run immediately
 * @param njobs the count of jobs that can be stored in the array jobs
 * @param delayms the long pointed receives the delay in ms before first delayed job.
 *
 * @return the first job that isn't blocked or NULL
 */
extern int afb_jobs_dequeue_multiple(struct afb_job **jobs, int njobs, long *delayms);

/**
 * Run the given job now.
 *
 * @param job   a job a retrieved with afb_jobs_dequeue
 */
extern void afb_jobs_run(struct afb_job *job);

/**
 * Cancel the job gotten by afb_jobs_dequeue.
 *
 * The callback function is called with the signal
 * SIGABRT.
 *
 * @param job   a job a retrieved with afb_jobs_dequeue
 */
extern void afb_jobs_cancel(struct afb_job *job);

/**
 * Abort the job of the given id.
 *
 * The callback function is called with the signal
 * SIGABRT.
 *
 * @param jobid the job id as returned by afb_jobs_post
 *
 * @return zero on success or a negative code
 *  X_ENOENT if invalid jobid or X_EBUSY if in progress
 */
extern int afb_jobs_abort(int jobid);

/**
 * Get the current count of pending job
 * @return the current count of pending jobs
 */
extern int afb_jobs_get_pending_count(void);

/**
 * Get the maximum count of pending job
 *
 * @return the maximum count of job
 */
extern int afb_jobs_get_max_count(void);

/**
 * Set the maximum count of pending jobs to 'count'
 *
 * @param count the count to set
 */
extern void afb_jobs_set_max_count(int count);

/**
 * Get the count of job still active but not pending
 *
 * @return the count of active jobs
 */
extern int afb_jobs_get_active_count(void);

#if WITH_TRACK_JOB_CALL
/**
 * Check if the group if pending in the job stack of the thread
 *
 * @param group the group to be checked
 *
 * @return 1 if the group is in the stack of jobs of the thread
 */
extern int afb_jobs_check_group(void *group);
#endif