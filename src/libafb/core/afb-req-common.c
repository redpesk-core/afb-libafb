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
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <json-c/json.h>
#if !defined(JSON_C_TO_STRING_NOSLASHESCAPE)
#define JSON_C_TO_STRING_NOSLASHESCAPE 0
#endif

#include <afb/afb-binding-v3.h>
#include <afb/afb-req-x2.h>

#include "sys/x-errno.h"
#include "core/afb-apiset.h"
#include "core/afb-auth.h"
#include "core/afb-calls.h"
#include "core/afb-evt.h"
#include "core/afb-cred.h"
#include "core/afb-token.h"
#include "core/afb-hook.h"
#include "core/afb-msg-json.h"
#include "core/afb-req-common.h"
#include "core/afb-error-text.h"
#include "core/afb-jobs.h"
#include "core/afb-sched.h"
#include "core/afb-session.h"
#include "core/afb-perm.h"
#include "core/afb-permission-text.h"

#include "sys/verbose.h"

#include "containerof.h"

/******************************************************************************/
/*** LOCAL HELPERS                                                          ***/
/******************************************************************************/

static
int
async_cb_status_set(
	struct afb_req_common *req,
	void (*callback)(void *_closure, int _status),
	void *closure
) {
	return afb_req_common_async_push2(req, callback, closure);
}

static
void
async_cb_status_final(
	struct afb_req_common *req,
	int status
) {
	void (*callback)(void *_closure, int _status);
	void *closure;

	closure = afb_req_common_async_pop(req);
	callback = afb_req_common_async_pop(req);
	callback(closure, status);
}

/******************************************************************************/

int afb_req_common_reply_out_of_memory(struct afb_req_common *req)
{
	afb_req_common_reply_hookable(req, NULL, afb_error_text_internal_error, NULL);
	return X_ENOMEM;
}

int afb_req_common_reply_internal_error(struct afb_req_common *req)
{
	afb_req_common_reply_hookable(req, NULL, afb_error_text_internal_error, NULL);
	return X_ENOMEM;
}

int afb_req_common_reply_unavailable(struct afb_req_common *req)
{
	afb_req_common_reply_hookable(req, NULL, afb_error_text_not_available, NULL);
	return X_ENOMEM;
}

int afb_req_common_reply_api_unknown(struct afb_req_common *req)
{
	afb_req_common_reply_hookable(req, NULL, afb_error_text_unknown_api, NULL);
	return X_EINVAL;
}

int afb_req_common_reply_api_bad_state(struct afb_req_common *req)
{
	afb_req_common_reply_hookable(req, NULL, afb_error_text_not_available, NULL);
	return X_EINVAL;
}

int afb_req_common_reply_verb_unknown(struct afb_req_common *req)
{
	afb_req_common_reply_hookable(req, NULL, afb_error_text_unknown_verb, NULL);
	return X_EINVAL;
}

int afb_req_common_reply_invalid_token(struct afb_req_common *req)
{
	afb_req_common_reply_hookable(req, NULL, afb_error_text_invalid_token, NULL); /* TODO: or "no token" */
	return X_EPERM;
}

int afb_req_common_reply_insufficient_scope(struct afb_req_common *req, const char *scope)
{
	afb_req_common_reply_hookable(req, NULL, afb_error_text_insufficient_scope, scope);
	return X_EPERM;
}

const char *afb_req_common_on_behalf_cred_export(struct afb_req_common *req)
{
#if WITH_CRED
	return req->credentials ? afb_cred_export(req->credentials) : NULL;
#else
	return NULL;
#endif
}

int
afb_req_common_async_push(
	struct afb_req_common *req,
	void *value
) {
	unsigned i = req->asyncount;

	if (i == (sizeof req->asyncitems / sizeof req->asyncitems[0]))
		return 0;

	req->asyncitems[i] = value;
	req->asyncount = (uint16_t)((i + 1) & 15);
	return 1;
}

int
afb_req_common_async_push2(
	struct afb_req_common *req,
	void *value1,
	void *value2
) {
	unsigned i = req->asyncount;

	if (i + 1 >= (sizeof req->asyncitems / sizeof req->asyncitems[0]))
		return 0;

	req->asyncitems[i] = value1;
	req->asyncitems[i + 1] = value2;
	req->asyncount = (uint16_t)((i + 2) & 15);
	return 1;
}

