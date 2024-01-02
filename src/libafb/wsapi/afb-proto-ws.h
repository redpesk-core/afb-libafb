/*
 * Copyright (C) 2015-2024 IoT.bzh Company
 * Author: José Bollo <jose.bollo@iot.bzh>
 *
 * $RP_BEGIN_LICENSE$
 * Commercial License Usage
 *  Licensees holding valid commercial IoT.bzh licenses may use this file in
 *  accordance with the commercial license agreement provided with the
 *  Software or, alternatively, in accordance with the terms contained in
 *  a written agreement between you and The IoT.bzh Company. For licensing terms
 *  and conditions see https://www.iot.bzh/terms-conditions. For further
 *  information use the contact form at https://www.iot.bzh/contact.
 *
 * GNU General Public License Usage
 *  Alternatively, this file may be used under the terms of the GNU General
 *  Public license version 3. This license is as published by the Free Software
 *  Foundation and appearing in the file LICENSE.GPLv3 included in the packaging
 *  of this file. Please review the following information to ensure the GNU
 *  General Public License requirements will be met
 *  https://www.gnu.org/licenses/gpl-3.0.html.
 * $RP_END_LICENSE$
 */


#pragma once

#include <stdint.h>

/*
 * Defined since version 3, the value AFB_PROTO_WS_VERSION can be used to
 * track versions of afb-proto-ws. History:
 *
 * date          version      comment
 * 2018/04/09       3         introduced for bindings v3
 * 2019/11/20       4         introduced for tokens
 */
#define AFB_PROTO_WS_VERSION	4

struct afb_proto_ws;
struct afb_proto_ws_call;
struct afb_proto_ws_describe;
struct json_object;

typedef unsigned char afb_proto_ws_uuid_t[16];

struct afb_proto_ws_client_itf
{
	/* can't be NULL */
	void (*on_reply)(void *closure, void *request, struct json_object *obj, const char *error, const char *info);

	/* can be NULL */
	void (*on_event_create)(void *closure, uint16_t event_id, const char *event_name);
	void (*on_event_remove)(void *closure, uint16_t event_id);
	void (*on_event_subscribe)(void *closure, void *request, uint16_t event_id);
	void (*on_event_unsubscribe)(void *closure, void *request, uint16_t event_id);
	void (*on_event_push)(void *closure, uint16_t event_id, struct json_object *data);
	void (*on_event_broadcast)(void *closure, const char *event_name, struct json_object *data, const afb_proto_ws_uuid_t uuid, uint8_t hop);
};

struct afb_proto_ws_server_itf
{
	void (*on_session_create)(void *closure, uint16_t sessionid, const char *sessionstr);
	void (*on_session_remove)(void *closure, uint16_t sessionid);
	void (*on_token_create)(void *closure, uint16_t tokenid, const char *tokenstr);
	void (*on_token_remove)(void *closure, uint16_t tokenid);
	void (*on_call)(void *closure, struct afb_proto_ws_call *call, const char *verb, struct json_object *args, uint16_t sessionid, uint16_t tokenid, const char *user_creds);
	void (*on_describe)(void *closure, struct afb_proto_ws_describe *describe);
	void (*on_event_unexpected)(void *closure, uint16_t eventid);
};

extern struct afb_proto_ws *afb_proto_ws_create_client(int fd, int autoclose, const struct afb_proto_ws_client_itf *itf, void *closure);
extern struct afb_proto_ws *afb_proto_ws_create_server(int fd, int autoclose, const struct afb_proto_ws_server_itf *itf, void *closure);

extern void afb_proto_ws_unref(struct afb_proto_ws *protows);
extern void afb_proto_ws_addref(struct afb_proto_ws *protows);

extern int afb_proto_ws_is_client(struct afb_proto_ws *protows);
extern int afb_proto_ws_is_server(struct afb_proto_ws *protows);

extern void afb_proto_ws_hangup(struct afb_proto_ws *protows);

extern void afb_proto_ws_on_hangup(struct afb_proto_ws *protows, void (*on_hangup)(void *closure));
extern void afb_proto_ws_set_queuing(struct afb_proto_ws *protows, int (*queuing)(struct afb_proto_ws *, void (*)(int,void*), void*));


extern int afb_proto_ws_client_session_create(struct afb_proto_ws *protows, uint16_t sessionid, const char *sessionstr);
extern int afb_proto_ws_client_session_remove(struct afb_proto_ws *protows, uint16_t sessionid);
extern int afb_proto_ws_client_token_create(struct afb_proto_ws *protows, uint16_t tokenid, const char *tokenstr);
extern int afb_proto_ws_client_token_remove(struct afb_proto_ws *protows, uint16_t tokenid);
extern int afb_proto_ws_client_call(struct afb_proto_ws *protows, const char *verb, struct json_object *args, uint16_t sessionid, uint16_t tokenid, void *request, const char *user_creds);
extern int afb_proto_ws_client_describe(struct afb_proto_ws *protows, void (*callback)(void*, struct json_object*), void *closure);
extern int afb_proto_ws_client_event_unexpected(struct afb_proto_ws *protows, uint16_t eventid);

extern int afb_proto_ws_server_event_create(struct afb_proto_ws *protows, uint16_t event_id, const char *event_name);
extern int afb_proto_ws_server_event_remove(struct afb_proto_ws *protows, uint16_t event_id);
extern int afb_proto_ws_server_event_push(struct afb_proto_ws *protows, uint16_t event_id, struct json_object *data);
extern int afb_proto_ws_server_event_broadcast(struct afb_proto_ws *protows, const char *event_name, struct json_object *data, const afb_proto_ws_uuid_t uuid, uint8_t hop);

extern void afb_proto_ws_call_addref(struct afb_proto_ws_call *call);
extern void afb_proto_ws_call_unref(struct afb_proto_ws_call *call);

extern int afb_proto_ws_call_reply(struct afb_proto_ws_call *call, struct json_object *obj, const char *error, const char *info);

extern int afb_proto_ws_call_subscribe(struct afb_proto_ws_call *call, uint16_t event_id);
extern int afb_proto_ws_call_unsubscribe(struct afb_proto_ws_call *call, uint16_t event_id);

extern int afb_proto_ws_describe_put(struct afb_proto_ws_describe *describe, struct json_object *description);
