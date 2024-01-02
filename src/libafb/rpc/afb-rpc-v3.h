/*
 * Copyright (C) 2015-2024 IoT.bzh Company
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

/* NULL ID for any resource */
#define AFB_RPC_V3_ID_NULL                   0

/* standard verb id */
#define AFB_RPC_V3_ID_VERB_DESCRIBE          0xffff
#define AFB_RPC_V3_ID_VERB_GET_VERBIDS       0xfffe
#define AFB_RPC_V3_ID_VERB_SET_INTERFACES    0xfffd

/* standard operators */
#define AFB_RPC_V3_ID_OP_CALL_REQUEST        0xffff
#define AFB_RPC_V3_ID_OP_CALL_REPLY          0xfffe
#define AFB_RPC_V3_ID_OP_EVENT_PUSH          0xfffd
#define AFB_RPC_V3_ID_OP_EVENT_SUBSCRIBE     0xfffc
#define AFB_RPC_V3_ID_OP_EVENT_UNSUBSCRIBE   0xfffb
#define AFB_RPC_V3_ID_OP_EVENT_UNEXPECTED    0xfffa
#define AFB_RPC_V3_ID_OP_EVENT_BROADCAST     0xfff9
#define AFB_RPC_V3_ID_OP_RESOURCE_CREATE     0xfff8
#define AFB_RPC_V3_ID_OP_RESOURCE_DESTROY    0xfff7

/* standard resource kinds */
#define AFB_RPC_V3_ID_KIND_SESSION           0xffff
#define AFB_RPC_V3_ID_KIND_TOKEN             0xfffe
#define AFB_RPC_V3_ID_KIND_EVENT             0xfffd
#define AFB_RPC_V3_ID_KIND_API               0xfffc
#define AFB_RPC_V3_ID_KIND_VERB              0xfffb
#define AFB_RPC_V3_ID_KIND_TYPE              0xfffa
#define AFB_RPC_V3_ID_KIND_DATA              0xfff9
#define AFB_RPC_V3_ID_KIND_KIND              0xfff8
#define AFB_RPC_V3_ID_KIND_CREDS             0xfff7
#define AFB_RPC_V3_ID_KIND_OPERATOR          0xfff6

/* standard parameters types */
#define AFB_RPC_V3_ID_PARAM_PADDING          0x0000
#define AFB_RPC_V3_ID_PARAM_RES_ID           0xffff
#define AFB_RPC_V3_ID_PARAM_RES_PLAIN        0xfffe
#define AFB_RPC_V3_ID_PARAM_VALUE            0xfffd
#define AFB_RPC_V3_ID_PARAM_VALUE_TYPED      0xfffc
#define AFB_RPC_V3_ID_PARAM_VALUE_DATA       0xfffb
#define AFB_RPC_V3_ID_PARAM_TIMEOUT          0xfffa

/* standard data types */
#define AFB_RPC_V3_ID_TYPE_OPAQUE            0xffff
#define AFB_RPC_V3_ID_TYPE_BYTEARRAY         0xfffe
#define AFB_RPC_V3_ID_TYPE_STRINGZ           0xfffd
#define AFB_RPC_V3_ID_TYPE_JSON              0xfffc
#define AFB_RPC_V3_ID_TYPE_BOOL              0xfffb
#define AFB_RPC_V3_ID_TYPE_I8                0xfffa
#define AFB_RPC_V3_ID_TYPE_U8                0xfff9
#define AFB_RPC_V3_ID_TYPE_I16               0xfff8
#define AFB_RPC_V3_ID_TYPE_U16               0xfff7
#define AFB_RPC_V3_ID_TYPE_I32               0xfff6
#define AFB_RPC_V3_ID_TYPE_U32               0xfff5
#define AFB_RPC_V3_ID_TYPE_I64               0xfff4
#define AFB_RPC_V3_ID_TYPE_U64               0xfff3
#define AFB_RPC_V3_ID_TYPE_FLOAT             0xfff2
#define AFB_RPC_V3_ID_TYPE_DOUBLE            0xfff1

/* type of id for resources */
typedef uint16_t afb_rpc_v3_id_t;

/* type of other ids */
typedef uint16_t afb_rpc_v3_call_id_t;

