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

#include <json-c/json.h>

#define AFB_BINDING_VERSION 0
#include <afb/afb-binding.h>

#include "core/afb-calls.h"
#include "core/afb-evt.h"
#include "core/afb-api-common.h"
#include "core/afb-hook.h"
#include "core/afb-msg-json.h"
#include "core/afb-session.h"
#include "core/afb-req-common.h"
#include "core/afb-req-reply.h"
#include "core/afb-error-text.h"

#include "core/afb-sched.h"
#include "sys/verbose.h"
#include "sys/x-errno.h"
#include "containerof.h"

/**
 * Flags for calls
 */
#define CALLFLAGS            (afb_req_subcall_api_session|afb_req_subcall_catch_events)



/******************************************************************************/

/**
 * Structure for call requests
 */
struct req_calls
{
	/** the common request item */
	struct afb_req_common comreq;

	/** the calling api */
	struct afb_api_common *comapi;

	/** the closures for the result */
	void (*callback)(void*, void*, void*, const struct afb_req_reply*);
	void *closure1;
	void *closure2;
	void *closure3;

	/** caller req */
	struct afb_req_common *caller;

	/** flags for events */
	int flags;

	/* strings */
	char strings[];
};

/******************************************************************************/

/**
 * handle reply to a call
 */
static void req_calls_reply_cb(struct afb_req_common *comreq, const struct afb_req_reply *reply)
{
	struct req_calls *req = containerof(struct req_calls, comreq, comreq);
	req->callback(req->closure1, req->closure2, req->closure3, reply);
}

/**
 * handle releasing a call
 */
static void req_calls_destroy_cb(struct afb_req_common *comreq)
{
	struct req_calls *req = containerof(struct req_calls, comreq, comreq);
	json_object_put(comreq->json);
	afb_req_common_cleanup(comreq);
	free(req);
}

/**
 * handle subscribing from a call
 */
static int req_calls_subscribe_cb(struct afb_req_common *comreq, struct afb_evt *event)
{
	struct req_calls *req = containerof(struct req_calls, comreq, comreq);
	int rc = 0, rc2;

	if (req->flags & afb_req_subcall_pass_events)
		rc = afb_req_common_subscribe(req->caller, event);

	if (req->flags & afb_req_subcall_catch_events) {
		rc2 = afb_api_common_subscribe(req->comapi, event);
		if (rc2 < 0)
			rc = rc2;
	}

	return rc;
}

/**
 * handle unsubscribing from a call
 */
static int req_calls_unsubscribe_cb(struct afb_req_common *comreq, struct afb_evt *event)
{
	struct req_calls *req = containerof(struct req_calls, comreq, comreq);
	int rc = 0, rc2;

	if (req->flags & afb_req_subcall_pass_events)
		rc = afb_req_common_unsubscribe(req->caller, event);

	if (req->flags & afb_req_subcall_catch_events) {
		rc2 = afb_api_common_unsubscribe(req->comapi, event);
		if (rc2 < 0)
			rc = rc2;
	}

	return rc;
}

/**
 * call handling interface
 */
const struct afb_req_common_query_itf req_call_itf = {
	.unref = req_calls_destroy_cb,
	.reply = req_calls_reply_cb,
	.subscribe = req_calls_subscribe_cb,
	.unsubscribe = req_calls_unsubscribe_cb
};

/******************************************************************************/

