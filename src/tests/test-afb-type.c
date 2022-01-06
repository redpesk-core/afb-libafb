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
#include <errno.h>
#include <string.h>
#include <stdint.h>

#include <check.h>

#include <afb/afb-binding-v4.h>

#include "core/afb-type.h"
#include "sys/x-errno.h"

/*********************************************************************/

const char *names[] = {
	"type1",
	"type2",
	"type3",
	"type4",
	"type5"
};

START_TEST (check_type)
{
	int n = (int)(sizeof names / sizeof *names);
	int i;
	int r;
	struct afb_type *t, *T;


	/* ensure basis */
	for (i = 0 ; i < n ; i++) {
		t = afb_type_get(names[i]);
		ck_assert_ptr_eq(t, 0);
		r = afb_type_register(&t, names[i], 0, 0, 0);
		ck_assert_int_eq(r, 0);
		ck_assert_ptr_eq(names[i], afb_type_name(t));
		T = afb_type_get(names[i]);
		ck_assert_ptr_ne(T, 0);
		ck_assert_ptr_eq(T, t);
	}
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
	mksuite("afbtype");
		addtcase("afb-type");
			addtest(check_type);
	return !!srun();
}
