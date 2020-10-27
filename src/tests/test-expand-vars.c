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

/*********************************************************************/

#include "utils/expand-vars.h"

/*********************************************************************/

START_TEST (check_expand)
{
	char *r;

	putenv("X=$Y:$Y");
	putenv("Y=$A:$(unnom):tres:$long");
	putenv("A=a");
	putenv("unnom=hum:${long}");
	putenv("long=rien:$rien:rien");
	putenv("TEST=debut:$X:fin");
	unsetenv("rien");

	// check expansion
	r = expand_vars_env_only("$TEST", 0);
	ck_assert_ptr_nonnull(r);
	printf("%s\n",r);
	ck_assert_str_eq(r, "debut:a:hum:rien::rien:tres:rien::rien:a:hum:rien::rien:tres:rien::rien:fin");
	free(r);

	// check robust to infinite expansion
	putenv("V=xxx");
	putenv("Z=$Z:$V:$Z");
	r = expand_vars_env_only("$Z", 0);
	ck_assert_ptr_null(r);
	r = expand_vars_env_only("$Z", 1);
	ck_assert_ptr_nonnull(r);
	ck_assert_str_eq(r, "$Z");
	free(r);

	// check robust to null
	r = expand_vars_env_only(0, 0);
	ck_assert_ptr_null(r);
	free(r);
}
END_TEST

/*********************************************************************/

char **before;
char **after;

void mc(const char *in, const char *out)
{
	char *r = expand_vars(in, 1, before, after);
	printf("mc got %s\n", r);
	ck_assert_ptr_nonnull(r);
	ck_assert_str_eq(r, out);
	free(r);
}

START_TEST (check_order)
{
	char *x_before[] = { "X=before", "B=before", 0 };
	char *x_after[] = { "X=after", "A=after", "Z=last", 0 };

	putenv("X=env");
	putenv("A=env");
	putenv("B=env");

	before = 0;
	after = 0;
	mc("$A $B $X $Z", "env env env ");

	before = x_before;
	after = 0;
	mc("$A $B $X $Z", "env before before ");

	before = 0;
	after = x_after;
	mc("$A $B $X $Z", "env env env last");

	before = x_before;
	after = x_after;
	mc("$A $B $X $Z", "env before before last");
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
	mksuite("expand-vars");
		addtcase("expand-vars");
			addtest(check_expand);
			addtest(check_order);
	return !!srun();
}