void*
afb_req_common_async_pop(
	struct afb_req_common *req
) {
	unsigned i = req->asyncount;

	if (i == 0)
		return 0;

	i = i - 1;
	req->asyncount = (uint16_t)(i & 15);
	return req->asyncitems[i];
}

void
afb_req_common_init(
	struct afb_req_common *req,
	const struct afb_req_common_query_itf *queryitf,
	const char *apiname,
	const char *verbname
) {
	memset(req, 0, sizeof *req);
	req->refcount = 1;
	req->queryitf = queryitf;
	req->apiname = apiname;
	req->verbname = verbname;
}

void
afb_req_common_set_session(
	struct afb_req_common *req,
	struct afb_session *session
) {
	struct afb_session *osession;
	
	osession = req->session;
	req->session = afb_session_addref(session);
	afb_session_unref(osession);
}

void
afb_req_common_set_session_string(
	struct afb_req_common *req,
	const char *string
) {
	struct afb_session *osession;
	
	osession = req->session;
	req->session = afb_session_get(string, AFB_SESSION_TIMEOUT_DEFAULT, NULL);
	afb_session_unref(osession);
}

void
afb_req_common_set_token(
	struct afb_req_common *req,
	struct afb_token *token
) {
	struct afb_token *otoken;

	otoken = req->token;
	req->token = afb_token_addref(token);
	afb_token_unref(otoken);
}

void
afb_req_common_set_token_string(
	struct afb_req_common *req,
	const char *string
) {
	struct afb_token *otoken;

	otoken = req->token;
	afb_token_get(&req->token, string);
	afb_token_unref(otoken);
}

#if WITH_CRED
void
afb_req_common_set_cred(
	struct afb_req_common *req,
	struct afb_cred *cred
) {
	struct afb_cred *ocred = req->credentials;
	if (ocred != cred) {
		req->credentials = afb_cred_addref(cred);
		afb_cred_unref(ocred);
	}
}
#endif

void
afb_req_common_cleanup(
	struct afb_req_common *req
) {
	if (req->session && req->closing)
		afb_session_drop_key(req->session, req->api);
	afb_req_common_set_session(req, NULL);
	afb_req_common_set_token(req, NULL);
#if WITH_CRED
	afb_req_common_set_cred(req, NULL);
#endif
}


#if WITH_REQ_PROCESS_ASYNC
/**
 * job callback for asynchronous and secured processing of the x2.
 */
static void req_common_process_async_cb(int signum, void *arg)
{
	struct afb_req_common *req = arg;
	const struct afb_api_item *api;

	if (signum != 0) {
		/* emit the error (assumes that hooking is initialised) */
		ERROR("received signal %d (%s) when processing request", signum, strsignal(signum));
		afb_req_common_reply_internal_error(req);
	} else {
		/* invoke api call method to process the x2 */
		api = req->api;
		api->itf->process(api->closure, req);
	}
	/* release the x2 */
	afb_req_common_unref(req);
}

static void req_common_process_api(struct afb_req_common *req, int timeout)
{
	int rc;

	afb_req_common_addref(req);
	rc = afb_jobs_queue(req->api->group, timeout, req_common_process_async_cb, req);
	if (rc < 0) {
		/* TODO: allows or not to proccess it directly as when no threading? (see above) */
		ERROR("can't process job with threads: %s", strerror(-rc));
		afb_req_common_reply_internal_error(req);
		afb_req_common_unref(req);
	}
}

#else

static inline void req_common_process_api(struct afb_req_common *req, int timeout)
{
	const struct afb_api_item *api = req->api;
	api->itf->process(api->closure, req);
}

#endif

static void req_common_process_internal(struct afb_req_common *req, struct afb_apiset *apiset)
{
	int rc;

	/* lookup at the api */
	rc = afb_apiset_get_api(apiset, req->apiname, 1, 1, &req->api);
	if (rc >= 0) {
		req_common_process_api(req, afb_apiset_timeout_get(apiset));
	}
	else if (rc == X_ENOENT) {
		afb_req_common_reply_api_unknown(req);
	}
	else {
		afb_req_common_reply_api_bad_state(req);
	}
	afb_req_common_unref(req);
}

