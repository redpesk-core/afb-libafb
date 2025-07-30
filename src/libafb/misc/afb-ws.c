/*
 * Copyright (C) 2015-2025 IoT.bzh Company
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


#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <poll.h>
#include <alloca.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "utils/websock.h"
#include "misc/afb-ws.h"
#include "core/afb-ev-mgr.h"
#include "sys/x-uio.h"
#include "sys/x-errno.h"
#include "sys/x-alloca.h"

/*
 * declaration of the websock interface for afb-ws
 */
static ssize_t aws_writev(struct afb_ws *ws, const struct iovec *iov, int iovcnt);
static ssize_t aws_readv(struct afb_ws *ws, const struct iovec *iov, int iovcnt);
static void aws_cork(struct afb_ws *ws, int onoff);
static void aws_on_close(struct afb_ws *ws, uint16_t code, size_t size);
static void aws_on_text(struct afb_ws *ws, int last, size_t size);
static void aws_on_binary(struct afb_ws *ws, int last, size_t size);
static void aws_on_continue(struct afb_ws *ws, int last, size_t size);
static void aws_on_readable(struct afb_ws *ws);
static void aws_on_error(struct afb_ws *ws, uint16_t code, const void *data, size_t size);

static struct websock_itf aws_itf = {
	.writev = (void*)aws_writev,
	.readv = (void*)aws_readv,
	.cork = (void*)aws_cork,

	.on_ping = NULL,
	.on_pong = NULL,
	.on_close = (void*)aws_on_close,
	.on_text = (void*)aws_on_text,
	.on_binary = (void*)aws_on_binary,
	.on_continue = (void*)aws_on_continue,
	.on_extension = NULL,

	.on_error = (void*)aws_on_error
};

/*
 * a common scheme of buffer handling
 */
struct buf
{
	char *buffer;
	size_t size;
};

/*
 * the state
 */
enum state
{
	waiting,
	reading_text,
	reading_binary,
	closing
};

/*
 * the afb_ws structure
 */
struct afb_ws
{
	int fd;			/* the socket file descriptor */
	enum state state;	/* current state */
	const struct afb_ws_itf *itf; /* the callback interface */
	void *closure;		/* closure when calling the callbacks */
	struct websock *ws;	/* the websock handler */
	struct ev_fd *efd;	/* the fdev for the socket */
	struct buf buffer;	/* the last read fragment */
	size_t reading_pos;	/* when state reading, read position */
	size_t reading_length;	/* when state reading, remaining length */
	int reading_last;	/* when state reading, is last? */
	uint16_t closing_code;	/* when state closing, the code */
};

/*
 * Returns the current buffer of 'ws' that is reset.
 */
static inline struct buf aws_pick_buffer(struct afb_ws *ws)
{
	struct buf result = ws->buffer;
	if (result.buffer)
		result.buffer[result.size] = 0;
	ws->buffer.buffer = NULL;
	ws->buffer.size = 0;
	return result;
}

/*
 * Clear the current buffer
 */
static inline void aws_clear_buffer(struct afb_ws *ws)
{
	ws->buffer.size = 0;
}

/*
 * Disconnect the websocket 'ws' and calls on_hangup if
 * 'call_on_hangup' is not null.
 */
static void aws_disconnect(struct afb_ws *ws, int call_on_hangup)
{
	struct websock *wsi = ws->ws;
	if (wsi != NULL) {
		ws->ws = NULL;
		ev_fd_unref(ws->efd);
		websock_destroy(wsi);
		free(ws->buffer.buffer);
		ws->state = waiting;
		if (call_on_hangup && ws->itf->on_hangup)
			ws->itf->on_hangup(ws->closure);
	}
}

static void evfdcb(struct ev_fd *efd, int fd, uint32_t revents, void *ws)
{
	if ((revents & EV_FD_HUP) != 0)
		afb_ws_hangup(ws);
	else if ((revents & EV_FD_IN) != 0)
		aws_on_readable(ws);
}

/*
 * Creates the afb_ws structure for the file descritor
 * 'fd' and the callbacks described by the interface 'itf'
 * and its 'closure'.
 * When the creation is a success, the systemd event loop 'eloop' is
 * used for handling event for 'fd'.
 *
 * Returns the handle for the afb_ws created or NULL on error.
 */
