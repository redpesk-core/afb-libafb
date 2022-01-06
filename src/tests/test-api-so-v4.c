/*
 Copyright (C) 2015-2022 IoT.bzh Company

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
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include <check.h>

#include "sys/x-dynlib.h"
#include "apis/afb-api-so-v4.h"
#include "core/afb-apiset.h"
#include "core/afb-sig-monitor.h"

#define BUG_OFFSET 11
#define PATH_BUF_SIZE 200
#define TEST_LIB_PATH "libhello.so"

typedef struct{
	int nb;
	int expected_result;
	struct afb_apiset * declare_set;
	struct afb_apiset * call_set;
} bgTest;

/*********************************************************************/

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
		memmove(buffer + lenp, buffer, (size_t)len + 1);
		memcpy(buffer, *pp, (size_t)lenp);
		pp++;
		len += lenp;
		rc = access(buffer, F_OK);
	}
	if (rc == 0)
		fprintf(stderr, "FOUND %s for %s/%d\n", buffer, base, ival);
	else
		fprintf(stderr, "Cna't find file %s/%d\n", base, ival);
	return rc;
}

/*********************************************************************/

void bug_test(int sig, void * arg){

#if WITH_DYNAMIC_BINDING

	bgTest * bgArg = arg;
	char test_buglib_path[PATH_BUF_SIZE];
	x_dynlib_t dynlib;
	int r;

	if(sig == 0)
	{
		fprintf(stderr, "\n************* test on bug%d *************\n", bgArg->nb);

		getpath(test_buglib_path, "libbug%d.so", bgArg->nb);

		// load the binding dynamic library
		r = x_dynlib_open (test_buglib_path, &dynlib, 0, 0);
		ck_assert_int_eq(r, 0);

		// try to add binding api
		r = afb_api_so_v4_add(test_buglib_path, &dynlib, bgArg->declare_set, bgArg->call_set);
		fprintf(stderr,"tset bug%d done with result %d and sig %d\n", bgArg->nb, r, sig);

		// check that adding the bug api returned the correct error code
		ck_assert_int_eq(r, bgArg->expected_result);
	}
#endif
}

/*********************************************************************/
/* Test adding a minimal binding api */
START_TEST (test)
{
#if WITH_DYNAMIC_BINDING

	int r, i;
	const char ** apinames;
	x_dynlib_t dynlib;
	struct afb_apiset * declare_set, * call_set;
	char test_path[PATH_BUF_SIZE];

	// load the binding dynamic library
	getpath(test_path, TEST_LIB_PATH, 0);
	r = x_dynlib_open (test_path, &dynlib, 0, 0);
	ck_assert_int_eq(r, 0);

	declare_set = afb_apiset_create("toto", 1);
	call_set = afb_apiset_create("tata", 1);

	// add binding api
	r = afb_api_so_v4_add(test_path, &dynlib, declare_set, call_set);
	ck_assert_int_eq(r, 1);

	// check that the api apears in the loaded apis
	i =	r = 0;
	apinames = afb_apiset_get_names(declare_set, 0, 1);
	while (apinames[i])
	{
		fprintf(stderr, "api name %d : %s\n", i, apinames[i]);
		if(strcmp(apinames[i], "hello") == 0) r = 1;
		i++;
	}
	ck_assert_int_eq(r, 1);
#endif
}
END_TEST

/*********************************************************************/
/* Test a set of known bugs */
START_TEST (dirty_test)
{
#if WITH_DYNAMIC_BINDING

	int i, n;
	bgTest bugArg;
	const int expected_result[] = {
		/*bug11*/	-14,
		/*bug12*/	0,
		/*bug13*/	-22,
		/*bug14*/	-22,
		/*bug15*/	-22,
		/*bug16*/	1,
		/*bug17*/	-11,
		/*bug18*/	-11,
		/*bug19*/	-14,
		/*bug20*/	-11,
		/*bug21*/	-14,
		/*bug22*/	-22,
	};

	// activate signal monitoring
	ck_assert_int_eq(afb_sig_monitor_init(1), 0);

	bugArg.declare_set = afb_apiset_create("toto", 1);
	bugArg.call_set = afb_apiset_create("tata", 1);

	// Test bugs one by one with sig monitor to avoid test to break on an error
	n = (int)(sizeof expected_result / sizeof *expected_result);
	for(i = 0 ; i < n ; i++) {
		bugArg.nb = BUG_OFFSET + i;
		bugArg.expected_result = expected_result[i];
		afb_sig_monitor_run(0, bug_test, &bugArg);
	}
#endif
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
	mksuite("api-so-v4");
		addtcase("api-so-v4");
			addtest(test);
			addtest(dirty_test);
	return !!srun();
}
