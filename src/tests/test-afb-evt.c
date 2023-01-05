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

#include "libafb-config.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <check.h>
#include <signal.h>

#include <rp-utils/rp-jsonc.h>

#include "core/afb-hook.h"
#include "core/afb-evt.h"
#include "core/afb-sched.h"
#include "sys/x-errno.h"
#include <afb/afb-event-x2.h>

#define NAME "toto"
#define PREFIX "titi"
#define FULLNAME "titi/toto"
#define NB_LISTENER 3

#define push_mask 0x01
#define broadcast_mask 0x02
#define add_mask 0x04
#define remove_mask 0x08

#define TEST_EVT_MAX_COUNT 0

/**************** test callbasks ****************/

void test_ev_itf_push_cb(void *closure, const struct afb_evt_pushed *event){
    int * val = closure;
    *val = *val | push_mask;
    fprintf(stderr, "test_ev_itf_push_cb : closure = %d\n", *val);
}

void test_ev_itf_broadcast_cb(void *closure, const struct afb_evt_broadcasted *event){
    int * val = closure;
    *val |= broadcast_mask;
    fprintf(stderr, "test_ev_itf_broadcast_cb : closure = %d\n", *val);
}

void test_ev_itf_add_cb(void *closure, const char *event, uint16_t evtid){
    int * val = closure;
    *val |= add_mask;
    fprintf(stderr, "test_ev_itf_add_cb : closure = %d, event = %s, id = %d\n", *val, event, evtid);
}

void test_ev_itf_remove_cb(void *closure, const char *event, uint16_t evtid){
    int * val = closure;
    *val |= remove_mask;
    fprintf(stderr, "test_ev_itf_remove_cb : closure = %d, event = %s, id = %d\n", *val, event, evtid);
}

void test_ev_itf_rebroadcast_cb(void *closure, const struct afb_evt_broadcasted *event){
    int *val = closure;
    *val |= broadcast_mask;
    fprintf(stderr, "test_ev_itf_rebroadcast_cb : closure = %d\n", *val);
    afb_evt_rebroadcast_name_hookable(NAME, 0, NULL, event->uuid, event->hop);

}

/******************************* helpers *******************************/

void check_mask(int * tab, int mask){
    int i;
    for (i=0; i<NB_LISTENER; i++) {
        ck_assert_int_eq(tab[i], mask);
        tab[i] = 0;
    }
}

/******************************* tests *******************************/

START_TEST (test_init)
{
    struct afb_evt * evt;
    struct afb_evt * ev;

    struct afb_evt_listener * ev_listener;
    struct afb_evt_listener * rc_ev_listener;
    struct afb_evt_itf ev_itf;

    const char * rcname;

    int rc;
    int evt_id;

    fprintf(stderr, "\n******** test_intit ********\n");

    fprintf(stderr, "\n## afb_evt_create2...\n");
	rc = afb_evt_create2(&evt, PREFIX, NAME);
	fprintf(stderr, "-> rc = %d\n", rc);
	ck_assert_int_eq(rc, 0);

#if WITH_AFB_HOOK
    struct afb_hook_evt * hook_evt;
    hook_evt = afb_hook_create_evt("*", afb_hook_flags_api_all, NULL, NULL);
    ck_assert_ptr_ne(NULL, hook_evt);
    afb_evt_update_hooks();
#endif

    fprintf(stderr, "\n## afb_evt_id...\n");
	evt_id = afb_evt_id(evt);
	fprintf(stderr, "-> rc = %d\n", evt_id);
	ck_assert_int_ne(evt_id, 0);

    fprintf(stderr, "\n## afb_evt_fullname...\n");
	rcname = afb_evt_fullname(evt);
	fprintf(stderr, "-> rc = %s\n", rcname);
	ck_assert_str_eq(rcname, FULLNAME);

    fprintf(stderr, "\n## afb_evt_name...\n");
	rcname = afb_evt_name_hookable(evt);
	fprintf(stderr, "-> rcname = %s\n", rcname);
	ck_assert_str_eq(rcname, NAME);

    fprintf(stderr, "\n## afb_evt_addref...\n");
	ev = afb_evt_addref_hookable(evt);
	fprintf(stderr, "-> ev = %p\n", ev);
	ck_assert_ptr_eq(ev, evt);
    afb_evt_unref_hookable(evt);

    fprintf(stderr, "\n## afb_evt_listener_create...\n");
	ev_listener = afb_evt_listener_create(&ev_itf, NULL);
	fprintf(stderr, "-> ev_listener = %p\n", ev_listener);
	ck_assert_ptr_ne(ev_listener, NULL);

    fprintf(stderr, "\n## afb_evt_listener_addref...\n");
	rc_ev_listener = afb_evt_listener_addref(ev_listener);
	fprintf(stderr, "-> rc_ev_listener = %p\n", rc_ev_listener);
	ck_assert_ptr_eq(rc_ev_listener, ev_listener);
    afb_evt_listener_unref(rc_ev_listener);

    afb_evt_unref_hookable(evt);

    afb_evt_listener_unref(rc_ev_listener);

#if WITH_AFB_HOOK
   afb_hook_unref_evt(hook_evt);
#endif
}
END_TEST

