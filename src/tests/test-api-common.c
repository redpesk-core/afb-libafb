/*
 Copyright (C) 2015-2023 IoT.bzh Company

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

#include "libafb-config.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include <check.h>

#include <rp-utils/rp-jsonc.h>
#include <rp-utils/rp-verbose.h>

#if !defined(ck_assert_ptr_null)
# define ck_assert_ptr_null(X)      ck_assert_ptr_eq(X, NULL)
# define ck_assert_ptr_nonnull(X)   ck_assert_ptr_ne(X, NULL)
#endif

#if !defined(CK_FUNCTION)
	#define CK_FUNCTION(MSG, EXPECTED_RC, RC, FUNCTION) {\
		fprintf(stderr, "\n## %s\n", MSG);\
		RC = FUNCTION;\
		fprintf(stderr, "-> rc = %d\n", RC);\
		ck_assert_int_eq(EXPECTED_RC, RC);\
	}
#endif

#include "core/afb-apiset.h"
#include "core/afb-api-common.h"
#include "core/afb-sched.h"
#include "core/afb-type.h"
#include "core/afb-data.h"
#include "core/afb-hook.h"
#include "sys/x-errno.h"

#if WITH_REQ_PROCESS_ASYNC
#define RUNJOB afb_sched_wait_idle(1,1);
#else
#define RUNJOB (void)0
#endif

#define NBPARAMS 3

/*********************************************************************/

struct afb_apiset *callset;
struct afb_apiset *declset;
struct afb_api_common capi;
char name[] = "name";
char aliasname[] = "aliasname";
char info[] = "info";
char path[] = "path";

/*********************************************************************/

START_TEST (test_init)
{
	struct afb_api_common *comapi = &capi;
	struct afb_session *s;

	rp_set_logmask(-1);
	declset = afb_apiset_create("test-apiv3-decl", 1);
	ck_assert_ptr_nonnull(declset);
	callset = afb_apiset_create("test-apiv3-call", 1);
	ck_assert_ptr_nonnull(callset);

	afb_api_common_init(
			comapi,
			declset,
			callset,
			name,
			0,
			info,
			0,
			path,
			0
		);

	/* test refcount */
	ck_assert_int_eq(comapi->state, Api_State_Pre_Init);
	ck_assert_ptr_eq(name, comapi->name);
	ck_assert_ptr_eq(name, afb_api_common_apiname(comapi));
	ck_assert_ptr_eq(name, afb_api_common_visible_name(comapi));
	ck_assert_ptr_eq(info, comapi->info);
	ck_assert_ptr_eq(path, comapi->path);

	ck_assert_ptr_eq(declset, comapi->declare_set);
	ck_assert_ptr_eq(callset, comapi->call_set);
	ck_assert_ptr_eq(callset, afb_api_common_call_set(comapi));

	ck_assert_int_eq(0, comapi->free_name);
	ck_assert_int_eq(0, comapi->free_info);
	ck_assert_int_eq(0, comapi->free_path);

	ck_assert_ptr_eq(NULL, comapi->listener);
	ck_assert_ptr_eq(NULL, comapi->event_handlers);
	ck_assert_ptr_eq(NULL, comapi->onevent);
	ck_assert_ptr_eq(NULL, comapi->settings);

	s = afb_api_common_session_get(comapi);
	ck_assert_ptr_ne(NULL, s);
#if WITH_API_SESSIONS
	ck_assert_ptr_eq(s, comapi->session);
	ck_assert_int_eq(0, afb_api_common_unshare_session(comapi));
	ck_assert_ptr_ne(s, comapi->session);
	s = afb_api_common_session_get(comapi);
	ck_assert_ptr_eq(s, comapi->session);
#endif

#if WITH_AFB_HOOK
	ck_assert_int_eq(0, comapi->hookflags);
#endif

	ck_assert_int_eq(0, comapi->sealed);
	ck_assert_int_eq(0, afb_api_common_is_sealed(comapi));
	afb_api_common_api_seal(comapi);
	ck_assert_int_eq(1, comapi->sealed);
	ck_assert_int_eq(1, afb_api_common_is_sealed(comapi));

	/* test refcount */
	ck_assert_int_eq(1, comapi->refcount);
	afb_api_common_incref(comapi);
	ck_assert_int_eq(2, comapi->refcount);
	ck_assert_int_eq(0, afb_api_common_decref(comapi));
	ck_assert_int_eq(1, comapi->refcount);
	ck_assert_int_ne(0, afb_api_common_decref(comapi));
	ck_assert_int_eq(0, comapi->refcount);
	afb_api_common_cleanup(comapi);
}
END_TEST

/*********************************************************************/

