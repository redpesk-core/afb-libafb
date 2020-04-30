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
#include "core/afb-context.h"
#include "core/afb-evt.h"
#include "core/afb-cred.h"
#include "core/afb-hook.h"
#include "core/afb-msg-json.h"
#include "core/afb-xreq.h"
#include "core/afb-error-text.h"
#include "core/afb-jobs.h"
#include "core/afb-sched.h"

#include "sys/verbose.h"

/******************************************************************************/

static void xreq_finalize(struct afb_xreq *xreq)
{
	if (!xreq->replied)
		afb_xreq_reply(xreq, NULL, afb_error_text_not_replied, NULL);
#if WITH_AFB_HOOK
	if (xreq->hookflags)
		afb_hook_xreq_end(xreq);
#endif
	if (xreq->caller)
		afb_xreq_unhooked_unref(xreq->caller);
	xreq->queryitf->unref(xreq);
}

inline void afb_xreq_unhooked_addref(struct afb_xreq *xreq)
{
	__atomic_add_fetch(&xreq->refcount, 1, __ATOMIC_RELAXED);
}

inline void afb_xreq_unhooked_unref(struct afb_xreq *xreq)
{
	if (!__atomic_sub_fetch(&xreq->refcount, 1, __ATOMIC_RELAXED))
		xreq_finalize(xreq);
}

/******************************************************************************/

struct json_object *afb_xreq_unhooked_json(struct afb_xreq *xreq)
{
	if (!xreq->json && xreq->queryitf->json)
		xreq->json = xreq->queryitf->json(xreq);
	return xreq->json;
}

static struct json_object *xreq_json_cb(struct afb_req_x2 *closure)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	return afb_xreq_unhooked_json(xreq);
}

static struct afb_arg xreq_get_cb(struct afb_req_x2 *closure, const char *name)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	struct afb_arg arg;
	struct json_object *object, *value;

	if (xreq->queryitf->get)
		arg = xreq->queryitf->get(xreq, name);
	else {
		object = xreq_json_cb(closure);
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

static int check_not_replied(struct afb_xreq *xreq, struct json_object *obj)
{
	int result = !xreq->replied;
	if (!result) {
		ERROR("reply called more than one time!!");
		json_object_put(obj);
	}
	return result;
}

#if WITH_REPLY_JOB
static void reply_job(int signum, void *closure)
{
	struct afb_xreq *xreq = closure;
	if (!signum)
		xreq->queryitf->reply(xreq, xreq->reply.object, xreq->reply.error, xreq->reply.info);
	free(xreq->reply.error);
	free(xreq->reply.info);
	afb_xreq_unhooked_unref(xreq);
}

static void do_reply_dynamic(struct afb_xreq *xreq, struct json_object *obj, const char *error, char *info)
{
	xreq->replied = 1;
	xreq->reply.object = obj;
	xreq->reply.info = info;
	if (error == NULL)
		xreq->reply.error = NULL;
	else {
		xreq->reply.error = strdup(error);
		if (xreq->reply.error == NULL) {
			xreq->queryitf->reply(xreq, obj, error, info);
			free(info);
			return;
		}
	}
	afb_xreq_unhooked_addref(xreq);
	if (afb_jobs_queue(NULL, 0, reply_job, xreq) < 0)
		reply_job(0, xreq);
}

static void do_reply_static(struct afb_xreq *xreq, struct json_object *obj, const char *error, const char *info)
{
	do_reply_dynamic(xreq, obj, error, info ? strdup(info) : NULL); /* ignore NULL of strdup */
}

#else

static void do_reply_static(struct afb_xreq *xreq, struct json_object *obj, const char *error, const char *info)
{
	xreq->replied = 1;
	xreq->queryitf->reply(xreq, obj, error, info);
}

static void do_reply_dynamic(struct afb_xreq *xreq, struct json_object *obj, const char *error, char *info)
{
	do_reply_static(xreq, obj, error, info);
	free(info);
}

#endif

static void xreq_reply_cb(struct afb_req_x2 *closure, struct json_object *obj, const char *error, const char *info)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);

	if (check_not_replied(xreq, obj))
		do_reply_static(xreq, obj, error, info);
}

static void xreq_vreply_cb(struct afb_req_x2 *closure, struct json_object *obj, const char *error, const char *fmt, va_list args)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	char *info;

	if (check_not_replied(xreq, obj)) {
		if (fmt == NULL || vasprintf(&info, fmt, args) < 0)
			info = NULL;
		do_reply_dynamic(xreq, obj, error, info);
	}
}

static void xreq_legacy_success_cb(struct afb_req_x2 *closure, struct json_object *obj, const char *info)
{
	xreq_reply_cb(closure, obj, NULL, info);
}

static void xreq_legacy_fail_cb(struct afb_req_x2 *closure, const char *status, const char *info)
{
	xreq_reply_cb(closure, NULL, status, info);
}