START_TEST (test_functional)
{
    // struct afb_evt * evt;
    struct afb_evt_itf ev_itf = {
        .push = test_ev_itf_push_cb,
        .broadcast = test_ev_itf_rebroadcast_cb,
        .add = test_ev_itf_add_cb,
        .remove = test_ev_itf_remove_cb
    };
    struct afb_evt_listener * ev_listener[NB_LISTENER];
    struct afb_evt * evt;

    int cb_closure[NB_LISTENER] = {0}, rc, i;

    fprintf(stderr, "\n******** test_functionnal *******\n");

    fprintf(stderr, "\n## afb_evt_create...\n");
	rc = afb_evt_create(&evt, NAME);
	fprintf(stderr, "-> rc = %d\n", rc);
	ck_assert_int_eq(rc, 0);

#if WITH_AFB_HOOK
    struct afb_hook_evt * hook_evt;
    hook_evt = afb_hook_create_evt("*", afb_hook_flags_api_all, NULL, NULL);
    ck_assert_ptr_ne(NULL, hook_evt);
    afb_evt_update_hooks();
#endif

    fprintf(stderr, "\n## afb_evt_push...\n");
	rc = afb_evt_push_hookable(evt, 0, NULL);
	fprintf(stderr, "-> rc = %d\n", rc);
	ck_assert_int_eq(rc, 0);

    fprintf(stderr, "\n## afb_evt_listener_create...\n");

    for (i=0; i<NB_LISTENER; i++) {
        cb_closure[i] = 0;
        ev_listener[i] = afb_evt_listener_create(&ev_itf, &cb_closure[i]);
        ck_assert_ptr_ne(ev_listener[i], NULL);

        rc = afb_evt_listener_watch_evt(ev_listener[i], evt);
        fprintf(stderr, "-> rc = %d\n", rc);
        ck_assert_int_eq(rc, 0);

        afb_sched_wait_idle(1,1);
        fprintf(stderr, "-> cb_closure[%d] = %d\n", i, cb_closure[i]);
        ck_assert_int_eq(cb_closure[i], add_mask);
        cb_closure[i] = 0;
    }

    fprintf(stderr, "\n## afb_evt_push...\n");
	rc = afb_evt_push_hookable(evt, 0, NULL);
	fprintf(stderr, "-> rc = %d\n", rc);
	ck_assert_int_eq(rc, 1);
    afb_sched_wait_idle(1,1);
    check_mask(cb_closure, push_mask);

    fprintf(stderr, "\n## afb_evt_broadcast...\n");
	rc = afb_evt_broadcast_hookable(evt, 0, NULL);
	fprintf(stderr, "-> rc = %d\n", rc);
	ck_assert_int_eq(rc, 0);
    afb_sched_wait_idle(1,1);
    check_mask(cb_closure, broadcast_mask);

    fprintf(stderr, "\n## afb_evt_listener_unwatch_evt...\n");
    rc = afb_evt_listener_unwatch_evt(ev_listener[0], evt);
	fprintf(stderr, "-> rc = %d\n", rc);
	ck_assert_int_eq(rc, 0);
    afb_sched_wait_idle(1,1);
    ck_assert_int_eq(cb_closure[0], remove_mask);
    cb_closure[0] = 0;

    fprintf(stderr, "\n## afb_evt_listener_unwatch_id...\n");
    rc = afb_evt_listener_unwatch_id(ev_listener[1], afb_evt_id(evt));
	fprintf(stderr, "-> rc = %d\n", rc);
	ck_assert_int_eq(rc, 0);
    afb_sched_wait_idle(1,1);
    ck_assert_int_eq(cb_closure[1], remove_mask);
    cb_closure[1] = 0;

    fprintf(stderr, "\n## afb_evt_listener_unwatch_all...\n");
    afb_evt_listener_unwatch_all(ev_listener[2], 1);
	fprintf(stderr, "-> rc = %d\n", rc);
    afb_sched_wait_idle(1,1);
    ck_assert_int_eq(cb_closure[2], remove_mask);
    cb_closure[2] = 0;

    fprintf(stderr, "\n## check that unwatching again return an error...\n");

    rc = afb_evt_listener_unwatch_evt(ev_listener[0], evt);
    fprintf(stderr, "evt -> rc = %d\n", rc);
    ck_assert_int_eq(rc, X_ENOENT);

    rc = afb_evt_listener_unwatch_id(ev_listener[1], afb_evt_id(evt));
    fprintf(stderr, "id -> rc = %d\n", rc);
    ck_assert_int_eq(rc, X_ENOENT);

    rc = afb_evt_listener_unwatch_evt(ev_listener[2], evt);
    fprintf(stderr, "evt -> rc = %d\n", rc);
    ck_assert_int_eq(rc, X_ENOENT);

    fprintf(stderr, "\n## check that listeners get removed when event are deleted...\n");
	rc = afb_evt_listener_watch_evt(ev_listener[0], evt);
    ck_assert_int_eq(rc, 0);
    cb_closure[0] = 0;
    afb_evt_unref(evt);
    afb_sched_wait_idle(1,1);
    fprintf(stderr, "-> cb_closure = %d\n", cb_closure[0]);
    ck_assert_int_eq(cb_closure[0], remove_mask);

    for (i=0; i<NB_LISTENER; i++)
        afb_evt_listener_unref(ev_listener[i]);

#if WITH_AFB_HOOK
   afb_hook_unref_evt(hook_evt);
#endif

}
END_TEST

