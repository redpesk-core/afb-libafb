/*
 * Copyright (C) 2015-2020 IoT.bzh Company
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

#include "libafb-config.h"

#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include "core/afb-jobs.h"
#include "core/afb-sched.h"
#include "sys/evmgr.h"
#include "core/afb-sig-monitor.h"
#include "sys/verbose.h"
#include "sys/x-mutex.h"
#include "sys/x-cond.h"
#include "sys/x-thread.h"
#include "sys/x-errno.h"

#define EVENT_TIMEOUT_TOP  	((uint64_t)-1)
#define EVENT_TIMEOUT_CHILD	((uint64_t)10000)

/** Description of threads */
struct thread
{
	struct thread *next;   /**< next thread of the list */
	struct thread *upper;  /**< upper same thread */
	struct thread *nholder;/**< next holder for evloop */
	x_cond_t *cwhold;/**< condition wait for holding */
	volatile unsigned stop: 1;      /**< stop requested */
	volatile unsigned waits: 1;     /**< is waiting? */
	volatile unsigned leaved: 1;    /**< was leaved? */
};

/**
 * Description of synchronous callback
 */
struct sync
{
	struct thread thread;	/**< thread loop data */
	union {
		void (*callback)(int, void*);	/**< the synchronous callback */
		void (*enter)(int signum, void *closure, struct afb_sched_lock *afb_sched_lock);
				/**< the entering synchronous routine */
	};
	void *arg;		/**< the argument of the callback */
};

/* synchronisation of threads */
static x_mutex_t mutex = X_MUTEX_INITIALIZER;
static x_cond_t  cond = X_COND_INITIALIZER;

/* counts for threads */
static int allowed_thread_count = 0; /** allowed count of threads */
static int started_thread_count = 0; /** started count of threads */
static int busy_thread_count = 0;    /** count of busy threads */

/* list of threads */
static struct thread *threads;

/* event loop */
static struct evmgr *evmgr;

/* exit manager */
static void (*exit_handler)();

/* current thread */
X_TLS(struct thread,current_thread)

/**
 * wakeup the event loop if needed by sending
 * an event.
 */
static void evloop_wakeup()
{
	if (evmgr)
		evmgr_wakeup(evmgr);
}

/**
 * Release the currently held event loop
 */
static void evloop_release()
{
	struct thread *nh, *ct = x_tls_get_current_thread();

	if (ct && evmgr && evmgr_release_if(evmgr, ct)) {
		nh = ct->nholder;
		ct->nholder = 0;
		if (nh) {
			evmgr_try_hold(evmgr, nh);
			x_cond_signal(nh->cwhold);
		}
	}
}

/**
 * get the eventloop for the current thread
 */
static int evloop_get()
{
	return evmgr && evmgr_try_hold(evmgr, x_tls_get_current_thread());
}

/**
 * acquire the eventloop for the current thread
 */
static void evloop_acquire()
{
	struct thread *pwait, *ct;
	x_cond_t cond;

	/* try to get the evloop */
	if (!evloop_get()) {
		/* failed, init waiting state */
		ct = x_tls_get_current_thread();
		ct->nholder = NULL;
		ct->cwhold = &cond;
		x_cond_init(&cond);

		/* queue current thread in holder list */
		pwait = evmgr_holder(evmgr);
		while (pwait->nholder)
			pwait = pwait->nholder;
		pwait->nholder = ct;

		/* wake up the evloop */
		evloop_wakeup();

		/* wait to acquire the evloop */
		x_cond_wait(&cond, &mutex);
		x_cond_destroy(&cond);
	}
}

/**
 * Enter the thread
 * @param me the description of the thread to enter
 */
static void thread_enter(volatile struct thread *me)
{
	evloop_release();
	/* initialize description of itself and link it in the list */
	me->stop = exit_handler != NULL;
	me->waits = 0;
	me->leaved = 0;
	me->nholder = 0;
	me->upper = x_tls_get_current_thread();
	me->next = threads;
	threads = (struct thread*)me;
	x_tls_set_current_thread((struct thread*)me);
}

