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
#include <errno.h>
#include <sys/wait.h>

#include <check.h>
#include <signal.h>
#include <pthread.h>

#include <rp-utils/rp-verbose.h>

#include "core/afb-sig-monitor.h"
#include "core/afb-jobs.h"

/*********************************************************************/

void nsleep(long usec) /* like nsleep */
{
	struct timespec ts = { .tv_sec = (usec / 1000000), .tv_nsec = (usec % 1000000) * 1000 };
	nanosleep(&ts, NULL);
}

/*********************************************************************/

#define TRUE 1
#define FALSE 0

#define i2p(x)  ((void*)((intptr_t)(x)))
#define p2i(x)  ((int)((intptr_t)(x)))


pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_t thread;

int timeout_completed;


int gval;
int clean_timeout = FALSE;
int auto_timeout = FALSE;
int dumpstack = FALSE;
int observation;


void * timeout_backup(void * timeout){
	sleep((unsigned)p2i(timeout));
	pthread_mutex_lock(&mutex);
	timeout_completed = TRUE;
	pthread_mutex_unlock(&mutex);
	fprintf(stderr, "timeout_backup terminated after %d secondes\n", p2i(timeout));
	return NULL;
}

void test_job(int sig, void * arg){

	int i, r;

	fprintf(stderr, "test_job received sig %d with arg %d\n", sig, p2i(arg));

	if(sig == 0){
		gval+=2;
		if(dumpstack)
				afb_sig_monitor_dumpstack();
		if(p2i(arg)){
			if(clean_timeout)
				afb_sig_monitor_clean_timeouts();
			timeout_completed = FALSE;
			pthread_create(&thread, NULL, timeout_backup, arg);
			do{
				for(i=0;i<INT32_MAX; i++);
				pthread_mutex_lock(&mutex);
				r = timeout_completed;
				pthread_mutex_unlock(&mutex);
			}while(r == FALSE);
		}
		gval++;
	}
	else if (sig == SIGALRM){
		gval = -1;
	}
	else{
		gval+=10;
	}
	pthread_mutex_unlock(&mutex);

	if(p2i(arg)){
		// make sure that backup_timeout thread is done
		pthread_join(thread, NULL);
	}
}

void observe(int loglevel, const char *file, int line, const char *function, const char *fmt, va_list args){
	if(strstr(fmt,"BACKTRACE:")){
		observation++;
	}
}

/*********************************************************************/

START_TEST (run_test)
{
	gval = 0;

	fprintf(stderr,"\n*************** run_test ***************\n");

	// activate signal monitoring
	ck_assert_int_eq(afb_sig_monitor_init(TRUE), 0);

	// check that sig_monitor correctly run a job
	afb_sig_monitor_run(0, test_job, i2p(0));
	ck_assert_int_eq(gval, 3);

}
END_TEST

START_TEST (timeout_test)
{
	gval = 0;

	fprintf(stderr,"\n*************** timeout_test ***************\n");

	// activate signal monitoring
	ck_assert_int_eq(afb_sig_monitor_init(TRUE), 0);

	// check that sig_monitor timeout works
	afb_sig_monitor_run(1, test_job, i2p(2));
	ck_assert_int_eq(gval,-1);

}
END_TEST

START_TEST (clean_timeout_test)
{
	gval = 0;

	fprintf(stderr,"\n*************** clean_timeout_test ***************\n");

	// activate signal monitoring
	ck_assert_int_eq(afb_sig_monitor_init(TRUE), 0);

	// disable timeouts
	clean_timeout = TRUE;

	// run the same job with the same 1s time out with a 2 secondes backup timeout
	afb_sig_monitor_run(1, test_job, i2p(2));

	// check that the sig monitor timeout didn't pop-up
	ck_assert_int_eq(gval,3);

	clean_timeout = FALSE;

}
END_TEST

START_TEST (dumpstack_test)
{
	gval = 0;
	observation = 0;
	rp_verbose_observer = observe;

	fprintf(stderr,"\n*************** dumpstack_test ***************\n");

	// activate signal monitoring
	ck_assert_int_eq(afb_sig_monitor_init(TRUE), 0);

	// activate afb_sig_monitor_dumpstack
	dumpstack = TRUE;

	// run the job
	afb_sig_monitor_run(1, test_job, i2p(2));

	// the job have been hereased from afb sigmal monitoring
	// so the job shoud have been killed => gval = -1.
	ck_assert_int_eq(gval, -1);

	// and a BACKTRACE message should have popup => observation != 0.
	ck_assert_int_ne(observation,0);

	dumpstack = FALSE;
	rp_verbose_observer = NULL;
}
END_TEST

void on_exit_test(int status, void *args){
	fprintf(stderr, "on_exit_test was call with status = %d\n", status);
	ck_assert_int_eq(status, 1);
	abort();
}

START_TEST(sigterm_test)
{

	gval = 0;
	int status;
	fprintf(stderr, "\n***************** sigterm_test *****************\n");

	pid_t apid, gpid = fork();

	if(gpid == 0){

		// set up an on exit call back
		ck_assert_int_eq(0,on_exit(on_exit_test, NULL));

		// set max runing jobs to 0 in order to reach on_rescue_exit
		// callback when the job will be killed
		afb_jobs_set_max_count(0);

		// activate signal monitoring
		ck_assert_int_eq(afb_sig_monitor_init(TRUE), 0);

		// run a job
		afb_sig_monitor_run(1, test_job, i2p(2));
	}
	else {
		fprintf(stderr, "job with gpid %d sleeping for 10000µs\n", (int)gpid);
		nsleep(10000);
		fprintf(stderr, "afb_jobs_get_pending_count = %d\n", afb_jobs_get_pending_count());
		kill(gpid, SIGTERM);
		apid = wait(&status);
		fprintf(stderr, "wait returned pid %d and status = %d\n", (int)apid, status);
		ck_assert_int_ne(0, status);
	}
	fprintf(stderr, "job with gpid = %d done gval = %d\n", (int)gpid, gval);

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
	mksuite("afb-sig-monitor");
		addtcase("afb-sig-monitor");
			addtest(run_test);
			addtest(timeout_test);
			addtest(clean_timeout_test);
			addtest(dumpstack_test);
			addtest(sigterm_test);
	return !!srun();
}
