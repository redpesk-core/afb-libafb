/*
 * Copyright (C) 2015-2021 IoT.bzh Company
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


#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "sys/x-buf.h"
#include "sys/x-errno.h"
#include "utils/ws.h"

#if !defined(WS_DEFAULT_MAXLENGTH)
#  define WS_DEFAULT_MAXLENGTH 1048500  /* 76 less than 1M, probably enougth for headers */
#endif

#define FRAME_GET_FIN(BYTE)         (((BYTE) >> 7) & 0x01)
#define FRAME_GET_RSV1(BYTE)        (((BYTE) >> 6) & 0x01)
#define FRAME_GET_RSV2(BYTE)        (((BYTE) >> 5) & 0x01)
#define FRAME_GET_RSV3(BYTE)        (((BYTE) >> 4) & 0x01)
#define FRAME_GET_RSV123(BYTE)      (((BYTE) >> 4) & 0x07)
#define FRAME_GET_OPCODE(BYTE)      ( (BYTE)       & 0x0F)
#define FRAME_GET_MASK(BYTE)        (((BYTE) >> 7) & 0x01)
#define FRAME_GET_PAYLOAD_LEN(BYTE) ( (BYTE)       & 0x7F)

#define FRAME_MAKE_FIN(BYTE)         (((BYTE) & 0x01) << 7)
#define FRAME_MAKE_RSV1(BYTE)        (((BYTE) & 0x01) << 6)
#define FRAME_MAKE_RSV2(BYTE)        (((BYTE) & 0x01) << 5)
#define FRAME_MAKE_RSV3(BYTE)        (((BYTE) & 0x01) << 4)
#define FRAME_MAKE_RSV123(BYTE)      (((BYTE) & 0x07) << 4)
#define FRAME_MAKE_OPCODE(BYTE)       ((BYTE) & 0x0F)
#define FRAME_MAKE_MASK(BYTE)        (((BYTE) & 0x01) << 7)
#define FRAME_MAKE_LENGTH(X64, IDX)  (char)((sizeof(X64)) <= (IDX) ? 0 : (((X64) >> ((IDX)*8)) & 0xFF))

#define OPCODE_CONTINUATION 0x0
#define OPCODE_TEXT         0x1
#define OPCODE_BINARY       0x2
#define OPCODE_CLOSE        0x8
#define OPCODE_PING         0x9
#define OPCODE_PONG         0xA

#define STATE_INIT    0
#define STATE_START   1
#define STATE_LENGTH  2
#define STATE_DATA    3

static size_t default_maxlength = WS_DEFAULT_MAXLENGTH;

/**
 * Apply an xor of the 'mask' to the 'buffer' of 'size'
 * @param mask    the mask to xor in memory order
 * @param buffer  the buffer to alter
 * @param count   the count of bytes
 * @return the mask as it has to be applied for next bytes
 */
static uint32_t unmask(uint32_t mask, char *buffer, size_t count)
{
	uint32_t *b32;
	char m, *b8, *m8;

	/* ensure uint32_t alignment */
	m8 = (char*)&mask;
	b8 = buffer;
	while (count && ((sizeof(uint32_t) - 1) & (uintptr_t) b8)) {
		m = m8[0];
		m8[0] = m8[1];
		m8[1] = m8[2];
		m8[2] = m8[3];
		m8[3] = m;
		*b8++ ^= m;
		count--;
	}
	/* uint32_ aligned xors */
	b32 = (uint32_t*)b8;
	while (count >= sizeof(uint32_t)) {
		*b32++ ^= mask;
		count -= sizeof(uint32_t);
	}
	/* terminates */
	b8 = (char*)b32;
	while (count) {
		m = m8[0];
		m8[0] = m8[1];
		m8[1] = m8[2];
		m8[2] = m8[3];
		m8[3] = m;
		*b8++ ^= m;
		count--;
	}
	return mask;
}


static ssize_t _writev_(ws_t *ws, const x_buf_t *bufs, int iovcnt)
{
	return ws->itf->writev(ws, bufs, iovcnt);
}