/**
 * Enqueue a job for processing the request 'req' using the given 'apiset'.
 * Errors are reported as request failures.
 */
void
afb_req_common_process(
	struct afb_req_common *req,
	struct afb_apiset *apiset
) {
	/* init hooking */
#if WITH_AFB_HOOK
	afb_hook_init_req(req);
	if (req->hookflags)
		afb_hook_req_begin(req);
#endif

	req_common_process_internal(req, apiset);
}

#if WITH_CRED
static
void
process_on_behalf_cb(
	void *closure,
	int status
) {
	struct afb_req_common *req = closure;
	struct afb_apiset *apiset;
	struct afb_cred *cred;

	cred = afb_req_common_async_pop(req);
	apiset = afb_req_common_async_pop(req);

	if (status > 0) {
		afb_req_common_set_cred(req, cred);
		req_common_process_internal(req, apiset);
	}
	else {
		afb_req_common_reply_insufficient_scope(req, NULL);
		afb_cred_unref(cred);
		afb_req_common_unref(req);
	}
}
#endif

/**
 */
void
afb_req_common_process_on_behalf(
	struct afb_req_common *req,
	struct afb_apiset *apiset,
	const char *import
) {
#if !WITH_CRED
	afb_req_common_process(req, apiset);
#else
	int rc;
	struct afb_cred *cred;

	/* init hooking */
#if WITH_AFB_HOOK
	afb_hook_init_req(req);
	if (req->hookflags)
		afb_hook_req_begin(req);
#endif
	if (import == NULL) {
		req_common_process_internal(req, apiset);
		return;
	}

	rc = afb_cred_import(&cred, import);
	if (rc < 0) {
		ERROR("Can't import on behalf credentials: %m");
	}
	else if (afb_req_common_async_push2(req, apiset, cred) == 0) {
		ERROR("internal error when importing creds");
		afb_cred_unref(cred);
	}
	else {
		afb_req_common_check_permission(req, afb_permission_on_behalf_credential, process_on_behalf_cb, req);
		return;
	}
	afb_req_common_reply_internal_error(req);
	afb_req_common_unref(req);
#endif
}

/******************************************************************************/

static
void
validate_async_cb(
	void *closure,
	int status
) {
	struct afb_req_common *req = closure;
	if (status <= 0)
		req->invalidated = 1;
	else
		req->validated = 1;
	async_cb_status_final(req, status);
}

void
afb_req_common_validate_async(
	struct afb_req_common *req,
	void (*callback)(void *_closure, int _status),
	void *closure
) {
	int status;

	if (req->validated)
		status = 1;
	else if (req->invalidated)
		status = 0;
	else if (!async_cb_status_set(req, callback, closure))
		status = X_EBUSY;
	else {
		afb_perm_check_req_async(req, afb_permission_token_valid, validate_async_cb, req);
		return;
	}

	callback(closure, status);
}

int
afb_req_common_has_loa(
	struct afb_req_common *req,
	int value
) {
	return value && afb_session_get_loa(req->session, req->api) >= value;
}

/******************************************************************************/


static void check_and_set_final(
	struct afb_req_common *req,
	int status
) {
	if (status <= 0) {
		/* TODO FIXME report consistent error: scope/invalid... */
		afb_req_common_reply(req, NULL, "invalid", NULL);
	}
	async_cb_status_final(req, status);
}

static void check_and_set_auth_async(
	struct afb_req_common *req,
	const struct afb_auth *auth
);

/**
 * callback receiving status checks
 */
static void check_and_set_auth_async_next(
	struct afb_req_common *req,
	int status
)
{
	const struct afb_auth *auth;

	/* pop the result as needed */
	while (req->asyncount > 2) {
		auth = (const struct afb_auth*)afb_req_common_async_pop(req);
		switch (auth->type) {
		case afb_auth_Or:
			if (status == 0) {
				/* query continuation of or */
				check_and_set_auth_async(req, auth->next);
				return;
			}
			break;

		case afb_auth_And:
			if (status > 0) {
				/* query continuation of and */
				check_and_set_auth_async(req, auth->next);
				return;
			}
			break;

		case afb_auth_Not:
			status = status < 0 ? status : !status;
			break;

		default:
			status = 0;
			break;
		}
	}

	/* emit the results */
	check_and_set_final(req, status);
}

