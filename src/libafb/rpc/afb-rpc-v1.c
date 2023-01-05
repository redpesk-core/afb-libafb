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

#include <stdint.h>
#include <string.h>

#include "afb-rpc-coder.h"
#include "afb-rpc-decoder.h"
#include "afb-rpc-v1.h"

/************** constants for protocol V1 *************************/

#define CHAR_FOR_CALL             'K'	/* client -> server */
#define CHAR_FOR_REPLY            'k'	/* server -> client */
#define CHAR_FOR_EVT_BROADCAST    'B'	/* server -> client */
#define CHAR_FOR_EVT_ADD          'E'	/* server -> client */
#define CHAR_FOR_EVT_DEL          'e'	/* server -> client */
#define CHAR_FOR_EVT_PUSH         'P'	/* server -> client */
#define CHAR_FOR_EVT_SUBSCRIBE    'X'	/* server -> client */
#define CHAR_FOR_EVT_UNSUBSCRIBE  'x'	/* server -> client */
#define CHAR_FOR_EVT_UNEXPECTED   'U'	/* client -> server */
#define CHAR_FOR_DESCRIBE         'D'	/* client -> server */
#define CHAR_FOR_DESCRIPTION      'd'	/* server -> client */
#define CHAR_FOR_TOKEN_ADD        'T'	/* client -> server */
#define CHAR_FOR_TOKEN_DROP       't'	/* client -> server */
#define CHAR_FOR_SESSION_ADD      'S'	/* client -> server */
#define CHAR_FOR_SESSION_DROP     's'	/* client -> server */

/*************************************************************************************
* helper functions
*************************************************************************************/

static int write_string_length(afb_rpc_coder_t *coder, const char *value, size_t length)
{
	uint32_t len = (uint32_t)++length;
	int rc = len && (size_t)len == length ? 0 : X_EINVAL;
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint32le(coder, len);
	if (rc >= 0)
		rc = afb_rpc_coder_write(coder, value, len);
	return rc;
}

static int write_string(afb_rpc_coder_t *coder, const char *value)
{
	return write_string_length(coder, value, strlen(value));
}

static int write_nullstring(afb_rpc_coder_t *coder, const char *value)
{
	return value ? write_string(coder, value) : afb_rpc_coder_write_uint32le(coder, 0);
}

static int write_binary(afb_rpc_coder_t *coder, const void *value, size_t length)
{
	int rc = length <= UINT32_MAX ? 0 : X_EINVAL;
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint32le(coder, (uint32_t)length);
	if (rc >= 0)
		rc = afb_rpc_coder_write(coder, value, (uint32_t)length);
	return rc;
}

static int write_uint8_uint16(afb_rpc_coder_t *coder, uint8_t x1, uint16_t x2)
{
	int rc = afb_rpc_coder_write_uint8(coder, x1);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint16le(coder, x2);
	return rc;
}

static int write_uint8_uint16_uint16(afb_rpc_coder_t *coder, uint8_t x1, uint16_t x2, uint16_t x3)
{
	int rc = write_uint8_uint16(coder, x1, x2);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint16le(coder, x3);
	return rc;
}

static int write_uint8_uint16_str(afb_rpc_coder_t *coder, uint8_t x1, uint16_t x2, const char *x3)
{
	int rc = write_uint8_uint16(coder, x1, x2);
	if (rc >= 0)
		rc = write_string(coder, x3);
	return rc;
}

static int readbin(afb_rpc_decoder_t *decoder, const void **value, uint32_t *length, int nulok, int isString)
{
	const void *ptr;
	uint32_t len;
	int rc;

	rc = afb_rpc_decoder_read_uint32le(decoder, &len);
	if (rc < 0)
		return rc;

	if (len) {
		rc = afb_rpc_decoder_read_pointer(decoder, &ptr, len);
		if (rc < 0)
			return rc;
	}
	else if (!nulok)
		return X_EPROTO;
	else
		ptr = 0;

	*value = ptr;
	if (len && isString && ((const char*)ptr)[--len])
		return X_EPROTO;

	if (length)
		*length = len;

	return rc;
}

static int read_string(afb_rpc_decoder_t *decoder, const char **value, uint32_t *length)
{
	return readbin(decoder, (const void **)value, length, 0, 1);
}

static int read_nullstring(afb_rpc_decoder_t *decoder, const char **value, uint32_t *length)
{
	return readbin(decoder, (const void **)value, length, 1, 1);
}

