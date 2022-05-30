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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "afb-rpc-coder.h"

/*************************************************************************************
* manage output
*************************************************************************************/

/* initialize the coder object */
void afb_rpc_coder_init(afb_rpc_coder_t *coder)
{
	coder->dispose_count = 0;
	coder->buffer_count = 0;
	coder->inline_remain = 0;
	coder->pos = 0;
	coder->size = 0;
}

/* Get the output sizes */
int afb_rpc_coder_output_sizes(afb_rpc_coder_t *coder, uint32_t *size)
{
	if (size)
		*size = coder->size;
	return (int)coder->buffer_count;
}

static inline void *extract(afb_rpc_coder_t *coder, uint32_t offset, uint32_t size, void *(*add)(void*,void*,uint32_t), void *arg)
{
	afb_rpc_coder_iovec_t *itbuf, *endbuf;
	uint32_t off, noff, copied, soff, slen;
	char *cptr;

	off = copied = 0;
	itbuf = coder->buffers;
	endbuf = &itbuf[coder->buffer_count];

	/* skip beginning */
	while (itbuf != endbuf && off + itbuf->size <= offset)
		off += itbuf++->size;

	/* emit data */
	if (itbuf != endbuf) {
		if (off < offset) {
			soff = offset - off;
			slen = itbuf->size - soff;
			cptr = itbuf->size > AFB_RPC_OUTPUT_INLINE_SIZE ? itbuf->data.pointer : itbuf->data.inl;
			if (slen > size)
				slen = size;
			arg = add(arg, cptr + soff, slen);
			copied = slen;
			itbuf++;
		}
		while (arg && itbuf != endbuf && copied < size) {
			slen = itbuf->size;
			cptr = itbuf->size > AFB_RPC_OUTPUT_INLINE_SIZE ? itbuf->data.pointer : itbuf->data.inl;
			noff = copied + slen;
			if (noff > size) {
				slen -= noff - size;
				noff = size;
			}
			arg = add(arg, cptr, slen);
			copied = noff;
			itbuf++;
		}
	}
	return arg;
}

static inline void *extrbuff(void *closure, void *data, uint32_t length)
{
	memcpy(closure, data, length);
	return ((char*)closure) + length;
}

/* Get part of output as a buffer */
uint32_t afb_rpc_coder_output_get_subbuffer(afb_rpc_coder_t *coder, void *buffer, uint32_t size, uint32_t offset)
{
	void *end = extract(coder, offset, size, extrbuff, buffer);
	return (uint32_t)((char*)end - (char*)buffer);
}

/* Get the output as a buffer */
uint32_t afb_rpc_coder_output_get_buffer(afb_rpc_coder_t *coder, void *buffer, uint32_t size)
{
	return afb_rpc_coder_output_get_subbuffer(coder, buffer, size, 0);
}

/* dispose of the memory of the coder */
void afb_rpc_coder_output_dispose(afb_rpc_coder_t *coder)
{
	afb_rpc_coder_dispose_t *disp;

	while (coder->dispose_count) {
		disp = &coder->disposes[--coder->dispose_count];
		if (disp->arg == disp)
			disp->dispose.args1(disp->closure);
		else
			disp->dispose.args2(disp->closure, disp->arg);
	}
	coder->buffer_count = 0;
	coder->inline_remain = 0;
	coder->pos = 0;
	coder->size = 0;
}

/* set on dispose action */
int afb_rpc_coder_on_dispose2_output(afb_rpc_coder_t *coder, void (*dispose)(void*,void*), void *closure, void *arg)
{
	afb_rpc_coder_dispose_t *disp;

	if (coder->dispose_count >= AFB_RPC_OUTPUT_DISPOSE_COUNT_MAX)
		return X_ENOSPC;

	disp = &coder->disposes[coder->dispose_count++];
	disp->dispose.args2 = dispose;
	disp->closure = closure;
	disp->arg = arg;
	return 0;
}

/* set on dispose action */
int afb_rpc_coder_on_dispose_output(afb_rpc_coder_t *coder, void (*dispose)(void*), void *closure)
{
	afb_rpc_coder_dispose_t *disp;

	if (coder->dispose_count >= AFB_RPC_OUTPUT_DISPOSE_COUNT_MAX)
		return X_ENOSPC;

	disp = &coder->disposes[coder->dispose_count++];
	disp->dispose.args1 = dispose;
	disp->closure = closure;
	disp->arg = disp;
	return 0;
}