static int ws_send_internal_v(ws_t *ws, char first, const x_buf_t *bufs, int count)
{
	x_buf_t locbufs[32];
	int i, j;
	size_t pos, size, len;
	ssize_t rc;
	char header[32];

	/* checks count */
	if (count < 0 || (count + 1) > (int)(sizeof locbufs / sizeof * locbufs))
		return X_EINVAL;

	/* computes the size */
	size = 0;
	i = 1;
	for (j = 0 ; j < count ; j++) {
		locbufs[i].base = bufs[j].base;
		len = bufs[j].len;
		if (len != 0) {
			locbufs[i].len = len;
			size += len;
			i++;
		}
	}

	/* makes the header */
	pos = 0;
	header[pos++] = first;
	size = (uint64_t) size;
	if (size < 126) {
		header[pos++] = FRAME_MAKE_MASK(0) | FRAME_MAKE_LENGTH(size, 0);
	} else {
		if (size < 65536) {
			header[pos++] = FRAME_MAKE_MASK(0) | 126;
		} else {
			header[pos++] = FRAME_MAKE_MASK(0) | 127;
			header[pos++] = FRAME_MAKE_LENGTH(size, 7);
			header[pos++] = FRAME_MAKE_LENGTH(size, 6);
			header[pos++] = FRAME_MAKE_LENGTH(size, 5);
			header[pos++] = FRAME_MAKE_LENGTH(size, 4);
			header[pos++] = FRAME_MAKE_LENGTH(size, 3);
			header[pos++] = FRAME_MAKE_LENGTH(size, 2);
		}
		header[pos++] = FRAME_MAKE_LENGTH(size, 1);
		header[pos++] = FRAME_MAKE_LENGTH(size, 0);
	}

	/* allocates the vec */
	locbufs[0].base = header;
	locbufs[0].len = pos;
	rc = _writev_(ws, locbufs, i);

	return rc < 0 ? -errno : 0;
}

static int ws_send_internal(ws_t *ws, char first, const void *buffer, size_t size)
{
	x_buf_t bufs;

	bufs.base = (void *)buffer;
	bufs.len = size;
	return ws_send_internal_v(ws, first, &bufs, 1);
}

static inline int ws_send_v(ws_t *ws, int last, int rsv123, int opcode, const x_buf_t bufs[], int count)
{
	char first = (char)(FRAME_MAKE_FIN(last)
				| FRAME_MAKE_RSV123(rsv123)
				| FRAME_MAKE_OPCODE(opcode));
	return ws_send_internal_v(ws, first, bufs, count);
}

static inline int ws_send(ws_t *ws, int last, int rsv123, int opcode, const void *buffer, size_t size)
{
	char first = (char)(FRAME_MAKE_FIN(last)
				| FRAME_MAKE_RSV123(rsv123)
				| FRAME_MAKE_OPCODE(opcode));
	return ws_send_internal(ws, first, buffer, size);
}

int ws_close_empty(ws_t *ws)
{
	return ws_close(ws, WS_CODE_NOT_SET, NULL, 0);
}

int ws_close(ws_t *ws, uint16_t code, const void *data, size_t length)
{
	char buffer[2];
	x_buf_t bufs[2];

	if (code == WS_CODE_NOT_SET && length == 0)
		return ws_send(ws, 1, 0, OPCODE_CLOSE, NULL, 0);

	/* checks the length */
	if (length > 123)
		return X_EINVAL;

	/* prepare the buffer */
	buffer[0] = (char)((code >> 8) & 0xFF);
	buffer[1] = (char)(code & 0xFF);

	/* Send server-side closing handshake */
	bufs[0].base = (void *)buffer;
	bufs[0].len = 2;
	bufs[1].base = (void *)data;
	bufs[1].len = length;
	return ws_send_v(ws, 1, 0, OPCODE_CLOSE, bufs, 2);
}

int ws_ping(ws_t *ws, const void *data, size_t length)
{
	/* checks the length */
	if (length > 125)
		return X_EINVAL;

	return ws_send(ws, 1, 0, OPCODE_PING, data, length);
}

int ws_pong(ws_t *ws, const void *data, size_t length)
{
	/* checks the length */
	if (length > 125)
		return X_EINVAL;

	return ws_send(ws, 1, 0, OPCODE_PONG, data, length);
}

int ws_text(ws_t *ws, int last, const void *text, size_t length)
{
	return ws_send(ws, last, 0, OPCODE_TEXT, text, length);
}

int ws_text_v(ws_t *ws, int last, const x_buf_t bufs[], int count)
{
	return ws_send_v(ws, last, 0, OPCODE_TEXT, bufs, count);
}

int ws_binary(ws_t *ws, int last, const void *data, size_t length)
{
	return ws_send(ws, last, 0, OPCODE_BINARY, data, length);
}

int ws_binary_v(ws_t *ws, int last, const x_buf_t bufs[], int count)
{
	return ws_send_v(ws, last, 0, OPCODE_BINARY, bufs, count);
}

int ws_continue(ws_t *ws, int last, const void *data, size_t length)
{
	return ws_send(ws, last, 0, OPCODE_CONTINUATION, data, length);
}

int ws_continue_v(ws_t *ws, int last, const x_buf_t bufs[], int count)
{
	return ws_send_v(ws, last, 0, OPCODE_CONTINUATION, bufs, count);
}

