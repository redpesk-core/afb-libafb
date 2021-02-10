/*
 Copyright (C) 2015-2021 IoT.bzh Company

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

#include <check.h>

#if !defined(ck_assert_ptr_null)
# define ck_assert_ptr_null(X)      ck_assert_ptr_eq(X, NULL)
# define ck_assert_ptr_nonnull(X)   ck_assert_ptr_ne(X, NULL)
#endif

#define AFB_BINDING_VERSION 0

#include <afb/afb-binding.h>
#include "core/afb-apiset.h"
#include "core/afb-api-v4.h"
#include "core/afb-string-mode.h"
#include "core/afb-sched.h"
#include "core/afb-hook.h"
#include "core/afb-req-v4.h"
#include "core/afb-data.h"
#include "core/afb-type.h"
#include "core/afb-type-predefined.h"
#include "sys/verbose.h"
#include "sys/x-errno.h"
#include "utils/wrap-json.h"

#define NBDATA 3

#if WITH_REQ_PROCESS_ASYNC
#define RUNJOB afb_sched_wait_idle(1,1);
#else
#define RUNJOB (void)0
#endif

#define VERB_INFO "This is test info"
#define API_NAME "test_api"
#define API_ALIAS_NAME "api_alias_name"
#define API_INFO "This is an api for tests purposes"
#define EXPECTED_API_DESCRIPTION "{\
    \"openapi\": \"3.0.0\",\
    \"info\": {\
        \"version\": \"0.0.0\",\
		\"description\": \"This is an api for tests purposes\",\
        \"title\": \"test_api\"\
    }\
    ,\
    \"paths\": {\
        \"\\/verb\": {\
            \"get\": {\
                \"responses\": {\
                    \"200\": {\
                        \"description\": \"verb\"\
                    }\
                }\
            }\
        },\
        \"\\/info\": {\
            \"get\": {\
                \"responses\": {\
                    \"200\": {\
                        \"description\": \"This is test info\"\
                    }\
                }\
            }\
        },\
        \"\\/static_test_verb\": {\
            \"get\": {\
                \"responses\": {\
                    \"200\": {\
                        \"description\": \"static_test_verb\"\
                    }\
                }\
            }\
        }\
    }\
}"

struct inapis {
	struct afb_binding_v4 desc;
	struct afb_api_v4 *api;
	int init;
	const char * expected_desc;
};

struct inapis inapis[] = {
	{
	.desc = {
		.api = "ezra",
		.provide_class = "e",
		.require_class = "c",
		.require_api = "armel",
		.info = "Info"
		},
	.expected_desc = "{ \"openapi\": \"3.0.0\", \"info\": { \"version\": \"0.0.0\", \"description\": \"Info\", \"title\": \"ezra\" }, \"paths\": { } }"
	},
	{ 
	.desc = {
		.api = "clara",
		.provide_class = "c",
		.require_class = "a",
		.info = "{\"testInfo\" : \"this is a test\"}"
		},
	.expected_desc = "{ \"openapi\": \"3.0.0\", \"info\": { \"version\": \"0.0.0\", \"testInfo\": \"this is a test\", \"title\": \"clara\" }, \"paths\": { } }"
	},
	{
	.desc = {
		.api = "amelie",
		.provide_class = "a",
		.require_api = "albert armel",
		.info = "\"testInfo\" : {\"bad\" : \"json\", \"function\" : \"test subapi\"}"
		},
	.expected_desc = "{ \"openapi\": \"3.0.0\", \"info\": { \"version\": \"0.0.0\", \"description\": \"testInfo\", \"title\": \"amelie\" }, \"paths\": { } }"
	},
	{
	.desc = {
		.api = "chloe",
		.provide_class = "c a"},
	.expected_desc = "{ \"openapi\": \"3.0.0\", \"info\": { \"version\": \"0.0.0\", \"title\": \"chloe\" }, \"paths\": { } }"
	},
	{
	.desc = {
		.api = "albert",
		.provide_class = "a"
	},
	.expected_desc = NULL
	},
	{
	.desc = {
		.api = "armel",
		.provide_class = "a",
		.require_api = "albert"
	},
	.expected_desc = NULL
	},
	{ .desc = { .api = NULL }}
};

int mainctl_test_cb(afb_api_x4_t api_t, afb_ctlid_t ctlid, afb_ctlarg_t ctlarg, void *userdata){
	int rc;
	struct inapis *desc = userdata;

	struct afb_api_v4 * api = (struct afb_api_v4 *)api_t;
	
	printf("default preinit of %s\n", afb_api_v4_name(api));

	ck_assert_ptr_nonnull(api);
	ck_assert_ptr_nonnull(desc);
	ck_assert_ptr_nonnull(afb_api_v4_name(api));
	fprintf(stderr, "ctlid = %d\n", ctlid);
	ck_assert(ctlid = afb_ctlid_Pre_Init);
	ck_assert_str_eq(afb_api_v4_name(api), desc->desc.api);
	ck_assert_ptr_null(desc->api);
	ck_assert_int_eq(desc->init, 0);

	rc = afb_api_v4_set_binding_fields(api, &desc->desc);
	ck_assert_int_eq(rc, 0);

	afb_api_v4_set_userdata(api, desc);
	desc->api = api;

	return 0;
}


int out_init(struct afb_api_x4 *api)
{
	return 0;
}

struct afb_apiset *apiset;
char out_apiname[] = "out";
struct afb_api_v4 *out_v4;
struct afb_api_v4 *out_api;

int out_preinit(struct afb_api_v4 *api, void *closure)
{
	int i;
	int rc;
	struct afb_api_v4 *napi;

	struct json_object * json, * expected_json;

	ck_assert_ptr_nonnull(api);
	ck_assert_ptr_eq(closure, out_apiname);
	ck_assert_ptr_nonnull(afb_api_v4_get_api_common(api));
	ck_assert_ptr_null(afb_api_v4_get_userdata(api));
	ck_assert_str_eq(afb_api_v4_name(api), out_apiname);
	ck_assert_str_eq(afb_api_v4_info(api), API_INFO);
	out_api = api;

	for (i = 0 ; inapis[i].desc.api ; i++) {
		ck_assert_ptr_null(inapis[i].api);
		rc = afb_api_v4_new_api_hookable(
				api,
				&napi,
				inapis[i].desc.api,
				inapis[i].desc.info,
				0,
				mainctl_test_cb,
				&inapis[i]);
		ck_assert_int_eq(rc, 0);
		ck_assert_ptr_nonnull(napi);
		ck_assert_ptr_nonnull(inapis[i].api);
		ck_assert_ptr_eq(inapis[i].api, napi);
		
		if(inapis[i].expected_desc){
			json = afb_api_v4_make_description_openAPIv3(napi);
			fprintf(stderr, "api description :\n%s\n", json_object_to_json_string(json));
			expected_json = json_tokener_parse(inapis[i].expected_desc);
			ck_assert_int_eq(rc, 0);
			fprintf(stderr, "expected :\n%s\n", json_object_to_json_string(expected_json));

			rc = wrap_json_cmp(json, expected_json);
			ck_assert_int_eq(rc, 0);
		}
	}
	return 0;
}

START_TEST (test_init)
{
	int rc;

	fprintf(stderr, "\n******** test_init ********\n");

	verbosity_set(-1);
	apiset = afb_apiset_create("test-apiv4", 1);
	ck_assert_ptr_nonnull(apiset);

	rc = afb_api_v4_create(
			&out_v4,
			apiset,
			apiset,
			out_apiname,
			Afb_String_Copy,
			API_INFO,
			Afb_String_Const,
			0,
			out_preinit,
			out_apiname,
			NULL,
			Afb_String_Const);
	ck_assert_int_eq(rc, 0);
	ck_assert_ptr_nonnull(out_v4);
	ck_assert_ptr_nonnull(out_api);
	ck_assert_ptr_eq(out_v4, out_api);

	/* start all services */
	rc = afb_apiset_start_all_services(apiset);
	ck_assert_int_eq(rc, 0);

	rc = afb_api_v4_add_verb_hookable(out_v4, "verb", NULL, NULL, NULL, NULL, 0, 0);
	ck_assert_int_eq(rc, 0);

	rc = afb_api_v4_delete_api_hookable(out_v4);
	ck_assert_int_eq(rc, 0);

	afb_api_v4_seal_hookable(out_api);

	rc = afb_api_v4_delete_api_hookable(out_v4);
	ck_assert_int_eq(rc, X_EPERM);

	afb_api_v4_unref(out_v4);
}
END_TEST

