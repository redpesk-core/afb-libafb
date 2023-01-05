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

#include "rpc/afb-rpc-coder.h"

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

/*************************** Helpers Functions ***************************/

static int disp2_nr = 0;
static void *disp2_val[10][2];

static void disp2(void *clo, void *arg)
{
	disp2_val[disp2_nr][0] = clo;
	disp2_val[disp2_nr][1] = arg;
	disp2_nr++;
}

/******************************** Test output ********************************/

START_TEST(test_output_int)
{
	int rc;
	afb_rpc_coder_t rpc_coder;
	uint32_t sz, sz2;
	char buf[10 + sizeof ref_int];

	disp2_nr = 0;

	afb_rpc_coder_init(&rpc_coder);
	ck_assert_int_eq(rpc_coder.dispose_count, 0);
	ck_assert_int_eq(rpc_coder.buffer_count, 0);
	ck_assert_int_eq(rpc_coder.inline_remain, 0);
	ck_assert_int_eq(rpc_coder.size, 0);

	rc = afb_rpc_coder_write_uint8(&rpc_coder, 1);
	ck_assert_int_eq(rc, 0);

	rc = afb_rpc_coder_write_uint16le(&rpc_coder, 2);
	ck_assert_int_eq(rc, 0);

	rc = afb_rpc_coder_write_uint32le(&rpc_coder, 3);
	ck_assert_int_eq(rc, 0);

	rc = afb_rpc_coder_write_align(&rpc_coder, 4);
	ck_assert_int_eq(rc, 0);
	rc = afb_rpc_coder_write_uint8(&rpc_coder, 11);
	ck_assert_int_eq(rc, 0);

	rc = afb_rpc_coder_write_align(&rpc_coder, 4);
	ck_assert_int_eq(rc, 0);
	rc = afb_rpc_coder_write_uint16le(&rpc_coder, 12);
	ck_assert_int_eq(rc, 0);

	rc = afb_rpc_coder_write_align(&rpc_coder, 4);
	ck_assert_int_eq(rc, 0);
	rc = afb_rpc_coder_write_uint32le(&rpc_coder, 13);
	ck_assert_int_eq(rc, 0);

	rc = afb_rpc_coder_output_sizes(&rpc_coder, &sz);
	ck_assert_int_eq(sz, sizeof ref_int);
	ck_assert_int_eq(rc, 2);

	sz2 = afb_rpc_coder_output_get_buffer(&rpc_coder, buf, sz + 10);
	ck_assert_int_eq(sz2, sz);
	rc = memcmp(ref_int, buf, sz);
	ck_assert_int_eq(rc, 0);

	afb_rpc_coder_on_dispose2_output(&rpc_coder, disp2, NULL, NULL);
	ck_assert_int_eq(disp2_nr, 0);
	afb_rpc_coder_output_dispose(&rpc_coder);
	ck_assert_int_eq(disp2_nr, 1);

	rc = afb_rpc_coder_output_sizes(&rpc_coder, &sz);
	ck_assert_int_eq(rc, 0);
	ck_assert_int_eq(sz, 0);

}

END_TEST START_TEST(test_output_bufs)
{
	int rc;
	afb_rpc_coder_t rpc_coder;
	uint32_t sz;
	static const char ref[] =
	    "Progress is impossible without change, and those who cannot change their minds cannot change anything.\n";
	struct iovec iovecs[10];

	disp2_nr = 0;

	afb_rpc_coder_init(&rpc_coder);
	ck_assert_int_eq(rpc_coder.dispose_count, 0);

	rc = afb_rpc_coder_write(&rpc_coder, ref, (uint32_t)strlen(ref));
	ck_assert_int_eq(rc, 0);

	rc = afb_rpc_coder_write_copy(&rpc_coder, ref, (uint32_t)strlen(ref));
	ck_assert_int_eq(rc, 0);

	rc = afb_rpc_coder_write(&rpc_coder, ref, (uint32_t)strlen(ref));
	ck_assert_int_eq(rc, 0);

	rc = afb_rpc_coder_on_dispose2_output(&rpc_coder, disp2, (void *)ref,
					(void *)ref + 1);
	ck_assert_int_eq(rc, 0);

	rc = afb_rpc_coder_write(&rpc_coder, ref, (uint32_t)strlen(ref));
	ck_assert_int_eq(rc, 0);

	rc = afb_rpc_coder_output_sizes(&rpc_coder, &sz);
	ck_assert_int_eq(rc, 4);
	ck_assert_int_eq(sz, 4 * (uint32_t)strlen(ref));

	rc = afb_rpc_coder_output_get_iovec(&rpc_coder, iovecs, 10);
	ck_assert_int_eq(rc, 4);

	ck_assert_int_eq(disp2_nr, 0);
	afb_rpc_coder_output_dispose(&rpc_coder);
	ck_assert_int_eq(disp2_nr, 1);
	ck_assert_ptr_eq(disp2_val[0][0], ref);
	ck_assert_ptr_eq(disp2_val[0][1], ref + 1);
	disp2_nr = 0;

	rc = afb_rpc_coder_output_sizes(&rpc_coder, &sz);
	ck_assert_int_eq(rc, 0);
	ck_assert_int_eq(sz, 0);

}

END_TEST
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
	addtcase("output");
	addtest(test_output_int);
	addtest(test_output_bufs);
	return !!srun();
}