static void xreq_legacy_vsuccess_cb(struct afb_req_x2 *closure, struct json_object *obj, const char *fmt, va_list args)
{
	xreq_vreply_cb(closure, obj, NULL, fmt, args);
}

static void xreq_legacy_vfail_cb(struct afb_req_x2 *closure, const char *status, const char *fmt, va_list args)
{
	xreq_vreply_cb(closure, NULL, status, fmt, args);
}

static void *xreq_legacy_context_get_cb(struct afb_req_x2 *closure)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	return afb_context_get(&xreq->context);
}

static void xreq_legacy_context_set_cb(struct afb_req_x2 *closure, void *value, void (*free_value)(void*))
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	afb_context_set(&xreq->context, value, free_value);
}

static struct afb_req_x2 *xreq_addref_cb(struct afb_req_x2 *closure)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	afb_xreq_unhooked_addref(xreq);
	return closure;
}

static void xreq_unref_cb(struct afb_req_x2 *closure)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	afb_xreq_unhooked_unref(xreq);
}

static void xreq_session_close_cb(struct afb_req_x2 *closure)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	afb_context_close(&xreq->context);
}

#if SYNCHRONOUS_CHECKS
static int xreq_session_set_LOA_cb(struct afb_req_x2 *closure, unsigned level)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	return afb_context_change_loa(&xreq->context, level);
}
#else
struct chgloa {
	struct afb_sched_lock *schedlock;
	struct afb_xreq *xreq;
	unsigned level;
	int rc;
};

static void chgloa_cb(void *closure, int status)
{
	struct chgloa *cloa = closure;
	cloa->rc = status;
	afb_sched_leave(cloa->schedlock);
}

static void chgloa_job_cb(int signum, void *closure, struct afb_sched_lock *schedlock)
{
	struct chgloa *cloa = closure;

	if (signum) {
		cloa->rc = EINTR;
		afb_sched_leave(schedlock);
	}
	else {
		cloa->schedlock = schedlock;
		afb_context_change_loa_async(&(cloa->xreq->context), cloa->level, chgloa_cb, cloa);
	}
}

static int xreq_session_set_LOA_cb(struct afb_req_x2 *closure, unsigned level)
{
	int rc;
	struct chgloa cloa;

	cloa.level = level;
	cloa.xreq = xreq_from_req_x2(closure);
	rc = afb_sched_enter(NULL, 0, chgloa_job_cb, &cloa);
	if (rc == 0)
		rc = cloa.rc;
	return rc;
}
#endif

static int xreq_subscribe_event_x2_cb(struct afb_req_x2 *closure, struct afb_event_x2 *event)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	return afb_xreq_subscribe(xreq, event);
}

static int xreq_legacy_subscribe_event_x1_cb(struct afb_req_x2 *closure, struct afb_event_x1 event)
{
	return xreq_subscribe_event_x2_cb(closure, event.closure);
}

int afb_xreq_subscribe(struct afb_xreq *xreq, struct afb_event_x2 *event)
{
	if (xreq->replied) {
		ERROR("request replied, subscription impossible");
		return X_EINVAL;
	}
	if (xreq->queryitf->subscribe)
		return xreq->queryitf->subscribe(xreq, event);
	ERROR("no event listener, subscription impossible");
	return X_ENOTSUP;
}

static int xreq_unsubscribe_event_x2_cb(struct afb_req_x2 *closure, struct afb_event_x2 *event)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	return afb_xreq_unsubscribe(xreq, event);
}

static int xreq_legacy_unsubscribe_event_x1_cb(struct afb_req_x2 *closure, struct afb_event_x1 event)
{
	return xreq_unsubscribe_event_x2_cb(closure, event.closure);
}

int afb_xreq_unsubscribe(struct afb_xreq *xreq, struct afb_event_x2 *event)
{
	if (xreq->replied) {
		ERROR("request replied, unsubscription impossible");
		return X_EINVAL;
	}
	if (xreq->queryitf->unsubscribe)
		return xreq->queryitf->unsubscribe(xreq, event);
	ERROR("no event listener, unsubscription impossible");
	return X_ENOTSUP;
}

static void xreq_legacy_subcall_cb(struct afb_req_x2 *req, const char *api, const char *verb, struct json_object *args, void (*callback)(void*, int, struct json_object*), void *closure)
{
#if WITH_LEGACY_CALLS
	struct afb_xreq *xreq = xreq_from_req_x2(req);
	return afb_calls_legacy_subcall_v1(xreq, api, verb, args, callback, closure);
#else
	ERROR("Legacy subcall not supported");
	if (callback) {
		callback(closure, X_ENOTSUP, NULL);
	}
#endif
}

static void xreq_legacy_subcall_req_cb(struct afb_req_x2 *req, const char *api, const char *verb, struct json_object *args, void (*callback)(void*, int, struct json_object*, struct afb_req_x1), void *closure)
{
	struct afb_xreq *xreq = xreq_from_req_x2(req);
#if WITH_LEGACY_CALLS
	return afb_calls_legacy_subcall_v2(xreq, api, verb, args, callback, closure);
#else
	ERROR("Legacy subcall not supported");
	if (callback) {
		callback(closure, X_ENOTSUP, NULL, xreq_to_req_x1(xreq));
	}
#endif
}

