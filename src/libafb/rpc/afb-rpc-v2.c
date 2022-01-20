/*
 * Copyright (C) 2015-2022 IoT.bzh Company
 * Author: José Bollo <jose.bollo@iot.bzh>
 * Author: Johann Gautier <johann.gautier@iot.bzh>
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

#include "afb-rpc-decoder.h"
#include "afb-rpc-coder.h"
#include "afb-rpc-v2.h"

/************** constants for protocol V2 *************************/

#define ALIGNMENT                  (4)
#define ALIGNSZ(x)                 ((x)+((ALIGNMENT-1)&-(x)))

#define OPER_SHORTCUT_MIN          (0xff80)
#define OPER_SHORTCUT_MAX          (0xffff)
#define CAN_SHORTCUT_OPER(x)       ((x) >= OPER_SHORTCUT_MIN)
#define SHORTCUT_TYPE_TO_OPER(x)   ((uint16_t)(0xff00 + ((x)&0xff)))
#define SHORTCUT_OPER_TO_TYPE(x)   ((uint8_t)(x))

#define SZ_U16_MAX                 (65536)
#define SZ_SHORTCUT_HEADER         (1+1+2)                  /* msg, flags, length */
#define SZ_CALL_REQ_BASE           (2+2)                    /* callid, nvalues */
#define SZ_CALL_REPLY_BASE         (2+2+4)                  /* callid, nvalues, status */
#define SZ_EVENT_PUSH_BASE         (2+2)                    /* eventid, nvalues */
#define SZ_EVENT_SUB_UNSUB         (2+2)                    /* eventid, callid */
#define SZ_EVENT_UNEXPECTED        (2)                      /* eventid */
#define SZ_EVENT_BROADCAST_BASE    (2+2+16+1)               /* nvalues, length, uuid, hop */
#define SZ_RES_CREATE_BASE         (2+2)                    /* kindid, id */
#define SZ_RES_CREATE(sz)          (SZ_RES_CREATE_BASE+(sz))/* kindid, id, data */
#define SZ_RES_DESTROY             (2+2)                    /* kindid, id */

#define SZ_PARAM_BASE              (2+2)                            /* paramid, length */
#define SZ_PARAM_RES_ID            (SZ_PARAM_BASE+2+2)              /* base, kindid, id */
#define SZ_PARAM_RES_PLAIN_BASE    (SZ_PARAM_BASE+2+2)              /* base, kindid, align, data */
#define SZ_PARAM_RES_PLAIN(sz)     (SZ_PARAM_RES_PLAIN_BASE+(sz))   /* base, kindid, align, data */
#define SZ_PARAM_VALUE_BASE        (SZ_PARAM_BASE)                  /* base, data */
#define SZ_PARAM_VALUE(sz)         (SZ_PARAM_VALUE_BASE+(sz))       /* base, data */
#define SZ_PARAM_VALUE_TYPED_BASE  (SZ_PARAM_BASE+2+2)              /* base, typeid, align, data */
#define SZ_PARAM_VALUE_TYPED(sz)   (SZ_PARAM_VALUE_TYPED_BASE+(sz)) /* base, typeid, align, data */
#define SZ_PARAM_VALUE_DATA        (SZ_PARAM_BASE+2)                /* base, dataid */
#define SZ_PARAM_TIMEOUT           (SZ_PARAM_BASE+4)                /* base, timeout */

/** internal structure for reading parameters */
struct param
{
	/** the parameter type as in protocol */
	uint16_t type;
	/** kind, only for resource */
	uint16_t kind;
	/** id or dataid or typeid */
	uint16_t id;
	/** length */
	uint16_t length;
	/** data */
	const void *data;
	/** timeout */
	uint32_t timeout;
};

/*************************************************************************************
* size indicator
*************************************************************************************/

uint16_t afb_rpc_v2_size_to_indicator(uint32_t size)
{
	uint16_t r = 0;
	uint32_t s = size >> 1;
	while (s > 0xffff) {
		s >>= 1;
		r++;
	}
	r += (uint16_t)(s & 0xfff0);
	return r;
}