static
void
check_and_set_auth_async_cb(
	void *closure,
	int status
) {
	struct afb_req_common *req = closure;
	check_and_set_auth_async_next(req, status);
}

/**
 * asynchronous check of auth in the context of stack
 */
static void check_and_set_auth_async(
	struct afb_req_common *req,
	const struct afb_auth *auth
)
{
	int status = 0;

again:
	switch (auth->type) {
	default:
	case afb_auth_No:
		status = 0;
		break;

	case afb_auth_Token:
		afb_req_common_validate_async(req, check_and_set_auth_async_cb, req);
		return;

	case afb_auth_LOA:
		status = afb_req_common_has_loa(req, auth->loa);
		break;

	case afb_auth_Permission:
		afb_req_common_check_permission(req, auth->text, check_and_set_auth_async_cb, req);
		return;

	case afb_auth_Or:
	case afb_auth_And:
	case afb_auth_Not:
		if (afb_req_common_async_push(req, (void*)auth)) {
			auth = auth->first;
			goto again;
		}
		status = 0;
		break;

	case afb_auth_Yes:
		status = 1;
		break;
	}
	check_and_set_auth_async_next(req, status);
}


static void check_and_set_validate_cb(void *closure, int status)
{
	struct afb_req_common *req = closure;
	const struct afb_auth *auth;

	auth = (const struct afb_auth *)afb_req_common_async_pop(req);
	if (status <= 0)
		check_and_set_final(req, status);
	else
		check_and_set_auth_async(req, auth);
}

void
afb_req_common_check_and_set_session_async(
	struct afb_req_common *req,
	const struct afb_auth *auth,
	uint32_t sessionflags,
	void (*callback)(void *_closure, int _status),
	void *closure
) {
	int loa;

	if (!sessionflags && !auth) {
		/* fast track: nothing to check */
		callback(closure, 1);
	}
	else if (req->asyncount != 0 || !async_cb_status_set(req, callback, closure)) {
		/* can't prepare async */
		afb_req_common_reply_internal_error(req);
		callback(closure, X_EBUSY);
	}
	else if (!sessionflags) {
		/* no flag case: just check auth */
		check_and_set_auth_async(req, auth);
	}
	else {
		/* check of session flags */
		if (sessionflags & AFB_SESSION_CLOSE_X2)
			req->closing = 1;
		loa = (int)(sessionflags & AFB_SESSION_LOA_MASK_X2);
		if (loa && afb_session_get_loa(req->session, req->api) < loa) {
			/* invalidated LOA */
			check_and_set_final(req, 0);
		}
		else if (!auth) {
			/* just validate, no auth */
			afb_req_common_validate_async(req, check_and_set_auth_async_cb, req);
		}
		else if (afb_req_common_async_push(req, (void*)auth)) {
			/* some auth, evaluate it after validation */
			afb_req_common_validate_async(req, check_and_set_validate_cb, req);
		}
		else {
			/* failed to push auth */
			check_and_set_final(req, X_EBUSY);
		}
	}
}

/******************************************************************************/

struct afb_req_common *
afb_req_common_addref(
	struct afb_req_common *req
) {
	if (req)
		__atomic_add_fetch(&req->refcount, 1, __ATOMIC_RELAXED);
	return req;
}

void
afb_req_common_unref(
	struct afb_req_common *req
) {
	if (req && !__atomic_sub_fetch(&req->refcount, 1, __ATOMIC_RELAXED)) {
		if (!req->replied) {
			afb_req_common_reply(req, NULL, afb_error_text_not_replied, NULL);
			if (__atomic_load_n(&req->refcount, __ATOMIC_RELAXED)) {
				/* replying may have the side effect to re-increment
				** the reference count, showing a delayed usage of
				** of the request for handling it's reply */
				return;
			}
		}
#if WITH_AFB_HOOK
		if (req->hookflags)
			afb_hook_req_end(req);
#endif
		req->queryitf->unref(req);
	}
}

