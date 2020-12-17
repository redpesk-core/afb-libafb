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
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include "core/afb-jobs.h"
#include "core/afb-sched.h"
#include "sys/ev-mgr.h"
#include "core/afb-sig-monitor.h"
#include "sys/verbose.h"
#include "sys/x-mutex.h"
#include "sys/x-cond.h"
#include "sys/x-thread.h"
#include "sys/x-errno.h"

#define EVENT_TIMEOUT_TOP  	((uint64_t)-1)
#define EVENT_TIMEOUT_CHILD	((uint64_t)10000)

#define DEBUGGING 0

#if DEBUGGING
#include <stdio.h>

enum tread_state {
	ts_Idle = 0,
	ts_Running = 1,
	ts_Event_Handling = 2,
	ts_Waiting = 3,
	ts_Leaving = 4,
	ts_Stopped = 5,
	ts_Stopping = 6,
	ts_Blocked = 7,
	ts_Acquiring = 8
};

#define STATE_BIT_COUNT 4
#define ID_BIT_COUNT    20

static const char *state_names[] = {
	"Idle",
	"Running",
	"Event-Handling",
	"Waiting",
	"Leaving",
	"Stopped",
	"Stopping",
	"Blocked",
	"Acquiring"
};

#endif

/** Description of threads */
struct thread
{
	/** next thread of the list */
	struct thread *next;

	/** stop requested */
	volatile unsigned stop: 1;

	/** leave requested */
	volatile unsigned leave: 1;

#if DEBUGGING
	/** state */
	unsigned state: STATE_BIT_COUNT;

	/** state saved */
	unsigned statesave: STATE_BIT_COUNT;

	/** id */
	unsigned id: ID_BIT_COUNT;
#endif
};

/**
 * Description of synchronous jobs
 */
struct sync_job
{
	/** synchronize condition */
	x_cond_t condsync;

	/** return code */
	int rc;

	/** timeout of the job */
	int timeout;

	/** group of the job */
	const void *group;

	/** handler */
	void (*handler)(int, void*);

	/** the argument of the job's callback */
	void *arg;

	union {
		/** the synchronous callback for call_sync */
		void (*callback)(int, void*);

		/** the entering callback */
		void (*enter)(int signum, void *closure, struct afb_sched_lock *afb_sched_lock);
	};
};

/* synchronisation of threads */
static x_mutex_t mutex = X_MUTEX_INITIALIZER;
static x_cond_t  cond = X_COND_INITIALIZER;
static x_cond_t  condhold = X_COND_INITIALIZER;

/* counts for threads */
static int allowed_thread_count = 0;	/**< allowed count of threads */
static int started_thread_count = 0;	/**< started count of threads */
static int waiting_thread_count = 0;	/**< count of waiting threads */
static int hold_request_count = 0;	/**< count of request to hold the event loop */
static int in_event_loop = 0;		/**< is waiting events */

/* list of threads */
static struct thread *threads;

/* event loop */
static struct ev_mgr *evmgr;

/* exit manager */
static void (*exit_handler)();

/* current thread */
X_TLS(struct thread,current_thread)

static inline struct thread *get_me()
{
	return x_tls_get_current_thread();
}

static inline void set_me(struct thread *me)
{
	x_tls_set_current_thread(me);
}

/* debugging */
#if DEBUGGING

//#define PDBG(...)               DEBUG(__VA_ARGS__)
#define PDBG(...)                 fprintf(stderr, __VA_ARGS__)
#define DUMP_THREAD_STATES        dump_thread_states()
#define DUMP_THREAD_ENTER(thr)    dump_thread_enter(thr)
#define DUMP_THREAD_LEAVE(thr)    dump_thread_leave(thr)
#define THREAD_STATE_SET(thr,st)  dump_thread_state_set(thr,st)
#define THREAD_STATE_PUSH(thr,st) (thr)->statesave = (thr)->state, THREAD_STATE_SET(thr,st)
#define THREAD_STATE_POP(thr)     THREAD_STATE_SET(thr,(thr)->statesave)

