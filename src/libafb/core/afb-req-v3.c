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
#include "core/afb-api-common.h"
#include "core/afb-api-v3.h"
#include "core/afb-auth.h"
#include "core/afb-calls.h"
#include "core/afb-evt.h"
#include "core/afb-cred.h"
#include "core/afb-hook.h"
#include "core/afb-msg-json.h"
#include "core/afb-req-common.h"
#include "core/afb-req-reply.h"
#include "core/afb-req-v3.h"
#include "core/afb-error-text.h"
#include "core/afb-jobs.h"
#include "core/afb-sched.h"

#include "sys/verbose.h"

#include "containerof.h"
#include <stdarg.h>
#include <stddef.h>

#include <afb/afb-req-x2-itf.h>

/**
 * Internal data for requests V3
 */
struct afb_req_v3
{
	/** the request */
	struct afb_req_common *comreq;

	/** the api */
	struct afb_api_v3 *api;

	/** exported x2 */
	struct afb_req_x2 x2;

	/** count of references */
	uint16_t refcount;
};

/******************************************************************************/

static inline struct afb_req_v3 *req_v3_from_req_x2(struct afb_req_x2 *req)
{
	return containerof(struct afb_req_v3, x2, req);
}

static inline struct afb_req_x2 *req_v3_to_req_x2(struct afb_req_v3 *req)
{
	return &req->x2;
}

/******************************************************************************/

#define CLOSURE_T                       struct afb_req_x2
#define CLOSURE_TO_REQ_COMMON(closure)  (req_v3_from_req_x2(closure)->comreq)

#include "afb-req-common.inc"

#undef CLOSURE_TO_REQ_COMMON
#undef CLOSURE_T

/******************************************************************************/

inline struct afb_req_v3 *afb_req_v3_addref(struct afb_req_v3 *req)
{
	__atomic_add_fetch(&req->refcount, 1, __ATOMIC_RELAXED);
	return req;
}

inline void afb_req_v3_unref(struct afb_req_v3 *req)
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
struct afb_req_x2 *
req_addref_cb(
	struct afb_req_x2 *xreq
) {
	struct afb_req_v3 *req = req_v3_from_req_x2(xreq);
	return req_v3_to_req_x2(afb_req_v3_addref(req));
}

static
void
req_unref_cb(
	struct afb_req_x2 *xreq
) {
	struct afb_req_v3 *req = req_v3_from_req_x2(xreq);
	afb_req_v3_unref(req);
}

static
void subcall_cb(
	void *closure1,
	void *closure2,
	void *closure3,
	const struct afb_req_reply *reply
) {
	struct afb_req_v3 *req = closure1;
	void (*callback)(void*, struct json_object*, const char*, const char*, struct afb_req_x2*) = closure2;
	void *closure = closure3;
	callback(closure, reply->object, reply->error, reply->info, req_v3_to_req_x2(req));
	afb_req_v3_unref(req);
}

static
void
req_subcall_cb(
	struct afb_req_x2 *xreq,
	const char *api,
	const char *verb,
	struct json_object *args,
	int flags,
	void (*callback)(void *closure, struct json_object *object, const char *error, const char * info, struct afb_req_x2 *req),
	void *closure
) {
	struct afb_req_v3 *req = req_v3_from_req_x2(xreq);
	afb_req_v3_addref(req);
	afb_calls_subcall(afb_api_v3_get_api_common(req->api), api, verb, args, subcall_cb, req, callback, closure, req->comreq, flags);
}

static
int
req_subcallsync_cb(
	struct afb_req_x2 *xreq,
	const char *api,
	const char *verb,
	struct json_object *args,
	int flags,
	struct json_object **object,
	char **error,
	char **info
) {
	int result;
	struct afb_req_reply reply;
	struct afb_req_v3 *req = req_v3_from_req_x2(xreq);
	result = afb_calls_subcall_sync(afb_api_v3_get_api_common(req->api), api, verb, args, &reply, req->comreq, flags);
	afb_req_reply_move_splitted(&reply, object, error, info);
	return result;
}

