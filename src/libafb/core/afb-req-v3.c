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
#include "core/afb-data.h"
#include "core/afb-params.h"
#include "core/afb-evt.h"
#include "core/afb-hook.h"
#include "core/afb-json-legacy.h"
#include "core/afb-req-common.h"
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

	/** as json object */
	struct json_object *json;

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

#define CLOSURE_T                       struct afb_req_x2 *
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
x2_req_addref(
	struct afb_req_x2 *xreq
) {
	struct afb_req_v3 *req = req_v3_from_req_x2(xreq);
	return req_v3_to_req_x2(afb_req_v3_addref(req));
}

static
void
x2_req_unref(
	struct afb_req_x2 *xreq
) {
	struct afb_req_v3 *req = req_v3_from_req_x2(xreq);
	afb_req_v3_unref(req);
}

struct x2subcallcb2 {
	struct afb_req_v3 *req;
	void (*callback)(void*, struct json_object*, const char*, const char*, struct afb_req_x2*);
	void *closure;
};

static
void subcall_cb2(
	void *closure,
	struct json_object *object,
	const char *error,
	const char *info
) {
	struct x2subcallcb2 *sc = closure;

	sc->callback(sc->closure, object, error, info, req_v3_to_req_x2(sc->req));
	afb_req_v3_unref(sc->req);
}

static
void subcall_cb(
	void *closure1,
	void *closure2,
	void *closure3,
	int status,
	unsigned nreplies,
	const struct afb_data_x4 * const *replies
) {
	struct x2subcallcb2 sc;

	sc.req = closure1;
	sc.callback = closure2;
	sc.closure = closure3;

	afb_json_legacy_do_reply_json_c(&sc, status, nreplies, replies, subcall_cb2);
}

static
void
x2_req_subcall(
	struct afb_req_x2 *xreq,
	const char *api,
	const char *verb,
	struct json_object *args,
	int flags,
	void (*callback)(void *closure, struct json_object *object, const char *error, const char * info, struct afb_req_x2 *req),
	void *closure
) {
	struct afb_req_v3 *req = req_v3_from_req_x2(xreq);
	const struct afb_data_x4 *data;
	int rc;

	rc = afb_json_legacy_make_data_x4_json_c(&data, args);
	if (rc < 0) {
		callback(closure, 0, afb_error_text_internal_error, 0, xreq);
	}
	else {
		afb_req_v3_addref(req);
		afb_calls_subcall_x4(afb_api_v3_get_api_common(req->api), api, verb, 1, &data,
					subcall_cb, req, callback, closure, req->comreq, flags);
	}
}

static
int
x2_req_subcall_sync(
	struct afb_req_x2 *xreq,
	const char *api,
	const char *verb,
	struct json_object *args,
	int flags,
	struct json_object **object,
	char **error,
	char **info
) {
	int rc;
	int result;
	const struct afb_data_x4 *data;
	const struct afb_data_x4 *replies[3];
	unsigned nreplies;
	int status;
	struct afb_req_v3 *req = req_v3_from_req_x2(xreq);

	rc = afb_json_legacy_make_data_x4_json_c(&data, args);
	if (rc < 0) {
		result = rc;
		*object = 0;
		*error = strdup(afb_error_text_internal_error);
		*info = 0;
	}
	else {
		nreplies = 3;
		result = afb_calls_subcall_sync_x4(afb_api_v3_get_api_common(req->api),
					api, verb, 1, &data,
					&status, &nreplies, replies, req->comreq, flags);
		afb_json_legacy_get_reply_sync(status, nreplies, replies, object, error, info);
		afb_params_unref(nreplies, replies);
	}
	return result;
}

static
struct json_object *
x2_req_json(
	struct afb_req_x2 *reqx2
) {
	struct afb_req_v3 *req = req_v3_from_req_x2(reqx2);
	return req->json;
}