int ws_error(ws_t *ws, uint16_t code, const void *data, size_t size)
{
	int rc = ws_close(ws, code, data, size);
	if (ws->itf->on_error != NULL)
		ws->itf->on_error(ws, code, data, size);
	return rc;
}

static int _addbuf_(ws_t *ws, const x_buf_t *buffer)
{
	int i = WS_BUFS_COUNT;
	while(i) {
		if (ws->uses[--i] == 0) {
			ws->uses[i] = 1;
			ws->bufs[i] = *buffer;
			return i;
		}
	}
	return X_EBUSY;
}

static void _getbuf_(ws_t *ws, int ibuf)
{
	ws->uses[ibuf]++;
}

static void _putbuf_(ws_t *ws, int ibuf)
{
	if (--ws->uses[ibuf] == 0)
		free(ws->bufs[ibuf].base);
}

static void _putbufat_(ws_t *ws, char *ptr)
{
	int i = WS_BUFS_COUNT;
	while(i) {
		if (ws->uses[--i]) {
			if (ws->bufs[i].base <= ptr && ptr < &ws->bufs[i].base[ws->bufs[i].len]) {
				_putbuf_(ws, i);
				break;
			}
		}
	}
}

int _dispatch_(ws_t *ws, int ibuf)
{
	uint16_t code;
	x_buf_t buf;
	int fin;
	char *buffer = ws->bufs[ibuf].base;
	size_t length = ws->bufs[ibuf].len;

	while (length)
	switch (ws->state) {
	case STATE_INIT:
		ws->lenhead = 0;
		ws->szhead = 2;
		ws->state = STATE_START;
		/*@fallthrough@*/

	case STATE_START:
		/* read the header */
		while (ws->lenhead < ws->szhead) {
			if (length == 0)
				goto end;
			ws->header[ws->lenhead++] = (unsigned char)*buffer++;
			length--;
		}

		/* fast track */
		switch (FRAME_GET_OPCODE(ws->header[0])) {
		case OPCODE_CONTINUATION:
		case OPCODE_TEXT:
		case OPCODE_BINARY:
			break;
		case OPCODE_CLOSE:
		case OPCODE_PING:
		case OPCODE_PONG:
			/* sanity checks for control headers */
			if (FRAME_GET_RSV123(ws->header[0]) != 0)
				goto protocol_error;
			if (FRAME_GET_PAYLOAD_LEN(ws->header[1]) > 125)
				goto protocol_error;
			break;
		default:
			break;
		}
		/* update heading size */
		switch (FRAME_GET_PAYLOAD_LEN(ws->header[1])) {
		case 127:
			ws->szhead += 8;
			break;
		case 126:
			ws->szhead += 2;
			break;
		default:
			break;
		}
		if (FRAME_GET_MASK(ws->header[1]))
			ws->szhead += 4;
		ws->state = STATE_LENGTH;
		/*@fallthrough@*/

	case STATE_LENGTH:
		/* continue to read the header */
		/* read the header */
		while (ws->lenhead < ws->szhead) {
			if (length == 0)
				goto end;
			ws->header[ws->lenhead++] = (unsigned char)*buffer++;
			length--;
		}

		/* compute length */
		switch (FRAME_GET_PAYLOAD_LEN(ws->header[1])) {
		case 127:
			ws->length = (((uint64_t) ws->header[2]) << 56)
			    | (((uint64_t) ws->header[3]) << 48)
			    | (((uint64_t) ws->header[4]) << 40)
			    | (((uint64_t) ws->header[5]) << 32)
			    | (((uint64_t) ws->header[6]) << 24)
			    | (((uint64_t) ws->header[7]) << 16)
			    | (((uint64_t) ws->header[8]) << 8)
			    | (uint64_t) ws->header[9];
			break;
		case 126:
			ws->length = (((uint64_t) ws->header[2]) << 8)
			    | (uint64_t) ws->header[3];
			break;
		default:
			ws->length = FRAME_GET_PAYLOAD_LEN(ws->header[1]);
			break;
		}
		if (ws->length > ws->maxlength)
			goto too_long_error;

		/* compute mask */
		if (FRAME_GET_MASK(ws->header[1])) {
			((unsigned char *)&ws->mask)[0] = ws->header[ws->szhead - 4];
			((unsigned char *)&ws->mask)[1] = ws->header[ws->szhead - 3];
			((unsigned char *)&ws->mask)[2] = ws->header[ws->szhead - 2];
			((unsigned char *)&ws->mask)[3] = ws->header[ws->szhead - 1];
		} else
			ws->mask = 0;

		/* all heading fields are known, process */
		ws->state = STATE_DATA;
		/*@fallthrough@*/

	case STATE_DATA:
		buf.base = ws->length ? buffer : NULL;
		if (ws->length <= length) {
			fin = FRAME_GET_FIN(ws->header[0]);
			buf.len = (size_t)ws->length;
			ws->state = STATE_INIT;
		} else {
			fin = 0;
			buf.len = length;
		}
		length -= buf.len;

		/* unmasking */
		if (ws->mask != 0)
			ws->mask = unmask(ws->mask, buf.base, buf.len);

		_getbuf_(ws, ibuf);

		/* check processing of extensions */
		if (ws->itf->on_extension != NULL) {
			if (ws->itf->on_extension(ws,
					fin,
					&buf,
					FRAME_GET_RSV123(ws->header[0]),
					FRAME_GET_OPCODE(ws->header[0]))) {
				break;
			}
		}

		/* not an extension case */
		if (FRAME_GET_RSV123(ws->header[0]) != 0)
			goto protocol_error_put;

		/* handle */
		switch (FRAME_GET_OPCODE(ws->header[0])) {
		case OPCODE_CONTINUATION:
			ws->itf->on_continue(ws, fin, &buf);
			break;
		case OPCODE_TEXT:
			ws->itf->on_text(ws, fin, &buf);
			break;
		case OPCODE_BINARY:
			ws->itf->on_binary(ws, fin, &buf);
			break;
		case OPCODE_CLOSE:
			if (!fin)
				goto protocol_error_put;
			if (buf.len == 0)
				code = WS_CODE_NOT_SET;
			else if (buf.len == 1)
				goto protocol_error;
			else {
				code = (uint16_t)(uint8_t)(buf.base[0]);
				code = (uint16_t)(code << 8);
				code = (uint16_t)(code | (uint16_t)(uint8_t)(buf.base[1]));
				buf.base += 2;
				buf.len -=2;
			}
			ws->itf->on_close(ws, code, &buf);
			return 0;
		case OPCODE_PING:
			if (!fin)
				goto protocol_error_put;
			if (ws->itf->on_ping)
				ws->itf->on_ping(ws, &buf);
			else
				ws_pong(ws, buf.base, buf.len);
			break;
		case OPCODE_PONG:
			if (!fin)
				goto protocol_error_put;
			if (ws->itf->on_pong)
				ws->itf->on_pong(ws, &buf);
			break;
		default:
			goto protocol_error_put;
		}
		break;
	}
end:
	return 0;

 too_long_error:
	ws_error(ws, WS_CODE_MESSAGE_TOO_LARGE, NULL, 0);
	return X_EMSGSIZE;


 protocol_error_put:
	_putbuf_(ws, ibuf);
 protocol_error:
	ws_error(ws, WS_CODE_PROTOCOL_ERROR, NULL, 0);
	return X_EPROTO;
}