int get_data_int(struct afb_data * data){
	const void * ptr;
	struct afb_data * item;
	ck_assert_int_eq(afb_data_convert_to(data, &afb_type_predefined_i32, &item), 0);
	afb_data_get_constant(item, &ptr, 0);
	return *(int*)ptr;
}

int test_api_preinit_cb(struct afb_api_v4 * api, void * closure){
	int * val = closure;
	*val = *val + 1;
	fprintf(stderr, "test_api_preinit_cb : test_val = %d\n", *val);
	return 0;
}

void test_verb_cb(const struct afb_req_v4 *req, unsigned nparams, const struct afb_data * const params[]){
	unsigned int i;
	//const void * ptr;
	int data_val[NBDATA];
	struct afb_data * reply[NBDATA]; 

	fprintf(stderr, "test_verb_cb : nparams = %d\n", nparams);
	if(nparams == 0){
		afb_req_v4_reply_hookable((struct afb_req_v4 *)req, 0, 0, NULL);
		return;
	}
	for(i=0; i<nparams; i++){
			data_val[i] = get_data_int((struct afb_data *)params[i]) + 1;
			ck_assert_int_eq(0,
				afb_data_create_copy(&reply[i], &afb_type_predefined_i32, &data_val[i], sizeof data_val[i])
			);
	}
	afb_req_v4_reply_hookable((struct afb_req_v4 *)req, 0, nparams, (struct afb_data * const *)reply);
}