static int read_binary(afb_rpc_decoder_t *decoder, const void **value, uint32_t *length)
{
	return readbin(decoder, value, length, 1, 0);
}

/*************************************************************************************
* coding potocol
*************************************************************************************/

int afb_rpc_v1_code_call(
	afb_rpc_coder_t *coder,
	uint16_t callid,
	const char *verb,
	const char *data,
	uint32_t data_size,
	uint16_t sessionid,
	uint16_t tokenid,
	const char *user_creds
)
{
	int rc = write_uint8_uint16_str(coder, CHAR_FOR_CALL, callid, verb);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint16le(coder, sessionid);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint16le(coder, tokenid);
	if (rc >= 0)
		rc = write_binary(coder, data, data_size);
	if (rc >= 0)
		rc = write_nullstring(coder, user_creds);
	return rc;
}

int afb_rpc_v1_code_event_create(afb_rpc_coder_t *coder, uint16_t eventid, const char *eventname)
{
	return write_uint8_uint16_str(coder, CHAR_FOR_EVT_ADD, eventid, eventname);
}

int afb_rpc_v1_code_event_remove(afb_rpc_coder_t *coder, uint16_t eventid)
{
	return write_uint8_uint16(coder, CHAR_FOR_EVT_DEL, eventid);
}

int afb_rpc_v1_code_event_push(afb_rpc_coder_t *coder, uint16_t eventid, const char *data)
{
	return write_uint8_uint16_str(coder, CHAR_FOR_EVT_PUSH, eventid, data);
}

int afb_rpc_v1_code_event_broadcast(afb_rpc_coder_t *coder, const char *eventname, const char *data, const unsigned char uuid[16], uint8_t hop)
{
	int rc = afb_rpc_coder_write_uint8(coder, CHAR_FOR_EVT_BROADCAST);
	if (rc >= 0)
		rc = write_string(coder, eventname);
	if (rc >= 0)
		rc = write_nullstring(coder, data);
	if (rc >= 0)
		rc = afb_rpc_coder_write(coder, uuid, 16);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint8(coder, hop);
	return rc;
}

int afb_rpc_v1_code_event_unexpected(afb_rpc_coder_t *coder, uint16_t eventid)
{
	return write_uint8_uint16(coder, CHAR_FOR_EVT_UNEXPECTED, eventid);
}

int afb_rpc_v1_code_session_create(afb_rpc_coder_t *coder, uint16_t sessionid, const char *sessionstr)
{
	return write_uint8_uint16_str(coder, CHAR_FOR_SESSION_ADD, sessionid, sessionstr);
}

int afb_rpc_v1_code_session_remove(afb_rpc_coder_t *coder, uint16_t sessionid)
{
	return write_uint8_uint16(coder, CHAR_FOR_SESSION_DROP, sessionid);
}

int afb_rpc_v1_code_token_create(afb_rpc_coder_t *coder, uint16_t tokenid, const char *tokenstr)
{
	return write_uint8_uint16_str(coder, CHAR_FOR_TOKEN_ADD, tokenid, tokenstr);

}

int afb_rpc_v1_code_token_remove(afb_rpc_coder_t *coder, uint16_t tokenid)
{
	return write_uint8_uint16(coder, CHAR_FOR_TOKEN_DROP, tokenid);
}

int afb_rpc_v1_code_describe(afb_rpc_coder_t *coder, uint16_t descid)
{
	int rc = afb_rpc_coder_write_uint8(coder, CHAR_FOR_DESCRIBE);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint16le(coder, descid);
	return rc;
}

int afb_rpc_v1_code_reply(afb_rpc_coder_t *coder, uint16_t callid, const char *data, uint32_t data_size, const char *error, const char *info)
{
	int rc = write_uint8_uint16(coder, CHAR_FOR_REPLY, callid);
	if (rc >= 0)
		rc = write_nullstring(coder, error);
	if (rc >= 0)
		rc = write_nullstring(coder, info);
	if (rc >= 0)
		rc = write_binary(coder, data, data_size);
	return rc;
}

int afb_rpc_v1_code_subscribe(afb_rpc_coder_t *coder, uint16_t callid, uint16_t eventid)
{
	return write_uint8_uint16_uint16(coder, CHAR_FOR_EVT_SUBSCRIBE, callid, eventid);
}

int afb_rpc_v1_code_unsubscribe(afb_rpc_coder_t *coder, uint16_t callid, uint16_t eventid)
{
	return write_uint8_uint16_uint16(coder, CHAR_FOR_EVT_UNSUBSCRIBE, callid, eventid);
}

