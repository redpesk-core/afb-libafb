/*
 Copyright (C) 2015-2021 IoT.bzh Company

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
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include <check.h>

#include <afb/afb-binding-v4.h>

#include "core/afb-data.h"
#include "core/afb-data-array.h"
#include "core/afb-type.h"
#include "core/afb-type-predefined.h"
#include "sys/x-errno.h"
#include <json-c/json.h>


/*********************************************************************/

#define i2p(x)  ((void*)((intptr_t)(x)))
#define p2i(x)  ((int)((intptr_t)(x)))

int gmask;
int cvtmask;
int dropmask;

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
	dropmask += p2i(p);
}

static int t2t(void *closure, struct afb_data *from, struct afb_type *type, struct afb_data **to)
{
	int c = p2i(closure);
	gmask = gmask * 100 + c;
	cvtmask = cvtmask * 100 + c;
	ck_assert_int_eq(0, afb_data_create_raw(to, type, 0, 0, cvdrop, i2p(c % 10)));
	return 0;
}


static struct afb_type *type1, *type2, *type3;

static struct afb_type *type5;

static struct afb_type *type7, *type8, *type9;

static struct afb_type *type4, *type6;

static
void
init_types()
{
	int rc;

	/* needed when CK_FORK=no */
	if (type1 && afb_type_get("type1") == type1)
		return;

	rc = afb_type_register(&type1, "type1", 0, 0, 0);
	ck_assert_int_eq(rc, 0);

	rc = afb_type_register(&type2, "type2", 0, 0, 0);
	ck_assert_int_eq(rc, 0);

	rc = afb_type_register(&type3, "type3", 0, 0, 0);
	ck_assert_int_eq(rc, 0);

	rc = afb_type_register(&type5, "type5", 0, 0, 0);
	ck_assert_int_eq(rc, 0);

	rc = afb_type_register(&type7, "type7", 0, 0, 0);
	ck_assert_int_eq(rc, 0);

	rc = afb_type_register(&type8, "type8", 0, 0, 0);
	ck_assert_int_eq(rc, 0);

	rc = afb_type_register(&type9, "type9", 0, 0, 0);
	ck_assert_int_eq(rc, 0);

	rc = afb_type_register(&type4, "type4", 0, 0, 0);
	ck_assert_int_eq(rc, 0);

	rc = afb_type_register(&type6, "type6", 0, 0, 0);
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(type1, type2, t2t, i2p(12));
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(type2, type1, t2t, i2p(21));
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(type3, type2, t2t, i2p(32));
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(type2, type3, t2t, i2p(23));
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(type1, type5, t2t, i2p(15));
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(type2, type5, t2t, i2p(25));
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(type3, type5, t2t, i2p(35));
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(type5, type1, t2t, i2p(51));
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(type5, type2, t2t, i2p(52));
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(type5, type3, t2t, i2p(53));
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(type7, type5, t2t, i2p(75));
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(type8, type5, t2t, i2p(85));
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(type9, type5, t2t, i2p(95));
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(type5, type7, t2t, i2p(57));
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(type5, type8, t2t, i2p(58));
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(type5, type9, t2t, i2p(59));
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(type4, &afb_type_predefined_i32, t2t, i2p(49));
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(&afb_type_predefined_i32, type6, t2t, i2p(96));
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(&afb_type_predefined_i64, type4, t2t, i2p(84));
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(type4, &afb_type_predefined_bytearray, t2t, i2p(49));
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
	ck_assert(buffer == afb_data_ro_pointer(data));
	ck_assert(size == afb_data_size(data));
	gmask = 0;
	afb_data_unref(data);
	ck_assert(gmask == m);
}
END_TEST

/*********************************************************************/
/* checking computation of converters using hypercube geometry */

static void tconv(struct afb_type *from, struct afb_type *to, int cvt, int drop)
{
	int rc;
	struct afb_data *dfrom, *dto;

	fprintf(stderr, "testing conversion from %s to %s\n", afb_type_name(from), afb_type_name(to));
	for(rc = cvt; rc >= 10 ; rc /=10);
	rc = afb_data_create_raw(&dfrom, from, 0, 0, cvdrop, i2p(rc));
	ck_assert_int_eq(rc, 0);
	gmask = cvtmask = dropmask = 0;
	rc = afb_data_convert(dfrom, to, &dto);
	ck_assert_int_eq(rc, 0);
	ck_assert_int_eq(cvtmask, cvt);
	afb_data_unref(dfrom);
	afb_data_unref(dto);
	ck_assert_int_eq(dropmask, drop);
}

