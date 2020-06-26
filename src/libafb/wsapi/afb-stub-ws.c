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

#include "libafb-config.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>

#include <json-c/json.h>

#include <afb/afb-event-x2.h>

#include "core/afb-session.h"
#include "core/afb-cred.h"
#include "core/afb-apiset.h"
#include "wsapi/afb-proto-ws.h"
#include "wsapi/afb-stub-ws.h"
#include "core/afb-evt.h"
#include "core/afb-req-common.h"
#include "core/afb-req-reply.h"
#include "core/afb-token.h"
#include "core/afb-error-text.h"
#include "core/afb-jobs.h"
#include "sys/verbose.h"
#include "sys/fdev.h"
#include "utils/u16id.h"
#include "core/containerof.h"

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

/**
 * structure for jobs of describing
 */
struct server_describe
{
	struct afb_stub_ws *stubws;
	struct afb_proto_ws_describe *describe;
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
				struct fdev *(*reopen)(void*);
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

static struct afb_proto_ws *afb_stub_ws_create_proto(struct afb_stub_ws *stubws, struct fdev *fdev, uint8_t server);

/******************* ws request part for server *****************/

/* decrement the reference count of the request and free/release it on falling to null */
static void server_req_destroy_cb(struct afb_req_common *comreq)
{
	struct server_req *wreq = containerof(struct server_req, comreq, comreq);

	json_object_put(comreq->json);
	afb_req_common_cleanup(comreq);

	afb_proto_ws_call_unref(wreq->call);
	afb_stub_ws_unref(wreq->stubws);
	free(wreq);
}

static void server_req_reply_cb(struct afb_req_common *comreq, const struct afb_req_reply *reply)
{
	int rc;
	struct server_req *wreq = containerof(struct server_req, comreq, comreq);

	rc = afb_proto_ws_call_reply(wreq->call, reply->object, reply->error, reply->info);
	if (rc < 0)
		ERROR("error while sending reply");
}

static int server_req_subscribe_cb(struct afb_req_common *comreq, struct afb_event_x2 *event)
{
	int rc;
	struct server_req *wreq = containerof(struct server_req, comreq, comreq);

	rc = afb_evt_listener_watch_x2(wreq->stubws->listener, event);
	if (rc >= 0)
		rc = afb_proto_ws_call_subscribe(wreq->call,  afb_evt_event_x2_id(event));
	if (rc < 0)
		ERROR("error while subscribing event");
	return rc;
}

static int server_req_unsubscribe_cb(struct afb_req_common *comreq, struct afb_event_x2 *event)
{
	int rc;
	struct server_req *wreq = containerof(struct server_req, comreq, comreq);

	rc = afb_proto_ws_call_unsubscribe(wreq->call,  afb_evt_event_x2_id(event));
	if (rc < 0)
		ERROR("error while unsubscribing event");
	return rc;
}

static const struct afb_req_common_query_itf server_req_req_common_itf = {
	.reply = server_req_reply_cb,
	.unref = server_req_destroy_cb,
	.subscribe = server_req_subscribe_cb,
	.unsubscribe = server_req_unsubscribe_cb
};

/******************* client part **********************************/

static struct afb_proto_ws *client_get_proto(struct afb_stub_ws *stubws)
{
	struct fdev *fdev;
	struct afb_proto_ws *proto;

