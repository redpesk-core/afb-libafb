#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

#include <check.h>

#include <afb/afb-binding-v4.h>

#include "core/afb-type.h"
#include "sys/x-errno.h"

/*********************************************************************/

uint8_t shares[] = {
	AFB_TYPE_X4_LOCAL,
	AFB_TYPE_X4_SHAREABLE,
	AFB_TYPE_X4_STREAMABLE
};

struct afb_type_x4 type1 =
{
	.name = "type1",
	.sharing = AFB_TYPE_X4_LOCAL,
	.family = 0,
	.closure = 0,
	.nconverts = 0,
	.converts = {}
};

struct afb_type_x4 type2 =
{
	.name = "type2",
	.sharing = AFB_TYPE_X4_LOCAL,
	.family = 0,
	.closure = 0,
	.nconverts = 0,
	.converts = {}
};

struct afb_type_x4 type3 =
{
	.name = "type3",
	.sharing = AFB_TYPE_X4_LOCAL,
	.family = 0,
	.closure = 0,
	.nconverts = 0,
	.converts = {}
};

struct afb_type_x4 type4 =
{
	.name = "type4",
	.sharing = AFB_TYPE_X4_LOCAL,
	.family = 0,
	.closure = 0,
	.nconverts = 0,
	.converts = {}
};

struct afb_type_x4 type5 =
{
	.name = "type5",
	.sharing = AFB_TYPE_X4_LOCAL,
	.family = 0,
	.closure = 0,
	.nconverts = 0,
	.converts = {}
};


afb_type_x4_t types[] = {
	&type1,
	&type2,
	&type3,
	&type4,
	&type5
};

START_TEST (check_type)
{
	int n = (int)(sizeof types / sizeof *types);
	int i, id;

	ck_assert(!afb_type_is_valid_id(0));
	ck_assert(!afb_type_is_valid_id(1));

	/* ensure basis */
	for (i = 0 ; i < n ; i++) {
		ck_assert(!afb_type_is_valid_id(afb_type_id_of_name(types[i]->name)));
		id = i + 1;
		ck_assert(!afb_type_is_valid_id(id));
		ck_assert(afb_type_name_of_id(id) == NULL);
//		ck_assert(afb_type_sharing(id) == afb_type_Process);
		id = afb_type_register_type_x4(types[i]);
		ck_assert(id > 0);
		ck_assert(i + 1 == id);
		ck_assert(id == afb_type_id_of_name(types[i]->name));
		ck_assert(afb_type_is_valid_id(id));
		ck_assert(afb_type_name_of_id(id) == types[i]->name);
//		ck_assert(afb_type_sharing(id) == shares[i % ns]);
	}
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
	mksuite("afbtype");
		addtcase("afb-type");
			addtest(check_type);
	return !!srun();
}
