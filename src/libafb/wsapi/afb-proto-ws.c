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


#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>

#include <json-c/json.h>
#include <rp-utils/rp-verbose.h>

#include "misc/afb-ws.h"
#include "wsapi/afb-proto-ws.h"
#include "sys/x-endian.h"
#include "sys/x-uio.h"
#include "sys/x-mutex.h"
#include "sys/x-errno.h"

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

/******************* handling calls *****************************/

/*
 * structure for recording calls on client side
 */
struct client_call {
	struct client_call *next;	/* the next call */
	void *request;			/* the request closure */
	uint16_t callid;		/* the message identifier */
};

/*
 * structure for a ws request
 */
struct afb_proto_ws_call {
	struct afb_proto_ws *protows;	/* the client of the request */
	char *buffer;			/* the incoming buffer */
	uint16_t refcount;		/* reference count */
	uint16_t callid;		/* the incoming request callid */
};

/*
 * structure for recording describe requests
 */
struct client_describe
{
	struct client_describe *next;
	void (*callback)(void*, struct json_object*);
	void *closure;
	uint16_t descid;
};

/*
 * structure for jobs of describing
 */
struct afb_proto_ws_describe
{
	struct afb_proto_ws *protows;
	uint16_t descid;
};

/******************* proto description for client or servers ******************/

struct afb_proto_ws
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
	x_mutex_t mutex;

	/* websocket */
	struct afb_ws *ws;

	/* the client closure */
	void *closure;

	/* the client side interface */
	const struct afb_proto_ws_client_itf *client_itf;

	/* the server side interface */
	const struct afb_proto_ws_server_itf *server_itf;

	/* emitted calls (client side) */
	struct client_call *calls;

	/* pending description (client side) */
	struct client_describe *describes;

	/* on hangup callback */
	void (*on_hangup)(void *closure);

	/* queuing facility for processing messages */
	int (*queuing)(struct afb_proto_ws *proto, void (*process)(int s, void *c), void *closure);
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

