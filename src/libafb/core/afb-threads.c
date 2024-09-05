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

	/** is extra */
	unsigned char extra;

	/** stop request */
	unsigned char stopped;

	/** job getter */
	afb_threads_job_getter_t getjob;

	/** closure of the job getter */
	void *getjobcls;

	/** synchronisation with the thread */
	x_cond_t  cond;

	/* an id if debugging */
	IFDBG(unsigned id;)
};

/***********************************************************************/

/** count of allowed threads */
static int normal_count = 1;

/** synchronisation of thread's list management */
static x_mutex_t list_lock = X_MUTEX_INITIALIZER;

/** synchronisation of running threads for job acquisition */
static x_mutex_t run_lock = X_MUTEX_INITIALIZER;

static x_cond_t *asleep_waiter_cond = 0;

/** list of threads */
static struct thread *threads = 0;

static int active_count = 0;

/***********************************************************************
* asleep threads will wait for being woken up on the wakeup_cond
*/

/** signaling condition for waking up threads */
static x_cond_t wakeup_cond = X_COND_INITIALIZER;

/** synchronisation of wakeup/asleep management */
static x_mutex_t asleep_lock = X_MUTEX_INITIALIZER;

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

/**
 * Removes @ref thr from the list of active threads.
 * The mutex @ref list_lock must be held when this function is called.
 * The value of active_count is unchanged because it should be done in @ref stop
 */
static void unlink_thread(struct thread *thr)
{
	struct thread *ithr;
	if (threads == thr)
		threads = thr->next;
	else {
		for (ithr = threads ; ithr && ithr->next != thr ; ithr = ithr->next);
		if (ithr)
			ithr->next = thr->next;
	}
}

static void thread_run(struct thread *me)
{
	int status;
	afb_threads_job_desc_t jobdesc;

IFDBG(static unsigned id = 0; me->id = ++id;)

PRINT("++++++++++++ START[%u] %p\n",me->id,me);

	x_mutex_lock(&run_lock);
	while (!me->stopped) {
		/* get a job */
		status = me->getjob(me->getjobcls, &jobdesc, me->tid);
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
			if (me->extra)
				goto stopme;
PRINT("++++++++++++ TRwB[%u]%p\n",me->id,me);
			x_mutex_lock(&asleep_lock);
			x_mutex_unlock(&run_lock);
			__atomic_add_fetch(&asleep_count, 1, __ATOMIC_ACQ_REL);
			if (asleep_waiter_cond != NULL)
				x_cond_signal(asleep_waiter_cond);
			x_cond_wait(&wakeup_cond, &asleep_lock);
			x_mutex_unlock(&asleep_lock);
			x_mutex_lock(&run_lock);
PRINT("++++++++++++ TRwA[%u]%p\n",me->id,me);
			break;

		default:
			/* stop current thread */
stopme:
PRINT("++++++++++++ TR stop B[%u]%p\n",me->id,me);
			__atomic_sub_fetch(&active_count, 1, __ATOMIC_ACQ_REL);
			me->stopped = 1;
			break;
		}
	}
	x_mutex_unlock(&run_lock);

	x_mutex_lock(&list_lock);
	unlink_thread(me);
	x_mutex_unlock(&list_lock);

	x_mutex_lock(&asleep_lock);
	if (asleep_waiter_cond != NULL)
		x_cond_signal(asleep_waiter_cond);
	x_mutex_unlock(&asleep_lock);

	afb_ev_mgr_try_recover_for_me();

	/* terminate */
	afb_sig_monitor_clean_timeouts();

PRINT("++++++++++++ STOP[%u] %p\n",me->id,me);
}

static void *thread_main(void *arg)
{
	struct thread *thr = arg;

	/* initiate thread tempo */
	afb_sig_monitor_init_timeouts();

	for (;;) {
		thread_run(thr);
		x_mutex_lock(&reserve_lock);
		if (current_reserve_count >= reserve_count) {
			x_mutex_unlock(&reserve_lock);
			free(thr);
			return 0;
		}
		current_reserve_count++;
		thr->next = reserve_head;
		reserve_head = thr;
		x_cond_wait(&thr->cond, &reserve_lock);
		x_mutex_unlock(&reserve_lock);
	}
}


/***********************************************************************/

void afb_threads_setup_counts(int normal, int reserve)
{
	if (normal >= 0)
		normal_count = normal;
	if (reserve >= 0)
		reserve_count = reserve;
}

/***********************************************************************/

int afb_threads_active_count()
{
	return __atomic_load_n(&active_count, __ATOMIC_SEQ_CST);
}

int afb_threads_asleep_count()
{
	return __atomic_load_n(&asleep_count, __ATOMIC_SEQ_CST);
}

int afb_threads_start_cond(afb_threads_job_getter_t jobget, void *closure, int force)
{
	return !force && __atomic_load_n(&active_count, __ATOMIC_SEQ_CST) >= normal_count
		? 0 : afb_threads_start(jobget, closure);
}