int afb_rpc_v1_code_description(afb_rpc_coder_t *coder, uint16_t descid, const char *data)
{
	return write_uint8_uint16_str(coder, CHAR_FOR_DESCRIPTION, descid, data);
}

/* callback when receiving binary data */
int afb_rpc_v1_code(afb_rpc_coder_t *coder, const struct afb_rpc_v1_msg *msg)
{
	switch(msg->type) {
	case afb_rpc_v1_msg_type_call:
		return afb_rpc_v1_code_call(coder,
				msg->call.callid,
				msg->call.verb,
				msg->call.data,
				msg->call.data_len,
				msg->call.sessionid,
				msg->call.tokenid,
				msg->call.user_creds
			);
	case afb_rpc_v1_msg_type_reply:
		return afb_rpc_v1_code_reply(coder,
				msg->reply.callid,
				msg->reply.data,
				msg->reply.data_len,
				msg->reply.error,
				msg->reply.info
			);
	case afb_rpc_v1_msg_type_event_create:
		return afb_rpc_v1_code_event_create(coder,
				msg->event_create.eventid,
				msg->event_create.eventname
			);
	case afb_rpc_v1_msg_type_event_remove:
		return afb_rpc_v1_code_event_remove(coder,
				msg->event_remove.eventid
			);
	case afb_rpc_v1_msg_type_event_subscribe:
		return afb_rpc_v1_code_subscribe(coder,
				msg->event_subscribe.callid,
				msg->event_subscribe.eventid
			);
	case afb_rpc_v1_msg_type_event_unsubscribe:
		return afb_rpc_v1_code_unsubscribe(coder,
				msg->event_unsubscribe.callid,
				msg->event_unsubscribe.eventid
			);
	case afb_rpc_v1_msg_type_event_push:
		return afb_rpc_v1_code_event_push(coder,
				msg->event_push.eventid,
				msg->event_push.data
			);
	case afb_rpc_v1_msg_type_event_broadcast:
		return afb_rpc_v1_code_event_broadcast(coder,
				msg->event_broadcast.name,
				msg->event_broadcast.data,
				*msg->event_broadcast.uuid,
				msg->event_broadcast.hop
			);
	case afb_rpc_v1_msg_type_event_unexpected:
		return afb_rpc_v1_code_event_unexpected(coder,
				msg->event_unexpected.eventid
			);
	case afb_rpc_v1_msg_type_session_create:
		return afb_rpc_v1_code_session_create(coder,
				msg->session_create.sessionid,
				msg->session_create.sessionname
			);
	case afb_rpc_v1_msg_type_session_remove:
		return afb_rpc_v1_code_session_remove(coder,
				msg->session_remove.sessionid
			);
	case afb_rpc_v1_msg_type_token_create:
		return afb_rpc_v1_code_token_create(coder,
				msg->token_create.tokenid,
				msg->token_create.tokenname
			);
	case afb_rpc_v1_msg_type_token_remove:
		return afb_rpc_v1_code_token_remove(coder,
				msg->token_remove.tokenid
			);
	case afb_rpc_v1_msg_type_describe:
		return afb_rpc_v1_code_describe(coder,
				msg->describe.descid
			);
	case afb_rpc_v1_msg_type_description:
		return afb_rpc_v1_code_description(coder,
				msg->description.descid,
				msg->description.data
			);
	default:
		return X_EINVAL;
	}
}

/*************************************************************************************
* decoding protocol
*************************************************************************************/

/* on call, propagate it to the ws service */
static int read_on_call(afb_rpc_decoder_t *decoder, struct afb_rpc_v1_msg *msg)
{
	int rc = afb_rpc_decoder_read_uint16le(decoder, &msg->call.callid);
	if (rc >= 0)
		rc = read_string(decoder, &msg->call.verb, NULL);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_uint16le(decoder, &msg->call.sessionid);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_uint16le(decoder, &msg->call.tokenid);
	if (rc >= 0)
		rc = read_binary(decoder, (const void**)&msg->call.data, &msg->call.data_len);
	if (rc >= 0)
		rc = read_nullstring(decoder, &msg->call.user_creds, NULL);
	if (rc >= 0)
		msg->type = afb_rpc_v1_msg_type_call;
	return rc;
}