static void xreq_legacy_subcall_request_cb(struct afb_req_x2 *req, const char *api, const char *verb, struct json_object *args, void (*callback)(void*, int, struct json_object*, struct afb_req_x2*), void *closure)
{
#if WITH_LEGACY_CALLS
	struct afb_xreq *xreq = xreq_from_req_x2(req);
	return afb_calls_legacy_subcall_v3(xreq, api, verb, args, callback, closure);
#else
	ERROR("Legacy subcall not supported");
	if (callback) {
		callback(closure, X_ENOTSUP, NULL, req);
	}
#endif
}

static int xreq_legacy_subcallsync_cb(struct afb_req_x2 *req, const char *api, const char *verb, struct json_object *args, struct json_object **result)
{
#if WITH_LEGACY_CALLS
#if WITH_AFB_CALL_SYNC
	struct afb_xreq *xreq = xreq_from_req_x2(req);
	return afb_calls_legacy_subcall_sync(xreq, api, verb, args, result);
#else
	ERROR("Subcall sync are not supported");
	if (result)
		*result = afb_msg_json_reply(NULL, "no-subcall-sync", NULL, NULL);
	errno = ENOTSUP;
	return -1;
#endif
#else
	ERROR("Legacy subcallsync not supported");
	if (result)
		*result = NULL;
	return X_ENOTSUP;
#endif
}

static void xreq_vverbose_cb(struct afb_req_x2 *closure, int level, const char *file, int line, const char *func, const char *fmt, va_list args)
{
	char *p;
	struct afb_xreq *xreq = xreq_from_req_x2(closure);

	if (!fmt || vasprintf(&p, fmt, args) < 0)
		vverbose(level, file, line, func, fmt, args);
	else {
		verbose(level, file, line, func, "[REQ/API %s] %s", xreq->request.called_api, p);
		free(p);
	}
}

static struct afb_stored_req *xreq_legacy_store_cb(struct afb_req_x2 *closure)
{
	xreq_addref_cb(closure);
	return (struct afb_stored_req*)closure;
}

#if SYNCHRONOUS_CHECKS
static int xreq_has_permission_cb(struct afb_req_x2 *closure, const char *permission)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	return afb_context_has_permission(&xreq->context, permission);
}
#else
struct hasperm {
	struct afb_sched_lock *schedlock;
	struct afb_xreq *xreq;
	const char *permision;
	int rc;
};

static void hasperm_cb(void *closure, int status)
{
	struct hasperm *hasp = closure;
	hasp->rc = status;
	afb_sched_leave(hasp->schedlock);
}

static void hasperm_job_cb(int signum, void *closure, struct afb_sched_lock *schedlock)
{
	struct hasperm *hasp = closure;

	if (signum) {
		hasp->rc = EINTR;
		afb_sched_leave(schedlock);
	}
	else {
		hasp->schedlock = schedlock;
		afb_context_has_permission_async(&(hasp->xreq->context), hasp->permision, hasperm_cb, hasp);
	}
}

static int xreq_has_permission_cb(struct afb_req_x2 *closure, const char *permission)
{
	int rc;
	struct hasperm hasp;

	hasp.permision = permission;
	hasp.xreq = xreq_from_req_x2(closure);
	rc = afb_sched_enter(NULL, 0, hasperm_job_cb, &hasp);
	if (rc == 0)
		rc = hasp.rc;
	return rc;
}
#endif

static char *xreq_get_application_id_cb(struct afb_req_x2 *closure)
{
#if WITH_CRED
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	struct afb_cred *cred = xreq->context.credentials;
	return cred && cred->id ? strdup(cred->id) : NULL;
#else
	return NULL;
#endif
}

static void *xreq_context_make_cb(struct afb_req_x2 *closure, int replace, void *(*create_value)(void*), void (*free_value)(void*), void *create_closure)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	return afb_context_make(&xreq->context, replace, create_value, free_value, create_closure);
}

static int xreq_get_uid_cb(struct afb_req_x2 *closure)
{
#if WITH_CRED
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	struct afb_cred *cred = xreq->context.credentials;
	return cred && cred->id ? (int)cred->uid : -1;
#else
	return -1;
#endif
}

static struct json_object *xreq_get_client_info_cb(struct afb_req_x2 *closure)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	struct json_object *r = json_object_new_object();
#if WITH_CRED
	struct afb_cred *cred = xreq->context.credentials;
	if (cred && cred->id) {
		json_object_object_add(r, "uid", json_object_new_int(cred->uid));
		json_object_object_add(r, "gid", json_object_new_int(cred->gid));
		json_object_object_add(r, "pid", json_object_new_int(cred->pid));
		json_object_object_add(r, "user", json_object_new_string(cred->user));
		json_object_object_add(r, "label", json_object_new_string(cred->label));
		json_object_object_add(r, "id", json_object_new_string(cred->id));
	}
