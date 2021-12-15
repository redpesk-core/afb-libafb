/*
 * Copyright (C) 2015-2021 IoT.bzh Company
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "afb-rpc-decoder.h"

/*************************************************************************************
* manage input
*************************************************************************************/

void afb_rpc_decoder_init(afb_rpc_decoder_t *decoder, const void *pointer, uint32_t size)
{
	decoder->size = size;
	decoder->pointer = pointer;
	decoder->offset = 0;
}

void afb_rpc_decoder_rewind(afb_rpc_decoder_t *decoder)
{
	decoder->offset = 0;
}

uint32_t afb_rpc_decoder_remaining_size(const afb_rpc_decoder_t *decoder)
{
	return decoder->size - decoder->offset;
}

int afb_rpc_decoder_peek_pointer(afb_rpc_decoder_t *decoder, const void **cptr, uint32_t size)
{
	int rc;
	uint32_t after = size + decoder->offset;
	if (after < size || after > decoder->size)
		rc = X_EINVAL;
	else {
		*cptr = (const char*)decoder->pointer + decoder->offset;
		rc = 0;
	}
	return rc;
}

int afb_rpc_decoder_peek_copy(afb_rpc_decoder_t *decoder, void *to, uint32_t size)
{
	const void *from;
	int rc;
	if (size == 0)
		rc = 0;
	else {
		rc = afb_rpc_decoder_peek_pointer(decoder, &from, size);
		if (rc == 0)
			memcpy(to, from, size);
	}
	return rc;
}

int afb_rpc_decoder_read_pointer(afb_rpc_decoder_t *decoder, const void **cptr, uint32_t size)
{
	int rc;
	uint32_t after = size + decoder->offset;
	if (after < size || after > decoder->size)
		rc = X_EINVAL;
	else {
		*cptr = (const char*)decoder->pointer + decoder->offset;
		decoder->offset = after;
		rc = 0;
	}
	return rc;
}

int afb_rpc_decoder_read_copy(afb_rpc_decoder_t *decoder, void *to, uint32_t size)
{
	const void *from;
	int rc;
	if (size == 0)
		rc = 0;
	else {
		rc = afb_rpc_decoder_read_pointer(decoder, &from, size);
		if (rc == 0)
			memcpy(to, from, size);
	}
	return rc;
}

int afb_rpc_decoder_skip(afb_rpc_decoder_t *decoder, uint32_t size)
{
	int rc;
	uint32_t after = size + decoder->offset;
	if (after < size || after > decoder->size)
		rc = X_EINVAL;
	else {
		decoder->offset = after;
		rc = 0;
	}
	return rc;
}

/* align to base (base MUST be a power of 2 */
static int read_align(afb_rpc_decoder_t *decoder, uint32_t base)
{
	return afb_rpc_decoder_skip(decoder, (uint32_t)((-decoder->offset) & (base - 1)));
}

/* align to base (base MUST be a power of 2 */
static int read_is_aligned(afb_rpc_decoder_t *decoder, uint32_t base)
{
	return !(decoder->offset & (base - 1));
}

int afb_rpc_decoder_read_align(afb_rpc_decoder_t *decoder, uint32_t base)
{
	return base & (base - 1) ? X_EINVAL : read_align(decoder, base);
}

int afb_rpc_decoder_read_is_align(afb_rpc_decoder_t *decoder, uint32_t base)
{
	return !(base & (base - 1)) && read_is_aligned(decoder, base);
}

int afb_rpc_decoder_read_uint8(afb_rpc_decoder_t *decoder, uint8_t *value)
{
	return afb_rpc_decoder_read_copy(decoder, value, sizeof *value);
}

int afb_rpc_decoder_peek_uint8(afb_rpc_decoder_t *decoder, uint8_t *value)
{
	return afb_rpc_decoder_peek_copy(decoder, value, sizeof *value);
}

int afb_rpc_decoder_read_uint16(afb_rpc_decoder_t *decoder, uint16_t *value)
{
	return afb_rpc_decoder_read_copy(decoder, value, sizeof *value);
}

int afb_rpc_decoder_read_uint16le(afb_rpc_decoder_t *decoder, uint16_t *value)
{
	int rc = afb_rpc_decoder_read_uint16(decoder, value);
	*value = le16toh(*value);
	return rc;
}

int afb_rpc_decoder_read_uint16be(afb_rpc_decoder_t *decoder, uint16_t *value)
{
	int rc = afb_rpc_decoder_read_uint16(decoder, value);
	*value = be16toh(*value);
	return rc;
}

int afb_rpc_decoder_read_uint32(afb_rpc_decoder_t *decoder, uint32_t *value)
{
	return afb_rpc_decoder_read_copy(decoder, value, sizeof *value);
}

int afb_rpc_decoder_read_uint32le(afb_rpc_decoder_t *decoder, uint32_t *value)
{
	int rc = afb_rpc_decoder_read_uint32(decoder, value);
	*value = le32toh(*value);
	return rc;
}

int afb_rpc_decoder_read_uint32be(afb_rpc_decoder_t *decoder, uint32_t *value)
{
	int rc = afb_rpc_decoder_read_uint32(decoder, value);
	*value = be32toh(*value);
	return rc;
}

