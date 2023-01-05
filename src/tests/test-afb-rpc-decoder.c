/*
 Copyright (C) 2015-2023 IoT.bzh Company

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

#include <stdlib.h>
#include <stdio.h>
#include <sys/uio.h>

#include <check.h>

#include "rpc/afb-rpc-decoder.h"

/*************************** Helpers Functions ***************************/

static char ref_int[] __attribute__((aligned(4))) = {
	1,
	2, 0,
	3, 0, 0, 0,
	0,
	11,
	0, 0, 0,
	12, 0,
	0, 0,
	13, 0, 0, 0
};

/******************************** Test input ********************************/
START_TEST(test_input_int)
{
	int rc;
	afb_rpc_decoder_t rpc_decoder;
	uint32_t sz;
	uint8_t u8;
	uint16_t u16;
	uint32_t u32;

	afb_rpc_decoder_init(&rpc_decoder, ref_int, sizeof ref_int);
	ck_assert_int_eq(rpc_decoder.size, sizeof ref_int);
	ck_assert_ptr_eq(rpc_decoder.pointer, ref_int);
	ck_assert_int_eq(rpc_decoder.offset, 0);

	sz = afb_rpc_decoder_remaining_size(&rpc_decoder);
	ck_assert_int_eq(sz, sizeof ref_int);

	rc = afb_rpc_decoder_read_uint8(&rpc_decoder, &u8);
	ck_assert_int_eq(rc, 0);
	ck_assert_int_eq(u8, 1);

	rc = afb_rpc_decoder_read_uint16le(&rpc_decoder, &u16);
	ck_assert_int_eq(rc, 0);
	ck_assert_int_eq(u16, 2);

	rc = afb_rpc_decoder_read_uint32le(&rpc_decoder, &u32);
	ck_assert_int_eq(rc, 0);
	ck_assert_int_eq(u32, 3);

	rc = afb_rpc_decoder_read_is_align(&rpc_decoder, 4);
	ck_assert_int_eq(rc, 0);

	rc = afb_rpc_decoder_read_align(&rpc_decoder, 4);
	ck_assert_int_eq(rc, 0);

	rc = afb_rpc_decoder_read_is_align(&rpc_decoder, 4);
	ck_assert_int_eq(rc, 1);

	rc = afb_rpc_decoder_read_uint8(&rpc_decoder, &u8);
	ck_assert_int_eq(rc, 0);
	ck_assert_int_eq(u8, 11);

	rc = afb_rpc_decoder_read_align(&rpc_decoder, 4);
	ck_assert_int_eq(rc, 0);

	rc = afb_rpc_decoder_read_uint16le(&rpc_decoder, &u16);
	ck_assert_int_eq(rc, 0);
	ck_assert_int_eq(u16, 12);

	rc = afb_rpc_decoder_read_align(&rpc_decoder, 4);
	ck_assert_int_eq(rc, 0);

	rc = afb_rpc_decoder_read_uint32le(&rpc_decoder, &u32);
	ck_assert_int_eq(rc, 0);
	ck_assert_int_eq(u32, 13);

	sz = afb_rpc_decoder_remaining_size(&rpc_decoder);
	ck_assert_int_eq(sz, 0);
}

END_TEST

// START_TEST(test_input_bufs)
// {

// }
// END_TEST

/******************************** Tests ********************************/
static Suite *suite;
static TCase *tcase;

void mksuite(const char *name)
{
	suite = suite_create(name);
}

void addtcase(const char *name)
{
	tcase = tcase_create(name);
	suite_add_tcase(suite, tcase);
}

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
	mksuite("afb-rpc-coder");
	addtcase("input");
	addtest(test_input_int);
	return !!srun();
}