struct binary
{
	struct afb_proto_ws *protows;
	struct readbuf rb;
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

static int readbuf_object(struct readbuf *rb, struct json_object **object)
{
	const char *string;
	struct json_object *o;
	enum json_tokener_error jerr;
	int rc = readbuf_string(rb, &string, NULL);
	if (rc) {
		o = json_tokener_parse_verbose(string, &jerr);
		if (jerr != json_tokener_success)
			o = json_object_new_string(string);
		*object = o;
	}
	return rc;
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
	if (nafter > (int)WRITEBUF_BUFSZ)
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

static int writebuf_object(struct writebuf *wb, struct json_object *object)
{
	const char *string = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
	return string != NULL && writebuf_string(wb, string);
}

/******************* queuing of messages *****************/

/* queue the processing of the received message (except if size=0 cause it's not a valid message) */
static int queue_message_processing(struct afb_proto_ws *protows, char *data, size_t size, void (*processing)(int,void*))
{
	struct binary *binary;

	if (!size) {
		free(data);
		return 0;
	}

	binary = malloc(sizeof *binary);
	if (!binary) {
		/* TODO process the problem */
		free(data);
		return X_ENOMEM;
	}

	binary->protows = protows;
	binary->rb.base = data;
	binary->rb.head = data;
	binary->rb.end = data + size;
	if (!protows->queuing
	 || protows->queuing(protows, processing, binary) < 0)
		processing(0, binary);
	return 0;
}

/******************* sending messages *****************/

static int proto_write(struct afb_proto_ws *protows, struct writebuf *wb)
{
	int rc;
	struct afb_ws *ws;

	x_mutex_lock(&protows->mutex);
	ws = protows->ws;
	if (ws == NULL) {
		rc = X_EPIPE;
	} else {
		rc = afb_ws_binary_v(ws, wb->iovec, wb->iovcount);
		if (rc > 0)
			rc = 0;
	}
	x_mutex_unlock(&protows->mutex);
	return rc;
}

static int send_version_offer_1(struct afb_proto_ws *protows, uint8_t version)
{
	int rc = -1;
	struct writebuf wb = { .iovcount = 0, .bufcount = 0 };

	if (writebuf_char(&wb, CHAR_FOR_VERSION_OFFER)
	 && writebuf_uint32(&wb, WSAPI_IDENTIFIER)
	 && writebuf_uint8(&wb, 1) /* offer one version */
	 && writebuf_uint8(&wb, version))
		rc = proto_write(protows, &wb);
	return rc;
}

static int send_version_set(struct afb_proto_ws *protows, uint8_t version)
{
	int rc = -1;
	struct writebuf wb = { .iovcount = 0, .bufcount = 0 };

	if (writebuf_char(&wb, CHAR_FOR_VERSION_SET)
	 && writebuf_uint8(&wb, version))
		rc = proto_write(protows, &wb);
	return rc;
}

/******************* ws request part for server *****************/

void afb_proto_ws_call_addref(struct afb_proto_ws_call *call)
{
	__atomic_add_fetch(&call->refcount, 1, __ATOMIC_RELAXED);
}

void afb_proto_ws_call_unref(struct afb_proto_ws_call *call)
{
	if (__atomic_sub_fetch(&call->refcount, 1, __ATOMIC_RELAXED))
		return;

	afb_proto_ws_unref(call->protows);
	free(call->buffer);
	free(call);
}

int afb_proto_ws_call_reply(struct afb_proto_ws_call *call, struct json_object *obj, const char *error, const char *info)
{
	int rc = -1;
	struct writebuf wb = { .iovcount = 0, .bufcount = 0 };
	struct afb_proto_ws *protows = call->protows;

	if (writebuf_char(&wb, CHAR_FOR_REPLY)
	 && writebuf_uint16(&wb, call->callid)
	 && writebuf_nullstring(&wb, error)
	 && writebuf_nullstring(&wb, info)
	 && writebuf_object(&wb, obj))
		rc = proto_write(protows, &wb);
	return rc;
}

int afb_proto_ws_call_subscribe(struct afb_proto_ws_call *call, uint16_t event_id)
{
	int rc = -1;
	struct writebuf wb = { .iovcount = 0, .bufcount = 0 };
	struct afb_proto_ws *protows = call->protows;

	if (writebuf_char(&wb, CHAR_FOR_EVT_SUBSCRIBE)
	 && writebuf_uint16(&wb, call->callid)
	 && writebuf_uint16(&wb, event_id))
		rc = proto_write(protows, &wb);
	return rc;
}

int afb_proto_ws_call_unsubscribe(struct afb_proto_ws_call *call, uint16_t event_id)
{
	int rc = -1;
	struct writebuf wb = { .iovcount = 0, .bufcount = 0 };
	struct afb_proto_ws *protows = call->protows;

	if (writebuf_char(&wb, CHAR_FOR_EVT_UNSUBSCRIBE)
	 && writebuf_uint16(&wb, call->callid)
	 && writebuf_uint16(&wb, event_id))
		rc = proto_write(protows, &wb);
	return rc;
}

/******************* client part **********************************/

/* search a memorized call */
static struct client_call *client_call_search_locked(struct afb_proto_ws *protows, uint16_t callid)
{
	struct client_call *call;

	call = protows->calls;
	while (call != NULL && call->callid != callid)
		call = call->next;

	return call;
}

static struct client_call *client_call_search_unlocked(struct afb_proto_ws *protows, uint16_t callid)
{
	struct client_call *result;

	x_mutex_lock(&protows->mutex);
	result = client_call_search_locked(protows, callid);
	x_mutex_unlock(&protows->mutex);
	return result;
}

/* free and release the memorizing call */
static void client_call_destroy(struct afb_proto_ws *protows, struct client_call *call)
{
	struct client_call **prv;

	x_mutex_lock(&protows->mutex);
	prv = &protows->calls;
	while (*prv != NULL) {
		if (*prv == call) {
			protows->idcount--;
			*prv = call->next;
			pthread_mutex_unlock(&protows->mutex);
			free(call);
			return;
		}
		prv = &(*prv)->next;
	}
	x_mutex_unlock(&protows->mutex);
	free(call);
}

/* get event from the message */
static int client_msg_call_get(struct afb_proto_ws *protows, struct readbuf *rb, struct client_call **call)
{
	uint16_t callid;

	/* get event data from the message */
	if (!readbuf_uint16(rb, &callid))
		return 0;

	/* get the call */
	*call = client_call_search_unlocked(protows, callid);
	return *call != NULL;
}

/* adds an event */
static void client_on_event_create(struct afb_proto_ws *protows, struct readbuf *rb)
{
	const char *event_name;
	uint16_t event_id;

	if (protows->client_itf->on_event_create
			&& readbuf_uint16(rb, &event_id)
			&& readbuf_string(rb, &event_name, NULL))
		protows->client_itf->on_event_create(protows->closure, event_id, event_name);
	else
		RP_ERROR("Ignoring creation of event");
}

/* removes an event */
static void client_on_event_remove(struct afb_proto_ws *protows, struct readbuf *rb)
{
	uint16_t event_id;

	if (protows->client_itf->on_event_remove && readbuf_uint16(rb, &event_id))
		protows->client_itf->on_event_remove(protows->closure, event_id);
	else
		RP_ERROR("Ignoring deletion of event");
}

/* subscribes an event */
static void client_on_event_subscribe(struct afb_proto_ws *protows, struct readbuf *rb)
{
	uint16_t event_id;
	struct client_call *call;

	if (protows->client_itf->on_event_subscribe && client_msg_call_get(protows, rb, &call) && readbuf_uint16(rb, &event_id))
		protows->client_itf->on_event_subscribe(protows->closure, call->request, event_id);
	else
		RP_ERROR("Ignoring subscription to event");
}

/* unsubscribes an event */
static void client_on_event_unsubscribe(struct afb_proto_ws *protows, struct readbuf *rb)
{
	uint16_t event_id;
	struct client_call *call;

	if (protows->client_itf->on_event_unsubscribe && client_msg_call_get(protows, rb, &call) && readbuf_uint16(rb, &event_id))
		protows->client_itf->on_event_unsubscribe(protows->closure, call->request, event_id);
	else
		RP_ERROR("Ignoring unsubscription to event");
}

/* receives broadcasted events */
static void client_on_event_broadcast(struct afb_proto_ws *protows, struct readbuf *rb)
{
	const char *event_name, *uuid;
	uint8_t hop;
	struct json_object *object;

	if (protows->client_itf->on_event_broadcast && readbuf_string(rb, &event_name, NULL) && readbuf_object(rb, &object)) {
		if ((uuid = readbuf_get(rb, 16)) && readbuf_uint8(rb, &hop))
			protows->client_itf->on_event_broadcast(protows->closure, event_name, object, (unsigned char*)uuid, hop);
		else
			json_object_put(object);
	}
	else
		RP_ERROR("Ignoring broadcast of event");
}

/* pushs an event */
static void client_on_event_push(struct afb_proto_ws *protows, struct readbuf *rb)
{
	uint16_t event_id;
	struct json_object *object;

	if (protows->client_itf->on_event_push && readbuf_uint16(rb, &event_id) && readbuf_object(rb, &object))
		protows->client_itf->on_event_push(protows->closure, event_id, object);
	else
		RP_ERROR("Ignoring push of event");
}

static void client_on_reply(struct afb_proto_ws *protows, struct readbuf *rb)
{
	struct client_call *call;
	struct json_object *object;
	const char *error, *info;

	if (!client_msg_call_get(protows, rb, &call))
		return;

	if (readbuf_nullstring(rb, &error, NULL) && readbuf_nullstring(rb, &info, NULL) && readbuf_object(rb, &object)) {
		protows->client_itf->on_reply(protows->closure, call->request, object, error, info);
	} else {
		protows->client_itf->on_reply(protows->closure, call->request, NULL, "proto-error", "can't process success");
	}
	client_call_destroy(protows, call);
}

static void client_on_description(struct afb_proto_ws *protows, struct readbuf *rb)
{
	uint16_t descid;
	struct client_describe *desc, **prv;
	struct json_object *object;

	if (readbuf_uint16(rb, &descid)) {
		x_mutex_lock(&protows->mutex);
		prv = &protows->describes;
		while ((desc = *prv) && desc->descid != descid)
			prv = &desc->next;
		if (!desc)
			x_mutex_unlock(&protows->mutex);
		else {
			*prv = desc->next;
			protows->idcount--;
			x_mutex_unlock(&protows->mutex);
			if (!readbuf_object(rb, &object))
				object = NULL;
			desc->callback(desc->closure, object);
			free(desc);
		}
	}
}

/* received a version set */
static void client_on_version_set(struct afb_proto_ws *protows, struct readbuf *rb)
{
	uint8_t version;

	/* reads the descid */
	if (readbuf_uint8(rb, &version)
	 && WSAPI_VERSION_MIN <= version
	 && version <= WSAPI_VERSION_MAX) {
		protows->version = version;
		return;
	}
	afb_proto_ws_hangup(protows);
}


/* callback when receiving binary data */
static void client_on_binary_job(int sig, void *closure)
{
	struct binary *binary = closure;

	if (!sig) {
		switch (*binary->rb.head++) {
		case CHAR_FOR_REPLY: /* reply */
			client_on_reply(binary->protows, &binary->rb);
			break;
		case CHAR_FOR_EVT_BROADCAST: /* broadcast */
			client_on_event_broadcast(binary->protows, &binary->rb);
			break;
		case CHAR_FOR_EVT_ADD: /* creates the event */
			client_on_event_create(binary->protows, &binary->rb);
			break;
		case CHAR_FOR_EVT_DEL: /* removes the event */
			client_on_event_remove(binary->protows, &binary->rb);
			break;
		case CHAR_FOR_EVT_PUSH: /* pushs the event */
			client_on_event_push(binary->protows, &binary->rb);
			break;
		case CHAR_FOR_EVT_SUBSCRIBE: /* subscribe event for a request */
			client_on_event_subscribe(binary->protows, &binary->rb);
			break;
		case CHAR_FOR_EVT_UNSUBSCRIBE: /* unsubscribe event for a request */
			client_on_event_unsubscribe(binary->protows, &binary->rb);
			break;
		case CHAR_FOR_DESCRIPTION: /* description */
			client_on_description(binary->protows, &binary->rb);
			break;
		case CHAR_FOR_VERSION_SET: /* set the protocol version */
			client_on_version_set(binary->protows, &binary->rb);
			break;
		default: /* unexpected message */
			/* TODO: close the connection */
			break;
		}
	}
	free(binary->rb.base);
	free(binary);
}

/* callback when receiving binary data */
static void client_on_binary(void *closure, char *data, size_t size)
{
	struct afb_proto_ws *protows = closure;

	queue_message_processing(protows, data, size, client_on_binary_job);
}

static int client_send_cmd_id16_optstr(struct afb_proto_ws *protows, char order, uint16_t id, const char *value)
{
	struct writebuf wb = { .iovcount = 0, .bufcount = 0 };
	int rc = -1;

	if (writebuf_char(&wb, order)
	 && writebuf_uint16(&wb, id)
	 && (!value || writebuf_string(&wb, value)))
		rc = proto_write(protows, &wb);
	return rc;
}

int afb_proto_ws_client_session_create(struct afb_proto_ws *protows, uint16_t sessionid, const char *sessionstr)
{
	return client_send_cmd_id16_optstr(protows, CHAR_FOR_SESSION_ADD, sessionid, sessionstr);
}

int afb_proto_ws_client_session_remove(struct afb_proto_ws *protows, uint16_t sessionid)
{
	return client_send_cmd_id16_optstr(protows, CHAR_FOR_SESSION_DROP, sessionid, NULL);
}

int afb_proto_ws_client_token_create(struct afb_proto_ws *protows, uint16_t tokenid, const char *tokenstr)
{
	return client_send_cmd_id16_optstr(protows, CHAR_FOR_TOKEN_ADD, tokenid, tokenstr);

}

int afb_proto_ws_client_token_remove(struct afb_proto_ws *protows, uint16_t tokenid)
{
	return client_send_cmd_id16_optstr(protows, CHAR_FOR_TOKEN_DROP, tokenid, NULL);
}

int afb_proto_ws_client_event_unexpected(struct afb_proto_ws *protows, uint16_t eventid)
{
	return client_send_cmd_id16_optstr(protows, CHAR_FOR_EVT_UNEXPECTED, eventid, NULL);
}

int afb_proto_ws_client_call(
		struct afb_proto_ws *protows,
		const char *verb,
		struct json_object *args,
		uint16_t sessionid,
		uint16_t tokenid,
		void *request,
		const char *user_creds
)
{
	int rc = -1;
	struct client_call *call;
	struct writebuf wb = { .iovcount = 0, .bufcount = 0 };
	uint16_t id;

	/* allocate call data */
	call = malloc(sizeof *call);
	if (call == NULL)
		return X_ENOMEM;
	call->request = request;

	/* init call data */
	x_mutex_lock(&protows->mutex);
	if (protows->idcount >= ACTIVE_ID_MAX) {
		pthread_mutex_unlock(&protows->mutex);
		rc = X_EBUSY;
		goto clean;
	}
	protows->idcount++;
	id = ++protows->genid;
	while(!id || client_call_search_locked(protows, id) != NULL)
		id++;
	call->callid = protows->genid = id;
	call->next = protows->calls;
	protows->calls = call;
	x_mutex_unlock(&protows->mutex);

	/* creates the call message */
	if (!writebuf_char(&wb, CHAR_FOR_CALL)
	 || !writebuf_uint16(&wb, call->callid)
	 || !writebuf_string(&wb, verb)
	 || !writebuf_uint16(&wb, sessionid)
	 || !writebuf_uint16(&wb, tokenid)
	 || !writebuf_object(&wb, args)
	 || !writebuf_nullstring(&wb, user_creds)) {
		rc = X_EINVAL;
		goto clean;
	}

	/* send */
	rc = proto_write(protows, &wb);
	if (!rc)
		goto end;

clean:
	client_call_destroy(protows, call);
end:
	return rc;
}

/* get the description */
int afb_proto_ws_client_describe(struct afb_proto_ws *protows, void (*callback)(void*, struct json_object*), void *closure)
{
	struct client_describe *desc, *d;
	struct writebuf wb = { .iovcount = 0, .bufcount = 0 };
	uint16_t id;
	int rc;

	desc = malloc(sizeof *desc);
	if (!desc)
		return X_ENOMEM;

	/* fill in stack the description of the task */
	x_mutex_lock(&protows->mutex);
	if (protows->idcount >= ACTIVE_ID_MAX) {
		rc = X_EBUSY;
		goto busy;
	}
	protows->idcount++;
	id = ++protows->genid;
	d = protows->describes;
	while (d) {
		if (id && d->descid != id)
			d = d->next;
		else {
			id++;
			d = protows->describes;
		}
	}
	desc->descid = protows->genid = id;
	desc->callback = callback;
	desc->closure = closure;
	desc->next = protows->describes;
	protows->describes = desc;
	x_mutex_unlock(&protows->mutex);

	/* send */
	rc = X_EINVAL;
	if (writebuf_char(&wb, CHAR_FOR_DESCRIBE)
	 && writebuf_uint16(&wb, desc->descid)) {
		rc = proto_write(protows, &wb);
		if (rc >= 0)
			return 0;
	}

	x_mutex_lock(&protows->mutex);
	d = protows->describes;
	if (d == desc)
		protows->describes = desc->next;
	else {
		while(d && d->next != desc)
			d = d->next;
		if (d)
			d->next = desc->next;
	}
	protows->idcount--;
busy:
	x_mutex_unlock(&protows->mutex);
	free(desc);
	/* TODO? callback(closure, NULL); */
	return rc;
}

/******************* client description part for server *****************************/

/* on call, propagate it to the ws service */
static void server_on_call(struct afb_proto_ws *protows, struct readbuf *rb)
{
	struct afb_proto_ws_call *call;
	const char *verb, *user_creds;
	uint16_t callid, sessionid, tokenid;
	size_t lenverb;
	struct json_object *object;

	afb_proto_ws_addref(protows);

	/* reads the call message data */
	if (!readbuf_uint16(rb, &callid)
	 || !readbuf_string(rb, &verb, &lenverb)
	 || !readbuf_uint16(rb, &sessionid)
	 || !readbuf_uint16(rb, &tokenid)
	 || !readbuf_object(rb, &object)
	 || !readbuf_nullstring(rb, &user_creds, NULL))
		goto overflow;

	/* create the request */
	call = malloc(sizeof *call);
	if (call == NULL)
		goto out_of_memory;

	call->protows = protows;
	call->callid = callid;
	call->refcount = 1;
	call->buffer = rb->base;
	rb->base = NULL; /* don't free the buffer */

	protows->server_itf->on_call(protows->closure, call, verb, object, sessionid, tokenid, user_creds);
	return;

out_of_memory:
	json_object_put(object);

overflow:
	afb_proto_ws_unref(protows);
}

static int server_send_description(struct afb_proto_ws *protows, uint16_t descid, struct json_object *descobj)
{
	int rc = -1;
	struct writebuf wb = { .iovcount = 0, .bufcount = 0 };

	if (writebuf_char(&wb, CHAR_FOR_DESCRIPTION)
	 && writebuf_uint16(&wb, descid)
	 && writebuf_object(&wb, descobj))
		rc = proto_write(protows, &wb);
	return rc;
}

int afb_proto_ws_describe_put(struct afb_proto_ws_describe *describe, struct json_object *description)
{
	int rc = server_send_description(describe->protows, describe->descid, description);
	afb_proto_ws_unref(describe->protows);
	free(describe);
	return rc;
}

/* on describe, propagate it to the ws service */
static void server_on_describe(struct afb_proto_ws *protows, struct readbuf *rb)
{
	uint16_t descid;
	struct afb_proto_ws_describe *desc;

	/* reads the descid */
	if (readbuf_uint16(rb, &descid)) {
		if (protows->server_itf->on_describe) {
			/* create asynchronous job */
			desc = malloc(sizeof *desc);
			if (desc) {
				desc->descid = descid;
				desc->protows = protows;
				afb_proto_ws_addref(protows);
				protows->server_itf->on_describe(protows->closure, desc);
				return;
			}
		}
		server_send_description(protows, descid, NULL);
	}
}

static void server_on_session_add(struct afb_proto_ws *protows, struct readbuf *rb)
{
	uint16_t sessionid;
	const char *sessionstr;

	if (readbuf_uint16(rb, &sessionid) && readbuf_string(rb, &sessionstr, NULL))
		protows->server_itf->on_session_create(protows->closure, sessionid, sessionstr);
}

static void server_on_session_drop(struct afb_proto_ws *protows, struct readbuf *rb)
{
	uint16_t sessionid;

	if (readbuf_uint16(rb, &sessionid))
		protows->server_itf->on_session_remove(protows->closure, sessionid);
}

static void server_on_token_add(struct afb_proto_ws *protows, struct readbuf *rb)
{
	uint16_t tokenid;
	const char *tokenstr;

	if (readbuf_uint16(rb, &tokenid) && readbuf_string(rb, &tokenstr, NULL))
		protows->server_itf->on_token_create(protows->closure, tokenid, tokenstr);
}

static void server_on_token_drop(struct afb_proto_ws *protows, struct readbuf *rb)
{
	uint16_t tokenid;

	if (readbuf_uint16(rb, &tokenid))
		protows->server_itf->on_token_remove(protows->closure, tokenid);
}

static void server_on_event_unexpected(struct afb_proto_ws *protows, struct readbuf *rb)
{
	uint16_t eventid;

	if (readbuf_uint16(rb, &eventid))
		protows->server_itf->on_event_unexpected(protows->closure, eventid);
}

/* on version offer */
static void server_on_version_offer(struct afb_proto_ws *protows, struct readbuf *rb)
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
			if (send_version_set(protows, version) >= 0) {
				protows->version = version;
				return;
			}
		}
	}
	afb_proto_ws_hangup(protows);
}

