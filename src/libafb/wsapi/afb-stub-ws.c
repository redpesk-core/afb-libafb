/*
 * Copyright (C) 2015-2024 IoT.bzh Company
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

#include "../libafb-config.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>

#include <json-c/json.h>
#include <rp-utils/rp-verbose.h>

#include <afb/afb-event-x2.h>
#include <afb/afb-binding-x4.h>
#include <afb/afb-errno.h>

#include "core/afb-session.h"
#include "core/afb-cred.h"
#include "core/afb-apiset.h"
#include "wsapi/afb-proto-ws.h"
#include "wsapi/afb-stub-ws.h"
#include "core/afb-evt.h"
#include "core/afb-req-common.h"
#include "core/afb-json-legacy.h"
#include "core/afb-token.h"
#include "core/afb-error-text.h"
#include "core/afb-sched.h"
#include "utils/u16id.h"
#include "core/containerof.h"
#include "sys/x-errno.h"

struct afb_stub_ws;

/**
 * structure for a ws request: requests on server side
 */
struct server_req {
	struct afb_req_common comreq;	/**< the request */
	struct afb_stub_ws *stubws;	/**< the client of the request */
	struct afb_proto_ws_call *call;	/**< the incoming call */
	char strings[];			/**< for storing strings */
};

/******************* stub description for client or servers ******************/

struct afb_stub_ws
{
	/* protocol */
	struct afb_proto_ws *proto;

	/* apiset */
	struct afb_apiset *apiset;

	/* on hangup callback */
	void (*on_hangup)(struct afb_stub_ws *);

	union {
		/* server side */
		struct {
			/* listener for events */
			struct afb_evt_listener *listener;

			/* sessions */
			struct server_session *sessions;

#if WITH_CRED
			/* credentials of the client */
			struct afb_cred *cred;
#endif

			/* event from server */
			struct u16id2bool *event_flags;

			/* transmitted sessions */
			struct u16id2ptr *session_proxies;

			/* transmitted tokens */
			struct u16id2ptr *token_proxies;
		};

		/* client side */
		struct {
			/* event from server */
			struct u16id2ptr *event_proxies;

			/* transmitted sessions */
			struct u16id2bool *session_flags;

			/* transmitted tokens */
			struct u16id2bool *token_flags;

			/* robustify */
			struct {
				int (*reopen)(void*);
				void *closure;
				void (*release)(void*);
			} robust;
		};
	};

	/* count of references */
	unsigned refcount;

	/* type of the stub: 0=server, 1=client */
	uint8_t is_client;

	/* the api name */
	char apiname[];
};

static struct afb_proto_ws *create_proto(struct afb_stub_ws *stubws, int fd, int autoclose, uint8_t server);

/******************* ws request part for server *****************/

/* decrement the reference count of the request and free/release it on falling to null */
static void server_req_destroy_cb(struct afb_req_common *comreq)
{
	struct server_req *wreq = containerof(struct server_req, comreq, comreq);

	afb_req_common_cleanup(comreq);

	afb_proto_ws_call_unref(wreq->call);
	afb_stub_ws_unref(wreq->stubws);
	free(wreq);
}

struct reply_data {
	int rc;
	struct afb_proto_ws_call *call;
};

static void server_req_reply_cb2(void *closure, struct json_object *object, const char *error, const char *info)
{
	struct reply_data *rd = closure;

	rd->rc = afb_proto_ws_call_reply(rd->call, object, error, info);
}

static void server_req_reply_cb(struct afb_req_common *comreq, int status, unsigned nreplies, struct afb_data * const replies[])
{
	struct server_req *wreq = containerof(struct server_req, comreq, comreq);
	struct reply_data rd;
	int rc;

	rd.call = wreq->call;
	rc = afb_json_legacy_do_reply_json_c(&rd, status, nreplies, replies, server_req_reply_cb2);
	if (rc < 0 || rd.rc < 0)
		RP_ERROR("error while sending reply");
}