struct afb_ws *afb_ws_create(int fd, int autoclose, const struct afb_ws_itf *itf, void *closure)
{
	int rc;
	struct afb_ws *result;

	/* allocation */
	result = malloc(sizeof * result);
	if (result == NULL)
		goto error;

	/* init */
	rc = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &rc, (socklen_t)sizeof rc);
	result->fd = fd;
	result->state = waiting;
	result->itf = itf;
	result->closure = closure;
	result->buffer.buffer = NULL;
	result->buffer.size = 0;

	rc = afb_ev_mgr_add_fd(&result->efd, fd, EV_FD_IN, evfdcb, result, 0, autoclose);
	if (rc < 0)
		goto error2;

	/* creates the websocket */
	result->ws = websock_create_v13(&aws_itf, result);
	if (result->ws == NULL)
		goto error3;

	/* finalize */
	return result;

error3:
	ev_fd_unref(result->efd);
	autoclose = 0;
error2:
	free(result);
error:
	if (autoclose)
		close(fd);
	return NULL;
}

/*
 * Set the payload 'maxlen' for 'ws'
 */
void afb_ws_set_max_length(struct afb_ws *ws, size_t maxlen)
{
	if (ws->ws)
		websock_set_max_length(ws->ws, maxlen);
}

/*
 * Destroys the websocket 'ws'
 * It first hangup (but without calling on_hangup for safety reasons)
 * if needed.
 */
void afb_ws_destroy(struct afb_ws *ws)
{
	aws_disconnect(ws, 0);
	free(ws);
}

/*
 * Hangup the websocket 'ws'
 */
void afb_ws_hangup(struct afb_ws *ws)
{
	aws_disconnect(ws, 1);
}

/*
 * Set or not masking output
 */
void afb_ws_set_masking(struct afb_ws *ws, int onoff)
{
	websock_set_masking(ws->ws, onoff);
}

/*
 * Is the websocket 'ws' still connected ?
 */
int afb_ws_is_connected(struct afb_ws *ws)
{
	return ws->ws != NULL;
}

/*
 * Sends a 'close' command to the endpoint of 'ws' with the 'code' and the
 * 'reason' (that can be NULL and that else should not be greater than 123
 * characters).
 * Returns 0 on success or -1 in case of error.
 */
int afb_ws_close(struct afb_ws *ws, uint16_t code, const char *reason)
{
	if (ws->ws == NULL) /* disconnected */
		return X_EPIPE;
	return websock_close(ws->ws, code, reason, reason == NULL ? 0 : strlen(reason));
}

/*
 * Sends a 'close' command to the endpoint of 'ws' with the 'code' and the
 * 'reason' (that can be NULL and that else should not be greater than 123
 * characters).
 * Raise an error after 'close' command is sent.
 * Returns 0 on success or -1 in case of error.
 */
int afb_ws_error(struct afb_ws *ws, uint16_t code, const char *reason)
{
	if (ws->ws == NULL) /* disconnected */
		return X_EPIPE;
	return websock_error(ws->ws, code, reason, reason == NULL ? 0 : strlen(reason));
}

/*
 * Sends a 'text' of 'length' to the endpoint of 'ws'.
 * Returns 0 on success or -1 in case of error.
 */
int afb_ws_text(struct afb_ws *ws, const char *text, size_t length)
{
	if (ws->ws == NULL) /* disconnected */
		return X_EPIPE;
	return websock_text(ws->ws, 1, text, length);
}

/*
 * Sends a variable list of texts to the endpoint of 'ws'.
 * Returns 0 on success or -1 in case of error.
 */
int afb_ws_texts(struct afb_ws *ws, ...)
{
	va_list args;
	struct iovec ios[32];
	int count;
	const char *s;

	if (ws->ws == NULL) /* disconnected */
		return X_EPIPE;

	count = 0;
	va_start(args, ws);
	s = va_arg(args, const char *);
	while (s != NULL) {
		if (count == 32)
			return X_EINVAL;
		ios[count].iov_base = (void*)s;
		ios[count].iov_len = strlen(s);
		count++;
		s = va_arg(args, const char *);
	}
	va_end(args);
	return websock_text_v(ws->ws, 1, ios, count);
}

/*
 * Sends a text data described in the 'count' 'iovec' to the endpoint of 'ws'.
 * Returns 0 on success or -1 in case of error.
 */
