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
#include <unistd.h>
#include <assert.h>
#include <string.h>

#include <json-c/json.h>

#include "wsj1/afb-wsj1.h"
#include "wsj1/afb-ws-json1.h"
#include "core/afb-session.h"
#include "core/afb-data.h"
#include "core/afb-type.h"
#include "core/afb-type-predefined.h"
#include "core/afb-cred.h"
#include "core/afb-apiset.h"
#include "core/afb-req-common.h"
#include "core/afb-json-legacy.h"
#include "core/afb-evt.h"
#include "core/afb-token.h"
#include "core/containerof.h"

#include "sys/systemd.h"
#include "sys/verbose.h"
#include "legacy/fdev.h"

/* predeclaration of structures */
struct afb_ws_json1;
struct afb_wsreq;

/* predeclaration of websocket callbacks */
static void aws_on_hangup_cb(void *closure, struct afb_wsj1 *wsj1);
static void aws_on_call_cb(void *closure, const char *api, const char *verb, struct afb_wsj1_msg *msg);
static void aws_on_push_cb(void *closure, const struct afb_evt_pushed *event);
static void aws_on_broadcast_cb(void *closure, const struct afb_evt_broadcasted *event);

/* predeclaration of wsreq callbacks */
static void wsreq_destroy(struct afb_req_common *comreq);
static void wsreq_reply(struct afb_req_common *comreq, int status, unsigned nparams, struct afb_data * const params[]);
static int wsreq_subscribe(struct afb_req_common *comreq, struct afb_evt *event);
static int wsreq_unsubscribe(struct afb_req_common *comreq, struct afb_evt *event);

/* declaration of websocket structure */
struct afb_ws_json1
{
	int refcount;
	void (*cleanup)(void*);
	void *cleanup_closure;
	struct afb_session *session;
	struct afb_token *token;
	struct afb_evt_listener *listener;
	struct afb_wsj1 *wsj1;
	struct afb_apiset *apiset;
#if WITH_CRED
	struct afb_cred *cred;
#endif
};

/* declaration of wsreq structure */
struct afb_wsreq
{
	struct afb_req_common comreq;
	struct afb_ws_json1 *aws;
	struct afb_wsreq *next;
	struct afb_wsj1_msg *msgj1;
};

/* interface for afb_ws_json1 / afb_wsj1 */
static struct afb_wsj1_itf wsj1_itf = {
	.on_hangup = aws_on_hangup_cb,
	.on_call = aws_on_call_cb
};

/* interface for comreq */
const struct afb_req_common_query_itf afb_ws_json1_req_common_itf = {
	.reply = wsreq_reply,
	.subscribe = wsreq_subscribe,
	.unsubscribe = wsreq_unsubscribe,
	.unref = wsreq_destroy
};

/* the interface for events */
static const struct afb_evt_itf evt_itf = {
	.broadcast = aws_on_broadcast_cb,
	.push = aws_on_push_cb
};

/***************************************************************
****************************************************************
**
**  functions of afb_ws_json1 / afb_wsj1
**
****************************************************************
***************************************************************/

struct afb_ws_json1 *
afb_ws_json1_create(
	struct fdev *fdev,
	struct afb_apiset *apiset,
	struct afb_session *session,
	struct afb_token *token,
	void (*cleanup)(void*),
	void *cleanup_closure
) {
	struct afb_ws_json1 *result;

	assert(fdev);

	result = malloc(sizeof * result);
	if (result == NULL)
		goto error;

	result->refcount = 1;
	result->cleanup = cleanup;
	result->cleanup_closure = cleanup_closure;
	result->session = afb_session_addref(session);
	result->token = afb_token_addref(token);
	if (result->session == NULL)
		goto error2;

	result->wsj1 = afb_wsj1_create(fdev, &wsj1_itf, result);
	if (result->wsj1 == NULL)
		goto error3;

	result->listener = afb_evt_listener_create(&evt_itf, result);
	if (result->listener == NULL)
		goto error4;

#if WITH_CRED
	afb_cred_create_for_socket(&result->cred, fdev_fd(fdev));
#endif
	result->apiset = afb_apiset_addref(apiset);
	return result;

error4:
	afb_wsj1_unref(result->wsj1);
error3:
	afb_session_unref(result->session);
	afb_token_unref(result->token);
error2:
	free(result);
error:
	fdev_unref(fdev);
	return NULL;
}

struct afb_ws_json1 *afb_ws_json1_addref(struct afb_ws_json1 *ws)
{
	__atomic_add_fetch(&ws->refcount, 1, __ATOMIC_RELAXED);
	return ws;
}

void afb_ws_json1_unref(struct afb_ws_json1 *ws)
{
	if (!__atomic_sub_fetch(&ws->refcount, 1, __ATOMIC_RELAXED)) {
		afb_evt_listener_unref(ws->listener);
		afb_wsj1_unref(ws->wsj1);
		if (ws->cleanup != NULL)
			ws->cleanup(ws->cleanup_closure);
		afb_token_unref(ws->token);
		afb_session_unref(ws->session);
#if WITH_CRED
		afb_cred_unref(ws->cred);
#endif
		afb_apiset_unref(ws->apiset);
		free(ws);
	}
}