void test_cb(int sig, void * arg){
	int * val = arg;
	fprintf(stderr, "test_cb was called with sig = %d and arg = %d\n", sig, *val);
	*val = *val+1;
}

int test_start_cb(void * closure){
	int * val = closure;
	fprintf(stderr, "test_start_cb was called with and arg = %d\n", *val);
	*val = *val + 1;
	return *val;
}

struct afb_api_itf api_itf_null = {
	.process = NULL,
	.service_start = NULL,
#if WITH_AFB_HOOK
	.update_hooks = NULL,
#endif
	.get_logmask = NULL,
	.set_logmask = NULL,
	.describe = NULL,
	.unref = NULL
};

int observation;

void observe(int loglevel, const char *file, int line, const char *function, const char *fmt, va_list args){
	fprintf(stderr, "made an observation ! : loglevel = %d, file = %s, line = %d, function = %s, fmt = %s\n", loglevel, file, line, function, fmt);
	ck_assert_int_eq(4, loglevel);
	ck_assert_int_eq(666, line);
	ck_assert_str_eq("test", file);
	ck_assert_str_eq("this_is_a_test", function);

	observation++;
}

void dataClosureCB(void *arg)
{
	int * val = arg;
	fprintf(stderr, "went through Data Closure with val %d\n", *val);
	*val = *val + 1;
}

void test_vverbose(struct afb_api_common *comapi, ...)
{
	/******** vverbose ********/
	fprintf(stderr, "\n******** vverbose ********\n");

	observation = 0;
	va_list test_va_list;
	rp_verbose_observer = observe;
	va_start(test_va_list, comapi);
	afb_api_common_vverbose_hookable(comapi, 4, "test", 666, "this_is_a_test", "test message %d", test_va_list);
	va_end(test_va_list);
	fprintf(stderr, "vverbose test message observerd %d time\n", observation);
	ck_assert_int_eq(1, observation);
	rp_verbose_observer = NULL;
}