void
afb_req_common_vverbose(
	struct afb_req_common *req,
	int level,
	const char *file,
	int line,
	const char *func,
	const char *fmt,
	va_list args
) {
	char *p;

	if (!fmt || vasprintf(&p, fmt, args) < 0)
		vverbose(level, file, line, func, fmt, args);
	else {
		verbose(level, file, line, func, "[REQ/API %s] %s", req->apiname, p);
		free(p);
	}
}

/******************************************************************************/

struct json_object *
afb_req_common_json(
	struct afb_req_common *req
) {
	if (!req->json && req->queryitf->json)
		req->json = req->queryitf->json(req);
	return req->json;
}

struct afb_arg
afb_req_common_get(
	struct afb_req_common *req,
	const char *name
) {
	struct afb_arg arg;
	struct json_object *object, *value;

	if (req->queryitf->get)
		arg = req->queryitf->get(req, name);
	else {
		object = afb_req_common_json(req);
		if (json_object_object_get_ex(object, name, &value)) {
			arg.name = name;
			arg.value = json_object_get_string(value);
		} else {
			arg.name = NULL;
			arg.value = NULL;
		}
		arg.path = NULL;
	}
	return arg;
}

/******************************************************************************/

static
int
check_not_replied(
	struct afb_req_common *req,
	struct json_object *obj
) {
	int result = !req->replied;
	if (!result) {
		ERROR("reply called more than one time!!");
		json_object_put(obj);
	}
	return result;
}

#if WITH_REPLY_JOB
static
void
reply_job(
	int signum,
	void *closure
) {
	struct afb_req_common *req = closure;
	if (!signum)
		req->queryitf->reply(req, &req->reply);

	if (req->reply.object_put)
	 	json_object_put(req->reply.object);
	 if (req->reply.error_mode == Afb_String_Free)
		free(req->reply.error);
	 if (req->reply.info_mode == Afb_String_Free)
		free(req->reply.info);
	afb_req_common_unref(req);
}

static
void
do_reply(
	struct afb_req_common *req,
	const struct afb_req_reply *reply
) {
	char *string;
	int haderror = 0;

	req->replied = 1;

	req->reply.object = reply->object_put ? reply->object : json_object_get(reply->object);
	req->reply.object_put = 1;

	string = req->reply.error = reply->error;
	if (reply->error_mode != Afb_String_Copy) {
		req->reply.error_mode = reply->error_mode;
	}
	else if (!string) {
		req->reply.error_mode = Afb_String_Const;
	}
	else {
		string = strdup(string);
		if (string) {
			req->reply.error = string;
			req->reply.error_mode = Afb_String_Free;
		}
		else {
			haderror = 1;
			req->reply.error_mode = Afb_String_Copy;
		}
	}

	string = req->reply.info = reply->info;
	if (reply->info_mode != Afb_String_Copy) {
		req->reply.info_mode = reply->info_mode;
	}
	else if (!string) {
		req->reply.info_mode = Afb_String_Const;
	}
	else {
		string = strdup(string);
		if (string) {
			req->reply.info = string;
			req->reply.info_mode = Afb_String_Free;
		}
		else {
			haderror = 1;
			req->reply.info_mode = Afb_String_Const;
		}
	}

	afb_req_common_addref(req);
	if (haderror || afb_jobs_queue(NULL, 0, reply_job, req) < 0)
		reply_job(0, req);
}

#else

static
void
do_reply(
	struct afb_req_common *req,
	const struct afb_req_reply *reply
) {
	req->replied = 1;
	req->queryitf->reply(req, reply);
}

#endif

void
afb_req_common_reply(
	struct afb_req_common *req,
	struct json_object *obj,
	const char *error,
	const char *info
) {
	struct afb_req_reply reply;
	if (check_not_replied(req, obj)) {
		reply.object = obj;
		reply.error = (char*)error;
		reply.info = (char*)info;
		reply.object_put = 1;
		reply.error_mode = Afb_String_Copy;
		reply.info_mode = Afb_String_Copy;
		do_reply(req, &reply);
	}
}

