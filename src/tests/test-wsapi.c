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
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

#include <check.h>
#include <json-c/json.h>

#include <sys/types.h>
#include <sys/socket.h>

#define FDEV_PROVIDER

#include "wsapi/afb-wsapi.h"
#include "core/afb-ev-mgr.h"
#include "core/afb-sched.h"

/*************************** Helpers Functions ***************************/
#define NB_WSAPI 2

#define i2p(x)  ((void*)((intptr_t)(x)))
#define p2i(x)  ((int)((intptr_t)(x)))

#define SESSION_ID 465
#define SESSION_NAME "TestSession"

#define TOKEN_ID 367
#define TOKEN_NAME "TestToken"

#define USER_CREDS "totoCreds"

#define VERB "test"

#define CALL_DATA "hello"
#define CALL_CLOSURE 987

#define REPLY_DATA "hi!"
#define REPLY_ERROR "OK"
#define REPLY_INFO "this is a test reply"

#define DESCRIPTION_DATA "description_test"
#define DESCRIPTION_CLOSURE 684

#define EVENT_NAME "TestEvent"
#define EVENT_ID 478
#define EVENT_CLOSURE 531
#define EVENT_PUSH_DATA "data_to_push"
#define EVENT_BROADCAST_DATA "data_to_broadcast"
#define EVENT_BROADCAST_HOP 8
#define UUID "123456789azerty"

int cb_checksum, hangup_gcount;
json_object * globJson;