/**
 * leave the thread
 * @param me the description of the thread to leave
 */
static void thread_leave()
{
	struct thread **prv, *me;

	/* unlink the current thread and cleanup */
	me = x_tls_get_current_thread();
	prv = &threads;
	while (*prv != me)
		prv = &(*prv)->next;
	*prv = me->next;

	x_tls_set_current_thread(me->upper);
}

/**
 * Main processing loop of internal threads with processing jobs.
 * The loop must be called with the mutex locked
 * and it returns with the mutex locked.
 * @param me the description of the thread to use
 * TODO: how are timeout handled when reentering?
 */
static void thread_run_internal(volatile struct thread *me)
{
	struct afb_job *job;

	/* enter thread */
	thread_enter(me);

	/* loop until stopped */
	while (!me->stop) {
		/* release the current event loop */
		evloop_release();

		/* get a job */
		job = afb_jobs_dequeue();
		if (job) {
			/* run the job */
			x_mutex_unlock(&mutex);
			afb_jobs_run(job);
			x_mutex_lock(&mutex);
		/* no job, check event loop wait */
		} else if (evloop_get()) {
			if (!evmgr_can_run(evmgr)) {
				/* busy ? */
				CRITICAL("Can't enter dispatch while in dispatch!");
				abort();
			}
			/* run the events */
			evmgr_prepare_run(evmgr);
			x_mutex_unlock(&mutex);
			afb_sig_monitor_run(0, (void(*)(int,void*))evmgr_job_run, evmgr);
			x_mutex_lock(&mutex);
		} else {
			/* no job and no event loop */
			busy_thread_count--;
			if (!busy_thread_count)
				ERROR("Entering job deep sleep! Check your bindings.");
			me->waits = 1;
			x_cond_wait(&cond, &mutex);
			me->waits = 0;
			busy_thread_count++;
		}
	}
	/* cleanup */
	evloop_release();
	thread_leave();
}

/**
 * Main processing loop of external threads.
 * The loop must be called with the mutex locked
 * and it returns with the mutex locked.
 * @param me the description of the thread to use
 */
static void thread_run_external(volatile struct thread *me)
{
	/* enter thread */
	thread_enter(me);

	/* loop until stopped */
	me->waits = 1;
	while (!me->stop)
		x_cond_wait(&cond, &mutex);
	me->waits = 0;
	thread_leave();
}

/**
 * Root for created threads.
 */
static void thread_main()
{
	struct thread me;

	busy_thread_count++;
	started_thread_count++;
	afb_sig_monitor_init_timeouts();
	thread_run_internal(&me);
	afb_sig_monitor_clean_timeouts();
	started_thread_count--;
	busy_thread_count--;
}

/**
 * Entry point for created threads.
 * @param data not used
 * @return NULL
 */
static void thread_starter(void *data)
{
	x_mutex_lock(&mutex);
	thread_main();
	x_mutex_unlock(&mutex);
}

/**
 * Starts a new thread
 * @return 0 in case of success or -1 in case of error
 */
static int start_one_thread()
{
	x_thread_t tid;
	int rc;

	rc = x_thread_create(&tid, thread_starter, NULL);
	if (rc != 0)
		WARNING("not able to start thread: %m");
	return rc;
}

/**
 * Internal helper function for 'jobs_enter'.
 * @see jobs_enter, jobs_leave
 */
static void enter_cb(int signum, void *closure)
{
	struct sync *sync = closure;
	sync->enter(signum, sync->arg, (void*)&sync->thread);
}

/**
 * Internal helper function for 'jobs_call'.
 * @see jobs_call
 */
static void call_cb(int signum, void *closure)
{
	struct sync *sync = closure;
	sync->callback(signum, sync->arg);
	afb_sched_leave((void*)&sync->thread);
}

/**
 * Internal helper for synchronous jobs. It enters
 * a new thread loop for evaluating the given job
 * as recorded by the couple 'sync_cb' and 'sync'.
 * @see jobs_call, jobs_enter, jobs_leave
 */
