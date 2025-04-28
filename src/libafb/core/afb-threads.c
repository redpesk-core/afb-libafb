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
#include <string.h>
#include <time.h>

#include <rp-utils/rp-verbose.h>

#include "sys/x-mutex.h"
#include "sys/x-cond.h"
#include "sys/x-thread.h"
#include "sys/x-errno.h"

#include "core/afb-sig-monitor.h"
#include "core/afb-threads.h"
#include "core/afb-ev-mgr.h"

#ifndef AFB_THREADS_DEFAULT_RESERVE_COUNT
#define AFB_THREADS_DEFAULT_RESERVE_COUNT 4
#endif

#define DEBUGGING 0
#if DEBUGGING
#include <stdio.h>
#define PRINT(...) fprintf(stderr,__VA_ARGS__)
#define IFDBG(...) __VA_ARGS__
#else
#define IFDBG(...)
#define PRINT(...)
#endif

/** Description of threads */
struct thread
{
	/** next thread of the list */
	struct thread *next;

	/** thread id */
	x_thread_t tid;

	/** stop request */
	unsigned char stopped;

	/** next waiter of the waiter list */
	struct thread *next_asleep;

	/** synchronisation with the thread */
	x_cond_t  cond;

	/* an id if debugging */
	IFDBG(unsigned id;)
};

/***********************************************************************/

/** count of allowed threads */
static int normal_count = 1;

/** synchronisation of running threads for job acquisition */
static x_mutex_t run_lock = X_MUTEX_INITIALIZER;

static x_cond_t *asleep_waiter_cond = 0;

/** list of active threads */
static struct thread *threads = 0;

/** count of active threads */
static int active_count = 0;

/***********************************************************************
* asleep threads will wait for being woken up
*/

/** synchronisation of wakeup/asleep management */
static x_mutex_t asleep_lock = X_MUTEX_INITIALIZER;

/** lifo of asleep threads */
static struct thread *asleep_threads = 0;

/** count of asleep threads */
static int asleep_count = 0;

/***********************************************************************
* Reserve is a reserved already started threads but not
* active. These thread structures are stored in the list
* headed by `reserve_head` and counted using `current_reserve_count`.
*/

/** count of thread allowed to wait in reserve */
static int reserve_count = AFB_THREADS_DEFAULT_RESERVE_COUNT;

/** current count of thread in reserve */
static int current_reserve_count = 0;

/** head of the list of threads in reserve */
static struct thread *reserve_head = 0;

/** lock for managing reserve of threads */
static x_mutex_t reserve_lock = X_MUTEX_INITIALIZER;

/***********************************************************************/

static afb_threads_job_getter_t getjob = NULL;
static void *getjobcls = NULL;

/***********************************************************************/

typedef int (*afb_threads_job_getter_t)(void *closure, afb_threads_job_desc_t *desc, x_thread_t tid);
typedef int (*afb_threads_job_getter_t)(void *closure, afb_threads_job_desc_t *desc, x_thread_t tid);

/**
 * Removes @ref thr from the list of active threads.
 * The mutex @ref run_lock must be held when this function is called.
 * The value of active_count is unchanged because it should be done in @ref stop
 */
static void unlink_thread(struct thread *thr)
{
	struct thread *ithr, **pthr;
	for (pthr = &threads ; (ithr = *pthr) ; pthr = &ithr->next) {
		if (ithr == thr) {
			*pthr = thr->next;
			break;
		}
	}
}