#endif
	if (xreq->context.session) {
		json_object_object_add(r, "uuid", json_object_new_string(afb_context_uuid(&xreq->context)?:""));
		json_object_object_add(r, "LOA", json_object_new_int(afb_context_get_loa(&xreq->context)));
	}
	return r;
}

static void xreq_subcall_cb(
				struct afb_req_x2 *req,
				const char *api,
				const char *verb,
				struct json_object *args,
				int flags,
				void (*callback)(void *closure, struct json_object *object, const char *error, const char * info, struct afb_req_x2 *req),
				void *closure)
{
	struct afb_xreq *xreq = xreq_from_req_x2(req);
	afb_calls_subcall(xreq, api, verb, args, flags, callback, closure);
}

static int xreq_subcallsync_cb(
				struct afb_req_x2 *req,
				const char *api,
				const char *verb,
				struct json_object *args,
				int flags,
				struct json_object **object,
				char **error,
				char **info)
{
#if WITH_AFB_CALL_SYNC
	struct afb_xreq *xreq = xreq_from_req_x2(req);
	return afb_calls_subcall_sync(xreq, api, verb, args, flags, object, error, info);
#else
	ERROR("Subcall sync are not supported");
	if (object)
		*object = NULL;
	if (error)
		*error = strdup("no-subcall-sync");
	if (info)
		*info = NULL;
	errno = ENOTSUP;
	return -1;
#endif
}

struct chkperm {
	void (*callback)(void*,int,struct afb_req_x2*);
	void *closure;
	struct afb_xreq *xreq;
};

static void ckpermcb(void *closure, int status)
{
	struct chkperm *cp = closure;

	cp->callback(cp->closure, status, xreq_to_req_x2(cp->xreq));
	afb_xreq_unhooked_unref(cp->xreq);
	free(cp);
}

static void xreq_check_permission_cb(struct afb_req_x2 *req, const char *permission, void (*callback)(void*,int,struct afb_req_x2*), void *closure)
{
	struct chkperm *cp;
	struct afb_xreq *xreq = xreq_from_req_x2(req);

	cp = malloc(sizeof *cp);
	if (!cp)
#if SYNCHRONOUS_CHECKS
		callback(closure, afb_context_has_permission(&xreq->context, permission), req);
#else
		callback(closure, -1, req);
#endif
	else {
		cp->callback = callback;
		cp->closure = closure;
		cp->xreq = xreq;
		afb_xreq_unhooked_addref(xreq);
		afb_context_has_permission_async(&xreq->context, permission, ckpermcb, cp);
	}
}

/******************************************************************************/

const struct afb_req_x2_itf xreq_itf = {
	.json = xreq_json_cb,
	.get = xreq_get_cb,
	.legacy_success = xreq_legacy_success_cb,
	.legacy_fail = xreq_legacy_fail_cb,
	.legacy_vsuccess = xreq_legacy_vsuccess_cb,
	.legacy_vfail = xreq_legacy_vfail_cb,
	.legacy_context_get = xreq_legacy_context_get_cb,
	.legacy_context_set = xreq_legacy_context_set_cb,
	.addref = xreq_addref_cb,
	.unref = xreq_unref_cb,
	.session_close = xreq_session_close_cb,
	.session_set_LOA = xreq_session_set_LOA_cb,
	.legacy_subscribe_event_x1 = xreq_legacy_subscribe_event_x1_cb,
	.legacy_unsubscribe_event_x1 = xreq_legacy_unsubscribe_event_x1_cb,
	.legacy_subcall = xreq_legacy_subcall_cb,
	.legacy_subcallsync = xreq_legacy_subcallsync_cb,
	.vverbose = xreq_vverbose_cb,
	.legacy_store_req = xreq_legacy_store_cb,
	.legacy_subcall_req = xreq_legacy_subcall_req_cb,
	.has_permission = xreq_has_permission_cb,
	.get_application_id = xreq_get_application_id_cb,
	.context_make = xreq_context_make_cb,
	.subscribe_event_x2 = xreq_subscribe_event_x2_cb,
	.unsubscribe_event_x2 = xreq_unsubscribe_event_x2_cb,
	.legacy_subcall_request = xreq_legacy_subcall_request_cb,
	.get_uid = xreq_get_uid_cb,
	.reply = xreq_reply_cb,
	.vreply = xreq_vreply_cb,
	.get_client_info = xreq_get_client_info_cb,
	.subcall = xreq_subcall_cb,
	.subcallsync = xreq_subcallsync_cb,
	.check_permission = xreq_check_permission_cb,
};
/******************************************************************************/
#if WITH_AFB_HOOK

static struct json_object *xreq_hooked_json_cb(struct afb_req_x2 *closure)
{
	struct json_object *r = xreq_json_cb(closure);
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	return afb_hook_xreq_json(xreq, r);
}

