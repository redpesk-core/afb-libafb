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

#if !BACKEND_PERMISSION_IS_CYNAGORA
# error "CYNAGORA BACKEND IS EXPECTED FOR THAT TEST"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <check.h>

#include <pthread.h>
#include <sys/wait.h>

#include <cynagora.h>

#include "afb/afb-auth.h"
#include "core/afb-perm.h"
#include "core/afb-req-common.h"
#include "core/afb-cred.h"
#include "core/afb-sched.h"
#include "sys/ev-mgr.h"


#define BUF_SIZE 1024
#define SESSION_NAME "testSession"
#define TEST_LOA 1
#define GOOD_TOKEN "goodToken"
#define BAD_TOKEN "badToken"


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
	char *p;
	int r;

	p = getcwd(cwd, sizeof(cwd));
	ck_assert_ptr_ne(p, 0);
	r = snprintf(gpath, sizeof gpath, "%s/%d", cwd, (int)getpid());
	ck_assert_int_lt(r, (int)(sizeof gpath));
	r = snprintf(env, sizeof env, "CYNAGORA_SOCKET_CHECK=unix:%s/cynagora.check", gpath);
	ck_assert_int_lt(r, (int)(sizeof env));
	putenv(env);
	pathReady = 1;
}

void startDemonCynagora(){
	if(!pathReady) preparDemonCynagora();

	gpid = fork();
	ck_assert_int_ge(gpid,0);

	if(gpid == 0){
		int i, r;
		char path[1024];
		char cynagoraInitF[1024];
		getpath(path, "cynagoraTest.initial");
		r = snprintf(cynagoraInitF, sizeof cynagoraInitF, "%s/%s", cwd, path);
		ck_assert_int_lt(r, (int)(sizeof cynagoraInitF));
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
	int a, r;
	char cmd[1024];

	kill(gpid, 9);
	waitpid(gpid, &a, 0);

	r = snprintf(cmd, sizeof cmd, "rm -rf %s", gpath);
	ck_assert_int_lt(r, (int)(sizeof cmd));

	r = system(cmd);
	ck_assert_int_eq(r, 0);
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
	struct ev_mgr *evmgr = afb_sched_acquire_event_manager();
	do {
		ev_mgr_run(evmgr, 100);
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

/*********************************************************************/
/* Test with session set */

void test_rec_common_perm(struct afb_req_common * req, struct afb_auth * auth, char * token, char * session, int sessionflag){

	uid_t uid = 1;
	gid_t gid = 1;
	pid_t pid = 1;

	afb_req_common_init(req, &test_queryitf, "api", "verb", 0, NULL);
	afb_req_common_set_session_string(req, session);
	if(token) afb_req_common_set_token_string(req, token);

	ck_assert_int_eq(afb_cred_create(&req->credentials, uid, gid, pid, gpath), 0);

	val = done = 0;
	afb_req_common_check_and_set_session_async(req, auth, sessionflag, testCB, NULL);
	waiteForCB();
	ck_assert_int_eq(done, 1);

}

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
	test_rec_common_perm(&req1, &auth, NULL, SESSION_NAME, 0);
	ck_assert_int_eq(val, 0);

	/* check for token */
	fprintf(stderr, "\n****** afb_auth_Token ******\n");
	auth.type = afb_auth_Token;
	//Good
	fprintf(stderr, "good token  :\n");
	test_rec_common_perm(&req1, &auth, GOOD_TOKEN, SESSION_NAME, 0);
	ck_assert_int_eq(val, 1);
	// Bad
	fprintf(stderr, "bad token  :\n");
	test_rec_common_perm(&req1, &auth, BAD_TOKEN, SESSION_NAME, 0);
	ck_assert_int_eq(val, 0);

	/* check for LOA */
	fprintf(stderr, "\n****** afb_auth_LOA ******\n");
	auth.type = afb_auth_LOA;
	ck_assert_int_ge(afb_req_common_session_set_LOA_hookable(&req1, 1),0);
	for(i=0; i<=3; i++){
		fprintf(stderr, "LOA %d for %d :\n", i, TEST_LOA);
		auth.loa = i;
		test_rec_common_perm(&req1, &auth, NULL, SESSION_NAME, 0);
		if(i<2)ck_assert_int_eq(val, 1);
		else ck_assert_int_eq(val, 0);
	}


	/* check for text Permission */
	fprintf(stderr, "\n****** afb_auth_Permission ******\n");
	auth.type = afb_auth_Permission;
	// good
	fprintf(stderr, "good perm :\n");
	auth.text = "perm";
	test_rec_common_perm(&req1, &auth, NULL, SESSION_NAME, 0);
	ck_assert_int_eq(val, 1);
	// bad
	fprintf(stderr, "bad perm :\n");
	auth.text = "noPerm";
	test_rec_common_perm(&req1, &auth, NULL, SESSION_NAME, 0);
	ck_assert_int_eq(val, 0);


	/* check for "or" conditional Permission */
	fprintf(stderr, "\n****** afb_auth_Or ******\n");
	auth.type = afb_auth_Or;
	next.type = afb_auth_No;
	auth.first = &first;
	for(first.type=afb_auth_No; first.type<=afb_auth_Yes; first.type+=afb_auth_Yes){
		fprintf(stderr, "first %d | next %d :\n", !!first.type, !!next.type);
		test_rec_common_perm(&req1, &auth, NULL, SESSION_NAME, 0);
		ck_assert_int_eq(val, first.type || next.type);
	}

	/* check for "and" conditional Permission */
	fprintf(stderr, "\n****** afb_auth_And ******\n");
	auth.type = afb_auth_And;
	next.type = afb_auth_Yes;
	for(first.type=afb_auth_No; first.type<=afb_auth_Yes; first.type+=afb_auth_Yes){
		fprintf(stderr, "first %d | next %d :\n", !!first.type, !!next.type);
		test_rec_common_perm(&req1, &auth, NULL, SESSION_NAME, 0);
		ck_assert_int_eq(val, first.type && next.type);
	}

	/* check for "Not" conditional Permission */
	fprintf(stderr, "\n****** afb_auth_Not ******\n");
	auth.type = afb_auth_Not;
	for(first.type=afb_auth_No; first.type<=afb_auth_Yes; first.type+=afb_auth_Yes){
		fprintf(stderr, "first %d | next %d :\n", !!first.type, !!next.type);
		test_rec_common_perm(&req1, &auth, NULL, SESSION_NAME, 0);
		ck_assert_int_eq(val, !first.type);
	}


	/* check for "Yes" conditional Permission */
	fprintf(stderr, "\n****** afb_auth_Yes ******\n");
	auth.type = afb_auth_Yes;
	for(first.type=afb_auth_No; first.type<=afb_auth_Yes; first.type+=afb_auth_Yes){
		for(next.type=afb_auth_No; next.type<=afb_auth_Yes; next.type+=afb_auth_Yes){
			fprintf(stderr, "first %d | next %d :\n", !!first.type, !!next.type);
			test_rec_common_perm(&req1, &auth, NULL, SESSION_NAME, 0);
			ck_assert_int_eq(val, 1);
		}
	}

	/* check for sessions */
	fprintf(stderr, "\n**** session ****\n");
	// Session and LOA
	// auth.type = afb_auth_LOA;
	// auth.loa = 1;
	ck_assert_int_ge(afb_req_common_session_set_LOA_hookable(&req1, 1),0);
	for(i=1; i<=3; i++){
		fprintf(stderr, "sessionflag %d and LOA1 :\n", i);
		//auth.text = "perm";
		test_rec_common_perm(&req1, NULL, NULL, SESSION_NAME, i);
		ck_assert_int_eq(val, i<2);
	}
	//good session and no auth
	fprintf(stderr, "no auth good session name\n");
	test_rec_common_perm(&req1, NULL, NULL, SESSION_NAME, 4);
	ck_assert_int_eq(val, 1);

	//bad session and no auth
	fprintf(stderr, "no auth bad session name\n");
	test_rec_common_perm(&req1, NULL, NULL, "badSession", 4);
	ck_assert_int_eq(val, 0);

	auth.type = afb_auth_Yes;
	//good session valid auth
	fprintf(stderr, "valid auth good session name\n");
	test_rec_common_perm(&req1, &auth, NULL, SESSION_NAME, 4);
	ck_assert_int_eq(val, 1);

	//bad session and valid auth
	fprintf(stderr, "valid auth bad session name\n");
	test_rec_common_perm(&req1, &auth, NULL, "badSession", 16);
	ck_assert_int_eq(val, 0);

	fprintf(stderr, "\n#### stoping cynagora server ####\n");
	stopDemonCynagora();
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
	mksuite("afb-perm");
		addtcase("afb-perm");
			addtest(test);
#if BACKEND_PERMISSION_IS_CYNAGORA
			addtest(testRecCommonPerm);
			tcase_set_timeout(tcase, 10);
#endif
	return !!srun();
}
