#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>


#include <check.h>
#include <pthread.h>
#include <signal.h>

#include "libafb-config.h"

#include "core/afb-sched.h"
#include "core/afb-jobs.h"
#include "core/afb-sig-monitor.h"
#include "sys/verbose.h"

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

    int i, r;

    fprintf(stderr, "test_job received sig %d with arg %d\n", sig, p2i(arg));

    if(sig == 0){
        pthread_mutex_lock(&gval.mutex);
        gval.runingJobs++;
        gval.val ++;

        // if this job is not the last one whaite the las job
        if(p2i(arg) < NBJOBS){
            r = TRUE;
            while(r){
                pthread_mutex_unlock(&gval.mutex);
                for(i=0; i<__INT_MAX__; i++);
                pthread_mutex_lock(&gval.mutex);
                if(gval.lastJob)r = FALSE;
            }
        }
        // if this job is the last one relese the other jobs
        else{
            gval.lastJob = TRUE;
        }
    }
    // if the job receve a stoping signal, inform the test rotine throw gval
    else if(sig == SIGVTALRM ||
            sig == SIGTERM ||
            sig == SIGKILL)
    {
        // unlock mutex in case the job have been killed while mutex was locked
        pthread_mutex_unlock(&gval.mutex);
        pthread_mutex_lock(&gval.mutex);
        gval.killedJobs++;
    }

    gval.runingJobs--;
    pthread_mutex_unlock(&gval.mutex);

    fprintf(stderr, "test_job with arg %d trminate !\n", p2i(arg));

}

void exit_handler(){
    fprintf(stderr, "Exit scheduler\n");
    sched_runing = FALSE;
}

void test_start_job(int sig, void* arg){

    int i, r;

    fprintf(stderr, "start_test_job received sig %d with arg %d\n", sig, p2i(arg));

    if(sig == 0){

        r = TRUE;
        // wait for jobs to end
        while(r){
            for(i=0; i<__INT_MAX__; i++);
            pthread_mutex_lock(&gval.mutex);
            if(gval.runingJobs <= 0 && gval.lastJob) r = FALSE;
            pthread_mutex_unlock(&gval.mutex);
        }

        pthread_mutex_lock(&gval.mutex);
        gval.val *= -1;
        gval.lastJob = FALSE;
        pthread_mutex_unlock(&gval.mutex);
    }

    afb_sched_exit(exit_handler);
    fprintf(stderr, "living test_start_job\n");
}

void test_start_job_sync(int sig, void* arg){

    int i, r;

    fprintf(stderr, "start_test_job_sync received sig %d with arg %d\n", sig, p2i(arg));

    if(sig == 0){
        afb_sched_call_job_sync(NULL, 1, test_job, arg);

        // wait for jobs to end
        r = TRUE;
        while(r){
            for(i=0; i<__INT_MAX__; i++);
            pthread_mutex_lock(&gval.mutex);
            if(gval.runingJobs <= 0 && (gval.killedJobs || gval.lastJob)) r = FALSE;
            pthread_mutex_unlock(&gval.mutex);
        }

        pthread_mutex_lock(&gval.mutex);
        gval.val *= -1;
        gval.lastJob = FALSE;
        pthread_mutex_unlock(&gval.mutex);
    }

    afb_sched_exit(NULL);

    fprintf(stderr, "living test_start_job_sync\n");
    fflush(stderr);
}

/*********************************************************************/

START_TEST(test_async){

    int i;

    gval.val = 0;
    gval.lastJob = FALSE;
    gval.runingJobs = 0;
    struct evmgr * ev;

    fprintf(stderr, "\n***********************test_async***********************\n");

    // initialisation of the scheduler
    ck_assert_int_eq(afb_sig_monitor_init(TRUE), 0);

    ev = afb_sched_acquire_event_manager();
    ck_assert(ev != NULL);

    afb_jobs_set_max_count(NBJOBS);
    ck_assert_int_eq(afb_jobs_get_max_count(), NBJOBS);

    // queue N jobs
    for(i=0; i<NBJOBS; i++) afb_jobs_queue(NULL, 1, test_job, i2p(i+1));

    // run them asynchronously
    sched_runing = TRUE;
    ck_assert_int_eq(afb_sched_start(NBJOBS, NBJOBS, NBJOBS+1, test_start_job, i2p(NBJOBS)), 0);

    // check everything went alright
    ck_assert_int_eq(sched_runing,FALSE);
    ck_assert_int_eq(gval.val, -NBJOBS);
    ck_assert_int_eq(gval.runingJobs, 0);
    ck_assert_int_eq(gval.killedJobs, 0);

}
END_TEST

START_TEST(test_sync){

    gval.val = 0;
    gval.lastJob = FALSE;
    gval.runingJobs = 0;
    struct evmgr * ev;

    fprintf(stderr, "\n************************test_sync************************\n");

    // initialisation of the scheduler
    ck_assert_int_eq(afb_sig_monitor_init(TRUE), 0);

    ev = afb_sched_acquire_event_manager();
    ck_assert(ev != NULL);

    afb_jobs_set_max_count(NBJOBS);
    ck_assert_int_eq(afb_jobs_get_max_count(), NBJOBS);

    // run one sync job
    ck_assert_int_eq(afb_sched_start(1, 1, 2, test_start_job_sync, i2p(NBJOBS)), 0);

    ck_assert_int_eq(sched_runing,FALSE);
    ck_assert_int_eq(gval.runingJobs, 0);
    ck_assert_int_eq(gval.killedJobs, 0);

    // run a sync job that will reach timeout
    ck_assert_int_eq(afb_sched_start(3, 3, 4, test_start_job_sync, i2p(1)),0);

    // check everything went alright
    ck_assert_int_eq(sched_runing,FALSE);
    ck_assert_int_eq(gval.runingJobs, 0);
    ck_assert_int_eq(gval.killedJobs, 1);

}
END_TEST