void afb_req_common_vreply(
	struct afb_req_common *req,
	struct json_object *obj,
	const char *error,
	const char *fmt,
	va_list args
) {
	struct afb_req_reply reply;
	char *info;

	if (check_not_replied(req, obj)) {
		if (fmt == NULL || vasprintf(&info, fmt, args) < 0)
			info = NULL;
		reply.object = obj;
		reply.error = (char*)error;
		reply.info = info;
		reply.object_put = 1;
		reply.error_mode = Afb_String_Copy;
		reply.info_mode = info ? Afb_String_Free : Afb_String_Const;
		do_reply(req, &reply);
	}
}

/******************************************************************************/

int
afb_req_common_subscribe_event_x2(
	struct afb_req_common *req,
	struct afb_event_x2 *event
) {
	if (req->replied) {
		ERROR("request replied, subscription impossible");
		return X_EINVAL;
	}
	if (req->queryitf->subscribe)
		return req->queryitf->subscribe(req, event);
	ERROR("no event listener, subscription impossible");
	return X_ENOTSUP;
}

int
afb_req_common_unsubscribe_event_x2(
	struct afb_req_common *req,
	struct afb_event_x2 *event
) {
	if (req->replied) {
		ERROR("request replied, unsubscription impossible");
		return X_EINVAL;
	}
	if (req->queryitf->unsubscribe)
		return req->queryitf->unsubscribe(req, event);
	ERROR("no event listener, unsubscription impossible");
	return X_ENOTSUP;
}

/******************************************************************************/

void *
afb_req_common_cookie(
	struct afb_req_common *req,
	void *(*maker)(void*),
	void (*freeer)(void*),
	void *closure,
	int replace
) {
	return afb_session_cookie(req->session, req->api, maker, freeer, closure, replace);
}

int
afb_req_common_session_set_LOA(
	struct afb_req_common *req,
	unsigned level
) {
	return afb_session_set_loa(req->session, req->api, (int)level);
}

void
afb_req_common_session_close(
	struct afb_req_common *req
) {
	req->closing = 1;
}

struct json_object *
afb_req_common_get_client_info(
	struct afb_req_common *req
) {
	struct json_object *r = json_object_new_object();
#if WITH_CRED
	struct afb_cred *cred = req->credentials;
	if (cred && cred->id) {
		json_object_object_add(r, "uid", json_object_new_int(cred->uid));
		json_object_object_add(r, "gid", json_object_new_int(cred->gid));
		json_object_object_add(r, "pid", json_object_new_int(cred->pid));
		json_object_object_add(r, "user", json_object_new_string(cred->user));
		json_object_object_add(r, "label", json_object_new_string(cred->label));
		json_object_object_add(r, "id", json_object_new_string(cred->id));
	}
#endif
	if (req->session) {
		json_object_object_add(r, "uuid", json_object_new_string(afb_session_uuid(req->session)?:""));
		json_object_object_add(r, "LOA", json_object_new_int(afb_session_get_loa(req->session, req->api)));
	}
	return r;
}

void
afb_req_common_check_permission(
	struct afb_req_common *req,
	const char *permission,
	void (*callback)(void *, int),
	void *closure
) {
	afb_perm_check_req_async(req, permission, callback, closure);
}

struct has_permission_s
{
	struct afb_sched_lock *schedlock;
	struct afb_req_common *req;
	const char *permision;
	int rc;
};

static
void
has_permission_cb(
	void *closure,
	int status
) {
	struct has_permission_s *hasp = closure;
	hasp->rc = status;
	afb_sched_leave(hasp->schedlock);
}

static
void
has_permission_job_cb(
	int signum,
	void *closure,
	struct afb_sched_lock *schedlock
) {
	struct has_permission_s *hasp = closure;

	if (signum) {
		hasp->rc = EINTR;
		afb_sched_leave(schedlock);
	}
	else {
		hasp->schedlock = schedlock;
		afb_perm_check_req_async(hasp->req, hasp->permision, has_permission_cb, hasp);
	}
}

