/*
 * Copyright (C) 2015-2020 IoT.bzh Company
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

#define _GNU_SOURCE

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <endian.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>

#include <json-c/json.h>

#include "misc/afb-ws.h"
#include "wsapi/afb-wsapi.h"
#include "sys/fdev.h"
#include "sys/verbose.h"

/******** implementation of internal binder protocol per api **************/
/*

This protocol is asymmetric: there is a client and a server

The client can require the following actions:

  - call a verb

  - ask for description

The server must reply to the previous actions by

  - answering success or failure of the call

  - answering the required description

The server can also within the context of a call

  - subscribe or unsubscribe an event

For the purpose of handling events the server can:

  - create/destroy an event

  - push or broadcast data as an event

  - signal unexpected event

*/
/************** constants for protocol definition *************************/

#define CHAR_FOR_CALL             'K'	/* client -> server */
#define CHAR_FOR_REPLY            'k'	/* server -> client */
#define CHAR_FOR_EVT_BROADCAST    'B'	/* server -> client */
#define CHAR_FOR_EVT_ADD          'E'	/* server -> client */
#define CHAR_FOR_EVT_DEL          'e'	/* server -> client */
#define CHAR_FOR_EVT_PUSH         'P'	/* server -> client */
#define CHAR_FOR_EVT_SUBSCRIBE    'X'	/* server -> client */
#define CHAR_FOR_EVT_UNSUBSCRIBE  'x'	/* server -> client */
#define CHAR_FOR_EVT_UNEXPECTED   'U'	/* client -> server */
#define CHAR_FOR_DESCRIBE         'D'	/* client -> server */
#define CHAR_FOR_DESCRIPTION      'd'	/* server -> client */
#define CHAR_FOR_TOKEN_ADD        'T'	/* client -> server */
#define CHAR_FOR_TOKEN_DROP       't'	/* client -> server */
#define CHAR_FOR_SESSION_ADD      'S'	/* client -> server */
#define CHAR_FOR_SESSION_DROP     's'	/* client -> server */
#define CHAR_FOR_VERSION_OFFER    'V'	/* client -> server */
#define CHAR_FOR_VERSION_SET      'v'	/* server -> client */

/******************* manage versions *****************************/

#define WSAPI_IDENTIFIER        02723012011  /* wsapi: 23.19.1.16.9 */

#define WSAPI_VERSION_UNSET	0
#define WSAPI_VERSION_1		1

#define WSAPI_VERSION_MIN	WSAPI_VERSION_1
#define WSAPI_VERSION_MAX	WSAPI_VERSION_1

/******************* maximum count of ids ***********************/

#define ACTIVE_ID_MAX		4095

/******************* handling messages***************************/

/**
 * pending requests that require reply
 */
struct pending
{
	/** link to the next */
	struct pending *next;

	/** closure for the reply */
	void *closure;

	/** identifier of the request */
	uint16_t requestid;

	/** type of the sent request */
	enum afb_wsapi_msg_type type;
};

/**
 * handler for a message
 */
struct msg {
	/** the public message */
	struct afb_wsapi_msg msg;

	/** the json object if relevant */
	struct json_object *json;

	/** the wsapi */
	struct afb_wsapi *wsapi;

	/** the incoming buffer */
	char *buffer;

	/** count of use */
	uint16_t refcount;

	/** message id if relevant */
	uint16_t requestid;
};

/******************* proto description for client or servers ******************/

struct afb_wsapi
{
	/* count of references */
	uint16_t refcount;

	/* id generator */
	uint16_t genid;

	/* count actives ids */
	uint16_t idcount;

	/* version */
	uint8_t version;

	/* resource control */
	pthread_mutex_t mutex;

	/* websocket */
	struct afb_ws *ws;

	/* the closure */
	void *closure;

	/* pendings */
	struct pending *pendings;

	/* the interface */
	const struct afb_wsapi_itf *itf;
};

/******************* streaming objects **********************************/

#define WRITEBUF_COUNT_MAX	32
#define WRITEBUF_BUFSZ		(WRITEBUF_COUNT_MAX * sizeof(uint32_t))

struct writebuf
{
	int iovcount, bufcount;
	struct iovec iovec[WRITEBUF_COUNT_MAX];
	char buf[WRITEBUF_BUFSZ];
};

struct readbuf
{
	char *base, *head, *end;
};

/******************* serialization part **********************************/

static char *readbuf_get(struct readbuf *rb, uint32_t length)
{
	char *before = rb->head;
	char *after = before + length;
	if (after > rb->end)
		return 0;
	rb->head = after;
	return before;
}

static int readbuf_getat(struct readbuf *rb, void *to, uint32_t length)
{
	char *head = readbuf_get(rb, length);
	if (!head)
		return 0;
	memcpy(to, head, length);
	return 1;
}

__attribute__((unused))
static int readbuf_char(struct readbuf *rb, char *value)
{
	return readbuf_getat(rb, value, sizeof *value);
}

