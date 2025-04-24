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

#include "../libafb-config.h"

#include <time.h>

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
 * start a thread
 *
 * @return 0 on succes or a negative error code
 */
extern int afb_threads_start();

/**
 * start a thread but don't start it if
 * force == 0 and the normal_count of threads is already active
 *
 * @param force   enforce starting a thread
 *
 * @return 0 on succes or a negative error code
 */
extern int afb_threads_start_cond(int force);

/**
 * enter thread dispatch of jobs
 *
 * @param jobget  the getter function @see afb_threads_job_getter_t
 * @param closure closure for the getter
 *
 * @return 0 on succes or a negative error code
 */
extern int afb_threads_enter(afb_threads_job_getter_t jobget, void *closure);

/**
 * Stop all the managed threads.
 *
 * @param wait if not zero, wait all threads stopped
 */
extern void afb_threads_stop_all(int wait);

/**
 * Wake up all threads
 */
extern void afb_threads_wakeup();

/**
 * wait for expiration, not zero test result
 * Calls test and if it returns a not zero value, return it
 * When test returns 0, wait until the state change or expiration
 *
 * @param test    function to call
 * @param closure closure to the function to call
 * @param expire  the absolute expiration if not NULL
 *
 * @return the not zero value returned by test or on expiration X_ETIMEDOUT
 */
extern int afb_threads_wait_until(int (*test)(void *clo), void *closure, struct timespec *expire);

/**
 * Wait until every thread is idle
 *
 * @param expire  the absolute expiration if not NULL
 *
 * @return the not zero value returned by test or on expiration X_ETIMEDOUT
 */
extern int afb_threads_wait_idle(struct timespec *expire);

/**
 * deprecated, get current active count
 */
extern int afb_threads_active_count();

/**
 * deprecated, get current asleep count
 */
extern int afb_threads_asleep_count();