static
void
process(
	struct afb_api_common *comapi,
	const char *api,
	const char *verb,
	struct json_object *args,
	void (*callback)(void*, void*, void*, const struct afb_req_reply*),
	void *closure1,
	void *closure2,
	void *closure3,
	struct afb_req_common *caller,
	int flags,
	const struct afb_req_common_query_itf *itf,
	int copynames
) {
	struct req_calls *req;
	struct afb_req_reply reply;
	size_t lenapi, lenverb;

	/* allocates the call request */
	if (copynames) {
		lenapi = 1 + strlen(api);
		lenverb = 1 + strlen(verb);
	}
	else {
		lenapi = 0;
		lenverb = 0;
	}
	req = malloc(lenapi + lenverb + sizeof *req);
	if (!req) {
		/* error! out of memory */
		ERROR("out of memory");
		json_object_put(args);
		reply.object = NULL;
		reply.info = NULL;
		reply.error = "out-of-memory";
		callback(closure1, closure2, closure3, &reply);
	}
	else {
		/* success of allocation */
		if (copynames) {
			/* copy names */
			api = memcpy(req->strings, api, lenapi);
			verb = memcpy(req->strings + lenapi, verb, lenverb);
		}

		/* initialise the request */
		req->comapi = comapi;
		req->callback = callback;
		req->closure1 = closure1;
		req->closure2 = closure2;
		req->closure3 = closure3;
		req->caller = caller;
		req->flags = flags;
		afb_req_common_init(&req->comreq, itf, api, verb);
		req->comreq.json = args;
		if (flags & afb_req_subcall_api_session) {
			afb_req_common_set_session(&req->comreq, afb_api_common_session_get(comapi));
		}
		else {
			afb_req_common_set_session(&req->comreq, caller->session);
		}
		if (flags & afb_req_subcall_on_behalf) {
			afb_req_common_set_token(&req->comreq, caller->token);
#if WITH_CRED
			afb_req_common_set_cred(&req->comreq, caller->credentials);
#endif
		}
		else {
			afb_req_common_set_token(&req->comreq, NULL); /* FIXME: comapi */
#if WITH_CRED
			afb_req_common_set_cred(&req->comreq, NULL); /* FIXME: comapi */
#endif
		}

		/* process the request now */
		afb_req_common_process(&req->comreq, afb_api_common_call_set(comapi));
	}
}

#if WITH_AFB_CALL_SYNC
struct psync
{
	struct afb_api_common *comapi;
	const char *api;
	const char *verb;
	struct json_object *args;
	struct afb_req_reply *reply;
	struct afb_req_common *caller;
	int flags;

	struct afb_sched_lock *lock;
	int result;
	int completed;
};

static void call_sync_leave(void *closure1, void *closure2, void *closure3, const struct afb_req_reply *reply)
{
	struct psync *ps = closure1;
	int rc;

	rc = afb_req_reply_copy(reply, ps->reply);
	ps->result = rc < 0 ? rc : reply->error ? -X_EPROTO : 0;
	ps->completed = 1;
	afb_sched_leave(ps->lock);
}

static void process_sync_enter_cb(int signum, void *closure, struct afb_sched_lock *afb_sched_lock)
{
	struct psync *ps = closure;
	if (!signum) {
		ps->lock = afb_sched_lock;
		process(ps->comapi, ps->api, ps->verb, ps->args,
			call_sync_leave, ps, 0, 0,
			ps->caller, ps->flags, &req_call_itf, 0);
	} else {
		ps->result = X_EINTR;
	}
}

int
process_sync(
	struct afb_api_common *comapi,
	const char *api,
	const char *verb,
	struct json_object *args,
	struct afb_req_reply *reply,
	struct afb_req_common *caller,
	int flags
) {
	int rc;
	struct psync ps;

	ps.comapi = comapi;
	ps.api = api;
	ps.verb = verb;
	ps.args = args;
	ps.reply = reply;
	ps.caller = caller;
	ps.flags = flags;

	ps.result = 0;
	ps.completed = 0;

	rc = afb_sched_enter(NULL, 0, process_sync_enter_cb, &ps);
	if (rc >= 0 && ps.completed) {
		rc = ps.result;
	}
	else {
		if (rc >= 0)
			rc = X_EINTR;
		if (reply) {
			reply->object = NULL;
			reply->error = strdup("internal-error");
			reply->info = NULL;
		}
	}
	return rc;
}
#else
int
process_sync(
	struct afb_api_common *comapi,
	const char *api,
	const char *verb,
	struct json_object *args,
	struct afb_req_reply *reply,
	struct afb_req_common *caller,
	int flags
) {
	ERROR("Calls/Subcalls sync are not supported");
	if (reply) {
		reply->object = NULL;
		reply->error = strdup("no-call-sync");
		reply->info = NULL;
	}
	return X_ENOTSUP;
}
#endif

/******************************************************************************/
/** calls */
/******************************************************************************/

void
afb_calls_call(
	struct afb_api_common *comapi,
	const char *api,
	const char *verb,
	struct json_object *args,
	void (*callback)(void*, void*, void*, const struct afb_req_reply*),
	void *closure1,
	void *closure2,
	void *closure3
) {
	process(comapi, api, verb, args,
		callback, closure1, closure2, closure3,
		NULL, CALLFLAGS, &req_call_itf, 1);
}

int
afb_calls_call_sync(
	struct afb_api_common *comapi,
	const char *api,
	const char *verb,
	struct json_object *args,
	struct afb_req_reply *reply
) {
	return process_sync(comapi, api, verb, args, reply, NULL, CALLFLAGS);
}

