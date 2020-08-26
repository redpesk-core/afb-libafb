#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <check.h>

#include "libafb-config.h"

#include "sys/x-dynlib.h"
#include "apis/afb-api-so-v4.h"
#include "core/afb-apiset.h"
#include "core/afb-sig-monitor.h"

#define BUG_OFFSET 11
#define PATH_BUF_SIZE 100
#define TEST_LIB_PATH "src/tests/libhello.so"

typedef struct{
	int nb;
	int expected_result;
	struct afb_apiset * declare_set;
	struct afb_apiset * call_set;
} bgTest;

void bug_test(int sig, void * arg){

	bgTest * bgArg = arg;
	char test_buglib_path[PATH_BUF_SIZE];
	x_dynlib_t dynlib;
	int r;

	if(sig == 0)
	{
		fprintf(stderr, "\n************* test on bug%d *************\n", bgArg->nb);
		
		ck_assert_int_ge(snprintf(test_buglib_path, PATH_BUF_SIZE, "src/tests/bugs/libbug%d.so", bgArg->nb), 0);
		
		// load the binding dynamic library
		r = x_dynlib_open (test_buglib_path, &dynlib, 0, 0);
		ck_assert_int_eq(r, 0);
		
		// try to add binding api
		r = afb_api_so_v4_add(test_buglib_path, &dynlib, bgArg->declare_set, bgArg->call_set);
		fprintf(stderr,"tset bug%d done with result %d and sig %d\n", bgArg->nb, r, sig);

		// check that adding the bug api returned the correct error code
		ck_assert_int_eq(r, bgArg->expected_result);
	}
}

/*********************************************************************/
/* Test adding a minimal binding api */
START_TEST (test)
{
	int r, i;
	const char ** apinames;
	x_dynlib_t dynlib;
    struct afb_apiset * declare_set, * call_set;

	// load the binding dynamic library
	r = x_dynlib_open (TEST_LIB_PATH, &dynlib, 0, 0);
	ck_assert_int_eq(r, 0);

	declare_set = afb_apiset_create("toto", 1);
	call_set = afb_apiset_create("tata", 1);

	// add binding api
    r = afb_api_so_v4_add("", &dynlib, declare_set, call_set);
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
}
END_TEST

/*********************************************************************/
/* Test a set of known bugs */
START_TEST (dirty_test)
{
	bgTest bugArg;
	const int suposed_result[] = {
		/*bug11*/	-14,
		/*bug12*/	0,
		/*bug13*/	-22,
		/*bug14*/	-22,
		/*bug15*/	-22,
		/*bug16*/	-22,
		/*bug17*/	-1,
		/*bug18*/	-1,
		/*bug19*/	-14,
		/*bug20*/	-1,
		/*bug21*/	-14,
	};

	// activate signal monitoring
	ck_assert_int_eq(afb_sig_monitor_init(1), 0);

	bugArg.declare_set = afb_apiset_create("toto", 1);
	bugArg.call_set = afb_apiset_create("tata", 1);

	// Test bugs one by one with sig monitor to avoid test to break on an error
	for(bugArg.nb=BUG_OFFSET; bugArg.nb<=21; bugArg.nb++){
		bugArg.expected_result = suposed_result[bugArg.nb-BUG_OFFSET];
		afb_sig_monitor_run(0, bug_test, &bugArg);
	}
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
	mksuite("api-so-v4");
		addtcase("api-so-v4");
			addtest(test);
			addtest(dirty_test);
	return !!srun();
}