static int server_req_subscribe_cb(struct afb_req_common *comreq, struct afb_evt *event)
{
	int rc;
	struct server_req *wreq = containerof(struct server_req, comreq, comreq);

	rc = afb_evt_listener_watch_evt(wreq->stubws->listener, event);
	if (rc >= 0)
		rc = afb_proto_ws_call_subscribe(wreq->call,  afb_evt_id(event));
	if (rc < 0)
		RP_ERROR("error while subscribing event");
	return rc;
}

static int server_req_unsubscribe_cb(struct afb_req_common *comreq, struct afb_evt *event)
{
	int rc, rc2;
	struct server_req *wreq = containerof(struct server_req, comreq, comreq);

	rc = afb_evt_listener_unwatch_evt(wreq->stubws->listener, event);
	rc2 = afb_proto_ws_call_unsubscribe(wreq->call,  afb_evt_id(event));
	if (rc >= 0 && rc2 < 0)
		rc = rc2;
	if (rc < 0)
		RP_ERROR("error while unsubscribing event");
	return rc;
}

static const struct afb_req_common_query_itf server_req_req_common_itf = {
	.reply = server_req_reply_cb,
	.unref = server_req_destroy_cb,
	.subscribe = server_req_subscribe_cb,
	.unsubscribe = server_req_unsubscribe_cb,
	.interface = NULL
};

/******************* client part **********************************/

static struct afb_proto_ws *client_get_proto(struct afb_stub_ws *stubws)
{
	int fd;
	struct afb_proto_ws *proto;

	proto = stubws->proto;
	if (proto == NULL && stubws->robust.reopen) {
		fd = stubws->robust.reopen(stubws->robust.closure);
		if (fd >= 0)
			proto = create_proto(stubws, fd, 1, 1);
	}
	return proto;
}

static int client_make_ids(struct afb_stub_ws *stubws, struct afb_proto_ws *proto, struct afb_req_common *comreq, uint16_t *sessionid, uint16_t *tokenid)
{
	int rc, rc2;
	uint16_t sid, tid;

	rc = 0;

	/* get the session */
	if (!comreq->session)
		sid = 0;
	else {
		sid = afb_session_id(comreq->session);
		rc2 = u16id2bool_set(&stubws->session_flags, sid, 1);
		if (rc2 < 0)
			rc = rc2;
		else if (rc2 == 0)
			rc = afb_proto_ws_client_session_create(proto, sid, afb_session_uuid(comreq->session));
	}

	/* get the token */
	if (!comreq->token)
		tid = 0;
	else {
		tid = afb_token_id(comreq->token);
		rc2 = u16id2bool_set(&stubws->token_flags, tid, 1);
		if (rc2 < 0)
			rc = rc2;
		else if (rc2 == 0) {
			rc2 = afb_proto_ws_client_token_create(proto, tid, afb_token_string(comreq->token));
			if (rc2 < 0)
				rc = rc2;
		}
	}

	*sessionid = sid;
	*tokenid = tid;
	return rc;
}

/* on call, propagate it to the ws service */
static void process_cb(void * closure1, struct json_object *object, const void * closure2)
{
	int rc;
	struct afb_stub_ws *stubws = closure1;
	struct afb_req_common *comreq = (void*)closure2; /* remove const */
	struct afb_proto_ws *proto;
	uint16_t sessionid;
	uint16_t tokenid;

	proto = client_get_proto(stubws);
	if (proto == NULL) {
		afb_json_legacy_req_reply_hookable(comreq, NULL, afb_error_text(AFB_ERRNO_DISCONNECTED), NULL);
		return;
	}

	rc = client_make_ids(stubws, proto, comreq, &sessionid, &tokenid);
	if (rc >= 0) {
		afb_req_common_addref(comreq);
		rc = afb_proto_ws_client_call(
				proto,
				comreq->verbname,
				object,
				sessionid,
				tokenid,
				comreq,
				afb_req_common_on_behalf_cred_export(comreq));
	}
	if (rc < 0) {
		afb_json_legacy_req_reply_hookable(comreq, NULL, afb_error_text(AFB_ERRNO_INTERNAL_ERROR), "can't send message");
		afb_req_common_unref(comreq);
	}
}

