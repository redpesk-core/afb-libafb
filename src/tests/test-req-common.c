/*
 Copyright (C) 2015-2025 IoT.bzh Company

 Author: José Bollo <jose.bollo@iot.bzh>

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
#include <rp-utils/rp-jsonc.h>
#include <rp-utils/rp-verbose.h>

#if !defined(ck_assert_ptr_null)
# define ck_assert_ptr_null(X)      ck_assert_ptr_eq(X, NULL)
# define ck_assert_ptr_nonnull(X)   ck_assert_ptr_ne(X, NULL)
#endif

#define i2p(x)  ((void*)((intptr_t)(x)))
#define p2i(x)  ((int)((intptr_t)(x)))

#define NB_DATA 3

#include <json-c/json.h>
#include <afb/afb-arg.h>
#include <afb/afb-errno.h>

#include "core/afb-apiset.h"
#include "core/afb-req-common.h"
#include "core/afb-session.h"
#include "core/afb-data.h"
#include "core/afb-type.h"
#include "core/afb-jobs.h"
#include "core/afb-cred.h"
#include "core/afb-evt.h"

#include "core/afb-sched.h"
#include "core/afb-sig-monitor.h"


/*********************************************************************/

void nsleep(long usec) /* like nsleep */
{
	struct timespec ts = { .tv_sec = (usec / 1000000), .tv_nsec = (usec % 1000000) * 1000 };
	nanosleep(&ts, NULL);
}

/*********************************************************************/
/* afb_req_common requirement */

struct afb_req_common *test_reply_req;
int test_reply_status;
unsigned test_reply_nreplies;
struct afb_data *const *test_reply_replies;
void test_reply(struct afb_req_common *req, int status, unsigned nreplies, struct afb_data *const replies[])
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
	.unsubscribe = test_unsubscribe,
	.interface = NULL
};

struct afb_req_common comreq;
const char apiname[] = "hello";
const char verbname[] = "hello";

/*********************************************************************/
/* job scheduling */

static void sched_jobs_start(int sig, void * arg)
{
	const char *msg = arg;
	fprintf(stderr, "before exiting from %s\n", msg);
	nsleep(100);
	afb_sched_exit(0, NULL, NULL, 0);
	fprintf(stderr, "after exiting from %s\n", msg);
}

static void sched_jobs(const char *msg)
{
	fprintf(stderr, "before starting from %s\n", msg);
	ck_assert_int_eq(0, afb_sched_start(1, 1, 100, sched_jobs_start, (void*)msg));
	fprintf(stderr, "after starting from %s\n", msg);
}

/*********************************************************************/
/* Test Callbacks */

int gval;
int gApiVal;

void apiClosureCB(void *arg)
{
	gval += p2i(arg);
	fprintf(stderr, "went through Api Closure with val %d\n", p2i(arg));
}

void dataClosureCB(void *arg)
{
	gval += p2i(arg);
	fprintf(stderr, "went through Data Closure with val %d\n", p2i(arg));
}

void api_process(void *arg, struct afb_req_common *req)
{
	gApiVal += p2i(arg);
	fprintf(stderr, "api_process was called with arg = %d\n", p2i(arg));
}

void checkPermClosure(void * closure1, int status, void * closure2, void * closure3)
{
	fprintf(stderr, "checkPermClosure was called with status %d, closure1 %d, closure2 %d, closure3 %d\n", status, p2i(closure1), p2i(closure2), p2i(closure3));
	ck_assert_int_eq(status, 1);
	ck_assert_int_eq(p2i(closure1), 1);
	ck_assert_int_eq(p2i(closure2), 2);
	ck_assert_int_eq(p2i(closure3), 3);

	gval = 1;
}

