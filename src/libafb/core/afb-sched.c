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

#define CLASSID_MAIN   1
#define CLASSID_OTHERS 2
#define CLASSID_EXTRA  4
#define ANY_CLASSID    AFB_THREAD_ANY_CLASS

/**
 * Description of synchronous jobs
 */
struct sync_job
{
	/** next item */
	struct sync_job *next;

	/** identifier */
	uintptr_t id;

	/** status */
	int status;

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

/* counts for threads */
static int allowed_thread_count = 0;	/**< allowed count of threads */

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
		afb_ev_mgr_prepare_wait_dispatch_release(delayms);
	}
}

static int get_job_cb(void *closure, afb_threads_job_desc_t *desc, x_thread_t tid)
{
	struct afb_job *job;
	long delayms;
	int classid = (int)(intptr_t)closure;
	int rc = AFB_THREADS_IDLE;

	job = afb_jobs_dequeue(&delayms);
	if (job) {
		afb_jobs_run(job);
		afb_ev_mgr_release(tid);
		rc = AFB_THREADS_CONTINUE;
	}
	else if (classid == CLASSID_EXTRA || allowed_thread_count == 0)
		rc = AFB_THREADS_STOP;
	else if (afb_ev_mgr_try_get(tid) != NULL) {
		afb_sig_monitor_run(0, evloop_sig_run, (void*)(intptr_t)delayms);
		rc = AFB_THREADS_CONTINUE;
	}
	return rc;
}

static int start_one_thread(enum afb_sched_mode mode)
{
	int classid;
	if (afb_threads_active_count(ANY_CLASSID) < allowed_thread_count)
		classid = CLASSID_OTHERS;
	else if (mode == Afb_Sched_Mode_Start)
		classid = CLASSID_EXTRA;
	else {
		afb_ev_mgr_wakeup();
		return 0;
	}
	return afb_threads_start(classid, get_job_cb, (void*)(intptr_t)classid);
}

/**
 * Adapt the current threading to current job requirement
 */
static void adapt(enum afb_sched_mode mode)
{
	if (!afb_threads_wakeup(ANY_CLASSID, 1))
		start_one_thread(mode);
}

/* an event manager has no thread */
void afb_sched_ev_mgr_unheld()
{
	adapt(Afb_Sched_Mode_Normal);
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
	x_mutex_lock(&mutex);
	rc = afb_jobs_post(group, delayms, timeout, callback, arg);
	if (rc >= 0) {
		adapt(mode); //delayms > 0);
		afb_ev_mgr_wakeup();
	}
	x_mutex_unlock(&mutex);
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
	x_mutex_lock(&mutex);
	rc = afb_jobs_post2(group, delayms, timeout, callback, arg1, arg2);
	if (rc >= 0) {
		adapt(mode); //delayms > 0);
		afb_ev_mgr_wakeup();
	}
	x_mutex_unlock(&mutex);
	return rc;
}

/* Schedule the given job */
int afb_sched_abort_job(int jobid)
{
	return afb_jobs_abort(jobid);
}

