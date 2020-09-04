#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <check.h>

#include <pthread.h>
#include <sys/wait.h>

#include <cynagora.h>

#include "libafb-config.h"

#include "afb/afb-auth.h"
#include "core/afb-perm.h"
#include "core/afb-req-common.h"
#include "core/afb-cred.h"
#include "core/afb-sched.h"
#include "sys/evmgr.h"


#define BUF_SIZE 1024
#define TOKEN_NAME "Test Token"
#define SESSION_NAME "session"
#define TEST_LOA 1


int val, done;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;


pid_t gpid;
char cwd[BUF_SIZE];
char gpath[BUF_SIZE];
char env[BUF_SIZE];
int pathReady = 0;

void testCB(void * arg, int status){
	fprintf(stderr, "testCB was called with status %d\n", status);
	pthread_mutex_lock(&mutex);
	val = status;
	done = 1;
	pthread_mutex_unlock(&mutex);
}

int getpath(char buffer[BUF_SIZE], const char *base)
{
	static const char *paths[] = {"", "tests/", "src/", "build/", NULL };

	int rc;
	int len;
	int lenp;
	const char **pp = paths;

	len = snprintf(buffer, BUF_SIZE, "%s", base);
	ck_assert_int_ge(len, 0);
	rc = access(buffer, F_OK);
	while (rc < 0 && *pp) {
		lenp = (int)strlen(*pp);
		if (lenp + len + 1 > BUF_SIZE) break;
		memmove(buffer + lenp, buffer, len + 1);
		memcpy(buffer, *pp, lenp);
		pp++;
		len += lenp;
		rc = access(buffer, F_OK);
		fprintf(stderr, "Looking for %s in path %s\n", base, buffer);
	}
	if (rc == 0)
		fprintf(stderr, "FOUND %s for %s\n", buffer, base);
	else
		fprintf(stderr, "Cna't find file %s\n", base);
	return rc;
}

void preparDemonCynagora(){
	sprintf(gpath, "%s/%d", getcwd(cwd, sizeof(cwd)), (int)getpid());
	sprintf(env, "CYNAGORA_SOCKET_CHECK=unix:%s/cynagora.check", gpath);
	putenv(env);
	pathReady = 1;
}

void startDemonCynagora(){
	
	if(!pathReady) preparDemonCynagora();

	gpid = fork();
	ck_assert_int_ge(gpid,0);

	if(gpid == 0){
		int i;
		char path[1024];
		char cynagoraInitF[1024];
		getpath(path, "cynagoraTest.initial");
		sprintf(cynagoraInitF, "%s/%s", cwd, path);
		char * argv[] = {
			"cynagorad",
			"--dbdir",
			gpath,
			"--make-db-dir",
			"--socketdir",
			gpath,
			"--make-socket-dir",
			"--init",
			cynagoraInitF,
			"--log",
			NULL
		};
		for(i=0; argv[i] != NULL; i++) fprintf(stderr, "%s ", argv[i]);
		fprintf(stderr, "\n");
		ck_assert(!execvp("cynagorad", argv));
		_exit(1);
	}
	else if(gpid < 0){
		fprintf(stderr, "ERROR : Cynagora Deamon fork failed ! returned %d\n", gpid);
	}
	fprintf(stderr, "cynagora deamon starting on Id : %d\n", gpid);
	usleep(500000);
}

void stopDemonCynagora(){
	int a;
	char cmd[1024];

	kill(gpid, 9);
	waitpid(gpid, &a, 0);

	sprintf(cmd, "rm -rf %s", gpath);
	ck_assert_int_eq(system(cmd), 0);
}

/*********************************************************************/

struct afb_req_common *test_reply_req;
int test_reply_status;
unsigned test_reply_nreplies;
struct afb_data * const *test_reply_replies;
void test_reply(struct afb_req_common *req, int status, unsigned nreplies, struct afb_data * const *replies)
{
	test_reply_req = req;
	test_reply_status = status;
	test_reply_nreplies = nreplies;
	test_reply_replies = replies;
}

