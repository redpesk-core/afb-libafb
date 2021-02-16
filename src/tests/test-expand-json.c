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
#include <string.h>

#include <check.h>
#if !defined(ck_assert_ptr_null)
# define ck_assert_ptr_null(X)      ck_assert_ptr_eq(X, NULL)
# define ck_assert_ptr_nonnull(X)   ck_assert_ptr_ne(X, NULL)
#endif

/*********************************************************************/

#include "utils/expand-vars.h"
#include "utils/expand-json.h"
#include "utils/wrap-json.h"

/*********************************************************************/

char input[] = "{ \"key\": [ { \"$ref\": \"$valref\" }, 5, true, 0 ], \"item\": \"x$(valitem)x\" }";
char *vars[] = { "valref=toto", "valitem=HELLO", "toto=item", NULL };
char output[] = "{ \"key\": [ \"toto\", 5, true, 0 ], \"item\": \"xHELLOx\" }";

void printpath(expand_json_path_t path, struct json_object* object)
{
	int i, n = expand_json_path_length(path);

	printf("ROOT");
	for (i = 0 ; i < n ; i++) {
		if (expand_json_path_is_array(path, i))
			printf("[%d]", (int)expand_json_path_index(path, i));
		else
			printf(".%s", expand_json_path_key(path, i));
	}
	printf(" = %s\n", json_object_get_string(object));
}

struct json_object *expobj(void *closure, struct json_object* object, expand_json_path_t path)
{
	struct json_object *ref;

	ck_assert_ptr_eq(closure, input);
	printpath(path, object);
	if (json_object_object_get_ex(object, "$ref", &ref))
		object = json_object_get(ref);
	return object;
}

struct json_object *expstr(void *closure, struct json_object* object, expand_json_path_t path)
{
	char *trf;

	ck_assert_ptr_eq(closure, input);
	printpath(path, object);
	trf = expand_vars_only(json_object_get_string(object), 0, vars);
	if (trf) {
		object = json_object_new_string(trf);
		free(trf);
	}
	return object;
}

START_TEST (check_expand)
{
	struct json_object *in = json_tokener_parse(input);
	struct json_object *out = json_tokener_parse(output);
	struct json_object *res;

	res = expand_json(in, input, expobj, expstr);
	printf("got %s\n", json_object_get_string(res));
	ck_assert_int_eq(0, wrap_json_cmp(res, out));

	if (res != in)
		json_object_put(res);
	json_object_put(out);
	json_object_put(in);
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
	mksuite("expand-json");
		addtcase("expand-json");
			addtest(check_expand);
	return !!srun();
}