static int write_at_end(afb_rpc_coder_t *coder, const void *data, uint32_t size)
{
	afb_rpc_coder_iovec_t *buf;
	uint32_t rem;

	if (size <= AFB_RPC_OUTPUT_INLINE_SIZE) {
		rem = (uint32_t)coder->inline_remain;
		if (size <= rem) {
			/* append in last inline buffer */
			buf = &coder->buffers[coder->buffer_count - 1];
			memcpy(&buf->data.inl[AFB_RPC_OUTPUT_INLINE_SIZE - rem], data, size);
			coder->inline_remain = (uint8_t)(rem - size);
			buf->size += size;
		}
		else {
			if (coder->buffer_count >= AFB_RPC_OUTPUT_BUFFER_COUNT_MAX)
				return X_ENOSPC;
			if (rem) {
				/* append in last inline buffer */
				buf = &coder->buffers[coder->buffer_count - 1];
				memcpy(&buf->data.inl[AFB_RPC_OUTPUT_INLINE_SIZE - rem], data, rem);
				coder->size += rem;
				buf->size += rem;
				size -= rem;
				data = ((const char*)data) + rem;
			}
			/* copy in inline buffer */
			coder->inline_remain = (uint8_t)(AFB_RPC_OUTPUT_INLINE_SIZE - size);
			buf = &coder->buffers[coder->buffer_count++];
			memcpy(&buf->data.inl[0], data, size);
			buf->size = size;
		}
	}
	else if (coder->buffer_count >= AFB_RPC_OUTPUT_BUFFER_COUNT_MAX)
		return X_ENOSPC;
	else {
		/* record the buffer */
		buf = &coder->buffers[coder->buffer_count++];
		buf->data.const_pointer = data;
		buf->size = size;
		coder->inline_remain = 0;
	}
	coder->size += size;
	coder->pos = coder->size;
	return 0;
}

static int write_in_middle(afb_rpc_coder_t *coder, const void *data, uint32_t size)
{
	char *ptr;
	uint32_t sz, pos;
	afb_rpc_coder_iovec_t *buf = coder->buffers;

	/* position on first buffer */
	for (pos = coder->pos ; pos >= buf->size ; pos -= (buf++)->size);

	/* move forward */
	coder->pos += size;

	/* write now */
	sz = buf->size;
	ptr = sz > AFB_RPC_OUTPUT_INLINE_SIZE ? (char*)buf->data.pointer : buf->data.inl;
	sz -= pos;
	ptr += pos;
	if (sz >= size)
		memcpy(ptr, data, size);
	else {
		for (;;) {
			memcpy(ptr, data, sz);
			size -= sz;
			if (size == 0)
				break;
			data += sz;
			buf++;
			sz = buf->size;
			ptr = sz > AFB_RPC_OUTPUT_INLINE_SIZE ? (char*)buf->data.pointer : buf->data.inl;
			sz = sz > size ? size : sz;
		}
	}
	return 0;
}

int afb_rpc_coder_write(afb_rpc_coder_t *coder, const void *data, uint32_t size)
{
	int rc = 0;
	if (size > 0) {
		uint32_t exsz = coder->size - coder->pos;
		if (exsz == 0)
			rc = write_at_end(coder, data, size);
		else if (exsz >= size)
			rc = write_in_middle(coder, data, size);
		else {
			rc = write_in_middle(coder, data, exsz);
			if (rc >= 0)
				rc = write_at_end(coder, data + exsz, size - exsz);
		}
	}
	return rc;
}

int afb_rpc_coder_write_copy(afb_rpc_coder_t *coder, const void *data, uint32_t size)
{
	int rc;
	void *copy;

	if (size <= AFB_RPC_OUTPUT_INLINE_SIZE)
		rc = afb_rpc_coder_write(coder, data, size);
	else {
		copy = malloc(size);
		if (copy == NULL)
			rc = X_ENOMEM;
		else {
			memcpy(copy, data, size);
			rc = afb_rpc_coder_on_dispose_output(coder, free, copy);
			if (rc < 0)
				free(copy);
			else
				rc = afb_rpc_coder_write(coder, copy, size);
		}
	}
	return rc;
}

int afb_rpc_coder_write_zeroes(afb_rpc_coder_t *coder, uint32_t count)
{
	int rc;
	char buffer[AFB_RPC_OUTPUT_INLINE_SIZE];
	char *mem;
	if (count <= AFB_RPC_OUTPUT_INLINE_SIZE) {
		memset(buffer, 0, count);
		rc = afb_rpc_coder_write(coder, buffer, count);
	}
	else {
		mem = calloc(1, count);
		if (mem == NULL)
			rc = X_ENOMEM;
		else {
			rc = afb_rpc_coder_write(coder, mem, count);
			if (rc < 0)
				free(mem);
			else
				afb_rpc_coder_on_dispose_output(coder, free, mem);
		}
	}
	return rc;
}

uint32_t afb_rpc_coder_get_position(afb_rpc_coder_t *coder)
{
	return coder->pos;
}