uint32_t afb_rpc_v2_indicator_to_size(uint16_t indicator)
{
	uint32_t r = (uint32_t)(indicator & 0xfff0) + 0x00000010;
	r <<= 1 + (indicator & 0x000f);
	return r - 1;
}

/*************************************************************************************
* coding potocol
*************************************************************************************/

static int code_shortcut_message_header(afb_rpc_coder_t *coder, uint16_t oper, uint32_t size)
{
	int rc;

	if (size >= SZ_U16_MAX - SZ_SHORTCUT_HEADER)
		rc = X_EINVAL;
	else if (oper < OPER_SHORTCUT_MIN)
		rc = X_EINVAL;
	else {
		rc = afb_rpc_coder_write_uint8(coder, SHORTCUT_OPER_TO_TYPE(oper));
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint8(coder, AFB_RPC_V2_ID_PCKT_FLAG_MSG_BEGIN|AFB_RPC_V2_ID_PCKT_FLAG_MSG_END);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, (uint16_t)(SZ_SHORTCUT_HEADER + size));
	}
	return rc;
}

static int code_shortcut_message(afb_rpc_coder_t *coder, uint16_t oper, afb_rpc_coder_t *subcoder)
{
	uint32_t length;
	int rc;

	afb_rpc_coder_output_sizes(subcoder, &length);
	rc = code_shortcut_message_header(coder, oper, length);
	if (rc >= 0)
		rc = afb_rpc_coder_write_subcoder(coder, subcoder, 0, length);
	return rc;
}

static int opt_param_resource_write(afb_rpc_coder_t *coder, const afb_rpc_v2_value_t *value, afb_rpc_v2_id_t kind)
{
	int rc = 0;
	if (value->data) {
		rc = afb_rpc_coder_write_uint16le(coder, AFB_RPC_V2_ID_PARAM_RES_PLAIN);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, SZ_PARAM_RES_PLAIN(value->length));
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, kind);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, 0/*align*/);
		if (rc >= 0)
			rc = afb_rpc_coder_write(coder, value->data, value->length);
		if (rc >= 0)
			rc = afb_rpc_coder_write_align(coder, ALIGNMENT);
	}
	else if (value->id) {
		rc = afb_rpc_coder_write_uint16le(coder, AFB_RPC_V2_ID_PARAM_RES_ID);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, SZ_PARAM_RES_ID);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, kind);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, value->id);
	}
	return rc;
}

static int opt_param_timeout_write(afb_rpc_coder_t *coder, uint32_t timeout)
{
	int rc = 0;
	if (timeout) {
		rc = afb_rpc_coder_write_uint16le(coder, AFB_RPC_V2_ID_PARAM_TIMEOUT);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, SZ_PARAM_TIMEOUT);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint32le(coder, timeout);
	}
	return rc;
}

static int param_value_write(afb_rpc_coder_t *coder, const afb_rpc_v2_value_t *value)
{
	int rc = 0;
	if (value->data && value->id) {
		rc = afb_rpc_coder_write_uint16le(coder, AFB_RPC_V2_ID_PARAM_VALUE_TYPED);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, SZ_PARAM_VALUE_TYPED(value->length));
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, value->id); /* type-id */
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, 0/*align*/);
		if (rc >= 0)
			rc = afb_rpc_coder_write(coder, value->data, value->length);
		if (rc >= 0)
			rc = afb_rpc_coder_write_align(coder, ALIGNMENT);
	}
	else if (value->id) {
		rc = afb_rpc_coder_write_uint16le(coder, AFB_RPC_V2_ID_PARAM_VALUE_DATA);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, SZ_PARAM_VALUE_DATA);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, value->id);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, 0/*align*/);
	}
	else {
		rc = afb_rpc_coder_write_uint16le(coder, AFB_RPC_V2_ID_PARAM_VALUE);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, SZ_PARAM_VALUE(value->length));
		if (rc >= 0)
			rc = afb_rpc_coder_write(coder, value->data, value->length);
		if (rc >= 0)
			rc = afb_rpc_coder_write_align(coder, ALIGNMENT);
	}
	return rc;
}