void ws_release(ws_t *ws, const x_buf_t *buffer)
{
	_putbufat_(ws, buffer->base);
}

int ws_dispatch(ws_t *ws, const x_buf_t *buffers, int count)
{
	int rc = 0, ibuf;
	while (count && rc == 0) {
		ibuf = _addbuf_(ws, buffers);
		if (ibuf < 0) {
			rc = ibuf;
		} else {
			rc = _dispatch_(ws, ibuf);
			_putbuf_(ws, ibuf);
			buffers++;
			count --;
		}
	}
	while (count) {
		free (buffers++->base);
		count --;
	}
	return rc;
}

int ws_init(ws_t *ws, const ws_itf_t *itf)
{
	memset(ws, 0, sizeof *ws);
	ws->itf = itf;
	ws->state = STATE_INIT;
	ws->maxlength = default_maxlength;
	return 0;
}

void ws_set_default_max_length(size_t maxlen)
{
	default_maxlength = maxlen;
}

void ws_set_max_length(ws_t *ws, size_t maxlen)
{
	ws->maxlength = (uint64_t)maxlen;
}

const char *ws_strerror(uint16_t code)
{
	static const char *msgs[] = {
		"OK",                /* 1000 */
		"GOING_AWAY",        /* 1001 */
		"PROTOCOL_ERROR",    /* 1002 */
		"CANT_ACCEPT",       /* 1003 */
		"RESERVED",          /* 1004 */
		"NOT_SET",           /* 1005 */
		"ABNORMAL",          /* 1006 */
		"INVALID_UTF8",      /* 1007 */
		"POLICY_VIOLATION",  /* 1008 */
		"MESSAGE_TOO_LARGE", /* 1009 */
		"EXPECT_EXTENSION",  /* 1010 */
		"INTERNAL_ERROR",    /* 1011 */
	};
	if (code < 1000 || (code - 1000) >= (int)(sizeof msgs / sizeof *msgs))
		return "?";
	return msgs[code - 1000];
}

