/*
 * Copyright (C) 2015-2024 IoT.bzh Company
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
#include "afb-rpc-v3.h"

/************** constants for protocol V3 *************************/


#define SZ_PARAM_BASE              (2+2)                            /* paramid, length */
#define SZ_PARAM_RES_ID            (SZ_PARAM_BASE+2+2)              /* base, kindid, id */
#define SZ_PARAM_RES_PLAIN_BASE    (SZ_PARAM_BASE+2)                /* base, kindid, data */
#define SZ_PARAM_RES_PLAIN(sz)     (SZ_PARAM_RES_PLAIN_BASE+(sz))   /* base, kindid, data */
#define SZ_PARAM_VALUE_BASE        (SZ_PARAM_BASE)                  /* base, data */
#define SZ_PARAM_VALUE(sz)         (SZ_PARAM_VALUE_BASE+(sz))       /* base, data */
#define SZ_PARAM_VALUE_TYPED_BASE  (SZ_PARAM_BASE+2)                /* base, typeid, data */
#define SZ_PARAM_VALUE_TYPED(sz)   (SZ_PARAM_VALUE_TYPED_BASE+(sz)) /* base, typeid, data */
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
* coding potocol
*************************************************************************************/

static int code_packet_begin(afb_rpc_coder_t *coder, uint16_t operation, uint32_t *offsetsize)
{
	int rc = afb_rpc_coder_write_align_at(coder, 8, 0);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint16le(coder, operation);
	if (rc >= 0) {
		static uint16_t seqno = 0;
		seqno = seqno + 1 ? seqno + 1 : seqno + 2;
		rc = afb_rpc_coder_write_uint16le(coder, seqno);
	}
	if (rc >= 0) {
		*offsetsize = afb_rpc_coder_get_position(coder);
		rc = afb_rpc_coder_write_uint32le(coder, 0);
	}
	return rc;
}

static int code_packet_end(afb_rpc_coder_t *coder, uint32_t offsetsize)
{
	uint32_t position = afb_rpc_coder_get_position(coder);
	int rc = afb_rpc_coder_set_position(coder, offsetsize);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint32le(coder, position - offsetsize + 4);
	if (rc >= 0)
		rc = afb_rpc_coder_set_position(coder, position);
	if (rc >= 0)
		rc = afb_rpc_coder_write_align_at(coder, 8, 0);
	return rc;
}

static int opt_param_resource_write(afb_rpc_coder_t *coder, const afb_rpc_v3_value_t *value, afb_rpc_v3_id_t kind)
{
	int rc = 0;
	if (value->data) {
		rc = afb_rpc_coder_write_align_at(coder, 8, 2);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, AFB_RPC_V3_ID_PARAM_RES_PLAIN);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, SZ_PARAM_RES_PLAIN(value->length));
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, kind);
		if (rc >= 0)
			rc = afb_rpc_coder_write(coder, value->data, value->length);
	}
	else if (value->id) {
		rc = afb_rpc_coder_write_align_at(coder, 2, 0);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, AFB_RPC_V3_ID_PARAM_RES_ID);
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
		rc = afb_rpc_coder_write_align_at(coder, 4, 0);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, AFB_RPC_V3_ID_PARAM_TIMEOUT);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, SZ_PARAM_TIMEOUT);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint32le(coder, timeout);
	}
	return rc;
}