static void dump_thread_state_set(struct thread *thr, unsigned st)
{
	PDBG("    [SET] %u: %s -> %s\n", (unsigned)thr->id, state_names[thr->state], state_names[st]);
	thr->state = st & ((1 << STATE_BIT_COUNT) - 1);
}

static void dump_thread_states()
{
	struct thread *thr = threads;
	PDBG("=== BEGIN STATE started=%d, waiting=%d, in-loop=%d, allowed=%d\n",
				started_thread_count, waiting_thread_count, in_event_loop, allowed_thread_count);
	while (thr) {
		PDBG("    %u: %s\n", (unsigned)thr->id, state_names[thr->state]);
		thr = thr->next;
	}
	PDBG("=== END STATE\n");
}

static void dump_thread_enter(struct thread *thr)
{
	static unsigned idgen = 0;

	thr->id = ++idgen & ((1U << ID_BIT_COUNT) - 1);
	PDBG("=== ENTER %u %d/%d\n", (unsigned)thr->id, started_thread_count, allowed_thread_count);
	THREAD_STATE_SET(thr, ts_Idle);
}


static void dump_thread_leave(struct thread *thr)
{
	PDBG("=== LEAVE %u %d/%d\n", (unsigned)thr->id, started_thread_count, allowed_thread_count);
}

#else
#define PDBG(...)
#define DUMP_THREAD_STATES
#define DUMP_THREAD_ENTER(thr)
#define DUMP_THREAD_LEAVE(thr)
#define THREAD_STATE_SET(thr,st)
#define THREAD_STATE_PUSH(thr,st)
#define THREAD_STATE_POP(thr)
#endif


/**
 * wakeup the event loop if needed by sending
 * an event.
 */
static void evloop_wakeup()
{
	if (evmgr)
		ev_mgr_wakeup(evmgr);
}

/**
 * Release the currently held event loop
 */
static void evloop_release(void *me)
{
	if (evmgr && !ev_mgr_try_change_holder(evmgr, me, 0)) {
		if (hold_request_count)
			x_cond_signal(&condhold);
	}
}

/**
 * get the eventloop for the current thread
 */
static int evloop_get(void *me)
{
	return evmgr && ev_mgr_try_change_holder(evmgr, 0, me) == me;
}

/**
 * run the event loop
 */
static void evloop_sig_run(int signum, void *closure)
{
	if (signum) {
		ERROR("Signal %s catched in evloop", strsignal(signum));
		ev_mgr_recover_run(evmgr);
	}
	else {
		long delayms = (long)(intptr_t)closure;
		int to = delayms < 0 ? -1 : delayms <= INT_MAX ? (int)delayms : INT_MAX;
		int rc = ev_mgr_prepare(evmgr);
		if (rc >= 0) {
			rc = ev_mgr_wait(evmgr, to);
			if (rc > 0)
				ev_mgr_dispatch(evmgr);
		}
	}
}

/**
 * Main processing loop of internal threads with processing jobs.
 * The loop must be called with the mutex locked
 * and it returns with the mutex locked.
 * @param me the description of the thread to use
 * TODO: how are timeout handled when reentering?
 */
