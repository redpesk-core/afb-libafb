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

#define AFB_BINDING_VERSION 0
#include <afb/afb-binding.h>

#include "core/afb-apiset.h"
#include "core/afb-api-v3.h"
#include "core/afb-string-mode.h"

struct inapis {
	struct afb_binding_v3 desc;
	struct afb_api_x3 *api;
	int init;
};

struct inapis inapis[] = {
	{ .desc = {
		.api = "ezra",
		.provide_class = "e",
		.require_class = "c",
		.require_api = "armel"
	}},
	{ .desc = {
		.api = "clara",
		.provide_class = "c",
		.require_class = "a"
	}},
	{ .desc = {
		.api = "amelie",
		.provide_class = "a",
		.require_api = "albert armel"
	}},
	{ .desc = {
		.api = "chloe",
		.provide_class = "c a"
	}},
	{ .desc = {
		.api = "albert",
		.provide_class = "a"
	}},
	{ .desc = {
		.api = "armel",
		.provide_class = "a",
		.require_api = "albert"
	}},
	{ .desc = { .api = NULL }}
};

int last_in_init;

int in_init(struct afb_api_x3 *api)
{
	struct inapis *desc = api->userdata;

	ck_assert_str_eq(api->apiname, desc->desc.api);
	ck_assert_int_eq(desc->init, 0);

	desc->init = ++last_in_init;
	printf("init %d of %s\n", desc->init, api->apiname);
	return 0;
}

int in_preinit(void *closure, struct afb_api_x3 *apix3)
{
	int rc;
	struct inapis *desc = closure;

	printf("default preinit of %s\n", apix3->apiname);

	ck_assert_ptr_nonnull(apix3);
	ck_assert_ptr_nonnull(desc);
	ck_assert_ptr_nonnull(apix3->apiname);
	ck_assert_ptr_null(apix3->userdata);
	ck_assert_str_eq(apix3->apiname, desc->desc.api);
	ck_assert_ptr_null(desc->api);
	ck_assert_int_eq(desc->init, 0);

/*
	rc = afb_api_v3_set_binding_fields(api, &desc->desc);
	ck_assert_int_eq(rc, 0);
*/

	apix3->userdata = desc;
	desc->api = apix3;

	if (desc->desc.preinit)
		desc->desc.preinit(apix3);

	if (!desc->desc.init) {
		rc =  afb_api_x3_on_init(apix3, in_init);
		ck_assert_int_eq(rc, 0);
	}

	return 0;
}

int out_init(struct afb_api_x3 *api)
{
	return 0;
}

struct afb_apiset *apiset;
char out_apiname[] = "out";
struct afb_api_v3 *out_v3;
struct afb_api_v3 *out_api;

int out_preinit(void *closure, struct afb_api_v3 *api)
{
	int i;
	int rc;
	struct afb_api_x3 *napi;
	struct afb_api_x3 *apix3 = afb_api_v3_get_api_x3(api);

	ck_assert_ptr_nonnull(api);
	ck_assert_ptr_eq(closure, out_apiname);
	ck_assert_ptr_null(apix3->userdata);
	ck_assert_str_eq(apix3->apiname, out_apiname);
	out_api = api;

	for (i = 0 ; inapis[i].desc.api ; i++) {
		ck_assert_ptr_null(inapis[i].api);
		napi = afb_api_x3_new_api(
				apix3,
				inapis[i].desc.api,
				NULL,
				0,
				in_preinit,
				&inapis[i]);
		ck_assert_ptr_nonnull(napi);
		ck_assert_ptr_nonnull(inapis[i].api);
		ck_assert_ptr_eq(inapis[i].api, napi);
	}

	rc = afb_api_x3_on_init(apix3, out_init);
	ck_assert_int_eq(rc, 0);

	return 0;
}

START_TEST (test)
{
	int rc;

	rp_set_logmask(-1);
	apiset = afb_apiset_create("test-apiv3", 1);
	ck_assert_ptr_nonnull(apiset);

	rc = afb_api_v3_create(
			&out_v3,
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
	ck_assert_ptr_nonnull(out_v3);
	ck_assert_ptr_nonnull(out_api);
	ck_assert_ptr_eq(out_v3, out_api);

	/* start all services */
	rc = afb_apiset_start_all_services(apiset);
	ck_assert_int_eq(rc, 0);
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
	mksuite("apiv3");
		addtcase("apiv3");
			addtest(test);
	return !!srun();
}