/*********************************************************************/
/* Test */
START_TEST (test)
{
	struct afb_req_common *req = &comreq;

	afb_req_common_init(req, &test_queryitf, apiname, verbname, 0, NULL, NULL);
	ck_assert_ptr_eq(req->queryitf, &test_queryitf);
	ck_assert_ptr_eq(req->apiname, apiname);
	ck_assert_ptr_eq(req->verbname, verbname);

	test_unref_req = NULL;
	ck_assert_int_eq(req->refcount, 1);
	ck_assert_ptr_eq(req, afb_req_common_addref(req));
	ck_assert_int_eq(req->refcount, 2);
	ck_assert_ptr_null(test_unref_req);
	afb_req_common_unref(req);
	ck_assert_int_eq(req->refcount, 1);
	ck_assert_ptr_null(test_unref_req);
	req->replied = 1; /* ensure no side effect for unref */
	afb_req_common_unref(req);
	ck_assert_ptr_eq(req, test_unref_req);
	ck_assert_int_eq(req->refcount, 0);
	ck_assert_ptr_eq(req, afb_req_common_addref_hookable(req));
	ck_assert_int_eq(req->refcount, 1);
	afb_req_common_unref_hookable(req);
	ck_assert_ptr_eq(req, test_unref_req);
	ck_assert_int_eq(req->refcount, 0);

}
END_TEST

START_TEST (session)
{
	struct afb_req_common *req = &comreq;
	struct afb_session *sess;
	int r;

	afb_req_common_init(req, &test_queryitf, "", "", 0, NULL, NULL);

	// test on session
	r = afb_session_create(&sess, 0);
	ck_assert_int_eq(r, 0);
	ck_assert_ptr_ne(sess, 0);

	afb_req_common_set_session(req, sess);
	ck_assert_ptr_eq(req->session, sess);
	// show_session(sess);

	afb_req_common_set_session_string(req, "session");
	// show_session(req->session);
	ck_assert_str_eq("session", afb_session_uuid(req->session));
	ck_assert_int_ne(afb_session_id(sess), afb_session_id(req->session));

	ck_assert_int_ge(afb_req_common_session_set_LOA_hookable(req, 2), 0);
	// show_session(req->session);
	ck_assert_int_eq(afb_session_get_loa(req->session, NULL), 2);

	afb_req_common_session_close_hookable(req);
	// show_session(req->session);
	ck_assert_int_eq(req->closing,1);

	fprintf(stderr, "afb_req_common_get_client_info_hookable returned %s\n", json_object_to_json_string(afb_req_common_get_client_info_hookable(req)));

	afb_req_common_unref(req);
}
END_TEST

START_TEST (prepare_forwarding)
{
	struct afb_req_common *req = &comreq;

	struct afb_type *type1;
	struct afb_data *data[NB_DATA];

	int rc, i, j;
	int dataChecksum;

	afb_req_common_init(req, &test_queryitf, "", "", 0, NULL, NULL);

	// show_req(req);
	fprintf(stderr, "\n### Prepare forwarding...\n");

	type1 = afb_type_get("type1");
	if(!type1){
		rc = afb_type_register(&type1, "type1", 0, 0, 0);
		fprintf(stderr, "afb_type_register returned : %d\n", rc);
		ck_assert_int_eq(rc, 0);
	}

	dataChecksum = 0;
	for(j=1; j<=NB_DATA; j++){
		gval = 0;
		fprintf(stderr, "\nprepare forwarding of %d data\n", j);
		for(i=1; i<=j; i++){
			fprintf(stderr, "creating data with closure = %d\n", i);
			rc = afb_data_create_raw(&data[i-1], type1, NULL, 0, dataClosureCB, i2p(i));
			ck_assert_int_eq(rc, 0);
		}
		afb_req_common_prepare_forwarding(req, apiname, verbname, (unsigned)j, data);
		ck_assert_str_eq(req->apiname, apiname);
		ck_assert_str_eq(req->verbname, apiname);
		ck_assert_int_eq(req->params.ndata, j);
		ck_assert_int_eq(gval, dataChecksum); // check that previous data closure is call
		dataChecksum += j;
	}

	afb_req_common_unref(req);
}
END_TEST