START_TEST (test_functional)
{
	struct afb_api_common *comapi = &capi;
	// struct afb_session *s;
	struct json_object * settings;
	struct json_object * config;

	struct afb_api_item sa;

	struct afb_data * params[NBPARAMS];
	struct afb_type * type1;

	sa.itf = &api_itf_null;
	sa.closure = NULL;
	sa.group = NULL;

	int rc, i, test_val, checksum;

	/* initialisation */
	declset = afb_apiset_create("test-apiv3-decl", 1);
	ck_assert_ptr_nonnull(declset);
	callset = afb_apiset_create("test-apiv3-call", 1);
	ck_assert_ptr_nonnull(callset);

	afb_api_common_init(
			comapi,
			declset,
			callset,
			name,
			0,
			info,
			0,
			path,
			0
		);

#if WITH_AFB_HOOK
	comapi->hookflags = afb_hook_flags_api_all;
#endif

	/******** settings ********/
	fprintf(stderr, "\n******** settings ********\n");

	fprintf(stderr, "comapi->setting = %s\n", json_object_to_json_string(comapi->settings));
	ck_assert_ptr_null(comapi->settings);

	fprintf(stderr, "make settings...\n");
	settings = afb_api_common_settings_hookable(comapi);
	fprintf(stderr, "comapi->setting = %s\n", json_object_to_json_string(comapi->settings));
	ck_assert_ptr_eq(settings, comapi->settings);
	ck_assert_int_eq(0, rp_jsonc_check(comapi->settings, "{s:s}", "binding-path", path));
	settings = rp_jsonc_clone(comapi->settings);
	afb_api_common_set_config(settings);

	comapi->settings = NULL;
	fprintf(stderr, "set up a json config and load it ...\n");
	// set up the json config
	ck_assert_int_eq(0, rp_jsonc_pack(&config, "{ss ss}", "binding-path", path, "binding-info", info));
	ck_assert_int_eq(0,	rp_jsonc_pack(&settings, "{so}", name, config));
	// load it
	afb_api_common_set_config(settings);
	// set up the api settings based on the the priviously set config
	settings = afb_api_common_settings_hookable(comapi);
	fprintf(stderr, "comapi->setting = %s\n", json_object_to_json_string(comapi->settings));
	ck_assert_ptr_eq(settings, comapi->settings);
	ck_assert_int_eq(1, rp_jsonc_equal(comapi->settings, config));

	/******** job ********/
	fprintf(stderr, "\n******** job ********\n");

	test_val = 0;

	rc = afb_api_common_post_job_hookable(comapi, 0, 1, test_cb, &test_val, NULL);
	fprintf(stderr, "Posting a job with afb_api_common_post_job returned %d\n", rc);

	fprintf(stderr, "Run the job and and test it by checking that test_val has been incremented\n");
	RUNJOB;

	fprintf(stderr, "testval = %d\n", test_val);
	ck_assert_int_eq(1, test_val);

	/******** alias ********/
	fprintf(stderr, "\n******** alias ********\n");

	fprintf(stderr, "Create the alias '%s' to the api '%s'\n", aliasname, name);
	afb_apiset_add(declset, name, sa);
	rc = afb_api_common_add_alias_hookable(comapi, NULL, aliasname);
	ck_assert_int_eq(0, rc);

	fprintf(stderr, "Try to create it again and check that it pops an error\n");
	rc = afb_api_common_add_alias_hookable(comapi, NULL, aliasname);
	ck_assert_int_eq(X_EEXIST, rc);

	fprintf(stderr, "Try to create an invalid named alias and check that it pops an error\n");
	rc = afb_api_common_add_alias_hookable(comapi, NULL, "bad\\alias\"n&me");
	ck_assert_int_eq(X_EINVAL, rc);

	/******** vverbose ********/
	test_vverbose(comapi, 444);

	/******** event_broadcast ********/
	fprintf(stderr, "\n******** event_broadcast ********\n");

	// preparing params
	type1 = afb_type_get("type1");
	if (!type1) {
		rc = afb_type_register(&type1, "type1", 0, 0, 0);
		ck_assert_int_eq(rc, 0);
	}
	checksum = 0;
	int data_closure = 0;
	for(i=1; i<=NBPARAMS;  i++){
		rc = afb_data_create_raw(&params[i-1], type1, NULL, 0, dataClosureCB, &data_closure);
		checksum += i;
	}

	fprintf(stderr, "\n### try to broadcast event befor starting api...\n");
	rc = afb_api_common_event_broadcast_hookable(comapi, "test_event", NBPARAMS, params);
	fprintf(stderr, "-> rc = %d\n", rc);
	ck_assert_int_eq(X_EINVAL, rc);


	fprintf(stderr, "\n### start api...\n");
	int test_start_closure = 0;
	rc = afb_api_common_start(comapi, test_start_cb, &test_start_closure);
	fprintf(stderr, "-> rc = %d\n", rc);
	fprintf(stderr, "-> test_start_closure = %d\n", test_start_closure);
	ck_assert_int_eq(1, rc);
	ck_assert_int_eq(1, test_start_closure);
	ck_assert_int_eq(comapi->state, Api_State_Run);

	fprintf(stderr, "Cheeck that afb_api_common_start return an error when api is in inti state\n");
	comapi->state = Api_State_Init;
	test_start_closure = 0;
	rc = afb_api_common_start(comapi, test_start_cb, &test_start_closure);
	fprintf(stderr, "-> rc = %d\n", rc);
	fprintf(stderr, "-> test_start_closure = %d\n", test_start_closure);
	ck_assert_int_eq(X_EBUSY, rc);
	ck_assert_int_eq(0, test_start_closure);

	comapi->state = Api_State_Run;

	fprintf(stderr, "\n### retry to broadcast event...\n");
	rc = afb_api_common_event_broadcast_hookable(comapi, "test_event", NBPARAMS, params);
	RUNJOB;
	fprintf(stderr, "-> rc = %d\n", rc);
	fprintf(stderr, "-> data_closure = %d\n", data_closure);
	ck_assert_int_eq(0, rc);
	ck_assert_int_eq(NBPARAMS, data_closure);

	/******** require api ********/
	fprintf(stderr, "\n******** require api ********\n");
	fprintf(stderr, "require an api on an empty set...\n");
	rc = afb_api_common_require_api_hookable(comapi, name, 1);
	fprintf(stderr, "-> rc = %d\n", rc);
	ck_assert_int_eq(-2, rc);

	fprintf(stderr, "add api name to api set and try again...\n");
	afb_apiset_add(callset, name, sa);
	rc = afb_api_common_require_api_hookable(comapi, name, 1);
	fprintf(stderr, "-> rc = %d\n", rc);
	ck_assert_int_eq(0, rc);

	/******** api seal ********/
	fprintf(stderr, "\n******** api seal ********\n");
	ck_assert_int_eq(0, comapi->sealed);
	ck_assert_int_eq(0, afb_api_common_is_sealed(comapi));
	afb_api_common_api_seal_hookable(comapi);
	ck_assert_int_eq(1, comapi->sealed);
	ck_assert_int_eq(1, afb_api_common_is_sealed(comapi));
	fprintf(stderr, "ok\n");

	/******** class provide/require ********/
	fprintf(stderr, "\n******** class provide/require ********\n");

	/* WARNING :
	 * Does not return an error because the afb_apiset_require_class calls
	 * class_search with 1 at 2nd parametter witch means the class will be
	 * created if not found.
	 */
	fprintf(stderr, "require class '%s' before it has been provide...\n", name);
	rc = afb_api_common_class_require_hookable(comapi, name);
	fprintf(stderr, "-> rc = %d\n", rc);
	// ck_assert_int_eq(X_ENOENT, rc);

	fprintf(stderr, "provide the class '%s'...\n", name);
	rc = afb_api_common_class_provide_hookable(comapi, name);
	fprintf(stderr, "-> rc = %d\n", rc);
	ck_assert_int_eq(0, rc);

	fprintf(stderr, "require class '%s'...\n", name);
	rc = afb_api_common_class_require_hookable(comapi, name);
	fprintf(stderr, "-> rc = %d\n", rc);
	ck_assert_int_eq(0, rc);

	fprintf(stderr, "Deleet the declar api set and check that requiring a class now return an error...\n");
	afb_apiset_del(declset, name);
	rc = afb_api_common_class_require_hookable(comapi, name);
	fprintf(stderr, "-> rc = %d\n", rc);
	ck_assert_int_eq(X_ENOENT, rc);

	afb_api_common_cleanup(comapi);
}
END_TEST

