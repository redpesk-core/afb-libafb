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
#include <string.h>
#include <limits.h>

#include <check.h>

#include "core/afb-session.h"
#include "core/afb-hook.h"
#include "sys/x-errno.h"

#define GOOD_UUID  "123456789012345678901234567890123456"
#define BAD_UUID   "1234567890123456789012345678901234567"

/*********************************************************************/
/* check the initialisation */
START_TEST (check_initialisation)
{
	ck_assert_int_eq(0, afb_session_init(0, 0));
	ck_assert_int_eq(0, afb_session_init(200, 0));
}
END_TEST

/*********************************************************************/
/* check that NULL is a valid value for addref/unref */
START_TEST (check_sanity)
{
	struct afb_session *s;
	s = afb_session_addref(NULL);
	ck_assert(!s);
	afb_session_unref(NULL);
	ck_assert(1);
}
END_TEST

/*********************************************************************/
/* check creation and retrieval of sessions */
START_TEST (check_creation)
{
	char *uuid;
	struct afb_session *s, *x;
	int rc;

	/* init */
	ck_assert_int_eq(0, afb_session_init(10, 3600));

	/* create a session */
	rc = afb_session_create(&s, AFB_SESSION_TIMEOUT_DEFAULT);
	ck_assert_int_eq(rc, 0);
	ck_assert(s);

	/* the session is valid */
	ck_assert(afb_session_uuid(s) != NULL);
	ck_assert(!afb_session_is_closed(s));

	/* query the session */
	uuid = strdup(afb_session_uuid(s));
	x = afb_session_search(uuid);
	ck_assert(x == s);

	/* still alive after search */
	afb_session_unref(x);
	afb_session_unref(s);
	s = afb_session_search(uuid);
	ck_assert(s);
	ck_assert(x == s);

	/* but not after closing */
	afb_session_close(s);
	ck_assert(afb_session_is_closed(s));
	afb_session_unref(s);
	afb_session_purge();
	s = afb_session_search(uuid);
	ck_assert(!s);
	free(uuid);
}
END_TEST

/*********************************************************************/
/* check that the maximum capacity is ensured */

#define SESSION_COUNT_MIN 5

START_TEST (check_capacity)
{
	int i, rc;
	struct afb_session *s[1 + SESSION_COUNT_MIN];
	ck_assert_int_eq(0, afb_session_init(SESSION_COUNT_MIN, 3600));

	/* creation ok until count set */
	for (i = 0 ; i < SESSION_COUNT_MIN ; i++) {
		rc = afb_session_create(&s[i], AFB_SESSION_TIMEOUT_DEFAULT);
		ck_assert_int_eq(rc, 0);
		ck_assert(s[i]);
	}
	/* not ok the +1th */
	rc = afb_session_create(&s[i], AFB_SESSION_TIMEOUT_DEFAULT);
	ck_assert_int_lt(rc, 0);
	ck_assert(!s[i]);

	afb_session_close(s[0]);
	afb_session_unref(s[0]);
	rc = afb_session_create(&s[i], AFB_SESSION_TIMEOUT_DEFAULT);
	ck_assert_int_eq(rc, 0);
	ck_assert(s[i]);
	rc = afb_session_create(&s[0], AFB_SESSION_TIMEOUT_DEFAULT);
	ck_assert_int_lt(rc, 0);
	ck_assert(!s[0]);

	while (i) {
		afb_session_unref(s[i--]);
	}
}
END_TEST

/*********************************************************************/
/* check the handling of cookies */
void *freecookie_got;
void freecookie(void *item)
{
	freecookie_got = item;
}

