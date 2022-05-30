/*
 * Copyright (C) 2015-2022 IoT.bzh Company
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

/* maximum size of data copied locally */
#ifndef AFB_RPC_OUTPUT_INLINE_SIZE
# define AFB_RPC_OUTPUT_INLINE_SIZE        (3*sizeof(uint32_t))
#endif

/* maximum number of output buffers */
#ifndef AFB_RPC_OUTPUT_BUFFER_COUNT_MAX
# define AFB_RPC_OUTPUT_BUFFER_COUNT_MAX   32
#endif

/* maximum number of output disposers */
#ifndef AFB_RPC_OUTPUT_DISPOSE_COUNT_MAX
# define AFB_RPC_OUTPUT_DISPOSE_COUNT_MAX  32
#endif

/******************* declaration of types ***********************/

typedef struct afb_rpc_coder         afb_rpc_coder_t;
typedef struct afb_rpc_coder_iovec   afb_rpc_coder_iovec_t;
typedef struct afb_rpc_coder_dispose afb_rpc_coder_dispose_t;
typedef struct afb_rpc_coder_output  afb_rpc_coder_output_t;

/******************* definition of types ***********************/

/** improved internal iovec */
struct afb_rpc_coder_iovec
{
	/** data as a pointer or just inline */
	union {
		/** read/write pointer */
		void *pointer;
		/** read only pointer */
		const void *const_pointer;
		/** buffer */
		char inl[AFB_RPC_OUTPUT_INLINE_SIZE];
	} data;
	/** size. For sizes lesser than AFB_RPC_OUTPUT_INLINE_SIZE, data is inline */
	uint32_t size;
};

/** record of dispose */
struct afb_rpc_coder_dispose
{
	/** function dispose */
	union {
		void (*args1)(void*);
		void (*args2)(void*,void*);
	} dispose;

	/** closure pointer */
	void *closure;

	/** arg pointer */
	void *arg;
};

struct afb_rpc_coder
{
	/* output buffer count */
	uint8_t buffer_count;

	/* output dispose count */
	uint8_t dispose_count;

	/* output tiny size */
	uint8_t inline_remain;

	/* write position */
	uint32_t pos;

	/* size of the output buffer */
	uint32_t size;

	/* output buffers */
	afb_rpc_coder_iovec_t buffers[AFB_RPC_OUTPUT_BUFFER_COUNT_MAX];

	/* output dispose */
	afb_rpc_coder_dispose_t disposes[AFB_RPC_OUTPUT_DISPOSE_COUNT_MAX];
};

/*************************************************************************************
* manage output
*************************************************************************************/

/**
 * Initialize the coder
 *
 * @param coder the coder object
 */
extern void afb_rpc_coder_init(afb_rpc_coder_t *coder);

/**
 * Get the output sizes
 *
 * @param coder the coder object
 * @param size pointer for storing size, can be NULL
 *
 * @return the needed count of iovec
 */
extern int afb_rpc_coder_output_sizes(afb_rpc_coder_t *coder, uint32_t *size);

/**
 * Get part of the output as a buffer
 *
 * @param coder the coder object
 * @param buffer pointer for storing available output data
 * @param size size of the buffer
 * @param offset offset of the beginning within the output
 *
 * @return the size copied
 */
extern uint32_t afb_rpc_coder_output_get_subbuffer(afb_rpc_coder_t *coder, void *buffer, uint32_t size, uint32_t offset);

/**
 * Get start of the output as a buffer
 *
 * Synonym to afb_rpc_coder_output_get_subbuffer(coder, buffer, size, 0)
 *
 * @param coder the coder object
 * @param buffer pointer for storing available output data
 * @param size size of the buffer
 *
 * @return the size copied
 */
extern uint32_t afb_rpc_coder_output_get_buffer(afb_rpc_coder_t *coder, void *buffer, uint32_t size);

/**
 * Release the begin of the output, call dispose on need.
 * The released output matches the minimum of the two given values
 *
 * @param coder the coder object
 */
extern void afb_rpc_coder_output_dispose(afb_rpc_coder_t *coder);

/*  */
extern int afb_rpc_coder_on_dispose2_output(afb_rpc_coder_t *coder, void (*dispose)(void*,void*), void *closure, void *arg);
extern int afb_rpc_coder_on_dispose_output(afb_rpc_coder_t *coder, void (*dispose)(void*), void *closure);

extern int afb_rpc_coder_write(afb_rpc_coder_t *coder, const void *data, uint32_t size);

extern int afb_rpc_coder_write_copy(afb_rpc_coder_t *coder, const void *data, uint32_t size);

extern int afb_rpc_coder_write_zeroes(afb_rpc_coder_t *coder, uint32_t count);

/* align to base (base MUST be a power of 2) */
extern int afb_rpc_coder_write_align(afb_rpc_coder_t *coder, uint32_t base);
extern int afb_rpc_coder_write_align_at(afb_rpc_coder_t *coder, uint32_t base, uint32_t index);

extern int afb_rpc_coder_write_uint32(afb_rpc_coder_t *coder, uint32_t value);
extern int afb_rpc_coder_write_uint32le(afb_rpc_coder_t *coder, uint32_t value);
extern int afb_rpc_coder_write_uint32be(afb_rpc_coder_t *coder, uint32_t value);

extern int afb_rpc_coder_write_uint16(afb_rpc_coder_t *coder, uint16_t value);
extern int afb_rpc_coder_write_uint16le(afb_rpc_coder_t *coder, uint16_t value);
extern int afb_rpc_coder_write_uint16be(afb_rpc_coder_t *coder, uint16_t value);

extern int afb_rpc_coder_write_uint8(afb_rpc_coder_t *coder, uint8_t value);

extern int afb_rpc_coder_write_subcoder(afb_rpc_coder_t *coder, afb_rpc_coder_t *subcoder, uint32_t offset, uint32_t size);

extern uint32_t afb_rpc_coder_get_position(afb_rpc_coder_t *coder);
extern int afb_rpc_coder_set_position(afb_rpc_coder_t *coder, uint32_t pos);

#if !RPC_NO_IOVEC

struct iovec;

/**
 * Get start of the output as iovec
 *
 * @param coder the coder object
 * @param iov the array of iovec to set
 * @param iovcnt the count of iovec available
 *
 * @return the count of iovec initialized
 */
extern int afb_rpc_coder_output_get_iovec(afb_rpc_coder_t *coder, struct iovec *iov, int iovcnt);

extern int afb_rpc_coder_output_get_subiovec(afb_rpc_coder_t *coder, struct iovec *iov, int iovcnt, uint32_t size, uint32_t offset);

extern int afb_rpc_coder_write_iovec(afb_rpc_coder_t *coder, const struct iovec *iov, int iovcnt);

extern int afb_rpc_coder_write_copy_iovec(afb_rpc_coder_t *coder, const struct iovec *iov, int iovcnt);

#endif