static struct afb_arg xreq_hooked_get_cb(struct afb_req_x2 *closure, const char *name)
{
	struct afb_arg r = xreq_get_cb(closure, name);
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	return afb_hook_xreq_get(xreq, name, r);
}

static void xreq_hooked_reply_cb(struct afb_req_x2 *closure, struct json_object *obj, const char *error, const char *info)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	afb_hook_xreq_reply(xreq, obj, error, info);
	xreq_reply_cb(closure, obj, error, info);
}

static void xreq_hooked_vreply_cb(struct afb_req_x2 *closure, struct json_object *obj, const char *error, const char *fmt, va_list args)
{
	char *info;
	if (fmt == NULL || vasprintf(&info, fmt, args) < 0)
		info = NULL;
	xreq_hooked_reply_cb(closure, obj, error, info);
	free(info);
}

static void xreq_hooked_legacy_success_cb(struct afb_req_x2 *closure, struct json_object *obj, const char *info)
{
	xreq_hooked_reply_cb(closure, obj, NULL, info);
}

static void xreq_hooked_legacy_fail_cb(struct afb_req_x2 *closure, const char *status, const char *info)
{
	xreq_hooked_reply_cb(closure, NULL, status, info);
}

static void xreq_hooked_legacy_vsuccess_cb(struct afb_req_x2 *closure, struct json_object *obj, const char *fmt, va_list args)
{
	xreq_hooked_vreply_cb(closure, obj, NULL, fmt, args);
}

static void xreq_hooked_legacy_vfail_cb(struct afb_req_x2 *closure, const char *status, const char *fmt, va_list args)
{
	xreq_hooked_vreply_cb(closure, NULL, status, fmt, args);
}

static void *xreq_hooked_legacy_context_get_cb(struct afb_req_x2 *closure)
{
	void *r = xreq_legacy_context_get_cb(closure);
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	return afb_hook_xreq_legacy_context_get(xreq, r);
}

static void xreq_hooked_legacy_context_set_cb(struct afb_req_x2 *closure, void *value, void (*free_value)(void*))
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	afb_hook_xreq_legacy_context_set(xreq, value, free_value);
	xreq_legacy_context_set_cb(closure, value, free_value);
}

static struct afb_req_x2 *xreq_hooked_addref_cb(struct afb_req_x2 *closure)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	afb_hook_xreq_addref(xreq);
	return xreq_addref_cb(closure);
}

static void xreq_hooked_unref_cb(struct afb_req_x2 *closure)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	afb_hook_xreq_unref(xreq);
	xreq_unref_cb(closure);
}

static void xreq_hooked_session_close_cb(struct afb_req_x2 *closure)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	afb_hook_xreq_session_close(xreq);
	xreq_session_close_cb(closure);
}

static int xreq_hooked_session_set_LOA_cb(struct afb_req_x2 *closure, unsigned level)
{
	int r = xreq_session_set_LOA_cb(closure, level);
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	return afb_hook_xreq_session_set_LOA(xreq, level, r);
}

static int xreq_hooked_subscribe_event_x2_cb(struct afb_req_x2 *closure, struct afb_event_x2 *event)
{
	int r = xreq_subscribe_event_x2_cb(closure, event);
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	return afb_hook_xreq_subscribe(xreq, event, r);
}

static int xreq_hooked_legacy_subscribe_event_x1_cb(struct afb_req_x2 *closure, struct afb_event_x1 event)
{
	return xreq_hooked_subscribe_event_x2_cb(closure, event.closure);
}

static int xreq_hooked_unsubscribe_event_x2_cb(struct afb_req_x2 *closure, struct afb_event_x2 *event)
{
	int r = xreq_unsubscribe_event_x2_cb(closure, event);
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	return afb_hook_xreq_unsubscribe(xreq, event, r);
}

static int xreq_hooked_legacy_unsubscribe_event_x1_cb(struct afb_req_x2 *closure, struct afb_event_x1 event)
{
	return xreq_hooked_unsubscribe_event_x2_cb(closure, event.closure);
}

static void xreq_hooked_legacy_subcall_cb(struct afb_req_x2 *req, const char *api, const char *verb, struct json_object *args, void (*callback)(void*, int, struct json_object*), void *closure)
{
#if WITH_LEGACY_CALLS
	struct afb_xreq *xreq = xreq_from_req_x2(req);
	afb_calls_legacy_hooked_subcall_v1(xreq, api, verb, args, callback, closure);
#else
	xreq_legacy_subcall_cb(req, api, verb, args, callback, closure);
#endif
}

static void xreq_hooked_legacy_subcall_req_cb(struct afb_req_x2 *req, const char *api, const char *verb, struct json_object *args, void (*callback)(void*, int, struct json_object*, struct afb_req_x1), void *closure)
{
#if WITH_LEGACY_CALLS
	struct afb_xreq *xreq = xreq_from_req_x2(req);
	afb_calls_legacy_hooked_subcall_v2(xreq, api, verb, args, callback, closure);
#else
	xreq_legacy_subcall_req_cb(req, api, verb, args, callback, closure);
#endif
}