START_TEST(push_and_pop)
{
	struct afb_req_common *req = &comreq;

	int rc, i;

	fprintf(stderr, "\n### Push/Pop Requests...\n");

	fprintf(stderr, "push request 1\n");
	ck_assert_int_eq(afb_req_common_async_push(req, i2p(1)),1);
	ck_assert_int_eq(req->asyncount, 1);
	ck_assert_ptr_nonnull(req->asyncitems[0]);

	fprintf(stderr, "push request 2 and 3\n");
	ck_assert_int_eq(afb_req_common_async_push2(req, i2p(2), i2p(3)),1);
	ck_assert_int_eq(req->asyncount, 3);
	ck_assert_ptr_nonnull(req->asyncitems[1]);
	ck_assert_ptr_nonnull(req->asyncitems[2]);

	for(i=3; i>0; i--){
		rc = p2i(afb_req_common_async_pop(req));
		fprintf(stderr, "pop returned %d\n", rc);
		ck_assert_int_eq(rc, i);
		ck_assert_int_eq(req->asyncount, i-1);
	}
	//show_req(req);

	rc = 1;
	i = 0;
	while(rc == 1){
		rc = afb_req_common_async_push(req, i2p(i));
		fprintf(stderr, "afb_req_common_async_push(req, i2p(%d)) returned %d\n", i, rc);
		if(rc){
			i++;
			ck_assert_int_eq(req->asyncount, i);
		}
	}
	fprintf(stderr, "afb_req_common_async_push was able to push %d requests\n", i);
	ck_assert_int_eq(i, 7);

	afb_req_common_unref(req);
}
END_TEST

START_TEST (process)
{
	struct afb_req_common *req = &comreq;

	struct afb_type *type1;
	struct afb_data *data[NB_DATA];

	struct afb_api_itf itf = {
		.process = api_process,
		.unref = apiClosureCB
	};
	struct afb_apiset *test_apiset;
	struct afb_api_item api_item = {
		.closure = i2p(255),
		.group = 0,
		.itf = &itf
	};

	int rc, i;
	int dataChecksum = 0;

	fprintf(stderr, "\n### Processing Request...\n");

	type1 = afb_type_get("type1");
	if(!type1){
		rc = afb_type_register(&type1, "type1", 0, 0, 0);
		fprintf(stderr, "afb_type_register returned : %d\n", rc);
		ck_assert_int_eq(rc, 0);
	}

	for(i=1; i<=NB_DATA; i++){
		fprintf(stderr, "creating data with closure = %d\n", i);
		rc = afb_data_create_raw(&data[i-1], type1, NULL, 0, dataClosureCB, i2p(i));
		ck_assert_int_eq(rc, 0);
		dataChecksum += i;
	}

	afb_req_common_init(req, &test_queryitf, "", "", NB_DATA, data, NULL);

	test_apiset = afb_apiset_create("toto", 1);
	afb_apiset_add(test_apiset, req->apiname, api_item);

	i=0;
	gval = 0;

	afb_req_common_process(req, test_apiset);

	sched_jobs("PROCESS");

	afb_req_common_unref(req);

	ck_assert_int_eq(gApiVal, 255); // check that api callback was call
	ck_assert_int_eq(gval, dataChecksum); // check that data closure CB was call
}
END_TEST