int afb_ws_text_v(struct afb_ws *ws, const struct iovec *iovec, int count)
{
	if (ws->ws == NULL) /* disconnected */
		return X_EPIPE;
	return websock_text_v(ws->ws, 1, iovec, count);
}

/*
 * Sends a binary 'data' of 'length' to the endpoint of 'ws'.
 * Returns 0 on success or -1 in case of error.
 */
int afb_ws_binary(struct afb_ws *ws, const void *data, size_t length)
{
	if (ws->ws == NULL) /* disconnected */
		return X_EPIPE;
	return websock_binary(ws->ws, 1, data, length);
}

/*
 * Sends a binary data described in the 'count' 'iovec' to the endpoint of 'ws'.
 * Returns 0 on success or -1 in case of error.
 */
int afb_ws_binary_v(struct afb_ws *ws, const struct iovec *iovec, int count)
{
	if (ws->ws == NULL) /* disconnected */
		return X_EPIPE;
	return websock_binary_v(ws->ws, 1, iovec, count);
}

/*
 * callback for writing data
 */
static ssize_t aws_writev(struct afb_ws *ws, const struct iovec *iov, int iovcnt)
{
	int i;
	ssize_t rc, sz, dsz;
	struct iovec *iov2;
	struct pollfd pfd;

	/* compute the size */
	dsz = 0;
	i = 0;
	while (i < iovcnt) {
		dsz += (ssize_t)iov[i++].iov_len;
		if (dsz < 0)
			return X_EINVAL;
	}
	if (dsz == 0)
		return 0;

	/* write the data */
	iov2 = (struct iovec*)iov;
	sz = dsz;
	for (;;) {
		rc = writev(ws->fd, iov2, iovcnt);
		if (rc < 0) {
			if (errno == EINTR)
				continue;
			if (errno != EAGAIN)
				return -1;
		} else {
			dsz -= rc;
			if (dsz == 0)
				return sz; /* all is read */

			/* skip fully written blocs */
			i = 0;
			while (rc >= (ssize_t)iov2[i].iov_len)
				rc -= (ssize_t)iov2[i++].iov_len;

			iovcnt -= i;
			if (iov2 != iov) {
				/* copied block */
				iov2 += i;
				iov2->iov_base = (char*)iov2->iov_base + rc;
				iov2->iov_len -= (size_t)rc;
			}
			else if (rc == 0) {
				/* initial block */
				iov2 += i;
				iov += i;
			}
			else {
				/* needed copy block */
				iov2 = alloca((unsigned)iovcnt * sizeof *iov2);
				memcpy(iov2, &iov[i], (unsigned)iovcnt * sizeof *iov2);
				iov2->iov_base = (char*)iov2->iov_base + rc;
				iov2->iov_len -= (size_t)rc;
			}
		}
		pfd.fd = ws->fd;
		pfd.events = POLLOUT;
		poll(&pfd, 1, 10);
	}
}

/*
 * callback for reading data
 */
static ssize_t aws_readv(struct afb_ws *ws, const struct iovec *iov, int iovcnt)
{
	ssize_t rc;
	do {
		rc = readv(ws->fd, iov, iovcnt);
	} while(rc == -1 && errno == EINTR);
	if (rc == 0)
		rc = X_EPIPE;
	else if (rc < 0)
		rc = -errno;
	return rc;
}

/*
 * callback for corking emissions
 */
static void aws_cork(struct afb_ws *ws, int onoff)
{
#if !__ZEPHYR__
	int optval = !!onoff;
	setsockopt(ws->fd, IPPROTO_TCP, TCP_CORK, &optval, sizeof optval);
#endif
}

/*
 * Reads from the websocket handled by 'ws' the expected data
 * and append it to the current buffer of 'ws'.
 */
static int aws_read_async(struct afb_ws *ws)
{
	ssize_t sz;
	struct buf b;
	enum state s;

	if (ws->reading_length) {
		sz = websock_read(ws->ws, &ws->buffer.buffer[ws->reading_pos], ws->reading_length);
		if (sz <= 0)
			return (int)sz;
		ws->reading_pos += (size_t)sz;
		ws->reading_length -= (size_t)sz;
	}
	if (ws->reading_length == 0 && ws->reading_last) {
		s = ws->state;
		ws->state = waiting;
		b = aws_pick_buffer(ws);
		switch (s) {
		case reading_text:
			ws->itf->on_text(ws->closure, b.buffer, b.size);
			break;
		case reading_binary:
			ws->itf->on_binary(ws->closure, b.buffer, b.size);
			break;
		case closing:
			ws->itf->on_close(ws->closure, ws->closing_code, b.buffer, b.size);
			break;
		default:
			free(b.buffer);
			break;
		}
	}
	return 0;
}

