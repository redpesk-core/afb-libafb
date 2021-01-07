/*
 Copyright (C) 2015-2021 IoT.bzh Company

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
#include <string.h>
#include <unistd.h>

#include <check.h>

#include <afb/afb-req-subcall-flags.h>

#include "utils/wrap-json.h"
#include "apis/afb-api-so.h"
#include "core/afb-calls.h"
#include "core/afb-api-common.h"
#include "core/afb-req-common.h"
#include "core/afb-apiset.h"
#include "core/afb-jobs.h"
#include "core/afb-sched.h"
#include "core/afb-data.h"
#include "core/afb-type.h"
#include "core/afb-evt.h"
#include "core/afb-type-predefined.h"
#include "core/afb-params.h"



#define PATH_BUF_SIZE 200
#define NBPARAMS 3

#if WITH_REQ_PROCESS_ASYNC
#define RUNJOB afb_sched_wait_idle(1,1);
#else
#define RUNJOB (void)0
#endif

#define i2p(x)  ((void*)((intptr_t)(x)))
#define p2i(x)  ((int)((intptr_t)(x)))

/*********************************************************************/
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
	.unsubscribe = test_unsubscribe
};

/**************************************************************/

int gval, dataClosureGval, verbDataGval ;

int getpath(char buffer[PATH_BUF_SIZE], const char *base, int ival)
{
	static const char *paths[] = { "test-bindings/", "tests/", "src/", "build/", NULL };

	int rc;
	int len;
	int lenp;
	const char **pp = paths;


	len = snprintf(buffer, PATH_BUF_SIZE, base, ival);
	ck_assert_int_ge(len, 0);
	rc = access(buffer, F_OK);
	while (rc < 0 && *pp) {
		lenp = (int)strlen(*pp);
		if (lenp + len + 1 > PATH_BUF_SIZE)
			break;
		memmove(buffer + lenp, buffer, len + 1);
		memcpy(buffer, *pp, lenp);
		pp++;
		len += lenp;
		rc = access(buffer, F_OK);
	}
	if (rc == 0)
		fprintf(stderr, "FOUND %s for %s/%d\n", buffer, base, ival);
	else
		fprintf(stderr, "Can't find file %s/%d\n", base, ival);
	return rc;
}

void dataClosureCB(void *arg)
{
	dataClosureGval += p2i(arg);
	fprintf(stderr, "went through Data Closure with val %d\n", p2i(arg));
}

void testCB(void *closure1, void *closure2, void *closure3, int a, unsigned int b, struct afb_data *const * data){
    int i;
	fprintf(stderr, "testCB was called\n");
	for (i=0; i<b; i++) {
		verbDataGval += p2i(afb_data_const_pointer(data[i]));
	}
}

