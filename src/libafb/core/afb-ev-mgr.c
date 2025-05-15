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

#define SAME_TID(x,y) ((x) == (y))  /* x_thread_equal? */

static int ensure_evmgr()
{
	return evmgr ? 0 : ev_mgr_create(&evmgr);
}

static void release_locked()
{
	holder = INVALID_THREAD_ID;
	if (awaiters != NULL)
		x_cond_signal(&awaiters->cond);
}

static void release_unlocked()
{
	x_mutex_lock(&mutex);
	release_locked();
	x_mutex_unlock(&mutex);
}

/* set the holder and ensure the evmgr
 * release the lock
 * return 1 if held or a negative error code
 */
static int hold_locked(x_thread_t tid)
{
	int rc = ensure_evmgr();
	if (rc < 0) {
		release_locked();
		x_mutex_unlock(&mutex);
		return rc;
	}
	holder = tid;
	x_mutex_unlock(&mutex);
	return 1;
}

/**
 * try to get the evmgr for the thread tid.
 * return 1 if gotten or 0 otherwise
 * or a negative value on error
 */
static int try_get(x_thread_t tid)
{
	int gotit = 0;

	x_mutex_lock(&mutex);

	/* check if not used */
	if (SAME_TID(holder, INVALID_THREAD_ID) && awaiters == NULL)
		/* hold it now */
		gotit = hold_locked(tid);
	else {
		/* check if used by tid */
		if (SAME_TID(holder, tid)) {
			/* got it if not awaiten or else release it */
			if (awaiters == NULL)
				gotit = 1;
			else
				release_locked();
		}
		x_mutex_unlock(&mutex);
	}
	return gotit;
}

/**
 * get the evmgr for the thread tid.
 * return 1 if gotten or 0 if the thread already got the evmgr
 */
static int get(x_thread_t tid)
{
	if (SAME_TID(holder, tid))
		return 0;

	/* lock */
	x_mutex_lock(&mutex);
	if (!SAME_TID(holder, INVALID_THREAD_ID)) {
		struct waithold wait = { 0, X_COND_INITIALIZER };
		struct waithold **piw = &awaiters;
		while (*piw) piw = &(*piw)->next;
		*piw = &wait;
		afb_ev_mgr_wakeup();
		x_cond_wait(&wait.cond, &mutex);
		awaiters = wait.next;
	}

	/* hold it now */
	return hold_locked(tid);
}

int afb_ev_mgr_init()
{
	int rc;

	x_mutex_lock(&mutex);
	rc = ensure_evmgr();
	x_mutex_unlock(&mutex);
	return rc;
}

int afb_ev_mgr_release(x_thread_t tid)
{
	if (!SAME_TID(holder, tid))
		return 0;
	release_unlocked();
	return 1;
}

struct ev_mgr *afb_ev_mgr_try_get(x_thread_t tid)
{
	return try_get(tid) > 0 ? evmgr : NULL;
}

struct ev_mgr *afb_ev_mgr_get(x_thread_t tid)
{
	get(tid);
	return evmgr;
}

/**
 * wakeup the event loop if needed by sending
 * an event.
 */
int afb_ev_mgr_wakeup()
{
	return evmgr && ev_mgr_wakeup(evmgr);
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
	int result;
	long delayms;
	x_thread_t me = x_thread_self();
	struct ev_mgr *mgr = afb_ev_mgr_get(me);
	int njobs = afb_jobs_dequeue_multiple(0, 0, &delayms);
	result = ev_mgr_prepare_with_wakeup(mgr, njobs ? 0 : delayms > INT_MAX ? INT_MAX : (int)delayms);
	return result;
}

int afb_ev_mgr_wait(int ms)
{
	int result;
	x_thread_t me = x_thread_self();
	struct ev_mgr *mgr = afb_ev_mgr_get(me);
	result = ev_mgr_wait(mgr, ms);
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
	struct ev_mgr *mgr = afb_ev_mgr_get(me);
	dispatch_mgr(mgr, 1);
}

int afb_ev_mgr_wait_and_dispatch(int ms)
{
	int rc;
	x_thread_t me = x_thread_self();
	struct ev_mgr *mgr = afb_ev_mgr_get(me);
	rc = ev_mgr_wait(mgr, ms);
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
	x_thread_t me = x_thread_self();
	int rc, got = get(me);
	if (got < 0)
		rc = got;
	else {
		rc = ev_mgr_add_fd(evmgr, efd, fd, events, handler, closure,
		                   autounref, autoclose);
		if (got)
			afb_ev_mgr_release(me);
	}
	return rc;
}

int afb_ev_mgr_add_prepare(
	struct ev_prepare **prep,
	ev_prepare_cb_t handler,
	void *closure
) {
	x_thread_t me = x_thread_self();
	int rc, got = get(me);
	if (got < 0)
		rc = got;
	else {
		rc = ev_mgr_add_prepare(evmgr, prep, handler, closure);
		if (got)
			afb_ev_mgr_release(me);
	}
	return rc;
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
	x_thread_t me = x_thread_self();
	int rc, got = get(me);
	if (got < 0)
		rc = got;
	else {
		rc = ev_mgr_add_timer(evmgr, timer, absolute, start_sec,
		                      start_ms, count, period_ms, accuracy_ms,
		                      handler, closure, autounref);
		if (got)
			afb_ev_mgr_release(me);
	}
	return rc;
}

void afb_ev_mgr_prepare_wait_dispatch(int delayms, int release)
{
	int rc;
	int tempo = delayms < 0 ? -1 : delayms;
	x_thread_t me = x_thread_self();
	struct ev_mgr *mgr = afb_ev_mgr_get(me);
	rc = ev_mgr_prepare(mgr);
	if (rc >= 0) {
		rc = ev_mgr_wait(mgr, tempo);
		if (rc > 0)
			ev_mgr_dispatch(mgr);
	}
	if (release)
		afb_ev_mgr_release(me);
}

void afb_ev_mgr_prepare_wait_dispatch_release(int delayms)
{
	afb_ev_mgr_prepare_wait_dispatch(delayms, 1);
}

void afb_ev_mgr_try_recover(x_thread_t tid)
{
	if (try_get(tid) > 0) {
		ev_mgr_recover_run(evmgr);
		afb_ev_mgr_release(tid);
	}
}

void afb_ev_mgr_try_recover_for_me()
{
	afb_ev_mgr_try_recover(x_thread_self());
}