static int readbuf_uint32(struct readbuf *rb, uint32_t *value)
{
	int r = readbuf_getat(rb, value, sizeof *value);
	if (r)
		*value = le32toh(*value);
	return r;
}

static int readbuf_uint16(struct readbuf *rb, uint16_t *value)
{
	int r = readbuf_getat(rb, value, sizeof *value);
	if (r)
		*value = le16toh(*value);
	return r;
}

static int readbuf_uint8(struct readbuf *rb, uint8_t *value)
{
	return readbuf_getat(rb, value, sizeof *value);
}

static int _readbuf_string_(struct readbuf *rb, const char **value, size_t *length, int nulok)
{
	uint32_t len;
	if (!readbuf_uint32(rb, &len))
		return 0;
	if (!len) {
		if (!nulok)
			return 0;
		*value = NULL;
		if (length)
			*length = 0;
		return 1;
	}
	if (length)
		*length = (size_t)(len - 1);
	return (*value = readbuf_get(rb, len)) != NULL &&  rb->head[-1] == 0;
}


static int readbuf_string(struct readbuf *rb, const char **value, size_t *length)
{
	return _readbuf_string_(rb, value, length, 0);
}

static int readbuf_nullstring(struct readbuf *rb, const char **value, size_t *length)
{
	return _readbuf_string_(rb, value, length, 1);
}

static int writebuf_put(struct writebuf *wb, const void *value, size_t length)
{
	int i = wb->iovcount;
	if (i == WRITEBUF_COUNT_MAX)
		return 0;
	wb->iovec[i].iov_base = (void*)value;
	wb->iovec[i].iov_len = length;
	wb->iovcount = i + 1;
	return 1;
}

static int writebuf_putbuf(struct writebuf *wb, const void *value, int length)
{
	char *p;
	int i = wb->iovcount, n = wb->bufcount, nafter;

	/* check enough length */
	nafter = n + length;
	if (nafter > WRITEBUF_BUFSZ)
		return 0;

	/* get where to store */
	p = &wb->buf[n];
	if (i && p == (((char*)wb->iovec[i - 1].iov_base) + wb->iovec[i - 1].iov_len))
		/* increase previous iovec */
		wb->iovec[i - 1].iov_len += (size_t)length;
	else if (i == WRITEBUF_COUNT_MAX)
		/* no more iovec */
		return 0;
	else {
		/* new iovec */
		wb->iovec[i].iov_base = p;
		wb->iovec[i].iov_len = (size_t)length;
		wb->iovcount = i + 1;
	}
	/* store now */
	memcpy(p, value, (size_t)length);
	wb->bufcount = nafter;
	return 1;
}

__attribute__((unused))
static int writebuf_char(struct writebuf *wb, char value)
{
	return writebuf_putbuf(wb, &value, 1);
}

static int writebuf_uint32(struct writebuf *wb, uint32_t value)
{
	value = htole32(value);
	return writebuf_putbuf(wb, &value, (int)sizeof value);
}

static int writebuf_uint16(struct writebuf *wb, uint16_t value)
{
	value = htole16(value);
	return writebuf_putbuf(wb, &value, (int)sizeof value);
}

static int writebuf_uint8(struct writebuf *wb, uint8_t value)
{
	return writebuf_putbuf(wb, &value, (int)sizeof value);
}

static int writebuf_string_length(struct writebuf *wb, const char *value, size_t length)
{
	uint32_t len = (uint32_t)++length;
	return (size_t)len == length && len && writebuf_uint32(wb, len) && writebuf_put(wb, value, length);
}

static int writebuf_string(struct writebuf *wb, const char *value)
{
	return writebuf_string_length(wb, value, strlen(value));
}

static int writebuf_nullstring(struct writebuf *wb, const char *value)
{
	return value ? writebuf_string_length(wb, value, strlen(value)) : writebuf_uint32(wb, 0);
}

/******************* sending messages *****************/

static int proto_write(struct afb_wsapi *wsapi, struct writebuf *wb)
{
	int rc;
	struct afb_ws *ws;

	pthread_mutex_lock(&wsapi->mutex);
	ws = wsapi->ws;
	if (ws == NULL) {
		errno = EPIPE;
		rc = -1;
	} else {
		rc = afb_ws_binary_v(ws, wb->iovec, wb->iovcount);
		if (rc > 0)
			rc = 0;
	}
	pthread_mutex_unlock(&wsapi->mutex);
	return rc;
}

static int send_version_offer_1(struct afb_wsapi *wsapi, uint8_t version)
{
	int rc = -1;
	struct writebuf wb = { .iovcount = 0, .bufcount = 0 };

	if (writebuf_char(&wb, CHAR_FOR_VERSION_OFFER)
	 && writebuf_uint32(&wb, WSAPI_IDENTIFIER)
	 && writebuf_uint8(&wb, 1) /* offer one version */
	 && writebuf_uint8(&wb, version))
		rc = proto_write(wsapi, &wb);
	return rc;
}