static
char *
req_get_application_id_cb(
	struct afb_req_x2 *xreq
) {
#if WITH_CRED
	struct afb_req_v3 *req = req_v3_from_req_x2(xreq);
	struct afb_cred *cred = req->comreq->credentials;
	return cred && cred->id ? strdup(cred->id) : NULL;
#else
	return NULL;
#endif
}

static
int
req_get_uid_cb(
	struct afb_req_x2 *xreq
) {
#if WITH_CRED
	struct afb_req_v3 *req = req_v3_from_req_x2(xreq);
	struct afb_cred *cred = req->comreq->credentials;
	return cred && cred->id ? (int)cred->uid : -1;
#else
	return -1;
#endif
}

static
void
check_permission_status_cb(
	void *closure,
	int status
) {
	struct afb_req_v3 *req = closure;
	void *clo;
	void (*cb)(void*,int,struct afb_req_x2*);

	clo = afb_req_common_async_pop(req->comreq);
	cb = afb_req_common_async_pop(req->comreq);
	cb(clo, status, req_v3_to_req_x2(req));
}

static
void
req_check_permission_cb(
	struct afb_req_x2 *xreq,
	const char *permission,
	void (*callback)(void*,int,struct afb_req_x2*),
	void *closure
) {
	struct afb_req_v3 *req = req_v3_from_req_x2(xreq);
	if (afb_req_common_async_push2(req->comreq, callback, closure))
		afb_req_common_check_permission(req->comreq, permission, check_permission_status_cb, req);
	else
		callback(closure, X_EBUSY, xreq);
}

/******************************************************************************/

const struct afb_req_x2_itf req_v3_itf = {
	.json = req_json_cb,
	.get = req_get_cb,
	.legacy_success = NULL,
	.legacy_fail = NULL,
	.legacy_vsuccess = NULL,
	.legacy_vfail = NULL,
	.legacy_context_get = NULL,
	.legacy_context_set = NULL,
	.addref = req_addref_cb,
	.unref = req_unref_cb,
	.session_close = req_session_close_cb,
	.session_set_LOA = req_session_set_LOA_cb,
	.legacy_subscribe_event_x1 = NULL,
	.legacy_unsubscribe_event_x1 = NULL,
	.legacy_subcall = NULL,
	.legacy_subcallsync = NULL,
	.vverbose = req_vverbose_cb,
	.legacy_store_req = NULL,
	.legacy_subcall_req = NULL,
	.has_permission = req_has_permission_cb,
	.get_application_id = req_get_application_id_cb,
	.context_make = req_cookie_cb,
	.subscribe_event_x2 = req_subscribe_event_x2_cb,
	.unsubscribe_event_x2 = req_unsubscribe_event_x2_cb,
	.legacy_subcall_request = NULL,
	.get_uid = req_get_uid_cb,
	.reply = req_reply_cb,
	.vreply = req_vreply_cb,
	.get_client_info = req_get_client_info_cb,
	.subcall = req_subcall_cb,
	.subcallsync = req_subcallsync_cb,
	.check_permission = req_check_permission_cb,
};
/******************************************************************************/
#if WITH_AFB_HOOK

static struct afb_req_x2 *req_addref_hookable_cb(struct afb_req_x2 *closure)
{
	struct afb_req_v3 *req = req_v3_from_req_x2(closure);
	afb_hook_req_addref(req->comreq);
	return req_addref_cb(closure);
}

static void req_unref_hookable_cb(struct afb_req_x2 *closure)
{
	struct afb_req_v3 *req = req_v3_from_req_x2(closure);
	afb_hook_req_unref(req->comreq);
	req_unref_cb(closure);
}

static char *req_get_application_id_hookable_cb(struct afb_req_x2 *closure)
{
	struct afb_req_v3 *req = req_v3_from_req_x2(closure);
	char *r = req_get_application_id_cb(closure);
	return afb_hook_req_get_application_id(req->comreq, r);
}

static int req_get_uid_hookable_cb(struct afb_req_x2 *closure)
{
	struct afb_req_v3 *req = req_v3_from_req_x2(closure);
	int r = req_get_uid_cb(closure);
	return afb_hook_req_get_uid(req->comreq, r);
}

