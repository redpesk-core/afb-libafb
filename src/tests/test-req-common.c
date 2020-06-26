#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <check.h>

#if !defined(ck_assert_ptr_null)
# define ck_assert_ptr_null(X)      ck_assert_ptr_eq(X, NULL)
# define ck_assert_ptr_nonnull(X)   ck_assert_ptr_ne(X, NULL)
#endif

#include "libafb-config.h"

#include <json-c/json.h>
#include <afb/afb-arg.h>

#include "core/afb-apiset.h"
#include "core/afb-req-common.h"
#include "sys/verbose.h"

/*********************************************************************/

struct afb_req_common *test_json_req;
struct json_object *test_json_$result;
struct json_object *test_json(struct afb_req_common *req)
{
	test_json_req = req;
	return test_json_$result;
}

struct afb_req_common *test_get_req;
const char *test_get_name;
struct afb_arg test_get(struct afb_req_common *req, const char *name)
{
	struct afb_arg arg;
	test_get_req = req;
	test_get_name = name;
	arg.name = name;
	arg.value = NULL;
	arg.path = NULL;
	return arg;
}

struct afb_req_common *test_reply_req;
const struct afb_req_reply *test_reply_reply;
void test_reply(struct afb_req_common *req, const struct afb_req_reply *reply)
{
	test_reply_req = req;
	test_reply_reply = reply;
}

struct afb_req_common *test_unref_req;
void test_unref(struct afb_req_common *req)
{
	test_unref_req = req;
	afb_req_common_cleanup(req);
}

struct afb_req_common *test_subscribe_req;
struct afb_event_x2 *test_subscribe_event;
int test_subscribe(struct afb_req_common *req, struct afb_event_x2 *event)
{
	test_subscribe_req = req;
	test_subscribe_event = event;
	return 0;
}

struct afb_req_common *test_unsubscribe_req;
struct afb_event_x2 *test_unsubscribe_event;
int test_unsubscribe(struct afb_req_common *req, struct afb_event_x2 *event)
{
	test_unsubscribe_req = req;
	test_unsubscribe_event = event;
	return 0;
}

struct afb_req_common_query_itf test_queryitf =
{
	.json = test_json,
	.get = test_get,
	.reply = test_reply,
	.unref = test_unref,
	.subscribe = test_subscribe,
	.unsubscribe = test_unsubscribe
};

struct afb_req_common comreq;
const char apiname[] = "api";
const char verbname[] = "verb";

START_TEST (test)
{
	struct afb_req_common *req = &comreq;

	afb_req_common_init(req, &test_queryitf, apiname, verbname);
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
	mksuite("req-common");
		addtcase("req-common");
			addtest(test);
	return !!srun();
}