/* for values */

typedef struct afb_rpc_v3_value afb_rpc_v3_value_t;

/**
 * Structure for coding values
 *
 * data != 0 && id != 0    typed value (id is a typeid)
 * data == 0 && id != 0    value of a data (id is a dataid)
 * data != 0 && id == 0    untyped value
 * data == 0 && id == 0    invalid
 */
struct afb_rpc_v3_value
{
	/** depending on the value: kindid, typeid or dataid */
	afb_rpc_v3_id_t   id;
	/** length in data */
	uint16_t          length;
	/** data */
	const void       *data;
};

/* for array of values */

typedef struct afb_rpc_v3_value_array afb_rpc_v3_value_array_t;

struct afb_rpc_v3_value_array {
	/** count of values */
	uint16_t               count;
	/** the array of values */
	afb_rpc_v3_value_t    *values;
};

/* for decoding array of values */

typedef struct afb_rpc_v3_value_array_decode afb_rpc_v3_value_array_decode_t;

struct afb_rpc_v3_value_array_decode {

	/** the array */
	afb_rpc_v3_value_array_t *array;

	/** the allocator function */
	afb_rpc_v3_value_array_t *(*allocator)(void *closure, uint16_t count);

	/** the closure of the allocator */
	void *closure;
};


/* messages for operators */

typedef struct afb_rpc_v3_msg                    afb_rpc_v3_msg_t;
typedef struct afb_rpc_v3_msg_call_request       afb_rpc_v3_msg_call_request_t;
typedef struct afb_rpc_v3_msg_call_reply         afb_rpc_v3_msg_call_reply_t;
typedef struct afb_rpc_v3_msg_event_push         afb_rpc_v3_msg_event_push_t;
typedef struct afb_rpc_v3_msg_event_subscription afb_rpc_v3_msg_event_subscribe_t;
typedef struct afb_rpc_v3_msg_event_subscription afb_rpc_v3_msg_event_unsubscribe_t;
typedef struct afb_rpc_v3_msg_event_unexpected   afb_rpc_v3_msg_event_unexpected_t;
typedef struct afb_rpc_v3_msg_event_broadcast    afb_rpc_v3_msg_event_broadcast_t;
typedef struct afb_rpc_v3_msg_resource_create    afb_rpc_v3_msg_resource_create_t;
typedef struct afb_rpc_v3_msg_resource_destroy   afb_rpc_v3_msg_resource_destroy_t;

/********* Remote procedure invocation *********/

/* call request */
struct afb_rpc_v3_msg_call_request {
	afb_rpc_v3_call_id_t   callid;
	afb_rpc_v3_value_t     verb;
	afb_rpc_v3_value_t     session;
	afb_rpc_v3_value_t     token;
	afb_rpc_v3_value_t     creds;
	uint32_t               timeout;
};

/* call reply */
struct afb_rpc_v3_msg_call_reply {
	afb_rpc_v3_call_id_t   callid;
	int32_t                status;
};

/********* Management of events *********/

/* event push */
struct afb_rpc_v3_msg_event_push {
	afb_rpc_v3_id_t      eventid;
};

/* event subscribe / unsubscribe */
struct afb_rpc_v3_msg_event_subscription {
	afb_rpc_v3_call_id_t   callid;
	afb_rpc_v3_id_t        eventid;
};

/* event unexpected */
struct afb_rpc_v3_msg_event_unexpected {
	afb_rpc_v3_id_t   eventid;
};

/* event broadcast */
typedef unsigned char afb_rpc_v3_uuid_t[16];

struct afb_rpc_v3_msg_event_broadcast {
	const afb_rpc_v3_uuid_t *uuid;
	uint8_t                  hop;
	uint16_t                 length;
	char                    *event;
};

/********* Management of resources *********/

/* resource_create */
struct afb_rpc_v3_msg_resource_create {
	afb_rpc_v3_id_t  kind;
	afb_rpc_v3_id_t  id;
	uint32_t         length;
	void            *data;
};

/* resource_destroy */
struct afb_rpc_v3_msg_resource_destroy {
	afb_rpc_v3_id_t kind;
	afb_rpc_v3_id_t id;
};

/********* messages *********/

struct afb_rpc_v3_msg
{

