/*
 * Copyright (C) 2015-2025 IoT.bzh Company
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

#include "afb-rpc-coder.h"
#include "afb-rpc-decoder.h"
#include "afb-rpc-v0.h"

/******************* manage versions *****************************/

// #define AFBRPC_PROTO_VERSION_UNSET	0
// #define AFBRPC_PROTO_VERSION_1		1
// #define AFBRPC_PROTO_VERSION_2		2

// #define AFBRPC_PROTO_VERSION_MIN	AFBRPC_PROTO_VERSION_1
// #define AFBRPC_PROTO_VERSION_MAX	AFBRPC_PROTO_VERSION_2

/************** constants for protocol V0 *************************/

#define AFBRPC_PROTO_IDENTIFIER		02723012011  /* afbrpc: 23.19.1.16.9 (wsapi) */

#define CHAR_FOR_VERSION_OFFER		'V'	/* client -> server */
#define CHAR_FOR_VERSION_SET		'v'	/* server -> client */

/*************************************************************************************
* coding protocol
*************************************************************************************/

int afb_rpc_v0_code_version_offer(afb_rpc_coder_t *coder, uint8_t count, const uint8_t *versions)
{
	int rc = afb_rpc_coder_write_uint8(coder, CHAR_FOR_VERSION_OFFER);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint32le(coder, AFBRPC_PROTO_IDENTIFIER);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint8(coder, count);
	if (rc >= 0)
		rc = afb_rpc_coder_write_copy(coder, versions, count);
	return rc;
}

int afb_rpc_v0_code_version_offer_v1(afb_rpc_coder_t *coder)
{
	uint8_t versions[] = { AFBRPC_PROTO_VERSION_1 };
	return afb_rpc_v0_code_version_offer(coder, 1, versions);
}

int afb_rpc_v0_code_version_offer_v3(afb_rpc_coder_t *coder)
{
	uint8_t versions[] = { AFBRPC_PROTO_VERSION_3 };
	return afb_rpc_v0_code_version_offer(coder, 1, versions);
}

int afb_rpc_v0_code_version_offer_v1_or_v3(afb_rpc_coder_t *coder)
{
	uint8_t versions[] = { AFBRPC_PROTO_VERSION_3, AFBRPC_PROTO_VERSION_1 };
	return afb_rpc_v0_code_version_offer(coder, 2, versions);
}

int afb_rpc_v0_code_version_set(afb_rpc_coder_t *coder, uint8_t version)
{
	int rc = afb_rpc_coder_write_uint8(coder, CHAR_FOR_VERSION_SET);
	if (rc >= 0)
		rc = afb_rpc_coder_write_uint8(coder, version);
	if (rc >= 0 && version >= AFBRPC_PROTO_VERSION_2)
		rc = afb_rpc_coder_write_uint16le(coder, 4);
	return rc;
}

int afb_rpc_v0_code_version_set_v1(afb_rpc_coder_t *coder)
{
	return afb_rpc_v0_code_version_set(coder, AFBRPC_PROTO_VERSION_1);
}

int afb_rpc_v0_code_version_set_v3(afb_rpc_coder_t *coder)
{
	return afb_rpc_v0_code_version_set(coder, AFBRPC_PROTO_VERSION_3);
}

int afb_rpc_v0_code(afb_rpc_coder_t *coder, afb_rpc_v0_msg_t *msg)
{
	switch (msg->type) {
	case afb_rpc_v0_msg_type_version_offer:
		return afb_rpc_v0_code_version_offer(coder, msg->version_offer.count, msg->version_offer.versions);
	case afb_rpc_v0_msg_type_version_set:
		return afb_rpc_v0_code_version_set(coder, msg->version_set.version);
	default:
		return X_EINVAL;
	}
}

/*************************************************************************************
* decoding protocol
*************************************************************************************/

/* on version offer */
static int read_on_version_offer(afb_rpc_decoder_t *decoder, afb_rpc_v0_msg_t *msg)
{
	int rc;
	uint32_t id;

	rc = afb_rpc_decoder_read_uint32le(decoder, &id);
	if (rc >= 0 && id != AFBRPC_PROTO_IDENTIFIER)
		rc = X_EPROTO;
	if (rc >= 0)
		rc = afb_rpc_decoder_read_uint8(decoder, &msg->version_offer.count);
	if (rc >= 0)
		rc = afb_rpc_decoder_read_pointer(decoder, (const void**)&msg->version_offer.versions, msg->version_offer.count);
	if (rc >= 0)
		msg->type = afb_rpc_v0_msg_type_version_offer;
	return rc;
}

/* received a version set */
static int read_on_version_set(afb_rpc_decoder_t *decoder, afb_rpc_v0_msg_t *msg)
{
	uint16_t chlen;
	int rc = afb_rpc_decoder_read_uint8(decoder, &msg->version_set.version);
	if (rc >= 0 && msg->version_set.version >= AFBRPC_PROTO_VERSION_2) {
		rc = afb_rpc_decoder_read_uint16le(decoder, &chlen);
		if (rc >= 0 && chlen != 4)
			rc = X_EPROTO;
	}
	if (rc >= 0)
		msg->type = afb_rpc_v0_msg_type_version_set;
	return rc;
}

/* callback when receiving binary data */
int afb_rpc_v0_decode(afb_rpc_decoder_t *decoder, afb_rpc_v0_msg_t *msg)
{
	uint8_t code;
	int rc;

	msg->type = afb_rpc_v0_msg_type_NONE;
	rc = afb_rpc_decoder_peek_uint8(decoder, &code);
	if (rc < 0)
		return rc;

	if (code != CHAR_FOR_VERSION_OFFER && code != CHAR_FOR_VERSION_SET)
		return X_EPROTO;

	/* really consumes the byte */
	rc = afb_rpc_decoder_skip(decoder, sizeof code);
	if (rc < 0)
		return rc;

	/* some "out-of-band" message */
	if (code == CHAR_FOR_VERSION_OFFER)
		rc = read_on_version_offer(decoder, msg);
	else
		rc = read_on_version_set(decoder, msg);

	return rc < 0 ? rc : 1;
}
