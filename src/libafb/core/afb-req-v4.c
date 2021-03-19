/*
 * Copyright (C) 2015-2021 IoT.bzh Company
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
#include <stdint.h>
#include <stdarg.h>

#include "afb-v4-itf.h"

#include "sys/x-errno.h"
#include "core/afb-apiset.h"
#include "core/afb-api-common.h"
#include "core/afb-api-v4.h"
#include "core/afb-auth.h"
#include "core/afb-calls.h"
#include "core/afb-data.h"
#include "core/afb-evt.h"
#include "core/afb-cred.h"
#include "core/afb-hook.h"
#include "core/afb-req-common.h"
#include "core/afb-req-v4.h"
#include "core/afb-error-text.h"
#include "core/afb-sched.h"

#include "sys/verbose.h"

#include "containerof.h"
#include <stdarg.h>
#include <stddef.h>

/**
 * Internal data for requests V4
 */
struct afb_req_v4
{
	/** the request */
	struct afb_req_common *comreq;

	/** the api */
	struct afb_api_v4 *api;

	/** the verb */
	const struct afb_verb_v4 *verb;

	/** hook flag */
	unsigned hookflags;

	/** count of references */
	uint16_t refcount;

	/** logmask */
	int16_t logmask;
};


/******************************************************************************/

struct afb_req_v4 *afb_req_v4_addref(struct afb_req_v4 *reqv4)
{
	__atomic_add_fetch(&reqv4->refcount, 1, __ATOMIC_RELAXED);
	return reqv4;
}

void afb_req_v4_unref(struct afb_req_v4 *reqv4)
{
	struct afb_req_common *comreq;

	if (!__atomic_sub_fetch(&reqv4->refcount, 1, __ATOMIC_RELAXED)) {
		comreq = reqv4->comreq;
		free(reqv4);
		afb_req_common_unref(comreq);
	}
}

/******************************************************************************/

struct afb_req_v4 *afb_req_v4_addref_hookable(struct afb_req_v4 *reqv4)
#if !WITH_AFB_HOOK
	__attribute__((alias("afb_req_v4_addref")));
#else
{
	if (reqv4->hookflags & afb_hook_flag_req_addref)
		afb_hook_req_addref(reqv4->comreq);

	return afb_req_v4_addref(reqv4);
}
#endif

void afb_req_v4_unref_hookable(struct afb_req_v4 *reqv4)
#if !WITH_AFB_HOOK
	__attribute__((alias("afb_req_v4_unref")));
#else
{
	if (reqv4->hookflags & afb_hook_flag_req_unref)
		afb_hook_req_unref(reqv4->comreq);

	afb_req_v4_unref(reqv4);
}
#endif

void afb_req_v4_vverbose_hookable(
	struct afb_req_v4 *reqv4,
	int level,
	const char *file,
	int line,
	const char *func,
	const char *fmt,
	va_list args
) {
	afb_req_common_vverbose_hookable(reqv4->comreq, level, file, line, func, fmt, args);
}

void
afb_req_v4_verbose(
	struct afb_req_v4 *reqv4,
	int level,
	const char *file,
	int line,
	const char * func,
	const char *fmt,
	...
) {
	va_list args;
	va_start(args, fmt);
	afb_req_v4_vverbose_hookable(reqv4, level, file, line, func, fmt, args);
	va_end(args);
}

void *
afb_req_v4_cookie_hookable(
	struct afb_req_v4 *reqv4,
	int replace,
	void *(*create_value)(void*),
	void (*free_value)(void*),
	void *create_closure
) {
	return afb_req_common_cookie_hookable(reqv4->comreq, create_value, free_value, create_closure, replace);
}

int
afb_req_v4_session_set_LOA_hookable(
	struct afb_req_v4 *reqv4,
	unsigned level
) {
	return afb_req_common_session_set_LOA_hookable(reqv4->comreq, level);
}

unsigned
afb_req_v4_session_get_LOA_hookable(
	struct afb_req_v4 *reqv4
) {
	return afb_req_common_session_get_LOA_hookable(reqv4->comreq);
}

void
afb_req_v4_session_close_hookable(
	struct afb_req_v4 *reqv4
) {
	afb_req_common_session_close_hookable(reqv4->comreq);
}

struct json_object *
afb_req_v4_get_client_info_hookable(
	struct afb_req_v4 *reqv4
) {
	return afb_req_common_get_client_info_hookable(reqv4->comreq);
}

int
afb_req_v4_logmask(
	struct afb_req_v4 *reqv4
) {
	return reqv4->logmask;
}

struct afb_api_v4 *
afb_req_v4_api(
	struct afb_req_v4 *reqv4
) {
	return reqv4->api;
}

void *
afb_req_v4_vcbdata(
	struct afb_req_v4 *reqv4
) {
	return reqv4->verb->vcbdata;
}

const char *
afb_req_v4_called_api(
	struct afb_req_v4 *reqv4
) {
	return reqv4->comreq->apiname;
}

const char *
afb_req_v4_called_verb(
	struct afb_req_v4 *reqv4
) {
	return reqv4->comreq->verbname;
}