/* callback when receiving binary data */
static void server_on_binary_job(int sig, void *closure)
{
	struct binary *binary = closure;

	if (!sig) {
		switch (*binary->rb.head++) {
		case CHAR_FOR_CALL:
			server_on_call(binary->protows, &binary->rb);
			break;
		case CHAR_FOR_DESCRIBE:
			server_on_describe(binary->protows, &binary->rb);
			break;
		case CHAR_FOR_SESSION_ADD:
			server_on_session_add(binary->protows, &binary->rb);
			break;
		case CHAR_FOR_SESSION_DROP:
			server_on_session_drop(binary->protows, &binary->rb);
			break;
		case CHAR_FOR_TOKEN_ADD:
			server_on_token_add(binary->protows, &binary->rb);
			break;
		case CHAR_FOR_TOKEN_DROP:
			server_on_token_drop(binary->protows, &binary->rb);
			break;
		case CHAR_FOR_EVT_UNEXPECTED:
			server_on_event_unexpected(binary->protows, &binary->rb);
			break;
		case CHAR_FOR_VERSION_OFFER:
			server_on_version_offer(binary->protows, &binary->rb);
			break;
		default: /* unexpected message */
			/* TODO: close the connection */
			break;
		}
	}
	free(binary->rb.base);
	free(binary);
}

