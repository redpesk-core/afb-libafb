/*
 Copyright (C) 2015-2020 IoT.bzh Company

 Author: Johann Gautier <johann.gautier@iot.bzh>

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
#include <string.h>

#include <check.h>

#include "core/afb-token.h"
#include "sys/verbose.h"

#define TOKEN_NAME "Test Token"

START_TEST (test)
{
	struct afb_token *tok, *tok_bis;
	uint16_t tok_id, i;

	// Testing token creation
	ck_assert_int_eq(afb_token_get(&tok, TOKEN_NAME), 0);
	tok_id = afb_token_id(tok);
	ck_assert_uint_gt(tok_id, 0);
	ck_assert_str_eq(afb_token_string(tok), TOKEN_NAME);

	// Testing that token doesn't get recreated when already existing
	ck_assert_int_eq(afb_token_get(&tok_bis, TOKEN_NAME), 0);
	ck_assert_ptr_eq(tok_bis, tok);
	afb_token_unref(tok_bis);

	//testing that a different token get a different id
	afb_token_get(&tok_bis, "An other token");
	ck_assert_ptr_ne(tok_bis, tok);
	ck_assert_uint_ne(afb_token_id(tok_bis), tok_id);

	// testing unreferensing token
	for(i=0; i<256; i++){
		afb_token_unref(tok);
		afb_token_get(&tok, TOKEN_NAME);
		ck_assert_uint_ne(afb_token_id(tok), tok_id);
	}
}
END_TEST


static Suite *suite;
static TCase *tcase;

void mksuite(const char *name) { suite = suite_create(name); }
void addtcase(const char *name) { tcase = tcase_create(name); suite_add_tcase(suite, tcase); }
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
	mksuite("token");
		addtcase("token");
			addtest(test);
	return !!srun();
}
