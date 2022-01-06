/*
 Copyright (C) 2015-2022 IoT.bzh Company

 Author: Johann Gautier <johann.gautier@iot.bzh>

 $RP_BEGIN_LICENSE$
 Commercial License Usage
  Licensees holding valid commercial IoT.bzh licenses may use this file in
  accordance with the commercial license agreement provided with the
  Software or, alternatively, in accordance with the terms contained in
  a written agreement between you and The IoT.bzh Company. For licensing terms
  and conditions see https://www.iot.bzh/terms-conditions. For further
  information use the contact form at https://www.iot.bzh/contact.

 GNU General Public License Usage
  Alternatively, this file may be used under the terms of the GNU General
  Public license version 3. This license is as published by the Free Software
  Foundation and appearing in the file LICENSE.GPLv3 included in the packaging
  of this file. Please review the following information to ensure the GNU
  General Public License requirements will be met
  https://www.gnu.org/licenses/gpl-3.0.html.
 $RP_END_LICENSE$
*/


#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <check.h>
#include <signal.h>

#include "core/afb-jobs.h"
#include "core/afb-sig-monitor.h"
#include "sys/verbose.h"

/*********************************************************************/

void nsleep(long usec) /* like nsleep */
{
	struct timespec ts = { .tv_sec = (usec / 1000000), .tv_nsec = (usec % 1000000) * 1000 };
	nanosleep(&ts, NULL);
}

/*********************************************************************/

#define NB_TEST_JOBS 3 // must be >= 3

#define i2p(x)  ((void*)((intptr_t)(x)))
#define p2i(x)  ((int)((intptr_t)(x)))

#define DELAY 5

#define TEST_GROUPE i2p(666)

volatile int gval, gsig;

void test_job(int sig, void * arg){
	fprintf(stderr, "test job received sig %d with arg %d\n", sig, p2i(arg));
	gval += p2i(arg);
}

void timeout_test_job(int sig, void * arg){
	fprintf(stderr, "timeout_test_job received sig %d with arg %d\n", sig, p2i(arg));
	if(sig == 0){
		gval+=2;
		for(;;);
		gval++;
	}
	else if ( sig == SIGVTALRM || sig == SIGABRT ) {
		gval *= -1;
		gsig = sig;
	}
	else gval+=10;
}

/*********************************************************************/

START_TEST (simple)
{
	fprintf(stderr, "\n*********************** post, dequeue, run ***********************\n");

	int i,r;
	struct afb_job *job;

	if(afb_jobs_get_max_count() < NB_TEST_JOBS)
		afb_jobs_set_max_count(NB_TEST_JOBS);

	job = afb_jobs_dequeue(0);
	ck_assert(job == NULL);

	gval = 0;
	for(i=0; i<NB_TEST_JOBS; i++){
		r = afb_jobs_post(NULL, 0, 1, test_job, i2p(i+1));
		ck_assert_int_gt(r,0);
	}
	ck_assert_int_eq(afb_jobs_get_pending_count(), NB_TEST_JOBS);
	ck_assert_int_eq(gval, 0);

	for(i=0; i<NB_TEST_JOBS; i++){
		job = afb_jobs_dequeue(0);
		ck_assert(job != NULL);
		ck_assert_int_eq(afb_jobs_get_pending_count(), NB_TEST_JOBS-i-1);
		gval = 0;
		afb_jobs_run(job);
		ck_assert_int_eq(gval, i+1);
	}
}
END_TEST

START_TEST (max_count)
{
	fprintf(stderr, "\n*********************** max_count ***********************\n");

	int r, i;
	struct afb_job *job;
	afb_jobs_set_max_count(NB_TEST_JOBS-2);
	ck_assert_int_eq(afb_jobs_get_max_count(), NB_TEST_JOBS-2);
	for(i=0; i<NB_TEST_JOBS; i++){
		r = afb_jobs_post(NULL, 0, 1, test_job, i2p(i+1));
		if(i<NB_TEST_JOBS-2) ck_assert_int_gt(r, 0);
		else ck_assert_int_ne(r, 0);
	}
	gval = 0;
	for(i=0; i<NB_TEST_JOBS; i++){
		job = afb_jobs_dequeue(0);
		if(i<NB_TEST_JOBS-2) ck_assert(job != NULL);
		else ck_assert(job == NULL);
		ck_assert_int_eq(gval, 0);
	}
}
END_TEST

