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

struct afb_req_common *test_reply_req;
int test_reply_status;
struct afb_dataset *test_reply_reply;
unsigned test_reply_nreplies;
struct afb_data * const *test_reply_replies;
void test_reply(struct afb_req_common *req, int status, unsigned nreplies, struct afb_data * const replies[])
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

struct afb_req_common comreq;
const char apiname[] = "api";
const char verbname[] = "verb";

START_TEST (test)
{
	struct afb_req_common *req = &comreq;

	afb_req_common_init(req, &test_queryitf, apiname, verbname, 0, NULL);
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