static void thread_run(struct thread *me, afb_threads_job_getter_t mainjob)
{
	int status;
	afb_threads_job_desc_t jobdesc;

IFDBG(static unsigned id = 0; me->id = ++id;)

PRINT("++++++++++++ START[%u] %p\n",me->id,me);

	x_mutex_lock(&run_lock);
	me->next = threads;
	threads = me;
	active_count++;
	if (mainjob)
		getjob = mainjob;
	do {
		/* get a job */
		status = getjob ? getjob(getjobcls, &jobdesc, me->tid) : AFB_THREADS_IDLE;
		switch (status) {
		case AFB_THREADS_CONTINUE:
			/* continue the loop */
			break;

		case AFB_THREADS_EXEC:
			/* execute the retrieved job */
PRINT("++++++++++++ TR run B[%u]%p\n",me->id,me);
			x_mutex_unlock(&run_lock);
			jobdesc.run(jobdesc.job, me->tid);
			x_mutex_lock(&run_lock);
PRINT("++++++++++++ TR run A[%u]%p\n",me->id,me);
			break;

		case AFB_THREADS_IDLE:
			/* enter idle */
			if (!mainjob && active_count > normal_count)
				goto stopme;
PRINT("++++++++++++ TRwB[%u]%p\n",me->id,me);
			x_mutex_lock(&asleep_lock);
			me->next_asleep = asleep_threads;
			asleep_threads = me;
			asleep_count++;
			x_mutex_unlock(&run_lock);
			if (asleep_waiter_cond != NULL)
				x_cond_signal(asleep_waiter_cond);
			x_cond_wait(&me->cond, &asleep_lock);
			x_mutex_unlock(&asleep_lock);
			x_mutex_lock(&run_lock);
PRINT("++++++++++++ TRwA[%u]%p\n",me->id,me);
			break;

		default:
			/* stop current thread */
stopme:
PRINT("++++++++++++ TR stop B[%u]%p\n",me->id,me);
			me->stopped = 1;
			break;
		}
	}
	while (!me->stopped);
	if (mainjob)
		getjob = NULL;
	active_count--;
	unlink_thread(me);
	x_mutex_lock(&asleep_lock);
	x_mutex_unlock(&run_lock);
	if (asleep_waiter_cond != NULL)
		x_cond_signal(asleep_waiter_cond);
	x_mutex_unlock(&asleep_lock);

	afb_ev_mgr_try_recover_for_me();

	/* terminate */

PRINT("++++++++++++ STOP[%u] %p\n",me->id,me);
}

static void *thread_main(void *arg)
{
	struct thread *thr = arg;

	/* initiate thread tempo */
	afb_sig_monitor_init_timeouts();

	for (;;) {
		thread_run(thr, 0);
		x_mutex_lock(&reserve_lock);
		if (current_reserve_count >= reserve_count) {
			x_mutex_unlock(&reserve_lock);
			afb_sig_monitor_clean_timeouts();
			x_cond_destroy(&thr->cond);
			free(thr);
			return NULL;
		}
		current_reserve_count++;
		thr->stopped = 0;
		thr->next = reserve_head;
		reserve_head = thr;
		x_cond_wait(&thr->cond, &reserve_lock);
		x_mutex_unlock(&reserve_lock);
	}
}


/***********************************************************************/

void afb_threads_setup_counts(int normal, int reserve)
{
	x_mutex_lock(&run_lock);
	if (normal >= 0)
		normal_count = normal;
	if (reserve >= 0)
		reserve_count = reserve;
	x_mutex_unlock(&run_lock);
}

/***********************************************************************/

int afb_threads_start()
{
	int rc;
	struct thread *thr;

	x_mutex_lock(&reserve_lock);
	thr = reserve_head;
	if (thr != NULL) {

		reserve_head = thr->next;
		current_reserve_count--;
		x_cond_signal(&thr->cond);
		x_mutex_unlock(&reserve_lock);
		return 0;
	}
	x_mutex_unlock(&reserve_lock);

	/* create a new thread */
	thr = malloc(sizeof *thr);
	if (thr == NULL)
		return X_ENOMEM;

	/* init it */
	thr->next = 0;
	thr->stopped = 0;
	thr->cond = (x_cond_t)X_COND_INITIALIZER;

	rc = x_thread_create(&thr->tid, thread_main, thr, 1);
	if (rc < 0) {
		rc = -errno;
		x_cond_destroy(&thr->cond);
		free(thr);
		RP_CRITICAL("not able to start thread: %s", strerror(-rc));

	}
	return rc;
}