START_TEST(job_aborting)
{
    fprintf(stderr, "\n*********************** job_aborting and timeout ***********************\n");

	int r, i, jobId[3];
	gval = 0;

	struct afb_job *job;

	if(afb_jobs_get_max_count() < NB_TEST_JOBS)
		afb_jobs_set_max_count(3);
	r = afb_sig_monitor_init(1);
	ck_assert_int_eq(r, 0);

	// enqueue endless jobs to check canceling, aborting, and timeout
	for(i=0; i<3; i++){
		jobId[i] = afb_jobs_post(NULL, 0, 1, timeout_test_job, i2p(i+1));
		ck_assert_int_gt(jobId[i],0);
	}

	/* check afb_jobs_cancel */
	job = afb_jobs_dequeue(0);
	afb_jobs_cancel(job);
	ck_assert_int_eq(gval, 0);
	ck_assert_int_eq(gsig, SIGABRT);

	/* check afb_jobs_cancel */
	job = afb_jobs_dequeue(0);
	afb_jobs_abort(jobId[1]);
	ck_assert_int_eq(gval, 0);
	ck_assert_int_eq(gsig, SIGABRT);

	/* test job timeout */
	job = afb_jobs_dequeue(0);
	afb_jobs_run(job);
	// if gval = -2 it means that the job has been run once and have been killed
	ck_assert_int_eq(gval, -2);
	ck_assert_int_eq(gsig, SIGVTALRM);
}
END_TEST

static uint64_t getnow()
{
	struct timespec ts;
	ck_assert_int_eq(clock_gettime(CLOCK_MONOTONIC, &ts),0);
	/* X.10^-6 = X.(2^-6 * 5^-6) = X.(2^-6 / 15625) = (X >> 6) / 15625 */
	return (uint64_t)(ts.tv_sec * 1000) + (uint64_t)((ts.tv_nsec >> 6) / 15625);
}

START_TEST(job_delayed)
{
	fprintf(stderr, "\n*********************** job_delayed ***********************\n");

	int r, i;
	gval = 0;
	long delay;

	uint64_t start, t;

	struct afb_job *job;

	// initialisation of jobs handler
	if(afb_jobs_get_max_count() < NB_TEST_JOBS)
		afb_jobs_set_max_count(NB_TEST_JOBS);
	r = afb_sig_monitor_init(1);
	ck_assert_int_eq(r, 0);

	// enqueue simple jobs, the first one with no delay an the others with a delay
	for (i=0; i<NB_TEST_JOBS; i++){
		r = afb_jobs_post(TEST_GROUPE, DELAY*i, 1, test_job, i2p(i+1));
		ck_assert_int_gt(r,0);
	}

	// start time monitoring
	start = getnow();

	fprintf(stderr, "### Job 1 (no delay) ###\n");

	// check that the first job can start directly
	job = afb_jobs_dequeue(&delay);
	fprintf(stderr, "delay = %ld\n", delay);
	ck_assert_int_eq(delay, 0);
	ck_assert_ptr_ne(job, NULL);
	afb_jobs_run(job);
	ck_assert_int_eq(gval, 1);

	for (i=1; i<NB_TEST_JOBS; i++){
		gval = 0;

		fprintf(stderr, "\n### Job %d (%dms delay) ###\n", i+1, DELAY*i);

		// check that the job is not available yet
		job = afb_jobs_dequeue(&delay);
		fprintf(stderr, "delay = %ld spent time = %ld\n", delay, getnow()-start);
		ck_assert_ptr_eq(job, NULL);
		ck_assert_int_le(delay, DELAY);


		// wait for the delay to end
		fprintf(stderr, "wait to reach %dms after start...    ", DELAY*i);
		t = getnow();
		while((getnow()-start) < (long unsigned int)(DELAY*i))
			nsleep(100);
		fprintf(stderr, "slept %ldms\n", getnow()-t);

		// then check that the job is now available
		job = afb_jobs_dequeue(&delay);
		fprintf(stderr, "delay = %ld spent time = %ldms\n", delay, getnow()-start);
		ck_assert_ptr_ne(job, NULL);
		ck_assert_int_eq(delay, 0);
		afb_jobs_run(job);
		ck_assert_int_eq(gval, i+1);
	}
}
END_TEST


/*********************************************************************/

static Suite *suite;
static TCase *tcase;

void mksuite(const char *name) { suite = suite_create(name); }
void addtcase(const char *name) { tcase = tcase_create(name); suite_add_tcase(suite, tcase); }
#define addtest(test) tcase_add_test(tcase, test)
int srun()
{
	int nerr;
	SRunner *srunner = srunner_create(suite);
	srunner_run_all(srunner, CK_NORMAL);
	nerr = srunner_ntests_failed(srunner);
	srunner_free(srunner);
	return nerr;
}

int main(int ac, char **av)
{
	mksuite("afb-jobs");
		addtcase("afb-jobs");
			addtest(simple);
			addtest(max_count);
			addtest(job_aborting);
			addtest(job_delayed);
	return !!srun();
}
