/*
 * Copyright (C) 2015-2023 IoT.bzh Company
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
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <json-c/json.h>
#if !defined(JSON_C_TO_STRING_NOSLASHESCAPE)
#define JSON_C_TO_STRING_NOSLASHESCAPE 0
#endif
#include <rp-utils/rp-verbose.h>

#include <afb/afb-auth.h>
#include <afb/afb-session.h>
#include <afb/afb-errno.h>

#include "sys/x-errno.h"
#include "core/afb-apiset.h"
#include "core/afb-auth.h"
#include "core/afb-calls.h"
#include "core/afb-data.h"
#include "core/afb-data-array.h"
#include "core/afb-type-predefined.h"
#include "core/afb-evt.h"
#include "core/afb-cred.h"
#include "core/afb-token.h"
#include "core/afb-hook.h"
#include "core/afb-req-common.h"
#include "core/afb-json-legacy.h"
#include "core/afb-sched.h"
#include "core/afb-session.h"
#include "core/afb-perm.h"
#include "core/afb-permission-text.h"

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

/**
 * Returns a predefined error
 *
 * @param req    the request receiving the error
 * @param status the error status
 * @param arg    an argument to the error
 *
 * @return the value status
 */
static int reply_error(struct afb_req_common *req, int status)
{
	afb_req_common_reply_hookable(req, status, 0, 0);
	return status;
}

/**
 * Returns a predefined error with text
 *
 * @param req    the request receiving the error
 * @param status the error status
 * @param text   an argument to the error
 *
 * @return the value status
 */
static int reply_error_text(struct afb_req_common *req, int status, const char *text)
{
	int rc;
	struct afb_data *data;

	if (!text)
		return reply_error(req, status);
	rc = afb_data_create_raw(&data, &afb_type_predefined_stringz, text, 1 + strlen(text), 0, 0);
	afb_req_common_reply_hookable(req, status, rc >= 0, &data);
	return status;
}

int afb_req_common_reply_out_of_memory_error_hookable(struct afb_req_common *req)
{
	return reply_error(req, AFB_ERRNO_OUT_OF_MEMORY);
}

int afb_req_common_reply_internal_error_hookable(struct afb_req_common *req, int error)
{
	return reply_error(req, AFB_ERRNO_INTERNAL_ERROR);
}

int afb_req_common_reply_unavailable_error_hookable(struct afb_req_common *req)
{
	return reply_error(req, AFB_ERRNO_NOT_AVAILABLE);
}

int afb_req_common_reply_api_unknown_error_hookable(struct afb_req_common *req)
{
	return reply_error(req, AFB_ERRNO_UNKNOWN_API);
}

int afb_req_common_reply_api_bad_state_error_hookable(struct afb_req_common *req)
{
	return reply_error(req, AFB_ERRNO_BAD_API_STATE);
}

int afb_req_common_reply_verb_unknown_error_hookable(struct afb_req_common *req)
{
	return reply_error(req, AFB_ERRNO_UNKNOWN_VERB);
}

int afb_req_common_reply_invalid_token_error_hookable(struct afb_req_common *req)
{
	return reply_error(req, AFB_ERRNO_INVALID_TOKEN);
}