void test_job_enter(int sig, void * arg, struct afb_sched_lock * sched_lock){
    int r;
    r = afb_sched_leave(sched_lock);
    if(r)reachError++;
}

void test_start_sched_enter(int sig, void * arg){

    int r;

    if (sig == 0){
        r = afb_sched_enter(NULL, 1, test_job_enter, arg);
        if(r)reachError++;
    }
    afb_sched_exit(NULL);

    fprintf(stderr, "living test_start_sched_enter\n");
    fflush(stderr);
}

START_TEST(test_sched_enter){

    gval.val = 0;
    gval.lastJob = FALSE;
    gval.runingJobs = 0;
    gval.killedJobs = 0;
    reachError = FALSE;
    struct evmgr * ev;

    fprintf(stderr, "\n************************test_sched_enter************************\n");

    // initialisation of the scheduler
    ck_assert_int_eq(afb_sig_monitor_init(TRUE), 0);

    ev = afb_sched_acquire_event_manager();
    ck_assert(ev != NULL);

    afb_jobs_set_max_count(NBJOBS);
    ck_assert_int_eq(afb_jobs_get_max_count(), NBJOBS);

    // run one sync job
    ck_assert_int_eq(afb_sched_start(3, 3, 3, test_start_sched_enter, i2p(NBJOBS)),0);

    // check everything went alright
    ck_assert_int_eq(reachError, FALSE);
    ck_assert_int_eq(sched_runing,FALSE);
    ck_assert_int_eq(gval.runingJobs, 0);
    ck_assert_int_eq(gval.killedJobs, 0);

}
END_TEST

void test_start_sched_adapt(int sig, void * arg){
    int r,i;

    fprintf(stderr, "test_start_sched_adapt received sig %d with arg %d\n", sig, p2i(arg));

    if(sig == 0){

        // queue N jobs
        for(i=0; i<NBJOBS-1; i++){
            afb_jobs_queue(NULL, 0, test_job, i2p(i+1));
            fprintf(stderr, "job %d queued : pending jobs = %d\n", i+1, afb_jobs_get_pending_count());

        }

        r=0;
        while(afb_jobs_get_pending_count() != 0){
            usleep(100);
            r++;
        }

        fprintf(stderr, "r = %d\npending jobs = %d\nqueuing job %d\n", r, afb_jobs_get_pending_count(), NBJOBS);
        fflush(stderr);
        afb_jobs_queue_lazy(NULL, 1, test_job, i2p(NBJOBS));
        afb_sched_adapt(0);
        fprintf(stderr, "pending jobs = %d\n", afb_jobs_get_pending_count());


        // wait for jobs to end
        r = TRUE;
        while(r){
            pthread_mutex_lock(&gval.mutex);
            if(gval.runingJobs <= 0 && gval.lastJob) r = FALSE;
            pthread_mutex_unlock(&gval.mutex);
            usleep(1000);
            //for(i=0; i<__INT_MAX__; i++);
        }

        pthread_mutex_lock(&gval.mutex);
        gval.val *= -1;
        gval.lastJob = FALSE;
        pthread_mutex_unlock(&gval.mutex);
    }

    afb_sched_exit(exit_handler);
    fprintf(stderr, "living test_start_sched_adapt\n");
}

START_TEST(test_sched_adapt){

    gval.val = 0;
    gval.lastJob = FALSE;
    gval.runingJobs = 0;
    gval.killedJobs = 0;
    struct evmgr * ev;

    fprintf(stderr, "\n***********************test_sched_adapt***********************\n");

    // initialisation of the scheduler
    ck_assert_int_eq(afb_sig_monitor_init(TRUE), 0);
    ev = afb_sched_acquire_event_manager();
    ck_assert(ev != NULL);
    afb_jobs_set_max_count(NBJOBS+1);
    ck_assert_int_eq(afb_jobs_get_max_count(), NBJOBS+1);

    // run them asynchronously with N-1 threads allowed
    sched_runing = TRUE;
    ck_assert_int_eq(afb_sched_start(NBJOBS, NBJOBS, NBJOBS+1, test_start_sched_adapt, i2p(NBJOBS)), 0);

    // check everything went alright
    ck_assert_int_eq(sched_runing,FALSE);
    ck_assert_int_eq(gval.val, -NBJOBS);
    ck_assert_int_eq(gval.runingJobs, 0);
    ck_assert_int_eq(gval.killedJobs, 0);

}
END_TEST

/*********************************************************************/

static Suite *suite;
static TCase *tcase;

void mksuite(const char *name) { suite = suite_create(name); }
void addtcase(const char *name) { tcase = tcase_create(name); suite_add_tcase(suite, tcase); }
void addtest(TFun fun) { tcase_add_test(tcase, fun); }
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
            addtest(test_sync);
            addtest(test_sched_enter);
            addtest(test_sched_adapt);
	return !!srun();
}