static int do_sync(
		const void *group,
		int timeout,
		void (*sync_cb)(int signum, void *closure),
		struct sync *sync
)
{
	int rc;

	/* allocates the job */
	x_mutex_lock(&mutex);
	rc = afb_jobs_queue_lazy(group, timeout, sync_cb, sync);
	if (rc >= 0) {
		/* run until stopped */
		if (x_tls_get_current_thread())
			thread_run_internal(&sync->thread);
		else
			thread_run_external(&sync->thread);
		if (!sync->thread.leaved)
			rc = X_EINTR;
	}
	x_mutex_unlock(&mutex);
	return rc;
}

/**
 * Enter a synchronisation point: activates the job given by 'callback'
 * and 'closure' using 'group' and 'timeout' to control sequencing and
 * execution time.
 * @param group the group for sequencing jobs
 * @param timeout the time in seconds allocated to the job
 * @param callback the callback that will handle the job.
 *                 it receives 3 parameters: 'signum' that will be 0
 *                 on normal flow or the catched signal number in case
 *                 of interrupted flow, the context 'closure' as given and
 *                 a 'jobloop' reference that must be used when the job is
 *                 terminated to unlock the current execution flow.
 * @param closure the argument to the callback
 * @return 0 on success or -1 in case of error
 */
int afb_sched_enter(
		const void *group,
		int timeout,
		void (*callback)(int signum, void *closure, struct afb_sched_lock *afb_sched_lock),
		void *closure
)
{
	struct sync sync;

	sync.enter = callback;
	sync.arg = closure;
	return do_sync(group, timeout, enter_cb, &sync);
}

/**
 * Unlocks the execution flow designed by 'jobloop'.
 * @param jobloop indication of the flow to unlock
 * @return 0 in case of success of -1 on error
 */
int afb_sched_leave(struct afb_sched_lock *afb_sched_lock)
{
	struct thread *t;

	x_mutex_lock(&mutex);
#if 1
	/* check existing lock */
	t = threads;
	while (t && t != (struct thread*)afb_sched_lock)
		t = t->next;
	if (!t) {
		x_mutex_unlock(&mutex);
		return X_EINVAL;
	}
#else
	t = (struct thread*)afb_sched_lock;
#endif
	t->leaved = 1;
	t->stop = 1;
	if (t->waits)
		x_cond_broadcast(&cond);
	else
		evloop_wakeup();
	x_mutex_unlock(&mutex);
	return 0;
}

/**
 * Calls synchronously the job represented by 'callback' and 'arg1'
 * for the 'group' and the 'timeout' and waits for its completion.
 * @param group    The group of the job or NULL when no group.
 * @param timeout  The maximum execution time in seconds of the job
 *                 or 0 for unlimited time.
 * @param callback The function to execute for achieving the job.
 *                 Its first parameter is either 0 on normal flow
 *                 or the signal number that broke the normal flow.
 *                 The remaining parameter is the parameter 'arg1'
 *                 given here.
 * @param arg      The second argument for 'callback'
 * @return 0 in case of success or -1 in case of error
 */
int afb_sched_call_job(
		const void *group,
		int timeout,
		void (*callback)(int, void*),
		void *arg)
{
	struct sync sync;

	sync.callback = callback;
	sync.arg = arg;

	return do_sync(group, timeout, call_cb, &sync);
}

/**
 * Ensure that the current running thread can control the event loop.
 */
struct evmgr *afb_sched_acquire_event_manager()
{
	int fake;
	struct thread lt;

	/* lock */
	x_mutex_lock(&mutex);

	/* ensure an existing thread environment */
	fake = !x_tls_get_current_thread();
	if (fake) {
		memset(&lt, 0, sizeof lt);
		x_tls_set_current_thread(&lt);
	}

	/* creates the evloop on need */
	if (!evmgr)
		evmgr_create(&evmgr);

