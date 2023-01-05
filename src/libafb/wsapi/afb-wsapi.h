/*
 * Copyright (C) 2015-2023 IoT.bzh Company
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
 * Defined since version 1, the value AFB_WSAPI_VERSION can be used to
 * track versions of afb-wsapi. History:
 *
 * date          version      comment
 * 2020/02/24       1         introduced for asynchronous implementation
 */
#define AFB_WSAPI_VERSION	1

struct afb_wsapi;
struct afb_wsapi_call;
struct afb_wsapi_describe;
struct afb_wsapi_msg;
struct json_object;

typedef unsigned char afb_wsapi_uuid_t[16];

enum afb_wsapi_msg_type
{
	afb_wsapi_msg_type_NONE,
	afb_wsapi_msg_type_call,
	afb_wsapi_msg_type_reply,
	afb_wsapi_msg_type_event_create,
	afb_wsapi_msg_type_event_remove,
	afb_wsapi_msg_type_event_subscribe,
	afb_wsapi_msg_type_event_unsubscribe,
	afb_wsapi_msg_type_event_push,
	afb_wsapi_msg_type_event_broadcast,
	afb_wsapi_msg_type_event_unexpected,
	afb_wsapi_msg_type_session_create,
	afb_wsapi_msg_type_session_remove,
	afb_wsapi_msg_type_token_create,
	afb_wsapi_msg_type_token_remove,
	afb_wsapi_msg_type_describe,
	afb_wsapi_msg_type_description,
};

/* */
struct afb_wsapi_msg
{
	/** type of the message */
	enum afb_wsapi_msg_type type;

	union {
		/* call */
		struct {
			uint16_t sessionid;
			uint16_t tokenid;
			const char *verb;
			const char *data;
			const char *user_creds;
		} call;

		/* reply */
		struct {
			void *closure;
			const char *data;
			const char *error;
			const char *info;
		} reply;

		/* create event */
		struct {
			uint16_t eventid;
			const char *eventname;
		} event_create;

		/* remove event */
		struct {
			uint16_t eventid;

		} event_remove;

		/* unexpected event */
		struct {
			uint16_t eventid;

		} event_unexpected;

		/* subscribe event */
		struct {
			uint16_t eventid;
			void *closure;
		} event_subscribe;

		/* unsubscribe event */
		struct {
			uint16_t eventid;
			void *closure;
		} event_unsubscribe;

		/* push event */
		struct {
			uint16_t eventid;
			const char *data;
		} event_push;

		/* broadcast event */
		struct {
			const char *name;
			const char *data;
			const afb_wsapi_uuid_t *uuid;
			uint8_t hop;
		} event_broadcast;

		/* create session */
		struct {
			uint16_t sessionid;
			const char *sessionname;

		} session_create;

		/* remove session */
		struct {
			uint16_t sessionid;
		} session_remove;

		/* create token */
		struct {
			uint16_t tokenid;
			const char *tokenname;

		} token_create;

		/* remove token */
		struct {
			uint16_t tokenid;
		} token_remove;

		/* describe */
		struct {
		} describe;

		/* description */
		struct {
			void *closure;
			const char *data;
		} description;

	}; /* anonymous */
};

struct afb_wsapi_itf
{
	void (*on_hangup)(void *closure);
	void (*on_call)(void *closure, const struct afb_wsapi_msg *msg);
	void (*on_reply)(void *closure, const struct afb_wsapi_msg *msg);
	void (*on_event_create)(void *closure, const struct afb_wsapi_msg *msg);
	void (*on_event_remove)(void *closure, const struct afb_wsapi_msg *msg);
	void (*on_event_subscribe)(void *closure, const struct afb_wsapi_msg *msg);
	void (*on_event_unsubscribe)(void *closure, const struct afb_wsapi_msg *msg);
	void (*on_event_push)(void *closure, const struct afb_wsapi_msg *msg);
	void (*on_event_broadcast)(void *closure, const struct afb_wsapi_msg *msg);
	void (*on_event_unexpected)(void *closure, const struct afb_wsapi_msg *msg);
	void (*on_session_create)(void *closure, const struct afb_wsapi_msg *msg);
	void (*on_session_remove)(void *closure, const struct afb_wsapi_msg *msg);
	void (*on_token_create)(void *closure, const struct afb_wsapi_msg *msg);
	void (*on_token_remove)(void *closure, const struct afb_wsapi_msg *msg);
	void (*on_describe)(void *closure, const struct afb_wsapi_msg *msg);
	void (*on_description)(void *closure, const struct afb_wsapi_msg *msg);
};

