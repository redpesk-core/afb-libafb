/*
 Copyright (C) 2015-2024 IoT.bzh Company

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

#include <check.h>

#include "misc/afb-uri.h"

/**
 * Checks that afb_uri_api_name(__uri) is equal to __want and frees the result of afb_uri_api_name
 *
 * @param __uri full sockspec URI from which to extract the API name
 * @param __want correct API name in __uri
 */
#define CHECK_URI_API(__uri, __want) \
    res = afb_uri_api_name(__uri);   \
    ck_assert_str_eq(res, __want);   \
    free(res)

static char *res;

START_TEST(test)
{
    /* manpage examples */
    CHECK_URI_API("tcp:host:port/api", "api");
    CHECK_URI_API("unix:path/api", "api");
    CHECK_URI_API("unix:@name/api", "api");
    CHECK_URI_API("unix:@api", "api");
    CHECK_URI_API("sd:api", "api");
    CHECK_URI_API("unix:path/com-api-name?as-api=name", "name");
    CHECK_URI_API("unix:@foo?as-api=bar", "bar");

    /* other tests */
    CHECK_URI_API("unix:@api?arg=value", "api");
    CHECK_URI_API("tls+tcp:localhost:1235/helloworld?pouet=truc", "helloworld");
    CHECK_URI_API("tcp:host:port/api?key=./path/to/key.pem", "api");
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