static void client_api_process_cb(void * closure, struct afb_req_common *comreq)
{
	afb_json_legacy_do2_single_json_c(
		comreq->params.ndata, comreq->params.data,
		process_cb, closure, comreq);
}

/* get the description */
static void client_api_describe_cb(void * closure, void (*describecb)(void *, struct json_object *), void *clocb)
{
	struct afb_stub_ws *stubws = closure;
	struct afb_proto_ws *proto;

	proto = client_get_proto(stubws);
	if (proto)
		afb_proto_ws_client_describe(proto, describecb, clocb);
	else
		describecb(clocb, NULL);
}

/******************* server part: manage events **********************************/

static void server_event_add_cb(void *closure, const char *event, uint16_t eventid)
{
	int rc;
	struct afb_stub_ws *stubws = closure;

	if (stubws->proto != NULL) {
		rc = u16id2bool_set(&stubws->event_flags, eventid, 1);
		if (rc == 0) {
			rc = afb_proto_ws_server_event_create(stubws->proto, eventid, event);
			if (rc < 0)
				u16id2bool_set(&stubws->event_flags, eventid, 0);
		}
	}
}

static void server_event_remove_cb(void *closure, const char *event, uint16_t eventid)
{
	struct afb_stub_ws *stubws = closure;

	if (stubws->proto != NULL) {
		if (u16id2bool_set(&stubws->event_flags, eventid, 0))
			afb_proto_ws_server_event_remove(stubws->proto, eventid);
	}
}

static void server_event_push_cb2(void *closure1, struct json_object *object, const void *closure2)
{
	struct afb_proto_ws *proto = closure1;
	const struct afb_evt_pushed *event = closure2;
	afb_proto_ws_server_event_push(proto, event->data.eventid, object);
}

static void server_event_push_cb(void *closure, const struct afb_evt_pushed *event)
{
	struct afb_stub_ws *stubws = closure;

	if (stubws->proto != NULL && u16id2bool_get(stubws->event_flags, event->data.eventid))
		afb_json_legacy_do2_single_json_c(
				event->data.nparams, event->data.params,
				server_event_push_cb2, stubws->proto, event);
}

static void server_event_broadcast_cb2(void *closure1, struct json_object *object, const void *closure2)
{
	struct afb_proto_ws *proto = closure1;
	const struct afb_evt_broadcasted *event = closure2;
	afb_proto_ws_server_event_broadcast(proto, event->data.name, object, event->uuid, event->hop);
}

static void server_event_broadcast_cb(void *closure, const struct afb_evt_broadcasted *event)
{
	struct afb_stub_ws *stubws = closure;

	if (stubws->proto != NULL)
		afb_json_legacy_do2_single_json_c(
			event->data.nparams, event->data.params,
			server_event_broadcast_cb2, stubws->proto, event);
}

/*****************************************************/

static void client_on_reply_cb(void *closure, void *request, struct json_object *object, const char *error, const char *info)
{
	struct afb_req_common *comreq = request;

	afb_json_legacy_req_reply_hookable(comreq, object, error, info);
	afb_req_common_unref(comreq);
}

static void client_on_event_create_cb(void *closure, uint16_t event_id, const char *event_name)
{
	struct afb_stub_ws *stubws = closure;
	struct afb_evt *event;
	int rc;

	/* check conflicts */
	if (afb_evt_create(&event, event_name) < 0)
		RP_ERROR("can't create event %s, out of memory", event_name);
	else {
		rc = u16id2ptr_add(&stubws->event_proxies, event_id, event);
		if (rc < 0) {
			RP_ERROR("can't record event %s", event_name);
			afb_evt_unref(event);
		}
	}
}

static void client_on_event_remove_cb(void *closure, uint16_t event_id)
{
	struct afb_stub_ws *stubws = closure;
	struct afb_evt *event;
	int rc;

	rc = u16id2ptr_drop(&stubws->event_proxies, event_id, (void**)&event);
	if (rc == 0 && event)
		afb_evt_unref(event);
}