/******************************************************************************/
#if WITH_AFB_HOOK
static void req_calls_reply_hookable_cb(struct afb_req_common *comreq, const struct afb_req_reply *reply)
{
	struct req_calls *req = containerof(struct req_calls, comreq, comreq);
	afb_hook_api_call_result(req->comapi, reply->object, reply->error, reply->info);
	req_calls_reply_cb(comreq, reply);
}

const struct afb_req_common_query_itf req_calls_hookable_itf = {
	.unref = req_calls_destroy_cb,
	.reply = req_calls_reply_hookable_cb,
	.subscribe = req_calls_subscribe_cb,
	.unsubscribe = req_calls_unsubscribe_cb
};

void
afb_calls_call_hookable(
	struct afb_api_common *comapi,
	const char *api,
	const char *verb,
	struct json_object *args,
	void (*callback)(void*, void*, void*, const struct afb_req_reply*),
	void *closure1,
	void *closure2,
	void *closure3
) {
	afb_hook_api_call(comapi, api, verb, args);
	process(comapi, api, verb, args,
		callback, closure1, closure2, closure3,
		NULL, CALLFLAGS, &req_calls_hookable_itf, 1);
}

int
afb_calls_call_sync_hookable(
	struct afb_api_common *comapi,
	const char *api,
	const char *verb,
	struct json_object *args,
	struct afb_req_reply *reply
) {
	int result;
	afb_hook_api_callsync(comapi, api, verb, args);
	result = afb_calls_call_sync(comapi, api, verb, args, reply);
	return afb_hook_api_callsync_result(comapi, result, reply->object, reply->error, reply->info);
}
#endif

/******************************************************************************/
/** subcalls                                                                  */
/******************************************************************************/

void
afb_calls_subcall(
	struct afb_api_common *comapi,
	const char *api,
	const char *verb,
	struct json_object *args,
	void (*callback)(void*, void*, void*, const struct afb_req_reply*),
	void *closure1,
	void *closure2,
	void *closure3,
	struct afb_req_common *comreq,
	int flags
) {
	process(comapi, api, verb, args,
		callback, closure1, closure2, closure3,
		comreq, flags, &req_call_itf, 1);
}

int
afb_calls_subcall_sync(
	struct afb_api_common *comapi,
	const char *api,
	const char *verb,
	struct json_object *args,
	struct afb_req_reply *reply,
	struct afb_req_common *comreq,
	int flags
) {
	return process_sync(comapi, api, verb, args, reply, comreq, flags);
}

/******************************************************************************/
#if WITH_AFB_HOOK
static void req_subcalls_reply_hookable_cb(struct afb_req_common *comreq, const struct afb_req_reply *reply)
{
	struct req_calls *req = containerof(struct req_calls, comreq, comreq);
	afb_hook_req_subcall_result(&req->comreq, reply->object, reply->error, reply->info);
	req_calls_reply_hookable_cb(comreq, reply);
}

const struct afb_req_common_query_itf req_subcalls_hookable_itf = {
	.unref = req_calls_destroy_cb,
	.reply = req_subcalls_reply_hookable_cb,
	.subscribe = req_calls_subscribe_cb,
	.unsubscribe = req_calls_unsubscribe_cb
};

void
afb_calls_subcall_hookable(
	struct afb_api_common *comapi,
	const char *api,
	const char *verb,
	struct json_object *args,
	void (*callback)(void*, void*, void*, const struct afb_req_reply*),
	void *closure1,
	void *closure2,
	void *closure3,
	struct afb_req_common *comreq,
	int flags
) {
	afb_hook_req_subcall(comreq, api, verb, args, flags);
	process(comapi, api, verb, args,
		callback, closure1, closure2, closure3,
		comreq, flags, &req_subcalls_hookable_itf, 1);
}

int
afb_calls_subcall_sync_hookable(
	struct afb_api_common *comapi,
	const char *api,
	const char *verb,
	struct json_object *args,
	struct afb_req_reply *reply,
	struct afb_req_common *comreq,
	int flags
) {
	int result;
	afb_hook_req_subcallsync(comreq, api, verb, args, flags);
	result = afb_calls_subcall_sync(comapi, api, verb, args, reply, comreq, flags);
	return afb_hook_req_subcallsync_result(comreq, result, reply->object, reply->error, reply->info);
}
#endif