static int send_version_set(struct afb_wsapi *wsapi, uint8_t version)
{
	int rc = -1;
	struct writebuf wb = { .iovcount = 0, .bufcount = 0 };

	if (writebuf_char(&wb, CHAR_FOR_VERSION_SET)
	 && writebuf_uint8(&wb, version))
		rc = proto_write(wsapi, &wb);
	return rc;
}

/******************* handling messages *****************/

/* create a message */
static struct msg *create_message(struct afb_wsapi *wsapi, char *buffer)
{
	struct msg *msg = calloc(1, sizeof(struct msg));
	if (msg) {
		msg->buffer = buffer;
		msg->refcount = 1;
		msg->wsapi = afb_wsapi_addref(wsapi);
	}
	return msg;
}

static void msg_unref(struct msg *msg)
{
	if (__atomic_sub_fetch(&msg->refcount, 1, __ATOMIC_RELAXED))
		return;

	afb_wsapi_unref(msg->wsapi);
	free(msg->buffer);
	free(msg);
}

/******************* management of pendings **********************************/

static struct pending *pending_alloc_locked(struct afb_wsapi *wsapi)
{
	struct pending *pending;

	if (wsapi->idcount >= ACTIVE_ID_MAX) {
		errno = EBUSY;
		pending = NULL;
	}
	else {
		pending = malloc(sizeof *pending);
		if (pending != NULL)
			wsapi->idcount++;
	}
	return pending;
}

static void pending_free_locked(struct afb_wsapi *wsapi, struct pending *pending)
{
	if (pending != NULL) {
		wsapi->idcount--;
		free(pending);
	}
}

static void pending_free_unlocked(struct afb_wsapi *wsapi, struct pending *pending)
{
	pthread_mutex_lock(&wsapi->mutex);
	pending_free_locked(wsapi, pending);
	pthread_mutex_unlock(&wsapi->mutex);
}

/* search a pending request */
static struct pending *pending_get_locked(struct afb_wsapi *wsapi, uint16_t requestid, int unlink)
{
	struct pending *pending, **pptr;

	pptr = &wsapi->pendings;
	while ((pending = *pptr) != NULL) {
		if (pending->requestid == requestid) {
			if (unlink)
				*pptr = pending->next;
			break;
		}
		pptr = &pending->next;
	}
	return pending;
}

/* create a pending request */
static struct pending *pending_make_locked(struct afb_wsapi *wsapi, enum afb_wsapi_msg_type type, void *closure)
{
	struct pending *result;
	uint16_t id;

	/* create the result */
	result = pending_alloc_locked(wsapi);
	if (result) {
		/* init the data */
		result->type = type;
		result->closure = closure;
		id = ++wsapi->genid;
		while(!id || pending_get_locked(wsapi, id, 0) != NULL)
			id++;
		result->requestid = id;

		/* link it */
		result->next = wsapi->pendings;
		wsapi->pendings = result;
	}
	return result;
}

static struct pending *pending_make_unlocked(struct afb_wsapi *wsapi, enum afb_wsapi_msg_type type, void *closure)
{
	struct pending *result;

	pthread_mutex_lock(&wsapi->mutex);
	result = pending_make_locked(wsapi, type, closure);
	pthread_mutex_unlock(&wsapi->mutex);
	return result;
}

/* get pending request from the message */
static int pending_read_closure(struct afb_wsapi *wsapi, struct readbuf *rb, void **closure, enum afb_wsapi_msg_type type, int remove)
{
	int result = 0;
	uint16_t requestid;
	struct pending *pending;

	/* get event data from the message */
	if (readbuf_uint16(rb, &requestid)) {
		/* get the pending */
		pthread_mutex_lock(&wsapi->mutex);
		pending = pending_get_locked(wsapi, requestid, remove);
		if (pending != NULL) {
			if (type == pending->type) {
				*closure = pending->closure;
				result = 1;
			}
			if (remove)
				pending_free_locked(wsapi, pending);
		}
		pthread_mutex_unlock(&wsapi->mutex);
	}
	return result;
}

/*****************************************************/

/* callback when receiving a hangup */
static void on_hangup(void *closure)
{
	struct afb_wsapi *wsapi = closure;
	struct afb_ws *ws;
	struct pending *pending, *next;
	struct msg *msg;
	void (*clientcb)(void*, const struct afb_wsapi_msg*);

	pthread_mutex_lock(&wsapi->mutex);
	ws = wsapi->ws;
	wsapi->ws = NULL;
	pending = wsapi->pendings;
	wsapi->pendings = NULL;
	pthread_mutex_unlock(&wsapi->mutex);

	while (pending) {
		msg = create_message(wsapi, NULL);
		if (msg) {
			switch (pending->type) {
			case afb_wsapi_msg_type_call:
				msg->msg.type = afb_wsapi_msg_type_reply;
				msg->msg.reply.closure = pending->closure;
				msg->msg.reply.data = NULL;
				msg->msg.reply.info = NULL;
				msg->msg.reply.error = "disconnected";
				clientcb = wsapi->itf->on_reply;
				break;
			case afb_wsapi_msg_type_describe:
				msg->msg.type = afb_wsapi_msg_type_description;
				msg->msg.description.closure = pending->closure;
				msg->msg.description.data = NULL;
				clientcb = wsapi->itf->on_description;
				break;
			default:
				clientcb = NULL;
				break;
			}
			if (clientcb)
				clientcb(wsapi, &msg->msg);
			else
				msg_unref(msg);
		}
		next = pending->next;
		pending_free_unlocked(wsapi, pending);
		pending = next;
	}

	if (ws) {
		afb_ws_destroy(ws);
		if (wsapi->itf->on_hangup)
			wsapi->itf->on_hangup(wsapi->closure);
	}
}

