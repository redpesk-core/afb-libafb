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

#include <afb/afb-binding-v4.h>
#include <afb/afb-req-x4.h>

#include "sys/x-errno.h"
#include "core/afb-apiset.h"
#include "core/afb-api-common.h"
#include "core/afb-api-v4.h"
#include "core/afb-auth.h"
#include "core/afb-calls.h"
#include "core/afb-data.h"
#include "core/afb-params.h"
#include "core/afb-evt.h"
#include "core/afb-cred.h"
#include "core/afb-hook.h"
#include "core/afb-req-common.h"
#include "core/afb-req-v4.h"
#include "core/afb-error-text.h"
#include "core/afb-jobs.h"
#include "core/afb-sched.h"

#include "sys/verbose.h"

#include "containerof.h"
#include <stdarg.h>
#include <stddef.h>

#include <afb/afb-req-x4-itf.h>

/**
 * Internal data for requests V4
 */
struct afb_req_v4
{
	/** exported x4 */
	struct afb_req_x4 x4;

	/** the request */
	struct afb_req_common *comreq;

	/** the api */
	struct afb_api_v4 *api;

	/** the verb */
	const struct afb_verb_v4 *verb;

	/** count of references */
	uint16_t refcount;
};

/******************************************************************************/

static inline struct afb_req_v4 *req_v4_from_req_x4(afb_req_x4_t req)
{
	return containerof(struct afb_req_v4, x4, req);
}

static inline afb_req_x4_t req_v4_to_req_x4(struct afb_req_v4 *req)
{
	return &req->x4;
}

/******************************************************************************/

#define CLOSURE_T                       afb_req_x4_t
#define CLOSURE_TO_REQ_COMMON(closure)  (req_v4_from_req_x4(closure)->comreq)

#include "afb-req-common.inc"

#undef CLOSURE_TO_REQ_COMMON
#undef CLOSURE_T

/******************************************************************************/

inline struct afb_req_v4 *afb_req_v4_addref(struct afb_req_v4 *req)
{
	__atomic_add_fetch(&req->refcount, 1, __ATOMIC_RELAXED);
	return req;
}

inline void afb_req_v4_unref(struct afb_req_v4 *req)
{
	struct afb_req_common *comreq;

	if (!__atomic_sub_fetch(&req->refcount, 1, __ATOMIC_RELAXED)) {
		comreq = req->comreq;
		free(req);
		afb_req_common_unref(comreq);
	}
}

/******************************************************************************/

static
afb_req_x4_t
x4_req_addref(
	afb_req_x4_t reqx4
) {
	struct afb_req_v4 *req = req_v4_from_req_x4(reqx4);
	return req_v4_to_req_x4(afb_req_v4_addref(req));
}

static
void
x4_req_unref(
	afb_req_x4_t reqx4
) {
	struct afb_req_v4 *req = req_v4_from_req_x4(reqx4);
	afb_req_v4_unref(req);
}

static
afb_api_x4_t
x4_req_api(
	afb_req_x4_t reqx4
) {
	struct afb_req_v4 *req = req_v4_from_req_x4(reqx4);
	return afb_api_v4_get_api_x4(req->api);
}

static
void *
x4_req_vcbdata(
	afb_req_x4_t reqx4
) {
	struct afb_req_v4 *req = req_v4_from_req_x4(reqx4);
	return req->verb->vcbdata;
}

static
const char *
x4_req_called_api(
	afb_req_x4_t reqx4
) {
	struct afb_req_v4 *req = req_v4_from_req_x4(reqx4);
	return req->comreq->apiname;
}

static
const char *
x4_req_called_verb(
	afb_req_x4_t reqx4
) {
	struct afb_req_v4 *req = req_v4_from_req_x4(reqx4);
	return req->comreq->verbname;
}

static
int
x4_req_subscribe(
	afb_req_x4_t reqx4,
	afb_event_x4_t event
) {
	struct afb_req_common *comreq = req_v4_from_req_x4(reqx4)->comreq;
	struct afb_evt *evt = afb_evt_of_x4(event);
	return afb_req_common_subscribe(comreq, evt);
}

static
int
x4_req_unsubscribe(
	afb_req_x4_t reqx4,
	afb_event_x4_t event
) {
	struct afb_req_common *comreq = req_v4_from_req_x4(reqx4)->comreq;
	struct afb_evt *evt = afb_evt_of_x4(event);
	return afb_req_common_unsubscribe(comreq, evt);
}

static
void
check_permission_status_cb(
	void *closure,
	int status
) {
	struct afb_req_v4 *req = closure;
	void *clo;
	void (*cb)(void*,int,afb_req_x4_t);

	clo = afb_req_common_async_pop(req->comreq);
	cb = afb_req_common_async_pop(req->comreq);
	cb(clo, status, req_v4_to_req_x4(req));
}