static void xreq_hooked_legacy_subcall_request_cb(struct afb_req_x2 *req, const char *api, const char *verb, struct json_object *args, void (*callback)(void*, int, struct json_object*, struct afb_req_x2 *), void *closure)
{
#if WITH_LEGACY_CALLS
	struct afb_xreq *xreq = xreq_from_req_x2(req);
	afb_calls_legacy_hooked_subcall_v3(xreq, api, verb, args, callback, closure);
#else
	xreq_legacy_subcall_request_cb(req, api, verb, args, callback, closure);
#endif
}

static int xreq_hooked_legacy_subcallsync_cb(struct afb_req_x2 *req, const char *api, const char *verb, struct json_object *args, struct json_object **result)
{
#if WITH_LEGACY_CALLS && WITH_AFB_CALL_SYNC
	struct afb_xreq *xreq = xreq_from_req_x2(req);
	return afb_calls_legacy_hooked_subcall_sync(xreq, api, verb, args, result);
#else
	return xreq_legacy_subcallsync_cb(req, api, verb, args, result);
#endif
}

static void xreq_hooked_vverbose_cb(struct afb_req_x2 *closure, int level, const char *file, int line, const char *func, const char *fmt, va_list args)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	va_list ap;
	va_copy(ap, args);
	xreq_vverbose_cb(closure, level, file, line, func, fmt, args);
	afb_hook_xreq_vverbose(xreq, level, file, line, func, fmt, ap);
	va_end(ap);
}

static struct afb_stored_req *xreq_hooked_legacy_store_cb(struct afb_req_x2 *closure)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	struct afb_stored_req *r = xreq_legacy_store_cb(closure);
	afb_hook_xreq_legacy_store(xreq, r);
	return r;
}

static int xreq_hooked_has_permission_cb(struct afb_req_x2 *closure, const char *permission)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	int r = xreq_has_permission_cb(closure, permission);
	return afb_hook_xreq_has_permission(xreq, permission, r);
}

static char *xreq_hooked_get_application_id_cb(struct afb_req_x2 *closure)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	char *r = xreq_get_application_id_cb(closure);
	return afb_hook_xreq_get_application_id(xreq, r);
}

static void *xreq_hooked_context_make_cb(struct afb_req_x2 *closure, int replace, void *(*create_value)(void*), void (*free_value)(void*), void *create_closure)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	void *result = xreq_context_make_cb(closure, replace, create_value, free_value, create_closure);
	return afb_hook_xreq_context_make(xreq, replace, create_value, free_value, create_closure, result);
}

static int xreq_hooked_get_uid_cb(struct afb_req_x2 *closure)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	int r = xreq_get_uid_cb(closure);
	return afb_hook_xreq_get_uid(xreq, r);
}

static struct json_object *xreq_hooked_get_client_info_cb(struct afb_req_x2 *closure)
{
	struct afb_xreq *xreq = xreq_from_req_x2(closure);
	struct json_object *r = xreq_get_client_info_cb(closure);
	return afb_hook_xreq_get_client_info(xreq, r);
}

static void xreq_hooked_subcall_cb(
				struct afb_req_x2 *req,
				const char *api,
				const char *verb,
				struct json_object *args,
				int flags,
				void (*callback)(void *closure, struct json_object *object, const char *error, const char * info, struct afb_req_x2 *req),
				void *closure)
{
	struct afb_xreq *xreq = xreq_from_req_x2(req);
	afb_calls_hooked_subcall(xreq, api, verb, args, flags, callback, closure);
}

static int xreq_hooked_subcallsync_cb(
				struct afb_req_x2 *req,
				const char *api,
				const char *verb,
				struct json_object *args,
				int flags,
				struct json_object **object,
				char **error,
				char **info)
{
#if WITH_AFB_CALL_SYNC
	struct afb_xreq *xreq = xreq_from_req_x2(req);
	return afb_calls_hooked_subcall_sync(xreq, api, verb, args, flags, object, error, info);
#else
	return xreq_subcallsync_cb(req, api, verb, args, flags, object, error, info);
#endif
}

/******************************************************************************/

