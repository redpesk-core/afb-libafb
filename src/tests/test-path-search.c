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

#include "libafb-config.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <libgen.h>

#include <check.h>

#include "utils/path-search.h"
#include "sys/x-errno.h"

/*********************************************************************/

char *prog;
char *base;

/*********************************************************************/

struct listck {
    int idx;
    char **values;
};

void listcb(void *closure, const char *path)
{
    int *pi = closure;
    printf("PATH %d: %s\n", ++*pi, path);
}

void cklistcb(void *closure, const char *path)
{
    struct listck *l = closure;
    printf("PATH %d: %s/%s\n", 1+l->idx, path, l->values[l->idx]);
    ck_assert_str_eq(path, l->values[l->idx]);
    l->idx++;
}

/*********************************************************************/

START_TEST (check_addins)
{
	int rc, i, n;
	struct path_search *search, *next;
	struct listck l;
	char *expecteds[] = {
		"0",
		"1",
		"2",
		"3",
		"4",
		"5",
		"6"
	};

	fprintf(stdout, "\n************************************ CHECK ADDINS\n\n");

	search = 0;
	n = (int)(sizeof expecteds / sizeof *expecteds);
	for(i = 1 ; i <= n ; i++) {
		fprintf(stdout, "-----\n");
		l.idx = 0;
		l.values = &expecteds[(n - i) >> 1];
		if (i & 1)
			rc = path_search_add_dirs(&next, l.values[i - 1], 0, search);
		else
			rc = path_search_add_dirs(&next, l.values[0], 1, search);
		ck_assert_int_ge(rc, 0);

		path_search_unref(search);
		search = next;

		path_search_list(search, cklistcb, &l);
		ck_assert_int_eq(l.idx, i);
	}

	path_search_unref(search);
}
END_TEST

/*********************************************************************/

int cbsearch(void *closure, struct path_search_item *item)
{
    fprintf(stdout, "%s %s\n", item->isDir ? "D" : "F", item->path);
    return 0;
}

static int filter(void *closure, struct path_search_item *item)
{
	return strcmp(item->name, "CMakeFiles") != 0;
}

START_TEST (check_search)
{
#if WITH_DIRENT
	int rc, i;
	struct path_search *search;
	char *path;

	fprintf(stdout, "\n************************************ CHECK SEARCH\n\n");

	rc = path_search_add_dirs(&search, base, 0, 0);
	ck_assert_int_ge(rc, 0);

	rc = path_search_get_path(search, &path, 1, "test-path-search", 0);
	ck_assert_int_eq(rc, 0);
	ck_assert_ptr_ne(path, 0);

	rc = path_search_get_path(search, &path, 1, "t-e-s-t-path-search", 0);
	ck_assert_int_le(rc, 0);
	ck_assert_ptr_eq(path, 0);

	fprintf(stdout, "\n************************************ FULL\n\n");
	i = PATH_SEARCH_FILE | PATH_SEARCH_DIRECTORY | PATH_SEARCH_RECURSIVE;
	path_search(search, i, cbsearch, 0);

	fprintf(stdout, "\n************************************ FILTERED\n\n");
	path_search_filter(search, i, cbsearch, 0, filter);

	path_search_unref(search);
#endif
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
	prog = *av;
	base = dirname(strdup(prog));
	mksuite("path-search");
		addtcase("path-search");
			addtest(check_addins);
			addtest(check_search);
	return !!srun();
}