void test_cb(void * closure, const struct afb_wsapi_msg * msg){

    int rc;

    const char * str_msg = json_object_get_string(afb_wsapi_msg_json_data(msg));

    fprintf(stderr, "\ntest_cb was called : msg type = %d, closure = %d, wsapi_msg = %s\n",
        msg->type,
        p2i(closure),
        str_msg
    );

    json_object * json_data;

    switch(msg->type){

        case afb_wsapi_msg_type_NONE :
            fprintf(stderr, "\ttype : NONE\n");
        break;

        case afb_wsapi_msg_type_call :
            fprintf(stderr, "\ttype : call\n");
            fprintf(stderr, "\tsession id : %d\n", msg->call.sessionid);
            fprintf(stderr, "\ttoken id : %d\n", msg->call.tokenid);
            fprintf(stderr, "\tverb : %s\n", msg->call.verb);
            fprintf(stderr, "\tdata : %s\n", msg->call.data);
            fprintf(stderr, "\tuser creds : %s\n", msg->call.user_creds);
            ck_assert_str_eq(CALL_DATA, json_object_get_string(afb_wsapi_msg_json_data(msg)));
            ck_assert_int_eq(TOKEN_ID, msg->call.tokenid);
            ck_assert_int_eq(SESSION_ID, msg->call.sessionid);
            ck_assert_str_eq(VERB, msg->call.verb);
            ck_assert_str_eq(USER_CREDS, msg->call.user_creds);
            rc = afb_wsapi_msg_subscribe(msg, EVENT_ID);
            ck_assert_int_eq(rc, 0);
            json_data = json_object_new_string(REPLY_DATA);
            rc = afb_wsapi_msg_unsubscribe(msg, EVENT_ID);
            ck_assert_int_eq(rc, 0);
            afb_wsapi_msg_reply_j(msg, json_data, REPLY_ERROR, REPLY_INFO);
        break;

        case afb_wsapi_msg_type_reply :
            fprintf(stderr, "\ttype : reply\n");
            fprintf(stderr, "\tclosure : %d\n", p2i(msg->reply.closure));
            fprintf(stderr, "\tdata: %s\n", msg->reply.data);
            fprintf(stderr, "\terror: %s\n", msg->reply.error);
            fprintf(stderr, "\tinfo : %s\n", msg->reply.info);
            if (!msg->reply.closure) {
                ck_assert_str_eq("disconnected", msg->reply.error);
                break;
            }
            ck_assert_int_eq(CALL_CLOSURE, p2i(msg->reply.closure));
            ck_assert_str_eq(REPLY_ERROR, msg->reply.error);
            ck_assert_str_eq(REPLY_INFO, msg->reply.info);
            ck_assert_str_eq(REPLY_DATA, str_msg);
        break;

        case afb_wsapi_msg_type_event_create :
            fprintf(stderr, "\ttype : event_create\n");
            fprintf(stderr, "\tid : %d\n", msg->event_create.eventid);
            fprintf(stderr, "\tname : %s\n", msg->event_create.eventname);
            ck_assert_int_eq(EVENT_ID, msg->event_create.eventid);
            ck_assert_str_eq(EVENT_NAME, msg->event_create.eventname);
        break;

        case afb_wsapi_msg_type_event_remove :
            fprintf(stderr, "\ttype : event_remove\n");
            fprintf(stderr, "\tid : %d\n", msg->event_remove.eventid);
            ck_assert_int_eq(EVENT_ID, msg->event_remove.eventid);
        break;

        case afb_wsapi_msg_type_event_subscribe :
            fprintf(stderr, "\ttype : event_subscribe\n");
            fprintf(stderr, "\tid : %d\n", msg->event_subscribe.eventid);
            fprintf(stderr, "\tclosure : %d\n", p2i(msg->event_subscribe.closure));
            ck_assert_int_eq(EVENT_ID, msg->event_subscribe.eventid);
            ck_assert_int_eq(CALL_CLOSURE, p2i(msg->event_subscribe.closure));

        break;

        case afb_wsapi_msg_type_event_unsubscribe :
            fprintf(stderr, "\ttype : event_unsubscribe\n");
            fprintf(stderr, "\tid : %d\n", msg->event_unsubscribe.eventid);
            fprintf(stderr, "\tclosure : %d\n", p2i(msg->event_unsubscribe.closure));
            ck_assert_int_eq(EVENT_ID, msg->event_unsubscribe.eventid);
            ck_assert_int_eq(CALL_CLOSURE, p2i(msg->event_unsubscribe.closure));
        break;

        case afb_wsapi_msg_type_event_push :
            fprintf(stderr, "\ttype : event_push\n");
            fprintf(stderr, "\tid : %d\n", msg->event_push.eventid);
            fprintf(stderr, "\tdata : %s\n", msg->event_push.data);
            ck_assert_int_eq(EVENT_ID, msg->event_push.eventid);
            ck_assert_str_eq(EVENT_PUSH_DATA, str_msg);
        break;

        case afb_wsapi_msg_type_event_broadcast :
            fprintf(stderr, "\ttype : event_broadcast\n");
            fprintf(stderr, "\tname : %s\n", msg->event_broadcast.name);
            fprintf(stderr, "\tdata : %s\n", msg->event_broadcast.data);
            fprintf(stderr, "\tuuid : %s\n", (char *)msg->event_broadcast.uuid);
            fprintf(stderr, "\thop : %d\n", msg->event_broadcast.hop);
            ck_assert_str_eq(EVENT_BROADCAST_DATA, str_msg);
            ck_assert_str_eq(EVENT_NAME, msg->event_broadcast.name);
            ck_assert_str_eq((char*)msg->event_broadcast.uuid, UUID);
            ck_assert_int_eq(EVENT_BROADCAST_HOP-1, msg->event_broadcast.hop);
        break;

        case afb_wsapi_msg_type_event_unexpected :
            fprintf(stderr, "\ttype : event_unexpected\n");
            fprintf(stderr, "\tevent id : %d\n", msg->event_unexpected.eventid);
            ck_assert_int_eq(EVENT_ID, msg->event_unexpected.eventid);

            // test ref/unref of wsapi_msg
            ck_assert_ptr_eq(afb_wsapi_msg_addref(msg), msg);
            afb_wsapi_msg_unref(msg);
        break;

        case afb_wsapi_msg_type_session_create :
            fprintf(stderr, "\ttype : session_create\n");
            fprintf(stderr, "\tsession : %s\n", msg->session_create.sessionname);
            fprintf(stderr, "\tsession id : %d\n", msg->session_create.sessionid);
            ck_assert_int_eq(msg->session_create.sessionid , SESSION_ID);
            ck_assert_str_eq(msg->session_create.sessionname, SESSION_NAME);
        break;

        case afb_wsapi_msg_type_session_remove :
            fprintf(stderr, "\ttype : session_remove\n");
            fprintf(stderr, "\tsession id : %d\n", msg->session_remove.sessionid);
            ck_assert_int_eq(msg->session_remove.sessionid, SESSION_ID);
        break;

        case afb_wsapi_msg_type_token_create :
            fprintf(stderr, "\ttype : token_create\n");
            fprintf(stderr, "\tname : %s\n", msg->token_create.tokenname);
            fprintf(stderr, "\tid : %d\n", msg->token_create.tokenid);
            ck_assert_int_eq(msg->token_create.tokenid, TOKEN_ID);
            ck_assert_str_eq(msg->token_create.tokenname, TOKEN_NAME);
        break;

        case afb_wsapi_msg_type_token_remove :
            fprintf(stderr, "\ttype : token_remove\n");
            fprintf(stderr, "\tid : %d\n", TOKEN_ID);
            ck_assert_int_eq(msg->token_remove.tokenid, TOKEN_ID);
        break;

        case afb_wsapi_msg_type_describe :
            fprintf(stderr, "\ttype : describe\n");
            json_data = json_object_new_string(DESCRIPTION_DATA);
            afb_wsapi_msg_description_j(msg, json_data);
        break;

        case afb_wsapi_msg_type_description :
            fprintf(stderr, "\ttype : description\n");
            fprintf(stderr, "\tclosure : %d\n", p2i(msg->description.closure));
            fprintf(stderr, "\tdata : %s\n", msg->description.data);
            if (!msg->description.closure){
                ck_assert_ptr_eq(NULL, msg->description.data);
                break;
            }
            ck_assert_str_eq(DESCRIPTION_DATA, str_msg);
            ck_assert_int_eq(DESCRIPTION_CLOSURE, p2i(msg->description.closure));
        break;

        default:
            fprintf(stderr, "\ttype : unknown !\n");
        break;

    }

    cb_checksum += p2i(closure);

}