int afb_threads_start(afb_threads_job_getter_t jobget, void *closure)
{
	int rc;
	struct thread *thr;

	x_mutex_lock(&reserve_lock);
	thr = reserve_head;
	if (thr != NULL) {

		reserve_head = thr->next;
		current_reserve_count--;
		thr->stopped = 0;
		thr->getjob = jobget;
		thr->getjobcls = closure;
		x_mutex_lock(&list_lock);
		thr->next = threads;
		threads = thr;
		thr->extra = __atomic_add_fetch(&active_count, 1, __ATOMIC_ACQ_REL)
		           > __atomic_load_n(&normal_count, __ATOMIC_SEQ_CST);
		x_mutex_unlock(&list_lock);
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
	thr->getjob = jobget;
	thr->getjobcls = closure;

	x_mutex_lock(&list_lock);
	thr->next = threads;
	threads = thr;
	thr->extra = __atomic_add_fetch(&active_count, 1, __ATOMIC_ACQ_REL)
	           > __atomic_load_n(&normal_count, __ATOMIC_SEQ_CST);
	rc = x_thread_create(&thr->tid, thread_main, thr, 1);
	if (rc < 0) {
		rc = -errno;
		__atomic_sub_fetch(&active_count, 1, __ATOMIC_ACQ_REL);
		unlink_thread(thr);
		free(thr);
		RP_CRITICAL("not able to start thread: %s", strerror(-rc));

	}
	x_mutex_unlock(&list_lock);
	return rc;
}

int afb_threads_enter(afb_threads_job_getter_t jobget, void *closure)
{
	struct thread me;

	/* setup the structure for me */
	me.next = 0;
	me.tid = x_thread_self();
	me.extra = 0;
	me.stopped = 0;
	me.cond = (x_cond_t)X_COND_INITIALIZER;
	me.getjob = jobget;
	me.getjobcls = closure;

	/* initiate thread tempo */
	afb_sig_monitor_init_timeouts();

	/* enter the thread now */
	x_mutex_lock(&list_lock);
	me.next = threads;
	threads = &me;
	__atomic_add_fetch(&active_count, 1, __ATOMIC_ACQ_REL);
	x_mutex_unlock(&list_lock);

	thread_run(&me);

	return 0;
}

int afb_threads_wakeup_one()
{
	int result;
	x_mutex_lock(&asleep_lock);
PRINT("++++++++++++ B-TWU\n");
	if (__atomic_load_n(&asleep_count, __ATOMIC_SEQ_CST) == 0)
		result = 0;
	else {
		__atomic_sub_fetch(&asleep_count, 1, __ATOMIC_ACQ_REL);
		x_cond_signal(&wakeup_cond);
		result = 1;
	}
	x_mutex_unlock(&asleep_lock);
PRINT("++++++++++++ A-TWU -> %d\n", result);
	return result;
}

void afb_threads_stop_all()
{
	struct thread *ithr;
	x_mutex_lock(&list_lock);
	for (ithr = threads ; ithr ; ithr = ithr->next)
		if (ithr->stopped == 0) {
			ithr->stopped = 1;
			__atomic_sub_fetch(&active_count, 1, __ATOMIC_ACQ_REL);
		}
	x_mutex_unlock(&list_lock);
	x_mutex_lock(&asleep_lock);
	x_cond_broadcast(&wakeup_cond);
	x_mutex_unlock(&asleep_lock);
}

static struct thread *get_thread(x_thread_t tid)
{
	struct thread *ithr;
	for (ithr = threads ; ithr && !x_thread_equal(ithr->tid, tid) ; ithr = ithr->next);
	return ithr;
}

int afb_threads_has_thread(x_thread_t tid)
{
	int resu;
	struct thread *thr;
	x_mutex_lock(&list_lock);
	thr = get_thread(tid);
	resu = thr != NULL && !thr->stopped;
	x_mutex_unlock(&list_lock);
	return resu;
}

int afb_threads_has_me()
{
	return afb_threads_has_thread(x_thread_self());
}


int afb_threads_wait_new_asleep(struct timespec *expire)
{
	int rc, resu = 0;
	x_cond_t cond = X_COND_INITIALIZER;
	x_cond_t *oldcond;

	x_mutex_lock(&asleep_lock);
	oldcond = asleep_waiter_cond;
	asleep_waiter_cond = &cond;
	rc = expire == NULL
		? x_cond_wait(&cond, &asleep_lock)
		: x_cond_timedwait(&cond, &asleep_lock, expire);
	if (rc != 0)
		resu = X_ETIMEDOUT;
	if (asleep_waiter_cond == &cond)
		asleep_waiter_cond = oldcond;
	if (oldcond != NULL && resu == 0)
		x_cond_signal(oldcond);
	x_mutex_unlock(&asleep_lock);
	return resu;
}

