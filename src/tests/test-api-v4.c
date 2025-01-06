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

#include <check.h>

#if !defined(ck_assert_ptr_null)
# define ck_assert_ptr_null(X)      ck_assert_ptr_eq(X, NULL)
# define ck_assert_ptr_nonnull(X)   ck_assert_ptr_ne(X, NULL)
#endif

#include <rp-utils/rp-verbose.h>

#include "core/afb-apiset.h"
#include "core/afb-api-v4.h"
#include "core/afb-string-mode.h"


struct afb_apiset *apiset;
char out_apiname[] = "out";
struct afb_api_v4 *out_v4;
struct afb_api_v4 *out_api;

int out_preinit(struct afb_api_v4 *api, void *closure)
{
	ck_assert_ptr_nonnull(api);
	ck_assert_ptr_eq(closure, out_apiname);
	ck_assert_ptr_null(afb_api_v4_get_userdata(api));
	ck_assert_str_eq(afb_api_v4_name(api), out_apiname);
	out_api = api;

	return 0;
}

void cbvt(struct afb_req_v4 *req, unsigned nparams, struct afb_data * const params[])
{
}

START_TEST (test)
{
	int rc, i, j, k, n;
	char buffer[30];
	const struct afb_verb_v4 *verb;
	void *ptr;

	rp_set_logmask(-1);
	apiset = afb_apiset_create("test-apiv4", 1);
	ck_assert_ptr_nonnull(apiset);

	rc = afb_api_v4_create(
			&out_v4,
			apiset,
			apiset,
			out_apiname,
			Afb_String_Copy,
			NULL,
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

	/* creates 10000 verbs */
	n = 10000;
	for (i = 0 ; i < n ; i++) {
		snprintf(buffer,sizeof buffer,"proc%d",i+1);
		rc = afb_api_v4_add_verb(out_v4, buffer, NULL, cbvt, (void*)(intptr_t)i, NULL, 0, 0);
		ck_assert_int_eq(rc, 0);
	}

	/* random calls verbs */
	for (i = 0 ; i < n ; i++) {
		for (k = i, j = 0; k != 0; k >>= 1)
			j = (j << 1) | (k & 1);
		snprintf(buffer,sizeof buffer,"proc%d",j+1);
		verb = afb_api_v4_verb_matching(out_v4, buffer);
		if (j >= n)
			ck_assert_ptr_null(verb);
		else {
			ck_assert_ptr_nonnull(verb);
			ck_assert_str_eq(verb->verb, buffer);
			ck_assert_ptr_eq(verb->vcbdata, (void*)(intptr_t)j);
		}
	}

	/* remove odd verbs */
	for (i = 0 ; i < n ; i++) {
		if (i & 1) {
			snprintf(buffer,sizeof buffer,"proc%d",i+1);
			rc = afb_api_v4_del_verb(out_v4, buffer, &ptr);
			ck_assert_int_eq(rc, 0);
			ck_assert_ptr_eq(ptr, (void*)(intptr_t)i);
		}
	}

	/* random calls verbs */
	for (i = 0 ; i < n ; i++) {
		for (k = i, j = 0; k != 0; k >>= 1)
			j = (j << 1) | (k & 1);
		snprintf(buffer,sizeof buffer,"proc%d",j+1);
		verb = afb_api_v4_verb_matching(out_v4, buffer);
		if ((j & 1) || (j >= n))
			ck_assert_ptr_null(verb);
		else {
			ck_assert_ptr_nonnull(verb);
			ck_assert_str_eq(verb->verb, buffer);
			ck_assert_ptr_eq(verb->vcbdata, (void*)(intptr_t)j);
		}
	}
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
			addtest(test);
	return !!srun();
}