START_TEST(process_on_behalf)
{
	struct afb_req_common *req = &comreq;

	struct afb_type *type1;
	struct afb_data *data[NB_DATA];

	struct afb_api_itf itf = {
		.process = api_process,
		.unref = apiClosureCB
	};
	struct afb_apiset *test_apiset;
	struct afb_api_item api_item = {
		.closure = i2p(255),
		.group = 0,
		.itf = &itf
	};

	int rc, i;
	int dataChecksum = 0;

	struct json_object *res;

	fprintf(stderr, "\n### Processing Request on behalf...\n");

	afb_req_common_init(req, &test_queryitf, "", "", 0, NULL, NULL);

	type1 = afb_type_get("type1");
	if(!type1){
		rc = afb_type_register(&type1, "type1", 0, 0, 0);
		fprintf(stderr, "afb_type_register returned : %d\n", rc);
		ck_assert_int_eq(rc, 0);
	}

	for(i=1; i<=NB_DATA; i++){
		fprintf(stderr, "creating data with closure = %d\n", i);
		rc = afb_data_create_raw(&data[i-1], type1, NULL, 0, dataClosureCB, i2p(i));
		ck_assert_int_eq(rc, 0);
		dataChecksum += i;
	}

	afb_req_common_init(req, &test_queryitf, "", "", NB_DATA, data, NULL);

	test_apiset = afb_apiset_create("toto", 1);
	afb_apiset_add(test_apiset, req->apiname, api_item);

	i=0;
	gval = 0;
	gApiVal = 0;


	fprintf(stderr, "afb_req_common_process_on_behalf with \"1:1:1\" credential char : \n");
	//sprintf(buf, "%x:%x:%x-%n", uid, gid, pid, NULL);
	afb_req_common_process_on_behalf(req, test_apiset, "1:1:1-User::App::LABEL");
	ck_assert_ptr_nonnull(req->credentials);
	ck_assert_int_eq((int)req->credentials->uid, 1);
	ck_assert_int_eq((int)req->credentials->gid, 1);
	ck_assert_int_eq((int)req->credentials->pid, 1);

	res = afb_req_common_get_client_info_hookable(req);
	fprintf(stderr, "afb_req_common_get_client_info_hookable returned %s\n", json_object_to_json_string(res));
	ck_assert_int_eq(
		rp_jsonc_equal(
			res,
			json_tokener_parse(
				"{ \"uid\": 1, \"gid\": 1, \"pid\": 1, \"user\": \"1\", \"label\": \"User::App::LABEL\", \"id\": \"LABEL\" }"
			)
		),
		1
	);

	sched_jobs("PROCESS ON BEHALF 1");

	ck_assert_int_eq(gApiVal, 255); // check that api callback was call
	ck_assert_int_eq(gval, dataChecksum); // check that data closure CB was call
//	afb_req_common_unref(req);

	i =0;
	gval = 0;
	gApiVal = 0;

	fprintf(stderr, "afb_req_common_process_on_behalf wiht NULL credantial char : \n");
	//sprintf(buf, "%x:%x:%x-%n", uid, gid, pid, NULL);
	afb_req_common_process_on_behalf(req, test_apiset, NULL);
	ck_assert_ptr_null(req->credentials);

	sched_jobs("PROCESS ON BEHALF 2");

	ck_assert_int_eq(gApiVal, 255); // check that api callback was call
	//ck_assert_int_eq(gval, dataChecksum); // check that data closure CB was call

	gApiVal = 0;
}
END_TEST

START_TEST(errors)
{
	struct afb_req_common *req = &comreq;
	int r;

	fprintf(stderr, "\n### Errors\n");

	afb_req_common_init(req, &test_queryitf, apiname, verbname, 0, NULL, NULL);
	r = afb_req_common_reply_out_of_memory_error_hookable(req);
	fprintf(stderr, "afb_req_common_reply_out_of_memory_error_hookable returned %d\n", r);
	ck_assert_int_eq(r, AFB_ERRNO_OUT_OF_MEMORY);

	afb_req_common_init(req, &test_queryitf, apiname, verbname, 0, NULL, NULL);
	r = afb_req_common_reply_internal_error_hookable(req, -1);
	fprintf(stderr, "afb_req_common_reply_internal_error_hookable returned %d\n", r);
	ck_assert_int_eq(r,AFB_ERRNO_INTERNAL_ERROR);

	afb_req_common_init(req, &test_queryitf, apiname, verbname, 0, NULL, NULL);
	r = afb_req_common_reply_unavailable_error_hookable(req);
	fprintf(stderr, "afb_req_common_reply_unavailable_error_hookable returned %d\n", r);
	ck_assert_int_eq(r,AFB_ERRNO_NOT_AVAILABLE);

	afb_req_common_init(req, &test_queryitf, apiname, verbname, 0, NULL, NULL);
	r = afb_req_common_reply_api_unknown_error_hookable(req);
	fprintf(stderr, "afb_req_common_reply_api_unknown_error_hookable returned %d\n", r);
	ck_assert_int_eq(r,AFB_ERRNO_UNKNOWN_API);

	afb_req_common_init(req, &test_queryitf, apiname, verbname, 0, NULL, NULL);
	r = afb_req_common_reply_api_bad_state_error_hookable(req);
	fprintf(stderr, "afb_req_common_reply_api_bad_state_error_hookable returned %d\n", r);
	ck_assert_int_eq(r,AFB_ERRNO_BAD_API_STATE);

	afb_req_common_init(req, &test_queryitf, apiname, verbname, 0, NULL, NULL);
	r = afb_req_common_reply_verb_unknown_error_hookable(req);
	fprintf(stderr, "afb_req_common_reply_verb_unknown_error_hookable returned %d\n", r);
	ck_assert_int_eq(r,AFB_ERRNO_UNKNOWN_VERB);

	afb_req_common_init(req, &test_queryitf, apiname, verbname, 0, NULL, NULL);
	r = afb_req_common_reply_invalid_token_error_hookable(req);
	fprintf(stderr, "afb_req_common_reply_invalid_token_error_hookable returned %d\n", r);
	ck_assert_int_eq(r,AFB_ERRNO_INVALID_TOKEN);

	afb_req_common_init(req, &test_queryitf, apiname, verbname, 0, NULL, NULL);
	r = afb_req_common_reply_insufficient_scope_error_hookable(req, "scop");
	fprintf(stderr, "afb_req_common_reply_insufficient_scope_error_hookable returned %d\n", r);
	ck_assert_int_eq(r,AFB_ERRNO_INSUFFICIENT_SCOPE);
}
END_TEST