START_TEST (check_cookies)
{
	char *k[] = { "key1", "key2", "key3", NULL }, *p, *q, *d = "default";
	struct afb_session *s;
	int i, j, r;

	/* init */
	ck_assert_int_eq(0, afb_session_init(10, 3600));

	/* create a session */
	r = afb_session_create(&s, AFB_SESSION_TIMEOUT_DEFAULT);
	ck_assert_int_eq(r, 0);
	ck_assert(s);

	/* set the cookie */
	for (i = 0 ; k[i] ; i++) {
		for (j = 0 ; k[j] ; j++) {
			/* retrieve the previous value */
			freecookie_got = NULL;
			if (i == 0) {
				/* never set (i = 0) */
				r = afb_session_cookie_exists(s, k[j]);
				ck_assert(r == 0);
				r = afb_session_cookie_get(s, k[j], (void**)&p);
				ck_assert(r < 0);
				ck_assert(p == NULL);
				r = afb_session_cookie_getinit(s, k[j], (void**)&q, NULL, d);
				ck_assert(r == 1);
				ck_assert(q == d);
				r = afb_session_cookie_getinit(s, k[j], (void**)&q, NULL, d);
				ck_assert(r == 0);
				ck_assert(q == d);
				r = afb_session_cookie_set(s, k[j], NULL, NULL, NULL);
				ck_assert(!p);
			}
			else {
				r = afb_session_cookie_exists(s, k[j]);
				ck_assert(r == 1);
				r = afb_session_cookie_get(s, k[j], (void**)&p);
				ck_assert(r == 0);
			}
			afb_session_cookie_set(s, k[j], k[i], freecookie, k[i]);
			ck_assert(freecookie_got == p);
		}
	}

	/* drop cookies */
	for (i = 1 ; k[i] ; i++) {
		freecookie_got = NULL;
		r = afb_session_cookie_get(s, k[i], (void**)&p);
		ck_assert(r == 0);
		ck_assert(!freecookie_got);
		r = afb_session_cookie_delete(s, k[i]);
		ck_assert(r == 0);
		ck_assert(freecookie_got == p);
	}

	/* closing session */
	r = afb_session_cookie_get(s, k[0], (void**)&p);
	ck_assert(r == 0);
	freecookie_got = NULL;
	afb_session_close(s);
	ck_assert(freecookie_got == p);
	r = afb_session_cookie_get(s, k[0], (void**)&p);
	ck_assert(r < 0);
	afb_session_unref(s);
}
END_TEST


/*********************************************************************/
/* check the handling of LOA */

START_TEST (check_LOA)
{
	char *k[] = { "key1", "key2", "key3", NULL };
	struct afb_session *s;
	int i, j, r;

	/* init */
	ck_assert_int_eq(0, afb_session_init(10, 3600));

	/* create a session */
	r = afb_session_create(&s, AFB_SESSION_TIMEOUT_DEFAULT);
	ck_assert_int_eq(r, 0);
	ck_assert(s);

	/* special case of loa==0 */
	for (i = 0 ; k[i] ; i++) {
		ck_assert_int_eq(0, afb_session_get_loa(s, k[i]));
		ck_assert_int_eq(0, afb_session_set_loa(s, k[i], 0));
		ck_assert_int_eq(0, afb_session_get_loa(s, k[i]));
	}

	/* set the key/loa */
	for (i = 0 ; k[i] ; i++) {
		j = 0;
		while (j < 7) {
			ck_assert_int_eq(j, afb_session_get_loa(s, k[i]));
			j++;
			ck_assert_int_eq(j, afb_session_set_loa(s, k[i], j));
		}
		while (j <= (INT_MAX >> 2)) {
			ck_assert_int_eq(j, afb_session_get_loa(s, k[i]));
			j <<= 1;
			ck_assert_int_eq(j, afb_session_set_loa(s, k[i], j));
		}
		ck_assert_int_eq(j, afb_session_get_loa(s, k[i]));
	}

	/* set the loa/key */
	while (j) {
		for (i = 0 ; k[i] ; i++)
			ck_assert_int_eq(j, afb_session_get_loa(s, k[i]));
		j >>= 1;
		for (i = 0 ; k[i] ; i++)
			ck_assert_int_eq(j, afb_session_set_loa(s, k[i], j));
	}

	/* special case of loa==0 */
	for (i = 0 ; k[i] ; i++)
		ck_assert_int_eq(0, afb_session_set_loa(s, k[i], 0));

	/* closing session */
	afb_session_unref(s);
}
END_TEST


/*********************************************************************/
/* check the handling of LOA */

