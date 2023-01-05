/*
 * Copyright (C) 2015-2023 IoT.bzh Company
 * Author: José Bollo <jose.bollo@iot.bzh>
 * Author: johann Gautier <johann.gautier@iot.bzh>
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

struct afb_rpc_coder;
struct afb_rpc_decoder;


enum afb_rpc_v1_msg_type
{
	afb_rpc_v1_msg_type_NONE,
	afb_rpc_v1_msg_type_call,
	afb_rpc_v1_msg_type_reply,
	afb_rpc_v1_msg_type_event_create,
	afb_rpc_v1_msg_type_event_remove,
	afb_rpc_v1_msg_type_event_subscribe,
	afb_rpc_v1_msg_type_event_unsubscribe,
	afb_rpc_v1_msg_type_event_push,
	afb_rpc_v1_msg_type_event_broadcast,
	afb_rpc_v1_msg_type_event_unexpected,
	afb_rpc_v1_msg_type_session_create,
	afb_rpc_v1_msg_type_session_remove,
	afb_rpc_v1_msg_type_token_create,
	afb_rpc_v1_msg_type_token_remove,
	afb_rpc_v1_msg_type_describe,
	afb_rpc_v1_msg_type_description
};

/* */
typedef enum afb_rpc_v1_msg_type afb_rpc_v1_msg_type_t;
typedef unsigned char afb_rpc_v1_uuid_t[16];
typedef struct afb_rpc_v1_msg afb_rpc_v1_msg_t;

typedef struct afb_rpc_v1_msg_call		afb_rpc_v1_msg_call_t;
typedef struct afb_rpc_v1_msg_reply		afb_rpc_v1_msg_reply_t;
typedef struct afb_rpc_v1_msg_event_create	afb_rpc_v1_msg_event_create_t;
typedef struct afb_rpc_v1_msg_event_remove	afb_rpc_v1_msg_event_remove_t;
typedef struct afb_rpc_v1_msg_event_unexpected	afb_rpc_v1_msg_event_unexpected_t;
typedef struct afb_rpc_v1_msg_event_subscribe	afb_rpc_v1_msg_event_subscribe_t;
typedef struct afb_rpc_v1_msg_event_unsubscribe	afb_rpc_v1_msg_event_unsubscribe_t;
typedef struct afb_rpc_v1_msg_event_push	afb_rpc_v1_msg_event_push_t;
typedef struct afb_rpc_v1_msg_event_broadcast	afb_rpc_v1_msg_event_broadcast_t;
typedef struct afb_rpc_v1_msg_session_create	afb_rpc_v1_msg_session_create_t;
typedef struct afb_rpc_v1_msg_session_remove	afb_rpc_v1_msg_session_remove_t;
typedef struct afb_rpc_v1_msg_token_create	afb_rpc_v1_msg_token_create_t;
typedef struct afb_rpc_v1_msg_token_remove	afb_rpc_v1_msg_token_remove_t;
typedef struct afb_rpc_v1_msg_describe		afb_rpc_v1_msg_describe_t;
typedef struct afb_rpc_v1_msg_description	afb_rpc_v1_msg_description_t;


/* call */
struct afb_rpc_v1_msg_call
{
	uint16_t callid;
	uint16_t sessionid;
	uint16_t tokenid;
	const char *verb;
	const char *data;
	uint32_t data_len;
	const char *user_creds;
};

/* reply */
struct afb_rpc_v1_msg_reply
{
	uint16_t callid;
	const char *data;
	uint32_t data_len;
	const char *error;
	const char *info;
};

/* create event */
struct afb_rpc_v1_msg_event_create {
	uint16_t eventid;
	const char *eventname;
};

/* remove event */
struct afb_rpc_v1_msg_event_remove {
	uint16_t eventid;

};

/* unexpected event */
struct afb_rpc_v1_msg_event_unexpected {
	uint16_t eventid;

};

/* subscribe event */
struct afb_rpc_v1_msg_event_subscribe {
	uint16_t eventid;
	uint16_t callid;
};

/* unsubscribe event */
struct afb_rpc_v1_msg_event_unsubscribe {
	uint16_t eventid;
	uint16_t callid;
};

/* push event */
struct afb_rpc_v1_msg_event_push {
	uint16_t eventid;
	const char *data;
};