static int array_values_write(afb_rpc_coder_t *coder, const afb_rpc_v2_value_array_t *values)
{
	int rc = 0;
	uint16_t idx = 0;
	while (rc >= 0 && idx < values->count)
		rc = param_value_write(coder, &values->values[idx++]);
	return rc;
}

int afb_rpc_v2_code_call_request_body(afb_rpc_coder_t *coder, const afb_rpc_v2_msg_call_request_t *msg, const afb_rpc_v2_value_array_t *values)
{
	int rc = afb_rpc_coder_write_uint16le(coder, msg->callid);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint16le(coder, values ? values->count : 0);
	if (rc >= 0)
		rc = opt_param_resource_write(coder, &msg->verb, AFB_RPC_V2_ID_KIND_VERB);
	if (rc >= 0)
		rc = opt_param_resource_write(coder, &msg->session, AFB_RPC_V2_ID_KIND_SESSION);
	if (rc >= 0)
		rc = opt_param_resource_write(coder, &msg->token, AFB_RPC_V2_ID_KIND_TOKEN);
	if (rc >= 0)
		rc = opt_param_resource_write(coder, &msg->creds, AFB_RPC_V2_ID_KIND_CREDS);
	if (rc >= 0)
		rc = opt_param_timeout_write(coder, msg->timeout);
	if (rc >= 0 && values)
		rc = array_values_write(coder, values);

	return rc;
}

int afb_rpc_v2_code_call_reply_body(afb_rpc_coder_t *coder, const afb_rpc_v2_msg_call_reply_t *msg, const afb_rpc_v2_value_array_t *values)
{
	int rc = afb_rpc_coder_write_uint16le(coder, msg->callid);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint16le(coder, values ? values->count : 0);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint32le(coder, (uint32_t)(int32_t)msg->status);
	if (rc >= 0 && values)
		rc = array_values_write(coder, values);
	return rc;
}

int afb_rpc_v2_code_event_push_body(afb_rpc_coder_t *coder, const afb_rpc_v2_msg_event_push_t *msg, const afb_rpc_v2_value_array_t *values)
{
	int rc = afb_rpc_coder_write_uint16le(coder, msg->eventid);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint16le(coder, values ? values->count : 0);
	if (rc >= 0 && values)
		rc = array_values_write(coder, values);
	return rc;
}

int afb_rpc_v2_code_event_broadcast_body(afb_rpc_coder_t *coder, const afb_rpc_v2_msg_event_broadcast_t *msg, const afb_rpc_v2_value_array_t *values)
{
	int rc = afb_rpc_coder_write_uint16le(coder, values ? values->count : 0);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint16le(coder, msg->length);
	if (rc >= 0)
		rc = afb_rpc_coder_write(coder, msg->uuid, sizeof *msg->uuid);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint8(coder, msg->hop);
	if (rc >= 0)
		rc = afb_rpc_coder_write(coder, msg->event, msg->length);
	if (rc >= 0)
		rc = afb_rpc_coder_write_align(coder, ALIGNMENT);
	if (rc >= 0 && values)
		rc = array_values_write(coder, values);
	return rc;
}

int afb_rpc_v2_code_resource_create_body(afb_rpc_coder_t *coder, const afb_rpc_v2_msg_resource_create_t * msg)
{
	int rc = afb_rpc_coder_write_uint16le(coder, msg->kind);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint16le(coder, msg->id);
	if (rc >= 0)
		rc = afb_rpc_coder_write(coder, msg->data, msg->length);
	if (rc >= 0)
		rc = afb_rpc_coder_write_align(coder, ALIGNMENT);
	return rc;
}

int afb_rpc_v2_code_call_request(afb_rpc_coder_t *coder, const afb_rpc_v2_msg_call_request_t *msg, const afb_rpc_v2_value_array_t *values)
{
	afb_rpc_coder_t subcoder;
	int rc;

	afb_rpc_coder_init(&subcoder);
	rc = afb_rpc_v2_code_call_request_body(&subcoder, msg, values);
	if (rc >= 0)
		rc = code_shortcut_message(coder, AFB_RPC_V2_ID_OP_CALL_REQUEST, &subcoder);
	return rc;
}

