/*
 Copyright (C) 2015-2026 IoT.bzh Company

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
#include <stdarg.h>
#include <string.h>

#include <check.h>

#include <afb/afb-auth.h>
#include <afb/afb-session.h>
#include <rp-utils/rp-verbose.h>

#include "core/afb-auth.h"

typedef struct ps
{
	size_t alloc, count;
	char *buffer;
}
	ps_t;

void ps_ini(ps_t *ps)
{
	memset(ps, 0, sizeof *ps);
}

char *ps_fin(ps_t *ps)
{
	char *resu = NULL;
	if (ps->alloc > ps->count) {
		resu = ps->buffer;
		resu[ps->count] = 0;
		ps_ini(ps);
	}
	else {
		void *p = realloc(ps->buffer, ps->count + 1);
		if (p != NULL) {
			ps->buffer = p;
			ps->alloc = ps->count + 1;
			ps->count = 0;
		}
	}
	return resu;
}

void ps_put(ps_t *ps, const char *text)
{
	while(*text) {
		if (ps->count < ps->alloc)
			ps->buffer[ps->count] = *text;
		ps->count++;
		text++;
	}
}

char *auth2str(
	const struct afb_auth *auth,
	unsigned session
) {
	char *result;
	ps_t ps;
	ps_ini(&ps);
	do {
		afb_auth_put_string(auth, session, (void*)ps_put, &ps);
		result = ps_fin(&ps);
	}
	while (result == NULL);
	return result;
}

START_TEST (test)
{
	const char * expected_results[][8] = {
		{ /* session = 0 */
		    "no",
		    "check-token",
		    "loa>=2",
		    "check-token and urn:test",
		    "yes or no",
		    "yes and no",
		    "not yes",
		    "yes"
		},
		{ /* session = 1 */
		    "loa>=1 and no",
		    "loa>=1 and check-token",
		    "loa>=2",
		    "loa>=1 and check-token and urn:test",
		    "loa>=1 and (yes or no)",
		    "loa>=1 and yes and no",
		    "loa>=1 and not yes",
		    "loa>=1 and yes"
		},
		{ /* session = 2 */
		    "loa>=2 and no",
		    "loa>=2 and check-token",
		    "loa>=2",
		    "loa>=2 and check-token and urn:test",
		    "loa>=2 and (yes or no)",
		    "loa>=2 and yes and no",
		    "loa>=2 and not yes",
		    "loa>=2 and yes"
		},
		{ /* session = 3 */
		    "loa>=3 and no",
		    "loa>=3 and check-token",
		    "loa>=3",
		    "loa>=3 and check-token and urn:test",
		    "loa>=3 and (yes or no)",
		    "loa>=3 and yes and no",
		    "loa>=3 and not yes",
		    "loa>=3 and yes"
		},
		{ /* session = 4 */
		    "check-token and no",
		    "check-token",
		    "loa>=2 and check-token",
		    "check-token and urn:test",
		    "check-token and (yes or no)",
		    "check-token and yes and no",
		    "check-token and not yes",
		    "check-token and yes"
		},
		{ /* session = 5 */
		    "loa>=1 and check-token and no",
		    "loa>=1 and check-token",
		    "loa>=2 and check-token",
		    "loa>=1 and check-token and urn:test",
		    "loa>=1 and check-token and (yes or no)",
		    "loa>=1 and check-token and yes and no",
		    "loa>=1 and check-token and not yes",
		    "loa>=1 and check-token and yes"
		},
		{ /* session = 6 */
		    "loa>=2 and check-token and no",
		    "loa>=2 and check-token",
		    "loa>=2 and check-token",
		    "loa>=2 and check-token and urn:test",
		    "loa>=2 and check-token and (yes or no)",
		    "loa>=2 and check-token and yes and no",
		    "loa>=2 and check-token and not yes",
		    "loa>=2 and check-token and yes"
		},
		{ /* session = 7 */
		    "loa>=3 and check-token and no",
		    "loa>=3 and check-token",
		    "loa>=3 and check-token",
		    "loa>=3 and check-token and urn:test",
		    "loa>=3 and check-token and (yes or no)",
		    "loa>=3 and check-token and yes and no",
		    "loa>=3 and check-token and not yes",
		    "loa>=3 and check-token and yes"
		}
	};

	int i;
	struct afb_auth auth, first, next;
	char *result;
	const char *expected;
	uint32_t session;
	static const char prefix[] = "\t\t";

	auth.next = &next;
	first.type = afb_auth_Yes;
	next.type = afb_auth_No;

	// Compare generated result to expected result
	for (session=0; session<8; session++){
		fprintf(stderr, "%s{ /* session = %d */\n", prefix, (int)session);
		for(i=0; i<=7; i++){
			auth.type = i;
			switch(i){
			case afb_auth_LOA:
				auth.loa = 2;
				break;
			case afb_auth_Permission:
				auth.text = "urn:test";
				break;
			default:
				auth.first = &first;
				break;
			}
			result = auth2str(&auth, session);
			expected = expected_results[session][i];
			fprintf(stderr, "%s    \"%s\"", prefix, result);
			if(i<7) fprintf(stderr, ",");
			fprintf(stderr, "\n");
			ck_assert_str_eq(expected, result);
		}
		fprintf(stderr, "%s}", prefix);
		if(session<7) fprintf(stderr, ",");
		fprintf(stderr, "\n");
	}
}
END_TEST


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
	mksuite("afb_auth");
		addtcase("afb_auth");
			addtest(test);
	return !!srun();
}