int
afb_req_v4_subscribe_hookable(
	struct afb_req_v4 *reqv4,
	struct afb_evt *event
) {
	return afb_req_common_subscribe_hookable(reqv4->comreq, event);
}

int
afb_req_v4_unsubscribe_hookable(
	struct afb_req_v4 *reqv4,
	struct afb_evt *event
) {
	return afb_req_common_unsubscribe_hookable(reqv4->comreq, event);
}

static
void
check_permission_cb(
	void *closure1,
	int status,
	void *closure2,
	void *closure3
) {
	struct afb_req_v4 *reqv4 = closure2;
	void (*callback)(void*,int,struct afb_req_v4*) = closure3;

	callback(closure1, status, reqv4);
	afb_req_v4_unref(reqv4);
}

void
afb_req_v4_check_permission_hookable(
	struct afb_req_v4 *reqv4,
	const char *permission,
	void (*callback)(void*,int,struct afb_req_v4*),
	void *closure
) {
	afb_req_v4_addref(reqv4);
	afb_req_common_check_permission_hookable(reqv4->comreq, permission,
		check_permission_cb, closure, reqv4, callback);
}

unsigned
afb_req_v4_parameters(
	struct afb_req_v4 *reqv4,
	struct afb_data * const **params
) {
	if (params)
		*params = reqv4->comreq->params.data;
	return reqv4->comreq->params.ndata;
}

void
afb_req_v4_reply_hookable(
	struct afb_req_v4 *reqv4,
	int status,
	unsigned nparams,
	struct afb_data * const params[]
) {
	afb_req_common_reply_hookable(reqv4->comreq, status, nparams, params);
}

static
void
subcall_cb(
	void *closure1,
	void *closure2,
	void *closure3,
	int status,
	unsigned nreplies,
	struct afb_data * const replies[]
) {
	struct afb_req_v4 *reqv4 = closure1;
	void (*callback)(void*, int, unsigned, struct afb_data * const[], struct afb_req_v4*) = closure2;
	void *closure = closure3;
	callback(closure, status, nreplies, replies, reqv4);
	afb_req_v4_unref(reqv4);
}

void
afb_req_v4_subcall_hookable(
	struct afb_req_v4 *reqv4,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	struct afb_data * const params[],
	int flags,
	void (*callback)(void *closure, int status, unsigned, struct afb_data * const[], struct afb_req_v4 *reqv4),
	void *closure
) {
	afb_req_v4_addref(reqv4);
#if WITH_AFB_HOOK
	if (reqv4->hookflags & afb_hook_flag_req_subcall) {
		afb_calls_subcall_hooking(afb_api_v4_get_api_common(reqv4->api),
					apiname, verbname, nparams, params,
					subcall_cb, reqv4, callback, closure, reqv4->comreq, flags);
		return;
	}
#endif
	afb_calls_subcall
		(afb_api_v4_get_api_common(reqv4->api),
			apiname, verbname, nparams, params,
			subcall_cb, reqv4, callback, closure, reqv4->comreq, flags);
}

int
afb_req_v4_subcall_sync_hookable(
	struct afb_req_v4 *reqv4,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	struct afb_data * const params[],
	int flags,
	int *status,
	unsigned *nreplies,
	struct afb_data *replies[]
) {
#if WITH_AFB_HOOK
	if (reqv4->hookflags & afb_hook_flag_req_subcallsync) {
		return afb_calls_subcall_sync_hooking(afb_api_v4_get_api_common(reqv4->api),
					apiname, verbname, nparams, params,
					status, nreplies, replies, reqv4->comreq, flags);
	}
#endif
	return afb_calls_subcall_sync(afb_api_v4_get_api_common(reqv4->api),
			apiname, verbname, nparams, params,
			status, nreplies, replies, reqv4->comreq, flags);
}

/******************************************************************************/

static void call_checked_v4(void *closure, int status)
{
	struct afb_req_v4 *reqv4 = closure;

	if (status > 0)
		reqv4->verb->callback(
			reqv4,
			reqv4->comreq->params.ndata,
			reqv4->comreq->params.data
			);
	afb_req_v4_unref(reqv4);
}

void afb_req_v4_process(
	struct afb_req_common *comreq,
	struct afb_api_v4 *api,
	const struct afb_verb_v4 *verb
) {
	struct afb_req_v4 *reqv4;

	reqv4 = malloc(sizeof *reqv4);
	if (reqv4 == NULL) {
		afb_req_common_reply_out_of_memory_error_hookable(comreq);
	}
	else {
		reqv4->comreq = afb_req_common_addref(comreq);
		reqv4->api = api;
		reqv4->verb = verb;
		reqv4->logmask = (int16_t)afb_api_v4_logmask(api);
#if WITH_AFB_HOOK
		reqv4->hookflags = comreq->hookflags;
#endif
		reqv4->refcount = 1;
		afb_req_common_check_and_set_session_async(comreq, verb->auth, verb->session, call_checked_v4, reqv4);
	}
}