static void aws_on_hangup_cb(void *closure, struct afb_wsj1 *wsj1)
{
	struct afb_ws_json1 *ws = closure;
	afb_ws_json1_unref(ws);
}

static int aws_new_token(struct afb_ws_json1 *ws, const char *new_token_string)
{
	int rc;
	struct afb_token *newtok, *oldtok;

	rc = afb_token_get(&newtok, new_token_string);
	if (rc >= 0) {
		oldtok = ws->token;
		ws->token = newtok;
		afb_token_unref(oldtok);
	}
	return rc;
}

static void aws_on_call_cb(void *closure, const char *api, const char *verb, struct afb_wsj1_msg *msg)
{
	struct afb_ws_json1 *ws = closure;
	struct afb_wsreq *wsreq;
	const char *tok;
	const char *object;
	struct afb_data *arg;
	size_t len;
	int rc;

	object = afb_wsj1_msg_object_s(msg, &len);
	DEBUG("received websocket request for %s/%s: %s", api, verb, object);

	/* make params */
	afb_wsj1_msg_addref(msg);
	rc = afb_data_create_raw(&arg, &afb_type_predefined_json, object, 1+len,
					(void*)afb_wsj1_msg_unref, msg);
	if (rc < 0) {
		afb_wsj1_close(ws->wsj1, 1008, NULL);
		return;
	}

	/* handle new tokens */
	tok = afb_wsj1_msg_token(msg);
	if (tok)
		aws_new_token(ws, tok);

	/* allocate */
	wsreq = calloc(1, sizeof *wsreq);
	if (wsreq == NULL) {
		afb_data_unref(arg);
		afb_wsj1_close(ws->wsj1, 1008, NULL);
		return;
	}

	/* init the context */
	afb_req_common_init(&wsreq->comreq, &afb_ws_json1_req_common_itf, api, verb, 1, &arg);
	afb_req_common_set_session(&wsreq->comreq, ws->session);
	afb_req_common_set_token(&wsreq->comreq, ws->token);
#if WITH_CRED
	afb_req_common_set_cred(&wsreq->comreq, ws->cred);
#endif

	/* fill and record the request */
	afb_wsj1_msg_addref(msg);
	wsreq->msgj1 = msg;
	wsreq->aws = afb_ws_json1_addref(ws);

	/* emits the call */
	afb_req_common_process(&wsreq->comreq, ws->apiset);
}

static void aws_on_event(struct afb_ws_json1 *aws, const struct afb_evt_data *event)
{
	int rc;
	char *msg;
	size_t size;

	rc = afb_json_legacy_make_msg_string_event(&msg, &size, event->name, event->nparams, event->params);
	if (rc >= 0) {
		rc = afb_wsj1_send_event_s(aws->wsj1, event->name, msg);
		free(msg);
	}
	if (rc < 0)
		ERROR("Can't send event %s: %m", event->name);
}

static void aws_on_push_cb(void *closure, const struct afb_evt_pushed *event)
{
	aws_on_event(closure, &event->data);
}

static void aws_on_broadcast_cb(void *closure, const struct afb_evt_broadcasted *event)
{
	aws_on_event(closure, &event->data);
}

/***************************************************************
****************************************************************
**
**  functions of wsreq / afb_req
**
****************************************************************
***************************************************************/

static void wsreq_destroy(struct afb_req_common *comreq)
{
	struct afb_wsreq *wsreq = containerof(struct afb_wsreq, comreq, comreq);

	afb_req_common_cleanup(comreq);
	afb_wsj1_msg_unref(wsreq->msgj1);
	afb_ws_json1_unref(wsreq->aws);
	free(wsreq);
}

static void wsreq_reply(struct afb_req_common *comreq, int status, unsigned nparams, struct afb_data * const params[])
{
	struct afb_wsreq *wsreq = containerof(struct afb_wsreq, comreq, comreq);
	size_t size;
	int rc;
	char *msg;

	/* create the reply */
	rc = afb_json_legacy_make_msg_string_reply(&msg, &size, status, nparams, params);
	if (rc >= 0) {
		rc = afb_wsj1_reply_s(wsreq->msgj1, msg, NULL, status < 0);
		free(msg);
	}
	if (rc < 0)
		ERROR("Can't send reply: %m");
}

static int wsreq_subscribe(struct afb_req_common *comreq, struct afb_evt *event)
{
	struct afb_wsreq *wsreq = containerof(struct afb_wsreq, comreq, comreq);

	return afb_evt_listener_watch_evt(wsreq->aws->listener, event);
}

static int wsreq_unsubscribe(struct afb_req_common *comreq, struct afb_evt *event)
{
	struct afb_wsreq *wsreq = containerof(struct afb_wsreq, comreq, comreq);

	return afb_evt_listener_unwatch_evt(wsreq->aws->listener, event);
}