static void server_on_binary(void *closure, char *data, size_t size)
{
	struct afb_proto_ws *protows = closure;

	queue_message_processing(protows, data, size, server_on_binary_job);
}

/******************* server part: manage events **********************************/

static int server_event_send(struct afb_proto_ws *protows, char order, uint16_t event_id, const char *event_name, struct json_object *data)
{
	struct writebuf wb = { .iovcount = 0, .bufcount = 0 };
	int rc = -1;

	if (writebuf_char(&wb, order)
	 && writebuf_uint16(&wb, event_id)
	 && (order != CHAR_FOR_EVT_ADD || writebuf_string(&wb, event_name))
	 && (order != CHAR_FOR_EVT_PUSH || writebuf_object(&wb, data)))
		rc = proto_write(protows, &wb);
	return rc;
}

int afb_proto_ws_server_event_create(struct afb_proto_ws *protows, uint16_t event_id, const char *event_name)
{
	return server_event_send(protows, CHAR_FOR_EVT_ADD, event_id, event_name, NULL);
}

int afb_proto_ws_server_event_remove(struct afb_proto_ws *protows, uint16_t event_id)
{
	return server_event_send(protows, CHAR_FOR_EVT_DEL, event_id, NULL, NULL);
}

int afb_proto_ws_server_event_push(struct afb_proto_ws *protows, uint16_t event_id, struct json_object *data)
{
	return server_event_send(protows, CHAR_FOR_EVT_PUSH, event_id, NULL, data);
}