static
void
x4_req_check_permission(
	afb_req_x4_t reqx4,
	const char *permission,
	void (*callback)(void*,int,afb_req_x4_t),
	void *closure
) {
	struct afb_req_v4 *req = req_v4_from_req_x4(reqx4);
	if (afb_req_common_async_push2(req->comreq, callback, closure))
		afb_req_common_check_permission(req->comreq, permission, check_permission_status_cb, req);
	else
		callback(closure, X_EBUSY, reqx4);
}

static
int
x4_req_new_data_set(
	afb_req_x4_t req,
	afb_data_x4_t *data,
	afb_type_x4_t type,
	const void *pointer,
	size_t size,
	void (*dispose)(void*),
	void *closure
) {
	return afb_data_x4_create_set_x4(data, type, pointer, size, dispose, closure);
}

static
int
x4_req_new_data_copy(
	afb_req_x4_t req,
	afb_data_x4_t *data,
	afb_type_x4_t type,
	const void *pointer,
	size_t size
) {
	return afb_data_x4_create_copy_x4(data, type, pointer, size);
}

static
int
x4_req_new_data_alloc(
	afb_req_x4_t req,
	afb_data_x4_t *data,
	afb_type_x4_t type,
	void **pointer,
	size_t size,
	int zeroes
) {
	return afb_data_x4_create_alloc_x4(data, type, pointer, size, zeroes);
}


static
unsigned
x4_req_parameters(
	afb_req_x4_t reqx4,
	const struct afb_data_x4 * const **params
) {
	struct afb_req_v4 *req = req_v4_from_req_x4(reqx4);
	*params = req->comreq->params;
	return req->comreq->nparams;

}

static
void
x4_req_reply(
	afb_req_x4_t reqx4,
	int status,
	unsigned nparams,
	const struct afb_data_x4 * const *params
) {
	struct afb_req_common *comreq = req_v4_from_req_x4(reqx4)->comreq;
	afb_req_common_reply(comreq, status, nparams, params);
}

static
void
subcall_cb(
	void *closure1,
	void *closure2,
	void *closure3,
	int status,
	unsigned nreplies,
	const struct afb_data_x4 * const *replies
) {
	struct afb_req_v4 *req = closure1;
	void (*callback)(void*, int, unsigned, const struct afb_data_x4 * const *, afb_req_x4_t) = closure2;
	void *closure = closure3;
	callback(closure, status, nreplies, replies, req_v4_to_req_x4(req));
	afb_req_v4_unref(req);
}

static
void
x4_req_subcall(
	afb_req_x4_t reqx4,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	const struct afb_data_x4 * const *params,
	int flags,
	void (*callback)(void *closure, int status, unsigned, const struct afb_data_x4 * const *, afb_req_x4_t req),
	void *closure
) {
	struct afb_req_v4 *req = req_v4_from_req_x4(reqx4);
	afb_req_v4_addref(req);
	afb_calls_subcall_x4(afb_api_v4_get_api_common(req->api),
			apiname, verbname, nparams, params,
			subcall_cb, req, callback, closure, req->comreq, flags);
}

static
int
x4_req_subcall_sync(
	afb_req_x4_t reqx4,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	const struct afb_data_x4 * const *params,
	int flags,
	int *status,
	unsigned *nreplies,
	const struct afb_data_x4 **replies
) {
	struct afb_req_v4 *req = req_v4_from_req_x4(reqx4);

	return afb_calls_subcall_sync_x4(afb_api_v4_get_api_common(req->api),
			apiname, verbname, nparams, params,
			status, nreplies, replies, req->comreq, flags);
}

/******************************************************************************/

const struct afb_req_x4_itf req_v4_itf = {
	.addref = x4_req_addref,
	.unref = x4_req_unref,
	.api = x4_req_api,
	.vcbdata = x4_req_vcbdata,
	.called_api = x4_req_called_api,
	.called_verb = x4_req_called_verb,
	.session_close = common_req_session_close,
	.session_set_LOA = common_req_session_set_LOA,
	.vverbose = common_req_vverbose,
	.cookie = common_req_cookie,
	.subscribe = x4_req_subscribe,
	.unsubscribe = x4_req_unsubscribe,
	.get_client_info = common_req_get_client_info,
	.check_permission = x4_req_check_permission,
	.new_data_set = x4_req_new_data_set,
	.new_data_copy = x4_req_new_data_copy,
	.new_data_alloc = x4_req_new_data_alloc,
	.parameters = x4_req_parameters,
	.reply = x4_req_reply,
	.subcall = x4_req_subcall,
	.subcall_sync = x4_req_subcall_sync,
};
/******************************************************************************/
#if WITH_AFB_HOOK

static afb_req_x4_t x4_req_hooked_addref(afb_req_x4_t reqx4)
{
	struct afb_req_v4 *req = req_v4_from_req_x4(reqx4);
	afb_hook_req_addref(req->comreq);
	return x4_req_addref(reqx4);
}