START_TEST (test)
{
	struct afb_data * params[NBPARAMS];
	struct afb_data * replies[NBPARAMS+1];
	struct afb_type * type1;
    struct afb_api_common comapi;
	struct afb_req_common req;
	struct afb_apiset * declare_set;
    struct afb_apiset * call_set;
    char name[] = "hello";
    char info[] = "Info";
    char path[PATH_BUF_SIZE];
	unsigned int nreplies;
	int status;

	int rc, i, checksum;

	// preparing params
	type1 = afb_type_get("type1");
	if (!type1) {
		rc = afb_type_register(&type1, "type1", 0, 0, 0);
		fprintf(stderr, "afb_type_register returned : %d\n", rc);
		ck_assert_int_eq(rc, 0);
	}
	checksum = 0;
	for(i=1; i<=NBPARAMS;  i++){
		fprintf(stderr, "creating data with closure = %d\n", i);
		rc = afb_data_create_raw(&params[i-1], type1, i2p(i), 0, dataClosureCB, i2p(i));
		checksum += i;
	}

	declare_set = afb_apiset_create("toto", 1);
	call_set = afb_apiset_create("tata", 1);

	gval = 0;

	// load a binding
	getpath(path, "libhello.so", 0);
    afb_api_so_add_binding(path, call_set, call_set);

    // inti it's api
    afb_api_common_init(&comapi, declare_set, call_set, name, 0, info, 0, path, 0);

	// inti a common req
	afb_req_common_init(&req, &test_queryitf, "toto","patatate", 0, NULL);

	
	/***** Test acync calls *****/
	fprintf(stderr, "\n### Test async calls\n");

	// Test req_calls_call
	verbDataGval = dataClosureGval = 0;
	afb_params_addref(NBPARAMS, params); // increase params referensing to reuse it after
	afb_calls_call(&comapi, name, "call", NBPARAMS, params, testCB, NULL, NULL, NULL);
	RUNJOB // if the system is not set to run jobs automaticaly run the jobs
	fprintf(stderr, "dataClosureGval = %d\n", dataClosureGval);
	fprintf(stderr, "verbDataGval = %d\n", verbDataGval);
	ck_assert_int_eq(verbDataGval, checksum);
	ck_assert_int_eq(dataClosureGval, 0);

	// Test req_calls_subcall
	verbDataGval = dataClosureGval = 0;
	afb_params_addref(NBPARAMS, params); // increase params referensing to reuse it after
	afb_calls_subcall(&comapi, name, "call", NBPARAMS, params, testCB, NULL, NULL, NULL, &req, 0);
	RUNJOB // if the system is not set to run jobs automaticaly run the jobs
	fprintf(stderr, "dataClosureGval = %d\n", dataClosureGval);
	fprintf(stderr, "verbDataGval = %d\n", verbDataGval);
	ck_assert_int_eq(verbDataGval, checksum);
	ck_assert_int_eq(dataClosureGval, 0);

	// Test req_calls_subscribe_cb
	verbDataGval = dataClosureGval = 0;
	afb_params_addref(NBPARAMS, params); // increase params referensing to reuse it after
    afb_calls_call(&comapi, name, "subscribe", NBPARAMS, params, testCB, NULL, NULL, NULL);
	RUNJOB // if the system is not set to run jobs automaticaly run the jobs
	fprintf(stderr, "dataClosureGval = %d\n", dataClosureGval);
	fprintf(stderr, "verbDataGval = %d\n", verbDataGval);
	ck_assert_int_eq(verbDataGval, checksum);
	ck_assert_int_eq(dataClosureGval, 0);

	// Test req_calls_unsubscribe_cb
	verbDataGval = dataClosureGval = 0;
	afb_params_addref(NBPARAMS, params); // increase params referensing to reuse it after
	afb_calls_call(&comapi, name, "unsubscribe", NBPARAMS, params, testCB, NULL, NULL, NULL);
    RUNJOB // if the system is not set to run jobs automaticaly run the jobs
	fprintf(stderr, "dataClosureGval = %d\n", dataClosureGval);
	fprintf(stderr, "verbDataGval = %d\n", verbDataGval);
	ck_assert_int_eq(verbDataGval, checksum);
	ck_assert_int_eq(dataClosureGval, 0);

	// test afb_calls_call_hooking
	verbDataGval = dataClosureGval = 0;
	afb_params_addref(NBPARAMS, params); // increase params referensing to reuse it after
	afb_calls_call_hooking(&comapi, name, "call", NBPARAMS, params, testCB, NULL, NULL, NULL);
	RUNJOB // if the system is not set to run jobs automaticaly run the jobs
	fprintf(stderr, "dataClosureGval = %d\n", dataClosureGval);
	fprintf(stderr, "verbDataGval = %d\n", verbDataGval);
	ck_assert_int_eq(verbDataGval, checksum);
	ck_assert_int_eq(dataClosureGval, 0);

	// Test afb_calls_subcall_hooking
	verbDataGval = dataClosureGval = 0;
	afb_params_addref(NBPARAMS, params); // increase params referensing to reuse it after
	afb_calls_subcall_hooking(&comapi, name, "call", NBPARAMS, params, testCB, NULL, NULL, NULL, &req, 0);
	RUNJOB // if the system is not set to run jobs automaticaly run the jobs
	fprintf(stderr, "dataClosureGval = %d\n", dataClosureGval);
	fprintf(stderr, "verbDataGval = %d\n", verbDataGval);
	ck_assert_int_eq(verbDataGval, checksum);
	ck_assert_int_eq(dataClosureGval, 0);


	/***** Test sync_calls *****/
	fprintf(stderr, "\n### Test sync calls\n");

	// test sync call
	nreplies = NBPARAMS+1;
	afb_params_addref(NBPARAMS, params); // increase params referensing to reuse it after
	ck_assert_int_eq(0, afb_calls_call_sync(&comapi, name, "call", NBPARAMS, params, &status, &nreplies, replies));
	fprintf(stderr, "nreplyes = %d\n", nreplies);
	fprintf(stderr, "status = %d\n", status);
	ck_assert_int_eq(status, 0);
	ck_assert_int_eq(NBPARAMS, nreplies);
	for (i=0; i<nreplies; i++) {
		ck_assert_int_eq(i+1, p2i(afb_data_const_pointer(replies[i])));
		ck_assert_ptr_eq(replies[i], params[i]);
	}
	afb_params_unref(nreplies, replies);

	// test sync subscribe
	nreplies = NBPARAMS+1;
	afb_params_addref(NBPARAMS, params); // increase params referensing to reuse it after
	ck_assert_int_eq(0, afb_calls_call_sync(&comapi, name, "subscribe", NBPARAMS, params, &status, &nreplies, replies));
	fprintf(stderr, "nreplyes = %d\n", nreplies);
	fprintf(stderr, "status = %d\n", status);
	ck_assert_int_eq(status, 0);
	ck_assert_int_eq(NBPARAMS, nreplies);
	for (i=0; i<nreplies; i++) {
		ck_assert_int_eq(i+1, p2i(afb_data_const_pointer(replies[i])));
		ck_assert_ptr_eq(replies[i], params[i]);
	}
	afb_params_unref(nreplies, replies);

	// test sync unsubscribe
	nreplies = NBPARAMS+1;
	afb_params_addref(NBPARAMS, params); // increase params referensing to reuse it after
	ck_assert_int_eq(0, afb_calls_call_sync(&comapi, name, "unsubscribe", NBPARAMS, params, &status, &nreplies, replies));
	fprintf(stderr, "nreplyes = %d\n", nreplies);
	fprintf(stderr, "status = %d\n", status);
	ck_assert_int_eq(NBPARAMS, nreplies);
	ck_assert_int_eq(status, 0);
	for (i=0; i<nreplies; i++) {
		ck_assert_int_eq(i+1, p2i(afb_data_const_pointer(replies[i])));
		ck_assert_ptr_eq(replies[i], params[i]);
	}
	afb_params_unref(nreplies, replies);

	// test sync subcall
	nreplies = NBPARAMS+1;
	afb_params_addref(NBPARAMS, params); // increase params referensing to reuse it after
	ck_assert_int_eq(0, afb_calls_subcall_sync(&comapi, name, "call", NBPARAMS, params, &status, &nreplies, replies, &req, afb_req_subcall_pass_events));
	fprintf(stderr, "nreplyes = %d\n", nreplies);
	fprintf(stderr, "status = %d\n", status);
	ck_assert_int_eq(status, 0);
	ck_assert_int_eq(NBPARAMS, nreplies);
	for (i=0; i<nreplies; i++) {
		ck_assert_int_eq(i+1, p2i(afb_data_const_pointer(replies[i])));
		ck_assert_ptr_eq(replies[i], params[i]);
	}
	afb_params_unref(nreplies, replies);

	// test sync subcall subscribe
	nreplies = NBPARAMS+1;
	afb_params_addref(NBPARAMS, params); // increase params referensing to reuse it after
	ck_assert_int_eq(0, afb_calls_subcall_sync(&comapi, name, "subscribe", NBPARAMS, params, &status, &nreplies, replies, &req, afb_req_subcall_pass_events));
	fprintf(stderr, "nreplyes = %d\n", nreplies);
	fprintf(stderr, "status = %d\n", status);
	ck_assert_int_eq(status, 0);
	ck_assert_int_eq(NBPARAMS, nreplies);
	for (i=0; i<nreplies; i++) {
		ck_assert_int_eq(i+1, p2i(afb_data_const_pointer(replies[i])));
		ck_assert_ptr_eq(replies[i], params[i]);
	}
	afb_params_unref(nreplies, replies);

	// test sync subcall unsubscribe
	nreplies = NBPARAMS+1;
	afb_params_addref(NBPARAMS, params); // increase params referensing to reuse it after
	ck_assert_int_eq(0, afb_calls_subcall_sync(&comapi, name, "unsubscribe", NBPARAMS, params, &status, &nreplies, replies, &req, afb_req_subcall_pass_events));
	fprintf(stderr, "nreplyes = %d\n", nreplies);
	fprintf(stderr, "status = %d\n", status);
	ck_assert_int_eq(status, 0);
	ck_assert_int_eq(NBPARAMS, nreplies);
	for (i=0; i<nreplies; i++) {
		ck_assert_int_eq(i+1, p2i(afb_data_const_pointer(replies[i])));
		ck_assert_ptr_eq(replies[i], params[i]);
	}
	afb_params_unref(nreplies, replies);

	// test sync call hooking
	nreplies = NBPARAMS+1;
	afb_params_addref(NBPARAMS, params); // increase params referensing to reuse it after
	ck_assert_int_eq(0, afb_calls_call_sync_hooking(&comapi, name, "call", NBPARAMS, params, &status, &nreplies, replies));
	fprintf(stderr, "nreplyes = %d\n", nreplies);
	fprintf(stderr, "status = %d\n", status);
	ck_assert_int_eq(status, 0);
	ck_assert_int_eq(NBPARAMS, nreplies);
	for (i=0; i<nreplies; i++) {
		ck_assert_int_eq(i+1, p2i(afb_data_const_pointer(replies[i])));
		ck_assert_ptr_eq(replies[i], params[i]);
	}
	afb_params_unref(nreplies, replies);

	// test sync subcall hooking
	nreplies = NBPARAMS+1;
	afb_params_addref(NBPARAMS, params); // increase params referensing to reuse it after
	ck_assert_int_eq(0, afb_calls_subcall_sync_hooking(&comapi, name, "call", NBPARAMS, params, &status, &nreplies, replies, &req, 0));
	fprintf(stderr, "nreplyes = %d\n", nreplies);
	fprintf(stderr, "status = %d\n", status);
	ck_assert_int_eq(status, 0);
	ck_assert_int_eq(NBPARAMS, nreplies);
	for (i=0; i<nreplies; i++) {
		ck_assert_int_eq(i+1, p2i(afb_data_const_pointer(replies[i])));
		ck_assert_ptr_eq(replies[i], params[i]);
	}
	afb_params_unref(nreplies, replies);

	// test the handling of a table of reply smaller than expected
	nreplies = NBPARAMS-1;
	afb_params_addref(NBPARAMS, params); // increase params referensing to reuse it after
	ck_assert_int_eq(0, afb_calls_call_sync(&comapi, name, "call", NBPARAMS, params, &status, &nreplies, replies));
	fprintf(stderr, "nreplyes = %d\n", nreplies);
	fprintf(stderr, "status = %d\n", status);
	ck_assert_int_eq(status, 0);
	ck_assert_int_eq(NBPARAMS-1, nreplies);
	for (i=0; i<nreplies; i++) {
		ck_assert_int_eq(i+1, p2i(afb_data_const_pointer(replies[i])));
		ck_assert_ptr_eq(replies[i], params[i]);
	}
	afb_params_unref(nreplies, replies);

	afb_params_unref(NBPARAMS, params);

	RUNJOB // if the system is not set to run jobs automaticaly run the jobs

	ck_assert_int_eq(dataClosureGval, checksum);

}
END_TEST

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
	mksuite("afb-calls");
		addtcase("afb-calls");
			addtest(test);
	return !!srun();
}