int afb_proto_ws_server_event_broadcast(struct afb_proto_ws *protows, const char *event_name, struct json_object *data, const unsigned char uuid[16], uint8_t hop)
{
	struct writebuf wb = { .iovcount = 0, .bufcount = 0 };
	int rc = -1;

	if (!hop)
		return 0;

	if (writebuf_char(&wb, CHAR_FOR_EVT_BROADCAST)
	 && writebuf_string(&wb, event_name)
	 && writebuf_object(&wb, data)
	 && writebuf_put(&wb, uuid, 16)
	 && writebuf_uint8(&wb, (uint8_t)(hop - 1)))
		rc = proto_write(protows, &wb);
	return rc;
}

/*****************************************************/

/* callback when receiving a hangup */
static void on_hangup(void *closure)
{
	struct afb_proto_ws *protows = closure;
	struct client_describe *cd, *ncd;
	struct client_call *call, *ncall;
	struct afb_ws *ws;

	pthread_mutex_lock(&protows->mutex);
	ncd = protows->describes;
	protows->describes = NULL;
	ncall = protows->calls;
	protows->calls = NULL;
	ws = protows->ws;
	protows->ws = NULL;
	protows->idcount = 0;
	pthread_mutex_unlock(&protows->mutex);

	while (ncall) {
		call= ncall;
		ncall = call->next;
		protows->client_itf->on_reply(protows->closure, call->request, NULL, "disconnected", "server hung up");
		free(call);
	}

	while (ncd) {
		cd= ncd;
		ncd = cd->next;
		cd->callback(cd->closure, NULL);
		free(cd);
	}

	if (ws) {
		afb_ws_destroy(ws);
		if (protows->on_hangup)
			protows->on_hangup(protows->closure);
	}
}