static void client_on_event_subscribe_cb(void *closure, void *request, uint16_t event_id)
{
	struct afb_stub_ws *stubws = closure;
	struct afb_req_common *comreq = request;
	struct afb_evt *event;
	int rc;

	rc = u16id2ptr_get(stubws->event_proxies, event_id, (void**)&event);
	if (rc < 0 || !event || afb_req_common_subscribe_hookable(comreq, event) < 0)
		RP_ERROR("can't subscribe: %m");
}

static void client_on_event_unsubscribe_cb(void *closure, void *request, uint16_t event_id)
{
	struct afb_stub_ws *stubws = closure;
	struct afb_req_common *comreq = request;
	struct afb_evt *event;
	int rc;

	rc = u16id2ptr_get(stubws->event_proxies, event_id, (void**)&event);
	if (rc < 0 || !event || afb_req_common_unsubscribe_hookable(comreq, event) < 0)
		RP_ERROR("can't unsubscribe: %m");
}

static void client_on_event_push_cb(void *closure, uint16_t event_id, struct json_object *data)
{
	struct afb_stub_ws *stubws = closure;
	struct afb_evt *event;
	int rc;

	rc = u16id2ptr_get(stubws->event_proxies, event_id, (void**)&event);
	if (rc >= 0 && event)
		rc = afb_json_legacy_event_push_hookable(event, data);
	else
		RP_ERROR("unreadable push event");
	if (rc <= 0)
		afb_proto_ws_client_event_unexpected(stubws->proto, event_id);
}

static void client_on_event_broadcast_cb(void *closure, const char *event_name, struct json_object *data, const rp_uuid_binary_t uuid, uint8_t hop)
{
	afb_json_legacy_event_rebroadcast_name(event_name, data, uuid, hop);
}

/*****************************************************/

static struct afb_session *server_add_session(struct afb_stub_ws *stubws, uint16_t sessionid, const char *sessionstr)
{
	struct afb_session *session;
	int rc, created;

	rc = afb_session_get(&session, sessionstr, AFB_SESSION_TIMEOUT_DEFAULT, &created);
	if (rc < 0)
		RP_ERROR("can't create session %s", sessionstr);
	else {
		afb_session_set_autoclose(session, 1);
		rc = u16id2ptr_add(&stubws->session_proxies, sessionid, session);
		if (rc < 0) {
			RP_ERROR("can't record session %s", sessionstr);
			afb_session_unref(session);
			session = NULL;
		}
	}
	return session;
}

static void server_on_session_create_cb(void *closure, uint16_t sessionid, const char *sessionstr)
{
	struct afb_stub_ws *stubws = closure;

	server_add_session(stubws, sessionid, sessionstr);
}

static void server_on_session_remove_cb(void *closure, uint16_t sessionid)
{
	struct afb_stub_ws *stubws = closure;
	struct afb_session *session;
	int rc;

	rc = u16id2ptr_drop(&stubws->session_proxies, sessionid, (void**)&session);
	if (rc == 0 && session)
		afb_session_unref(session);
}

static void server_on_token_create_cb(void *closure, uint16_t tokenid, const char *tokenstr)
{
	struct afb_stub_ws *stubws = closure;
	struct afb_token *token;
	int rc;

	rc = afb_token_get(&token, tokenstr);
	if (rc < 0)
		RP_ERROR("can't create token %s, out of memory", tokenstr);
	else {
		rc = u16id2ptr_add(&stubws->token_proxies, tokenid, token);
		if (rc < 0) {
			RP_ERROR("can't record token %s", tokenstr);
			afb_token_unref(token);
		}
	}
}

static void server_on_token_remove_cb(void *closure, uint16_t tokenid)
{
	struct afb_stub_ws *stubws = closure;
	struct afb_token *token;
	int rc;

	rc = u16id2ptr_drop(&stubws->token_proxies, tokenid, (void**)&token);
	if (rc == 0 && token)
		afb_token_unref(token);
}

static void server_on_event_unexpected_cb(void *closure, uint16_t eventid)
{
	struct afb_stub_ws *stubws = closure;

	afb_evt_listener_unwatch_id(stubws->listener, eventid);
}