static int aws_read_async_continue(struct afb_ws *ws, size_t size)
{
	char *buffer;
	size_t bufsz;

	ws->reading_length = size;
	bufsz = ws->buffer.size + size;
	buffer = realloc(ws->buffer.buffer, bufsz + 1);
	if (!buffer) {
		ws->reading_length = 0; /* TODO: state error */
		aws_read_async(ws);
		return X_ENOMEM;
	}
	ws->buffer.buffer = buffer;
	ws->buffer.size = bufsz;
	buffer[bufsz] = 0;
	aws_read_async(ws);
	return 0;
}

static int aws_read_async_start(struct afb_ws *ws, size_t size)
{
	ws->reading_pos = 0;
	ws->buffer.size = 0;
	return aws_read_async_continue(ws, size);
}

/*
 * callback on incoming data
 */
static void aws_on_readable(struct afb_ws *ws)
{
	int rc;

	assert(ws->ws != NULL);
	rc = ws->state == waiting? websock_dispatch(ws->ws, 0) : aws_read_async(ws);
	if (rc == X_EPIPE)
		afb_ws_hangup(ws);
}

/*
 * Callback when 'close' command received from 'ws' with 'code' and 'size'.
 */
static void aws_on_close(struct afb_ws *ws, uint16_t code, size_t size)
{
	ws->state = waiting;
	aws_clear_buffer(ws);
	if (ws->itf->on_close == NULL) {
		websock_drop(ws->ws);
		afb_ws_hangup(ws);
	}
	else {
		ws->state = closing;
		ws->reading_last = 1;
		ws->closing_code = code;
		aws_read_async_start(ws, size);
		aws_read_async(ws);
	}
}

/*
 * Drops any incoming data and send an error of 'code'
 */
static void aws_drop_error(struct afb_ws *ws, uint16_t code)
{
	ws->state = waiting;
	aws_clear_buffer(ws);
	websock_drop(ws->ws);
	websock_error(ws->ws, code, NULL, 0);
}

/*
 * Callback when 'text' message received from 'ws' with 'size' and possibly 'last'.
 */
static void aws_on_text(struct afb_ws *ws, int last, size_t size)
{
	if (ws->state != waiting)
		aws_drop_error(ws, WEBSOCKET_CODE_PROTOCOL_ERROR);
	else if (ws->itf->on_text == NULL)
		aws_drop_error(ws, WEBSOCKET_CODE_CANT_ACCEPT);
	else {
		ws->state = reading_text;
		ws->reading_last = last;
		aws_read_async_start(ws, size);
	}
}

/*
 * Callback when 'binary' message received from 'ws' with 'size' and possibly 'last'.
 */
static void aws_on_binary(struct afb_ws *ws, int last, size_t size)
{
	if (ws->state != waiting)
		aws_drop_error(ws, WEBSOCKET_CODE_PROTOCOL_ERROR);
	else if (ws->itf->on_binary == NULL)
		aws_drop_error(ws, WEBSOCKET_CODE_CANT_ACCEPT);
	else {
		ws->state = reading_binary;
		ws->reading_last = last;
		aws_read_async_start(ws, size);
	}
}

/*
 * Callback when 'continue' command received from 'ws' with 'code' and 'size'.
 */
static void aws_on_continue(struct afb_ws *ws, int last, size_t size)
{
	if (ws->state == waiting)
		aws_drop_error(ws, WEBSOCKET_CODE_PROTOCOL_ERROR);
	else {
		ws->reading_last = last;
		aws_read_async_continue(ws, size);
	}
}

/*
 * Callback when 'close' command is sent to 'ws' with 'code' and 'size'.
 */
static void aws_on_error(struct afb_ws *ws, uint16_t code, const void *data, size_t size)
{
	if (ws->itf->on_error != NULL)
		ws->itf->on_error(ws->closure, code, data, size);
	else
		afb_ws_hangup(ws);
}


