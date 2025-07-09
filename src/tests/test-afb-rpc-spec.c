/*
 Copyright (C) 2015-2025 IoT.bzh Company

 Author: Louis-Baptiste Sobolewski <lb.sobolewski@iot.bzh>

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

#include <check.h>

#include "rpc/afb-rpc-spec.h"

/**
 * Checks that afb_uri_api_name(__uri) is equal to __want and frees the result of afb_uri_api_name
 *
 * @param __uri full sockspec URI from which to extract the API name
 * @param __want correct API name in __uri
 */
#define S(x) ((const char*)(x ?: "(null)"))
#define CHECK_SPEC(imports, exports, out)      \
  do {                                         \
    struct afb_rpc_spec *spc;                  \
    char *str;                                 \
    size_t sz;                                 \
    int rc;                                    \
    printf("in: %s\nex: %s\n", S(imports), S(exports)); \
    rc = afb_rpc_spec_make(&spc, imports, exports); \
    printf("rc: %d\n", rc);                    \
    ck_assert_int_eq(rc, 0);                   \
    str = afb_rpc_spec_dump(spc, &sz);         \
    printf("dp: %s\nsz: %u\n\n", str, (unsigned)sz); \
    ck_assert_str_eq(str, out);                \
    ck_assert_uint_eq(sz, strlen(str));        \
    free(str);                                 \
    afb_rpc_spec_unref(spc);                    \
  } while(0)

START_TEST(test)
{
    CHECK_SPEC(NULL, NULL, "NULL");

    CHECK_SPEC("*", NULL, "import=*");
    CHECK_SPEC("api@*", NULL, "import=api@*");
    CHECK_SPEC("api@", NULL, "import=api@");
    CHECK_SPEC("api", NULL, "import=api");
    CHECK_SPEC("api,xxx", NULL, "import=api,xxx");
    CHECK_SPEC("api,xxx,yyy", NULL, "import=api,xxx,yyy");
    CHECK_SPEC("api@api-bis", NULL, "import=api@api-bis");
    CHECK_SPEC("api@api-bis,xxx@xxx-bis,*", NULL, "import=*,api@api-bis,xxx@xxx-bis");

    CHECK_SPEC(NULL, "*", "export=*");
    CHECK_SPEC(NULL, "api@*", "export=api@*");
    CHECK_SPEC(NULL, "api@", "export=api@");
    CHECK_SPEC(NULL, "api", "export=api");
    CHECK_SPEC(NULL, "api,xxx", "export=api,xxx");
    CHECK_SPEC(NULL, "api,xxx,yyy", "export=api,xxx,yyy");
    CHECK_SPEC(NULL, "api@api-bis", "export=api@api-bis");
    CHECK_SPEC(NULL, "api@api-bis,xxx@xxx-bis,*", "export=*,api@api-bis,xxx@xxx-bis");

    CHECK_SPEC(
	"a@*,bb@ccc,dddd@eeeee,ffffff@ggggggg",
	"A@,BB@CCC,DDDD@EEEEE,FFFFFF@GGGGGGG",
	"import=a@*,bb@ccc,dddd@eeeee,ffffff@ggggggg&export=A@,BB@CCC,DDDD@EEEEE,FFFFFF@GGGGGGG");
}
END_TEST

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
    mksuite("afb-uri");
    addtcase("afb-uri");
    addtest(test);
    return !!srun();
}