static void
server_on_call_cb(
	void *closure,
	struct afb_proto_ws_call *call,
	const char *verb,
	struct json_object *object,
	uint16_t sessionid,
	uint16_t tokenid,
	const char *user_creds
) {
	struct afb_stub_ws *stubws = closure;
	struct server_req *wreq;
	struct afb_session *session;
	struct afb_token *token;
	size_t lenverb, lencreds;
	struct afb_data *arg;
	int rc;
	int err = AFB_ERRNO_OUT_OF_MEMORY;

	afb_stub_ws_addref(stubws);

	/* get tokens and sessions */
	rc = u16id2ptr_get(stubws->session_proxies, sessionid, (void**)&session);
	if (rc < 0) {
		if (sessionid != 0)
			goto no_session;
		session = server_add_session(stubws, sessionid, NULL);
		if (!session)
			goto out_of_memory;
	}
	if (!tokenid || u16id2ptr_get(stubws->token_proxies, tokenid, (void**)&token) < 0)
		token = NULL;

	/* create the request */
	lenverb = 1 + strlen(verb);
	lencreds = user_creds ? 1 + strlen(user_creds) : 0;
	wreq = malloc(sizeof *wreq + lenverb + lencreds);
	if (wreq == NULL)
		goto out_of_memory;

	/* copy strings */
	memcpy(wreq->strings, verb, lenverb);
	if (lencreds)
		user_creds = memcpy(wreq->strings + lenverb, user_creds, lencreds);

	rc = afb_json_legacy_make_data_json_c(&arg, object);
	if (rc < 0) {
		free(wreq);
		goto out_of_memory2;
	}

	/* initialise */
	wreq->stubws = stubws;
	wreq->call = call;
	afb_req_common_init(&wreq->comreq, &server_req_req_common_itf, stubws->apiname, wreq->strings, 1, &arg, stubws);
	afb_req_common_set_session(&wreq->comreq, session);
	afb_req_common_set_token(&wreq->comreq, token);
#if WITH_CRED
	afb_req_common_set_cred(&wreq->comreq, stubws->cred);
#endif
	afb_req_common_process_on_behalf(&wreq->comreq, wreq->stubws->apiset, user_creds);
	return;

no_session:
	err = AFB_ERRNO_INVALID_REQUEST;
out_of_memory:
	json_object_put(object);
out_of_memory2:
	afb_stub_ws_unref(stubws);
	afb_proto_ws_call_reply(call, NULL, afb_error_text(err), NULL);
	afb_proto_ws_call_unref(call);
}

static void server_on_description_cb(void *closure, struct json_object *description)
{
	struct afb_proto_ws_describe *describe = closure;
	afb_proto_ws_describe_put(describe, description);
	json_object_put(description);
}


static void server_on_describe_cb(void *closure, struct afb_proto_ws_describe *describe)
{
	struct afb_stub_ws *stubws = closure;

	afb_apiset_describe(stubws->apiset, stubws->apiname, server_on_description_cb, describe);
}

/*****************************************************/

static const struct afb_proto_ws_client_itf client_itf =
{
	.on_reply = client_on_reply_cb,
	.on_event_create = client_on_event_create_cb,
	.on_event_remove = client_on_event_remove_cb,
	.on_event_subscribe = client_on_event_subscribe_cb,
	.on_event_unsubscribe = client_on_event_unsubscribe_cb,
	.on_event_push = client_on_event_push_cb,
	.on_event_broadcast = client_on_event_broadcast_cb,
};

static struct afb_api_itf client_api_itf = {
	.process = client_api_process_cb,
	.describe = client_api_describe_cb
};

static const struct afb_proto_ws_server_itf server_itf =
{
	.on_session_create = server_on_session_create_cb,
	.on_session_remove = server_on_session_remove_cb,
	.on_token_create = server_on_token_create_cb,
	.on_token_remove = server_on_token_remove_cb,
	.on_call = server_on_call_cb,
	.on_describe = server_on_describe_cb,
	.on_event_unexpected = server_on_event_unexpected_cb
};

/* the interface for events pushing */
static const struct afb_evt_itf server_event_itf = {
	.broadcast = server_event_broadcast_cb,
	.push = server_event_push_cb,
	.add = server_event_add_cb,
	.remove = server_event_remove_cb
};