int afb_rpc_v2_code_call_reply(afb_rpc_coder_t *coder, const afb_rpc_v2_msg_call_reply_t *msg, const afb_rpc_v2_value_array_t *values)
{
	afb_rpc_coder_t subcoder;
	int rc;

	afb_rpc_coder_init(&subcoder);
	rc = afb_rpc_v2_code_call_reply_body(&subcoder, msg, values);
	if (rc >= 0)
		rc = code_shortcut_message(coder, AFB_RPC_V2_ID_OP_CALL_REPLY, &subcoder);
	return rc;
}

int afb_rpc_v2_code_event_push(afb_rpc_coder_t *coder, const afb_rpc_v2_msg_event_push_t *msg, const afb_rpc_v2_value_array_t *values)
{
	afb_rpc_coder_t subcoder;
	int rc;

	afb_rpc_coder_init(&subcoder);
	rc = afb_rpc_v2_code_event_push_body(&subcoder, msg, values);
	if (rc >= 0)
		rc = code_shortcut_message(coder, AFB_RPC_V2_ID_OP_EVENT_PUSH, &subcoder);
	return rc;
}

static int code_event_subscription(afb_rpc_coder_t *coder, const struct afb_rpc_v2_msg_event_subscription *msg, afb_rpc_v2_id_t oper)
{
	int rc = code_shortcut_message_header(coder, oper, SZ_EVENT_SUB_UNSUB);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint16le(coder, msg->callid);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint16le(coder, msg->eventid);
	return rc;
}

int afb_rpc_v2_code_event_subscribe(afb_rpc_coder_t *coder, const afb_rpc_v2_msg_event_subscribe_t *msg)
{
	return code_event_subscription(coder, msg, AFB_RPC_V2_ID_OP_EVENT_SUBSCRIBE);
}

int afb_rpc_v2_code_event_unsubscribe(afb_rpc_coder_t *coder, const afb_rpc_v2_msg_event_unsubscribe_t *msg)
{
	return code_event_subscription(coder, msg, AFB_RPC_V2_ID_OP_EVENT_UNSUBSCRIBE);
}


int afb_rpc_v2_code_event_unexpected(afb_rpc_coder_t *coder, const afb_rpc_v2_msg_event_unexpected_t *msg)
{
	int rc = code_shortcut_message_header(coder, AFB_RPC_V2_ID_OP_EVENT_UNEXPECTED, SZ_EVENT_UNEXPECTED);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint16le(coder, msg->eventid);
	if (rc >= 0)
		rc = afb_rpc_coder_write_align(coder, ALIGNMENT);
	return rc;
}

int afb_rpc_v2_code_event_broadcast(afb_rpc_coder_t *coder, const afb_rpc_v2_msg_event_broadcast_t *msg, const afb_rpc_v2_value_array_t *values)
{
	afb_rpc_coder_t subcoder;
	int rc;

	afb_rpc_coder_init(&subcoder);
	rc = afb_rpc_v2_code_event_broadcast_body(&subcoder, msg, values);
	if (rc >= 0)
		rc = code_shortcut_message(coder, AFB_RPC_V2_ID_OP_EVENT_BROADCAST, &subcoder);
	return rc;
}

int afb_rpc_v2_code_resource_create(afb_rpc_coder_t *coder, const afb_rpc_v2_msg_resource_create_t * msg)
{
	afb_rpc_coder_t subcoder;
	int rc;

	afb_rpc_coder_init(&subcoder);
	rc = afb_rpc_v2_code_resource_create_body(&subcoder, msg);
	if (rc >= 0)
		rc = code_shortcut_message(coder, AFB_RPC_V2_ID_OP_RESOURCE_CREATE, &subcoder);
	return rc;
}

int afb_rpc_v2_code_resource_destroy(afb_rpc_coder_t *coder, const afb_rpc_v2_msg_resource_destroy_t * msg)
{
	int rc = code_shortcut_message_header(coder, AFB_RPC_V2_ID_OP_RESOURCE_DESTROY, SZ_RES_DESTROY);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint16le(coder, msg->kind);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint16le(coder, msg->id);
	return rc;
}