static void req_subcall_hookable_cb(
				struct afb_req_x2 *xreq,
				const char *api,
				const char *verb,
				struct json_object *args,
				int flags,
				void (*callback)(void *closure, struct json_object *object, const char *error, const char * info, struct afb_req_x2 *req),
				void *closure)
{
	struct afb_req_v3 *req = req_v3_from_req_x2(xreq);
	afb_req_v3_addref(req);
	afb_calls_subcall_hookable(afb_api_v3_get_api_common(req->api), api, verb, args, subcall_cb, req, callback, closure, req->comreq, flags);
}

static int req_subcallsync_hookable_cb(
				struct afb_req_x2 *xreq,
				const char *api,
				const char *verb,
				struct json_object *args,
				int flags,
				struct json_object **object,
				char **error,
				char **info)
{
	struct afb_req_v3 *req = req_v3_from_req_x2(xreq);
	struct afb_req_reply reply;
	int result;

	result = afb_calls_subcall_sync_hookable(afb_api_v3_get_api_common(req->api), api, verb, args, &reply, req->comreq, flags);
	afb_req_reply_move_splitted(&reply, object, error, info);
	return result;
}

/******************************************************************************/

const struct afb_req_x2_itf req_v3_hooked_itf = {
	.json = req_json_hookable_cb,
	.get = req_get_hookable_cb,
	.legacy_success = NULL,
	.legacy_fail = NULL,
	.legacy_vsuccess = NULL,
	.legacy_vfail = NULL,
	.legacy_context_get = NULL,
	.legacy_context_set = NULL,
	.addref = req_addref_hookable_cb,
	.unref = req_unref_hookable_cb,
	.session_close = req_session_close_hookable_cb,
	.session_set_LOA = req_session_set_LOA_hookable_cb,
	.legacy_subscribe_event_x1 = NULL,
	.legacy_unsubscribe_event_x1 = NULL,
	.legacy_subcall = NULL,
	.legacy_subcallsync = NULL,
	.vverbose = req_vverbose_hookable_cb,
	.legacy_store_req = NULL,
	.legacy_subcall_req = NULL,
	.has_permission = req_has_permission_hookable_cb,
	.get_application_id = req_get_application_id_hookable_cb,
	.context_make = req_cookie_hookable_cb,
	.subscribe_event_x2 = req_subscribe_event_x2_hookable_cb,
	.unsubscribe_event_x2 = req_unsubscribe_event_x2_hookable_cb,
	.legacy_subcall_request = NULL,
	.get_uid = req_get_uid_hookable_cb,
	.reply = req_reply_hookable_cb,
	.vreply = req_vreply_hookable_cb,
	.get_client_info = req_get_client_info_hookable_cb,
	.subcall = req_subcall_hookable_cb,
	.subcallsync = req_subcallsync_hookable_cb,
	.check_permission = req_check_permission_cb, /* TODO */
};
#endif

/******************************************************************************/

static void call_checked_v3(void *closure, int status)
{
	struct afb_req_v3 *req = closure;
	const struct afb_verb_v3 *verb;

	if (status > 0) {
		verb = (const struct afb_verb_v3*)req->x2.vcbdata;
		req->x2.vcbdata = verb->vcbdata;
		verb->callback(req_v3_to_req_x2(req));
	}
	afb_req_v3_unref(req);
}

void afb_req_v3_process(
	struct afb_req_common *comreq,
	struct afb_api_v3 *api,
	struct afb_api_x3 *apix3,
	const struct afb_verb_v3 *verb
) {
	struct afb_req_v3 *req;


	req = malloc(sizeof *req);
	if (req == NULL) {
		afb_req_common_reply_internal_error(comreq);
	}
	else {
		req->comreq = afb_req_common_addref(comreq);
		req->api = api;
		req->x2.api = apix3;
		req->x2.called_api = comreq->apiname;
		req->x2.called_verb = comreq->verbname;
#if WITH_AFB_HOOK
		req->x2.itf = comreq->hookflags ? &req_v3_hooked_itf : &req_v3_itf;
#else
		req->x2.itf = &req_v3_itf;
#endif
		req->x2.vcbdata = (void*)verb;
		req->refcount = 1;
		afb_req_common_check_and_set_session_async(comreq, verb->auth, verb->session, call_checked_v3, req);
	}
}