static int param_value_write(afb_rpc_coder_t *coder, const afb_rpc_v3_value_t *value)
{
	int rc = 0;
	if (value->data && value->id) {
		rc = afb_rpc_coder_write_align_at(coder, 8, 2);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, AFB_RPC_V3_ID_PARAM_VALUE_TYPED);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, SZ_PARAM_VALUE_TYPED(value->length));
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, value->id); /* type-id */
		if (rc >= 0)
			rc = afb_rpc_coder_write(coder, value->data, value->length);
	}
	else if (value->id) {
		rc = afb_rpc_coder_write_align_at(coder, 2, 0);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, AFB_RPC_V3_ID_PARAM_VALUE_DATA);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, SZ_PARAM_VALUE_DATA);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, value->id);
	}
	else {
		rc = afb_rpc_coder_write_align_at(coder, 8, 4);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, AFB_RPC_V3_ID_PARAM_VALUE);
		if (rc >= 0)
			rc = afb_rpc_coder_write_uint16le(coder, SZ_PARAM_VALUE(value->length));
		if (rc >= 0)
			rc = afb_rpc_coder_write(coder, value->data, value->length);
	}
	return rc;
}

static int array_values_write(afb_rpc_coder_t *coder, const afb_rpc_v3_value_array_t *values)
{
	int rc = 0;
	uint16_t idx = 0;
	while (rc >= 0 && idx < values->count)
		rc = param_value_write(coder, &values->values[idx++]);
	return rc;
}

int afb_rpc_v3_code_call_request_body(afb_rpc_coder_t *coder, const afb_rpc_v3_msg_call_request_t *msg, const afb_rpc_v3_value_array_t *values)
{
	int rc = afb_rpc_coder_write_uint16le(coder, msg->callid);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint16le(coder, values ? values->count : 0);
	if (rc >= 0)
		rc = opt_param_resource_write(coder, &msg->api, AFB_RPC_V3_ID_KIND_API);
	if (rc >= 0)
		rc = opt_param_resource_write(coder, &msg->verb, AFB_RPC_V3_ID_KIND_VERB);
	if (rc >= 0)
		rc = opt_param_resource_write(coder, &msg->session, AFB_RPC_V3_ID_KIND_SESSION);
	if (rc >= 0)
		rc = opt_param_resource_write(coder, &msg->token, AFB_RPC_V3_ID_KIND_TOKEN);
	if (rc >= 0)
		rc = opt_param_resource_write(coder, &msg->creds, AFB_RPC_V3_ID_KIND_CREDS);
	if (rc >= 0)
		rc = opt_param_timeout_write(coder, msg->timeout);
	if (rc >= 0 && values)
		rc = array_values_write(coder, values);

	return rc;
}

int afb_rpc_v3_code_call_reply_body(afb_rpc_coder_t *coder, const afb_rpc_v3_msg_call_reply_t *msg, const afb_rpc_v3_value_array_t *values)
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

int afb_rpc_v3_code_event_push_body(afb_rpc_coder_t *coder, const afb_rpc_v3_msg_event_push_t *msg, const afb_rpc_v3_value_array_t *values)
{
	int rc = afb_rpc_coder_write_uint16le(coder, msg->eventid);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint16le(coder, values ? values->count : 0);
	if (rc >= 0 && values)
		rc = array_values_write(coder, values);
	return rc;
}

int afb_rpc_v3_code_event_broadcast_body(afb_rpc_coder_t *coder, const afb_rpc_v3_msg_event_broadcast_t *msg, const afb_rpc_v3_value_array_t *values)
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
	if (rc >= 0 && values)
		rc = array_values_write(coder, values);
	return rc;
}

int afb_rpc_v3_code_resource_create_body(afb_rpc_coder_t *coder, const afb_rpc_v3_msg_resource_create_t * msg)
{
	int rc = afb_rpc_coder_write_uint16le(coder, msg->kind);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint16le(coder, msg->id);
	if (rc >= 0)
		rc = afb_rpc_coder_write(coder, msg->data, msg->length);
	return rc;
}

int afb_rpc_v3_code_call_request(afb_rpc_coder_t *coder, const afb_rpc_v3_msg_call_request_t *msg, const afb_rpc_v3_value_array_t *values)
{
	uint32_t memo;
	int rc = code_packet_begin(coder, AFB_RPC_V3_ID_OP_CALL_REQUEST, &memo);
	if (rc >= 0)
		rc = afb_rpc_v3_code_call_request_body(coder, msg, values);
	if (rc >= 0)
		rc = code_packet_end(coder, memo);
	return rc;
}