int afb_rpc_v2_code(afb_rpc_coder_t *coder, const afb_rpc_v2_msg_t * msg)
{
	int rc;

	switch (msg->oper) {
	case AFB_RPC_V2_ID_OP_CALL_REQUEST:
		rc = afb_rpc_v2_code_call_request(coder, &msg->head.call_request, msg->values.array);
		break;

	case AFB_RPC_V2_ID_OP_CALL_REPLY:
		rc = afb_rpc_v2_code_call_reply(coder, &msg->head.call_reply, msg->values.array);
		break;

	case AFB_RPC_V2_ID_OP_EVENT_PUSH:
		rc = afb_rpc_v2_code_event_push(coder, &msg->head.event_push, msg->values.array);
		break;

	case AFB_RPC_V2_ID_OP_EVENT_SUBSCRIBE:
		rc = afb_rpc_v2_code_event_subscribe(coder, &msg->head.event_subscribe);
		break;

	case AFB_RPC_V2_ID_OP_EVENT_UNSUBSCRIBE:
		rc = afb_rpc_v2_code_event_unsubscribe(coder, &msg->head.event_unsubscribe);
		break;

	case AFB_RPC_V2_ID_OP_EVENT_UNEXPECTED:
		rc = afb_rpc_v2_code_event_unexpected(coder, &msg->head.event_unexpected);
		break;

	case AFB_RPC_V2_ID_OP_EVENT_BROADCAST:
		rc = afb_rpc_v2_code_event_broadcast(coder, &msg->head.event_broadcast, msg->values.array);
		break;

	case AFB_RPC_V2_ID_OP_RESOURCE_CREATE:
		rc = afb_rpc_v2_code_resource_create(coder, &msg->head.resource_create);
		break;

	case AFB_RPC_V2_ID_OP_RESOURCE_DESTROY:
		rc = afb_rpc_v2_code_resource_destroy(coder, &msg->head.resource_destroy);
		break;

	default:
		rc = X_EPROTO;
		break;
	}
	return rc;
}

/*************************************************************************************
* decoding protocol
*************************************************************************************/

static int decode_param(afb_rpc_decoder_t *decoder, struct param *param)
{
	uint16_t length;
	int rc;

	memset(param, 0, sizeof *param);
	rc = afb_rpc_decoder_read_uint16le(decoder, &param->type);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_uint16le(decoder, &length);
	if (rc >= 0) {
		switch (param->type) {
		case AFB_RPC_V2_ID_PARAM_RES_ID:
			rc = afb_rpc_decoder_read_uint16le(decoder, &param->kind);
			if (rc >= 0)
				rc = afb_rpc_decoder_read_uint16le(decoder, &param->id);
			break;
		case AFB_RPC_V2_ID_PARAM_RES_PLAIN:
			rc = afb_rpc_decoder_read_uint16le(decoder, &param->kind);
			if (rc >= 0)
				rc = afb_rpc_decoder_skip(decoder, sizeof(uint16_t));
			if (rc >= 0) {
				param->length = length - SZ_PARAM_RES_PLAIN_BASE;
				rc = afb_rpc_decoder_read_pointer(decoder, &param->data, param->length);
			}
			break;
		case AFB_RPC_V2_ID_PARAM_VALUE:
			param->length = length - SZ_PARAM_VALUE_BASE;
			rc = afb_rpc_decoder_read_pointer(decoder, &param->data, param->length);
			break;
		case AFB_RPC_V2_ID_PARAM_VALUE_TYPED:
			rc = afb_rpc_decoder_read_uint16le(decoder, &param->id);
			if (rc >= 0)
				rc = afb_rpc_decoder_skip(decoder, sizeof(uint16_t));
			if (rc >= 0) {
				param->length = length - SZ_PARAM_VALUE_TYPED_BASE;
				rc = afb_rpc_decoder_read_pointer(decoder, &param->data, param->length);
			}
			break;
		case AFB_RPC_V2_ID_PARAM_VALUE_DATA:
			rc = afb_rpc_decoder_read_uint16le(decoder, &param->id);
			break;
		case AFB_RPC_V2_ID_PARAM_TIMEOUT:
			rc = afb_rpc_decoder_read_uint32le(decoder, &param->timeout);
			break;
		default:
			rc = afb_rpc_decoder_skip(decoder, length - SZ_PARAM_BASE);
			break;
		}
		if (rc >= 0)
			rc = afb_rpc_decoder_read_align(decoder, ALIGNMENT);
	}
	return rc;
}