/******************* receiving **********************************/

/* on call, propagate it to the ws service */
static int read_on_call(struct readbuf *rb, struct msg *msg)
{
	msg->msg.type = afb_wsapi_msg_type_call;
	return readbuf_uint16(rb, &msg->requestid)
		&& readbuf_string(rb, &msg->msg.call.verb, NULL)
		&& readbuf_uint16(rb, &msg->msg.call.sessionid)
		&& readbuf_uint16(rb, &msg->msg.call.tokenid)
		&& readbuf_nullstring(rb, &msg->msg.call.data, NULL)
		&& readbuf_nullstring(rb, &msg->msg.call.user_creds, NULL);
}
static int read_on_reply(struct readbuf *rb, struct msg *msg)
{
	msg->msg.type = afb_wsapi_msg_type_reply;
	return pending_read_closure(msg->wsapi, rb, &msg->msg.reply.closure, afb_wsapi_msg_type_call, 1)
		&& readbuf_nullstring(rb, &msg->msg.reply.error, NULL)
		&& readbuf_nullstring(rb, &msg->msg.reply.info, NULL)
		&& readbuf_nullstring(rb, &msg->msg.reply.data, NULL);
}

/* adds an event */
static int read_on_event_create(struct readbuf *rb, struct msg *msg)
{
	msg->msg.type = afb_wsapi_msg_type_event_create;
	return readbuf_uint16(rb, &msg->msg.event_create.eventid)
		&& readbuf_string(rb, &msg->msg.event_create.eventname, NULL);
}

/* removes an event */
static int read_on_event_remove(struct readbuf *rb, struct msg *msg)
{
	msg->msg.type = afb_wsapi_msg_type_event_remove;
	return readbuf_uint16(rb, &msg->msg.event_remove.eventid);
}

/* subscribes an event */
static int read_on_event_subscribe(struct readbuf *rb, struct msg *msg)
{
	msg->msg.type = afb_wsapi_msg_type_event_subscribe;
	return pending_read_closure(msg->wsapi, rb, &msg->msg.event_subscribe.closure, afb_wsapi_msg_type_call, 0)
	 	&& readbuf_uint16(rb, &msg->msg.event_subscribe.eventid);
}

/* unsubscribes an event */
static int read_on_event_unsubscribe(struct readbuf *rb, struct msg *msg)
{
	msg->msg.type = afb_wsapi_msg_type_event_unsubscribe;
	return pending_read_closure(msg->wsapi, rb, &msg->msg.event_unsubscribe.closure, afb_wsapi_msg_type_call, 0)
	 	&& readbuf_uint16(rb, &msg->msg.event_unsubscribe.eventid);
}

/* pushs an event */
static int read_on_event_push(struct readbuf *rb, struct msg *msg)
{
	msg->msg.type = afb_wsapi_msg_type_event_push;
	return readbuf_uint16(rb, &msg->msg.event_push.eventid)
		&& readbuf_nullstring(rb, &msg->msg.event_push.data, NULL);
}

/* receives broadcasted events */
static int read_on_event_broadcast(struct readbuf *rb, struct msg *msg)
{
	msg->msg.type = afb_wsapi_msg_type_event_broadcast;
	return readbuf_string(rb, &msg->msg.event_broadcast.name, NULL)
		&& readbuf_nullstring(rb, &msg->msg.event_broadcast.data, NULL)
		&& NULL != (msg->msg.event_broadcast.uuid = (const uint8_t(*)[16])readbuf_get(rb, 16))
		&& readbuf_uint8(rb, &msg->msg.event_broadcast.hop);
}

static int read_on_event_unexpected(struct readbuf *rb, struct msg *msg)
{
	msg->msg.type = afb_wsapi_msg_type_event_unexpected;
	return readbuf_uint16(rb, &msg->msg.event_unexpected.eventid);
}

static int read_on_session_add(struct readbuf *rb, struct msg *msg)
{
	msg->msg.type = afb_wsapi_msg_type_session_create;
	return readbuf_uint16(rb, &msg->msg.session_create.sessionid)
		&& readbuf_string(rb, &msg->msg.session_create.sessionname, NULL);
}

static int read_on_session_drop(struct readbuf *rb, struct msg *msg)
{
	msg->msg.type = afb_wsapi_msg_type_session_remove;
	return readbuf_uint16(rb, &msg->msg.session_remove.sessionid);
}