/*****************************************************/
/*****************************************************/

static void release_all_sessions_cb(void*closure, uint16_t id, void *ptr)
{
	struct afb_session *session = ptr;
	afb_session_unref(session);
}

static void release_all_tokens_cb(void*closure, uint16_t id, void *ptr)
{
	struct afb_token *token = ptr;
	afb_token_unref(token);
}

static void release_all_events_cb(void*closure, uint16_t id, void *ptr)
{
	struct afb_evt *eventid = ptr;
	afb_evt_unref(eventid);
}

/* disconnect */
static void disconnect(struct afb_stub_ws *stubws)
{
	struct u16id2ptr *i2p;
	struct u16id2bool *i2b;

	afb_proto_ws_unref(__atomic_exchange_n(&stubws->proto, NULL, __ATOMIC_RELAXED));
	if (stubws->is_client) {
		i2p = __atomic_exchange_n(&stubws->event_proxies, NULL, __ATOMIC_RELAXED);
		if (i2p) {
			u16id2ptr_forall(i2p, release_all_events_cb, NULL);
			u16id2ptr_destroy(&i2p);
		}
		i2b = __atomic_exchange_n(&stubws->session_flags, NULL, __ATOMIC_RELAXED);
		u16id2bool_destroy(&i2b);
		i2b = __atomic_exchange_n(&stubws->token_flags, NULL, __ATOMIC_RELAXED);
		u16id2bool_destroy(&i2b);
	} else {
		afb_evt_listener_unref(__atomic_exchange_n(&stubws->listener, NULL, __ATOMIC_RELAXED));
#if WITH_CRED
		afb_cred_unref(__atomic_exchange_n(&stubws->cred, NULL, __ATOMIC_RELAXED));
#endif
		i2b = __atomic_exchange_n(&stubws->event_flags, NULL, __ATOMIC_RELAXED);
		u16id2bool_destroy(&i2b);
		i2p = __atomic_exchange_n(&stubws->session_proxies, NULL, __ATOMIC_RELAXED);
		if (i2p) {
			u16id2ptr_forall(i2p, release_all_sessions_cb, NULL);
			u16id2ptr_destroy(&i2p);
		}
		i2p = __atomic_exchange_n(&stubws->token_proxies, NULL, __ATOMIC_RELAXED);
		if (i2p) {
			u16id2ptr_forall(i2p, release_all_tokens_cb, NULL);
			u16id2ptr_destroy(&i2p);
		}
	}
}

/* callback when receiving a hangup */
static void on_hangup(void *closure)
{
	struct afb_stub_ws *stubws = closure;

	if (stubws->proto) {
		afb_stub_ws_addref(stubws);
		disconnect(stubws);
		if (stubws->on_hangup)
			stubws->on_hangup(stubws);
		afb_stub_ws_unref(stubws);
	}
}

static int enqueue_processing(struct afb_proto_ws *proto, void (*callback)(int signum, void* arg), void *arg)
{
	return afb_sched_post_job(proto, 0, 0, callback, arg, Afb_Sched_Mode_Normal);
}

/*****************************************************/

/**
 * Create the protocol handler for the given socket
 *
 * @param stubws the stub object
 * @param fd fileno of the socket
 * @param is_client boolean true for a client, false for a server
 *
 * @return the created protocol or NULL on error
 */
static struct afb_proto_ws *create_proto(struct afb_stub_ws *stubws, int fd, int autoclose, uint8_t is_client)
{
	struct afb_proto_ws *proto;

	stubws->proto = proto = is_client
		  ? afb_proto_ws_create_client(fd, autoclose, &client_itf, stubws)
		  : afb_proto_ws_create_server(fd, autoclose, &server_itf, stubws);
	if (proto) {
		afb_proto_ws_on_hangup(proto, on_hangup);
		afb_proto_ws_set_queuing(proto, enqueue_processing);
	}

	return proto;
}

/**
 * Creation of a wsapi web-socket stub either client or server for an api
 *
 * @param fd file descriptor of the socket
 * @param apiname name of the api stubbed
 * @param apiset apiset for declaring if client or serving if server
 * @param is_client boolean true for creating client stub and false for creating a server stub
 *
 * @return a handle on the created stub object
 */