START_TEST (check_drop)
{
	void *p, *key = NULL;
	struct afb_session *s;
	int r;

	/* init */
	ck_assert_int_eq(0, afb_session_init(10, 3600));

	/* create a session */
	r = afb_session_create(&s, AFB_SESSION_TIMEOUT_DEFAULT);
	ck_assert_int_eq(r, 0);
	ck_assert(s);

	/* set LOA */
	ck_assert_int_eq(4, afb_session_set_loa(s, key, 4));
	ck_assert_int_eq(4, afb_session_get_loa(s, key));

	/* set a cookie */
	freecookie_got = NULL;
	afb_session_cookie_set(s, key, (void*)check_drop, freecookie, (void*)check_drop);
	ck_assert(freecookie_got == NULL);

	/* check state */
	afb_session_cookie_get(s, key, (void**)&p);
	ck_assert(p == check_drop);
	ck_assert(freecookie_got == NULL);
	ck_assert_int_eq(4, afb_session_get_loa(s, key));

	/* drop key content */
	afb_session_drop_key(s, key);
	ck_assert(freecookie_got == check_drop);

	/* check new state */
	ck_assert_int_eq(0, afb_session_get_loa(s, key));
	r = afb_session_cookie_exists(s, key);
	ck_assert(r == 0);

	/* closing session */
	afb_session_unref(s);
}
END_TEST


/*********************************************************************/
/* check hooking */

#if WITH_AFB_HOOK
int hookflag;

void on_create(void *closure, const struct afb_hookid *hookid, struct afb_session *session)
{
	hookflag |= afb_hook_flag_session_create;
}

void on_close(void *closure, const struct afb_hookid *hookid, struct afb_session *session)
{
	hookflag |= afb_hook_flag_session_close;
}

void on_destroy(void *closure, const struct afb_hookid *hookid, struct afb_session *session)
{
	hookflag |= afb_hook_flag_session_destroy;
}

void on_addref(void *closure, const struct afb_hookid *hookid, struct afb_session *session)
{
	hookflag |= afb_hook_flag_session_addref;
}

void on_unref(void *closure, const struct afb_hookid *hookid, struct afb_session *session)
{
	hookflag |= afb_hook_flag_session_unref;
}

struct afb_hook_session_itf hookitf = {
	.hook_session_create = on_create,
	.hook_session_close = on_close,
	.hook_session_destroy = on_destroy,
	.hook_session_addref = on_addref,
	.hook_session_unref = on_unref
};

START_TEST (check_hooking)
{
	struct afb_hook_session *hs;
	struct afb_session *s;
	int r;

	/* init */
	ck_assert_int_eq(0, afb_session_init(10, 3600));

	/* create the hooking */
	hs = afb_hook_create_session(NULL, afb_hook_flags_session_all, &hookitf, NULL);
	ck_assert_ptr_ne(hs, 0);

	/* create a session */
	hookflag = 0;
	r = afb_session_create(&s, AFB_SESSION_TIMEOUT_DEFAULT);
	ck_assert_int_eq(r, 0);
	ck_assert_ptr_ne(s, 0);
	ck_assert_int_eq(hookflag, afb_hook_flag_session_create);

	/* addref session */
	hookflag = 0;
	afb_session_addref(s);
	ck_assert_int_eq(hookflag, afb_hook_flag_session_addref);

	/* unref session */
	hookflag = 0;
	afb_session_unref(s);
	ck_assert_int_eq(hookflag, afb_hook_flag_session_unref);

	/* close session */
	hookflag = 0;
	afb_session_close(s);
	ck_assert_int_eq(hookflag, afb_hook_flag_session_close);

	/* unref session */
	hookflag = 0;
	afb_session_unref(s);
	ck_assert_int_eq(hookflag, afb_hook_flag_session_unref);

	/* purge */
	hookflag = 0;
	afb_session_purge();
	ck_assert_int_eq(hookflag, afb_hook_flag_session_destroy);

	/* drop hooks */
	hookflag = 0;
	afb_hook_unref_session(hs);
	r = afb_session_create(&s, AFB_SESSION_TIMEOUT_DEFAULT);
	ck_assert_int_eq(r, 0);
	ck_assert_ptr_ne(s, 0);
	ck_assert_int_eq(hookflag, 0);
	afb_session_unref(s);
	ck_assert_int_eq(hookflag, 0);
}
END_TEST

#endif

/*********************************************************************/


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
	mksuite("session");
		addtcase("session");
			addtest(check_initialisation);
			addtest(check_sanity);
			addtest(check_creation);
			addtest(check_capacity);
		addtcase("cookie");
			addtest(check_cookies);
			addtest(check_LOA);
			addtest(check_drop);
#if WITH_AFB_HOOK
		addtcase("hooking");
			addtest(check_hooking);
#endif
	return !!srun();
}