static int read_on_token_add(struct readbuf *rb, struct msg *msg)
{
	msg->msg.type = afb_wsapi_msg_type_token_create;
	return readbuf_uint16(rb, &msg->msg.token_create.tokenid)
		&& readbuf_string(rb, &msg->msg.token_create.tokenname, NULL);
}

static int read_on_token_drop(struct readbuf *rb, struct msg *msg)
{
	msg->msg.type = afb_wsapi_msg_type_token_remove;
	return readbuf_uint16(rb, &msg->msg.token_remove.tokenid);
}

static int read_on_describe(struct readbuf *rb, struct msg *msg)
{
	msg->msg.type = afb_wsapi_msg_type_describe;
	return readbuf_uint16(rb, &msg->requestid);
}

static int read_on_description(struct readbuf *rb, struct msg *msg)
{
	msg->msg.type = afb_wsapi_msg_type_description;
	return pending_read_closure(msg->wsapi, rb, &msg->msg.description.closure, afb_wsapi_msg_type_describe, 1)
		&& readbuf_nullstring(rb, &msg->msg.description.data, NULL);
}

/* on version offer */
static int read_on_version_offer(struct afb_wsapi *wsapi, struct readbuf *rb)
{
	uint8_t count;
	uint8_t *versions;
	uint8_t version;
	uint8_t v;
	uint32_t id;

	/* reads the descid */
	if (readbuf_uint32(rb, &id)
		&& id == WSAPI_IDENTIFIER
		&& readbuf_uint8(rb, &count)
		&& count > 0
		&& (versions = (uint8_t*)readbuf_get(rb, (uint32_t)count))) {
		version = WSAPI_VERSION_UNSET;
		while (count) {
			v = versions[--count];
			if (v >= WSAPI_VERSION_MIN
			 && v <= WSAPI_VERSION_MAX
			 && (version == WSAPI_VERSION_UNSET || version < v))
				version = v;
		}
		if (version != WSAPI_VERSION_UNSET) {
			if (send_version_set(wsapi, version) >= 0) {
				wsapi->version = version;
				return 0;
			}
		}
	}
	return -1;
}

/* received a version set */
static int read_on_version_set(struct afb_wsapi *wsapi, struct readbuf *rb)
{
	uint8_t version;

	/* reads the descid */
	if (readbuf_uint8(rb, &version)
	 && WSAPI_VERSION_MIN <= version
	 && version <= WSAPI_VERSION_MAX) {
		wsapi->version = version;
		return 0;
	}
	return -1;
}

/* callback when receiving binary data */
static void on_binary(void *closure, char *data, size_t size)
{
	struct afb_wsapi *wsapi = closure;
	struct readbuf rb;
	struct msg *msg;
	char code;
	int rc;
	void (*clientcb)(void*,const struct afb_wsapi_msg*);

	/* scan the message */
	rb.base = data;
	rb.head = data;
	rb.end = data + size;
	code = *rb.head++;

	/* some "out-of-band" message */
	if (code == CHAR_FOR_VERSION_OFFER
		|| code == CHAR_FOR_VERSION_SET) {
		if (code == CHAR_FOR_VERSION_OFFER)
			/* a protocol offer for versions */
			rc = read_on_version_offer(wsapi, &rb);
		else
			/* set the protocol version */
			rc = read_on_version_set(wsapi, &rb);
		if (rc < 0)
			afb_wsapi_hangup(wsapi);
		return;
	}

	/* make a message */
	msg = create_message(wsapi, data);
	if (!msg) {
		free(data);
		afb_wsapi_hangup(wsapi);
		return;
	}

	/* read the incoming message */
	clientcb = NULL;
	msg->msg.type = afb_wsapi_msg_type_NONE;
	switch (code) {
	case CHAR_FOR_CALL: /* call */
		rc = read_on_call(&rb, msg);
		break;
	case CHAR_FOR_REPLY: /* reply */
		rc = read_on_reply(&rb, msg);
		break;
	case CHAR_FOR_EVT_ADD: /* creates the event */
		rc = read_on_event_create(&rb, msg);
		break;
	case CHAR_FOR_EVT_DEL: /* removes the event */
		rc = read_on_event_remove(&rb, msg);
		break;
	case CHAR_FOR_EVT_SUBSCRIBE: /* subscribe event for a request */
		rc = read_on_event_subscribe(&rb, msg);
		break;
	case CHAR_FOR_EVT_UNSUBSCRIBE: /* unsubscribe event for a request */
		rc = read_on_event_unsubscribe(&rb, msg);
		break;
	case CHAR_FOR_EVT_PUSH: /* pushs the event */
		rc = read_on_event_push(&rb, msg);
		break;
	case CHAR_FOR_EVT_BROADCAST: /* broadcast */
		rc = read_on_event_broadcast(&rb, msg);
		break;
	case CHAR_FOR_EVT_UNEXPECTED: /* unexpected event */
		rc = read_on_event_unexpected(&rb, msg);
		break;
	case CHAR_FOR_SESSION_ADD: /* create a session */
		rc = read_on_session_add(&rb, msg);
		break;
	case CHAR_FOR_SESSION_DROP: /* remove a session */
		rc = read_on_session_drop(&rb, msg);
		break;
	case CHAR_FOR_TOKEN_ADD: /* create a token */
		rc = read_on_token_add(&rb, msg);
		break;
	case CHAR_FOR_TOKEN_DROP: /* remove a token */
		rc = read_on_token_drop(&rb, msg);
		break;
	case CHAR_FOR_DESCRIBE: /* require description */
		rc = read_on_describe(&rb, msg);
		break;
	case CHAR_FOR_DESCRIPTION: /* description */
		rc = read_on_description(&rb, msg);
		break;
	default: /* unexpected message */
		rc = 0;
		break;
	}

	/* is the message expected */
	if (msg->msg.type != afb_wsapi_msg_type_NONE) {
		clientcb = (&wsapi->itf->on_call)[msg->msg.type - afb_wsapi_msg_type_call];
		if (clientcb) {
			if (rc > 0)
				clientcb(wsapi->closure, &msg->msg);
			else {
				ERROR("ignoring message of type %d", (int)afb_wsapi_msg_type_call);
				msg_unref(msg);
				/* TODO: close the connection? */
			}
			return;
		}
	}
	msg_unref(msg);
	/* TODO: close the connection? */
}

