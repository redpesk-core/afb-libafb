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


#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include <rp-utils/rp-verbose.h>

#include "sys/x-mutex.h"
#include "sys/x-cond.h"
#include "sys/x-thread.h"
#include "sys/x-errno.h"

#include "core/afb-jobs.h"
#include "core/afb-sched.h"
#include "core/afb-threads.h"
#include "core/afb-ev-mgr.h"
#include "sys/ev-mgr.h"
#include "core/afb-sig-monitor.h"

#define AFB_SCHED_WAIT_IDLE_MINIMAL_EXPIRATION	 30 /* thirty seconds */
#define AFB_SCHED_EXITING_EXPIRATION	         10 /* ten seconds */

/**
 * Description of synchronous jobs
 */
struct sync_job
{
	/** next item */
	struct sync_job *next;

	/** identifier */
	uintptr_t id;

	/** done */
	int done;

	/** last signal number */
	int signum;

	/** synchronize mutex */
	x_mutex_t mutex;

	/** synchronize condition */
	x_cond_t condsync;

	/** the entering callback */
	void (*enter)(int signum, void *closure, struct afb_sched_lock *lock);

	/** the argument of the job's callback */
	void *arg;
};

/* synchronisation of threads */
static x_mutex_t mutex = X_MUTEX_INITIALIZER;

/* synchronisation of sync jobs */
static x_mutex_t sync_jobs_mutex = X_MUTEX_INITIALIZER;
static uintptr_t sync_jobs_cptr = 0;
static struct sync_job *sync_jobs_head = 0;

/* exit manager */
struct exiting
{
	void (*handler)(void*);
	void *closure;
	int code;
};
static struct exiting *pexiting = 0;

/* request activity */
#define ACTIVE_JOBS  1
#define ACTIVE_EVMGR 2
static int8_t activity = 0;

/**
 * run the event loop
 */
static void evloop_sig_run(int signum, void *closure)
{
	if (signum) {
		RP_ERROR("Signal %s catched in evloop", strsignal(signum));
		afb_ev_mgr_try_recover_for_me();
	}
	else {
		int delayms = (int)(intptr_t)closure;
		afb_ev_mgr_prepare_wait_dispatch(delayms, 0);
	}
}

static void run_one_job(void *arg, x_thread_t tid)
{
	struct afb_job *job = arg;
	afb_jobs_run(job);
	afb_ev_mgr_release(tid);
}

static void run_ev_loop(void *arg, x_thread_t tid)
{
	afb_sig_monitor_run(0, evloop_sig_run, arg);
}

static int get_job_cb(void *closure, afb_threads_job_desc_t *desc, x_thread_t tid)
{
	struct afb_job *job;
	long delayms = 0;

	/* priority is to execute jobs */
	if (activity & ACTIVE_JOBS) {
		job = afb_jobs_dequeue(&delayms);
		if (job) {
			afb_ev_mgr_release(tid);
			desc->run = run_one_job;
			desc->job = job;
			return AFB_THREADS_EXEC;
		}
	}

	/* should handle the event loop? */
	if (activity & ACTIVE_EVMGR) {
		if (afb_ev_mgr_try_get(tid) != NULL) {
			/* yes, handle the event loop */
			desc->run = run_ev_loop;
			desc->job = (void*)(intptr_t)delayms;
			return AFB_THREADS_EXEC;
		}
	}

	/* nothing to do, idle */
	afb_ev_mgr_release(tid);
	return AFB_THREADS_IDLE;
}

static int start_one_thread(enum afb_sched_mode mode)
{
	return afb_threads_start_cond(get_job_cb, NULL, mode == Afb_Sched_Mode_Start);
}

/**
 * Adapt the current threading to current job requirement
 */
static void adapt(enum afb_sched_mode mode)
{
	if (activity & ACTIVE_JOBS)
		if (!afb_threads_wakeup_one())
			start_one_thread(mode);
}

/* Schedule the given job */
int afb_sched_post_job(
	const void *group,
	long delayms,
	int timeout,
	void (*callback)(int, void*),
	void *arg,
	enum afb_sched_mode mode
) {
	int rc;
	rc = afb_jobs_post(group, delayms, timeout, callback, arg);
	if (rc >= 0) {
		adapt(mode);
		if (delayms)
			afb_ev_mgr_wakeup();
	}
	return rc;
}

/* Schedule the given job */
int afb_sched_post_job2(
	const void *group,
	long delayms,
	int timeout,
	void (*callback)(int, void*, void*),
	void *arg1,
	void *arg2,
	enum afb_sched_mode mode
) {
	int rc;
	rc = afb_jobs_post2(group, delayms, timeout, callback, arg1, arg2);
	if (rc >= 0) {
		adapt(mode);
		if (delayms)
			afb_ev_mgr_wakeup();
	}
	return rc;
}