static void thread_run(int ismain)
{
	struct thread me;
	struct thread **prv;
	struct afb_job *job;
	long delayms;

	DUMP_THREAD_ENTER(&me);

	/* initialize description of itself */
	me.stop = exit_handler != NULL;

	/* link to the list */
	me.next = threads;
	threads = &me;
	set_me(&me);

	/* initiate thread tempo */
	afb_sig_monitor_init_timeouts();

	/* loop until stopped */
	while (!me.stop) {

		/* get a job */
		job = afb_jobs_dequeue(&delayms);
		if (job) {
			/* run the job */
			THREAD_STATE_SET(&me, ts_Running);
			x_mutex_unlock(&mutex);
			afb_jobs_run(job);
			x_mutex_lock(&mutex);
			THREAD_STATE_SET(&me, ts_Idle);

			/* release the current event loop */
			evloop_release(&me);

		/* no job, check if stopping is possible */
		} else if (!ismain && started_thread_count > allowed_thread_count) {
			THREAD_STATE_SET(&me, ts_Leaving);
			me.stop = 1;

		/* no job, check if stopping is possible */
		} else if (ismain && allowed_thread_count == 0 && started_thread_count == 1) {
			THREAD_STATE_SET(&me, ts_Leaving);
			me.stop = 1;

		/* no job, no stop, check if event loop waits handling */
		} else if (!hold_request_count && allowed_thread_count && !in_event_loop && evloop_get(&me)) {

			/* setup event loop */
			in_event_loop = 1;
			THREAD_STATE_SET(&me, ts_Event_Handling);

			/* run the events */
			x_mutex_unlock(&mutex);
			afb_sig_monitor_run(0, evloop_sig_run, (void*)(intptr_t)delayms);
			x_mutex_lock(&mutex);

			/* release the current event loop */
			THREAD_STATE_SET(&me, ts_Idle);
			evloop_release(&me);
			in_event_loop = 0;

		/* no job, no stop and no event loop */
		} else {
			THREAD_STATE_SET(&me, ts_Waiting);
			waiting_thread_count++;
			if (waiting_thread_count == started_thread_count)
				ERROR("Entering job deep sleep! Check your bindings.");
			x_cond_wait(&cond, &mutex);
			THREAD_STATE_SET(&me, ts_Idle);
		}
	}
	started_thread_count--;
	THREAD_STATE_SET(&me, ts_Stopping);

	/* cleanup */
	evloop_release(&me);
	set_me(0);

	/* unlink */
	prv = &threads;
	while (*prv != &me)
		prv = &(*prv)->next;
	*prv = me.next;

	/* terminate */
	afb_sig_monitor_clean_timeouts();

	/* avoid deep sleep */
	if (waiting_thread_count && waiting_thread_count >= started_thread_count) {
		PDBG("    >>>> SIGNAL-DEEP-SLEEP\n");
		waiting_thread_count--;
		x_cond_signal(&cond);
	}

	DUMP_THREAD_LEAVE(&me);
}

/**
 * Entry point for created threads.
 * @param data not used
 * @return NULL
 */
static void thread_starter(void *data)
{
	x_mutex_lock(&mutex);
	thread_run(0);
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

	started_thread_count++;
	rc = x_thread_create(&tid, thread_starter, NULL, 1);
	if (rc < 0) {
		CRITICAL("not able to start thread: %s", strerror(-rc));
		started_thread_count--;
	}
	return rc;
}

/**
 * Adapt the current threading to current job requirement
 */
static void adapt(int delayed)
{
	DUMP_THREAD_STATES;
	if (delayed && in_event_loop) {
		PDBG("    >>>> WAKEUP FOR DELAYED\n");
		evloop_wakeup();
	}
	if (waiting_thread_count) {
		PDBG("    >>>> SIGNAL\n");
		waiting_thread_count--;
		x_cond_signal(&cond);
	}
	else if (started_thread_count < allowed_thread_count) {
		PDBG("    >>>> START-THREAD\n");
		start_one_thread();
	}
	else if (in_event_loop && !delayed) {
		PDBG("    >>>> WAKEUP\n");
		evloop_wakeup();
	}
	else if (!started_thread_count) {
		PDBG("    >>>> START-THREAD-EXTRA\n");
		start_one_thread();
	}
	else {
		PDBG("    >>>> NOTHING\n");
	}
}

/**
 * Queues the job as described by parameters and takes the
 * actions to adapt thread pool to treat it.
 */
