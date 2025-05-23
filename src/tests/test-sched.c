/*
 Copyright (C) 2015-2025 IoT.bzh Company

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
#include <check.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#include <rp-utils/rp-verbose.h>

#include "core/afb-ev-mgr.h"
#include "core/afb-sched.h"
#include "core/afb-jobs.h"
#include "core/afb-sig-monitor.h"
#include "core/afb-threads.h"

/*********************************************************************/

void nsleep(long usec) /* like nsleep */
{
	struct timespec ts = { .tv_sec = (usec / 1000000), .tv_nsec = (usec % 1000000) * 1000 };
	nanosleep(&ts, NULL);
}

/*********************************************************************/

#define i2p(x)  ((void*)((intptr_t)(x)))
#define p2i(x)  ((int)((intptr_t)(x)))

#define TRUE 1
#define FALSE 0
#define NBJOBS 5
#define TIMEOUT 1

typedef struct {
    int timeout;
    pthread_mutex_t mutex;
    int timeout_compleet;
} ttArgs;

struct {
    int val;
    int lastJob;
    uint runingJobs;
    uint killedJobs;
    pthread_mutex_t mutex;
} gval;

int sched_runing;
int reachError;

void test_job(int sig, void* arg){


    if(sig == 0){
        pthread_mutex_lock(&gval.mutex);
        fprintf(stderr, "test_job received sig %d with arg %d\n", sig, p2i(arg));
        gval.runingJobs++;
        gval.val++;

        // if this job is not the last one wait the last job
        if(gval.runingJobs < NBJOBS){
            while(!gval.lastJob){
                pthread_mutex_unlock(&gval.mutex);
                nsleep(10000);
                pthread_mutex_lock(&gval.mutex);
            }
        }
        // if this job is the last one relese the other jobs
        else {
            fprintf(stderr, "***** Release waiting jobs ! *****\n");
            gval.lastJob = TRUE;
        }
    }
    // if the job receve a stoping signal, inform the test rotine throw gval
    else if(sig == SIGALRM ||
            sig == SIGTERM ||
            sig == SIGKILL)
    {
        // unlock mutex in case the job have been killed while mutex was locked
        pthread_mutex_unlock(&gval.mutex);
        pthread_mutex_lock(&gval.mutex);
        fprintf(stderr, "test_job killed sig %d with arg %d\n", sig, p2i(arg));
        gval.killedJobs++;
    }

    fprintf(stderr, "test_job with arg %d terminates !\n", p2i(arg));
    gval.runingJobs--;
    pthread_mutex_unlock(&gval.mutex);
}

void exit_handler(void *data){
    fprintf(stderr, "Exit scheduler\n");
    sched_runing = FALSE;
}

void test_start_job(int sig, void* arg){

    fprintf(stderr, "threads active %d asleep %d\n", afb_threads_active_count(), afb_threads_asleep_count());
    if(sig == 0){
        pthread_mutex_lock(&gval.mutex);
        fprintf(stderr, "start_test_job received sig %d with arg %d\n", sig, p2i(arg));

        // wait for jobs to end
        while(gval.runingJobs > 0 || !gval.lastJob) {
	    fprintf(stderr, "threads active %d asleep %d\n", afb_threads_active_count(), afb_threads_asleep_count());
            pthread_mutex_unlock(&gval.mutex);
            nsleep(10000);
            pthread_mutex_lock(&gval.mutex);
        }
        gval.val *= -1;
        gval.lastJob = FALSE;
    }
    else if(sig == SIGALRM ||
            sig == SIGTERM ||
            sig == SIGKILL)
    {
        // unlock mutex in case the job have been killed while mutex was locked
        pthread_mutex_unlock(&gval.mutex);
        pthread_mutex_lock(&gval.mutex);
        fprintf(stderr, "start_test_job killed sig %d with arg %d\n", sig, p2i(arg));
        gval.killedJobs++;
    }

    fprintf(stderr, "querying exit\n");
    pthread_mutex_unlock(&gval.mutex);

    fprintf(stderr, "threads active %d asleep %d\n", afb_threads_active_count(), afb_threads_asleep_count());
    afb_sched_exit(0, exit_handler, NULL, 0);
    fprintf(stderr, "leaving test_start_job\n");
    fprintf(stderr, "threads active %d asleep %d\n", afb_threads_active_count(), afb_threads_asleep_count());
}

/*********************************************************************/

