/*
 Copyright (C) 2015-2025 IoT.bzh Company

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

#include "sys/x-errno.h"

#include <rp-utils/rp-path-search.h>

/*********************************************************************/

char *prog;
char *base;

/*********************************************************************/

struct listck {
    int idx;
    char **values;
};

int cklistcb(void *closure, const char *path, size_t size)
{
    struct listck *l = closure;
    printf("PATH %d: %s/%s\n", 1+l->idx, path, l->values[l->idx]);
    ck_assert_str_eq(path, l->values[l->idx]);
    l->idx++;
    return 0;
}

/*********************************************************************/

START_TEST (check_addins)
{
	int rc, i, n;
	rp_path_search_t *search, *next;
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
			rc = rp_path_search_add_dirs(&next, l.values[i - 1], 0, search);
		else
			rc = rp_path_search_add_dirs(&next, l.values[0], 1, search);
		ck_assert_int_ge(rc, 0);

		rp_path_search_unref(search);
		search = next;

		rp_path_search_list(search, cklistcb, &l);
		ck_assert_int_eq(l.idx, i);
	}

	rp_path_search_unref(search);
}
END_TEST

/*********************************************************************/

int cbsearch(void *closure, const rp_path_search_entry_t *item)
{
    fprintf(stdout, "%s %s\n", item->isDir ? "D" : "F", item->path);
    return 0;
}

static int filter(void *closure, const rp_path_search_entry_t *item)
{
	return item->name != NULL && strcmp(item->name, "CMakeFiles") != 0;
}

int get_path_cb(void *closure, const rp_path_search_entry_t *item)
{
	char **path = closure;
	*path = strdup(item->path);
	return *path == NULL ? X_ENOMEM : 1;
}

int get_path(rp_path_search_t *search, char **path, const char *name, const char *ext)
{
	*path = NULL;
	int rc = rp_path_search_match(search, RP_PATH_SEARCH_FILE | RP_PATH_SEARCH_RECURSIVE, name, ext, get_path_cb, path);
	return rc < 0 ? rc : rc == 1 ? 0 : X_ENOENT;
}

START_TEST (check_search)
{
#if WITH_DIRENT
	int rc, i;
	rp_path_search_t *search;
	char *path;

	fprintf(stdout, "\n************************************ CHECK SEARCH\n\n");

	rc = rp_path_search_add_dirs(&search, base, 0, 0);
	ck_assert_int_ge(rc, 0);

	rc = get_path(search, &path, "test-path-search", 0);
	ck_assert_int_eq(rc, 0);
	ck_assert_ptr_ne(path, NULL);
	free(path);

	rc = get_path(search, &path, "t-e-s-t-path-search", 0);
	ck_assert_int_le(rc, 0);
	ck_assert_ptr_eq(path, NULL);
	free(path);

	fprintf(stdout, "\n************************************ FULL\n\n");
	i = RP_PATH_SEARCH_FILE | RP_PATH_SEARCH_DIRECTORY | RP_PATH_SEARCH_RECURSIVE;
	rp_path_search(search, i, cbsearch, 0);

	fprintf(stdout, "\n************************************ FILTERED\n\n");
	rp_path_search_filter(search, i, cbsearch, 0, filter, 0);

	rp_path_search_unref(search);
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