/* Schedule the given job */
int afb_sched_abort_job(int jobid)
{
	return afb_jobs_abort(jobid);
}

/**
 * @brief get the sync_job of id, optionaly unlink it, and return it
 *
 * @param id id to get
 *
 * @return struct sync_job* the job or NULL if not found
 */
static struct sync_job *get_sync_job(uintptr_t id)
{
	struct sync_job *sync = sync_jobs_head;
	while (sync != NULL && sync->id != id)
		sync = sync->next;
	return sync;
}

/**
 * Internal helper function for 'afb_sched_sync'.
 * @see afb_sched_sync, afb_sched_leave
 */
static void sync_cb(int signum, void *closure)
{
	struct sync_job *sync = closure;

	sync->signum = signum;
	sync->enter(signum, sync->arg, (struct afb_sched_lock*)sync->id);
	if (signum == 0) {
		x_mutex_lock(&sync->mutex);
		if (sync->done == 0) {
			afb_ev_mgr_release_for_me();
			adapt(Afb_Sched_Mode_Start);
			x_cond_wait(&sync->condsync, &sync->mutex);
		}
	}
	/* always unlock for ensuring destruction of mutex works even when signum != 0 */
	x_mutex_unlock(&sync->mutex);
}

/**
 * Enter a synchronisation point: activates the job given by 'callback'
 * and 'closure' using 'timeout' to control execution time.
 *
 * The given job callback receives 3 parameters:
 *   - 'signum': 0 on start but if a signal is caugth, its signal number
 *   - 'closure': closure data for the callback
 *   - 'lock': the lock to pass to 'afb_sched_leave' to release the synchronisation
 *
 * @param timeout the time in seconds or zero for no timeout
 * @param callback the callback that will handle the job.
 * @param closure the argument to the callback
 *
 * @return 0 on success or -1 in case of error
 */
int afb_sched_sync(
		int timeout,
		void (*callback)(int signum, void *closure, struct afb_sched_lock *lock),
		void *closure
) {
	struct sync_job sync, **itsync;

	/* init the structure */
	sync.enter = callback;
	sync.arg = closure;
	sync.signum = 0;
	sync.done = 0;
	sync.condsync = (x_cond_t) X_COND_INITIALIZER;
	sync.mutex = (x_mutex_t) X_MUTEX_INITIALIZER;

	/* link the structure */
	x_mutex_lock(&sync_jobs_mutex);
	sync.id = ++sync_jobs_cptr;
	sync.next = sync_jobs_head;
	sync_jobs_head = &sync;
	x_mutex_unlock(&sync_jobs_mutex);

	/* call the function with a timeout */
	afb_sig_monitor_run(timeout, sync_cb, &sync);

	x_mutex_lock(&sync_jobs_mutex);
	itsync = &sync_jobs_head;
	while (*itsync != &sync)
		itsync = &(*itsync)->next;
	*itsync = sync.next;
	x_mutex_unlock(&sync_jobs_mutex);

	/* release the sync data */
	if (x_cond_destroy(&sync.condsync) != 0)
		RP_CRITICAL("failed to destroy condition");
	if (x_mutex_destroy(&sync.mutex) != 0)
		RP_CRITICAL("failed to destroy mutex");

	return sync.done ? 0 : sync.signum ? X_EINTR : X_ECANCELED;
}

/**
 * Unlocks the execution flow designed by 'jobloop'.
 * @param afb_sched_lock indication of the flow to unlock
 * @return 0 in case of success of -1 on error
 */
int afb_sched_leave(struct afb_sched_lock *lock)
{
	int rc;
	struct sync_job *sync;

	x_mutex_lock(&sync_jobs_mutex);
	sync = get_sync_job((uintptr_t)lock);
	if (sync == NULL) {
		x_mutex_unlock(&sync_jobs_mutex);
		rc = X_ENOENT;
	}
	else if (sync->signum) {
		x_mutex_unlock(&sync_jobs_mutex);
		rc = X_EINTR;
	}
	else {
		x_mutex_lock(&sync->mutex);
		x_mutex_unlock(&sync_jobs_mutex);
		if (sync->done != 0)
			rc = X_EEXIST;
		else {
			sync->done = 1;
			x_cond_signal(&sync->condsync);
			rc = 0;
		}
		x_mutex_unlock(&sync->mutex);
	}

	return rc;
}