int afb_rpc_v3_code_call_reply(afb_rpc_coder_t *coder, const afb_rpc_v3_msg_call_reply_t *msg, const afb_rpc_v3_value_array_t *values)
{
	uint32_t memo;
	int rc = code_packet_begin(coder, AFB_RPC_V3_ID_OP_CALL_REPLY, &memo);
	if (rc >= 0)
		rc = afb_rpc_v3_code_call_reply_body(coder, msg, values);
	if (rc >= 0)
		rc = code_packet_end(coder, memo);
	return rc;
}

int afb_rpc_v3_code_event_push(afb_rpc_coder_t *coder, const afb_rpc_v3_msg_event_push_t *msg, const afb_rpc_v3_value_array_t *values)
{
	uint32_t memo;
	int rc = code_packet_begin(coder, AFB_RPC_V3_ID_OP_EVENT_PUSH, &memo);
	if (rc >= 0)
		rc = afb_rpc_v3_code_event_push_body(coder, msg, values);
	if (rc >= 0)
		rc = code_packet_end(coder, memo);
	return rc;
}

static int code_event_subscription(afb_rpc_coder_t *coder, const struct afb_rpc_v3_msg_event_subscription *msg, afb_rpc_v3_id_t oper)
{
	uint32_t memo;
	int rc = code_packet_begin(coder, oper, &memo);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint16le(coder, msg->callid);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint16le(coder, msg->eventid);
	if (rc >= 0)
		rc = code_packet_end(coder, memo);
	return rc;
}

int afb_rpc_v3_code_event_subscribe(afb_rpc_coder_t *coder, const afb_rpc_v3_msg_event_subscribe_t *msg)
{
	return code_event_subscription(coder, msg, AFB_RPC_V3_ID_OP_EVENT_SUBSCRIBE);
}

int afb_rpc_v3_code_event_unsubscribe(afb_rpc_coder_t *coder, const afb_rpc_v3_msg_event_unsubscribe_t *msg)
{
	return code_event_subscription(coder, msg, AFB_RPC_V3_ID_OP_EVENT_UNSUBSCRIBE);
}


int afb_rpc_v3_code_event_unexpected(afb_rpc_coder_t *coder, const afb_rpc_v3_msg_event_unexpected_t *msg)
{
	uint32_t memo;
	int rc = code_packet_begin(coder, AFB_RPC_V3_ID_OP_EVENT_UNEXPECTED, &memo);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint16le(coder, msg->eventid);
	if (rc >= 0)
		rc = code_packet_end(coder, memo);
	return rc;
}

int afb_rpc_v3_code_event_broadcast(afb_rpc_coder_t *coder, const afb_rpc_v3_msg_event_broadcast_t *msg, const afb_rpc_v3_value_array_t *values)
{
	uint32_t memo;
	int rc = code_packet_begin(coder, AFB_RPC_V3_ID_OP_EVENT_BROADCAST, &memo);
	if (rc >= 0)
		rc = afb_rpc_v3_code_event_broadcast_body(coder, msg, values);
	if (rc >= 0)
		rc = code_packet_end(coder, memo);
	return rc;
}

int afb_rpc_v3_code_resource_create(afb_rpc_coder_t *coder, const afb_rpc_v3_msg_resource_create_t * msg)
{
	uint32_t memo;
	int rc = code_packet_begin(coder, AFB_RPC_V3_ID_OP_RESOURCE_CREATE, &memo);
	if (rc >= 0)
		rc = afb_rpc_v3_code_resource_create_body(coder, msg);
	if (rc >= 0)
		rc = code_packet_end(coder, memo);
	return rc;
}