int
afb_req_common_has_permission(
	struct afb_req_common *req,
	const char *permission
) {
	int rc;
	struct has_permission_s hasp;

	hasp.permision = permission;
	hasp.req = req;
	rc = afb_sched_enter(NULL, 0, has_permission_job_cb, &hasp);
	if (rc == 0)
		rc = hasp.rc;
	return rc;
}

/******************************************************************************/

































/******************************************************************************/
#if WITH_AFB_HOOK

struct json_object *
afb_req_common_json_hookable(
	struct afb_req_common *req
) {
	struct json_object *r = afb_req_common_json(req);
	return afb_hook_req_json(req, r);
}

struct afb_arg
afb_req_common_get_hookable(
	struct afb_req_common *req,
	const char *name
) {
	struct afb_arg r = afb_req_common_get(req, name);
	return afb_hook_req_get(req, name, r);
}

void
afb_req_common_reply_hookable(
	struct afb_req_common *req,
	struct json_object *obj,
	const char *error,
	const char *info
) {
	if (req->hookflags & afb_hook_flag_req_reply)
		afb_hook_req_reply(req, obj, error, info);
	afb_req_common_reply(req, obj, error, info);
}

void
afb_req_common_vreply_hookable(
	struct afb_req_common *req,
	struct json_object *obj,
	const char *error,
	const char *fmt,
	va_list args
) {
	char *info;
	if (req->hookflags & afb_hook_flag_req_reply) {
		if (fmt == NULL || vasprintf(&info, fmt, args) < 0)
			info = NULL;
		afb_req_common_reply_hookable(req, obj, error, info);
		free(info);
	}
	else {
		afb_req_common_vreply(req, obj, error, fmt, args);
	}
}


struct afb_req_common *
afb_req_common_addref_hookable(
	struct afb_req_common *req
) {
	afb_hook_req_addref(req);
	return afb_req_common_addref(req);
}

void
afb_req_common_unref_hookable(
	struct afb_req_common *req
) {
	afb_hook_req_unref(req);
	afb_req_common_unref(req);
}

void
afb_req_common_session_close_hookable(
	struct afb_req_common *req
) {
	afb_hook_req_session_close(req);
	afb_req_common_session_close(req);
}

int
afb_req_common_session_set_LOA_hookable(
	struct afb_req_common *req,
	unsigned level
) {
	int r = afb_req_common_session_set_LOA(req, level);
	return afb_hook_req_session_set_LOA(req, level, r);
}

int
afb_req_common_subscribe_event_x2_hookable(
	struct afb_req_common *req,
	struct afb_event_x2 *event
) {
	int r = afb_req_common_subscribe_event_x2(req, event);
	return afb_hook_req_subscribe(req, event, r);
}

int
afb_req_common_unsubscribe_event_x2_hookable(
	struct afb_req_common *req,
	struct afb_event_x2 *event
) {
	int r = afb_req_common_unsubscribe_event_x2(req, event);
	return afb_hook_req_unsubscribe(req, event, r);
}

void
afb_req_common_vverbose_hookable(
	struct afb_req_common *req,
	int level, const char *file,
	int line,
	const char *func,
	const char *fmt,
	va_list args
) {
	va_list ap;
	va_copy(ap, args);
	afb_req_common_vverbose(req, level, file, line, func, fmt, args);
	afb_hook_req_vverbose(req, level, file, line, func, fmt, ap);
	va_end(ap);
}

int
afb_req_common_has_permission_hookable(
	struct afb_req_common *req,
	const char *permission
) {
	int r = afb_req_common_has_permission(req, permission);
	return afb_hook_req_has_permission(req, permission, r);
}

void *
afb_req_common_cookie_hookable(
	struct afb_req_common *req,
	void *(*maker)(void*),
	void (*freeer)(void*),
	void *closure,
	int replace
) {
	void *result = afb_req_common_cookie(req, maker, freeer, closure, replace);
	return afb_hook_req_context_make(req, replace, maker, freeer, closure, result);
}

struct json_object *
afb_req_common_get_client_info_hookable(
	struct afb_req_common *req
) {
	struct json_object *r = afb_req_common_get_client_info(req);
	return afb_hook_req_get_client_info(req, r);
}

#endif