static void x4_req_hooked_unref(afb_req_x4_t reqx4)
{
	struct afb_req_v4 *req = req_v4_from_req_x4(reqx4);
	afb_hook_req_unref(req->comreq);
	x4_req_unref(reqx4);
}

static
afb_api_x4_t
x4_req_hooked_api(
	afb_req_x4_t reqx4
) {
//	struct afb_req_v4 *req = req_v4_from_req_x4(reqx4);
	return x4_req_api(reqx4);
}

static
void *
x4_req_hooked_vcbdata(
	afb_req_x4_t reqx4
) {
//	struct afb_req_v4 *req = req_v4_from_req_x4(reqx4);
	return x4_req_vcbdata(reqx4);
}

static
const char *
x4_req_hooked_called_api(
	afb_req_x4_t reqx4
) {
//	struct afb_req_v4 *req = req_v4_from_req_x4(reqx4);
	return x4_req_called_api(reqx4);
}

static
const char *
x4_req_hooked_called_verb(
	afb_req_x4_t reqx4
) {
//	struct afb_req_v4 *req = req_v4_from_req_x4(reqx4);
	return x4_req_called_verb(reqx4);
}

static void x4_req_hooked_subcall(
	afb_req_x4_t reqx4,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	const struct afb_data_x4 * const *params,
	int flags,
	void (*callback)(void *closure, int status, unsigned nreplies, const struct afb_data_x4 * const *replies, afb_req_x4_t req),
	void *closure
) {
	struct afb_req_v4 *req = req_v4_from_req_x4(reqx4);
	afb_req_v4_addref(req);
	afb_calls_subcall_hookable_x4(afb_api_v4_get_api_common(req->api),
			apiname, verbname, nparams, params,
			subcall_cb, req, callback, closure, req->comreq, flags);
}

static int x4_req_hooked_subcall_sync(
	afb_req_x4_t reqx4,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	const struct afb_data_x4 * const *params,
	int flags,
	int *status,
	unsigned *nreplies,
	const struct afb_data_x4 **replies
) {
	struct afb_req_v4 *req = req_v4_from_req_x4(reqx4);
	return afb_calls_subcall_sync_hookable_x4(afb_api_v4_get_api_common(req->api),
			apiname, verbname, nparams, params,
			status, nreplies, replies, req->comreq, flags);
}

/******************************************************************************/

const struct afb_req_x4_itf req_v4_hooked_itf = {
	.addref = x4_req_hooked_addref,
	.unref = x4_req_hooked_unref,
	.api = x4_req_hooked_api,
	.vcbdata = x4_req_hooked_vcbdata,
	.called_api = x4_req_hooked_called_api,
	.called_verb = x4_req_hooked_called_verb,
	.session_close = common_req_hooked_session_close,
	.session_set_LOA = common_req_hooked_session_set_LOA,
	.vverbose = common_req_hooked_vverbose,
	.cookie = common_req_hooked_cookie,
	.subscribe = x4_req_subscribe, /* FIXME HOOKING */
	.unsubscribe = x4_req_unsubscribe, /* FIXME HOOKING */
	.get_client_info = common_req_hooked_get_client_info,
	.check_permission = x4_req_check_permission, /* FIXME HOOKING */
	.new_data_set = x4_req_new_data_set, /* FIXME HOOKING */
	.new_data_copy = x4_req_new_data_copy, /* FIXME HOOKING */
	.new_data_alloc = x4_req_new_data_alloc, /* FIXME HOOKING */
	.parameters = x4_req_parameters, /* FIXME HOOKING */
	.reply = x4_req_reply, /* FIXME HOOKING */
	.subcall = x4_req_hooked_subcall,
	.subcall_sync = x4_req_hooked_subcall_sync,
};
#endif

/******************************************************************************/

static void call_checked_v4(void *closure, int status)
{
	struct afb_req_v4 *req = closure;

	if (status > 0)
		req->verb->callback(
			req_v4_to_req_x4(req),
			req->comreq->nparams,
			req->comreq->params
			);
	afb_req_v4_unref(req);
}

void afb_req_v4_process(
	struct afb_req_common *comreq,
	struct afb_api_v4 *api,
	const struct afb_verb_v4 *verb
) {
	struct afb_req_v4 *req;

	req = malloc(sizeof *req);
	if (req == NULL) {
		afb_req_common_reply_internal_error(comreq);
	}
	else {
		req->comreq = afb_req_common_addref(comreq);
		req->api = api;
		req->verb = verb;
#if WITH_AFB_HOOK
		req->x4.itf = comreq->hookflags ? &req_v4_hooked_itf : &req_v4_itf;
#else
		req->x4.itf = &req_v4_itf;
#endif
		req->x4.logmask = afb_api_v4_get_api_x4(api)->logmask;
		req->refcount = 1;
		afb_req_common_check_and_set_session_async(comreq, verb->auth, verb->session, call_checked_v4, req);
	}
}