START_TEST (test_afb_event_x2)
{
    struct afb_evt * evt;
    struct afb_event_x2 * evt_x2;
    struct afb_event_x2 * rc_evt_x2;

    struct afb_evt_itf ev_itf = {
        .push = test_ev_itf_push_cb,
        .broadcast = test_ev_itf_broadcast_cb,
        .add = test_ev_itf_add_cb,
        .remove = test_ev_itf_remove_cb
    };
    struct afb_evt_listener * ev_listener;

    const char * rc_name;

    int rc;
    int cb_closure = 0;

    fprintf(stderr, "\n******** test_afb_event_x2 *******\n");

    fprintf(stderr, "\n## afb_evt_create...\n");
	rc = afb_evt_create(&evt, NAME);
	fprintf(stderr, "-> rc = %d\n", rc);
	ck_assert_int_eq(rc, 0);

#if WITH_AFB_HOOK
    struct afb_hook_evt * hook_evt;
    hook_evt = afb_hook_create_evt("*", afb_hook_flags_api_all, NULL, NULL);
    ck_assert_ptr_ne(NULL, hook_evt);
    afb_evt_update_hooks();
#endif

    fprintf(stderr, "\n## afb_evt_make_x2...\n");
    evt_x2 = afb_evt_make_x2(evt);
    fprintf(stderr, "-> evt = %p\n", evt_x2);
    ck_assert_ptr_ne(evt, NULL);

    cb_closure = 0;
    ev_listener = afb_evt_listener_create(&ev_itf, &cb_closure);
    ck_assert_ptr_ne(ev_listener, NULL);

    rc = afb_evt_listener_watch_evt(ev_listener, evt);
    fprintf(stderr, "-> rc = %d\n", rc);
    ck_assert_int_eq(rc, 0);

    afb_sched_wait_idle(1,1);
    fprintf(stderr, "-> cb_closure = %d\n", cb_closure);
    ck_assert_int_eq(cb_closure, add_mask);
    cb_closure = 0;

    fprintf(stderr, "\n## afb_event_x2_broadcast...\n");
    rc = afb_event_x2_broadcast(evt_x2, NULL);
    fprintf(stderr, "-> rc = %d\n", rc);
    ck_assert_int_eq(rc, 0);
    afb_sched_wait_idle(1,1);
    fprintf(stderr, "-> cb_closure = %d\n", cb_closure);
    ck_assert_int_eq(cb_closure, broadcast_mask);
    cb_closure = 0;

    fprintf(stderr, "\n## afb_event_x2_push...\n");
    rc = afb_event_x2_push(evt_x2, NULL);
    fprintf(stderr, "-> rc = %d\n", rc);
    ck_assert_int_eq(rc, 1);
    afb_sched_wait_idle(1,1);
    fprintf(stderr, "-> cb_closure = %d\n", cb_closure);
    ck_assert_int_eq(cb_closure, push_mask);
    cb_closure = 0;

    fprintf(stderr, "\n## afb_event_x2_name...\n");
    rc_name = afb_event_x2_name(evt_x2);
    fprintf(stderr, "-> rc = %s\n", rc_name);
    ck_assert_str_eq(rc_name, NAME);

    fprintf(stderr, "\n## afb_event_x2_addref...\n");
    rc_evt_x2 = afb_event_x2_addref(evt_x2);
    fprintf(stderr, "-> rc = %p\n", rc_evt_x2);
    ck_assert_ptr_eq(rc_evt_x2, evt_x2);

    afb_event_x2_unref(evt_x2);

}
END_TEST

#if TEST_EVT_MAX_COUNT
START_TEST (test_afb_maxcount)
{
    struct afb_evt * evt;
    char * name_n;
    int i;

    // reach max count of events
    for (i=0; i<UINT16_MAX; i++){
        ck_assert_int_ge(asprintf(&name_n, "%s%d", NAME, i), 0);
        ck_assert_int_eq(0, afb_evt_create(&evt, NAME));
    }
    ck_assert_int_ge(asprintf(&name_n, "%s%d", NAME, i), 0);
    ck_assert_int_ne(0, afb_evt_create(&evt, NAME));
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
	mksuite("afb-jobs");
		addtcase("afb-jobs");
			addtest(test_init);
            addtest(test_functional);
            addtest(test_afb_event_x2);
#if TEST_EVT_MAX_COUNT
            addtest(test_afb_maxcount);
#endif
	return !!srun();
}