/* return the argument of the sched_enter */
void *afb_sched_lock_arg(struct afb_sched_lock *lock)
{
	void *result = NULL;
	struct sync_job *sync;

	x_mutex_lock(&sync_jobs_mutex);
	sync = get_sync_job((uintptr_t)lock);
	if (sync != NULL) {
		x_mutex_lock(&sync->mutex);
		result = sync->arg;
		x_mutex_unlock(&sync->mutex);
	}
	x_mutex_unlock(&sync_jobs_mutex);
	return result;
}

#if WITH_DEPRECATED_OLDER_THAN_5_1
/**
 * Legacy function
 */
int afb_sched_enter(
		const void *group,
		int timeout,
		void (*callback)(int signum, void *closure, struct afb_sched_lock *afb_sched_lock),
		void *closure
) {
	RP_NOTICE("Legacy afb_sched_enter called!");
	if (group != NULL)
		return X_EINVAL;

	return afb_sched_sync(timeout, callback, closure);
}
#endif

/* wait that no thread is running jobs */
int afb_sched_wait_idle(int wait_jobs, int timeout)
{
	struct timespec expire;
	int has_me, result = 0;

	/* release the event loop */
	afb_ev_mgr_release_for_me();

	/* start a thread for processing */
	has_me = afb_threads_has_me();
	if (afb_threads_active_count() <= has_me)
		start_one_thread(Afb_Sched_Mode_Start);

	/* compute the expiration */
	clock_gettime(CLOCK_REALTIME, &expire);
	expire.tv_sec += timeout > 0 ? timeout : AFB_SCHED_WAIT_IDLE_MINIMAL_EXPIRATION;

	/* wait for job completion */
	while (result == 0 && wait_jobs != 0 && afb_jobs_get_pending_count() > 0) {
		adapt(Afb_Sched_Mode_Start); /* ensure someone process the job */
		result = afb_threads_wait_new_asleep(&expire);
	}

	/* wait for idle completion */
	if (result == 0) {
		activity = 0;
		afb_ev_mgr_wakeup();
		while (result == 0 && afb_threads_active_count() > afb_threads_asleep_count() + has_me)
			result = afb_threads_wait_new_asleep(&expire);
		activity = ACTIVE_JOBS | ACTIVE_EVMGR;
	}
	return result;
}

/* Exit threads and call handler if not NULL. */
void afb_sched_exit(int force, void (*handler)(void*), void *closure, int exitcode)
{
	x_mutex_lock(&mutex);
	if (pexiting) {
	        /* record handler and exit code */
		pexiting->handler = handler;
		pexiting->closure = closure;
		pexiting->code = exitcode;
		pexiting = 0;
	}
	x_mutex_unlock(&mutex);

	/* stop */
	if (!force)
		afb_sched_wait_idle(1, AFB_SCHED_EXITING_EXPIRATION);
	activity = 0;
	afb_threads_stop_all();
	afb_ev_mgr_release_for_me();
	afb_ev_mgr_wakeup();
}

/* Enter the jobs processing loop */
int afb_sched_start(
	int allowed_count,
	int start_count,
	int max_jobs_count,
	void (*start)(int signum, void* arg),
	void *arg)
{
	struct exiting exiting = { 0, 0, 0 };

	assert(allowed_count >= 1);
	assert(start_count >= 0);
	assert(max_jobs_count > 0);
	assert(start_count <= allowed_count);

	afb_ev_mgr_init();

	x_mutex_lock(&mutex);

	/* check whether already running */
	if (pexiting != NULL) {
		RP_ERROR("sched already started");
		x_mutex_unlock(&mutex);
		return X_EBUSY;
	}
	pexiting = &exiting;

	/* records the allowed count */
	afb_jobs_set_max_count(max_jobs_count);
	afb_threads_setup_counts(allowed_count, -1);
	activity = ACTIVE_JOBS | ACTIVE_EVMGR;

	/* start at least one thread: the current one */
	while (afb_threads_active_count() + 1 < start_count) {
		exiting.code = start_one_thread(Afb_Sched_Mode_Start);
		if (exiting.code != 0) {
			RP_ERROR("Not all threads can be started");
			goto error;
		}
	}

	/* queue the start job */
	exiting.code = afb_sched_post_job(NULL, 0, 0, start, arg, Afb_Sched_Mode_Normal);
	if (exiting.code < 0)
		goto error;

	/* run until end */
	x_mutex_unlock(&mutex);
	afb_threads_enter(get_job_cb, NULL);
	afb_ev_mgr_release_for_me();
	x_mutex_lock(&mutex);
error:
	pexiting = 0;
	x_mutex_unlock(&mutex);
	afb_threads_setup_counts(0, -1);
	afb_threads_stop_all();
	if (exiting.handler) {
		exiting.handler(exiting.closure);
		exiting.handler = 0;
	}
	return exiting.code;
}
