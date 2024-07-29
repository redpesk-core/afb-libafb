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

	/** sleeping status */
	unsigned char aslept;

	/** stop request */
	unsigned char stopped;

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

/***********************************************************************/
#ifndef WITH_THREADS_RESERVE
#define  WITH_THREADS_RESERVE 1
#endif
#if WITH_THREADS_RESERVE
/*
* Reserve is a reserved already started threads but not
* active. These thread structures are stored in the list
* headed by `reserve_head`. The value `reserve_decount` is the
* remaining count of threads allowed to enter the reserve.
*/
#ifndef AFB_THREADS_RESERVE_COUNT
#define AFB_THREADS_RESERVE_COUNT 4
#endif
static x_mutex_t reserve_lock = X_MUTEX_INITIALIZER;
static int reserve_decount = AFB_THREADS_RESERVE_COUNT;
static struct thread *reserve_head = 0;
#endif

/***********************************************************************/
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
	x_mutex_lock(&mutex);
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
	if (thr->stopped || !thr->aslept)
		return 0;

PRINT("++++++++++++ WUsB%p\n",thr);
	thr->aslept = 0;
	x_cond_signal(&thr->cond);
PRINT("++++++++++++ WUsA%p\n",thr);
	return 1;
}

static void stop(struct thread *thr)
{
	active_count--;
	thr->stopped = 1;
	wakeup_asleep_waiter(0);
	if (thr->aslept) {
		thr->aslept = 0;
		x_cond_signal(&thr->cond);
	}
}

static void thread_run(struct thread *me)
{
	int status;
	afb_threads_job_desc_t jobdesc;

	x_mutex_unlock(&mutex);

PRINT("++++++++++++ START %p classid=%d\n",me,me->classid);
	/* initiate thread tempo */
	afb_sig_monitor_init_timeouts();

	while (!me->stopped) {
		/* get a job */
		status = me->getjob(me->getjobcls, &jobdesc, me->tid);
		switch (status) {
		case AFB_THREADS_CONTINUE:
			/* continue the loop */
			break;

		case AFB_THREADS_EXEC:
			/* execute the retrieved job */
PRINT("++++++++++++ TR run B%p classid=%d\n",me,me->classid);
			jobdesc.run(jobdesc.job, me->tid);
			break;

		case AFB_THREADS_IDLE:
			/* enter idle */
PRINT("++++++++++++ TRwB%p classid=%d\n",me,me->classid);
			x_mutex_lock(&mutex);
			me->aslept = 1;
			wakeup_asleep_waiter(0);
			x_cond_wait(&me->cond, &mutex);
			x_mutex_unlock(&mutex);
PRINT("++++++++++++ TRwA%p classid=%d\n",me,me->classid);
			break;

		default:
			/* stop current thread */
PRINT("++++++++++++ TR stop B%p classid=%d\n",me,me->classid);
			x_mutex_lock(&mutex);
			stop(me);
			x_mutex_unlock(&mutex);
			break;
		}
	}

	x_mutex_lock(&mutex);
	unlink_thread(me);
	x_mutex_unlock(&mutex);

	afb_ev_mgr_try_recover_for_me();

	/* terminate */
	afb_sig_monitor_clean_timeouts();

PRINT("++++++++++++ STOP %p classid=%d\n",me,me->classid);

}

static void *thread_main(void *arg)
{
	struct thread *thr = arg;

	for (;;) {
		x_mutex_lock(&mutex);
		thread_run(thr);
#if WITH_THREADS_RESERVE
		x_mutex_lock(&reserve_lock);
		if (reserve_decount > 0) {
			thr->next = reserve_head;
			reserve_head = thr;
			reserve_decount--;
			x_cond_wait(&thr->cond, &reserve_lock);
			x_mutex_unlock(&reserve_lock);
			continue;
		}
		x_mutex_unlock(&reserve_lock);
#endif
		break;
	}
	free(thr);
	return 0;
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
			count += (!ithr->stopped && match_class(ithr, classid));
	}
	x_mutex_unlock(&mutex);
	return count;
}

int afb_threads_asleep_count(int classid)
{
	struct thread *ithr;
	int count;
	x_mutex_lock(&mutex);
	for (count = 0, ithr = threads ; ithr ; ithr = ithr->next)
		count += (ithr->aslept && match_class(ithr, classid));
	x_mutex_unlock(&mutex);
	return count;
}

int afb_threads_start(int classid, afb_threads_job_getter_t jobget, void *closure)
{
	int rc;
	struct thread *thr;

#if WITH_THREADS_RESERVE
	x_mutex_lock(&reserve_lock);
	thr = reserve_head;
	if (thr != NULL) {

		reserve_head = thr->next;
		reserve_decount++;
		thr->aslept = 0;
		thr->stopped = 0;
		thr->classid = classid;
		thr->getjob = jobget;
		thr->getjobcls = closure;
		link_thread(thr);
		x_cond_signal(&thr->cond);
		x_mutex_unlock(&mutex);
		x_mutex_unlock(&reserve_lock);
		return 0;
	}
	x_mutex_unlock(&reserve_lock);
#endif
	thr = malloc(sizeof *thr);
	if (thr == NULL)
		return X_ENOMEM;

	thr->next = 0;
	thr->aslept = 0;
	thr->stopped = 0;
	thr->cond = (x_cond_t)PTHREAD_COND_INITIALIZER;
	thr->classid = classid;
	thr->getjob = jobget;
	thr->getjobcls = closure;

	link_thread(thr);
	rc = x_thread_create(&thr->tid, thread_main, thr, 1);
	if (rc < 0) {
		rc = -errno;
		unlink_thread(thr);
		active_count--;
		free(thr);
		RP_CRITICAL("not able to start thread: %s", strerror(-rc));

	}
	x_mutex_unlock(&mutex);
	return rc;
}

int afb_threads_enter(int classid, afb_threads_job_getter_t jobget, void *closure)
{
	struct thread me;

	me.next = 0;
	me.tid = x_thread_self();
	me.aslept = 0;
	me.stopped = 0;
	me.cond = (x_cond_t)PTHREAD_COND_INITIALIZER;
	me.classid = classid;
	me.getjob = jobget;
	me.getjobcls = closure;

	link_thread(&me);
	thread_run(&me);

	return 0;
}

int afb_threads_wakeup(int classid, int count)
{
	int decount = 0;
	struct thread *ithr;
	x_mutex_lock(&mutex);
	for (ithr = threads ; ithr && decount < count ; ithr = ithr->next)
		if (ithr->aslept && match_class(ithr, classid))
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
		if (!ithr->stopped && match_class(ithr, classid)) {
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
	if ((resu = (thr != NULL && !thr->stopped)))
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
		 && !ithr->stopped
		 && match_class(ithr, classid) /* match the given class */
		 && !ithr->aslept /* active but not asleep */) {
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
