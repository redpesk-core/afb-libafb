/*
 Copyright (C) 2015-2020 IoT.bzh Company

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

#include "libafb-config.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include <check.h>
#include <signal.h>

#include "core/afb-jobs.h"
#include "core/afb-sig-monitor.h"
#include "sys/verbose.h"

/*********************************************************************/

#define NB_TEST_JOBS 10

#define i2p(x)  ((void*)((intptr_t)(x)))
#define p2i(x)  ((int)((intptr_t)(x)))

volatile int gval;

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
	else if (sig == SIGVTALRM) gval *= -1;
	else gval+=10;
}

/*********************************************************************/

START_TEST (simple)
{
	int i,r;
	struct afb_job *job;

	job = afb_jobs_dequeue(0);
	ck_assert(job == NULL);

	gval = 0;
	for(i=0; i<NB_TEST_JOBS; i++){
		r = afb_jobs_post(NULL, 0, 1, test_job, i2p(i+1));
		ck_assert_int_gt(r,0);
	}
	ck_assert_int_eq(afb_jobs_get_pending_count(), 10);
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

START_TEST (timeout)
{
	int r;
	gval = 0;

	struct afb_job *job;

	r = afb_sig_monitor_init(1);
	ck_assert_int_eq(r, 0);
	// check that a job get killed if it goes over it's timeout
	r = afb_jobs_post(NULL, 0, 1, timeout_test_job, i2p(3));
	ck_assert_int_gt(r,0);
	job = afb_jobs_dequeue(0);
	afb_jobs_run(job);
	// if gval = -2 it means that the job has been run once and have been killed
	ck_assert_int_eq(gval, -2);

}
END_TEST


START_TEST (max_count)
{
	int r, i;
	struct afb_job *job;
	afb_jobs_set_max_count(8);
	ck_assert_int_eq(afb_jobs_get_max_count(), 8);
	for(i=0; i<10; i++){
		r = afb_jobs_post(NULL, 0, 1, test_job, i2p(i+1));
		if(i<8) ck_assert_int_gt(r, 0);
		else ck_assert_int_ne(r, 0);
	}
	gval = 0;
	for(i=0; i<10; i++){
		job = afb_jobs_dequeue(0);
		if(i<8) ck_assert(job != NULL);
		else ck_assert(job == NULL);
		ck_assert_int_eq(gval, 0);
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
			addtest(timeout);
			addtest(max_count);
	return !!srun();
}