static int post_job(
	const void *group,
	long delayms,
	int timeout,
	void (*callback)(int, void*),
	void *arg
) {
	int rc = afb_jobs_post(group, delayms, timeout, callback, arg);
	if (rc > 0) {
		adapt(delayms > 0);
		rc = 0;
	}
	return rc;
}

/* Schedule the given job */
int afb_sched_post_job(
	const void *group,
	long delayms,
	int timeout,
	void (*callback)(int, void*),
	void *arg
) {
	int rc;

	x_mutex_lock(&mutex);
	rc = post_job(group, delayms, timeout, callback, arg);
	x_mutex_unlock(&mutex);
	return rc;
}

/* call a monitored routine synchronousely */
void afb_sched_call_sync(
		void (*callback)(int, void*),
		void *arg
) {
	struct thread *me;
	/* lock */
	me = get_me();
	if (me) {
		x_mutex_lock(&mutex);
		THREAD_STATE_PUSH(me, ts_Blocked);
		evloop_release(me);
		started_thread_count--;
		adapt(0);
		x_mutex_unlock(&mutex);
	}
	afb_sig_monitor_run(0, callback, arg);
	if (me) {
		x_mutex_lock(&mutex);
		started_thread_count++;
		THREAD_STATE_POP(me);
		x_mutex_unlock(&mutex);
	}
}

/**
 * Internal helper for synchronous jobs. It queues
 * the job and waits for its asynchronous completion.
 */
static void do_sync_cb(int signum, void *closure)
{
	int rc;
	struct sync_job *sync = closure;

	if (signum != 0)
		rc = X_EINTR;
	else {
		x_cond_init(&sync->condsync);
		x_mutex_lock(&mutex);
		rc = post_job(sync->group, 0, sync->timeout, sync->handler, sync);
		if (rc >= 0)
			x_cond_wait(&sync->condsync, &mutex);
		x_mutex_unlock(&mutex);
	}
	sync->rc = rc;
}

/**
 * Internal helper function for 'afb_jobs_enter'.
 * @see afb_jobs_enter, afb_jobs_leave
 */
static void enter_cb(int signum, void *closure)
{
	struct sync_job *sync = closure;
	sync->enter(signum, sync->arg, (struct afb_sched_lock*)&sync->condsync);
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
) {
	struct sync_job sync;

	sync.group = group;
	sync.timeout = timeout;
	sync.enter = callback;
	sync.arg = closure;
	sync.handler = enter_cb;

	afb_sched_call_sync(do_sync_cb, &sync);
	return sync.rc;
}

/**
 * Unlocks the execution flow designed by 'jobloop'.
 * @param afb_sched_lock indication of the flow to unlock
 * @return 0 in case of success of -1 on error
 */
int afb_sched_leave(struct afb_sched_lock *afb_sched_lock)
{
	int rc;
	x_cond_t *cond = (x_cond_t*)afb_sched_lock;
	x_mutex_lock(&mutex);
	rc = x_cond_signal(cond);
	x_mutex_unlock(&mutex);
	return rc;
}

/**
 * Internal helper function for 'afb_sched_call_job_sync'.
 * @see afb_sched_call_job_sync
 */
static void call_cb(int signum, void *closure)
{
	struct sync_job *sync = closure;
	sync->callback(signum, sync->arg);
	x_mutex_lock(&mutex);
	x_cond_signal(&sync->condsync);
	x_mutex_unlock(&mutex);
}

/* call a job synchronousely */
int afb_sched_call_job_sync(
		const void *group,
		int timeout,
		void (*callback)(int, void*),
		void *arg)
{
	struct sync_job sync;

	sync.group = group;
	sync.timeout = timeout;
	sync.callback = callback;
	sync.arg = arg;
	sync.handler = call_cb;

	afb_sched_call_sync(do_sync_cb, &sync);
	return sync.rc;
}

/**
 * Ensure that the current running thread can control the event loop.
 */
struct ev_mgr *afb_sched_acquire_event_manager()
{
	struct thread *me;
	void *holder;