void test_static_verb_cb(afb_req_x4_t req, unsigned nparams,	afb_data_x4_t const params[]){
	test_verb_cb((const struct afb_req_v4 *)req, nparams, (const struct afb_data * const *)params);
}

void data_cb(void * arg){
	*(int *)arg = *(int *)arg + 1;
	fprintf(stderr, "test_cb : arg = %d\n", *(int*)arg);
}

void test_cb(int sig, void * arg){
	if(!sig)
		data_cb(arg);
	else
		fprintf(stderr, "test_cb reseved sig %d\n", sig);
}

void test_reply_cb (
		void *closure,
		int status,
		unsigned nreplies,
		struct afb_data * const replies[],
		struct afb_api_v4 *api)
{
	int * val = closure;
	if (status == 0)
		*val = *val+1;
	fprintf(stderr, "test_reply_cb : status = %d, nreplies = %d, closure = %d\n", status, nreplies, *val);
}

int observation;

void observe(int loglevel, const char *file, int line, const char *function, const char *fmt, va_list args){
	fprintf(stderr, "made an observation ! : loglevel = %d, file = %s, line = %d, function = %s, fmt = %s\n", loglevel, file, line, function, fmt);
	ck_assert_int_eq(4, loglevel);
	ck_assert_int_eq(666, line);
	ck_assert_str_eq("test", file);
	ck_assert_str_eq("this_is_a_test", function);

	observation++;
}

void test_api_handle_cb(void * closure, const char * ev_name, unsigned nparams, struct afb_data * const params[], struct afb_api_v4 * api){
	fprintf(stderr, "test_api_handle_cb : closure = %d, event_name = %s, nparams = %d\n", *(int*)closure, ev_name, nparams);
}
	

