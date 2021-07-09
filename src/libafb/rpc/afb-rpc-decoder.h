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


#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <errno.h>

/******************* x-errors ***********************/

/* X error number
 * (to move in an other header ?)
 */
#ifndef X_EINVAL
# define X_EINVAL -EINVAL
#endif

#ifndef X_ENOMEM
# define X_ENOMEM -ENOMEM
#endif

#ifndef X_EPIPE
# define X_EPIPE  -EPIPE
#endif

#ifndef X_ENOSPC
# define X_ENOSPC -ENOSPC
#endif

#ifndef X_EPROTO
# define X_EPROTO -EPROTO
#endif

#ifndef X_ENOENT
# define X_ENOENT -ENOENT
#endif

#ifndef X_ECANCELED
# define X_ECANCELED -ECANCELED
#endif

/******************* constants ***********************/

typedef struct afb_rpc_decoder   afb_rpc_decoder_t;

/******************* definition of types ***********************/

struct afb_rpc_decoder
{
	/* read offset */
	uint32_t offset;

	/** size */
	uint32_t size;

	/** data */
	const void *pointer;
};

/*************************************************************************************
* manage input
*************************************************************************************/

extern void afb_rpc_decoder_init(afb_rpc_decoder_t *decoder, const void *pointer, uint32_t size);

extern void afb_rpc_decoder_rewind(afb_rpc_decoder_t *decoder);

extern uint32_t afb_rpc_decoder_remaining_size(const afb_rpc_decoder_t *decoder);

extern int afb_rpc_decoder_peek_copy(afb_rpc_decoder_t *decoder, void *to, uint32_t size);

extern int afb_rpc_decoder_peek_pointer(afb_rpc_decoder_t *decoder, const void **cptr, uint32_t size);

extern int afb_rpc_decoder_read_copy(afb_rpc_decoder_t *decoder, void *to, uint32_t size);

extern int afb_rpc_decoder_read_pointer(afb_rpc_decoder_t *decoder, const void **cptr, uint32_t size);

extern int afb_rpc_decoder_skip(afb_rpc_decoder_t *decoder, uint32_t size);

extern int afb_rpc_decoder_read_align(afb_rpc_decoder_t *decoder, uint32_t base);

extern int afb_rpc_decoder_read_is_align(afb_rpc_decoder_t *decoder, uint32_t base);

extern int afb_rpc_decoder_peek_uint8(afb_rpc_decoder_t *decoder, uint8_t *value);

extern int afb_rpc_decoder_read_uint8(afb_rpc_decoder_t *decoder, uint8_t *value);

extern int afb_rpc_decoder_read_uint16(afb_rpc_decoder_t *decoder, uint16_t *value);

extern int afb_rpc_decoder_read_uint32(afb_rpc_decoder_t *decoder, uint32_t *value);