static int read_on_reply(afb_rpc_decoder_t *decoder, struct afb_rpc_v1_msg *msg)
{
	int rc = afb_rpc_decoder_read_uint16le(decoder, &msg->reply.callid);
	if (rc >= 0)
		rc = read_nullstring(decoder, &msg->reply.error, NULL);
	if (rc >= 0)
		rc = read_nullstring(decoder, &msg->reply.info, NULL);
	if (rc >= 0)
		rc = read_binary(decoder, (const void**)&msg->reply.data, &msg->reply.data_len);
	if (rc >= 0)
		msg->type = afb_rpc_v1_msg_type_reply;
	return rc;
}

/* adds an event */
static int read_on_event_create(afb_rpc_decoder_t *decoder, struct afb_rpc_v1_msg *msg)
{
	int rc = afb_rpc_decoder_read_uint16le(decoder, &msg->event_create.eventid);
	if (rc >= 0)
		rc = read_string(decoder, &msg->event_create.eventname, NULL);
	if (rc >= 0)
		msg->type = afb_rpc_v1_msg_type_event_create;
	return rc;
}

/* removes an event */
static int read_on_event_remove(afb_rpc_decoder_t *decoder, struct afb_rpc_v1_msg *msg)
{
	int rc = afb_rpc_decoder_read_uint16le(decoder, &msg->event_remove.eventid);
	if (rc >= 0)
		msg->type = afb_rpc_v1_msg_type_event_remove;
	return rc;
}

/* subscribes an event */
static int read_on_event_subscribe(afb_rpc_decoder_t *decoder, struct afb_rpc_v1_msg *msg)
{
	int rc = afb_rpc_decoder_read_uint16le(decoder, &msg->event_subscribe.callid);
	if (rc >= 0)
	 	rc = afb_rpc_decoder_read_uint16le(decoder, &msg->event_subscribe.eventid);
	if (rc >= 0)
		msg->type = afb_rpc_v1_msg_type_event_subscribe;
	return rc;
}

/* unsubscribes an event */
static int read_on_event_unsubscribe(afb_rpc_decoder_t *decoder, struct afb_rpc_v1_msg *msg)
{
	int rc = afb_rpc_decoder_read_uint16le(decoder, &msg->event_unsubscribe.callid);
	if (rc >= 0)
	 	rc = afb_rpc_decoder_read_uint16le(decoder, &msg->event_unsubscribe.eventid);
	if (rc >= 0)
		msg->type = afb_rpc_v1_msg_type_event_unsubscribe;
	return rc;
}

/* pushs an event */
static int read_on_event_push(afb_rpc_decoder_t *decoder, struct afb_rpc_v1_msg *msg)
{
	int rc = afb_rpc_decoder_read_uint16le(decoder, &msg->event_push.eventid);
	if (rc >= 0)
	 	rc = read_nullstring(decoder, &msg->event_push.data, NULL);
	if (rc >= 0)
		msg->type = afb_rpc_v1_msg_type_event_push;
	return rc;
}

/* receives broadcasted events */
static int read_on_event_broadcast(afb_rpc_decoder_t *decoder, struct afb_rpc_v1_msg *msg)
{
	int rc = read_string(decoder, &msg->event_broadcast.name, NULL);
	if (rc >= 0)
		rc = read_nullstring(decoder, &msg->event_broadcast.data, NULL);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_pointer(decoder, (const void**)&msg->event_broadcast.uuid, sizeof *msg->event_broadcast.uuid);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_uint8(decoder, &msg->event_broadcast.hop);
	if (rc >= 0)
		msg->type = afb_rpc_v1_msg_type_event_broadcast;
	return rc;
}

static int read_on_event_unexpected(afb_rpc_decoder_t *decoder, struct afb_rpc_v1_msg *msg)
{
	int rc = afb_rpc_decoder_read_uint16le(decoder, &msg->event_unexpected.eventid);
	if (rc >= 0)
		msg->type = afb_rpc_v1_msg_type_event_unexpected;
	return rc;
}

/* adds an session */
static int read_on_session_create(afb_rpc_decoder_t *decoder, struct afb_rpc_v1_msg *msg)
{
	int rc = afb_rpc_decoder_read_uint16le(decoder, &msg->session_create.sessionid);
	if (rc >= 0)
		rc = read_string(decoder, &msg->session_create.sessionname, NULL);
	if (rc >= 0)
		msg->type = afb_rpc_v1_msg_type_session_create;
	return rc;
}