static
struct afb_arg
x2_req_get(
	struct afb_req_x2 *reqx2,
	const char *name
) {
	struct afb_req_v3 *req = req_v3_from_req_x2(reqx2);
	struct afb_arg arg;
	struct json_object *value, *file, *path;

	if (json_object_object_get_ex(req->json, name, &value)) {
		arg.name = name;
		if (json_object_object_get_ex(value, "file", &file)
		 && json_object_object_get_ex(value, "path", &path)) {
			arg.value = json_object_get_string(file);
			arg.path = json_object_get_string(path);
		}
		else {
			arg.value = json_object_get_string(value);
			arg.path = NULL;
		}
	} else {
		arg.name = NULL;
		arg.value = NULL;
		arg.path = NULL;
	}
	return arg;
}

static
void
x2_req_reply(
	struct afb_req_x2 *reqx2,
	struct json_object *obj,
	const char *error,
	const char *info
) {
	struct afb_req_common *comreq = req_v3_from_req_x2(reqx2)->comreq;

	afb_json_legacy_req_reply(comreq, obj, error, info);
}

static
void
x2_req_vreply(
	struct afb_req_x2 *reqx2,
	struct json_object *obj,
	const char *error,
	const char *fmt,
	va_list args
) {
	struct afb_req_common *comreq = req_v3_from_req_x2(reqx2)->comreq;

	afb_json_legacy_req_vreply(comreq, obj, error, fmt, args);
}

static
int
x2_req_subscribe_event_x2(
	struct afb_req_x2 *reqx2,
	struct afb_event_x2 *event
) {
	struct afb_req_common *comreq = req_v3_from_req_x2(reqx2)->comreq;
	struct afb_evt *evt = afb_evt_of_x2(event);
	return afb_req_common_subscribe(comreq, evt);
}

static
int
x2_req_unsubscribe_event_x2(
	struct afb_req_x2 *reqx2,
	struct afb_event_x2 *event
) {
	struct afb_req_common *comreq = req_v3_from_req_x2(reqx2)->comreq;
	struct afb_evt *evt = afb_evt_of_x2(event);
	return afb_req_common_unsubscribe(comreq, evt);
}

static
int
x2_req_has_permission(
	struct afb_req_x2 *reqx2,
	const char *permission
) {
	struct afb_req_common *comreq = req_v3_from_req_x2(reqx2)->comreq;
	return afb_req_common_has_permission(comreq, permission);
}

