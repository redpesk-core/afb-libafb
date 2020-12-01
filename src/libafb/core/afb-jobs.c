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

#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <limits.h>

#include "core/afb-jobs.h"
#include "core/afb-sig-monitor.h"
#include "sys/verbose.h"
#include "sys/x-mutex.h"
#include "sys/x-errno.h"

#if !defined(AFB_JOBS_DEFAULT_MAX_COUNT)
#    define AFB_JOBS_DEFAULT_MAX_COUNT    64
#endif

/** Description of a pending job */
struct afb_job
{
	struct afb_job *next;    /**< link to the next job enqueued */
	const void *group;   /**< group of the request */
	void (*callback)(int,void*);   /**< processing callback */
	void *arg;           /**< argument */
	long delayms;
#if WITH_SIG_MONITOR_TIMERS
	int timeout;         /**< timeout in second for processing the request */
#endif
	unsigned blocked: 1; /**< is an other request blocking this one ? */
	unsigned active: 1;  /**< is the request active ? */
};

/* synchronization */
static x_mutex_t mutex = X_MUTEX_INITIALIZER;

/* counts for jobs */
static int max_pending_count = AFB_JOBS_DEFAULT_MAX_COUNT;  /** maximum count of pending jobs */
static int pending_count = 0;      /** count of pending jobs */

/* queue of jobs */
static struct afb_job *pending_jobs;
static struct afb_job *free_jobs;

/* delayed jobs */
static int delayed_count = 0;
static uint64_t delayed_base;

static uint64_t getnow()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	/* X.10^-6 = X.(2^-6 * 5^-6) = X.(2^-6 / 15625) = (X >> 6) / 15625 */
	return (uint64_t)(ts.tv_sec * 1000) + (uint64_t)((ts.tv_nsec >> 6) / 15625);
}

/**
 * Create a new job with the given parameters
 * @param group    the group of the job
 * @param delayms  minimal delay in ms before starting the job
 * @param timeout  the timeout of the job (0 if none)
 * @param callback the function that achieves the job
 * @param arg      the argument of the callback
 * @return zero in case of success or a negative ernno like number
 */
static int job_add(
		const void *group,
		long delayms,
		int timeout,
		void (*callback)(int,void*),
		void *arg,
		struct afb_job **result)
{
	int rc;
	uint64_t dt;
	struct afb_job *job, *ijob, **pjob;

	/* try recycle existing job */
	job = free_jobs;
	if (job)
		free_jobs = job->next;
	else {
		/* allocation without blocking */
		job = malloc(sizeof *job);
		if (!job) {
			rc = -X_ENOMEM;
			goto end;
		}
	}
	/* update the delay */
	if (delayms) {
		if (!delayed_count)
			delayed_base = getnow();
		else {
			dt = (getnow() + delayms) - delayed_base;
			if (dt > LONG_MAX) {
				job->next = free_jobs;
				free_jobs = job;
				job = 0;
				rc = X_E2BIG;
				goto end;
			}
			delayms = (long)dt;
		}
		delayed_count++;
	}
	/* initializes the job */
	job->group = group;
	job->delayms = delayms;
	job->callback = callback;
	job->arg = arg;
#if WITH_SIG_MONITOR_TIMERS
	job->timeout = timeout;
#endif
	job->blocked = 0;
	job->active = 0;
	job->next = NULL;

	/* search end and blockers */
	pjob = &pending_jobs;
	ijob = pending_jobs;
	while (ijob) {
		if (group && ijob->group == group)
			job->blocked = 1;
		pjob = &ijob->next;
		ijob = ijob->next;
	}

	/* queue the jobs */
	*pjob = job;
	rc = ++pending_count;
end:
	*result = job;
	return rc;
}

/* enqueue the job */
int afb_jobs_post(
		const void *group,
		long delayms,
		int timeout,
		void (*callback)(int, void*),
		void *arg)
{
	struct afb_job *job;
	int rc;

	/* enter critical */
	x_mutex_lock(&mutex);

	/* check availability */
	if (pending_count >= max_pending_count) {
		ERROR("too many jobs");
		rc = X_EBUSY;
	} else {
		/* add the job */
		rc = job_add(group, delayms, timeout, callback, arg, &job);
	}

	/* leave critical */
	x_mutex_unlock(&mutex);

	return rc;
}

/**
 * Releases the processed 'job': removes it
 * from the list of jobs and unblock the first
 * pending job of the same group if any.
 * @param job the job to release
 */
static void job_release(struct afb_job *job)
{
	struct afb_job *ijob, **pjob;
	const void *group;

	/* enter critical */
	x_mutex_lock(&mutex);

	/* first dequeue the job */
	pjob = &pending_jobs;
	ijob = pending_jobs;
	while (ijob != job) {
		pjob = &ijob->next;
		ijob = ijob->next;
	}
	*pjob = job->next;

	/* then unblock jobs of the same group */
	group = job->group;
	if (group) {
		do { ijob = ijob->next; } while (ijob && ijob->group != group);
		if (ijob)
			ijob->blocked = 0;
	}

	/* recycle the job */
	job->next = free_jobs;
	free_jobs = job;

	/* leave critical */
	x_mutex_unlock(&mutex);
}

/* get next pending job */
struct afb_job *afb_jobs_dequeue(long *delayms)
{
	struct afb_job *job;
	long d;

	/* enter critical */
	x_mutex_lock(&mutex);

	/* search a job */
	if (!delayed_count)
		d = LONG_MAX;
	else {
		d = getnow() - delayed_base;
	}
	for (job = pending_jobs ; job && (job->blocked || job->delayms > d); job = job->next);
	if (job) {
		job->blocked = 1; /* mark job as blocked */
		job->active = 1; /* mark job as active */
		pending_count--;
		if (job->delayms)
			delayed_count--;
		d = 0;
	}
	else if (delayed_count) {
		d = LONG_MAX;
		for (job = pending_jobs ; job ; job = job->next)
			if (!job->blocked && job->delayms && job->delayms < d)
				d = job->delayms;
		d = (long)((delayed_base + d) - getnow());
		if (d < 0)
			d = -1;
	}
	else {
		d = -1;
	}

	/* leave critical */
	x_mutex_unlock(&mutex);

	if (delayms)
		*delayms = d;
	return job;
}

/* cancel the given job */
void afb_jobs_cancel(struct afb_job *job)
{
	job->callback(SIGABRT, job->arg);
	job_release(job);
}

/* Run the given job */
void afb_jobs_run(struct afb_job *job)
{
#if WITH_SIG_MONITOR_TIMERS
	afb_sig_monitor_run(job->timeout, job->callback, job->arg);
#else
	afb_sig_monitor_run(0, job->callback, job->arg);
#endif
	/* release the run job */
	job_release(job);
}

/* get pending count of jobs */
int afb_jobs_get_pending_count(void)
{
	return pending_count;
}

/* get maximum pending count */
int afb_jobs_get_max_count(void)
{
	return max_pending_count;
}

/* set maximum pending count */
void afb_jobs_set_max_count(int count)
{
	if (count > 0)
		max_pending_count = count;
}

/* get the count of job still active but not pending */
int afb_jobs_get_active_count(void)
{
	int count = 0;
	struct afb_job *job;

	/* enter critical */
	x_mutex_lock(&mutex);

	/* search a job */
	for (job = pending_jobs ; job ; job = job->next)
		count += (int)job->active;

	/* leave critical */
	x_mutex_unlock(&mutex);

	return count;
}
