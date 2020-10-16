/*
 Copyright (C) 2015-2020 IoT.bzh Company

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

#include "core/afb-apiset.h"
#include "core/afb-api-common.h"
#include "sys/verbose.h"

/*********************************************************************/

struct afb_apiset *callset;
struct afb_apiset *declset;
struct afb_api_common capi;
char name[] = "name";
char info[] = "info";
char path[] = "path";

/*********************************************************************/

START_TEST (test_init)
{
	struct afb_api_common *comapi = &capi;
	struct afb_session *s;

	verbosity_set(-1);
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

START_TEST (test_functionnal)
{
	/* remains to be done */
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
	mksuite("api-common");
		addtcase("api-common");
			addtest(test_init);
			addtest(test_functionnal);
	return !!srun();
}
