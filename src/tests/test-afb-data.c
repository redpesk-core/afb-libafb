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

static struct afb_type_x4 type3;

static int type1_to_type2(void *closure, afb_data_x4_t from, afb_type_x4_t type, afb_data_x4_t *to);
static int type2_to_type1(void *closure, afb_data_x4_t from, afb_type_x4_t type, afb_data_x4_t *to);
static int type2_to_type3(void *closure, afb_data_x4_t from, afb_type_x4_t type, afb_data_x4_t *to);
static int type3_to_type2(void *closure, afb_data_x4_t from, afb_type_x4_t type, afb_data_x4_t *to);

static
struct afb_type_x4 type1 =
{
	.name = "type1",
	.sharing = AFB_TYPE_X4_LOCAL,
	.family = 0,
	.closure = i2p(1),
	.nconverts = 0,
	.converts = {}
};

static
struct afb_type_x4 type2 =
{
	.name = "type2",
	.sharing = AFB_TYPE_X4_LOCAL,
	.family = 0,
	.closure = i2p(2),
	.nconverts = 2,
	.converts = {
		{ .type = &type1, .convert_from = type1_to_type2, .convert_to = type2_to_type1 },
		{ .type = &type3, .convert_from = type3_to_type2, .convert_to = type2_to_type3 }
	}
};

static
struct afb_type_x4 type3 =
{
	.name = "type3",
	.sharing = AFB_TYPE_X4_LOCAL,
	.family = 0,
	.closure = i2p(3),
	.nconverts = 0,
	.converts = {}
};

/*********************************************************************/

START_TEST (check_data)
{
	int s, m;
	struct afb_data *data;
	char buffer[50];
	size_t size = 10;

	/* creation */
	m = 0x77777777;
	s = afb_data_create_set_x4(&data, &type1, buffer, size, dor, i2p(m));
	ck_assert(s == 0);
	ck_assert(&type1 == afb_data_type_x4(data));
	ck_assert(buffer == afb_data_pointer(data));
	ck_assert(size == afb_data_size(data));
	gmask = 0;
	afb_data_clear(data);
	ck_assert(gmask == m);
	ck_assert(NULL == afb_data_type_x4(data));
	ck_assert(NULL == afb_data_pointer(data));
	ck_assert(0 == afb_data_size(data));

	/* check no more cleaners */
	m = gmask = 0;
	afb_data_clear(data);
	ck_assert(gmask == m);
	ck_assert(NULL == afb_data_type_x4(data));
	ck_assert(NULL == afb_data_pointer(data));
	ck_assert(0 == afb_data_size(data));

	afb_data_unref(data);
}
END_TEST

/*********************************************************************/
/* checking computation of converters using hypercube geometry */

static void cvdrop(void *p)
{
	gmask += p2i(p);
}

static int type1_to_type2(void *closure, afb_data_x4_t from, afb_type_x4_t type, afb_data_x4_t *to)
{
	gmask = gmask * 100 + 12;
	ck_assert_int_eq(0, afb_data_x4_create_set_x4(to, &type2, 0, 0, cvdrop, i2p(2)));
	return 0;
}
static int type2_to_type1(void *closure, afb_data_x4_t from, afb_type_x4_t type, afb_data_x4_t *to)
{
	gmask = gmask * 100 + 21;
	ck_assert_int_eq(0, afb_data_x4_create_set_x4(to, &type1, 0, 0, cvdrop, i2p(1)));
	return 0;
}
static int type2_to_type3(void *closure, afb_data_x4_t from, afb_type_x4_t type, afb_data_x4_t *to)
{
	gmask = gmask * 100 + 23;
	ck_assert_int_eq(0, afb_data_x4_create_set_x4(to, &type3, 0, 0, cvdrop, i2p(3)));
	return 0;
}
static int type3_to_type2(void *closure, afb_data_x4_t from, afb_type_x4_t type, afb_data_x4_t *to)
{
	gmask = gmask * 100 + 32;
	ck_assert_int_eq(0, afb_data_x4_create_set_x4(to, &type2, 0, 0, cvdrop, i2p(2)));
	return 0;
}

static void tconv(afb_type_x4_t from, afb_type_x4_t to, int sig)
{
	int rc;
	struct afb_data *dfrom, *dto;

	fprintf(stderr, "testing conversion from %s to %s\n", from->name, to->name);
	rc = afb_data_create_set_x4(&dfrom, from, 0, 0, cvdrop, i2p(p2i(from->closure) * 10));
	ck_assert_int_eq(rc, 0);
	gmask = 0;
	rc = afb_data_convert_to_x4(dfrom, to, &dto);
	ck_assert_int_eq(rc, 0);
	ck_assert_int_eq(gmask, sig);
	gmask = 0;
	afb_data_unref(dfrom);
	afb_data_unref(dto);
	ck_assert_int_eq(gmask, sig);
}

START_TEST (check_convert)
{
	tconv(&type1, &type2, 12);
	tconv(&type2, &type1, 21);
	tconv(&type2, &type3, 23);
	tconv(&type3, &type2, 32);
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
	return !!srun();
}