/* broadcast event */
struct afb_rpc_v1_msg_event_broadcast {
	const char *name;
	const char *data;
	const afb_rpc_v1_uuid_t *uuid;
	uint8_t hop;
};

/* create session */
struct afb_rpc_v1_msg_session_create {
	uint16_t sessionid;
	const char *sessionname;

};

/* remove session */
struct afb_rpc_v1_msg_session_remove {
	uint16_t sessionid;
};

/* create token */
struct afb_rpc_v1_msg_token_create {
	uint16_t tokenid;
	const char *tokenname;

};

/* remove token */
struct afb_rpc_v1_msg_token_remove {
	uint16_t tokenid;
};

/* describe */
struct afb_rpc_v1_msg_describe {
	uint16_t descid;
};

/* description */
struct afb_rpc_v1_msg_description {
	uint16_t descid;
	const char *data;
};

struct afb_rpc_v1_msg
{
	/** type of the message */
	afb_rpc_v1_msg_type_t type;

	union {
		afb_rpc_v1_msg_call_t call;
		afb_rpc_v1_msg_reply_t reply;
		afb_rpc_v1_msg_event_create_t event_create;
		afb_rpc_v1_msg_event_remove_t event_remove;
		afb_rpc_v1_msg_event_unexpected_t event_unexpected;
		afb_rpc_v1_msg_event_subscribe_t event_subscribe;
		afb_rpc_v1_msg_event_unsubscribe_t event_unsubscribe;
		afb_rpc_v1_msg_event_push_t event_push;
		afb_rpc_v1_msg_event_broadcast_t event_broadcast;
		afb_rpc_v1_msg_session_create_t session_create;
		afb_rpc_v1_msg_session_remove_t session_remove;
		afb_rpc_v1_msg_token_create_t token_create;
		afb_rpc_v1_msg_token_remove_t token_remove;
		afb_rpc_v1_msg_describe_t describe;
		afb_rpc_v1_msg_description_t description;

	}; /* anonymous */
};

/*************************************************************************************
* coding protocol
*************************************************************************************/

extern int afb_rpc_v1_code_call(
	struct afb_rpc_coder *coder,
	uint16_t callid,
	const char *verb,
	const char *data,
	uint32_t data_size,
	uint16_t sessionid,
	uint16_t tokenid,
	const char *user_creds
);

extern int afb_rpc_v1_code_reply(struct afb_rpc_coder *coder, uint16_t callid, const char *data, uint32_t data_size, const char *error, const char *info);

extern int afb_rpc_v1_code_event_create(struct afb_rpc_coder *coder, uint16_t eventid, const char *eventname);

extern int afb_rpc_v1_code_event_remove(struct afb_rpc_coder *coder, uint16_t eventid);

extern int afb_rpc_v1_code_event_push(struct afb_rpc_coder *coder, uint16_t eventid, const char *data);

extern int afb_rpc_v1_code_event_broadcast(struct afb_rpc_coder *coder, const char *eventname, const char *data, const unsigned char uuid[16], uint8_t hop);

extern int afb_rpc_v1_code_event_unexpected(struct afb_rpc_coder *coder, uint16_t eventid);

extern int afb_rpc_v1_code_session_create(struct afb_rpc_coder *coder, uint16_t sessionid, const char *sessionstr);

extern int afb_rpc_v1_code_session_remove(struct afb_rpc_coder *coder, uint16_t sessionid);

extern int afb_rpc_v1_code_token_create(struct afb_rpc_coder *coder, uint16_t tokenid, const char *tokenstr);

extern int afb_rpc_v1_code_token_remove(struct afb_rpc_coder *coder, uint16_t tokenid);

extern int afb_rpc_v1_code_describe(struct afb_rpc_coder *coder, uint16_t descid);

extern int afb_rpc_v1_code_subscribe(struct afb_rpc_coder *coder, uint16_t callid, uint16_t eventid);

extern int afb_rpc_v1_code_unsubscribe(struct afb_rpc_coder *coder, uint16_t callid, uint16_t eventid);

extern int afb_rpc_v1_code_description(struct afb_rpc_coder *coder, uint16_t descid, const char *data);

extern int afb_rpc_v1_code(struct afb_rpc_coder *coder, const afb_rpc_v1_msg_t *msg);

/*************************************************************************************
* decoding protocol
*************************************************************************************/

extern int afb_rpc_v1_decode(struct afb_rpc_decoder *decoder, afb_rpc_v1_msg_t *msg);