/* call a monitored routine synchronousely, taking care of releasing event loop */
void afb_sched_call(
		int timeout,
		void (*callback)(int, void*),
		void *arg,
		enum afb_sched_mode mode
) {
	if (afb_ev_mgr_release_for_me())
		adapt(mode);
	afb_sig_monitor_run(timeout, callback, arg);
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
	sync->enter(signum, sync->arg, (struct afb_sched_lock*)sync->id);
	if (signum != 0) {
		x_mutex_lock(&sync->mutex);
		if (sync->status == 0) {
			sync->status = signum == SIGVTALRM ? X_ETIMEDOUT : X_EINTR;
			x_cond_signal(&sync->condsync);
		}
		x_mutex_unlock(&sync->mutex);
	}
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
	int rc;
	struct sync_job sync, *ps;
	struct timespec ts, *pts;

	/* init the structure */
	sync.enter = callback;
	sync.arg = closure;
	sync.status = 0;
	x_mutex_init(&sync.mutex);
	x_cond_init(&sync.condsync);

	/* link the structure */
	x_mutex_lock(&sync_jobs_mutex);
	sync.id = ++sync_jobs_cptr;
	sync.next = sync_jobs_head;
	sync_jobs_head = &sync;
	x_mutex_unlock(&sync_jobs_mutex);

	/* prepare timeout */
	if (!timeout)
		pts = NULL;
	else {
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += timeout;
		pts = &ts;
	}

	/* call the function */
	afb_sched_call(timeout, sync_cb, &sync, Afb_Sched_Mode_Start);

	/* wait unlock */
	x_mutex_lock(&sync.mutex);
	if (sync.status != 0)
		rc = sync.status < 0 ? sync.status : 0;
	else {
		if (pts == NULL)
			rc = x_cond_wait(&sync.condsync, &sync.mutex);
		else
			rc = x_cond_timedwait(&sync.condsync, &sync.mutex, pts);
		rc = rc < 0 ? -errno : sync.status == 0 ? X_ETIMEDOUT : 0;
	}
	x_mutex_unlock(&sync.mutex);

	/* unlink the structure */
	x_mutex_lock(&sync_jobs_mutex);
	ps = sync_jobs_head;
	if (ps == &sync)
		sync_jobs_head = sync.next;
	else {
		while (ps->next != &sync)
			ps = ps->next;
		ps->next = sync.next;
	}
	x_mutex_unlock(&sync_jobs_mutex);

	return rc;
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
	if (sync == NULL)
		rc = X_ENOENT;
	else {
		x_mutex_lock(&sync->mutex);
		if (sync->status != 0)
			rc = X_EINVAL;
		else {
			sync->status = 1;
			rc = x_cond_signal(&sync->condsync);
			if (rc < 0)
				rc = -errno;
		}
		x_mutex_unlock(&sync->mutex);
	}
	x_mutex_unlock(&sync_jobs_mutex);

	return rc;
}

/* return the argument of the sched_enter */
void *afb_sched_lock_arg(struct afb_sched_lock *lock)
{
	void *result;
	struct sync_job *sync;

	x_mutex_lock(&sync_jobs_mutex);
	sync = get_sync_job((uintptr_t)lock);
	result = sync == NULL ? NULL : sync->arg;
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
	if (group != NULL)
		return X_EINVAL;

	return afb_sched_sync(timeout, callback, closure);
}
#endif

/* wait that no thread is running jobs */
int afb_sched_wait_idle(int wait_jobs, int timeout)
{
	if (afb_threads_active_count(ANY_CLASSID) <= afb_threads_has_me())
		start_one_thread(Afb_Sched_Mode_Start);
	return afb_threads_wait_idle(ANY_CLASSID, timeout * 1000);
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

	/* disallow start */
	allowed_thread_count = 0;
	x_mutex_unlock(&mutex);

	/* release the evloop */
	afb_ev_mgr_release_for_me();
	afb_ev_mgr_wakeup();

	/* stop */
	if (!force)
		afb_sched_wait_idle(1,1);
	afb_threads_stop(ANY_CLASSID, INT_MAX);
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
	pexiting = &exiting;

	/* check whether already running */
	if (allowed_thread_count) {
		RP_ERROR("sched already started");
		exiting.code = X_EINVAL;
		goto error;
	}

	/* records the allowed count */
	allowed_thread_count = allowed_count;
	afb_jobs_set_max_count(max_jobs_count);

	/* start at least one thread: the current one */
	while (afb_threads_active_count(CLASSID_OTHERS) + 1 < start_count) {
		exiting.code = start_one_thread(Afb_Sched_Mode_Start);
		if (exiting.code != 0) {
			RP_ERROR("Not all threads can be started");
			allowed_thread_count = 0;
			afb_threads_stop(CLASSID_OTHERS, INT_MAX);
			goto error;
		}
	}

	/* queue the start job */
	exiting.code = afb_jobs_post(NULL, 0, 0, start, arg);
	if (exiting.code < 0)
		goto error;

	/* run until end */
	x_mutex_unlock(&mutex);
	afb_threads_enter(CLASSID_MAIN, get_job_cb, (void*)(intptr_t)CLASSID_MAIN);
	x_mutex_lock(&mutex);
error:
	pexiting = 0;
	allowed_thread_count = 0;
	x_mutex_unlock(&mutex);
	if (exiting.handler) {
		exiting.handler(exiting.closure);
		exiting.handler = 0;
	}
	return exiting.code;
}