/*****************************************************/

static const struct afb_ws_itf proto_ws_client_ws_itf =
{
	.on_close = NULL,
	.on_text = NULL,
	.on_binary = client_on_binary,
	.on_error = NULL,
	.on_hangup = on_hangup
};

static const struct afb_ws_itf server_ws_itf =
{
	.on_close = NULL,
	.on_text = NULL,
	.on_binary = server_on_binary,
	.on_error = NULL,
	.on_hangup = on_hangup
};

/*****************************************************/

static struct afb_proto_ws *afb_proto_ws_create(int fd, int autoclose, const struct afb_proto_ws_server_itf *itfs, const struct afb_proto_ws_client_itf *itfc, void *closure, const struct afb_ws_itf *itf)
{
	struct afb_proto_ws *protows;

	protows = calloc(1, sizeof *protows);
	if (protows) {
		fcntl(fd, F_SETFD, FD_CLOEXEC);
		fcntl(fd, F_SETFL, O_NONBLOCK);
		protows->ws = afb_ws_create(fd, autoclose, itf, protows);
		if (protows->ws != NULL) {
			protows->refcount = 1;
			protows->version = WSAPI_VERSION_UNSET;
			protows->closure = closure;
			protows->server_itf = itfs;
			protows->client_itf = itfc;
			x_mutex_init(&protows->mutex);
			return protows;
		}
		free(protows);
	}
	return NULL;
}