/************************************************/

static int send_cmd_id16(struct afb_wsapi *wsapi, char order, uint16_t id)
{
	struct writebuf wb = { .iovcount = 0, .bufcount = 0 };
	int rc = -1;

	if (writebuf_char(&wb, order)
	 && writebuf_uint16(&wb, id))
		rc = proto_write(wsapi, &wb);
	return rc;
}

static int send_cmd_id16_id16(struct afb_wsapi *wsapi, char order, uint16_t id1, uint16_t id2)
{
	struct writebuf wb = { .iovcount = 0, .bufcount = 0 };
	int rc = -1;

	if (writebuf_char(&wb, order)
	 && writebuf_uint16(&wb, id1)
	 && writebuf_uint16(&wb, id2))
		rc = proto_write(wsapi, &wb);
	return rc;
}

static int send_cmd_id16_str(struct afb_wsapi *wsapi, char order, uint16_t id, const char *value)
{
	struct writebuf wb = { .iovcount = 0, .bufcount = 0 };
	int rc = -1;

	if (writebuf_char(&wb, order)
	 && writebuf_uint16(&wb, id)
	 && writebuf_string(&wb, value))
		rc = proto_write(wsapi, &wb);
	return rc;
}

/************************************************/

int afb_wsapi_call_s(
		struct afb_wsapi *wsapi,
		const char *verb,
		const char *data,
		uint16_t sessionid,
		uint16_t tokenid,
		void *closure,
		const char *user_creds
)
{
	struct pending *pending;
	struct writebuf wb = { .iovcount = 0, .bufcount = 0 };
	int rc = -1;

	pending = pending_make_unlocked(wsapi, afb_wsapi_msg_type_call, closure);
	if (pending != NULL) {
		/* creates and send the call message */
		if (writebuf_char(&wb, CHAR_FOR_CALL)
		&& writebuf_uint16(&wb, pending->requestid)
		&& writebuf_string(&wb, verb)
		&& writebuf_uint16(&wb, sessionid)
		&& writebuf_uint16(&wb, tokenid)
		&& writebuf_nullstring(&wb, data)
		&& writebuf_nullstring(&wb, user_creds))
			rc = proto_write(wsapi, &wb);

		if (rc < 0)
			pending_free_unlocked(wsapi, pending);
	}
	return rc;
}

int afb_wsapi_call_j(
		struct afb_wsapi *wsapi,
		const char *verb,
		struct json_object *data,
		uint16_t sessionid,
		uint16_t tokenid,
		void *closure,
		const char *user_creds
)
{
	return afb_wsapi_call_s(
		wsapi,
		verb,
		json_object_to_json_string_ext(data, JSON_C_TO_STRING_PLAIN),
		sessionid,
		tokenid,
		closure,
		user_creds);
}

int afb_wsapi_event_create(struct afb_wsapi *wsapi, uint16_t eventid, const char *eventname)
{
	return send_cmd_id16_str(wsapi, CHAR_FOR_EVT_ADD, eventid, eventname);
}

int afb_wsapi_event_remove(struct afb_wsapi *wsapi, uint16_t eventid)
{
	return send_cmd_id16(wsapi, CHAR_FOR_EVT_DEL, eventid);
}

int afb_wsapi_event_push_s(struct afb_wsapi *wsapi, uint16_t eventid, const char *data)
{
	return send_cmd_id16_str(wsapi, CHAR_FOR_EVT_PUSH, eventid, data);
}