START_TEST(test_async){

    int i;

    gval.val = 0;
    gval.lastJob = FALSE;
    gval.runingJobs = 0;

    fprintf(stderr, "\n***********************test_async***********************\n");

    // initialisation of the scheduler
    ck_assert_int_eq(afb_sig_monitor_init(TRUE), 0);

    afb_jobs_set_max_count(NBJOBS);
    ck_assert_int_eq(afb_jobs_get_max_count(), NBJOBS);

    // queue N jobs
    for(i=0; i<NBJOBS; i++) afb_sched_post_job(NULL, 0, 1, test_job, i2p(i+1), Afb_Sched_Mode_Normal);

    // run them asynchronously
    sched_runing = TRUE;
    fprintf(stderr, "threads active %d asleep %d\n", afb_threads_active_count(), afb_threads_asleep_count());
    ck_assert_int_eq(afb_sched_start(NBJOBS, NBJOBS, NBJOBS+1, test_start_job, i2p(NBJOBS)), 0);

    // check everything went alright
    ck_assert_int_eq(sched_runing,FALSE);
    ck_assert_int_eq(gval.val, -NBJOBS);
    ck_assert_int_eq(gval.runingJobs, 0);
    ck_assert_int_eq(gval.killedJobs, 0);

}
END_TEST

void test_job_enter(int sig, void * arg, struct afb_sched_lock * sched_lock){
    int r;
    fprintf(stderr, "entering test_job_enter\n");
    r = afb_sched_leave(sched_lock);
    fprintf(stderr, "leaving test_job_enter %d\n", r);
    if(r)reachError++;
}

void test_job_enter_timeout(int sig, void * arg, struct afb_sched_lock * sched_lock){
    int r = 0;
    fprintf(stderr, "entering test_job_enter_timeout sig=%d\n",sig);
    if (sig == 0) {
        sleep(2);
        fprintf(stderr, "unbroken test_job_enter_timeout!!\n");
        r = afb_sched_leave(sched_lock);
        fprintf(stderr, "unbroken test_job_enter_timeout afb_sched_leave=%d!!\n",r);
        if(r)reachError++;
    }
    fprintf(stderr, "leaving test_job_enter_timeout %d\n", r);
}

void test_start_sched_enter(int sig, void * arg){

    int r;

    if (sig == 0){
        fprintf(stderr, "test_start_sched_enter before\n");
        r = afb_sched_sync(1, test_job_enter, arg);
	    fprintf(stderr, "test_start_sched_enter after %d\n", r);
        if(r) reachError++;
        fprintf(stderr, "test_job_enter_timeout before\n");
        r = afb_sched_sync(1, test_job_enter_timeout, arg);
        fprintf(stderr, "test_job_enter_timeout after %d\n", r);
        if(r>=0) reachError++;
    }
    fprintf(stderr, "test_start_sched_enter exiting\n");
    afb_sched_exit(0, NULL, NULL, 0);

    fprintf(stderr, "leaving test_start_sched_enter\n");
    fflush(stderr);
}

START_TEST(test_sched_enter){

    gval.val = 0;
    gval.lastJob = FALSE;
    gval.runingJobs = 0;
    gval.killedJobs = 0;
    reachError = FALSE;

    fprintf(stderr, "\n************************test_sched_enter************************\n");

    // initialisation of the scheduler
    ck_assert_int_eq(afb_sig_monitor_init(TRUE), 0);

    afb_jobs_set_max_count(NBJOBS);
    ck_assert_int_eq(afb_jobs_get_max_count(), NBJOBS);

    // run one sync job
    ck_assert_int_eq(afb_sched_start(3, 3, 3, test_start_sched_enter, i2p(NBJOBS)),0);

    // check everything went alright
    ck_assert_int_eq(reachError, 0);
    ck_assert_int_eq(sched_runing,FALSE);
    ck_assert_int_eq(gval.runingJobs, 0);
    ck_assert_int_eq(gval.killedJobs, 0);

}
END_TEST

void test_start_sched_adapt(int sig, void * arg){
    int r,i;

    fprintf(stderr, "test_start_sched_adapt received sig %d with arg %d\n", sig, p2i(arg));
    fprintf(stderr, "threads active %d asleep %d\n", afb_threads_active_count(), afb_threads_asleep_count());

    if(sig == 0){

        // queue N jobs
        for(i=0; i<NBJOBS; i++){
            r = afb_sched_post_job(NULL, 0, 0, test_job, i2p(i+1), Afb_Sched_Mode_Start);
            ck_assert_int_gt(r, 0);
            fprintf(stderr, "job %d queued with id %d: pending jobs = %d\n", i+1, r, afb_jobs_get_pending_count());
            fprintf(stderr, "threads active %d asleep %d\n", afb_threads_active_count(), afb_threads_asleep_count());
        }

        r=0;
        while(afb_jobs_get_pending_count() != 0){
            fprintf(stderr, "[%d] pending jobs = %d\n", r, afb_jobs_get_pending_count());
            fprintf(stderr, "threads active %d asleep %d\n", afb_threads_active_count(), afb_threads_asleep_count());
            fflush(stderr);
            nsleep(250000);
            r++;
        }

        fprintf(stderr, "[%d] pending jobs = %d\n", r, afb_jobs_get_pending_count());
        fflush(stderr);

        // wait for jobs to end
        r = TRUE;
        fprintf(stderr, "WAITING for jobs to end ! (pending jobs = %d)\n", afb_jobs_get_pending_count());
        while(r){
            pthread_mutex_lock(&gval.mutex);
            if(gval.runingJobs <= 0 && gval.lastJob) r = FALSE;
            fprintf(stderr, "\npending jobs = %d\nrunning job %d\nlast job = %d\n", afb_jobs_get_pending_count(), gval.runingJobs, gval.lastJob);
            fflush(stderr);
            pthread_mutex_unlock(&gval.mutex);
            nsleep(250000);
        }

        fprintf(stderr, "All jobs ended\n");
        pthread_mutex_lock(&gval.mutex);
        gval.val *= -1;
        gval.lastJob = FALSE;
        pthread_mutex_unlock(&gval.mutex);
    }

    fprintf(stderr, "before exiting sched\n");
    afb_sched_exit(0, exit_handler, NULL, 0);
    fprintf(stderr, "leaving test_start_sched_adapt\n");
}