struct afb_proto_ws *afb_proto_ws_create_client(int fd, int autoclose, const struct afb_proto_ws_client_itf *itf, void *closure)
{
	struct afb_proto_ws *protows;

	protows = afb_proto_ws_create(fd, autoclose, NULL, itf, closure, &proto_ws_client_ws_itf);
	if (protows) {
		if (send_version_offer_1(protows, WSAPI_VERSION_1) != 0) {
			afb_proto_ws_unref(protows);
			protows = NULL;
		}
	}
	return protows;
}

struct afb_proto_ws *afb_proto_ws_create_server(int fd, int autoclose, const struct afb_proto_ws_server_itf *itf, void *closure)
{
	return afb_proto_ws_create(fd, autoclose, itf, NULL, closure, &server_ws_itf);
}

void afb_proto_ws_unref(struct afb_proto_ws *protows)
{
	if (protows && !__atomic_sub_fetch(&protows->refcount, 1, __ATOMIC_RELAXED)) {
		afb_proto_ws_hangup(protows);
		x_mutex_destroy(&protows->mutex);
		free(protows);
	}
}

void afb_proto_ws_addref(struct afb_proto_ws *protows)
{
	__atomic_add_fetch(&protows->refcount, 1, __ATOMIC_RELAXED);
}

int afb_proto_ws_is_client(struct afb_proto_ws *protows)
{
	return !!protows->client_itf;
}

int afb_proto_ws_is_server(struct afb_proto_ws *protows)
{
	return !!protows->server_itf;
}

void afb_proto_ws_hangup(struct afb_proto_ws *protows)
{
	if (protows->ws)
		afb_ws_hangup(protows->ws);
}

void afb_proto_ws_on_hangup(struct afb_proto_ws *protows, void (*on_hangup)(void *closure))
{
	protows->on_hangup = on_hangup;
}

void afb_proto_ws_set_queuing(struct afb_proto_ws *protows, int (*queuing)(struct afb_proto_ws*, void (*)(int,void*), void*))
{
	protows->queuing = queuing;
}
