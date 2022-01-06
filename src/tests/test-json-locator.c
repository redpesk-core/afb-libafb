/*
 Copyright (C) 2015-2022 IoT.bzh Company

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
#include <limits.h>
#include <unistd.h>

#include <check.h>
#if !defined(ck_assert_ptr_null)
# define ck_assert_ptr_null(X)      ck_assert_ptr_eq(X, NULL)
# define ck_assert_ptr_nonnull(X)   ck_assert_ptr_ne(X, NULL)
#endif

/*********************************************************************/

#include "utils/json-locator.h"
#include "utils/wrap-json.h"

/*********************************************************************/

int getpath(char buffer[PATH_MAX], const char *base)
{
	static const char *paths[] = { "tests/", "src/", "build/", NULL };

	int rc;
	int len;
	int lenp;
	const char **pp = paths;


	len = snprintf(buffer, PATH_MAX, "%s", base);
	ck_assert_int_ge(len, 0);
	rc = access(buffer, F_OK);
	while (rc < 0 && *pp) {
		fprintf(stderr, "Can't access file %s\n", buffer);
		lenp = (int)strlen(*pp);
		if (lenp + len + 1 > PATH_MAX)
			break;
		memmove(buffer + lenp, buffer, (size_t)len + 1);
		memcpy(buffer, *pp, (size_t)lenp);
		pp++;
		len += lenp;
		rc = access(buffer, F_OK);
	}
	if (rc == 0)
		fprintf(stderr, "FOUND %s for %s\n", buffer, base);
	else {
		fprintf(stderr, "Can't access file %s\n", buffer);
		fprintf(stderr, "Can't find file %s\n", base);
	}
	return rc;
}

/*********************************************************************/

void explore(struct json_object *root, struct json_object *jso)
{
	unsigned linenum;
	const char *filename;
	char *locat;

	locat = json_locator_search_path(root, jso);
	filename = json_locator_locate(jso, &linenum);
	fprintf (stderr, "%p %s:%d {%s} -> %s\n", jso, filename?:"<null>", linenum, locat ?: "<null>", json_object_get_string(jso));
	free(locat);
	switch (json_object_get_type(jso)) {
	case json_type_object:
		wrap_json_object_for_all(jso, (void*)explore, root);
		break;
	case json_type_array:
		wrap_json_array_for_all(jso, (void*)explore, root);
		break;
	default:
		break;
	}
}

/*********************************************************************/

START_TEST (check_read)
{
	char buffer[PATH_MAX];
	struct json_object *obj;
	int rc;

	rc = getpath(buffer, TEST_SOURCE_DIR"test-json-locator.json");
	ck_assert_int_eq(0, rc);
	rc = json_locator_from_file(&obj, buffer);
	ck_assert_int_eq(0, rc);
	explore(obj, obj);
	json_object_put(obj);
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
			addtest(check_read);
	return !!srun();
}
