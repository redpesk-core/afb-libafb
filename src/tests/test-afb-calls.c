#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <check.h>

#include <afb/afb-binding-v4.h>

#include "libafb-config.h"

#include "utils/wrap-json.h"
#include "apis/afb-api-so.h"
#include "core/afb-calls.h"
#include "core/afb-api-common.h"
#include "core/afb-apiset.h"
#include "core/afb-jobs.h"
#include "core/afb-data.h"
#include "core/afb-type.h"


#define PATH_BUF_SIZE 200
#define NBPARAMS 3

#if WITH_REQ_PROCESS_ASYNC
#define RUNJOB while(afb_jobs_get_pending_count()) { afb_jobs_run(afb_jobs_dequeue(0)); }
#else
#define RUNJOB (void)0
#endif

#define i2p(x)  ((void*)((intptr_t)(x)))
#define p2i(x)  ((int)((intptr_t)(x)))

/*********************************************************************/

int gval, dataClosureGval, verbDataGval ;

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
		memmove(buffer + lenp, buffer, len + 1);
		memcpy(buffer, *pp, lenp);
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

void show(struct afb_api_common * comapi){
    fprintf(stderr, "comapi.refcount = %d\n", comapi->refcount);
    fprintf(stderr, "comapi.state = %d\n", comapi->state);
    fprintf(stderr, "comapi.settings = %s\n", json_object_to_json_string(comapi->settings));
	fprintf(stderr, "comapi.path = %s\n", comapi->path);
	fprintf(stderr, "comapi.name = %s\n", comapi->name);
	fprintf(stderr, "comapi.sealed = %d\n", comapi->sealed);
}

void dataClosureCB(void *arg)
{
	dataClosureGval += p2i(arg);
	fprintf(stderr, "went through Data Closure with val %d\n", p2i(arg));
}

void testCB(void *closure1, void *closure2, void *closure3, int a, unsigned int b, struct afb_data *const * data){
    int i;
	fprintf(stderr, "testCB was called\n");
	for (i=0; i<b; i++) {
		verbDataGval += p2i(afb_data_const_pointer(data[i]));
	}
}

/*********************************************************************/
/* Test ... */
START_TEST (test)
{
	struct afb_data * params[NBPARAMS];
	struct afb_type * type1;
    struct afb_api_common comapi;
	struct afb_apiset * declare_set;
    struct afb_apiset * call_set;
    char name[] = "hello";
    char info[] = "Info";
    char path[PATH_BUF_SIZE];

	int rc, i, checksum;

	fprintf(stderr, "### test sync call");

	// preparing params
	type1 = afb_type_get("type1");
	if (!type1) {
		rc = afb_type_register(&type1, "type1", 0, 0, 0);
		fprintf(stderr, "afb_type_register returned : %d\n", rc);
		ck_assert_int_eq(rc, 0);
	}
	checksum = 0;
	for(i=1; i<=NBPARAMS;  i++){
		fprintf(stderr, "creating data with closure = %d\n", i);
		rc = afb_data_create_raw(&params[i-1], type1, i2p(i), 0, dataClosureCB, i2p(i));
		checksum += i;
	}

	declare_set = afb_apiset_create("toto", 1);
	call_set = afb_apiset_create("tata", 1);

	gval = 0;

	// load a binding
	getpath(path, "libhello.so", 0);
    afb_api_so_add_binding(path, call_set, call_set);

    // inti it's api
    afb_api_common_init(&comapi, declare_set, call_set, name, 0, info, 0, path, 0);

    // call one of it's verb
    afb_calls_call(&comapi, name, "hello", NBPARAMS, params, testCB, NULL, NULL, NULL);

	dataClosureGval = verbDataGval = 0;

    RUNJOB // if the system is not set to run jobs automaticaly run the jobs

    show(&comapi);

	fprintf(stderr, "dataClosureGval = %d\n", dataClosureGval);
	fprintf(stderr, "verbDataGval = %d\n", verbDataGval);

    // check that the testCB receved correctly the verb reply
	ck_assert_int_eq(verbDataGval, checksum);

	// check that data went through closure
	ck_assert_int_eq(dataClosureGval, checksum);

	ck_assert_int_eq(comapi.refcount, 1);
	ck_assert_int_eq(comapi.state, 0);
}
END_TEST

START_TEST (test_sync_call){

	struct afb_data * params[NBPARAMS];
	struct afb_data * replies[NBPARAMS];
	struct afb_type * type1;
    struct afb_api_common comapi;
	struct afb_apiset * declare_set;
    struct afb_apiset * call_set;
    char name[] = "hello";
    char info[] = "Info";
    char path[PATH_BUF_SIZE];
	unsigned nreplies;
	int rc, i, checksum;

	fprintf(stderr, "### test sync call");

	// preparing params
	type1 = afb_type_get("type1");
	if (!type1) {
		rc = afb_type_register(&type1, "type1", 0, 0, 0);
		fprintf(stderr, "afb_type_register returned : %d\n", rc);
		ck_assert_int_eq(rc, 0);
	}
	checksum = 0;
	for(i=1; i<=NBPARAMS;  i++){
		fprintf(stderr, "creating data with closure = %d\n", i);
		rc = afb_data_create_raw(&params[i-1], type1, i2p(i), 0, dataClosureCB, i2p(i));
		checksum += i;
	}

	declare_set = afb_apiset_create("toto", 1);
	call_set = afb_apiset_create("tata", 1);

	gval = 0;

	// load a binding
	getpath(path, "libhello.so", 0);
    afb_api_so_add_binding(path, call_set, call_set);

    // inti it's api
    afb_api_common_init(&comapi, declare_set, call_set, name, 0, info, 0, path, 0);

    // call one of it's verb
    afb_calls_call_sync(&comapi, name, "hello", NBPARAMS, params, 0, &nreplies, replies);

	dataClosureGval = verbDataGval = 0;

    show(&comapi);

	fprintf(stderr, "%d replies\n", nreplies);

	for (i=0; i<nreplies; i++) {
		ck_assert_int_eq(p2i(afb_data_const_pointer(replies[i])), i+1);
	}

	fprintf(stderr, "dataClosureGval = %d\n", dataClosureGval);

	// check that data went through closure
	ck_assert_int_eq(dataClosureGval, checksum);

	ck_assert_int_eq(comapi.refcount, 1);
	ck_assert_int_eq(comapi.state, 0);
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
	mksuite("afb-calls");
		addtcase("afb-calls");
			addtest(test);
			// addtest(test_sync_call);
	return !!srun();
}