void test_hangup_cb(void * closure){
    fprintf(stderr, "-> wsapi %d hanging-up\n", p2i(closure));
    hangup_gcount += p2i(closure);
}

void purge_events()
{
    fprintf(stderr, "--- Purging events ---\n");
    for(;;) {
        afb_ev_mgr_prepare();
        if (afb_ev_mgr_wait(0) <= 0)
            break;
        afb_ev_mgr_dispatch();
    }
    fprintf(stderr, "----------------------\n");
}

const struct afb_wsapi_itf itf = {
    on_hangup : test_hangup_cb,
    on_call : test_cb,
    on_reply : test_cb,
    on_event_create : test_cb,
    on_event_remove : test_cb,
    on_event_subscribe : test_cb,
    on_event_unsubscribe : test_cb,
    on_event_push : test_cb,
    on_event_broadcast : test_cb,
    on_event_unexpected : test_cb,
    on_session_create : test_cb,
    on_session_remove : test_cb,
    on_token_create : test_cb,
    on_token_remove : test_cb,
    on_describe : test_cb,
    on_description : test_cb,
};

void wsapi_test_init(int fd[], struct afb_wsapi * wsapi[]){

    int rc, i;

    for (i=0; i<NB_WSAPI; i++) {

        rc = afb_wsapi_create(&wsapi[i], fd[i], 0, &itf, i2p(i+1));
        ck_assert_int_eq(rc, 0);

        rc = afb_wsapi_initiate(wsapi[i]);
        ck_assert_int_ge(rc, 0);
    }

    cb_checksum = hangup_gcount = 0;
}

void wsapi_test_hangup(struct afb_wsapi * wsapi[]){

    int i;

    for (i=0; i<NB_WSAPI; i++) {
        hangup_gcount = 0;
        fprintf(stderr, "<- Hangup wsapi %d\n", i+1);
        afb_wsapi_hangup(wsapi[i]);
        ck_assert_int_eq(hangup_gcount, i+1);
    }
}

/******************************** Tests ********************************/