START_TEST (check_convert)
{
	init_types();
	tconv(type1, type2, 12, 3);
	tconv(type2, type1, 21, 3);
	tconv(type2, type3, 23, 5);
	tconv(type3, type2, 32, 5);
	tconv(type1, type7, 1557, 13);
	tconv(type2, type8, 2558, 15);
	tconv(type3, type9, 3559, 17);
	tconv(type4, type6, 4996, 19);
	tconv(&afb_type_predefined_i64, &afb_type_predefined_bytearray, 8449, 21);
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
	rc = afb_data_convert(data, type2, &convertedData);

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
	rc = afb_data_convert(data, type2, &convertedDataBis);
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

void data_dispose(void * closure){
	fprintf(stderr, "went through data_dispose with closure %d\n", p2i(closure));
	gmask += p2i(closure);
}

void predefconv(struct afb_type *from, struct afb_type *to){
	struct afb_data *data, *result;
	int r;
	uint64_t i = 0;

	r = afb_data_create_raw(&data, from, &i, 0, data_dispose, i2p(1));
	ck_assert_int_eq(r, 0);
	r = afb_data_convert(data, &afb_type_predefined_i64, &result);
	ck_assert_int_eq(r, 0);
}

START_TEST (test_predefine_types){
	struct afb_data *data, *result;
	int i, j, r;
	char * b[64];
	json_object * js = json_object_new_int(35);

	struct test_type_data
	{
		struct afb_type * predef_type;
		void * buff;
	};

	struct test_type_data type_data[] = {
		{&afb_type_predefined_opaque, b},
		{&afb_type_predefined_stringz, b},
		{&afb_type_predefined_json, js},
		{&afb_type_predefined_json_c, js},
		{&afb_type_predefined_bool, b},
		{&afb_type_predefined_i32, b},
		{&afb_type_predefined_u32, b},
		{&afb_type_predefined_i64, b},
		{&afb_type_predefined_u64, b},
		{&afb_type_predefined_double, b},
		{NULL,NULL}
	};

	for(i=0; type_data[i].predef_type!=NULL; i++){
		fprintf(stderr, "\n== %s ==\n", afb_type_name(type_data[i].predef_type));
		gmask = 0;
		r = afb_data_create_raw(&data, type_data[i].predef_type, type_data[i].buff, 0, data_dispose, i2p(i));
		ck_assert_int_eq(r, 0);
		for(j=0; type_data[j].predef_type!=NULL; j++){
			fprintf(stderr, "testing convertion from %s to %s => ", afb_type_name(type_data[i].predef_type), afb_type_name(type_data[j].predef_type));
			r = afb_data_convert(data, type_data[j].predef_type, &result);
			if (r == X_ENOENT) {
				fprintf(stderr, "no convertion available !\n");
				ck_assert_ptr_eq(result, NULL);
			}
			else {
				fprintf(stderr, "result = %d\n", r);
				ck_assert_int_eq(r, 0);
				afb_data_unref(result);
			}
		}
		afb_data_unref(data);
		ck_assert_int_eq(gmask, i);
	}
}
END_TEST

/*********************************************************************/

static struct afb_type *deptype1, *deptype2, *deptype3;

static unsigned depflags = 0;

static void depdrop1(void *closure)
{
	depflags += 1;
}

static void depdrop2(void *closure)
{
	depflags += 10;
}

static void depdrop3(void *closure)
{
	depflags += 100;
}

static int cvtdep12(void *closure, struct afb_data *from, struct afb_type *type, struct afb_data **to)
{
	int rc = afb_data_create_raw(to, deptype2, to, 0, depdrop2, 0);
	ck_assert_int_eq(rc, 0);
	rc = afb_data_dependency_add(*to, from);
	ck_assert_int_eq(rc, 0);
	depflags += 1000;
	return 0;
}

static int cvtdep23(void *closure, struct afb_data *from, struct afb_type *type, struct afb_data **to)
{
	int rc = afb_data_create_raw(to, deptype3, to, 0, depdrop3, 0);
	ck_assert_int_eq(rc, 0);
	rc = afb_data_dependency_add(*to, from);
	ck_assert_int_eq(rc, 0);
	depflags += 10000;
	return 0;
}

START_TEST (check_depend)
{
	struct afb_data *data, *result;
	int rc;

	rc = afb_type_register(&deptype1, "deptype1", 0, 0, 0);
	ck_assert_int_eq(rc, 0);

	rc = afb_type_register(&deptype2, "deptype2", 0, 0, 0);
	ck_assert_int_eq(rc, 0);

	rc = afb_type_register(&deptype3, "deptype3", 0, 0, 0);
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(deptype1, deptype2, cvtdep12, 0);
	ck_assert_int_eq(rc, 0);

	rc = afb_type_add_converter(deptype2, deptype3, cvtdep23, 0);
	ck_assert_int_eq(rc, 0);

	depflags = 0;
	rc = afb_data_create_raw(&data, deptype1, &data, 0, depdrop1, 0);
	ck_assert_int_eq(rc, 0);
	ck_assert_int_eq(depflags, 0);

	rc = afb_data_array_convert(1, &data, &deptype3, &result);
	ck_assert_int_eq(rc, 0);
	ck_assert_int_eq(depflags, 11000);

	afb_data_notify_changed(result);
	ck_assert_int_eq(depflags, 11000);

	afb_data_unref(data);
	ck_assert_int_eq(depflags, 11000);

	afb_data_unref(result);
	ck_assert_int_eq(depflags, 11111);
}
END_TEST

/*********************************************************************/

static Suite *suite;
static TCase *tcase;

void mksuite(const char *name) { suite = suite_create(name); }
void addtcase(const char *name) { tcase = tcase_create(name); suite_add_tcase(suite, tcase); tcase_set_timeout(tcase, 120); }
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
	mksuite("afbdata");
		addtcase("afb-data");
			addtest(check_data);
			addtest(check_convert);
			addtest(check_cache);
			addtest(test_predefine_types);
			addtest(check_depend);
	return !!srun();
}