START_TEST (subscribe)
{
	struct afb_req_common *req = &comreq;
	struct afb_evt *ev;
	int r;

	fprintf(stderr, "\n### Subscribe\n");

	afb_req_common_init(req, &test_queryitf, apiname, verbname, 0, NULL, NULL);

	ck_assert_int_eq(afb_evt_create(&ev, "test_event"),0);

	test_subscribe_req = NULL;
	test_subscribe_event = NULL;
	r = afb_req_common_subscribe(req, ev);
	fprintf(stderr, "afb_req_common_subscribe returned %d\n", r);
	ck_assert_int_eq(r, 0);
	ck_assert_ptr_eq(test_subscribe_req, req);
	ck_assert_ptr_eq(test_subscribe_event, ev);

	test_unsubscribe_req = NULL;
	test_unsubscribe_event = NULL;
	r = afb_req_common_unsubscribe(req, ev);
	fprintf(stderr, "afb_req_common_unsubscribe returned %d\n", r);
	ck_assert_int_eq(r, 0);
	ck_assert_ptr_eq(test_unsubscribe_req, req);
	ck_assert_ptr_eq(test_unsubscribe_event, ev);

	test_subscribe_req = NULL;
	test_subscribe_event = NULL;
	r = afb_req_common_subscribe_hookable(req, ev);
	fprintf(stderr, "afb_req_common_subscribe_hookable returned %d\n", r);
	ck_assert_int_eq(r, 0);
	ck_assert_ptr_eq(test_subscribe_req, req);
	ck_assert_ptr_eq(test_subscribe_event, ev);

	test_unsubscribe_req = NULL;
	test_unsubscribe_event = NULL;
	r = afb_req_common_unsubscribe_hookable(req, ev);
	fprintf(stderr, "afb_req_common_unsubscribe_hookable returned %d\n", r);
	ck_assert_int_eq(r, 0);
	ck_assert_ptr_eq(test_unsubscribe_req, req);
	ck_assert_ptr_eq(test_unsubscribe_event, ev);

	test_subscribe_req = NULL;
	test_subscribe_event = NULL;
	req->replied = 1;
	r = afb_req_common_subscribe(req, ev);
	fprintf(stderr, "afb_req_common_subscribe returned %d\n", r);
	ck_assert_int_eq(r, -22);
	ck_assert_ptr_eq(test_subscribe_req, NULL);
	ck_assert_ptr_eq(test_subscribe_event, NULL);

	test_unsubscribe_req = NULL;
	test_unsubscribe_event = NULL;
	req->replied = 1;
	r = afb_req_common_unsubscribe(req, ev);
	fprintf(stderr, "afb_req_common_unsubscribe returned %d\n", r);
	ck_assert_int_eq(r, -22);
	ck_assert_ptr_eq(test_unsubscribe_req, NULL);
	ck_assert_ptr_eq(test_unsubscribe_event, NULL);
}
END_TEST