void start_afb_scheduler(int signum, void* arg){

    if (signum){
        fprintf(stderr, "start_afb_scheduler receved sig %d", signum);
	afb_sched_exit(1, NULL, NULL, -1);
	return;
    }

    int rc, i=0;
	struct afb_wsapi *wsapi[NB_WSAPI];
    json_object *json_data;

    int fd[NB_WSAPI];
    socketpair(AF_UNIX, SOCK_STREAM, 0, fd);

	wsapi_test_init(fd, wsapi);

    // Test ref/unref
    ck_assert_ptr_eq(afb_wsapi_addref(wsapi[0]), wsapi[0]);
    afb_wsapi_unref(wsapi[0]);
    ck_assert_int_eq(hangup_gcount, 0);

    // test session creation
    rc = afb_wsapi_session_create(wsapi[0], SESSION_ID, SESSION_NAME);
    ck_assert_int_eq(rc, 0);

    // test token create
    rc = afb_wsapi_token_create(wsapi[0], TOKEN_ID, TOKEN_NAME);
    ck_assert_int_eq(rc, 0);

    // test call/reply
    json_data = json_object_new_string(CALL_DATA);
    rc = afb_wsapi_call_j(wsapi[0], VERB, json_data, SESSION_ID, TOKEN_ID, i2p(CALL_CLOSURE), USER_CREDS);
    ck_assert_int_eq(rc, 0);

    //test describe/description
    rc = afb_wsapi_describe(wsapi[0], i2p(DESCRIPTION_CLOSURE));
    ck_assert_int_eq(rc, 0);

    // test event
    rc = afb_wsapi_event_create(wsapi[0], EVENT_ID, EVENT_NAME);
    ck_assert_int_eq(rc, 0);

    json_data = json_object_new_string(EVENT_PUSH_DATA);
    rc = afb_wsapi_event_push_j(wsapi[0], EVENT_ID, json_data);
    ck_assert_int_eq(rc, 0);

    json_data = json_object_new_string(EVENT_BROADCAST_DATA);
    const unsigned char uuid[16] = UUID;
    rc = afb_wsapi_event_broadcast_j(wsapi[0], EVENT_NAME, json_data, uuid, EVENT_BROADCAST_HOP);
    ck_assert_int_eq(rc, 0);

    rc = afb_wsapi_event_unexpected(wsapi[0], EVENT_ID);
    ck_assert_int_eq(rc, 0);

    rc = afb_wsapi_event_remove(wsapi[0], EVENT_ID);
    ck_assert_int_eq(rc, 0);

    // test session remove
    rc = afb_wsapi_session_remove(wsapi[0], SESSION_ID);
    ck_assert_int_eq(rc, 0);

    // test token remove
    rc = afb_wsapi_token_remove(wsapi[0], TOKEN_ID);
    ck_assert_int_eq(rc, 0);

    cb_checksum = 0;
    purge_events();
    fprintf(stderr, "after purge events cb_checksum = %d\n", cb_checksum);
    ck_assert_int_eq(cb_checksum, 26);

    json_data = json_object_new_string(CALL_DATA);
    rc = afb_wsapi_call_j(wsapi[0], "coverage", json_data, SESSION_ID, TOKEN_ID, NULL, USER_CREDS);
    ck_assert_int_eq(rc, 0);

    rc = afb_wsapi_describe(wsapi[0], NULL);
    ck_assert_int_eq(rc, 0);

    cb_checksum = 0;
    for (i=0; i<NB_WSAPI; i++) {
        hangup_gcount = 0;
        fprintf(stderr, "<- Hangup wsapi %d\n", i+1);
        afb_wsapi_hangup(wsapi[i]);
        ck_assert_int_eq(hangup_gcount, i+1);
    }
    ck_assert_int_eq(cb_checksum, 2);

    afb_sched_exit(1, NULL, NULL, 0);
}


START_TEST (test)
{

    ck_assert_int_eq(afb_sched_start(1, 1, 1, start_afb_scheduler, NULL), 0);

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
	mksuite("ws-api");
		addtcase("ws-api");
			addtest(test);
	return !!srun();
}
