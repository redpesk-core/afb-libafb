/*
 * Copyright (C) 2015-2023 IoT.bzh Company
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

#define DEBUGGING 0
#if DEBUGGING
#include <stdio.h>
#define PRINT(...) fprintf(stderr,__VA_ARGS__)
#else
#define PRINT(...)
#endif

/** Description of threads */
struct thread
{
	/** next thread of the list */
	struct thread *next;

	/** thread id */
	x_thread_t tid;

	/** stop requested */
	volatile unsigned char active: 1;

	/** is asleep */
	volatile unsigned char asleep: 1;

	/** class of the thread */
	int classid;

	/** job getter */
	afb_threads_job_getter_t getjob;

	/** closure of the job getter */
	void *getjobcls;

	/** synchronisation with the thread */
	x_cond_t  cond;

};

/* synchronisation of threads (TODO: try to use lock-free technics) */
static x_mutex_t mutex = X_MUTEX_INITIALIZER;
static x_cond_t *asleep_waiter_cond = 0;


/* list of threads */
static struct thread *threads = 0;

static int active_count = 0;
static int asleep_count = 0;

#ifndef AFB_THREADS_RESERVE_COUNT
#define AFB_THREADS_RESERVE_COUNT 16
#endif
static int reserve_count = AFB_THREADS_RESERVE_COUNT;
static struct thread *reserve = 0;

static inline int match_any_class(int classid)
{
	return !~classid;
}

static inline int match_class(struct thread *thr, int classid)
{
	return thr->classid & classid;
}

static void link_thread(struct thread *thr)
{
	thr->next = threads;
	threads = thr;
	active_count++;
}

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

static inline void wakeup_asleep_waiter(x_cond_t *newcond)
{
	x_cond_t *oldcond = asleep_waiter_cond;
	asleep_waiter_cond = newcond;
	if (oldcond)
		x_cond_signal(oldcond);
}

static inline void cancel_asleep_waiter(x_cond_t *ifcond)
{
	if (asleep_waiter_cond == ifcond)
		asleep_waiter_cond = 0;
}

static int wakeup(struct thread *thr)
{
	if (!thr->asleep)
		return 0;

PRINT("++++++++++++ WUsB%p\n",thr);
	thr->asleep = 0;
	asleep_count--;
	x_cond_signal(&thr->cond);
PRINT("++++++++++++ WUsA%p\n",thr);
	return 1;
}

static void stop(struct thread *thr)
{
	active_count--;
	thr->active = 0;
	wakeup_asleep_waiter(0);
	wakeup(thr);
}

static void thread_run(struct thread *me)
{
	int status;
	afb_threads_job_desc_t jobdesc;
	x_thread_t tid = me->tid;

PRINT("++++++++++++ START %p classid=%d\n",me,me->classid);
	/* initiate thread tempo */
	afb_sig_monitor_init_timeouts();

	while (me->active) {
		/* get a job */
		x_mutex_unlock(&mutex);
		status = me->getjob(me->getjobcls, &jobdesc, tid);
		if (status > 0) {
			/* run the job */
			jobdesc.run(jobdesc.job, tid);
			x_mutex_lock(&mutex);
		}
		else {
			x_mutex_lock(&mutex);
			if (me->active) {
				if (status < 0)
					stop(me);
				else {
					/* no job, wait */
					me->asleep = 1;
					asleep_count++;
PRINT("++++++++++++ TRwB%p classid=%d\n",me,me->classid);
					wakeup_asleep_waiter(0);
					x_cond_wait(&me->cond, &mutex);
PRINT("++++++++++++ TRwA%p classid=%d\n",me,me->classid);
				}
			}
		}
	}
	/* terminate */
	afb_sig_monitor_clean_timeouts();

PRINT("++++++++++++ STOP %p classid=%d\n",me,me->classid);
}

static void *thread_starter(void *arg)
{
	struct thread *thr = arg;
	x_mutex_lock(&mutex);
	for (;;) {
		thread_run(thr);
		unlink_thread(thr);
		if (reserve_count <= 0)
			break;
		thr->next = reserve;
		reserve = thr;
		reserve_count--;
		x_cond_wait(&thr->cond, &mutex);
	}
	x_mutex_unlock(&mutex);
	free(thr);
	return 0;
}

static int start_thread(struct thread *thr)
{
	int rc;

	x_mutex_lock(&mutex);
	link_thread(thr);
	rc = x_thread_create(&thr->tid, thread_starter, thr, 1);
	if (rc < 0)
		unlink_thread(thr);
	x_mutex_unlock(&mutex);
	if (rc < 0) {
		free(thr);
		RP_CRITICAL("not able to start thread: %s", strerror(-rc));
	}
	return rc;
}

static int enter_thread(struct thread *thr)
{
	thr->tid = x_thread_self();
	x_mutex_lock(&mutex);
	link_thread(thr);
	thread_run(thr);
	unlink_thread(thr);
	x_mutex_unlock(&mutex);
	free(thr);
	return 0;
}