int afb_rpc_v3_code_resource_destroy(afb_rpc_coder_t *coder, const afb_rpc_v3_msg_resource_destroy_t * msg)
{
	uint32_t memo;
	int rc = code_packet_begin(coder, AFB_RPC_V3_ID_OP_RESOURCE_DESTROY, &memo);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint16le(coder, msg->kind);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint16le(coder, msg->id);
	if (rc >= 0)
		rc = code_packet_end(coder, memo);
	return rc;
}

int afb_rpc_v3_code(afb_rpc_coder_t *coder, const afb_rpc_v3_msg_t * msg)
{
	int rc;

	switch (msg->oper) {
	case AFB_RPC_V3_ID_OP_CALL_REQUEST:
		rc = afb_rpc_v3_code_call_request(coder, &msg->head.call_request, msg->values.array);
		break;

	case AFB_RPC_V3_ID_OP_CALL_REPLY:
		rc = afb_rpc_v3_code_call_reply(coder, &msg->head.call_reply, msg->values.array);
		break;

	case AFB_RPC_V3_ID_OP_EVENT_PUSH:
		rc = afb_rpc_v3_code_event_push(coder, &msg->head.event_push, msg->values.array);
		break;

	case AFB_RPC_V3_ID_OP_EVENT_SUBSCRIBE:
		rc = afb_rpc_v3_code_event_subscribe(coder, &msg->head.event_subscribe);
		break;

	case AFB_RPC_V3_ID_OP_EVENT_UNSUBSCRIBE:
		rc = afb_rpc_v3_code_event_unsubscribe(coder, &msg->head.event_unsubscribe);
		break;

	case AFB_RPC_V3_ID_OP_EVENT_UNEXPECTED:
		rc = afb_rpc_v3_code_event_unexpected(coder, &msg->head.event_unexpected);
		break;

	case AFB_RPC_V3_ID_OP_EVENT_BROADCAST:
		rc = afb_rpc_v3_code_event_broadcast(coder, &msg->head.event_broadcast, msg->values.array);
		break;

	case AFB_RPC_V3_ID_OP_RESOURCE_CREATE:
		rc = afb_rpc_v3_code_resource_create(coder, &msg->head.resource_create);
		break;

	case AFB_RPC_V3_ID_OP_RESOURCE_DESTROY:
		rc = afb_rpc_v3_code_resource_destroy(coder, &msg->head.resource_destroy);
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

	/* read the type, skipping any padding */
	rc = afb_rpc_decoder_read_align(decoder, 2);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_uint16le(decoder, &param->type);
	while (rc >= 0 && param->type == AFB_RPC_V3_ID_PARAM_PADDING)
		rc = afb_rpc_decoder_read_uint16le(decoder, &param->type);
	/* read the length */
	if (rc >= 0)
		rc = afb_rpc_decoder_read_uint16le(decoder, &length);
	/* interpret */
	if (rc >= 0) {
		switch (param->type) {
		case AFB_RPC_V3_ID_PARAM_RES_ID:
			rc = afb_rpc_decoder_read_uint16le(decoder, &param->kind);
			if (rc >= 0)
				rc = afb_rpc_decoder_read_uint16le(decoder, &param->id);
			break;
		case AFB_RPC_V3_ID_PARAM_RES_PLAIN:
			rc = afb_rpc_decoder_read_uint16le(decoder, &param->kind);
			if (rc >= 0) {
				param->length = length - SZ_PARAM_RES_PLAIN_BASE;
				rc = afb_rpc_decoder_read_pointer(decoder, &param->data, param->length);
			}
			break;
		case AFB_RPC_V3_ID_PARAM_VALUE:
			param->length = length - SZ_PARAM_VALUE_BASE;
			rc = afb_rpc_decoder_read_pointer(decoder, &param->data, param->length);
			break;
		case AFB_RPC_V3_ID_PARAM_VALUE_TYPED:
			rc = afb_rpc_decoder_read_uint16le(decoder, &param->id);
			if (rc >= 0) {
				param->length = length - SZ_PARAM_VALUE_TYPED_BASE;
				rc = afb_rpc_decoder_read_pointer(decoder, &param->data, param->length);
			}
			break;
		case AFB_RPC_V3_ID_PARAM_VALUE_DATA:
			rc = afb_rpc_decoder_read_uint16le(decoder, &param->id);
			break;
		case AFB_RPC_V3_ID_PARAM_TIMEOUT:
			rc = afb_rpc_decoder_read_uint32le(decoder, &param->timeout);
			break;
		default:
			rc = afb_rpc_decoder_skip(decoder, length - SZ_PARAM_BASE);
			break;
		}
	}
	return rc;
}

static int set_request_resource(afb_rpc_v3_msg_call_request_t *msg, struct param *param)
{
	int rc;
	afb_rpc_v3_value_t *val;

	switch (param->kind) {
	case AFB_RPC_V3_ID_KIND_SESSION:
		val = &msg->session;
		break;
	case AFB_RPC_V3_ID_KIND_TOKEN:
		val = &msg->token;
		break;
	case AFB_RPC_V3_ID_KIND_API:
		val = &msg->api;
		break;
	case AFB_RPC_V3_ID_KIND_VERB:
		val = &msg->verb;
		break;
	case AFB_RPC_V3_ID_KIND_CREDS:
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

static int decode_values(afb_rpc_decoder_t *decoder, uint16_t nval, afb_rpc_v3_value_array_decode_t *valdec)
{
	uint16_t ival;
	struct param param;
	afb_rpc_v3_value_array_t *values;
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
				case AFB_RPC_V3_ID_PARAM_VALUE:
				case AFB_RPC_V3_ID_PARAM_VALUE_TYPED:
				case AFB_RPC_V3_ID_PARAM_VALUE_DATA:
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

static int decode_call_request(afb_rpc_decoder_t *decoder, afb_rpc_v3_msg_call_request_t *msg, afb_rpc_v3_value_array_decode_t *valdec)
{
	uint16_t nval;
	struct param param;
	afb_rpc_v3_value_t *val;
	afb_rpc_v3_value_array_t *values = NULL;

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
			case AFB_RPC_V3_ID_PARAM_RES_ID:
			case AFB_RPC_V3_ID_PARAM_RES_PLAIN:
				rc = set_request_resource(msg, &param);
				break;
			case AFB_RPC_V3_ID_PARAM_VALUE:
			case AFB_RPC_V3_ID_PARAM_VALUE_TYPED:
			case AFB_RPC_V3_ID_PARAM_VALUE_DATA:
				if (!values || values->count == nval)
					rc = X_EPROTO;
				else {
					val = &values->values[values->count++];
					val->id = param.id;
					val->length = param.length;
					val->data = param.data;
				}
				break;
			case AFB_RPC_V3_ID_PARAM_TIMEOUT:
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

static int decode_call_reply(afb_rpc_decoder_t *decoder, afb_rpc_v3_msg_call_reply_t *msg, afb_rpc_v3_value_array_decode_t *valdec)
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

static int decode_event_push(afb_rpc_decoder_t *decoder, afb_rpc_v3_msg_event_push_t *msg, afb_rpc_v3_value_array_decode_t *valdec)
{
	uint16_t nval;

	int rc = afb_rpc_decoder_read_uint16le(decoder, &msg->eventid);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_uint16le(decoder, &nval);
	if (rc >= 0)
		rc = decode_values(decoder, nval, valdec);
	return rc;
}

static int decode_event_subscription(afb_rpc_decoder_t *decoder, struct afb_rpc_v3_msg_event_subscription *msg)
{
	int rc = afb_rpc_decoder_read_uint16le(decoder, &msg->callid);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_uint16le(decoder, &msg->eventid);
	return rc;
}

static int decode_event_unexpected(afb_rpc_decoder_t *decoder, afb_rpc_v3_msg_event_unexpected_t *msg)
{
	return  afb_rpc_decoder_read_uint16le(decoder, &msg->eventid);
}

static int decode_event_broadcast(afb_rpc_decoder_t *decoder, afb_rpc_v3_msg_event_broadcast_t *msg, afb_rpc_v3_value_array_decode_t *valdec)
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
		rc = decode_values(decoder, nval, valdec);
	return rc;
}

static int decode_resource_create(afb_rpc_decoder_t *decoder, afb_rpc_v3_msg_resource_create_t *msg)
{
	int rc = afb_rpc_decoder_read_uint16le(decoder, &msg->kind);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_uint16le(decoder, &msg->id);
	if (rc >= 0) {
		msg->length = (uint32_t)afb_rpc_decoder_remaining_size(decoder);
		if (msg->length) {
			rc = afb_rpc_decoder_read_pointer(decoder, (const void**)&msg->data, msg->length);
		}
	}
	return rc;
}

static int decode_resource_destroy(afb_rpc_decoder_t *decoder, struct afb_rpc_v3_msg_resource_destroy *msg)
{
	int rc = afb_rpc_decoder_read_uint16le(decoder, &msg->kind);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_uint16le(decoder, &msg->id);
	return rc;
}

/*  */
int afb_rpc_v3_decode_operation(afb_rpc_v3_pckt_t *pckt, afb_rpc_v3_msg_t *msg)
{
	int rc;
	afb_rpc_decoder_t decoder;

	afb_rpc_decoder_init(&decoder, pckt->payload, pckt->length);
	memset(&msg->head, 0, sizeof msg->head);
	switch (pckt->operation)
	{
	case AFB_RPC_V3_ID_OP_CALL_REQUEST:
		rc = decode_call_request(&decoder, &msg->head.call_request, &msg->values);
		break;
	case AFB_RPC_V3_ID_OP_CALL_REPLY:
		rc = decode_call_reply(&decoder, &msg->head.call_reply, &msg->values);
		break;
	case AFB_RPC_V3_ID_OP_EVENT_PUSH:
		rc = decode_event_push(&decoder, &msg->head.event_push, &msg->values);
		break;
	case AFB_RPC_V3_ID_OP_EVENT_SUBSCRIBE:
		rc = decode_event_subscription(&decoder, &msg->head.event_subscribe);
		break;
	case AFB_RPC_V3_ID_OP_EVENT_UNSUBSCRIBE:
		rc = decode_event_subscription(&decoder, &msg->head.event_unsubscribe);
		break;
	case AFB_RPC_V3_ID_OP_EVENT_UNEXPECTED:
		rc = decode_event_unexpected(&decoder, &msg->head.event_unexpected);
		break;
	case AFB_RPC_V3_ID_OP_EVENT_BROADCAST:
		rc = decode_event_broadcast(&decoder, &msg->head.event_broadcast, &msg->values);
		break;
	case AFB_RPC_V3_ID_OP_RESOURCE_CREATE:
		rc = decode_resource_create(&decoder, &msg->head.resource_create);
		break;
	case AFB_RPC_V3_ID_OP_RESOURCE_DESTROY:
		rc = decode_resource_destroy(&decoder, &msg->head.resource_destroy);
		break;
	default:
		rc = X_ENOENT;
		break;
	}
	if (rc >= 0)
		msg->oper = pckt->operation;

	return rc;
}

/* decoding protocol */
int afb_rpc_v3_decode_packet(afb_rpc_decoder_t *decoder, afb_rpc_v3_pckt_t *pckt)
{
	int rc = afb_rpc_decoder_read_align(decoder, 8);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_uint16le(decoder, &pckt->operation);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_uint16le(decoder, &pckt->seqno);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_uint32le(decoder, &pckt->length);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_pointer(decoder, &pckt->payload, pckt->length -= 8);
	if (rc >= 0)
		afb_rpc_decoder_skip(decoder, 7 & (8 - decoder->offset));
	return rc;
}