void onevent_comapi_test_cb(void *callback, void *closure, const struct afb_evt_data *event, struct afb_api_common *comapi){
	int * val1 = callback;
	int * val2 = closure;
	if(closure && callback){
		fprintf(stderr, "went through onevent_comapi_test_cb with callback = %d, closure = %d\n", *val1, *val2);
		*val2 =+ *val1;
	}
	else
		fprintf(stderr, "went through onevent_comapi_test_cb with callback = %p, closure = %p\n", val1, val2);
}

START_TEST (test_listeners)
{
	fprintf(stderr, "\n******** listeners ********\n");

	struct afb_api_common *comapi = &capi;

	struct afb_data * params[NBPARAMS];
	struct afb_type * type1;

	struct afb_evt * evt;

	int rc, i, test_val, checksum;

	/* initialisation */
	declset = afb_apiset_create("test-apiv3-decl", 1);
	ck_assert_ptr_nonnull(declset);
	callset = afb_apiset_create("test-apiv3-call", 1);
	ck_assert_ptr_nonnull(callset);

	afb_api_common_init(
			comapi,
			declset,
			callset,
			name,
			0,
			info,
			0,
			path,
			0
		);
	comapi->onevent = onevent_comapi_test_cb;

	// preparing params
	type1 = afb_type_get("type1");
	if (!type1) {
		rc = afb_type_register(&type1, "type1", 0, 0, 0);
		ck_assert_int_eq(rc, 0);
	}
	checksum = 0;
	int data_closure = 0;
	for(i=1; i<=NBPARAMS;  i++){
		rc = afb_data_create_raw(&params[i-1], type1, NULL, 0, dataClosureCB, &data_closure);
		checksum += i;
	}

#if WITH_AFB_HOOK
	comapi->hookflags = afb_hook_flags_api_all;
#endif

	CK_FUNCTION( "add an event handler...", 0, rc,
		afb_api_common_event_handler_add(comapi, name, test_cb, &test_val));

	CK_FUNCTION("try to re-add the same event handler...", X_EEXIST, rc,
		afb_api_common_event_handler_add(comapi, name, test_cb, &test_val));

	int test_start_closure = 0;
	CK_FUNCTION("start api...", 1, rc,
		afb_api_common_start(comapi, test_start_cb, &test_start_closure));
	fprintf(stderr, "-> test_start_closure = %d\n", test_start_closure);
	ck_assert_int_eq(1, test_start_closure);

	CK_FUNCTION("afb_api_common_new_event...", 0, rc,
		afb_api_common_new_event(comapi, name, &evt));

	CK_FUNCTION("afb_api_common_subscribe...", 0, rc,
		afb_api_common_subscribe(comapi, evt));

	CK_FUNCTION("try to broadcast event...", 0, rc,
		afb_api_common_event_broadcast_hookable(comapi, "test_event", NBPARAMS, params);
		RUNJOB);
	fprintf(stderr, "-> data_closure = %d\n", data_closure);
	ck_assert_int_eq(3, data_closure);

	CK_FUNCTION("deleet event handler", 0, rc,
		afb_api_common_event_handler_del(comapi, name, NULL));

	CK_FUNCTION("try to re-deleet the same event handler...", X_ENOENT, rc,
		afb_api_common_event_handler_del(comapi, name, NULL));

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
	mksuite("api-common");
		addtcase("api-common");
			addtest(test_init);
			addtest(test_functional);
			addtest(test_listeners);
	return !!srun();
}