/* removes an session */
static int read_on_session_remove(afb_rpc_decoder_t *decoder, struct afb_rpc_v1_msg *msg)
{
	int rc = afb_rpc_decoder_read_uint16le(decoder, &msg->session_remove.sessionid);
	if (rc >= 0)
		msg->type = afb_rpc_v1_msg_type_session_remove;
	return rc;
}

/* adds an token */
static int read_on_token_create(afb_rpc_decoder_t *decoder, struct afb_rpc_v1_msg *msg)
{
	int rc = afb_rpc_decoder_read_uint16le(decoder, &msg->token_create.tokenid);
	if (rc >= 0)
		rc = read_string(decoder, &msg->token_create.tokenname, NULL);
	if (rc >= 0)
		msg->type = afb_rpc_v1_msg_type_token_create;
	return rc;
}

/* removes an token */
static int read_on_token_remove(afb_rpc_decoder_t *decoder, struct afb_rpc_v1_msg *msg)
{
	int rc = afb_rpc_decoder_read_uint16le(decoder, &msg->token_remove.tokenid);
	if (rc >= 0)
		msg->type = afb_rpc_v1_msg_type_token_remove;
	return rc;
}

static int read_on_describe(afb_rpc_decoder_t *decoder, struct afb_rpc_v1_msg *msg)
{
	int rc = afb_rpc_decoder_read_uint16le(decoder, &msg->describe.descid);
	if (rc >= 0)
		msg->type = afb_rpc_v1_msg_type_describe;
	return rc;
}

static int read_on_description(afb_rpc_decoder_t *decoder, struct afb_rpc_v1_msg *msg)
{
	int rc = afb_rpc_decoder_read_uint16le(decoder, &msg->description.descid);
	if (rc >= 0)
		rc = read_nullstring(decoder, &msg->description.data, NULL);
	if (rc >= 0)
		msg->type = afb_rpc_v1_msg_type_description;
	return rc;
}

/* callback when receiving binary data */
int afb_rpc_v1_decode(afb_rpc_decoder_t *decoder, struct afb_rpc_v1_msg *msg)
{
	uint8_t code;
	int rc;

	msg->type = afb_rpc_v1_msg_type_NONE;

	/* scan the message */
	rc = afb_rpc_decoder_read_uint8(decoder, &code);
	if (rc < 0)
		return rc;

	/* read the incoming message */
	switch (code) {
	case CHAR_FOR_CALL: /* call */
		rc = read_on_call(decoder, msg);
		break;
	case CHAR_FOR_REPLY: /* reply */
		rc = read_on_reply(decoder, msg);
		break;
	case CHAR_FOR_EVT_ADD: /* creates the event */
		rc = read_on_event_create(decoder, msg);
		break;
	case CHAR_FOR_EVT_DEL: /* removes the event */
		rc = read_on_event_remove(decoder, msg);
		break;
	case CHAR_FOR_EVT_SUBSCRIBE: /* subscribe event for a request */
		rc = read_on_event_subscribe(decoder, msg);
		break;
	case CHAR_FOR_EVT_UNSUBSCRIBE: /* unsubscribe event for a request */
		rc = read_on_event_unsubscribe(decoder, msg);
		break;
	case CHAR_FOR_EVT_PUSH: /* pushs the event */
		rc = read_on_event_push(decoder, msg);
		break;
	case CHAR_FOR_EVT_BROADCAST: /* broadcast */
		rc = read_on_event_broadcast(decoder, msg);
		break;
	case CHAR_FOR_EVT_UNEXPECTED: /* unexpected event */
		rc = read_on_event_unexpected(decoder, msg);
		break;
	case CHAR_FOR_SESSION_ADD: /* create a session */
		rc = read_on_session_create(decoder, msg);
		break;
	case CHAR_FOR_SESSION_DROP: /* remove a session */
		rc = read_on_session_remove(decoder, msg);
		break;
	case CHAR_FOR_TOKEN_ADD: /* create a token */
		rc = read_on_token_create(decoder, msg);
		break;
	case CHAR_FOR_TOKEN_DROP: /* remove a token */
		rc = read_on_token_remove(decoder, msg);
		break;
	case CHAR_FOR_DESCRIBE: /* require description */
		rc = read_on_describe(decoder, msg);
		break;
	case CHAR_FOR_DESCRIPTION: /* description */
		rc = read_on_description(decoder, msg);
		break;
	default: /* unexpected message */
		rc = X_EPROTO;
		break;
	}
	return rc;
}