static int set_request_resource(afb_rpc_v2_msg_call_request_t *msg, struct param *param)
{
	int rc;
	afb_rpc_v2_value_t *val;

	switch (param->kind) {
	case AFB_RPC_V2_ID_KIND_SESSION:
		val = &msg->session;
		break;
	case AFB_RPC_V2_ID_KIND_TOKEN:
		val = &msg->token;
		break;
	case AFB_RPC_V2_ID_KIND_VERB:
		val = &msg->verb;
		break;
	case AFB_RPC_V2_ID_KIND_CREDS:
		val = &msg->creds;
		break;
	default:
		val = 0;
		break;
	}
	if (!val)
		rc = X_EPROTO;
	else {
		val->id = param->id;
		val->length = param->length;
		val->data = param->data;
		rc = 0;
	}
	return rc;
}

static int decode_values(afb_rpc_decoder_t *decoder, uint16_t nval, afb_rpc_v2_value_array_decode_t *valdec)
{
	uint16_t ival;
	struct param param;
	afb_rpc_v2_value_array_t *values;
	int rc;

	values = valdec->array;
	if (values != NULL) {
		if (nval > values->count) {
			rc = X_ECANCELED;
			values = NULL;
		}
	}
	else if (valdec->allocator == NULL)
		rc = X_EINVAL;
	else {
		values = valdec->allocator(valdec->closure, nval);
		if (values == NULL)
			rc = X_ECANCELED;
		else
			valdec->array = values;
	}
	if (values != NULL) {
		values->count = 0;
		rc = 0;
		ival = 0;
		while (rc >= 0 && afb_rpc_decoder_remaining_size(decoder) > 0) {
			rc = decode_param(decoder, &param);
			if (rc >= 0)
				switch (param.type) {
				case AFB_RPC_V2_ID_PARAM_VALUE:
				case AFB_RPC_V2_ID_PARAM_VALUE_TYPED:
				case AFB_RPC_V2_ID_PARAM_VALUE_DATA:
					if (ival == nval)
						rc = X_EPROTO;
					else {
						values->values[ival].id = param.id;
						values->values[ival].length = param.length;
						values->values[ival].data = param.data;
						ival++;
					}
					break;
				default:
					rc = X_EPROTO;
					break;
				}
		}
		if (rc >= 0 && ival != nval)
			rc = X_EPROTO;
		else if (values)
			values->count = ival;
	}

	return rc;
}

static int decode_call_request(afb_rpc_decoder_t *decoder, afb_rpc_v2_msg_call_request_t *msg, afb_rpc_v2_value_array_decode_t *valdec)
{
	uint16_t nval;
	struct param param;
	afb_rpc_v2_value_t *val;
	afb_rpc_v2_value_array_t *values = NULL;

	int rc = afb_rpc_decoder_read_uint16le(decoder, &msg->callid);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_uint16le(decoder, &nval);
	if (rc >= 0) {
		values = valdec->array;
		if (values != NULL) {
			if (nval > values->count)
				rc = X_ECANCELED;
		}
		else if (valdec->allocator == NULL)
			rc = X_EINVAL;
		else {
			values = valdec->allocator(valdec->closure, nval);
			if (values == NULL)
				rc = X_ECANCELED;
			else
				valdec->array = values;
		}
		if (rc >= 0)
			values->count = 0;
	}
	while (rc >= 0 && afb_rpc_decoder_remaining_size(decoder) > 0) {

		rc = decode_param(decoder, &param);
		if (rc >= 0)
			switch (param.type) {
			case AFB_RPC_V2_ID_PARAM_RES_ID:
			case AFB_RPC_V2_ID_PARAM_RES_PLAIN:
				rc = set_request_resource(msg, &param);
				break;
			case AFB_RPC_V2_ID_PARAM_VALUE:
			case AFB_RPC_V2_ID_PARAM_VALUE_TYPED:
			case AFB_RPC_V2_ID_PARAM_VALUE_DATA:
				if (!values || values->count == nval)
					rc = X_EPROTO;
				else {
					val = &values->values[values->count++];
					val->id = param.id;
					val->length = param.length;
					val->data = param.data;
				}
				break;
			case AFB_RPC_V2_ID_PARAM_TIMEOUT:
				msg->timeout = param.timeout;
				break;
			default:
				rc = X_EPROTO;
				break;
			}
	}
	if (rc >= 0 && values != NULL && values->count != nval)
		rc = X_EPROTO;

	return rc;
}

