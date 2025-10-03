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

#include "../libafb-config.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <rp-utils/rp-verbose.h>
#include <afb/afb-req-subcall-flags.h>
#include <afb/afb-errno.h>

#include "core/afb-data.h"
#include "core/afb-calls.h"
#include "core/afb-evt.h"
#include "core/afb-data-array.h"
#include "core/afb-api-common.h"
#include "core/afb-hook.h"
#include "core/afb-session.h"
#include "core/afb-req-common.h"

#include "core/afb-sched.h"
#include "sys/x-errno.h"
#include "containerof.h"

/**
 * Flags for calls
 */
#define CALLFLAGS            (afb_req_subcall_api_session|afb_req_subcall_catch_events)



/******************************************************************************/
/* TODO: synchronous (sub)calls could be made as real synchronous calls       */
/******************************************************************************/

/**
 * Structure for call requests
 */
struct req_calls
{
	/** the common request item */
	struct afb_req_common comreq;

	/** the calling apiname */
	struct afb_api_common *comapi;

	/** the closures for the result */
	void (*callback)(void*, void*, void*, int, unsigned, struct afb_data * const[]);
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
static void req_calls_reply_cb(struct afb_req_common *comreq, int status, unsigned nreplies, struct afb_data * const replies[])
{
	struct req_calls *req = containerof(struct req_calls, comreq, comreq);
	if (req->callback)
		req->callback(req->closure1, req->closure2, req->closure3, status, nreplies, replies);
}

/**
 * handle releasing a call
 */
static void req_calls_destroy_cb(struct afb_req_common *comreq)
{
	struct req_calls *req = containerof(struct req_calls, comreq, comreq);
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
	.unsubscribe = req_calls_unsubscribe_cb,
	.interface = NULL
};

/******************************************************************************/

static
struct req_calls *
make_call_req(
	struct afb_api_common *comapi,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	struct afb_data * const params[],
	void (*callback)(void*, void*, void*, int, unsigned, struct afb_data * const[]),
	void *closure1,
	void *closure2,
	void *closure3,
	struct afb_req_common *caller,
	int flags,
	const struct afb_req_common_query_itf *itf,
	int copynames
) {
	struct req_calls *req;
	size_t lenapi, lenverb;
	struct afb_session *session;
	struct afb_token *token;
#if WITH_CRED
	struct afb_cred *cred;
#endif

	/* allocates the call request */
	if (copynames) {
		lenapi = 1 + strlen(apiname);
		lenverb = 1 + strlen(verbname);
	}
	else {
		lenapi = 0;
		lenverb = 0;
	}
	req = malloc(lenapi + lenverb + sizeof *req);
	if (!req) {
		/* error! out of memory */
		afb_data_array_unref(nparams, params);
		RP_ERROR("out of memory");
		callback(closure1, closure2, closure3, X_ENOMEM, 0, NULL);
		return NULL;
	}

	/* success of allocation */
	if (copynames) {
		/* copy names */
		apiname = memcpy(req->strings, apiname, lenapi);
		verbname = memcpy(req->strings + lenapi, verbname, lenverb);
	}

	/* records the parameters later used */
	req->comapi = comapi;
	req->callback = callback;
	req->closure1 = closure1;
	req->closure2 = closure2;
	req->closure3 = closure3;
	req->caller = caller;
	req->flags = flags;

	/* initialise the common request */
	afb_req_common_init(&req->comreq, itf, apiname, verbname, nparams, params, comapi->group);

	/* set the session of the request */
	session = (flags & afb_req_subcall_api_session)
			? afb_api_common_session_get(comapi)
			: caller->session;
	afb_req_common_set_session(&req->comreq, session);

	/*
	** TODO: for token and credentials, try to use items from the commen apiname
	** (but first make it available)
	*/

	/* set the token of the request */
	token = (flags & afb_req_subcall_on_behalf) ? caller->token : NULL;
	afb_req_common_set_token(&req->comreq, token);

#if WITH_CRED
	/* set the cred of the request */
	cred = (flags & afb_req_subcall_on_behalf) ? caller->credentials : NULL;
	afb_req_common_set_cred(&req->comreq, cred);
#endif

	/* return the made request now */
	return req;
}

static
void
process(
	struct afb_api_common *comapi,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	struct afb_data * const params[],
	void (*callback)(void*, void*, void*, int, unsigned, struct afb_data * const[]),
	void *closure1,
	void *closure2,
	void *closure3,
	struct afb_req_common *caller,
	int flags,
	const struct afb_req_common_query_itf *itf,
	int copynames
) {
	struct req_calls *req = make_call_req(comapi, apiname, verbname, nparams, params,
	                                      callback, closure1, closure2, closure3,
	                                      caller, flags, itf, copynames);
	if (req != NULL)
		afb_req_common_process(&req->comreq, afb_api_common_call_set(req->comapi));
}

#if WITH_AFB_CALL_SYNC
struct psync
{
	struct afb_api_common *comapi;
	const char *apiname;
	const char *verbname;
	unsigned nparams;
	struct afb_data * const *params;
	int *status;
	unsigned *nreplies;
	struct afb_data **replies;
	struct afb_req_common *caller;
	int flags;
	int completed;
	struct req_calls *callreq;
};

static void call_sync_reply(struct psync *ps, int status, unsigned nreplies, struct afb_data * const replies[])
{
	if (!ps->completed) {
		if (ps->nreplies) {
			if (ps->replies) {
				if (nreplies > *ps->nreplies)
					nreplies = *ps->nreplies;
				afb_data_array_copy_addref(nreplies, replies, ps->replies);
			}
			*ps->nreplies = nreplies;
		}
		if (ps->status)
			*ps->status = status;
		ps->completed = 1;
	}
}

static void call_sync_leave(void *closure1, void *closure2, void *closure3, int status, unsigned nreplies, struct afb_data * const replies[])
{
	struct afb_sched_lock *lock = closure1;
	struct psync *ps = closure2;

	call_sync_reply(ps, status, nreplies, replies);
	afb_sched_leave(lock);
}

static void process_sync_enter_cb(int signum, void *closure, struct afb_sched_lock *lock)
{
	struct psync *ps = closure;
	if (signum == 0) {
		ps->callreq = make_call_req(ps->comapi, ps->apiname, ps->verbname, ps->nparams, ps->params,
		                            call_sync_leave, lock, ps, 0,
		                            ps->caller, ps->flags, &req_call_itf, 1);
		if (ps->callreq == NULL)
			ps->completed = 1;
		else {
			afb_req_common_addref(&ps->callreq->comreq);
			afb_req_common_process(&ps->callreq->comreq, afb_api_common_call_set(ps->comapi));
		}
	}
	else if (ps->callreq != NULL)
		ps->callreq->callback = NULL;
}

static
int
process_sync(
	struct afb_api_common *comapi,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	struct afb_data * const params[],
	int *status,
	unsigned *nreplies,
	struct afb_data *replies[],
	struct afb_req_common *caller,
	int flags
) {
	int rc;
	struct psync ps;

	ps.comapi = comapi;
	ps.apiname = apiname;
	ps.verbname = verbname;
	ps.nparams = nparams;
	ps.params = params;
	ps.status = status;
	ps.nreplies = nreplies;
	ps.replies = replies;
	ps.caller = caller;
	ps.flags = flags;
	ps.callreq = NULL;

	ps.completed = 0;

	rc = afb_sched_sync(0, process_sync_enter_cb, &ps);
	if (ps.callreq != NULL) {
		ps.callreq->callback = NULL;
		afb_req_common_unref(&ps.callreq->comreq);
	}
	call_sync_reply(&ps, AFB_ERRNO_NO_REPLY, 0, NULL);
	return rc;
}
#else
static
int
process_sync(
	struct afb_api_common *comapi,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	struct afb_data * const params[],
	int *status,
	unsigned *nreplies,
	struct afb_data *replies[],
	struct afb_req_common *caller,
	int flags
) {
	RP_ERROR("Calls/Subcalls sync are not supported");
	if (status)
		*status = X_ENOTSUP;
	if (nreplies)
		*nreplies = 0;
	return X_ENOTSUP;
}
#endif

/******************************************************************************/
/** calls */
/******************************************************************************/

void
afb_calls_call(
	struct afb_api_common *comapi,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	struct afb_data * const params[],
	void (*callback)(void*, void*, void*, int, unsigned, struct afb_data * const[]),
	void *closure1,
	void *closure2,
	void *closure3
) {
	process(comapi, apiname, verbname, nparams, params,
		callback, closure1, closure2, closure3,
		NULL, CALLFLAGS, &req_call_itf, 1);
}

void
afb_calls_subcall(
	struct afb_api_common *comapi,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	struct afb_data * const params[],
	void (*callback)(void*, void*, void*, int, unsigned, struct afb_data * const[]),
	void *closure1,
	void *closure2,
	void *closure3,
	struct afb_req_common *comreq,
	int flags
) {
	process(comapi, apiname, verbname, nparams, params,
		callback, closure1, closure2, closure3,
		comreq, flags, &req_call_itf, 1);
}

int
afb_calls_call_sync(
	struct afb_api_common *comapi,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	struct afb_data * const params[],
	int *status,
	unsigned *nreplies,
	struct afb_data *replies[]
) {
	return process_sync(comapi, apiname, verbname, nparams, params,
				status, nreplies, replies, NULL, CALLFLAGS);
}

int
afb_calls_subcall_sync(
	struct afb_api_common *comapi,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	struct afb_data * const params[],
	int *status,
	unsigned *nreplies,
	struct afb_data *replies[],
	struct afb_req_common *comreq,
	int flags
) {
	return process_sync(comapi, apiname, verbname, nparams, params,
				status, nreplies, replies, comreq, flags);
}

/******************************************************************************/
#if WITH_AFB_HOOK
static void req_calls_reply_hookable_cb(struct afb_req_common *comreq, int status, unsigned nreplies, struct afb_data * const replies[])
{
	struct req_calls *req = containerof(struct req_calls, comreq, comreq);
	afb_hook_api_call_result(req->comapi, status, nreplies, replies);
	req_calls_reply_cb(comreq, status, nreplies, replies);
}

const struct afb_req_common_query_itf req_calls_hookable_itf = {
	.unref = req_calls_destroy_cb,
	.reply = req_calls_reply_hookable_cb,
	.subscribe = req_calls_subscribe_cb,
	.unsubscribe = req_calls_unsubscribe_cb,
	.interface = NULL
};

static void req_subcalls_reply_hookable_cb(struct afb_req_common *comreq, int status, unsigned nreplies, struct afb_data * const replies[])
{
	struct req_calls *req = containerof(struct req_calls, comreq, comreq);
	afb_hook_req_subcall_result(&req->comreq, status, nreplies, replies);
	req_calls_reply_cb(comreq, status, nreplies, replies);
}

const struct afb_req_common_query_itf req_subcalls_hookable_itf = {
	.unref = req_calls_destroy_cb,
	.reply = req_subcalls_reply_hookable_cb,
	.subscribe = req_calls_subscribe_cb,
	.unsubscribe = req_calls_unsubscribe_cb,
	.interface = NULL
};

void
afb_calls_call_hooking(
	struct afb_api_common *comapi,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	struct afb_data * const params[],
	void (*callback)(void*, void*, void*, int, unsigned, struct afb_data * const[]),
	void *closure1,
	void *closure2,
	void *closure3
) {
	afb_hook_api_call(comapi, apiname, verbname, nparams, params);
	process(comapi, apiname, verbname, nparams, params,
		callback, closure1, closure2, closure3,
		NULL, CALLFLAGS, &req_calls_hookable_itf, 1);
}

void
afb_calls_subcall_hooking(
	struct afb_api_common *comapi,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	struct afb_data * const params[],
	void (*callback)(void*, void*, void*, int, unsigned, struct afb_data * const[]),
	void *closure1,
	void *closure2,
	void *closure3,
	struct afb_req_common *comreq,
	int flags
) {
	afb_hook_req_subcall(comreq, apiname, verbname, nparams, params, flags);
	process(comapi, apiname, verbname, nparams, params,
		callback, closure1, closure2, closure3,
		comreq, flags, &req_subcalls_hookable_itf, 1);
}

int
afb_calls_call_sync_hooking(
	struct afb_api_common *comapi,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	struct afb_data * const params[],
	int *status,
	unsigned *nreplies,
	struct afb_data *replies[]
) {
	int result;
	afb_hook_api_callsync(comapi, apiname, verbname, nparams, params);
	result = afb_calls_call_sync(comapi, apiname, verbname, nparams, params, status, nreplies, replies);
	afb_hook_api_callsync_result(comapi, result, status, nreplies, replies);
	return result;
}

int
afb_calls_subcall_sync_hooking(
	struct afb_api_common *comapi,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	struct afb_data * const params[],
	int *status,
	unsigned *nreplies,
	struct afb_data *replies[],
	struct afb_req_common *comreq,
	int flags
) {
	int result;
	afb_hook_req_subcallsync(comreq, apiname, verbname, nparams, params, flags);
	result = afb_calls_subcall_sync(comapi, apiname, verbname, nparams, params, status, nreplies, replies, comreq, flags);
	afb_hook_req_subcallsync_result(comreq, result, status, nreplies, replies);
	return result;
}
#endif