static
char *
x2_req_get_application_id(
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
x2_req_get_uid(
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
x2_req_check_permission(
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
	.json = x2_req_json,
	.get = x2_req_get,
	.legacy_success = NULL,
	.legacy_fail = NULL,
	.legacy_vsuccess = NULL,
	.legacy_vfail = NULL,
	.legacy_context_get = NULL,
	.legacy_context_set = NULL,
	.addref = x2_req_addref,
	.unref = x2_req_unref,
	.session_close = common_req_session_close,
	.session_set_LOA = common_req_session_set_LOA,
	.legacy_subscribe_event_x1 = NULL,
	.legacy_unsubscribe_event_x1 = NULL,
	.legacy_subcall = NULL,
	.legacy_subcallsync = NULL,
	.vverbose = common_req_vverbose,
	.legacy_store_req = NULL,
	.legacy_subcall_req = NULL,
	.has_permission = x2_req_has_permission,
	.get_application_id = x2_req_get_application_id,
	.context_make = common_req_cookie,
	.subscribe_event_x2 = x2_req_subscribe_event_x2,
	.unsubscribe_event_x2 = x2_req_unsubscribe_event_x2,
	.legacy_subcall_request = NULL,
	.get_uid = x2_req_get_uid,
	.reply = x2_req_reply,
	.vreply = x2_req_vreply,
	.get_client_info = common_req_get_client_info,
	.subcall = x2_req_subcall,
	.subcallsync = x2_req_subcall_sync,
	.check_permission = x2_req_check_permission,
};
/******************************************************************************/
#if WITH_AFB_HOOK

static struct afb_req_x2 *x2_req_hooked_addref(struct afb_req_x2 *closure)
{
	struct afb_req_v3 *req = req_v3_from_req_x2(closure);
	afb_hook_req_addref(req->comreq);
	return x2_req_addref(closure);
}

static void x2_req_hooked_unref(struct afb_req_x2 *closure)
{
	struct afb_req_v3 *req = req_v3_from_req_x2(closure);
	afb_hook_req_unref(req->comreq);
	x2_req_unref(closure);
}

static char *x2_req_hooked_get_application_id(struct afb_req_x2 *closure)
{
	struct afb_req_v3 *req = req_v3_from_req_x2(closure);
	char *r = x2_req_get_application_id(closure);
	return afb_hook_req_get_application_id(req->comreq, r);
}

static int x2_req_hooked_get_uid(struct afb_req_x2 *closure)
{
	struct afb_req_v3 *req = req_v3_from_req_x2(closure);
	int r = x2_req_get_uid(closure);
	return afb_hook_req_get_uid(req->comreq, r);
}

static void x2_req_hooked_subcall(
	struct afb_req_x2 *xreq,
	const char *api,
	const char *verb,
	struct json_object *args,
	int flags,
	void (*callback)(void *closure, struct json_object *object, const char *error, const char * info, struct afb_req_x2 *req),
	void *closure)
{
	struct afb_req_v3 *req = req_v3_from_req_x2(xreq);
	const struct afb_data_x4 *data;
	int rc;

	rc = afb_json_legacy_make_data_x4_json_c(&data, args);
	if (rc < 0) {
		callback(closure, 0, afb_error_text_internal_error, 0, xreq);
	}
	else {
		afb_req_v3_addref(req);
		afb_calls_subcall_hookable_x4(afb_api_v3_get_api_common(req->api), api, verb, 1, &data,
					subcall_cb, req, callback, closure, req->comreq, flags);
	}
}

static
int
x2_req_hooked_subcall_sync(
	struct afb_req_x2 *xreq,
	const char *api,
	const char *verb,
	struct json_object *args,
	int flags,
	struct json_object **object,
	char **error,
	char **info
) {
	int rc;
	int result;
	const struct afb_data_x4 *data;
	const struct afb_data_x4 *replies[3];
	unsigned nreplies;
	int status;
	struct afb_req_v3 *req = req_v3_from_req_x2(xreq);

	rc = afb_json_legacy_make_data_x4_json_c(&data, args);
	if (rc < 0) {
		result = rc;
		*object = 0;
		*error = strdup(afb_error_text_internal_error);
		*info = 0;
	}
	else {
		nreplies = 3;
		result = afb_calls_subcall_sync_hookable_x4(afb_api_v3_get_api_common(req->api),
					api, verb, 1, &data,
					&status, &nreplies, replies, req->comreq, flags);
		afb_json_legacy_get_reply_sync(status, nreplies, replies, object, error, info);
		afb_params_unref(nreplies, replies);
	}
	return result;
}

static
struct json_object *
x2_req_hooked_json(
	struct afb_req_x2 *reqx2
) {
	struct afb_req_common *comreq = req_v3_from_req_x2(reqx2)->comreq;
	struct json_object *r = x2_req_json(reqx2);
	return afb_hook_req_json(comreq, r);
}

static
struct afb_arg
x2_req_hooked_get(
	struct afb_req_x2 *reqx2,
	const char *name
) {
	struct afb_req_common *comreq = req_v3_from_req_x2(reqx2)->comreq;
	struct afb_arg r = x2_req_get(reqx2, name);
	return afb_hook_req_get(comreq, name, r);
}

static
void
x2_req_hooked_reply(
	struct afb_req_x2 *reqx2,
	struct json_object *obj,
	const char *error,
	const char *info
) {
	struct afb_req_common *comreq = req_v3_from_req_x2(reqx2)->comreq;

	afb_json_legacy_req_hooked_reply(comreq, obj, error, info);
}

static
void
x2_req_hooked_vreply(
	struct afb_req_x2 *reqx2,
	struct json_object *obj,
	const char *error,
	const char *fmt,
	va_list args
) {
	struct afb_req_common *comreq = req_v3_from_req_x2(reqx2)->comreq;

	afb_json_legacy_req_hooked_vreply(comreq, obj, error, fmt, args);
}

static
int
x2_req_hooked_subscribe_event_x2(
	struct afb_req_x2 *reqx2,
	struct afb_event_x2 *event
) {
	struct afb_req_common *comreq = req_v3_from_req_x2(reqx2)->comreq;
	struct afb_evt *evt = afb_evt_of_x2(event);
	return afb_req_common_subscribe_hookable(comreq, evt);
}

static
int
x2_req_hooked_unsubscribe_event_x2(
	struct afb_req_x2 *reqx2,
	struct afb_event_x2 *event
) {
	struct afb_req_common *comreq = req_v3_from_req_x2(reqx2)->comreq;
	struct afb_evt *evt = afb_evt_of_x2(event);
	return afb_req_common_unsubscribe_hookable(comreq, evt);
}

static
int
x2_req_hooked_has_permission(
	struct afb_req_x2 *reqx2,
	const char *permission
) {
	struct afb_req_common *comreq = req_v3_from_req_x2(reqx2)->comreq;
	return afb_req_common_has_permission_hookable(comreq, permission);
}

/******************************************************************************/

const struct afb_req_x2_itf req_v3_hooked_itf = {
	.json = x2_req_hooked_json,
	.get = x2_req_hooked_get,
	.legacy_success = NULL,
	.legacy_fail = NULL,
	.legacy_vsuccess = NULL,
	.legacy_vfail = NULL,
	.legacy_context_get = NULL,
	.legacy_context_set = NULL,
	.addref = x2_req_hooked_addref,
	.unref = x2_req_hooked_unref,
	.session_close = common_req_hooked_session_close,
	.session_set_LOA = common_req_hooked_session_set_LOA,
	.legacy_subscribe_event_x1 = NULL,
	.legacy_unsubscribe_event_x1 = NULL,
	.legacy_subcall = NULL,
	.legacy_subcallsync = NULL,
	.vverbose = common_req_hooked_vverbose,
	.legacy_store_req = NULL,
	.legacy_subcall_req = NULL,
	.has_permission = x2_req_hooked_has_permission,
	.get_application_id = x2_req_hooked_get_application_id,
	.context_make = common_req_hooked_cookie,
	.subscribe_event_x2 = x2_req_hooked_subscribe_event_x2,
	.unsubscribe_event_x2 = x2_req_hooked_unsubscribe_event_x2,
	.legacy_subcall_request = NULL,
	.get_uid = x2_req_hooked_get_uid,
	.reply = x2_req_hooked_reply,
	.vreply = x2_req_hooked_vreply,
	.get_client_info = common_req_hooked_get_client_info,
	.subcall = x2_req_hooked_subcall,
	.subcallsync = x2_req_hooked_subcall_sync,
	.check_permission = x2_req_check_permission, /* TODO */
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

static void get_json_object(void *closure, struct json_object *object, const void *unused)
{
	struct afb_req_v3 *req = closure;
	req->json = json_object_get(object);
}

void afb_req_v3_process(
	struct afb_req_common *comreq,
	struct afb_api_v3 *api,
	struct afb_api_x3 *apix3,
	const struct afb_verb_v3 *verb
) {
	int rc;
	struct afb_req_v3 *req;

	req = malloc(sizeof *req);
	if (req == NULL) {
		afb_req_common_reply_internal_error(comreq);
		return;
	}

	rc = afb_json_legacy_do2_single_json_c(
			comreq->nparams, comreq->params,
			get_json_object, req, 0);
	if (rc < 0) {
		free(req);
		afb_req_common_reply_internal_error(comreq);
		return;
	}

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
	afb_req_common_check_and_set_session_async(comreq,
			verb->auth, verb->session,
			call_checked_v3, req);
}