int afb_wsapi_event_push_j(struct afb_wsapi *wsapi, uint16_t eventid, struct json_object *data)
{
	return afb_wsapi_event_push_s(wsapi, eventid,
			json_object_to_json_string_ext(data, JSON_C_TO_STRING_PLAIN));
}

int afb_wsapi_event_broadcast_s(struct afb_wsapi *wsapi, const char *eventname, const char *data, const unsigned char uuid[16], uint8_t hop)
{
	struct writebuf wb = { .iovcount = 0, .bufcount = 0 };
	int rc = -1;

	if (!hop)
		return 0;

	if (writebuf_char(&wb, CHAR_FOR_EVT_BROADCAST)
	 && writebuf_string(&wb, eventname)
	 && writebuf_nullstring(&wb, data)
	 && writebuf_put(&wb, uuid, 16)
	 && writebuf_uint8(&wb, (uint8_t)(hop - 1)))
		rc = proto_write(wsapi, &wb);
	return rc;
}

int afb_wsapi_event_broadcast_j(struct afb_wsapi *wsapi, const char *eventname, struct json_object *data, const unsigned char uuid[16], uint8_t hop)
{
	return afb_wsapi_event_broadcast_s(wsapi, eventname,
			json_object_to_json_string_ext(data, JSON_C_TO_STRING_PLAIN), uuid, hop);
}

int afb_wsapi_event_unexpected(struct afb_wsapi *wsapi, uint16_t eventid)
{
	return send_cmd_id16(wsapi, CHAR_FOR_EVT_UNEXPECTED, eventid);
}

int afb_wsapi_session_create(struct afb_wsapi *wsapi, uint16_t sessionid, const char *sessionstr)
{
	return send_cmd_id16_str(wsapi, CHAR_FOR_SESSION_ADD, sessionid, sessionstr);
}

int afb_wsapi_session_remove(struct afb_wsapi *wsapi, uint16_t sessionid)
{
	return send_cmd_id16(wsapi, CHAR_FOR_SESSION_DROP, sessionid);
}

int afb_wsapi_token_create(struct afb_wsapi *wsapi, uint16_t tokenid, const char *tokenstr)
{
	return send_cmd_id16_str(wsapi, CHAR_FOR_TOKEN_ADD, tokenid, tokenstr);

}

int afb_wsapi_token_remove(struct afb_wsapi *wsapi, uint16_t tokenid)
{
	return send_cmd_id16(wsapi, CHAR_FOR_TOKEN_DROP, tokenid);
}

/* get the description */
int afb_wsapi_describe(struct afb_wsapi *wsapi, void *closure)
{
	struct pending *pending;
	struct writebuf wb = { .iovcount = 0, .bufcount = 0 };
	int rc = -1;

	pending = pending_make_unlocked(wsapi, afb_wsapi_msg_type_describe, closure);
	if (pending != NULL) {

		if (writebuf_char(&wb, CHAR_FOR_DESCRIBE)
		 && writebuf_uint16(&wb, pending->requestid))
			rc = proto_write(wsapi, &wb);

		if (rc < 0)
			pending_free_unlocked(wsapi, pending);
	}
	return rc;
}

/******************* handling messages *****************/

const struct afb_wsapi_msg *afb_wsapi_msg_addref(const struct afb_wsapi_msg *msg)
{
	struct msg *m = (struct msg *)msg;
	__atomic_add_fetch(&m->refcount, 1, __ATOMIC_RELAXED);
	return msg;
}

void afb_wsapi_msg_unref(const struct afb_wsapi_msg *msg)
{
	msg_unref((struct msg*)msg);
}

struct json_object *afb_wsapi_msg_json_data(const struct afb_wsapi_msg *msg)
{
	struct msg *m = (struct msg *)msg;
	const char *data;
	struct json_object *result;

	result = m->json;
	if (result == NULL) {
		switch (m->msg.type) {
		case afb_wsapi_msg_type_call:
			data = m->msg.call.data;
			break;
		case afb_wsapi_msg_type_reply:
			data = m->msg.call.data;
			break;
		case afb_wsapi_msg_type_description:
			data = m->msg.call.data;
			break;
		case afb_wsapi_msg_type_event_push:
			data = m->msg.call.data;
			break;
		case afb_wsapi_msg_type_event_broadcast:
			data = m->msg.call.data;
			break;
		default:
			data = NULL;
			break;
		}
		if (data)
			result = m->json = json_tokener_parse(data);
	}
	return result;
}

int afb_wsapi_msg_reply_s(const struct afb_wsapi_msg *msg, const char *data, const char *error, const char *info)
{
	struct writebuf wb = { .iovcount = 0, .bufcount = 0 };
	struct msg *m = (struct msg *)msg;
	int rc = -1;

	if (m->msg.type == afb_wsapi_msg_type_call
	 && writebuf_char(&wb, CHAR_FOR_REPLY)
	 && writebuf_uint16(&wb, m->requestid)
	 && writebuf_nullstring(&wb, error)
	 && writebuf_nullstring(&wb, info)
	 && writebuf_nullstring(&wb, data))
		rc = proto_write(m->wsapi, &wb);

	msg_unref(m);
	return rc;
}

