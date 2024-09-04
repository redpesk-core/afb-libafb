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

/**
 * This module implement a basic thread manager.
 * Each started (or entered -see below-) thread run
 * the loop below:
 *
 *    while (running) {
 *       sts = getjob(closure, &jobdesc, threadid);
 *       switch (sts) {
 *       default:
 *       case AFB_THREADS_STOP:
 *            running = 0;
 *            break;
 *       case AFB_THREADS_IDLE:
 *            sleep_until_awaken();
 *            break;
 *       case AFB_THREADS_EXEC:
 *            jobdesc.run(jobdesc.job, threadid);
 *            break;
 *       case AFB_THREADS_CONTINUE:
 *            break;
 *       }
 *    }
 *
 * The function for getting the job is given at creation
 * of the thread.
 */

/**
 * Each started thread has a classid. The classid is an
 * integer seen for its bits.
 *
 * The constant AFB_THREAD_ANY_CLASS matches any classid
 */
#define AFB_THREAD_ANY_CLASS (-1)

/**
 * Structure for getting jobs to be executed.
 *
 * When getting a job, the structure must be filled with accurate values
 * if the getter returns a positive number.
 */
struct afb_threads_job_desc
{
	/** the routine to call */
	void (*run)(void *job, x_thread_t tid);

	/** the first argument for 'run' */
	void *job;
};

/**
 * Alias for struct afb_threads_job_desc
 */
typedef struct afb_threads_job_desc afb_threads_job_desc_t;

/** stops running the thread loop */
#define AFB_THREADS_STOP	-1
/** pause running the thread loop */
#define AFB_THREADS_IDLE	0
/** run the job given in the description then continue */
#define AFB_THREADS_EXEC	1
/** continue the thread loop without job */
#define AFB_THREADS_CONTINUE	2

/**
 * The call back for getting the jobs.
 *
 * That function receives:
 *  @param closure     a pointer given when creating the thread
 *  @param desc        a pointer for describing the job to be run
 *  @param tid         the thread id of the current thread
 *
 * It must return:
 *  - 1 (one) for running the job set in the desciption
 *  - a positive value for continuing executing the loop
 *  - a negative value for stopping the thread
 *  - zero for waiting to be awaken
 */
typedef int (*afb_threads_job_getter_t)(void *closure, afb_threads_job_desc_t *desc, x_thread_t tid);

/**
 * Setup the count of threads.
 *
 * If any of the given value is negative, the recorded value is unchanged.
 *
 * @param normal count of thread that can normally be kept alive and waiting
 * @param reserve count of dead thread that can be hold ready
 */
extern void afb_threads_setup_counts(int normal, int reserve);

/**
 * start a thread of classid with a job getter
 *
 * @param classid classid of the started thread
 * @param jobget  the getter function @see afb_threads_job_getter_t
 * @param closure closure for the getter
 *
 * @return 0 on succes or a negative error code
 */
extern int afb_threads_start(int classid, afb_threads_job_getter_t jobget, void *closure);

/**
 * dont start a thread but use the current one to run the loop.
 *
 * @param classid classid of the started thread
 * @param jobget  the getter function @see afb_threads_job_getter_t
 * @param closure closure for the getter
 *
 * @return 0 on succes or a negative error code
 */
extern int afb_threads_enter(int classid, afb_threads_job_getter_t jobget, void *closure);

/**
 * Get the count of active threads.
 *
 * @return the count found
 */
extern int afb_threads_active_count();

/**
 * Wake up one managed threads being sleeping.
 *
 * @return the 1 if one thread awaken or 0 else when none was awake
 */
extern int afb_threads_wakeup_one();

/**
 * Stop all the managed threads.
 *
 */
extern void afb_threads_stop_all();

/**
 * Checks if the given thread is managed
 *
 * @param tid  the thread id of the thread to check
 *
 * @return 1 if the thread is managed or 0 else
 */
extern int afb_threads_has_thread(x_thread_t tid);

/**
 * Checks if the current thread is managed
 *
 * @return 1 if the current thread is managed or 0 else
 */
extern int afb_threads_has_me();

/**
 * Wait until all managed threads matching the classid mask are sleeping.
 * Correctly handles the case of the current thread being managed.
 *
 * @param classid the mask of the threads to wait
 * @param timeout if greater than zero, the timeout in milliseconds to wait.
 *
 * @return 0 on success or a negative error code on timeout
 */
extern int afb_threads_wait_idle(int classid, int timeoutms);