static int start(int classid, afb_threads_job_getter_t jobget, void * closure, int (*run)(struct thread *thr))
{
	int rc;
	struct thread *thr = malloc(sizeof *thr);
	if (!thr)
		rc = -ENOMEM;
	else {
		thr->next = 0;
		thr->tid = 0;
		thr->active = 1;
		thr->asleep = 0;
		thr->cond = (x_cond_t)PTHREAD_COND_INITIALIZER;
		thr->classid = classid;
		thr->getjob = jobget;
		thr->getjobcls = closure;
		rc = run(thr);
	}
	return rc;
}

int afb_threads_active_count(int classid)
{
	struct thread *ithr;
	int count;
	x_mutex_lock(&mutex);
	if (match_any_class(classid))
		count = active_count;
	else {
		for (count = 0, ithr = threads ; ithr ; ithr = ithr->next)
			count += (ithr->active && match_class(ithr, classid));
	}
	x_mutex_unlock(&mutex);
	return count;
}

int afb_threads_asleep_count(int classid)
{
	struct thread *ithr;
	int count;
	x_mutex_lock(&mutex);
	if (match_any_class(classid))
		count = asleep_count;
	else {
		for (count = 0, ithr = threads ; ithr ; ithr = ithr->next)
			count += (ithr->asleep && match_class(ithr, classid));
	}
	x_mutex_unlock(&mutex);
	return count;
}

int afb_threads_start(int classid, afb_threads_job_getter_t jobget, void *closure)
{
	struct thread *thr;
	x_mutex_lock(&mutex);
	thr = reserve;
	if (!thr) {
		x_mutex_unlock(&mutex);
		return start(classid, jobget, closure, start_thread);
	}
	reserve = thr->next;
	reserve_count++;
	thr->next = 0;
	thr->active = 1;
	thr->asleep = 0;
	thr->classid = classid;
	thr->getjob = jobget;
	thr->getjobcls = closure;
	link_thread(thr);
	x_cond_signal(&thr->cond);
	x_mutex_unlock(&mutex);
	return 0;
}

int afb_threads_enter(int classid, afb_threads_job_getter_t jobget, void *closure)
{
	return start(classid, jobget, closure, enter_thread);
}

int afb_threads_wakeup(int classid, int count)
{
	int decount = 0;
	struct thread *ithr;
	x_mutex_lock(&mutex);
	for (ithr = threads ; ithr && decount < count ; ithr = ithr->next)
		if (ithr->active && match_class(ithr, classid))
			decount += wakeup(ithr);
	x_mutex_unlock(&mutex);
	return decount;
}

int afb_threads_stop(int classid, int count)
{
	int decount = 0;
	struct thread *ithr;
	x_mutex_lock(&mutex);
	for (ithr = threads ; ithr && decount < count ; ithr = ithr->next)
		if (ithr->active && match_class(ithr, classid)) {
			stop(ithr);
			decount++;
		}
	x_mutex_unlock(&mutex);
	return decount;
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
	x_mutex_lock(&mutex);
	thr = get_thread(tid);
	resu = thr ? thr->classid : 0;
	x_mutex_unlock(&mutex);
	return resu;
}

int afb_threads_stop_thread(x_thread_t tid)
{
	int resu;
	struct thread *thr;
	x_mutex_lock(&mutex);
	thr = get_thread(tid);
	if ((resu = (thr && thr->active)))
		stop(thr);
	x_mutex_unlock(&mutex);
	return resu;
}

int afb_threads_has_me()
{
	return afb_threads_has_thread(x_thread_self());
}

int afb_threads_stop_me()
{
	return afb_threads_stop_thread(x_thread_self());
}

int afb_threads_wait_idle(int classid, int timeoutms)
{
	x_thread_t tid = x_thread_self();
	int rc, resu = 0;
	struct timespec expire;
	struct thread *ithr;
	if (timeoutms > 0) {
		clock_gettime(CLOCK_REALTIME, &expire);
		expire.tv_sec += timeoutms / 1000;
		expire.tv_nsec = (timeoutms % 1000) * 1000000;
		if (expire.tv_nsec >= 1000000000) {
			expire.tv_nsec -= 1000000000;
			expire.tv_sec++;
		}
	}
	x_mutex_lock(&mutex);
	for (ithr = threads ; ithr ; ) {
		if (!x_thread_equal(ithr->tid, tid) /* not me */
		 && match_class(ithr, classid) /* match the given class */
		 && ithr->active && !ithr->asleep /* active but not asleep */) {
			x_cond_t cond = PTHREAD_COND_INITIALIZER;
			wakeup_asleep_waiter(&cond);
			if (timeoutms <= 0)
				x_cond_wait(&cond, &mutex);
			else {
				rc = x_cond_timedwait(&cond, &mutex, &expire);
				if (rc) {
					if (errno == ETIMEDOUT) {
						cancel_asleep_waiter(&cond);
						resu = X_ETIMEDOUT;
						break;
					}
				}
			}
			cancel_asleep_waiter(&cond);
			ithr = threads;
		}
		else
			ithr = ithr->next;
	}
	x_mutex_unlock(&mutex);
	return resu;
}

void afb_threads_set_reserve_count(int count)
{
	int curco;
	struct thread *ithr;
	x_mutex_lock(&mutex);
	curco = reserve_count;
	for (ithr = reserve ; ithr ; ithr = ithr->next)
		curco++;
	reserve_count += count - curco;
	x_mutex_unlock(&mutex);
}