int afb_rpc_coder_set_position(afb_rpc_coder_t *coder, uint32_t pos)
{
	if (pos > coder->size) {
		coder->pos = coder->size;
		return afb_rpc_coder_write_zeroes(coder, coder->size - pos);
	}
	coder->pos = pos;
	return 0;
}

/* align to index for the base (base MUST be a power of 2 */
int afb_rpc_coder_write_align_at(afb_rpc_coder_t *coder, uint32_t base, uint32_t index)
{
	uint32_t count, mask = base - 1;
	if ((base & mask) != 0)
		return X_EINVAL;
	count = (uint32_t)((index-coder->size) & mask);
	return count == 0 ? 0 : afb_rpc_coder_write_zeroes(coder, count);
}

/* align to base (base MUST be a power of 2 */
int afb_rpc_coder_write_align(afb_rpc_coder_t *coder, uint32_t base)
{
	return afb_rpc_coder_write_align_at(coder, base, 0);
}

int afb_rpc_coder_write_uint32le(afb_rpc_coder_t *coder, uint32_t value)
{
	return afb_rpc_coder_write_uint32(coder, htole32(value));
}

int afb_rpc_coder_write_uint32be(afb_rpc_coder_t *coder, uint32_t value)
{
	return afb_rpc_coder_write_uint32(coder, htobe32(value));
}

int afb_rpc_coder_write_uint32(afb_rpc_coder_t *coder, uint32_t value)
{
	return afb_rpc_coder_write(coder, &value, (int)sizeof value);
}

int afb_rpc_coder_write_uint16le(afb_rpc_coder_t *coder, uint16_t value)
{
	return afb_rpc_coder_write_uint16(coder, htole16(value));
}

int afb_rpc_coder_write_uint16be(afb_rpc_coder_t *coder, uint16_t value)
{
	return afb_rpc_coder_write_uint16(coder, htobe16(value));
}

int afb_rpc_coder_write_uint16(afb_rpc_coder_t *coder, uint16_t value)
{
	return afb_rpc_coder_write(coder, &value, (int)sizeof value);
}

int afb_rpc_coder_write_uint8(afb_rpc_coder_t *coder, uint8_t value)
{
	return afb_rpc_coder_write(coder, &value, (int)sizeof value);
}

static inline void *extrsubcod(void *closure, void *data, uint32_t length)
{
	afb_rpc_coder_t *coder = closure;
	return afb_rpc_coder_write(coder, data, length) >= 0 ? coder : 0;
}

int afb_rpc_coder_write_subcoder(afb_rpc_coder_t *coder, afb_rpc_coder_t *subcoder, uint32_t offset, uint32_t size)
{
	return extract(subcoder, offset, size, extrsubcod, coder) ? 0 : X_ENOSPC;
}

#if !RPC_NO_IOVEC

#include <sys/uio.h>

static inline void *extriovec(void *closure, void *data, uint32_t length)
{
	struct iovec **ariovs = closure;
	ariovs[0]->iov_base = data;
	ariovs[0]->iov_len = length;
	ariovs[0]++;
	return ariovs[0] == ariovs[1] ? 0 : ariovs;
}

/**
 * Get part of output as a buffer
 */
int afb_rpc_coder_output_get_subiovec(afb_rpc_coder_t *coder, struct iovec *iov, int iovcnt, uint32_t size, uint32_t offset)
{
	struct iovec *ariovs[2] = { iov, iov + iovcnt };

	extract(coder, offset, size, extriovec, ariovs);

	return (int)(ariovs[0] - iov);
}

/**
 * Get the output as a iovec
 */
int afb_rpc_coder_output_get_iovec(afb_rpc_coder_t *coder, struct iovec *iov, int iovcnt)
{
	return afb_rpc_coder_output_get_subiovec(coder, iov, iovcnt, coder->size, 0);
}

int afb_rpc_coder_write_iovec(afb_rpc_coder_t *coder, const struct iovec *iov, int iovcnt)
{
	int rc, idx;
	for(idx = rc = 0 ; rc == 0 && idx < iovcnt ; idx++)
		if (iov[idx].iov_len > UINT32_MAX)
			rc = X_EINVAL;
		else
			rc = afb_rpc_coder_write(coder, iov[idx].iov_base, (uint32_t)iov[idx].iov_len);
	return rc;
}

int afb_rpc_coder_write_copy_iovec(afb_rpc_coder_t *coder, const struct iovec *iov, int iovcnt)
{
	int rc, idx;
	for(idx = rc = 0 ; rc == 0 && idx < iovcnt ; idx++)
		if (iov[idx].iov_len > UINT32_MAX)
			rc = X_EINVAL;
		else
			rc = afb_rpc_coder_write_copy(coder, iov[idx].iov_base, (uint32_t)iov[idx].iov_len);
	return rc;
}
#endif