static int decode_call_reply(afb_rpc_decoder_t *decoder, afb_rpc_v2_msg_call_reply_t *msg, afb_rpc_v2_value_array_decode_t *valdec)
{
	uint16_t nval;

	int rc = afb_rpc_decoder_read_uint16le(decoder, &msg->callid);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_uint16le(decoder, &nval);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_uint32le(decoder, (uint32_t*)&msg->status);
	if (rc >= 0)
		rc = decode_values(decoder, nval, valdec);
	return rc;
}

static int decode_event_push(afb_rpc_decoder_t *decoder, afb_rpc_v2_msg_event_push_t *msg, afb_rpc_v2_value_array_decode_t *valdec)
{
	uint16_t nval;

	int rc = afb_rpc_decoder_read_uint16le(decoder, &msg->eventid);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_uint16le(decoder, &nval);
	if (rc >= 0)
		rc = decode_values(decoder, nval, valdec);
	return rc;
}

static int decode_event_subscription(afb_rpc_decoder_t *decoder, struct afb_rpc_v2_msg_event_subscription *msg)
{
	int rc;
	if (afb_rpc_decoder_remaining_size(decoder) != SZ_EVENT_SUB_UNSUB)
		rc = X_EPROTO;
	else {
		rc = afb_rpc_decoder_read_uint16le(decoder, &msg->callid);
		if (rc >= 0)
			rc = afb_rpc_decoder_read_uint16le(decoder, &msg->eventid);
	}
	return rc;
}

static int decode_event_unexpected(afb_rpc_decoder_t *decoder, afb_rpc_v2_msg_event_unexpected_t *msg)
{
	int rc;

	if (afb_rpc_decoder_remaining_size(decoder) != SZ_EVENT_UNEXPECTED)
		rc = X_EPROTO;
	else {
		rc = afb_rpc_decoder_read_uint16le(decoder, &msg->eventid);
		if (rc >= 0)
			rc = afb_rpc_decoder_read_align(decoder, ALIGNMENT);
	}
	return rc;
}

static int decode_event_broadcast(afb_rpc_decoder_t *decoder, afb_rpc_v2_msg_event_broadcast_t *msg, afb_rpc_v2_value_array_decode_t *valdec)
{
	uint16_t nval;

	int rc = afb_rpc_decoder_read_uint16le(decoder, &nval);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_uint16le(decoder, &msg->length);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_pointer(decoder, (const void**)&msg->uuid, sizeof *msg->uuid);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_uint8(decoder, &msg->hop);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_pointer(decoder, (const void**)&msg->event, msg->length);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_align(decoder, ALIGNMENT);
	if (rc >= 0)
		rc = decode_values(decoder, nval, valdec);
	return rc;
}

static int decode_resource_create(afb_rpc_decoder_t *decoder, afb_rpc_v2_msg_resource_create_t *msg)
{
	int rc;

	if (afb_rpc_decoder_remaining_size(decoder) < SZ_RES_CREATE_BASE)
		rc = X_EPROTO;
	else {
		rc = afb_rpc_decoder_read_uint16le(decoder, &msg->kind);
		if (rc >= 0)
			rc = afb_rpc_decoder_read_uint16le(decoder, &msg->id);
		if (rc >= 0) {
			msg->length = (uint32_t)afb_rpc_decoder_remaining_size(decoder);
			if (msg->length) {
				rc = afb_rpc_decoder_read_pointer(decoder, (const void**)&msg->data, msg->length);
				if (rc >= 0)
					rc = afb_rpc_decoder_read_align(decoder, ALIGNMENT);
			}
		}
	}
	return rc;
}