struct afb_req_common *test_unref_req;
void test_unref(struct afb_req_common *req)
{
	test_unref_req = req;
	afb_req_common_cleanup(req);
}

struct afb_req_common *test_subscribe_req;
struct afb_evt *test_subscribe_event;
int test_subscribe(struct afb_req_common *req, struct afb_evt *event)
{
	test_subscribe_req = req;
	test_subscribe_event = event;
	return 0;
}

struct afb_req_common *test_unsubscribe_req;
struct afb_evt *test_unsubscribe_event;
int test_unsubscribe(struct afb_req_common *req, struct afb_evt *event)
{
	test_unsubscribe_req = req;
	test_unsubscribe_event = event;
	return 0;
}

struct afb_req_common_query_itf test_queryitf =
{
	.reply = test_reply,
	.unref = test_unref,
	.subscribe = test_subscribe,
	.unsubscribe = test_unsubscribe
};

void waiteForCB(){
	struct evmgr *evmgr = afb_sched_acquire_event_manager();
	do {
		evmgr_run(evmgr, 100);
	}
	while(done == 0);
}

/*********************************************************************/
/* Test */
START_TEST (test)
{

	signal(SIGPIPE, SIG_IGN);

	struct afb_req_common req;

	afb_req_common_init(&req, &test_queryitf, "api", "verb", 0, NULL);

	// test with no credantial set
	fprintf(stderr, "\n****** test with no credantial set ******\n");
	done = 0;
	afb_perm_check_req_async(&req, "perm", testCB, NULL);
	waiteForCB();
	ck_assert_int_eq(val, 1);

#if BACKEND_PERMISSION_IS_CYNAGORA

	uid_t uid = 1;
	gid_t gid = 1;
	pid_t pid = 1;

	preparDemonCynagora();

	ck_assert_int_eq(afb_cred_create(&req.credentials, uid, gid, pid, gpath), 0);

	// test with cynagora server OFF
	fprintf(stderr, "\n****** test with cynagora server OFF ******\n");

	done = 0;
	afb_perm_check_req_async(&req, "perm", testCB, NULL);
	waiteForCB();
	ck_assert_int_eq(val, -2);


	// test with cynagora server ON
	fprintf(stderr, "\n#### starting cynagora server ####\n");
	startDemonCynagora();

	// check for allowed perm
	fprintf(stderr, "\n****** test with cynagora server ON and allowed perm. ******\n");
	done = 0;
	afb_perm_check_req_async(&req, "perm", testCB, NULL);
	waiteForCB();
	ck_assert_int_eq(val, 1);

	// check for not allowed perm
	fprintf(stderr, "\n****** test with cynagora server ON and not allowed perm. ******\n");
	afb_perm_check_req_async(&req, "toto", testCB, NULL);
	done = 0;
	waiteForCB();
	ck_assert_int_eq(val, 0);

	fprintf(stderr, "\n#### stoping cynagora server ####\n");
	stopDemonCynagora();

#endif

}
END_TEST

void test_rec_common_perm(struct afb_req_common * req, struct afb_auth * auth){
	
	uid_t uid = 1;
	gid_t gid = 1;
	pid_t pid = 1;

	afb_req_common_init(req, &test_queryitf, "api", "verb", 0, NULL);
	afb_req_common_set_session_string(req, SESSION_NAME);
	afb_req_common_set_token_string(req, TOKEN_NAME);

	ck_assert_int_eq(afb_cred_create(&req->credentials, uid, gid, pid, gpath), 0);

	val = done = 0;
	afb_req_common_check_and_set_session_async(req, auth, 0, testCB, NULL);
	waiteForCB();
	ck_assert_int_eq(done, 1);

}