	/* lock */
	x_mutex_lock(&mutex);

	/* creates the evloop on need */
	if (!evmgr) {
		ev_mgr_create(&evmgr);
		if (!evmgr) {
			x_mutex_unlock(&mutex);
			return 0;
		}
	}

	/* get the thread environment if existing */
	me = get_me();

	/* try to hold the event loop under lock */
	holder = me ? (void*)me : (void*)&holder;
	if (holder != ev_mgr_try_change_holder(evmgr, 0, holder)) {
		if (me) {
			THREAD_STATE_PUSH(me, ts_Acquiring);
		}

		/* wait for the event loop */
		hold_request_count++;
		do {
			ev_mgr_wakeup(evmgr);
			x_cond_wait(&condhold, &mutex);
		}
		while (holder != ev_mgr_try_change_holder(evmgr, 0, holder));
		hold_request_count--;

		if (me) {
			THREAD_STATE_POP(me);
		}
	}

	/* warn if faked */
	if (!me) {
		/*
		 * Releasing it is needed because there is no way to guess
		 * when it has to be released really. But here is where it is
		 * hazardous: if the caller modifies the eventloop when it
		 * is waiting, there is no way to make the change effective.
		 * A workaround to achieve that goal is for the caller to
		 * require the event loop a second time after having modified it.
		 */
		ERROR("Requiring event manager/loop from outside of binder's callback is hazardous!");
		if (verbose_wants(Log_Level_Info))
			afb_sig_monitor_dumpstack();
		evloop_release(holder);
	}

	/* unlock */
	x_mutex_unlock(&mutex);
	return evmgr;
}

/**
 * Exit jobs threads and call handler if not NULL.
 */
static void exit_threads(int force, void (*handler)())
{
	struct thread *t;

	/* set the handler */
	exit_handler = handler;

	/* ask to leave */
	allowed_thread_count = 0;

	/* stops the threads if forced */
	if (force) {
		for (t = threads ; t ; t = t->next) {
			THREAD_STATE_SET(t, ts_Stopped);
			t->stop = 1;
		}
	}

	/* wake up the threads */
	evloop_wakeup();
	if (waiting_thread_count) {
		PDBG("    >>>> SIGNAL-EXIT\n");
		waiting_thread_count--;
		x_cond_signal(&cond);
	}
}

/* Exit threads and call handler if not NULL. */
void afb_sched_exit(int force, void (*handler)())
{
	x_mutex_lock(&mutex);
	exit_threads(force, handler);
	x_mutex_unlock(&mutex);
}

/* Enter the jobs processing loop */
int afb_sched_start(
	int allowed_count,
	int start_count,
	int waiter_count,
	void (*start)(int signum, void* arg),
	void *arg)
{
	int rc;

	assert(allowed_count >= 1);
	assert(start_count >= 0);
	assert(waiter_count > 0);
	assert(start_count <= allowed_count);

	x_mutex_lock(&mutex);

	/* check whether already running */
	if (get_me() || allowed_thread_count) {
		ERROR("thread already started");
		rc = X_EINVAL;
		goto error;
	}

	/* records the allowed count */
	allowed_thread_count = allowed_count;
	started_thread_count = 0;
	waiting_thread_count = 0;
	afb_jobs_set_max_count(waiter_count);

	/* start at least one thread: the current one */
	while (started_thread_count + 1 < start_count) {
		rc = start_one_thread();
		if (rc != 0) {
			ERROR("Not all threads can be started");
			exit_threads(1, 0);
			goto error;
		}
	}

	/* queue the start job */
	rc = afb_jobs_post(NULL, 0, 0, start, arg);
	if (rc < 0)
		goto error;

	/* run until end */
	started_thread_count++;
	thread_run(1);
	rc = 0;
error:
	allowed_thread_count = 0;
	x_mutex_unlock(&mutex);
	if (exit_handler) {
		exit_handler();
		exit_handler = 0;
	}
	return rc;
}
