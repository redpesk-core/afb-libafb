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

#include <stdint.h>
#include <limits.h>
#include <string.h>

#include <rp-utils/rp-verbose.h>

#include "sys/ev-mgr.h"
#include "core/afb-jobs.h"
#include "core/afb-ev-mgr.h"

#include "sys/x-mutex.h"
#include "sys/x-cond.h"
#include "sys/x-errno.h"
#include "sys/x-thread.h"

/* synchronisation of threads */
static x_mutex_t mutex = X_MUTEX_INITIALIZER;

/* event manager */
static struct ev_mgr *evmgr = 0;

/* holding the event manager */
#define INVALID_THREAD_ID 0                       /**< assume 0 is not a valid thread id */
static x_thread_t holder = INVALID_THREAD_ID;     /**< current holder */
struct waithold
{
	struct waithold *next;
	x_cond_t  cond;
};
static struct waithold *awaiters;

/**
 * Release the event manager if held currently
 */
static void unhold_evmgr()
{
	holder = INVALID_THREAD_ID;
	if (awaiters)
		x_cond_signal(&awaiters->cond);
}

/**
 * Release the event manager if held currently
 */
static int try_unhold_evmgr(x_thread_t tid)
{
	if (!evmgr || holder != tid) /* x_thread_equal? */
		return 0;
	unhold_evmgr();
	return 1;
}

/**
 * try to get the eventloop for the thread of tid
 */
static int try_hold_evmgr(x_thread_t tid)
{
	if (!evmgr && ev_mgr_create(&evmgr) < 0)
		return 0;
	if (holder == INVALID_THREAD_ID) /* x_thread_equal? */
		holder = tid;
	return holder == tid; /* x_thread_equal? */
}

int afb_ev_mgr_release(x_thread_t tid)
{
	x_mutex_lock(&mutex);
	int unheld = try_unhold_evmgr(tid);
	x_mutex_unlock(&mutex);
	return unheld;
}

struct ev_mgr *afb_ev_mgr_try_get(x_thread_t tid)
{
	x_mutex_lock(&mutex);
	int gotit = !awaiters && try_hold_evmgr(tid);
	x_mutex_unlock(&mutex);
	return gotit ? evmgr : 0;
}
struct ev_mgr *afb_ev_mgr_get(x_thread_t tid)
{
	/* lock */
	x_mutex_lock(&mutex);

	/* try to hold the event loop under lock */
	if (holder != tid && (awaiters || !try_hold_evmgr(tid)) && evmgr) {
		struct waithold wait = { 0, X_COND_INITIALIZER };
		struct waithold **piw = &awaiters;
		while (*piw) piw = &(*piw)->next;
		*piw = &wait;
		do {
			ev_mgr_wakeup(evmgr);
			x_cond_wait(&wait.cond, &mutex);
		} while(!try_hold_evmgr(tid));
		awaiters = wait.next;
	}

	/* unlock */
	x_mutex_unlock(&mutex);

	return evmgr;
}

/**
 * wakeup the event loop if needed by sending
 * an event.
 */
void afb_ev_mgr_wakeup()
{
	if (evmgr)
		ev_mgr_wakeup(evmgr);
}

int afb_ev_mgr_release_for_me()
{
	return afb_ev_mgr_release(x_thread_self());
}

struct ev_mgr *afb_ev_mgr_try_get_for_me()
{
	return afb_ev_mgr_try_get(x_thread_self());
}

struct ev_mgr *afb_ev_mgr_get_for_me()
{
	return afb_ev_mgr_get(x_thread_self());
}

int afb_ev_mgr_get_fd()
{
	struct ev_mgr *mgr = afb_ev_mgr_get_for_me();
	return ev_mgr_get_fd(mgr);
}

int afb_ev_mgr_prepare()
{
	long delayms;
	x_thread_t me = x_thread_self();
	struct ev_mgr *mgr = afb_ev_mgr_get(me);
	int njobs = afb_jobs_dequeue_multiple(0, 0, &delayms);
	int result = ev_mgr_prepare_with_wakeup(mgr, njobs ? 0 : delayms > INT_MAX ? INT_MAX : (int)delayms);
	afb_ev_mgr_release(me);
	return result;
}

int afb_ev_mgr_wait(int ms)
{
	x_thread_t me = x_thread_self();
	int result = ev_mgr_wait(afb_ev_mgr_get(me), ms);
	afb_ev_mgr_release(me);
	return result;
}

static void dispatch_mgr(struct ev_mgr *mgr, unsigned max_count_jobs)
{
	struct afb_job *job;
	ev_mgr_dispatch(mgr);
	while(max_count_jobs && (job = afb_jobs_dequeue(0))) {
		afb_jobs_run(job);
		max_count_jobs--;
	}
}

void afb_ev_mgr_dispatch()
{
	x_thread_t me = x_thread_self();
	dispatch_mgr(afb_ev_mgr_get(me), 1);
	afb_ev_mgr_release(me);
}

int afb_ev_mgr_wait_and_dispatch(int ms)
{
	x_thread_t me = x_thread_self();
	struct ev_mgr *mgr = afb_ev_mgr_get(me);
	int rc = ev_mgr_wait(mgr, ms);
	if (rc >= 0)
		dispatch_mgr(mgr, 1);
	afb_ev_mgr_release(me);
	return rc;
}

int afb_ev_mgr_add_fd(
	struct ev_fd **efd,
	int fd,
	uint32_t events,
	ev_fd_cb_t handler,
	void *closure,
	int autounref,
	int autoclose
) {
	struct ev_mgr *mgr = afb_ev_mgr_get_for_me();
	return ev_mgr_add_fd(mgr, efd, fd, events, handler, closure, autounref, autoclose);
}

int afb_ev_mgr_add_prepare(
	struct ev_prepare **prep,
	ev_prepare_cb_t handler,
	void *closure
) {
	struct ev_mgr *mgr = afb_ev_mgr_get_for_me();
	return ev_mgr_add_prepare(mgr, prep, handler, closure);
}

int afb_ev_mgr_add_timer(
	struct ev_timer **timer,
	int absolute,
	time_t start_sec,
	unsigned start_ms,
	unsigned count,
	unsigned period_ms,
	unsigned accuracy_ms,
	ev_timer_cb_t handler,
	void *closure,
	int autounref
) {
	struct ev_mgr *mgr = afb_ev_mgr_get_for_me();
	return ev_mgr_add_timer(mgr, timer, absolute, start_sec, start_ms, count, period_ms, accuracy_ms, handler, closure, autounref);
}