static struct afb_stub_ws *create_stub_ws(int fd, int autoclose, const char *apiname, struct afb_apiset *apiset, uint8_t is_client)
{
	struct afb_stub_ws *stubws;

	/* allocation */
	stubws = calloc(1, sizeof *stubws + 1 + strlen(apiname));
	if (stubws) {
		/* create the underlying protocol object */
		if (create_proto(stubws, fd, autoclose, is_client)) {
			/* terminate initialization */
			stubws->refcount = 1;
			stubws->is_client = is_client;
			strcpy(stubws->apiname, apiname);
			stubws->apiset = afb_apiset_addref(apiset);
			return stubws;
		}
		free(stubws);
	}
	else if (autoclose)
		close(fd);
	return NULL;
}

/* creates a client stub */
struct afb_stub_ws *afb_stub_ws_create_client(int fd, int autoclose, const char *apiname, struct afb_apiset *apiset)
{
	return create_stub_ws(fd, autoclose, apiname, apiset, 1);
}

/* creates a server stub */
struct afb_stub_ws *afb_stub_ws_create_server(int fd, int autoclose, const char *apiname, struct afb_apiset *apiset)
{
	struct afb_stub_ws *stubws;

	/* create the stub */
	stubws = create_stub_ws(fd, autoclose, apiname, apiset, 0);
	if (stubws) {
#if WITH_CRED
		/*
		 * creds of the peer are not changing
		 * except if passed to other processes
		 * TODO check how to track changes if needed
		 */
		afb_cred_create_for_socket(&stubws->cred, fd);
#endif
		/* add event listener for propagating events */
		stubws->listener = afb_evt_listener_create(&server_event_itf, stubws, stubws);
		if (stubws->listener != NULL)
			return stubws; /* success! */
		afb_stub_ws_unref(stubws);
	}
	return NULL;
}

/* sub one reference and free resources if falling to zero */
void afb_stub_ws_unref(struct afb_stub_ws *stubws)
{
	if (stubws && !__atomic_sub_fetch(&stubws->refcount, 1, __ATOMIC_RELAXED)) {

		/* release the robustification */
		if (stubws->is_client) {
			stubws->robust.reopen = NULL;
			if (stubws->robust.release)
				stubws->robust.release(stubws->robust.closure);
		}

		/* cleanup */
		disconnect(stubws);
		afb_apiset_unref(stubws->apiset);
		free(stubws);
	}
}

/* add one reference */
void afb_stub_ws_addref(struct afb_stub_ws *stubws)
{
	__atomic_add_fetch(&stubws->refcount, 1, __ATOMIC_RELAXED);
}

/* return the api name */
const char *afb_stub_ws_apiname(struct afb_stub_ws *stubws)
{
	return stubws->apiname;
}

/* set a hangup handler */
void afb_stub_ws_set_on_hangup(struct afb_stub_ws *stubws, void (*on_hangup)(struct afb_stub_ws*))
{
	stubws->on_hangup = on_hangup;
}

/* return the api object for declaring apiname in apiset */
struct afb_api_item afb_stub_ws_client_api(struct afb_stub_ws *stubws)
{
	struct afb_api_item api;

	assert(stubws->is_client); /* check client */
	api.closure = stubws;
	api.itf = &client_api_itf;
	api.group = stubws; /* serialize for reconnections */
	return api;
}

/* declares the client api in apiset */
int afb_stub_ws_client_add(struct afb_stub_ws *stubws, struct afb_apiset *apiset)
{
	return afb_apiset_add(apiset, stubws->apiname, afb_stub_ws_client_api(stubws));
}

/* set robustification functions */
void afb_stub_ws_client_robustify(struct afb_stub_ws *stubws, int (*reopen)(void*), void *closure, void (*release)(void*))
{
	assert(stubws->is_client); /* check client */

	if (stubws->robust.release)
		stubws->robust.release(stubws->robust.closure);

	stubws->robust.reopen = reopen;
	stubws->robust.closure = closure;
	stubws->robust.release = release;
}