	/* acquire the event loop under lock */
	if (evmgr)
		evloop_acquire();

	/* release the faked thread environment if needed */
	if (fake) {
		evloop_release();
		x_tls_set_current_thread(NULL);
	}
	/* unlock */
	x_mutex_unlock(&mutex);

	/* warn if faked */
	if (fake) {
		/*
		 * Releasing it is needed because there is no way to guess
		 * when it has to be released really. But here is where it is
		 * hazardous: if the caller modifies the eventloop when it
		 * is waiting, there is no way to make the change effective.
		 * A workaround to achieve that goal is for the caller to
		 * require the event loop a second time after having modified it.
		 */
		NOTICE("Requiring event manager/loop from outside of binder's callback is hazardous!");
		if (verbose_wants(Log_Level_Info))
			afb_sig_monitor_dumpstack();
	}
	return evmgr;
}

/**
 * Enter the jobs processing loop.
 * @param allowed_count Maximum count of thread for jobs including this one
 * @param start_count   Count of thread to start now, must be lower.
 * @param waiter_count  Maximum count of jobs that can be waiting.
 * @param start         The start routine to activate (can't be NULL)
 * @return 0 in case of success or -1 in case of error.
 */
int afb_sched_start(
	int allowed_count,
	int start_count,
	int waiter_count,
	void (*start)(int signum, void* arg),
	void *arg)
{
	int rc, launched;

	assert(allowed_count >= 1);
	assert(start_count >= 0);
	assert(waiter_count > 0);
	assert(start_count <= allowed_count);

	x_mutex_lock(&mutex);

	/* check whether already running */
	if (x_tls_get_current_thread() || allowed_thread_count) {
		ERROR("thread already started");
		rc = X_EINVAL;
		goto error;
	}

	/* records the allowed count */
	allowed_thread_count = allowed_count;
	started_thread_count = 0;
	busy_thread_count = 0;
	afb_jobs_set_max_count(waiter_count);

	/* start at least one thread: the current one */
	launched = 1;
	while (launched < start_count) {
		rc = start_one_thread();
		if (rc != 0) {
			ERROR("Not all threads can be started");
			goto error;
		}
		launched++;
	}

	/* queue the start job */
	rc = afb_jobs_queue_lazy(NULL, 0, start, arg);
	if (rc < 0)
		goto error;

	/* run until end */
	thread_main();
	rc = 0;
error:
	x_mutex_unlock(&mutex);
	if (exit_handler) {
		exit_handler();
		exit_handler = NULL;
	}
	return rc;
}

/* a null exit handler */
static void null_exit_handler() {}

/**
 * Exit jobs threads and call handler if not NULL.
 */
void afb_sched_exit(void (*handler)())
{
	struct thread *t;

	/* request all threads to stop */
	x_mutex_lock(&mutex);

	/* set the handler */
	exit_handler = handler ?: null_exit_handler;

	/* stops the threads */
	t = threads;
	while (t) {
		t->stop = 1;
		t = t->next;
	}

	/* wake up the threads */
	evloop_wakeup();
	x_cond_broadcast(&cond);

	/* leave */
	x_mutex_unlock(&mutex);
}

/**
 * adapt the count of thread to the given count of pending jobs
 */
void afb_sched_adapt(int pending_job_count)
{
	int rc, busy, esig;

	esig = 0;
	x_mutex_lock(&mutex);
	if (exit_handler == NULL) {
		/* start a thread if needed */
		busy = busy_thread_count == started_thread_count;
		if (busy && started_thread_count < allowed_thread_count) {
			/* all threads are busy and a new can be started */
			rc = start_one_thread();
			if (rc < 0 && started_thread_count == 0)
				ERROR("can't start initial thread: %m");
			else
				busy = 0;
		}

		/* wakeup an evloop if needed */
		if (busy)
			evloop_wakeup();

		/* signal an existing job */
		esig = 1;
	}
	x_mutex_unlock(&mutex);

	/* signal an existing job */
	if (esig)
		x_cond_signal(&cond);
}