START_TEST (test_calls)
{
	int rc, i;
	int test_val = 0;
	int test_data_val[NBDATA];
	struct afb_api_v4 * api;
	struct json_object * json;
	struct json_object * expected_json;

	struct afb_evt * evt;

	struct afb_verb_v4 static_verbs[] = {
		{.verb = "info", .callback = test_static_verb_cb, .vcbdata = &test_val, .info = VERB_INFO},
		{.verb = "static_test_verb", .callback = test_static_verb_cb, .vcbdata = &test_val},
		{.verb = NULL}
	};
	
	struct afb_data * data[NBDATA];
	struct afb_data * reply[NBDATA];
	unsigned nreplies;

	struct afb_api_item sa;

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

	sa.itf = &api_itf_null;
	sa.closure = NULL;
	sa.group = NULL;

	fprintf(stderr, "\n******** test_calls ********\n");

#if WITH_AFB_HOOK
    struct afb_hook_api * hook_api;
    hook_api = afb_hook_create_api("*", afb_hook_flags_api_all, NULL, NULL);
    ck_assert_ptr_ne(NULL, hook_api);
#endif

	verbosity_set(-1);
	apiset = afb_apiset_create("test-apiv4", 1);
	ck_assert_ptr_nonnull(apiset);

	rc = afb_api_v4_create(
			&api,
			apiset,
			apiset,
			API_NAME,
			Afb_String_Copy,
			API_INFO,
			Afb_String_Const,
			0,
			test_api_preinit_cb,
			&test_val,
			NULL,
			Afb_String_Const);
	ck_assert_int_eq(rc, 0);
	ck_assert_ptr_nonnull(api);
	
	/******** verbs : add/set/dell/describe ********/
	fprintf(stderr, "\n******** verbs : add/set/dell/describe ********\n");
	
	rc = afb_api_v4_add_verb_hookable(api, "verb", NULL, test_verb_cb, &test_val, NULL, 0, 0);
	ck_assert_int_eq(rc, 0);

	// theck that overwriting works when setting glob to 1
	rc = afb_api_v4_add_verb(api, "verb", NULL, test_verb_cb, &test_val, NULL, 0, 1);
	ck_assert_int_eq(rc, 0);

	// check that overwriting returns an error
	rc = afb_api_v4_add_verb(api, "verb", "This is a dynamic verb for tests purpose" , test_verb_cb, &test_val, NULL, 0, 1);
	ck_assert_int_eq(rc, X_EEXIST);

	rc = afb_api_v4_set_verbs_hookable(api, static_verbs);
	ck_assert_int_eq(rc, 0);

	json = afb_api_v4_make_description_openAPIv3(api);
	fprintf(stderr, "api description :\n%s\n", json_object_to_json_string(json));

	expected_json = json_tokener_parse(EXPECTED_API_DESCRIPTION);
	ck_assert_int_eq(rc, 0);
	fprintf(stderr, "expected :\n%s\n", json_object_to_json_string(expected_json));

	rc = wrap_json_cmp(json, expected_json);
	ck_assert_int_eq(rc, 0);

	rc = afb_api_v4_add_verb(api, "transient", NULL, test_verb_cb, &test_val, NULL, 0, 1);
	ck_assert_int_eq(rc, 0);

	rc = afb_api_v4_del_verb_hookable(api, "transient", NULL);
	ck_assert_int_eq(rc, 0);

	// check that deleeting a static verb returns an error
	rc = afb_api_v4_del_verb_hookable(api, "static_test_verb", NULL);
	ck_assert_int_eq(rc, X_ENOENT);

	// check that after being sealed the verb or the api can't be delet
	afb_api_v4_seal(api);

	rc = afb_api_v4_del_verb_hookable(api, "verb", NULL);
	ck_assert_int_eq(rc, X_EPERM);

	rc = afb_api_v4_delete_api_hookable(api);
	ck_assert_int_eq(rc, X_EPERM);

	/******** settings ********/
	fprintf(stderr, "\n******** settings ********\n");

	json = NULL;
	json = afb_api_v4_settings_hookable(api);
	fprintf(stderr, "comapi->setting = %s\n", json_object_to_json_string(json));
	ck_assert_ptr_nonnull(json);

	/* start all services */
	rc = afb_apiset_start_all_services(apiset);
	ck_assert_int_eq(rc, 0);

	for(i=0; i<NBDATA; i++){
		test_data_val[i] = i;
		rc = afb_data_create_raw(&data[i], &afb_type_predefined_i32, &test_data_val[i], 0, data_cb, &test_val);
	}
	test_val = 0;

	/******** call ********/
	fprintf(stderr, "\n******** call ********\n");

	afb_api_v4_call_hookable(
		api,
		afb_api_v4_name(api),
		"verb",
		0,
		NULL,
		test_reply_cb,
		&test_val
	);
	
	RUNJOB;
	ck_assert_int_eq(test_val, 1);
	
	/******** call_sync ********/
	fprintf(stderr, "\n******** call_sync ********\n");

	nreplies = NBDATA;

	rc = afb_api_v4_call_sync_hookable(
		api,
		afb_api_v4_name(api),
		"static_test_verb",
		NBDATA,
		data,
		0,
		&nreplies,
		reply
	);

	RUNJOB;

	ck_assert_int_eq(rc, 0);
	ck_assert_int_eq(nreplies, NBDATA);

	for(i=0; (unsigned)i<nreplies; i++){
		rc = get_data_int(reply[i]);
		fprintf(stderr, "Reply[%d] = %d\n", i, rc);
		ck_assert_int_eq(rc, i+1);
	}

	/******** vverbose ********/
	fprintf(stderr, "\n******** vverbose ********\n");
 
	observation = 0;
	va_list test_va_list;
	verbose_observer = observe;
	afb_api_v4_vverbose_hookable(api, 4, "test", 666, "this_is_a_test", "test message", test_va_list);
	fprintf(stderr, "vverbose test message observerd %d time\n", observation);
	ck_assert_int_eq(1, observation);
	verbose_observer = NULL;

	/******** job ********/
	fprintf(stderr, "\n******** job ********\n");
	test_val = 0;
	rc = afb_api_v4_post_job_hookable(api, 0, 1, test_cb, &test_val, NULL);
	ck_assert_int_gt(rc, 0);

	RUNJOB;

	fprintf(stderr, "-> testval = %d\n", test_val);
	ck_assert_int_eq(1, test_val);

	/******** alias ********/
	fprintf(stderr, "\n******** alias ********\n");
	fprintf(stderr, "Create the alias '%s' to the api '%s'\n", API_ALIAS_NAME, API_NAME);
	rc = afb_apiset_add(apiset, "api_set", sa);
	ck_assert_int_eq(rc, 0);
	rc = afb_api_v4_add_alias_hookable(api, NULL, API_ALIAS_NAME);
	ck_assert_int_eq(0, rc);

	/******** add_event_handler ********/
	fprintf(stderr, "\n******** add_event_handler ********\n");
	test_val = 0;
	rc = afb_api_v4_event_handler_add_hookable(api, "plop!", test_api_handle_cb, &test_val);
	ck_assert_int_eq(rc, 0);

	rc = afb_api_v4_new_event_hookable(api, "plop!", &evt);
	ck_assert_int_eq(rc, 0);

	// rc = afb_api_v4_subscribe(api, evt);
	// ck_assert_int_eq(rc, 0);

	/******** event_broadcast ********/
	fprintf(stderr, "\n******** event_broadcast ********\n");

	// preparing params
	for(i=0; i<NBDATA; i++)
		rc = afb_data_create_raw(&data[i], NULL, NULL, 0, data_cb, &test_val);

	rc = afb_api_v4_event_broadcast_hookable(api, "plop!", NBDATA, data);
	fprintf(stderr, "-> rc = %d\n", rc);
	ck_assert_int_eq(rc, 0);

	RUNJOB;

	fprintf(stderr, "-> test_val = %d\n", test_val);
	ck_assert_int_eq(test_val, 3);

	/******** del_event_handler ********/
	fprintf(stderr, "\n******** del_event_handler ********\n");
	rc = afb_api_v4_event_handler_del_hookable(api, "plop!", NULL);
	ck_assert_int_eq(rc, 0);

	/******** require api ********/
	fprintf(stderr, "\n******** require api ********\n");

	rc = afb_api_v4_require_api_hookable(api, API_NAME, 1);
	fprintf(stderr, "-> rc = %d\n", rc);
	ck_assert_int_eq(0, rc);

	/******** class provide/require ********/
	fprintf(stderr, "\n******** class provide/require ********\n");

	fprintf(stderr, "provide the class '%s'...\n", "class");
	rc = afb_api_v4_class_provide_hookable(api, "class");
	fprintf(stderr, "-> rc = %d\n", rc);
	ck_assert_int_eq(0, rc);

	fprintf(stderr, "require class '%s'...\n", "class");
	rc = afb_api_v4_class_require_hookable(api, "class");
	fprintf(stderr, "-> rc = %d\n", rc);
	ck_assert_int_eq(0, rc);

	afb_api_v4_unref(api);

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
	mksuite("apiv4");
		addtcase("apiv4");
			addtest(test_init);
			addtest(test_calls);
	return !!srun();
}