int afb_wsapi_msg_reply_j(const struct afb_wsapi_msg *msg, struct json_object *data, const char *error, const char *info)
{
	return afb_wsapi_msg_reply_s(msg,
			json_object_to_json_string_ext(data, JSON_C_TO_STRING_PLAIN),
			error, info);
}

int afb_wsapi_msg_subscribe(const struct afb_wsapi_msg *msg, uint16_t eventid)
{
	struct msg *m = (struct msg *)msg;
	return send_cmd_id16_id16(m->wsapi, CHAR_FOR_EVT_SUBSCRIBE, m->requestid, eventid);
}

int afb_wsapi_msg_unsubscribe(const struct afb_wsapi_msg *msg, uint16_t eventid)
{
	struct msg *m = (struct msg *)msg;
	return send_cmd_id16_id16(m->wsapi, CHAR_FOR_EVT_UNSUBSCRIBE, m->requestid, eventid);
}

int afb_wsapi_msg_description_s(const struct afb_wsapi_msg *msg, const char *data)
{
	struct msg *m = (struct msg *)msg;
	int rc = send_cmd_id16_str(m->wsapi, CHAR_FOR_EVT_UNSUBSCRIBE, m->requestid, data);
	msg_unref(m);
	return rc;
}

int afb_wsapi_msg_description_j(const struct afb_wsapi_msg *msg, struct json_object *data)
{
	return afb_wsapi_msg_description_s(msg,
			json_object_to_json_string_ext(data, JSON_C_TO_STRING_PLAIN));
}

/*****************************************************/

static const struct afb_ws_itf ws_itf =
{
	.on_close = NULL,
	.on_text = NULL,
	.on_binary = on_binary,
	.on_error = NULL,
	.on_hangup = on_hangup
};

/*****************************************************/

int afb_wsapi_create(struct afb_wsapi **wsapi, struct fdev *fdev, const struct afb_wsapi_itf *itf, void *closure)
{
	struct afb_wsapi *wsa;

	wsa = calloc(1, sizeof *wsa);
	if (wsa == NULL)
		errno = ENOMEM;
	else {
		wsa->refcount = 1;
		wsa->version = WSAPI_VERSION_UNSET;
		wsa->closure = closure;
		wsa->itf = itf;
		pthread_mutex_init(&wsa->mutex, NULL);

		fcntl(fdev_fd(fdev), F_SETFD, FD_CLOEXEC);
		fcntl(fdev_fd(fdev), F_SETFL, O_NONBLOCK);
		wsa->ws = afb_ws_create(fdev, &ws_itf, wsa);
		if (wsa->ws != NULL) {
			*wsapi = wsa;
			return 0;
		}
		pthread_mutex_destroy(&wsa->mutex);
		free(wsa);
	}
	*wsapi = NULL;
	return -1;
}

int afb_wsapi_initiate(struct afb_wsapi *wsapi)
{
	return wsapi->version != WSAPI_VERSION_UNSET ? 0
		: send_version_offer_1(wsapi, WSAPI_VERSION_1);
}

struct afb_wsapi *afb_wsapi_create_client(struct fdev *fdev, const struct afb_wsapi_itf *itf, void *closure, int init)
{
	struct afb_wsapi *wsapi;

	wsapi = calloc(1, sizeof *wsapi);
	if (wsapi == NULL)
		errno = ENOMEM;
	else {
		wsapi->refcount = 1;
		wsapi->version = WSAPI_VERSION_UNSET;
		wsapi->closure = closure;
		wsapi->itf = itf;
		pthread_mutex_init(&wsapi->mutex, NULL);

		fcntl(fdev_fd(fdev), F_SETFD, FD_CLOEXEC);
		fcntl(fdev_fd(fdev), F_SETFL, O_NONBLOCK);
		wsapi->ws = afb_ws_create(fdev, &ws_itf, wsapi);
		if (wsapi->ws != NULL) {
			if (init && send_version_offer_1(wsapi, WSAPI_VERSION_1) != 0) {
				afb_wsapi_unref(wsapi);
				wsapi = NULL;
			}
			return wsapi;
		}
		pthread_mutex_destroy(&wsapi->mutex);
		free(wsapi);
	}
	return NULL;
}

void afb_wsapi_unref(struct afb_wsapi *wsapi)
{
	if (wsapi && !__atomic_sub_fetch(&wsapi->refcount, 1, __ATOMIC_RELAXED)) {
		afb_wsapi_hangup(wsapi);
		pthread_mutex_destroy(&wsapi->mutex);
		free(wsapi);
	}
}

struct afb_wsapi *afb_wsapi_addref(struct afb_wsapi *wsapi)
{
	__atomic_add_fetch(&wsapi->refcount, 1, __ATOMIC_RELAXED);
	return wsapi;
}

void afb_wsapi_hangup(struct afb_wsapi *wsapi)
{
	if (wsapi->ws)
		afb_ws_hangup(wsapi->ws);
}
