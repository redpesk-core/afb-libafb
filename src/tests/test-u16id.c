/*
 Copyright (C) 2015-2024 IoT.bzh Company

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

#include "utils/u16id.h"
#include "sys/x-errno.h"

/*********************************************************************/

#define S  29

void cbi2p(void*closure, uint16_t id, void *ptr)
{
	int *y = closure;
	int ni = y[0];
	ck_assert((ni >> id) & 1);
	ck_assert((uintptr_t)ptr == (uintptr_t)(ni + id));
	y[1]++;
}

void test_i2ptr(struct u16id2ptr **pi2p)
{
	int i, ni, n, x, y[2];
	uint16_t j;
	void *p;

	i = 0;
	while (!(i >> S)) {
		ni = i * 3 + 1;
		n = 0;
		for (j = 0 ; j < S ; j++) {
			if ((i >> j) & 1) {
				ck_assert_int_eq(1, u16id2ptr_has(*pi2p, j));
				ck_assert_int_eq(0, u16id2ptr_get(*pi2p, j, &p));
				ck_assert((uintptr_t)p == (uintptr_t)(i + j));
				ck_assert_int_eq(X_EEXIST, u16id2ptr_add(pi2p, j, p));
				ck_assert_int_eq(0, u16id2ptr_put(*pi2p, j, p));
			} else {
				ck_assert_int_eq(0, u16id2ptr_has(*pi2p, j));
				ck_assert_int_eq(X_ENOENT, u16id2ptr_get(*pi2p, j, &p));
				ck_assert_int_eq(X_ENOENT, u16id2ptr_put(*pi2p, j, p));
			}
			if ((ni >> j) & 1) {
				p = (void*)(uintptr_t)(ni + j);
				ck_assert_int_eq(0, u16id2ptr_set(pi2p, j, p));
				n++;
			} else if ((i >> j) & 1) {
				ck_assert_int_eq(0, u16id2ptr_drop(pi2p, j, &p));
				ck_assert((uintptr_t)p == (uintptr_t)(i + j));
			} else {
				ck_assert_int_eq(X_ENOENT, u16id2ptr_drop(pi2p, j, NULL));
			}
		}
		ck_assert_int_eq(n, u16id2ptr_count(*pi2p));
		for (x = 0 ; x < n ; x++) {
			ck_assert_int_eq(0, u16id2ptr_at(*pi2p, x, &j, &p));
			ck_assert((ni >> j) & 1);
			ck_assert((uintptr_t)p == (uintptr_t)(ni + j));
		}
		y[0] = ni;
		y[1] = 0;
		u16id2ptr_forall(*pi2p, cbi2p, y);
		ck_assert_int_eq(n, y[1]);
		i = ni;
	}
}

START_TEST (check_u16id2ptr)
{
	struct u16id2ptr *i2p;

	i2p = NULL;
	test_i2ptr(&i2p);
	ck_assert(i2p);
	u16id2ptr_destroy(&i2p);
	ck_assert(!i2p);
	ck_assert_int_eq(0, u16id2ptr_create(&i2p));
	test_i2ptr(&i2p);
	ck_assert(i2p);
	u16id2ptr_destroy(&i2p);
	ck_assert(!i2p);
}
END_TEST

/*********************************************************************/

void test_i2bool(struct u16id2bool **pi2b)
{
	int i, j, ni, v;
	uint16_t x;

	i = 0;
	while (!(i >> S)) {
		ni = i * 3 + 1;
		for (j = 0 ; j < S ; j++) {
			x = (uint16_t)(j * 5);
			v = (i >> j) & 1;
			ck_assert_int_eq(v, u16id2bool_get(*pi2b, x));
			ck_assert_int_eq(v, u16id2bool_set(pi2b, x, (ni >> j) & 1));
		}
		i = ni;
	}
	for (j = 0 ; j < S ; j++) {
		x = (uint16_t)(j * 5);
		v = (i >> j) & 1;
		ck_assert_int_eq(v, u16id2bool_get(*pi2b, x));
		ck_assert_int_eq(v, u16id2bool_set(pi2b, x, 0));
	}
}

START_TEST (check_u16id2bool)
{
	struct u16id2bool *i2b;

	i2b = NULL;
	test_i2bool(&i2b);
	ck_assert(i2b);
	u16id2bool_destroy(&i2b);
	ck_assert(!i2b);
	ck_assert_int_eq(0, u16id2bool_create(&i2b));
	test_i2bool(&i2b);
	ck_assert(i2b);
	u16id2bool_destroy(&i2b);
	ck_assert(!i2b);
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
	mksuite("u16id");
		addtcase("u16id");
			addtest(check_u16id2ptr);
			addtest(check_u16id2bool);
	return !!srun();
}