const struct afb_req_x2_itf xreq_hooked_itf = {
	.json = xreq_hooked_json_cb,
	.get = xreq_hooked_get_cb,
	.legacy_success = xreq_hooked_legacy_success_cb,
	.legacy_fail = xreq_hooked_legacy_fail_cb,
	.legacy_vsuccess = xreq_hooked_legacy_vsuccess_cb,
	.legacy_vfail = xreq_hooked_legacy_vfail_cb,
	.legacy_context_get = xreq_hooked_legacy_context_get_cb,
	.legacy_context_set = xreq_hooked_legacy_context_set_cb,
	.addref = xreq_hooked_addref_cb,
	.unref = xreq_hooked_unref_cb,
	.session_close = xreq_hooked_session_close_cb,
	.session_set_LOA = xreq_hooked_session_set_LOA_cb,
	.legacy_subscribe_event_x1 = xreq_hooked_legacy_subscribe_event_x1_cb,
	.legacy_unsubscribe_event_x1 = xreq_hooked_legacy_unsubscribe_event_x1_cb,
	.legacy_subcall = xreq_hooked_legacy_subcall_cb,
	.legacy_subcallsync = xreq_hooked_legacy_subcallsync_cb,
	.vverbose = xreq_hooked_vverbose_cb,
	.legacy_store_req = xreq_hooked_legacy_store_cb,
	.legacy_subcall_req = xreq_hooked_legacy_subcall_req_cb,
	.has_permission = xreq_hooked_has_permission_cb,
	.get_application_id = xreq_hooked_get_application_id_cb,
	.context_make = xreq_hooked_context_make_cb,
	.subscribe_event_x2 = xreq_hooked_subscribe_event_x2_cb,
	.unsubscribe_event_x2 = xreq_hooked_unsubscribe_event_x2_cb,
	.legacy_subcall_request = xreq_hooked_legacy_subcall_request_cb,
	.get_uid = xreq_hooked_get_uid_cb,
	.reply = xreq_hooked_reply_cb,
	.vreply = xreq_hooked_vreply_cb,
	.get_client_info = xreq_hooked_get_client_info_cb,
	.subcall = xreq_hooked_subcall_cb,
	.subcallsync = xreq_hooked_subcallsync_cb,
	.check_permission = xreq_check_permission_cb, /* TODO */
};
#endif

/******************************************************************************/

struct afb_req_x1 afb_xreq_unstore(struct afb_stored_req *sreq)
{
	struct afb_xreq *xreq = (struct afb_xreq *)sreq;
#if WITH_AFB_HOOK
	if (xreq->hookflags)
		afb_hook_xreq_legacy_unstore(xreq);
#endif
	return xreq_to_req_x1(xreq);
}

struct json_object *afb_xreq_json(struct afb_xreq *xreq)
{
	return afb_req_x2_json(xreq_to_req_x2(xreq));
}

void afb_xreq_reply(struct afb_xreq *xreq, struct json_object *obj, const char *error, const char *info)
{
	afb_req_x2_reply(xreq_to_req_x2(xreq), obj, error, info);
}

void afb_xreq_reply_v(struct afb_xreq *xreq, struct json_object *obj, const char *error, const char *info, va_list args)
{
	afb_req_x2_reply_v(xreq_to_req_x2(xreq), obj, error, info, args);
}

void afb_xreq_reply_f(struct afb_xreq *xreq, struct json_object *obj, const char *error, const char *info, ...)
{
	va_list args;

	va_start(args, info);
	afb_xreq_reply_v(xreq, obj, error, info, args);
	va_end(args);
}

const char *afb_xreq_raw(struct afb_xreq *xreq, size_t *size)
{
	struct json_object *obj = xreq_json_cb(xreq_to_req_x2(xreq));
	const char *result = json_object_to_json_string_ext(obj, JSON_C_TO_STRING_NOSLASHESCAPE);
	if (size != NULL)
		*size = strlen(result);
	return result;
}

void afb_xreq_unhooked_subcall(struct afb_xreq *xreq, const char *api, const char *verb, struct json_object *args, int flags, void (*callback)(void*, struct json_object*, const char*, const char*, struct afb_req_x2 *), void *closure)
{
	xreq_subcall_cb(xreq_to_req_x2(xreq), api, verb, args, flags, callback, closure);
}

void afb_xreq_addref(struct afb_xreq *xreq)
{
	afb_req_x2_addref(xreq_to_req_x2(xreq));
}

void afb_xreq_unref(struct afb_xreq *xreq)
{
	afb_req_x2_unref(xreq_to_req_x2(xreq));
}

void afb_xreq_subcall(struct afb_xreq *xreq, const char *api, const char *verb, struct json_object *args, int flags, void (*callback)(void*, struct json_object*, const char*, const char*, struct afb_req_x2 *), void *closure)
{
	afb_req_x2_subcall(xreq_to_req_x2(xreq), api, verb, args, flags, callback, closure);
}

int afb_xreq_reply_unknown_api(struct afb_xreq *xreq)
{
	afb_xreq_reply_f(xreq, NULL, afb_error_text_unknown_api, "api %s not found (for verb %s)", xreq->request.called_api, xreq->request.called_verb);
	return X_EINVAL;
}

int afb_xreq_reply_unknown_verb(struct afb_xreq *xreq)
{
	afb_xreq_reply_f(xreq, NULL, afb_error_text_unknown_verb, "verb %s unknown within api %s", xreq->request.called_verb, xreq->request.called_api);
	return X_EINVAL;
}