extern int afb_wsapi_create(struct afb_wsapi **wsapi, int fd, int autoclose, const struct afb_wsapi_itf *itf, void *closure);

extern struct afb_wsapi *afb_wsapi_addref(struct afb_wsapi *wsapi);
extern void afb_wsapi_unref(struct afb_wsapi *wsapi);

extern int afb_wsapi_initiate(struct afb_wsapi *wsapi);
extern void afb_wsapi_hangup(struct afb_wsapi *wsapi);

extern int afb_wsapi_session_create(struct afb_wsapi *wsapi, uint16_t sessionid, const char *sessionname);
extern int afb_wsapi_session_remove(struct afb_wsapi *wsapi, uint16_t sessionid);

extern int afb_wsapi_token_create(struct afb_wsapi *wsapi, uint16_t tokenid, const char *tokenname);
extern int afb_wsapi_token_remove(struct afb_wsapi *wsapi, uint16_t tokenid);

extern int afb_wsapi_event_create(struct afb_wsapi *wsapi, uint16_t eventid, const char *eventname);
extern int afb_wsapi_event_remove(struct afb_wsapi *wsapi, uint16_t eventid);

extern int afb_wsapi_call_s(struct afb_wsapi *wsapi, const char *verb, const char *data, uint16_t sessionid, uint16_t tokenid, void *closure, const char *user_creds);
extern int afb_wsapi_call_j(struct afb_wsapi *wsapi, const char *verb, struct json_object *data, uint16_t sessionid, uint16_t tokenid, void *closure, const char *user_creds);
extern int afb_wsapi_describe(struct afb_wsapi *wsapi, void *closure);

extern int afb_wsapi_event_unexpected(struct afb_wsapi *wsapi, uint16_t eventid);

extern int afb_wsapi_event_push_s(struct afb_wsapi *wsapi, uint16_t eventid, const char *data);
extern int afb_wsapi_event_push_j(struct afb_wsapi *wsapi, uint16_t eventid, struct json_object *data);

extern int afb_wsapi_event_broadcast_s(struct afb_wsapi *wsapi, const char *eventname, const char *data, const afb_wsapi_uuid_t uuid, uint8_t hop);
extern int afb_wsapi_event_broadcast_j(struct afb_wsapi *wsapi, const char *eventname, struct json_object *data, const afb_wsapi_uuid_t uuid, uint8_t hop);

extern const struct afb_wsapi_msg *afb_wsapi_msg_addref(const struct afb_wsapi_msg *msg);
extern void afb_wsapi_msg_unref(const struct afb_wsapi_msg *msg);

extern int afb_wsapi_msg_reply_s(const struct afb_wsapi_msg *msg, const char *data, const char *error, const char *info);
extern int afb_wsapi_msg_reply_j(const struct afb_wsapi_msg *msg, struct json_object *obj, const char *error, const char *info);

extern int afb_wsapi_msg_subscribe(const struct afb_wsapi_msg *msg, uint16_t eventid);
extern int afb_wsapi_msg_unsubscribe(const struct afb_wsapi_msg *msg, uint16_t eventid);

extern int afb_wsapi_msg_description_s(const struct afb_wsapi_msg *msg, const char *data);
extern int afb_wsapi_msg_description_j(const struct afb_wsapi_msg *msg, struct json_object *data);

extern struct json_object *afb_wsapi_msg_json_data(const struct afb_wsapi_msg *msg);