int afb_threads_enter(afb_threads_job_getter_t jobget, void *closure)
{
	struct thread me;

	if (getjob)
		return X_EINVAL;

	/* setup the structure for me */
	me.next = 0;
	me.stopped = 0;
	me.tid = x_thread_self();
	me.cond = (x_cond_t)X_COND_INITIALIZER;

	/* initiate thread tempo */
	afb_sig_monitor_init_timeouts();

	/* enter the thread now */
	getjobcls = closure;
	thread_run(&me, jobget);

	afb_sig_monitor_clean_timeouts();
	x_cond_destroy(&me.cond);

	return 0;
}

static int wakeup_one()
{
	struct thread *thr;
	int result = 0;
PRINT("++++++++++++ B-TWU\n");
	x_mutex_lock(&asleep_lock);
	thr = asleep_threads;
	if (!thr) {
		x_mutex_unlock(&asleep_lock);
		result = 0;
	}
	else {
		asleep_count--;
		asleep_threads = thr->next_asleep;
		x_cond_signal(&thr->cond);
		x_mutex_unlock(&asleep_lock);
		result = 1;
	}
PRINT("++++++++++++ A-TWU -> %d\n", result);
	return result;
}

void afb_threads_wakeup()
{
	x_mutex_lock(&run_lock);
	while (wakeup_one());
	x_mutex_unlock(&run_lock);
}

int afb_threads_start_cond(int force)
{
	int start, result = 0;
PRINT("++++++++++++ B-TSC\n");
	x_mutex_lock(&run_lock);
	start = !wakeup_one() && (force || active_count < normal_count);
	x_mutex_unlock(&run_lock);
	if (start)
		result = afb_threads_start();
PRINT("++++++++++++ A-TSC -> %d\n", result);
	return result;
}

static int has_me()
{
	int resu = 0;
	x_thread_t tid = x_thread_self();
	struct thread *ithr;
	x_mutex_lock(&run_lock);
	for (ithr = threads ; ithr && !resu ; ithr = ithr->next)
		resu = x_thread_equal(ithr->tid, tid);
	x_mutex_unlock(&run_lock);
	return resu;
}

static int is_stopped(void *closure)
{
	int result, hasme = (int)(intptr_t)closure;
	x_mutex_lock(&run_lock);
	result = hasme == active_count;
	x_mutex_unlock(&run_lock);
	return result;
}

static int wait_stopped()
{
	int hasme = has_me();
	return afb_threads_wait_until(is_stopped, (void*)(intptr_t)hasme, 0);
}

void afb_threads_stop_all(int wait)
{
	struct thread *ithr;
	x_mutex_lock(&run_lock);
	for (ithr = threads ; ithr ; ithr = ithr->next)
		ithr->stopped = 1;
	while(wakeup_one());
	x_mutex_unlock(&run_lock);
	if (wait)
		wait_stopped();
}

int afb_threads_wait_until(int (*test)(void *clo), void *closure, struct timespec *expire)
{
	for(;;) {
		int rc = test(closure);
		if (rc != 0)
			return rc;

		x_mutex_lock(&run_lock);
		if(wakeup_one() || !expire)
			x_mutex_unlock(&run_lock);
		else {
			x_cond_t *oldcond;
			x_cond_t cond = X_COND_INITIALIZER;

			x_mutex_lock(&asleep_lock);
			x_mutex_unlock(&run_lock);
			oldcond = asleep_waiter_cond;
			asleep_waiter_cond = &cond;
			rc = expire
				? x_cond_timedwait(&cond, &asleep_lock, expire)
				: x_cond_wait(&cond, &asleep_lock);
			asleep_waiter_cond = oldcond;
			x_mutex_unlock(&asleep_lock);
			if (rc)
				return X_ETIMEDOUT;
		}
	}
}

static int is_idle(void *closure)
{
	int result, hasme = (int)(intptr_t)closure;
	x_mutex_lock(&run_lock);
	result = (asleep_count + hasme) == active_count;
	x_mutex_unlock(&run_lock);
	return result;
}

int afb_threads_wait_idle(struct timespec *expire)
{
	int hasme = has_me();
	return afb_threads_wait_until(is_idle, (void*)(intptr_t)hasme, expire);
}

int afb_threads_active_count()
{
       return active_count;
}

int afb_threads_asleep_count()
{
       return asleep_count;
}