int afb_req_common_reply_insufficient_scope_error_hookable(struct afb_req_common *req, const char *scope)
{
	return reply_error_text(req, AFB_ERRNO_INSUFFICIENT_SCOPE, scope);
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


static
void
set_args(
	unsigned ndata,
	struct afb_data * const data[],
	struct afb_req_common_arg * args
) {
	struct afb_data **dest;

	if (ndata <= REQ_COMMON_NDATA_DEF)
		dest = args->local;
	else {
		dest = malloc(ndata * sizeof *dest);
		if (!dest) {
			RP_ERROR("fail to allocate memory for afb_req_common_arg");
			afb_data_array_unref(ndata - REQ_COMMON_NDATA_DEF, &data[REQ_COMMON_NDATA_DEF]);
			ndata = REQ_COMMON_NDATA_DEF;
			dest = args->local;
		}
	}
	args->ndata = ndata;
	args->data = dest;
	afb_data_array_copy(ndata, data, dest);
}

static
void
clean_args(struct afb_req_common_arg * args){
	if (args->ndata) {
		afb_data_array_unref(args->ndata, args->data);
		if (args->data != args->local)
			free(args->data);
		args->ndata = 0;
	}
}

void
afb_req_common_init(
	struct afb_req_common *req,
	const struct afb_req_common_query_itf *queryitf,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	struct afb_data * const params[]
) {
	memset(req, 0, sizeof *req);
	req->refcount = 1;
	req->queryitf = queryitf;
	req->apiname = apiname;
	req->verbname = verbname;
	set_args(nparams, params, &req->params);
}

void
afb_req_common_set_params(
	struct afb_req_common *req,
	unsigned nparams,
	struct afb_data * const params[]
) {
	clean_args(&req->params);
	set_args(nparams, params, &req->params);
}

void
afb_req_common_prepare_forwarding(
	struct afb_req_common *req,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	struct afb_data * const params[]
) {
	req->apiname = apiname;
	req->verbname = verbname;
	clean_args(&req->params);
	set_args(nparams, params, &req->params);
}

void
afb_req_common_set_session(
	struct afb_req_common *req,
	struct afb_session *session
) {
	struct afb_session *osession;

	osession = req->session;
	req->session = afb_session_touch(afb_session_addref(session));
	afb_session_unref(osession);
}

int
afb_req_common_set_session_string(
	struct afb_req_common *req,
	const char *string
) {
	struct afb_session *osession;
	int rc;

	osession = req->session;
	rc = afb_session_get(&req->session, string, AFB_SESSION_TIMEOUT_DEFAULT, NULL);
	afb_session_touch(req->session);
	afb_session_unref(osession);
	return rc;
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

int
afb_req_common_set_token_string(
	struct afb_req_common *req,
	const char *string
) {
	struct afb_token *otoken;
	int rc;

	otoken = req->token;
	rc = afb_token_get(&req->token, string);
	afb_token_unref(otoken);
	return rc;
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
	clean_args(&req->params);
	if (req->session && req->closing)
		afb_session_drop_key(req->session, req->api);
	afb_req_common_set_session(req, NULL);
	afb_req_common_set_token(req, NULL);
#if WITH_CRED
	afb_req_common_set_cred(req, NULL);
#endif
}

static
int
get_interface(
	struct afb_req_common *req,
	int id,
	const char *name,
	void **result
) {
	int rc;
	void *itf;

	rc = req->queryitf->interface ? req->queryitf->interface(req, id, name, &itf) : X_ENOENT;
	if (result)
		*result = rc >= 0 ? itf : NULL;
	return rc;
}

int
afb_req_common_interface_by_id(
	struct afb_req_common *req,
	int id,
	void **result
) {
	return get_interface(req, id, NULL, result);
}

int
afb_req_common_interface_by_name(
	struct afb_req_common *req,
	const char *name,
	void **result
) {
	return get_interface(req, 0, name, result);
}

int
afb_req_common_interface_by_id_hookable(
	struct afb_req_common *req,
	int id,
	void **result
) {
	int rc = afb_req_common_interface_by_id(req, id, result);
#if WITH_AFB_HOOK
	if (req->hookflags & afb_hook_flag_req_interface)
		rc = afb_hook_req_interface_by_id(req, id, *result, rc);
#endif
	return rc;
}

int
afb_req_common_interface_by_name_hookable(
	struct afb_req_common *req,
	const char *name,
	void **result
) {
	int rc = afb_req_common_interface_by_name(req, name, result);
#if WITH_AFB_HOOK
	if (req->hookflags & afb_hook_flag_req_interface)
		rc = afb_hook_req_interface_by_name(req, name, *result, rc);
#endif
	return rc;
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
		RP_ERROR("received signal %d (%s) when processing request", signum, strsignal(signum));
		afb_req_common_reply_internal_error_hookable(req, X_EINTR);
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
	rc = afb_sched_post_job(req->api->group, 0, timeout, req_common_process_async_cb, req, Afb_Sched_Mode_Normal);
	if (rc < 0) {
		/* TODO: allows or not to proccess it directly as when no threading? (see above) */
		RP_ERROR("can't process job with threads: %s", strerror(-rc));
		afb_req_common_reply_internal_error_hookable(req, rc);
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
		afb_req_common_reply_api_unknown_error_hookable(req);
	}
	else {
		afb_req_common_reply_api_bad_state_error_hookable(req);
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
		afb_req_common_reply_insufficient_scope_error_hookable(req, NULL);
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
		RP_ERROR("Can't import on behalf credentials: %m");
	}
	else if (afb_req_common_async_push2(req, apiset, cred) == 0) {
		RP_ERROR("internal error when importing creds");
		afb_cred_unref(cred);
		rc = X_EOVERFLOW;
	}
	else {
		afb_perm_check_req_async(req, afb_permission_on_behalf_credential, process_on_behalf_cb, req);
		return;
	}
	afb_req_common_reply_internal_error_hookable(req, rc);
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
	unsigned value
) {
	return value <= 0 || afb_session_get_loa(req->session, req->api) >= (int)value;
}

/******************************************************************************/

static void check_and_set_final(
	struct afb_req_common *req,
	int status
) {
	if (status <= 0) {
		/* TODO FIXME report consistent error: scope/invalid... */
		afb_req_common_reply_insufficient_scope_error_hookable(req, NULL);
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
		afb_perm_check_req_async(req, auth->text, check_and_set_auth_async_cb, req);
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
		afb_req_common_reply_internal_error_hookable(req, X_EBUSY);
		callback(closure, X_EBUSY);
	}
	else if (!sessionflags) {
		/* no flag case: just check auth */
		check_and_set_auth_async(req, auth);
	}
	else {
		/* check of session flags */
		if (sessionflags & AFB_SESSION_CLOSE)
			req->closing = 1;
		loa = (int)(sessionflags & AFB_SESSION_LOA_MASK);
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

struct afb_req_common *
afb_req_common_addref_hookable(
	struct afb_req_common *req
)
#if !WITH_AFB_HOOK
	__attribute__((alias("afb_req_common_addref")));
#else
{
	if (req->hookflags & afb_hook_flag_req_addref)
		afb_hook_req_addref(req);
	return afb_req_common_addref(req);
}
#endif

void
afb_req_common_unref(
	struct afb_req_common *req
) {
	if (req && !__atomic_sub_fetch(&req->refcount, 1, __ATOMIC_RELAXED)) {
		if (!req->replied) {
			reply_error(req, AFB_ERRNO_NO_REPLY);
			if (__atomic_load_n(&req->refcount, __ATOMIC_RELAXED)) {
				/* replying may have the side effect to re-increment
				** the reference count, showing a delayed usage of
				** of the request for handling it's reply */
				return;
			}
		}
#if WITH_AFB_HOOK
		if (req->hookflags & afb_hook_flag_req_end)
			afb_hook_req_end(req);
#endif
		req->queryitf->unref(req);
	}
}

void
afb_req_common_unref_hookable(
	struct afb_req_common *req
)
#if !WITH_AFB_HOOK
	__attribute__((alias("afb_req_common_unref")));
#else
{
	if (req->hookflags & afb_hook_flag_req_unref)
		afb_hook_req_unref(req);
	afb_req_common_unref(req);
}
#endif

/******************************************************************************/
void
afb_req_common_vverbose_hookable(
	struct afb_req_common *req,
	int level, const char *file,
	int line,
	const char *func,
	const char *fmt,
	va_list args
) {
	char *p;
#if WITH_AFB_HOOK
	va_list ap;
	if (req->hookflags & afb_hook_flag_req_vverbose) {
		va_copy(ap, args);
		afb_hook_req_vverbose(req, level, file, line, func, fmt, ap);
		va_end(ap);
	}
#endif
	if (!fmt || vasprintf(&p, fmt, args) < 0)
		rp_vverbose(level, file, line, func, fmt, args);
	else {
		rp_verbose(level, file, line, func, "[REQ/API %s] %s", req->apiname, p);
		free(p);
	}
}


/******************************************************************************/

#if WITH_REPLY_JOB
static
void
reply_job(
	int signum,
	void *closure
) {
	struct afb_req_common *req = closure;

	if (!signum)
		req->queryitf->reply(req, req->status, req->replies.ndata, req->replies.data);
	clean_args(&req->replies);
	afb_req_common_unref(req);
}

static inline
void
do_reply(
	struct afb_req_common *req,
	int status,
	unsigned nreplies,
	struct afb_data * const replies[]
) {
	req->status = status;
	set_args(nreplies, replies, &req->replies);

	afb_req_common_addref(req);
	if (afb_sched_post_job(NULL, 0, 0, reply_job, req, Afb_Sched_Mode_Normal) < 0)
		reply_job(0, req);
}

#else

static inline
void
do_reply(
	struct afb_req_common *req,
	int status,
	unsigned nreplies,
	struct afb_data * const replies[]
) {
	req->queryitf->reply(req, status, nreplies, replies);
	afb_data_array_unref(nreplies, replies);
}

#endif

/**
 * Emits the reply to the request
 *
 * @param req       the common request to be replied
 * @param status    the integer status of the reply
 * @param nreplies  the count of data in the array replies
 * @param replies   the data of the reply (can be NULL if nreplies is zero)
 */
void
afb_req_common_reply_hookable(
	struct afb_req_common *req,
	int status,
	unsigned nreplies,
	struct afb_data * const replies[]
) {
#if WITH_AFB_HOOK
	if (req->hookflags & afb_hook_flag_req_reply)
		afb_hook_req_reply(req, status, nreplies, replies);
#endif
	if (req->replied) {
		/* it is an error to reply more than one time */
		RP_ERROR("reply called more than one time!!");
		afb_data_array_unref(nreplies, replies);
	}
	else {
		/* first reply, so emit it */
		req->replied = 1;
		do_reply(req, status, nreplies, replies);
	}
}

/******************************************************************************/

int
afb_req_common_subscribe(
	struct afb_req_common *req,
	struct afb_evt *evt
) {
	if (req->replied) {
		RP_ERROR("request replied, subscription impossible");
		return X_EINVAL;
	}
	if (req->queryitf->subscribe)
		return req->queryitf->subscribe(req, evt);
	RP_ERROR("no event listener, subscription impossible");
	return X_ENOTSUP;
}

int
afb_req_common_subscribe_hookable(
	struct afb_req_common *req,
	struct afb_evt *evt
) {
	int r = afb_req_common_subscribe(req, evt);
#if WITH_AFB_HOOK
	if (req->hookflags & afb_hook_flag_req_subscribe)
		r = afb_hook_req_subscribe(req, evt, r);
#endif
	return r;
}

int
afb_req_common_unsubscribe(
	struct afb_req_common *req,
	struct afb_evt *evt
) {
	if (req->replied) {
		RP_ERROR("request replied, unsubscription impossible");
		return X_EINVAL;
	}
	if (req->queryitf->unsubscribe)
		return req->queryitf->unsubscribe(req, evt);
	RP_ERROR("no event listener, unsubscription impossible");
	return X_ENOTSUP;
}

int
afb_req_common_unsubscribe_hookable(
	struct afb_req_common *req,
	struct afb_evt *evt
) {
	int r = afb_req_common_unsubscribe(req, evt);
#if WITH_AFB_HOOK
	if (req->hookflags & afb_hook_flag_req_unsubscribe)
		r = afb_hook_req_unsubscribe(req, evt, r);
#endif
	return r;
}

/******************************************************************************/

int
afb_req_common_param_convert(
	struct afb_req_common *req,
	unsigned index,
	struct afb_type *type,
	struct afb_data **result
) {
	int rc;
	struct afb_data *before, *after;

	after = NULL;
	if (index >= req->params.ndata)
		rc = X_EINVAL;
	else {
		before = req->params.data[index];
		rc = afb_data_convert(before, type, &after);
		if (rc >= 0) {
			req->params.data[index] = after;
			afb_data_unref(before);
		}
	}
	if (result != NULL)
		*result = after;
	return rc;
}

/******************************************************************************/

/**
 * intermediate structure used for afb_session_cookie_init_basic
 */
struct memo_cookie_init_basic {
	void *(*makecb)(void *closure); /**< memorize the creation callback */
	void (*freecb)(void *item);     /**< memorize the destruction callback */
	void *closure;                  /**< closure of the creation callback */
};

/**
 * internal function for implementing afb_session_cookie_init_basic
 */
static int cookie_init_basic(void *closure, void **value, void (**freecb)(void*), void **freeclo)
{
	struct memo_cookie_init_basic *mib = closure;
	void *val;

	if (!mib->makecb)
		val = mib->closure;
	else {
		val = mib->makecb(mib->closure);
		if (val == NULL)
			return X_ENOMEM;
	}
	*value = *freeclo = val;
	*freecb = mib->freecb;
	return 0;
}

void *
afb_req_common_cookie_hookable(
	struct afb_req_common *req,
	void *(*maker)(void*),
	void (*freeer)(void*),
	void *closure,
	int replace
) {
	void *r;
	struct memo_cookie_init_basic mib;

	if (replace) {
		r = maker ? maker(closure) : closure;
		afb_session_cookie_set(req->session, req->api, r, freeer, r);
	}
	else {

		mib.makecb = maker;
		mib.freecb = freeer;
		mib.closure = closure;

		afb_session_cookie_getinit(req->session, req->api, &r, cookie_init_basic, &mib);
	}
#if WITH_AFB_HOOK
	if (req->hookflags & afb_hook_flag_req_context_make)
		r = afb_hook_req_context_make(req, replace, maker, freeer, closure, r);
#endif
	return r;
}

/* set the cookie of the api getting the request */
int
afb_req_common_cookie_set_hookable(
	struct afb_req_common *req,
	void *value,
	void (*freecb)(void*),
	void *freeclo
) {
	int rc = afb_session_cookie_set(req->session, req->api, value, freecb, freeclo);
#if WITH_AFB_HOOK
	if (req->hookflags & afb_hook_flag_req_context_set)
		rc = afb_hook_req_context_set(req, value, freecb, freeclo, rc);
#endif
	return rc;
}


/* get the cookie of the api getting the request */
int
afb_req_common_cookie_get_hookable(
	struct afb_req_common *req,
	void **value
) {
	int rc = afb_session_cookie_get(req->session, req->api, value);
#if WITH_AFB_HOOK
	if (req->hookflags & afb_hook_flag_req_context_get)
		rc = afb_hook_req_context_get(req, *value, rc);
#endif
	return rc;
}


/* get the cookie of the api getting the request */
int
afb_req_common_cookie_getinit_hookable(
	struct afb_req_common *req,
	void **value,
	int (*initcb)(void *closure, void **value, void (**freecb)(void*), void **freeclo),
	void *closure
) {
	int rc = afb_session_cookie_getinit(req->session, req->api, value, initcb, closure);
#if WITH_AFB_HOOK
	if (req->hookflags & afb_hook_flag_req_context_getinit)
		rc = afb_hook_req_context_getinit(req, *value, initcb, closure, rc);
#endif
	return rc;
}

/* set the cookie of the api getting the request */
int
afb_req_common_cookie_drop_hookable(
	struct afb_req_common *req
) {
	int rc = afb_session_cookie_delete(req->session, req->api);
#if WITH_AFB_HOOK
	if (req->hookflags & afb_hook_flag_req_context_drop)
		rc = afb_hook_req_context_drop(req, rc);
#endif
	return rc;
}

int
afb_req_common_session_set_LOA_hookable(
	struct afb_req_common *req,
	unsigned level
) {
	int r = afb_session_set_loa(req->session, req->api, (int)level);
#if WITH_AFB_HOOK
	if (req->hookflags & afb_hook_flag_req_session_set_LOA)
		r = afb_hook_req_session_set_LOA(req, level, r);
#endif
	return r;
}

unsigned
afb_req_common_session_get_LOA_hookable(
	struct afb_req_common *req
) {
	int rc = afb_session_get_loa(req->session, req->api);
	unsigned r = rc < 0 ? 0 : (unsigned)rc;
#if WITH_AFB_HOOK
	if (req->hookflags & afb_hook_flag_req_session_get_LOA)
		r = afb_hook_req_session_get_LOA(req, r);
#endif
	return r;
}

void
afb_req_common_session_close_hookable(
	struct afb_req_common *req
) {
#if WITH_AFB_HOOK
	if (req->hookflags & afb_hook_flag_req_session_close)
		afb_hook_req_session_close(req);
#endif
	req->closing = 1;
}

struct json_object *
afb_req_common_get_client_info_hookable(
	struct afb_req_common *req
) {
	struct json_object *r = json_object_new_object();
#if WITH_CRED
	struct afb_cred *cred = req->credentials;
	if (cred && cred->id) {
		json_object_object_add(r, "uid", json_object_new_int64((int64_t)cred->uid));
		json_object_object_add(r, "gid", json_object_new_int64((int64_t)cred->gid));
		json_object_object_add(r, "pid", json_object_new_int64((int64_t)cred->pid));
		json_object_object_add(r, "user", json_object_new_string(cred->user));
		json_object_object_add(r, "label", json_object_new_string(cred->label));
		json_object_object_add(r, "id", json_object_new_string(cred->id));
	}
#endif
	if (req->session) {
		json_object_object_add(r, "uuid", json_object_new_string(afb_session_uuid(req->session)?:""));
		json_object_object_add(r, "LOA", json_object_new_int((int)afb_session_get_loa(req->session, req->api)));
	}
#if WITH_AFB_HOOK
	if (req->hookflags & afb_hook_flag_req_get_client_info)
		r = afb_hook_req_get_client_info(req, r);
#endif
	return r;
}

static
void
check_permission_hookable_reply(
	struct afb_req_common *req,
	int status,
	void (*callback)(void*,int,void*,void*),
	void *closure1,
	void *closure2,
	void *closure3,
	const char *permission
) {
#if WITH_AFB_HOOK
	if (req->hookflags & afb_hook_flag_req_has_permission) {
		afb_hook_req_has_permission(req, permission, status);
	}
#endif
	callback(closure1, status, closure2, closure3);
	afb_req_common_unref(req);
}

struct ck_perm_s
{
	struct afb_req_common *req;
	const char *permission;
	void (*callback)(void*,int,void*,void*);
	void *closure1;
	void *closure2;
	void *closure3;
};

static
void
check_permission_hookable_cb(
	void *closure,
	int status
) {
	struct ck_perm_s *cps = closure;

	check_permission_hookable_reply(cps->req, status, cps->callback,
		cps->closure1, cps->closure2, cps->closure3, cps->permission);
	free(cps);
}

void
afb_req_common_check_permission_hookable(
	struct afb_req_common *req,
	const char *permission,
	void (*callback)(void*,int,void*,void*),
	void *closure1,
	void *closure2,
	void *closure3
) {
	struct ck_perm_s *cps;

	afb_req_common_addref(req);
	cps = malloc(sizeof *cps);
	if (!cps)
		check_permission_hookable_reply(req, X_ENOMEM, callback,
			closure1, closure2, closure3,permission);
	else {
		cps->req = req;
		cps->permission = permission;
		cps->callback = callback;
		cps->closure1 = closure1;
		cps->closure2 = closure2;
		cps->closure3 = closure3;
		afb_perm_check_req_async(req, permission,
			check_permission_hookable_cb, cps);
	}
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
		hasp->rc = X_EINTR;
		afb_sched_leave(schedlock);
	}
	else {
		hasp->schedlock = schedlock;
		afb_perm_check_req_async(hasp->req, hasp->permision, has_permission_cb, hasp);
	}
}

int
afb_req_common_has_permission_hookable(
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
#if WITH_AFB_HOOK
	if (req->hookflags & afb_hook_flag_req_has_permission)
		rc = afb_hook_req_has_permission(req, permission, rc);
#endif
	return rc;
}