	proto = stubws->proto;
	if (proto == NULL && stubws->robust.reopen) {
		fdev = stubws->robust.reopen(stubws->robust.closure);
		if (fdev != NULL)
			proto = afb_stub_ws_create_proto(stubws, fdev, 1);
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
static void client_api_process_cb(void * closure, struct afb_req_common *comreq)
{
	int rc;
	struct afb_stub_ws *stubws = closure;
	struct afb_proto_ws *proto;
	uint16_t sessionid;
	uint16_t tokenid;

	proto = client_get_proto(stubws);
	if (proto == NULL) {
		afb_req_common_reply(comreq, NULL, afb_error_text_disconnected, NULL);
		return;
	}

	rc = client_make_ids(stubws, proto, comreq, &sessionid, &tokenid);
	if (rc >= 0) {
		afb_req_common_addref(comreq);
		rc = afb_proto_ws_client_call(
				proto,
				comreq->verbname,
				afb_req_common_json(comreq),
				sessionid,
				tokenid,
				comreq,
				afb_req_common_on_behalf_cred_export(comreq));
	}
	if (rc < 0) {
		afb_req_common_reply(comreq, NULL, afb_error_text_internal_error, "can't send message");
		afb_req_common_unref(comreq);
	}
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

static void server_event_push_cb(void *closure, const char *event, uint16_t eventid, struct json_object *object)
{
	struct afb_stub_ws *stubws = closure;

	if (stubws->proto != NULL && u16id2bool_get(stubws->event_flags, eventid))
		afb_proto_ws_server_event_push(stubws->proto, eventid, object);
	json_object_put(object);
}

static void server_event_broadcast_cb(void *closure, const char *event, struct json_object *object, const uuid_binary_t uuid, uint8_t hop)
{
	struct afb_stub_ws *stubws = closure;

	if (stubws->proto != NULL)
		afb_proto_ws_server_event_broadcast(stubws->proto, event, object, uuid, hop);
	json_object_put(object);
}

/*****************************************************/

static void client_on_reply_cb(void *closure, void *request, struct json_object *object, const char *error, const char *info)
{
	struct afb_req_common *comreq = request;

	afb_req_common_reply_hookable(comreq, object, error, info);
	afb_req_common_unref(comreq);
}

static void client_on_event_create_cb(void *closure, uint16_t event_id, const char *event_name)
{
	struct afb_stub_ws *stubws = closure;
	struct afb_event_x2 *event;
	int rc;
	
	/* check conflicts */
	event = afb_evt_event_x2_create(event_name);
	if (event == NULL)
		ERROR("can't create event %s, out of memory", event_name);
	else {
		rc = u16id2ptr_add(&stubws->event_proxies, event_id, event);
		if (rc < 0) {
			ERROR("can't record event %s", event_name);
			afb_evt_event_x2_unref(event);
		}
	}
}

static void client_on_event_remove_cb(void *closure, uint16_t event_id)
{
	struct afb_stub_ws *stubws = closure;
	struct afb_event_x2 *event;
	int rc;

	rc = u16id2ptr_drop(&stubws->event_proxies, event_id, (void**)&event);
	if (rc == 0 && event)
		afb_evt_event_x2_unref(event);
}

static void client_on_event_subscribe_cb(void *closure, void *request, uint16_t event_id)
{
	struct afb_stub_ws *stubws = closure;
	struct afb_req_common *comreq = request;
	struct afb_event_x2 *event;
	int rc;

	rc = u16id2ptr_get(stubws->event_proxies, event_id, (void**)&event);
	if (rc < 0 || !event || afb_req_common_subscribe_event_x2_hookable(comreq, event) < 0)
		ERROR("can't subscribe: %m");
}

static void client_on_event_unsubscribe_cb(void *closure, void *request, uint16_t event_id)
{
	struct afb_stub_ws *stubws = closure;
	struct afb_req_common *comreq = request;
	struct afb_event_x2 *event;
	int rc;

	rc = u16id2ptr_get(stubws->event_proxies, event_id, (void**)&event);
	if (rc < 0 || !event || afb_req_common_unsubscribe_event_x2_hookable(comreq, event) < 0)
		ERROR("can't unsubscribe: %m");
}

static void client_on_event_push_cb(void *closure, uint16_t event_id, struct json_object *data)
{
	struct afb_stub_ws *stubws = closure;
	struct afb_event_x2 *event;
	int rc;

	rc = u16id2ptr_get(stubws->event_proxies, event_id, (void**)&event);
	if (rc >= 0 && event)
		rc = afb_evt_event_x2_push(event, data);
	else
		ERROR("unreadable push event");
	if (rc <= 0)
		afb_proto_ws_client_event_unexpected(stubws->proto, event_id);
}

static void client_on_event_broadcast_cb(void *closure, const char *event_name, struct json_object *data, const uuid_binary_t uuid, uint8_t hop)
{
	afb_evt_rebroadcast_name(event_name, data, uuid, hop);
}

/*****************************************************/

static struct afb_session *server_add_session(struct afb_stub_ws *stubws, uint16_t sessionid, const char *sessionstr)
{
	struct afb_session *session;
	int rc, created;

	session = afb_session_get(sessionstr, AFB_SESSION_TIMEOUT_DEFAULT, &created);
	if (session == NULL)
		ERROR("can't create session %s, out of memory", sessionstr);
	else {
		afb_session_set_autoclose(session, 1);
		rc = u16id2ptr_add(&stubws->session_proxies, sessionid, session);
		if (rc < 0) {
			ERROR("can't record session %s", sessionstr);
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
		ERROR("can't create token %s, out of memory", tokenstr);
	else {
		rc = u16id2ptr_add(&stubws->token_proxies, tokenid, token);
		if (rc < 0) {
			ERROR("can't record token %s", tokenstr);
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
	struct json_object *args,
	uint16_t sessionid,
	uint16_t tokenid,
	const char *user_creds
) {
	const char *errstr = afb_error_text_internal_error;
	struct afb_stub_ws *stubws = closure;
	struct server_req *wreq;
	struct afb_session *session;
	struct afb_token *token;
	size_t lenverb, lencreds;
	int rc;

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

	/* initialise */
	wreq->stubws = stubws;
	wreq->call = call;
	afb_req_common_init(&wreq->comreq, &server_req_req_common_itf, stubws->apiname, wreq->strings);
	wreq->comreq.json = args;
	afb_req_common_set_session(&wreq->comreq, session);
	afb_req_common_set_token(&wreq->comreq, token);
#if WITH_CRED
	afb_req_common_set_cred(&wreq->comreq, stubws->cred);
#endif
	afb_req_common_process_on_behalf(&wreq->comreq, wreq->stubws->apiset, user_creds);
	return;

no_session:
	errstr = afb_error_text_unknown_session;
out_of_memory:
	json_object_put(args);
	afb_stub_ws_unref(stubws);
	afb_proto_ws_call_reply(call, NULL, errstr, NULL);
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
	struct afb_event_x2 *eventid = ptr;
	afb_evt_event_x2_unref(eventid);
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
	return afb_jobs_queue(proto, 0, callback, arg);
}

/*****************************************************/

static struct afb_proto_ws *afb_stub_ws_create_proto(struct afb_stub_ws *stubws, struct fdev *fdev, uint8_t is_client)
{
	struct afb_proto_ws *proto;

	stubws->proto = proto = is_client
		  ? afb_proto_ws_create_client(fdev, &client_itf, stubws)
		  : afb_proto_ws_create_server(fdev, &server_itf, stubws);
	if (proto) {
		afb_proto_ws_on_hangup(proto, on_hangup);
		afb_proto_ws_set_queuing(proto, enqueue_processing);
	}

	return proto;
}

static struct afb_stub_ws *afb_stub_ws_create(struct fdev *fdev, const char *apiname, struct afb_apiset *apiset, uint8_t is_client)
{
	struct afb_stub_ws *stubws;

	stubws = calloc(1, sizeof *stubws + 1 + strlen(apiname));
	if (stubws) {
		if (afb_stub_ws_create_proto(stubws, fdev, is_client)) {
			stubws->refcount = 1;
			stubws->is_client = is_client;
			strcpy(stubws->apiname, apiname);
			stubws->apiset = afb_apiset_addref(apiset);
			return stubws;
		}
		free(stubws);
	}
	fdev_unref(fdev);
	return NULL;
}

struct afb_stub_ws *afb_stub_ws_create_client(struct fdev *fdev, const char *apiname, struct afb_apiset *apiset)
{
	return afb_stub_ws_create(fdev, apiname, apiset, 1);
}

struct afb_stub_ws *afb_stub_ws_create_server(struct fdev *fdev, const char *apiname, struct afb_apiset *apiset)
{
	struct afb_stub_ws *stubws;

	stubws = afb_stub_ws_create(fdev, apiname, apiset, 0);
	if (stubws) {
#if WITH_CRED
		afb_cred_create_for_socket(&stubws->cred, fdev_fd(fdev));
#endif
		stubws->listener = afb_evt_listener_create(&server_event_itf, stubws);
		if (stubws->listener != NULL)
			return stubws;
		afb_stub_ws_unref(stubws);
	}
	return NULL;
}

void afb_stub_ws_unref(struct afb_stub_ws *stubws)
{
	if (stubws && !__atomic_sub_fetch(&stubws->refcount, 1, __ATOMIC_RELAXED)) {

		if (stubws->is_client) {
			stubws->robust.reopen = NULL;
			if (stubws->robust.release)
				stubws->robust.release(stubws->robust.closure);
		}

		disconnect(stubws);
		afb_apiset_unref(stubws->apiset);
		free(stubws);
	}
}

void afb_stub_ws_addref(struct afb_stub_ws *stubws)
{
	__atomic_add_fetch(&stubws->refcount, 1, __ATOMIC_RELAXED);
}

void afb_stub_ws_set_on_hangup(struct afb_stub_ws *stubws, void (*on_hangup)(struct afb_stub_ws*))
{
	stubws->on_hangup = on_hangup;
}

const char *afb_stub_ws_name(struct afb_stub_ws *stubws)
{
	return stubws->apiname;
}

struct afb_api_item afb_stub_ws_client_api(struct afb_stub_ws *stubws)
{
	struct afb_api_item api;

	assert(stubws->is_client); /* check client */
	api.closure = stubws;
	api.itf = &client_api_itf;
	api.group = stubws; /* serialize for reconnections */
	return api;
}

int afb_stub_ws_client_add(struct afb_stub_ws *stubws, struct afb_apiset *apiset)
{
	return afb_apiset_add(apiset, stubws->apiname, afb_stub_ws_client_api(stubws));
}

void afb_stub_ws_client_robustify(struct afb_stub_ws *stubws, struct fdev *(*reopen)(void*), void *closure, void (*release)(void*))
{
	assert(stubws->is_client); /* check client */

	if (stubws->robust.release)
		stubws->robust.release(stubws->robust.closure);

	stubws->robust.reopen = reopen;
	stubws->robust.closure = closure;
	stubws->robust.release = release;
}