	/** operator of the message */
	afb_rpc_v3_id_t oper;

	union {
		afb_rpc_v3_msg_call_request_t      call_request;
		afb_rpc_v3_msg_call_reply_t        call_reply;
		afb_rpc_v3_msg_event_push_t        event_push;
		afb_rpc_v3_msg_event_subscribe_t   event_subscribe;
		afb_rpc_v3_msg_event_unsubscribe_t event_unsubscribe;
		afb_rpc_v3_msg_event_unexpected_t  event_unexpected;
		afb_rpc_v3_msg_event_broadcast_t   event_broadcast;
		afb_rpc_v3_msg_resource_create_t   resource_create;
		afb_rpc_v3_msg_resource_destroy_t  resource_destroy;
	} head;

	afb_rpc_v3_value_array_decode_t values;
};

/********* packets *********/

typedef struct afb_rpc_v3_pckt afb_rpc_v3_pckt_t;

struct afb_rpc_v3_pckt {

	/** operation */
	uint16_t operation;

	/** seqno */
	uint16_t seqno;

	/** length */
	uint32_t length;

	/** buffer */
	const void *payload;
};

/*********  *********/

/* coding protocol */

extern int afb_rpc_v3_code_call_request_body(struct afb_rpc_coder *coder, const afb_rpc_v3_msg_call_request_t *msg, const afb_rpc_v3_value_array_t *values);
extern int afb_rpc_v3_code_call_reply_body(struct afb_rpc_coder *coder, const afb_rpc_v3_msg_call_reply_t *msg, const afb_rpc_v3_value_array_t *values);
extern int afb_rpc_v3_code_event_push_body(struct afb_rpc_coder *coder, const afb_rpc_v3_msg_event_push_t *msg, const afb_rpc_v3_value_array_t *values);
extern int afb_rpc_v3_code_event_broadcast_body(struct afb_rpc_coder *coder, const afb_rpc_v3_msg_event_broadcast_t *msg, const afb_rpc_v3_value_array_t *values);
extern int afb_rpc_v3_code_resource_create_body(struct afb_rpc_coder *coder, const afb_rpc_v3_msg_resource_create_t *msg);

extern int afb_rpc_v3_code_call_request(struct afb_rpc_coder *coder, const afb_rpc_v3_msg_call_request_t *msg, const afb_rpc_v3_value_array_t *values);
extern int afb_rpc_v3_code_call_reply(struct afb_rpc_coder *coder, const afb_rpc_v3_msg_call_reply_t *msg, const afb_rpc_v3_value_array_t *values);
extern int afb_rpc_v3_code_event_push(struct afb_rpc_coder *coder, const afb_rpc_v3_msg_event_push_t *msg, const afb_rpc_v3_value_array_t *values);
extern int afb_rpc_v3_code_event_subscribe(struct afb_rpc_coder *coder, const afb_rpc_v3_msg_event_subscribe_t *msg);
extern int afb_rpc_v3_code_event_unsubscribe(struct afb_rpc_coder *coder, const afb_rpc_v3_msg_event_unsubscribe_t *msg);
extern int afb_rpc_v3_code_event_unexpected(struct afb_rpc_coder *coder, const afb_rpc_v3_msg_event_unexpected_t *msg);
extern int afb_rpc_v3_code_event_broadcast(struct afb_rpc_coder *coder, const afb_rpc_v3_msg_event_broadcast_t *msg, const afb_rpc_v3_value_array_t *values);
extern int afb_rpc_v3_code_resource_create(struct afb_rpc_coder *coder, const afb_rpc_v3_msg_resource_create_t *msg);
extern int afb_rpc_v3_code_resource_destroy(struct afb_rpc_coder *coder, const afb_rpc_v3_msg_resource_destroy_t *msg);

extern int afb_rpc_v3_code(struct afb_rpc_coder *coder, const afb_rpc_v3_msg_t *msg);

/* decoding protocol */
extern int afb_rpc_v3_decode_packet(struct afb_rpc_decoder *decoder, afb_rpc_v3_pckt_t *pckt);

/* decoding operation */
extern int afb_rpc_v3_decode_operation(afb_rpc_v3_pckt_t *pckt, afb_rpc_v3_msg_t *msg);


