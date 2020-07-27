#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

#include <check.h>

#include <afb/afb-binding-v4.h>

#include "core/afb-data.h"
#include "core/afb-type.h"
#include "sys/x-errno.h"

/*********************************************************************/

#define i2p(x)  ((void*)((intptr_t)(x)))
#define p2i(x)  ((int)((intptr_t)(x)))

int gmask;

void dor(void *p)
{
	gmask |= p2i(p);
}

void dand(void *p)
{
	gmask &= p2i(p);
}

static void cvdrop(void *p)
{
	gmask += p2i(p);
}

static int type1_to_type2(void *closure, struct afb_data *from, struct afb_type *type, struct afb_data **to)
{
	gmask = gmask * 100 + 12;
	ck_assert_int_eq(0, afb_data_create_raw(to, type, 0, 0, cvdrop, i2p(2)));
	return 0;
}

static int type2_to_type1(void *closure, struct afb_data *from, struct afb_type *type, struct afb_data **to)
{
	gmask = gmask * 100 + 21;
	ck_assert_int_eq(0, afb_data_create_raw(to, type, 0, 0, cvdrop, i2p(1)));
	return 0;
}

static int type2_to_type3(void *closure, struct afb_data *from, struct afb_type *type, struct afb_data **to)
{
	gmask = gmask * 100 + 23;
	ck_assert_int_eq(0, afb_data_create_raw(to, type, 0, 0, cvdrop, i2p(3)));
	return 0;
}

static int type3_to_type2(void *closure, struct afb_data *from, struct afb_type *type, struct afb_data **to)
{
	gmask = gmask * 100 + 32;
	ck_assert_int_eq(0, afb_data_create_raw(to, type, 0, 0, cvdrop, i2p(2)));
	return 0;
}

static struct afb_type *type1, *type2, *type3;

static
void
init_types()
{
	int rc;

	rc = afb_type_register(&type1, "type1", 0, 0, 0);
	ck_assert_int_eq(rc, 0);

	rc = afb_type_register(&type2, "type2", 0, 0, 0);
	ck_assert_int_eq(rc, 0);

	rc = afb_type_register(&type3, "type3", 0, 0, 0);
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(type1, type2, type1_to_type2, i2p(2));
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(type2, type1, type2_to_type1, i2p(2));
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(type3, type2, type3_to_type2, i2p(2));
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(type2, type3, type2_to_type3, i2p(2));
	ck_assert_int_eq(rc, 0);
}

/*********************************************************************/

START_TEST (check_data)
{
	int s, m;
	struct afb_data *data;
	char buffer[50];
	size_t size = 10;

	init_types();

	/* creation */
	m = 0x77777777;
	s = afb_data_create_raw(&data, type1, buffer, size, dor, i2p(m));
	ck_assert(s == 0);
	ck_assert(type1 == afb_data_type(data));
	ck_assert(buffer == afb_data_const_pointer(data));
	ck_assert(size == afb_data_size(data));
	gmask = 0;
	afb_data_unref(data);
	ck_assert(gmask == m);
}
END_TEST

/*********************************************************************/
/* checking computation of converters using hypercube geometry */

static void tconv(struct afb_type *from, struct afb_type *to, int sig)
{
	int rc;
	struct afb_data *dfrom, *dto;

	fprintf(stderr, "testing conversion from %s to %s\n", afb_type_name(from), afb_type_name(to));
	rc = afb_data_create_raw(&dfrom, from, 0, 0, cvdrop, i2p(sig - (sig % 10)));
	ck_assert_int_eq(rc, 0);
	gmask = 0;
	rc = afb_data_convert_to(dfrom, to, &dto);
	ck_assert_int_eq(rc, 0);
	ck_assert_int_eq(gmask, sig);
	gmask = 0;
	afb_data_unref(dfrom);
	afb_data_unref(dto);
	ck_assert_int_eq(gmask, sig);
}

START_TEST (check_convert)
{
	init_types();
	tconv(type1, type2, 12);
	tconv(type2, type1, 21);
	tconv(type2, type3, 23);
	tconv(type3, type2, 32);
}
END_TEST

START_TEST (check_cache)
{
	int rc;
	struct afb_data *data, *convertedData, *convertedDataBis;
	init_types();

	gmask = 0;

	// create data
	rc = afb_data_create_raw(&data, type1, 0, 0, cvdrop, i2p(10));

	// make a first convertion
	rc = afb_data_convert_to(data, type2, &convertedData);

	// check that the convertion went wel
	ck_assert_int_eq(rc, 0);
	ck_assert_int_eq(gmask, 12);

	gmask = 0;

	// Chechk that un referencing the data doesn't deleat it
	afb_data_unref(convertedData);
	ck_assert_int_eq(gmask, 0);

	// check that remaking the same convertion with a different variable
	// on the output doesn't generate a new conversation but just point
	// to the previously generated result meaning that the convertion
	// has correctly been stored in the cache
	rc = afb_data_convert_to(data, type2, &convertedDataBis);
	ck_assert_int_eq(rc, 0);
	ck_assert_int_eq(gmask, 0);
	ck_assert_ptr_eq(convertedData, convertedDataBis);

	// free the cache corresponding to data and check if unreferencing
	// data and it's convertion result activate the actual deletion of them
	afb_data_notify_changed(data);
	afb_data_unref(convertedData);
	ck_assert_int_eq(gmask, 2);
	gmask = 0;
	afb_data_unref(data);
	ck_assert_int_eq(gmask, 10);

}
END_TEST

/*********************************************************************/

static Suite *suite;
static TCase *tcase;

void mksuite(const char *name) { suite = suite_create(name); }
void addtcase(const char *name) { tcase = tcase_create(name); suite_add_tcase(suite, tcase); tcase_set_timeout(tcase, 120); }
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
	mksuite("afbdata");
		addtcase("afb-data");
			addtest(check_data);
			addtest(check_convert);
			addtest(check_cache);
	return !!srun();
}