void test_check_perm(int sig, void * arg){

	struct afb_req_common * req = (struct afb_req_common *)arg;
	int r = 0;

	fprintf(stderr, "Entered test_check_perm with sig %d\n", sig);
	ck_assert_int_eq(sig, 0);

	r = afb_req_common_has_permission_hookable(req, "perm");
	fprintf(stderr, "afb_req_common_has_permission_hookable returned %d\n", r);
	ck_assert_int_eq(r,1);

	gval++;

	afb_sched_exit(0, NULL, NULL, 0);
}

START_TEST(check_perm)
{
	struct afb_req_common req;

	fprintf(stderr, "\n### Check Perm\n");

	afb_req_common_init(&req, &test_queryitf, "api", "verb", 0, NULL, NULL);

	afb_req_common_check_permission_hookable(&req, "perm", checkPermClosure, i2p(1), i2p(2), i2p(3));

	ck_assert_int_eq(gval, 1);

	// initialisation of the scheduler
	ck_assert_int_eq(afb_sig_monitor_init(1), 0);

	gval = 0;
	ck_assert_int_eq(afb_sched_start(10, 1, 10, test_check_perm, &req),0);
	ck_assert_int_eq(gval, 1);

}
END_TEST

START_TEST(reply)
{
	struct afb_req_common *req = &comreq;
	struct afb_type *type1;
	struct afb_data *data[REQ_COMMON_NDATA_DEF + NB_DATA];
	int rc, i;
	int dataChecksum = 0;

	fprintf(stderr, "\n### reply\n");

	afb_req_common_init(req, &test_queryitf, "", "", 0, NULL, NULL);

	type1 = afb_type_get("type1");
	if(!type1){
		rc = afb_type_register(&type1, "type1", 0, 0, 0);
		fprintf(stderr, "afb_type_register returned : %d\n", rc);
		ck_assert_int_eq(rc, 0);
	}

	fprintf(stderr, "------\ntest that memory get allocated when reply requires more than REQ_COMMON_NREPLIES_MAX=%d data\n", REQ_COMMON_NDATA_DEF);

	for (i=1; i<=REQ_COMMON_NDATA_DEF + NB_DATA; i++){
		fprintf(stderr, "creating data with closure = %d\n", i);
		rc = afb_data_create_raw(&data[i-1], type1, NULL, 0, dataClosureCB, i2p(i));
		ck_assert_int_eq(rc, 0);
		dataChecksum += i;
	}

	afb_req_common_reply_hookable(req, 0, REQ_COMMON_NDATA_DEF + NB_DATA, data);

#if WITH_REPLY_JOB
	ck_assert_ptr_ne(req->replies.data, req->replies.local);
#endif

	gval = 0;

	sched_jobs("REPLY 1");

	ck_assert_int_eq(gval, dataChecksum);

	fprintf(stderr, "------\ntest that the static buffer is used when reply requires les than REQ_COMMON_NREPLIES_MAX=%d data\n", REQ_COMMON_NDATA_DEF);

	req->replied = 0;
	dataChecksum = 0;

	for (i=1; i<=REQ_COMMON_NDATA_DEF; i++){
		fprintf(stderr, "creating data with closure = %d\n", i);
		rc = afb_data_create_raw(&data[i-1], type1, NULL, 0, dataClosureCB, i2p(i));
		ck_assert_int_eq(rc, 0);
		dataChecksum += i;
	}

	afb_req_common_reply_hookable(req, 0, REQ_COMMON_NDATA_DEF, data);

#if WITH_REPLY_JOB
	ck_assert_ptr_eq(req->replies.data, req->replies.local);
#endif

	gval = 0;

	sched_jobs("REPLY 2");

	ck_assert_int_eq(gval, dataChecksum);

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
	mksuite("req-common");
		addtcase("req-common");
			addtest(test);
			addtest(session);
			addtest(prepare_forwarding);
			addtest(push_and_pop);
			addtest(process);
			addtest(process_on_behalf);
			addtest(errors);
			addtest(subscribe);
			addtest(check_perm);
			addtest(reply);
	return !!srun();
}