int afb_xreq_reply_invalid_token(struct afb_xreq *xreq)
{
	afb_xreq_reply(xreq, NULL, afb_error_text_invalid_token, "invalid token"); /* TODO: or "no token" */
	return X_EINVAL;
}

int afb_xreq_reply_insufficient_scope(struct afb_xreq *xreq, const char *scope)
{
	afb_xreq_reply(xreq, NULL, afb_error_text_insufficient_scope, scope ?: "insufficient scope");
	return X_EPERM;
}

static void call_checked_v3(struct afb_xreq *xreq, int status, void *closure)
{
	const struct afb_verb_v3 *verb = closure;
	if (status > 0)
		verb->callback(xreq_to_req_x2(xreq));
}

void afb_xreq_call_verb_v3(struct afb_xreq *xreq, const struct afb_verb_v3 *verb)
{
	if (!verb)
		afb_xreq_reply_unknown_verb(xreq);
	else
		afb_auth_check_and_set_session_x2_async(xreq, verb->auth, verb->session, call_checked_v3, (void*)verb);
}

void afb_xreq_init(struct afb_xreq *xreq, const struct afb_xreq_query_itf *queryitf)
{
	memset(xreq, 0, sizeof *xreq);
	xreq->request.itf = &xreq_itf; /* no hook by default */
	xreq->refcount = 1;
	xreq->queryitf = queryitf;
}

#if WITH_AFB_HOOK
static void init_hooking(struct afb_xreq *xreq)
{
	afb_hook_init_xreq(xreq);
	if (xreq->hookflags) {
		xreq->request.itf = &xreq_hooked_itf; /* unhook the interface */
		afb_hook_xreq_begin(xreq);
	}
}
#endif

/**
 * job callback for asynchronous and secured processing of the request.
 */
static void process_async(int signum, void *arg)
{
	struct afb_xreq *xreq = arg;
	const struct afb_api_item *api;

	if (signum != 0) {
		/* emit the error (assumes that hooking is initialised) */
		afb_xreq_reply_f(xreq, NULL, afb_error_text_aborted, "signal %s(%d) caught", strsignal(signum), signum);
	} else {
#if WITH_AFB_HOOK
		/* init hooking */
		init_hooking(xreq);
#endif
		/* invoke api call method to process the request */
		api = (const struct afb_api_item*)xreq->context.api_key;
		api->itf->call(api->closure, xreq);
	}
	/* release the request */
	afb_xreq_unhooked_unref(xreq);
}

/**
 * Early request failure of the request 'xreq' with, as usual, 'status' and 'info'
 * The early failure occurs only in function 'afb_xreq_process' where normally,
 * the hooking is not initialised. So this "early" failure takes care to initialise
 * the hooking in first.
 */
static void early_failure(struct afb_xreq *xreq, const char *status, const char *info, ...)
{
	va_list args;

#if WITH_AFB_HOOK
	/* init hooking */
	init_hooking(xreq);
#endif

	/* send error */
	va_start(args, info);
	afb_xreq_reply_v(xreq, NULL, status, info, args);
	va_end(args);
}

/**
 * Enqueue a job for processing the request 'xreq' using the given 'apiset'.
 * Errors are reported as request failures.
 */
void afb_xreq_process(struct afb_xreq *xreq, struct afb_apiset *apiset)
{
	int rc;
	const struct afb_api_item *api;
	struct afb_xreq *caller;

	/* lookup at the api */
	xreq->apiset = apiset;
	rc = afb_apiset_get_api(apiset, xreq->request.called_api, 1, 1, &api);
	if (rc < 0) {
		if (rc == X_ENOENT)
			early_failure(xreq, "unknown-api", "api %s not found (for verb %s)", xreq->request.called_api, xreq->request.called_verb);
		else
			early_failure(xreq, "bad-api-state", "api %s not started correctly: %m", xreq->request.called_api);
		goto end;
	}
	xreq->context.api_key = api;

	/* check self locking */
	if (api->group) {
		caller = xreq->caller;
		while (caller) {
			if (((const struct afb_api_item*)caller->context.api_key)->group == api->group) {
				/* noconcurrency lock detected */
				ERROR("self-lock detected in call stack for API %s", xreq->request.called_api);
				early_failure(xreq, "self-locked", "recursive self lock, API %s", xreq->request.called_api);
				goto end;
			}
			caller = caller->caller;
		}
	}

	/* queue the request job */
	afb_xreq_unhooked_addref(xreq);
	if (afb_jobs_queue(api->group, afb_apiset_timeout_get(apiset), process_async, xreq) < 0) {
		/* TODO: allows or not to proccess it directly as when no threading? (see above) */
		ERROR("can't process job with threads: %m");
		early_failure(xreq, "cancelled", "not able to create a job for the task");
		afb_xreq_unhooked_unref(xreq);
	}
end:
	afb_xreq_unhooked_unref(xreq);
}

const char *xreq_on_behalf_cred_export(struct afb_xreq *xreq)
{
	return afb_context_on_behalf_export(&xreq->context);
}