// /*********************************************************************/
// /* Test with session set */
START_TEST (testRecCommonPerm)
{
	
	signal(SIGPIPE, SIG_IGN);

	fprintf(stderr, "\n------------- test_rec_common_perm -------------\n");

	struct afb_req_common req1;
	struct afb_auth auth, first, next;
	int i;

	auth.next = &next;

    first.type = afb_auth_Yes;
    next.type = afb_auth_No;


	fprintf(stderr, "\n#### starting cynagora server ####\n");
	startDemonCynagora();

	/* check for no perm */
	fprintf(stderr, "\n****** afb_auth_No ******\n");
	auth.type = afb_auth_No;
	test_rec_common_perm(&req1, &auth);
	ck_assert_int_eq(val, 0);

	
	/* check for LOA */
	fprintf(stderr, "\n****** afb_auth_LOA ******\n");
	auth.type = afb_auth_LOA;
	ck_assert_int_ge(afb_req_common_session_set_LOA_hookable(&req1, 1),0);
	for(i=0; i<=3; i++){
		fprintf(stderr, "LOA %d for %d :\n", i, TEST_LOA);
		auth.loa = i;
		test_rec_common_perm(&req1, &auth);
		if(i<2)ck_assert_int_eq(val, 1);
		else ck_assert_int_eq(val, 0);
	}

	
	/* check for text Permission */
	fprintf(stderr, "\n****** afb_auth_Permission ******\n");
	auth.type = afb_auth_Permission;
	// good
	fprintf(stderr, "good perm :\n");
	auth.text = "perm";
	test_rec_common_perm(&req1, &auth);
	ck_assert_int_eq(val, 1);
	// bad
	fprintf(stderr, "bad perm :\n");
	auth.text = "noPerm";
	test_rec_common_perm(&req1, &auth);
	ck_assert_int_eq(val, 0);


	/* check for "or" conditional Permission */
	fprintf(stderr, "\n****** afb_auth_Or ******\n");
	auth.type = afb_auth_Or;
	next.type = afb_auth_No;
	auth.first = &first;
	for(first.type=afb_auth_No; first.type<=afb_auth_Yes; first.type+=afb_auth_Yes){
		fprintf(stderr, "first %d | next %d :\n", !!first.type, !!next.type);
		test_rec_common_perm(&req1, &auth);
		ck_assert_int_eq(val, first.type || next.type);
	}

	/* check for "and" conditional Permission */
	fprintf(stderr, "\n****** afb_auth_And ******\n");
	auth.type = afb_auth_And;
	next.type = afb_auth_Yes;
	for(first.type=afb_auth_No; first.type<=afb_auth_Yes; first.type+=afb_auth_Yes){
		fprintf(stderr, "first %d | next %d :\n", !!first.type, !!next.type);
		test_rec_common_perm(&req1, &auth);
		ck_assert_int_eq(val, first.type && next.type);
	}

	/* check for "Not" conditional Permission */
	fprintf(stderr, "\n****** afb_auth_Not ******\n");
	auth.type = afb_auth_Not;
	for(first.type=afb_auth_No; first.type<=afb_auth_Yes; first.type+=afb_auth_Yes){
		fprintf(stderr, "first %d | next %d :\n", !!first.type, !!next.type);
		test_rec_common_perm(&req1, &auth);
		ck_assert_int_eq(val, !first.type);
	}


	/* check for "Yes" conditional Permission */
	fprintf(stderr, "\n****** afb_auth_Yes ******\n");
	auth.type = afb_auth_Yes;
	for(first.type=afb_auth_No; first.type<=afb_auth_Yes; first.type+=afb_auth_Yes){
		for(next.type=afb_auth_No; next.type<=afb_auth_Yes; next.type+=afb_auth_Yes){
			fprintf(stderr, "first %d | next %d :\n", !!first.type, !!next.type);
			test_rec_common_perm(&req1, &auth);
			ck_assert_int_eq(val, 1);
		}
	}

	fprintf(stderr, "\n#### stoping cynagora server ####\n");
	stopDemonCynagora();
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
	mksuite("afb-perm");
		addtcase("afb-perm");
			addtest(test);
			#if BACKEND_PERMISSION_IS_CYNAGORA
			addtest(testRecCommonPerm);
			tcase_set_timeout(tcase, 10);
			#endif
	return !!srun();
}