START_TEST(test_sched_adapt){

    gval.val = 0;
    gval.lastJob = FALSE;
    gval.runingJobs = 0;
    gval.killedJobs = 0;
    int r;

    fprintf(stderr, "\n***********************test_sched_adapt***********************\n");
    // initialisation of the scheduler
    ck_assert_int_eq(afb_sig_monitor_init(TRUE), 0);
    afb_jobs_set_max_count(NBJOBS+1);
    ck_assert_int_eq(afb_jobs_get_max_count(), NBJOBS+1);

    // run them asynchronously with N-1 threads allowed
    sched_runing = TRUE;
    fprintf(stderr, "threads active %d asleep %d\n", afb_threads_active_count(), afb_threads_asleep_count());
    r = afb_sched_start(NBJOBS+1, NBJOBS, NBJOBS+1, test_start_sched_adapt, i2p(NBJOBS));
    ck_assert_int_eq(r, 0);

    // check everything went alright
    ck_assert_int_eq(sched_runing,FALSE);
    ck_assert_int_eq(gval.val, -NBJOBS);
    ck_assert_int_eq(gval.runingJobs, 0);
    ck_assert_int_eq(gval.killedJobs, 0);

}
END_TEST

/*********************************************************************/

int evmgr_gotten;
int evmgr_expected;
pthread_mutex_t evmgr_mutex;

void getevmgr(int num)
{
    static char spaces[] = "                                                          ";
    struct ev_mgr * ev1, *ev2;
    int off = (int)(sizeof spaces - 1) - (num << 1);
    char *prefix = &spaces[off < 0 ? 0 : off];

    fprintf(stderr, "%sBEFORE %d\n", prefix, num);
    ev1 = afb_ev_mgr_get_for_me();
    ck_assert(ev1 != NULL);
    fprintf(stderr, "%sMIDDLE %d\n", prefix, num);
    ev2 = afb_ev_mgr_get_for_me();
    ck_assert(ev2 == ev1);
    fprintf(stderr, "%sAFTER %d\n", prefix, num);
    pthread_mutex_lock(&evmgr_mutex);
    evmgr_gotten++;
    pthread_mutex_unlock(&evmgr_mutex);
}

void jobgetevmgr(int signum, void *arg)
{
    int num = p2i(arg);
    getevmgr(num);
}

void do_test_evmgr(int signum, void *arg)
{
    int i, s;

    fprintf(stderr, "-- MAIN ENTRY --\n");
    pthread_mutex_init(&evmgr_mutex, NULL);
    getevmgr(0);
    evmgr_gotten = 0;
    evmgr_expected = 20;
    for (i = 0 ; i < evmgr_expected ; i++) {
        fprintf(stderr, "-- MAIN launch of %d...\n", 1+i);
        s = afb_sched_post_job(NULL, 0, 0, jobgetevmgr, i2p(i+1), Afb_Sched_Mode_Normal);
        fprintf(stderr, "-- MAIN launch of %d -> %d\n", 1+i, s);
        ck_assert_int_ge(s, 0);
    }
    afb_sched_exit(0, 0, NULL, 0);
    fprintf(stderr, "-- MAIN EXIT --\n");
}

START_TEST(test_evmgr)
{
    fprintf(stderr, "\n***********************test_evmgr***********************\n");

    // initialisation of the scheduler
    ck_assert_int_eq(afb_sig_monitor_init(TRUE), 0);
    afb_jobs_set_max_count(NBJOBS+1);
    ck_assert_int_eq(afb_jobs_get_max_count(), NBJOBS+1);

    // run them asynchronously with N-1 threads allowed
    sched_runing = TRUE;
    ck_assert_int_eq(afb_sched_start(5, 0, 40, do_test_evmgr, 0), 0);

    // check everything went alright
    ck_assert_int_eq(evmgr_gotten, evmgr_expected);
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
	mksuite("sched");
		addtcase("sched");
			addtest(test_async);
			addtest(test_sched_enter);
			addtest(test_sched_adapt);
			addtest(test_evmgr);
	return !!srun();
}