static int decode_resource_destroy(afb_rpc_decoder_t *decoder, struct afb_rpc_v2_msg_resource_destroy *msg)
{
	int rc;
	if (afb_rpc_decoder_remaining_size(decoder) != SZ_RES_DESTROY)
		rc = X_EPROTO;
	else {
		rc = afb_rpc_decoder_read_uint16le(decoder, &msg->kind);
		if (rc >= 0)
			rc = afb_rpc_decoder_read_uint16le(decoder, &msg->id);
	}
	return rc;
}

/*  */
int afb_rpc_v2_decode_std_oper(const void *message, uint32_t size, uint16_t oper, afb_rpc_v2_msg_t *msg)
{
	int rc;
	afb_rpc_decoder_t decoder;

	afb_rpc_decoder_init(&decoder, message, size);
	memset(&msg->head, 0, sizeof msg->head);
	switch (oper)
	{
	case AFB_RPC_V2_ID_OP_CALL_REQUEST:
		rc = decode_call_request(&decoder, &msg->head.call_request, &msg->values);
		break;
	case AFB_RPC_V2_ID_OP_CALL_REPLY:
		rc = decode_call_reply(&decoder, &msg->head.call_reply, &msg->values);
		break;
	case AFB_RPC_V2_ID_OP_EVENT_PUSH:
		rc = decode_event_push(&decoder, &msg->head.event_push, &msg->values);
		break;
	case AFB_RPC_V2_ID_OP_EVENT_SUBSCRIBE:
		rc = decode_event_subscription(&decoder, &msg->head.event_subscribe);
		break;
	case AFB_RPC_V2_ID_OP_EVENT_UNSUBSCRIBE:
		rc = decode_event_subscription(&decoder, &msg->head.event_unsubscribe);
		break;
	case AFB_RPC_V2_ID_OP_EVENT_UNEXPECTED:
		rc = decode_event_unexpected(&decoder, &msg->head.event_unexpected);
		break;
	case AFB_RPC_V2_ID_OP_EVENT_BROADCAST:
		rc = decode_event_broadcast(&decoder, &msg->head.event_broadcast, &msg->values);
		break;
	case AFB_RPC_V2_ID_OP_RESOURCE_CREATE:
		rc = decode_resource_create(&decoder, &msg->head.resource_create);
		break;
	case AFB_RPC_V2_ID_OP_RESOURCE_DESTROY:
		rc = decode_resource_destroy(&decoder, &msg->head.resource_destroy);
		break;
	default:
		rc = X_ENOENT;
		break;
	}
	/* final alignment */
	if (rc >= 0)
		msg->oper = oper;

	return rc;
}

/*  */
int afb_rpc_v2_decode_single_packet_std_oper(const afb_rpc_v2_pckt_t *pckt, afb_rpc_v2_msg_t *msg)
{
	int rc;

	if ((pckt->type & AFB_RPC_V2_ID_PCKT_MSG_SHORTCUT_MASK) != 0)
		rc = afb_rpc_v2_decode_std_oper(pckt->payload, pckt->length - SZ_SHORTCUT_HEADER, SHORTCUT_TYPE_TO_OPER(pckt->type), msg);
	else
		rc = X_EINVAL;
	return rc;
}

/*  */
int afb_rpc_v2_is_single_packet_std_oper(afb_rpc_v2_pckt_t *pckt)
{
	return (pckt->type & AFB_RPC_V2_ID_PCKT_MSG_SHORTCUT_MASK) != 0;
}

/* decoding protocol */
int afb_rpc_v2_decode_packet(afb_rpc_decoder_t *decoder, afb_rpc_v2_pckt_t *pckt)
{
	int rc;

	/* scan the message */
	rc = afb_rpc_decoder_read_uint8(decoder, &pckt->type);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_uint8(decoder, &pckt->flags);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_uint16le(decoder, &pckt->length);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_pointer(decoder, &pckt->payload, pckt->length - 4);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_align(decoder, ALIGNMENT);
	return rc;
}

