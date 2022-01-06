/*
 * Copyright (C) 2015-2022 IoT.bzh Company
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

#if WITH_AFB_HOOK && WITH_AFB_TRACE

#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>

#include <json-c/json.h>
#if !defined(JSON_C_TO_STRING_NOSLASHESCAPE)
#define JSON_C_TO_STRING_NOSLASHESCAPE 0
#endif

#include <afb/afb-arg.h>

#include "core/afb-hook.h"
#include "core/afb-hook-flags.h"
#include "core/afb-cred.h"
#include "core/afb-data.h"
#include "core/afb-type.h"
#include "core/afb-type-predefined.h"
#include "core/afb-json-legacy.h"
#include "core/afb-session.h"
#include "core/afb-req-common.h"
#include "core/afb-api-common.h"
#include "core/afb-evt.h"
#include "misc/afb-trace.h"

#include "utils/wrap-json.h"
#include "sys/verbose.h"
#include "sys/x-mutex.h"
#include "sys/x-errno.h"

/*******************************************************************************/
/*****  default names                                                      *****/
/*******************************************************************************/

#if !defined(DEFAULT_EVENT_NAME)
#  define DEFAULT_EVENT_NAME "trace"
#endif
#if !defined(DEFAULT_TAG_NAME)
#  define DEFAULT_TAG_NAME "trace"
#endif

/*******************************************************************************/
/*****  types                                                              *****/
/*******************************************************************************/

/* struct for tags */
struct tag {
	struct tag *next;	/* link to the next */
	char tag[];		/* name of the tag */
};

/* struct for events */
struct event {
	struct event *next;	/* link to the next event */
	struct afb_evt *evt;	/* the event */
};

/* struct for sessions */
struct cookie {
	struct afb_session *session;    /* the session */
	struct afb_trace *trace;        /* the tracer */
};

/* struct for recording hooks */
struct hook {
	struct hook *next;		/* link to next hook */
	void *handler;			/* the handler of the hook */
	struct event *event;		/* the associated event */
	struct tag *tag;		/* the associated tag */
	struct afb_session *session;	/* the associated session */
};

/* types of hooks */
enum trace_type
{
	Trace_Type_Req,			/* req hooks */
	Trace_Type_Api,			/* api hooks */
	Trace_Type_Evt,			/* evt hooks */
	Trace_Type_Session,		/* session hooks */
	Trace_Type_Global,		/* global hooks */
	Trace_Type_Count,		/* count of types of hooks */
};

/* client data */
struct afb_trace
{
	int refcount;				/* reference count */
	x_mutex_t mutex;			/* concurrency management */
	const char *apiname;			/* api name for events */
	struct afb_session *bound;		/* bound to session */
	struct event *events;			/* list of events */
	struct tag *tags;			/* list of tags */
	struct hook *hooks[Trace_Type_Count];	/* hooks */
};

/*******************************************************************************/
/*****  utility functions                                                  *****/
/*******************************************************************************/

static struct json_object *json_object_of_data_x4(struct afb_data *data)
{
	int rc;
	struct afb_data *item;
	struct json_object *result;

	rc = afb_data_convert(data, &afb_type_predefined_json_c, &item);
	if (rc < 0)
		result = NULL;
	else {
		result = (struct json_object*)afb_data_ro_pointer(item);
		result = json_object_get(result);
		afb_data_unref(item);
	}
	return result;
}

static struct json_object *json_object_of_dataset(unsigned count, struct afb_data * const dataset[])
{
	unsigned i;
	struct json_object *result;

	result = json_object_new_array();
	for (i = 0 ; i < count ; i++)
		json_object_array_add(result, json_object_of_data_x4(dataset[i]));

	return result;
}

static void ctxt_error(char **errors, const char *format, ...)
{
	int len;
	char *errs;
	size_t sz;
	char buffer[1024];
	va_list ap;

	va_start(ap, format);
	len = vsnprintf(buffer, sizeof buffer, format, ap);
	va_end(ap);
	if (len > (int)(sizeof buffer - 2))
		len = (int)(sizeof buffer - 2);
	buffer[len++] = '\n';
	buffer[len++] = 0;

	errs = *errors;
	sz = errs ? strlen(errs) : 0;
	errs = realloc(errs, sz + (size_t)len);
	if (errs) {
		memcpy(errs + sz, buffer, (size_t)len);
		*errors = errs;
	}
}

/* timestamp */
static struct json_object *timestamp(const struct afb_hookid *hookid)
{
	return json_object_new_double((double)hookid->time.tv_sec +
			(double)hookid->time.tv_nsec * .000000001);
}

/* verbosity level name or NULL */
static const char *verbosity_level_name(int level)
{
	static const char *names[] = {
		"error",
		"warning",
		"notice",
		"info",
		"debug"
	};

	return level >= Log_Level_Error && level <= Log_Level_Debug ? names[level - Log_Level_Error] : NULL;
}

/* generic hook */
static void emit(void *closure, const struct afb_hookid *hookid, const char *type, const char *fmt1, const char *fmt2, va_list ap2, ...)
{
	struct hook *hook = closure;
	struct json_object *data, *data1, *data2;
	va_list ap1;

	data1 = data2 = data = NULL;
	va_start(ap1, ap2);
	wrap_json_vpack(&data1, fmt1, ap1);
	va_end(ap1);
	if (fmt2)
		wrap_json_vpack(&data2, fmt2, ap2);

	wrap_json_pack(&data, "{so ss ss si so so*}",
					"time", timestamp(hookid),
					"tag", hook->tag->tag,
					"type", type,
					"id", (int)(hookid->id & INT_MAX),
					type, data1,
					"data", data2);

	afb_json_legacy_event_push(hook->event->evt, data);
}

/*******************************************************************************/
/*****  trace the requests                                                 *****/
/*******************************************************************************/

static void hook_req(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, const char *action, const char *format, ...)
{
#if WITH_CRED
	struct afb_cred *cred;
#endif
	struct json_object *jcred = NULL;
	const char *session = NULL;
	va_list ap;

	if (req->session)
		session = afb_session_uuid(req->session);

#if WITH_CRED
	cred = req->credentials;
	if (cred)
		wrap_json_pack(&jcred, "{si ss si si ss* ss*}",
						"uid", (int)cred->uid,
						"user", cred->user,
						"gid", (int)cred->gid,
						"pid", (int)cred->pid,
						"label", cred->label,
						"id", cred->id
					);
#endif

	va_start(ap, format);
	emit(closure, hookid, "request", "{si ss ss ss so* ss*}", format, ap,
					"index", req->hookindex,
					"api", req->apiname,
					"verb", req->verbname,
					"action", action,
					"credentials", jcred,
					"session", session);
	va_end(ap);
}

static void hook_req_begin(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req)
{
	struct json_object *params = json_object_of_dataset(req->params.ndata, req->params.data);
	hook_req(closure, hookid, req, "begin", "{sO?}", "params", params);
	json_object_put(params);
}

static void hook_req_end(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req)
{
	hook_req(closure, hookid, req, "end", NULL);
}

static void hook_req_json(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, struct json_object *obj)
{
	hook_req(closure, hookid, req, "json", "{sO?}",
						"result", obj);
}

static void hook_req_get(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, const char *name, struct afb_arg arg)
{
	hook_req(closure, hookid, req, "get", "{ss? ss? ss? ss?}",
						"query", name,
						"name", arg.name,
						"value", arg.value,
						"path", arg.path);
}

static void hook_req_reply(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, int status, unsigned nparams, struct afb_data * const params[])
{
	hook_req(closure, hookid, req, "reply", "{si sO?}",
						"status", status,
						"reply", json_object_of_dataset(nparams, params));
}

static void hook_req_addref(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req)
{
	hook_req(closure, hookid, req, "addref", NULL);
}

static void hook_req_unref(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req)
{
	hook_req(closure, hookid, req, "unref", NULL);
}

static void hook_req_session_close(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req)
{
	hook_req(closure, hookid, req, "session_close", NULL);
}

static void hook_req_session_set_LOA(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, unsigned level, int result)
{
	hook_req(closure, hookid, req, "session_set_LOA", "{si si}",
					"level", level,
					"result", result);
}

static void hook_req_session_get_LOA(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, unsigned result)
{
	hook_req(closure, hookid, req, "session_get_LOA", "{si}",
					"result", result);
}

static void hook_req_subscribe(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, struct afb_evt *event, int result)
{
	hook_req(closure, hookid, req, "subscribe", "{s{ss si} si}",
					"event",
						"name", afb_evt_fullname(event),
						"id", afb_evt_id(event),
					"result", result);
}

static void hook_req_unsubscribe(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, struct afb_evt *event, int result)
{
	hook_req(closure, hookid, req, "unsubscribe", "{s{ss? si} si}",
					"event",
						"name", afb_evt_fullname(event),
						"id", afb_evt_id(event),
					"result", result);
}

static void hook_req_subcall(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, const char *api, const char *verb, unsigned nparams, struct afb_data * const params[])
{
	hook_req(closure, hookid, req, "subcall", "{ss? ss? sO?}",
					"api", api,
					"verb", verb,
					"params", json_object_of_dataset(nparams, params));
}

static void hook_req_subcall_result(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, int status, unsigned nreplies, struct afb_data * const replies[])
{
	hook_req(closure, hookid, req, "subcall_result", "{si sO?}",
					"status", status,
					"reply", json_object_of_dataset(nreplies, replies));
}

static void hook_req_subcallsync(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, const char *api, const char *verb, unsigned nparams, struct afb_data * const params[])
{
	hook_req(closure, hookid, req, "subcallsync", "{ss? ss? sO?}",
					"api", api,
					"verb", verb,
					"params", json_object_of_dataset(nparams, params));
}

static void hook_req_subcallsync_result(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, int result, int *status, unsigned *nreplies, struct afb_data * const replies[])
{
	hook_req(closure, hookid, req, "subcallsync_result",  "{si si sO?}",
					"result", result,
					"status", *status,
					"reply", json_object_of_dataset(*nreplies, replies));
}

static void hook_req_vverbose(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, int level, const char *file, int line, const char *func, const char *fmt, va_list args)
{
	struct json_object *pos;
	int len;
	char *msg;
	va_list ap;

	pos = NULL;
	msg = NULL;

	va_copy(ap, args);
	len = vasprintf(&msg, fmt, ap);
	va_end(ap);

	if (file)
		wrap_json_pack(&pos, "{ss si ss*}", "file", file, "line", line, "function", func);

	hook_req(closure, hookid, req, "vverbose", "{si ss* ss? so*}",
					"level", level,
 					"type", verbosity_level_name(level),
					len < 0 ? "format" : "message", len < 0 ? fmt : msg,
					"position", pos);

	free(msg);
}

static void hook_req_has_permission(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, const char *permission, int result)
{
	hook_req(closure, hookid, req, "has_permission", "{ss sb}",
					"permission", permission,
					"result", result);
}

static void hook_req_get_application_id(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, char *result)
{
	hook_req(closure, hookid, req, "get_application_id", "{ss?}",
					"result", result);
}

static void hook_req_context_make(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, int replace, void *(*create_value)(void*), void (*free_value)(void*), void *create_closure, void *result)
{
	char pc[50], pf[50], pv[50], pr[50];
	snprintf(pc, sizeof pc, "%p", create_value);
	snprintf(pf, sizeof pf, "%p", free_value);
	snprintf(pv, sizeof pv, "%p", create_closure);
	snprintf(pr, sizeof pr, "%p", result);
	hook_req(closure, hookid, req, "context_make", "{sb ss ss ss ss}",
					"replace", replace,
					"create", pc,
					"free", pf,
					"closure", pv,
					"result", pr);
}

static void hook_req_context_set(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, void *value, void (*freecb)(void*), void *freeclo, int result)
{
	char pc[50], pf[50], pv[50];
	snprintf(pv, sizeof pv, "%p", value);
	snprintf(pf, sizeof pf, "%p", freecb);
	snprintf(pc, sizeof pc, "%p", freeclo);
	hook_req(closure, hookid, req, "context_set", "{ss ss ss si}",
					"free", pf,
					"closure", pc,
					"value", pv,
					"result", result);
}

static void hook_req_context_get(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, void *value, int result)
{
	char pv[50];
	snprintf(pv, sizeof pv, "%p", value);
	hook_req(closure, hookid, req, "context_get", "{ss si}",
					"value", pv,
					"result", result);
}

static void hook_req_context_getinit(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, void *value, int (*initcb)(void*,void**,void(**)(void*),void**), void *initclo, int result)
{
	char pc[50], pf[50], pv[50];
	snprintf(pv, sizeof pv, "%p", value);
	snprintf(pf, sizeof pf, "%p", initcb);
	snprintf(pc, sizeof pc, "%p", initclo);
	hook_req(closure, hookid, req, "context_getinit", "{ss ss ss si}",
					"init", pf,
					"closure", pc,
					"value", pv,
					"result", result);
}

static void hook_req_context_drop(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, int result)
{
	hook_req(closure, hookid, req, "context_drop", "{si}",
					"result", result);
}

static void hook_req_get_uid(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, int result)
{
	hook_req(closure, hookid, req, "get_uid", "{si}",
					"result", result);
}

static void hook_req_get_client_info(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, struct json_object *result)
{
	hook_req(closure, hookid, req, "get_client_info", "{sO}",
					"result", result);
}

static void hook_req_get_userdata(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, void *userdata)
{
	char pu[50];
	snprintf(pu, sizeof pu, "%p", userdata);
	hook_req(closure, hookid, req, "get_userdata", "{ss}",
					"userdata", pu);
}

static void hook_req_set_userdata(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, void *userdata, void (*freecb)(void*))
{
	char pu[50], pf[50];
	snprintf(pu, sizeof pu, "%p", userdata);
	snprintf(pf, sizeof pf, "%p", freecb);
	hook_req(closure, hookid, req, "set_userdata", "{ss ss}",
					"userdata", pu,
					"freecb", pf);
}

static void hook_req_interface_by_name(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, const char *name, void *value, int result)
{
	char pv[50];
	snprintf(pv, sizeof pv, "%p", value);
	hook_req(closure, hookid, req, "interface_by_name", "{ss ss sd}",
					"name", name,
					"interface", pv,
					"result", result);
}

static void hook_req_interface_by_id(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, int id, void *value, int result)
{
	char pv[50];
	snprintf(pv, sizeof pv, "%p", value);
	hook_req(closure, hookid, req, "interface_by_id", "{sd ss sd}",
					"id", id,
					"interface", pv,
					"result", result);
}

static struct afb_hook_req_itf hook_req_itf = {
	.hook_req_begin = hook_req_begin,
	.hook_req_end = hook_req_end,
	.hook_req_json = hook_req_json,
	.hook_req_get = hook_req_get,
	.hook_req_reply = hook_req_reply,
	.hook_req_addref = hook_req_addref,
	.hook_req_unref = hook_req_unref,
	.hook_req_session_close = hook_req_session_close,
	.hook_req_session_set_LOA = hook_req_session_set_LOA,
	.hook_req_session_get_LOA = hook_req_session_get_LOA,
	.hook_req_subscribe = hook_req_subscribe,
	.hook_req_unsubscribe = hook_req_unsubscribe,
	.hook_req_subcall = hook_req_subcall,
	.hook_req_subcall_result = hook_req_subcall_result,
	.hook_req_subcallsync = hook_req_subcallsync,
	.hook_req_subcallsync_result = hook_req_subcallsync_result,
	.hook_req_vverbose = hook_req_vverbose,
	.hook_req_has_permission = hook_req_has_permission,
	.hook_req_get_application_id = hook_req_get_application_id,
	.hook_req_context_make = hook_req_context_make,
	.hook_req_get_uid = hook_req_get_uid,
	.hook_req_get_client_info = hook_req_get_client_info,
	.hook_req_context_set = hook_req_context_set,
	.hook_req_context_get = hook_req_context_get,
	.hook_req_context_getinit = hook_req_context_getinit,
	.hook_req_context_drop = hook_req_context_drop,
	.hook_req_get_userdata = hook_req_get_userdata,
	.hook_req_set_userdata = hook_req_set_userdata,
	.hook_req_interface_by_name = hook_req_interface_by_name,
	.hook_req_interface_by_id = hook_req_interface_by_id,
};

/*******************************************************************************/
/*****  trace the api interface                                            *****/
/*******************************************************************************/

static void hook_api(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *action, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	emit(closure, hookid, "api", "{ss ss}", format, ap,
					"api", afb_api_common_apiname(comapi),
					"action", action);
	va_end(ap);
}

static void hook_api_event_broadcast_before(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *name, unsigned nparams, struct afb_data * const params[])
{
	hook_api(closure, hookid, comapi, "event_broadcast_before", "{ss sO?}",
			"name", name, "data", json_object_of_dataset(nparams, params));
}

static void hook_api_event_broadcast_after(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *name, unsigned nparams, struct afb_data * const params[], int result)
{
	hook_api(closure, hookid, comapi, "event_broadcast_after", "{ss sO? si}",
			"name", name, "data", json_object_of_dataset(nparams, params), "result", result);
}

static void hook_api_get_event_loop(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, struct sd_event *result)
{
	hook_api(closure, hookid, comapi, "get_event_loop", NULL);
}

static void hook_api_get_user_bus(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, struct sd_bus *result)
{
	hook_api(closure, hookid, comapi, "get_user_bus", NULL);
}

static void hook_api_get_system_bus(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, struct sd_bus *result)
{
	hook_api(closure, hookid, comapi, "get_system_bus", NULL);
}

static void hook_api_vverbose(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int level, const char *file, int line, const char *function, const char *fmt, va_list args)
{
	struct json_object *pos;
	int len;
	char *msg;
	va_list ap;

	pos = NULL;
	msg = NULL;

	va_copy(ap, args);
	len = vasprintf(&msg, fmt, ap);
	va_end(ap);

	if (file)
		wrap_json_pack(&pos, "{ss si ss*}", "file", file, "line", line, "function", function);

	hook_api(closure, hookid, comapi, "vverbose", "{si ss* ss? so*}",
					"level", level,
 					"type", verbosity_level_name(level),
					len < 0 ? "format" : "message", len < 0 ? fmt : msg,
					"position", pos);

	free(msg);
}

static void hook_api_event_make(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *name, struct afb_evt *result)
{
	hook_api(closure, hookid, comapi, "event_make", "{ss ss si}",
			"name", name, "event", afb_evt_fullname(result), "id", afb_evt_id(result));
}

static void hook_api_rootdir_get_fd(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result)
{
	char path[PATH_MAX], proc[100];
	const char *key, *val;
	ssize_t s;

	if (result >= 0) {
		snprintf(proc, sizeof proc, "/proc/self/fd/%d", result);
		s = readlink(proc, path, sizeof path);
		path[s < 0 ? 0 : (size_t)s >= sizeof path ? sizeof path - 1 : (size_t)s] = 0;
		key = "path";
		val = path;
	} else {
		key = "error";
		val = strerror(-result);
	}

	hook_api(closure, hookid, comapi, "rootdir_get_fd", "{ss}", key, val);
}

static void hook_api_rootdir_open_locale(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *filename, int flags, const char *locale, int result)
{
	char path[PATH_MAX], proc[100];
	const char *key, *val;
	ssize_t s;

	if (result >= 0) {
		snprintf(proc, sizeof proc, "/proc/self/fd/%d", result);
		s = readlink(proc, path, sizeof path);
		path[s < 0 ? 0 : (size_t)s >= sizeof path ? sizeof path - 1 : (size_t)s] = 0;
		key = "path";
		val = path;
	} else {
		key = "error";
		val = strerror(-result);
	}

	hook_api(closure, hookid, comapi, "rootdir_open_locale", "{ss si ss* ss}",
			"file", filename,
			"flags", flags,
			"locale", locale,
			key, val);
}

static void hook_api_post_job(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, long delayms, int timeout, void (*callback)(int signum, void *arg), void *argument, void *group, int result)
{
	hook_api(closure, hookid, comapi, "post_job", "{ss}", "result", result);
}

static void hook_api_require_api(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *name, int initialized)
{
	hook_api(closure, hookid, comapi, "require_api", "{ss sb}", "name", name, "initialized", initialized);
}

static void hook_api_require_api_result(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *name, int initialized, int result)
{
	hook_api(closure, hookid, comapi, "require_api_result", "{ss sb si}", "name", name, "initialized", initialized, "result", result);
}

static void hook_api_add_alias_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *api, const char *alias, int result)
{
	hook_api(closure, hookid, comapi, "add_alias", "{si ss? ss}", "status", result, "api", api, "alias", alias);
}

static void hook_api_start_before(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi)
{
	hook_api(closure, hookid, comapi, "start_before", NULL);
}

static void hook_api_start_after(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int status)
{
	hook_api(closure, hookid, comapi, "start_after", "{si}", "result", status);
}

static void hook_api_on_event_before(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *event, int evtid, unsigned nparams, struct afb_data * const params[])
{
	hook_api(closure, hookid, comapi, "on_event_before", "{ss si sO*}",
			"event", event, "id", evtid, "data", json_object_of_dataset(nparams, params));
}

static void hook_api_on_event_after(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *event, int evtid, unsigned nparams, struct afb_data * const params[])
{
	hook_api(closure, hookid, comapi, "on_event_after", "{ss si sO?}",
			"event", event, "id", evtid, "data", json_object_of_dataset(nparams, params));
}

static void hook_api_call(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *api, const char *verb, unsigned nparams, struct afb_data * const params[])
{
	hook_api(closure, hookid, comapi, "call", "{ss ss sO?}",
			"api", api, "verb", verb, "params", json_object_of_dataset(nparams, params));
}

static void hook_api_call_result(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int status, unsigned nreplies, struct afb_data * const replies[])
{
	hook_api(closure, hookid, comapi, "call_result", "{si sO?}",
			"status", status, "reply", json_object_of_dataset(nreplies, replies));
}

static void hook_api_callsync(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *api, const char *verb, unsigned nparams, struct afb_data * const params[])
{
	hook_api(closure, hookid, comapi, "callsync", "{ss ss sO?}",
			"api", api, "verb", verb, "params", json_object_of_dataset(nparams, params));
}

static void hook_api_callsync_result(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result, int *status, unsigned *nreplies, struct afb_data * const replies[])
{
	hook_api(closure, hookid, comapi, "callsync_result", "{si si sO?}",
			"result", result, "status", *status, "reply", json_object_of_dataset(*nreplies, replies));
}

static void hook_api_new_api_before(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *api, const char *info, int noconcurrency)
{
	hook_api(closure, hookid, comapi, "new_api.before", "{ss ss? sb}",
			"api", api, "info", info, "noconcurrency", noconcurrency);
}

static void hook_api_new_api_after(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result, const char *api)
{
	hook_api(closure, hookid, comapi, "new_api.after", "{si ss}",
						"status", result, "api", api);
}

static void hook_api_api_set_verbs_v3(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result, const struct afb_verb_v3 *verbs)
{
	hook_api(closure, hookid, comapi, "set_verbs_v3", "{si}",  "status", result);
}

static void hook_api_api_set_verbs_v4(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result, const struct afb_verb_v4 *verbs)
{
	hook_api(closure, hookid, comapi, "set_verbs_v4", "{si}",  "status", result);
}

static void hook_api_api_add_verb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result, const char *verb, const char *info, int glob)
{
	hook_api(closure, hookid, comapi, "add_verb", "{si ss ss? sb}", "status", result, "verb", verb, "info", info, "glob", glob);
}

static void hook_api_api_del_verb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result, const char *verb)
{
	hook_api(closure, hookid, comapi, "del_verb", "{si ss}", "status", result, "verb", verb);
}

static void hook_api_api_set_on_event(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result)
{
	hook_api(closure, hookid, comapi, "set_on_event", "{si}",  "status", result);
}

static void hook_api_api_set_on_init(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result)
{
	hook_api(closure, hookid, comapi, "set_on_init", "{si}",  "status", result);
}

static void hook_api_api_seal(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi)
{
	hook_api(closure, hookid, comapi, "seal", NULL);
}

static void hook_api_event_handler_add(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result, const char *pattern)
{
	hook_api(closure, hookid, comapi, "event_handler_add", "{si ss?}",  "status", result, "pattern", pattern);
}

static void hook_api_event_handler_del(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result, const char *pattern)
{
	hook_api(closure, hookid, comapi, "event_handler_del", "{si ss?}",  "status", result, "pattern", pattern);
}

static void hook_api_class_provide(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result, const char *name)
{
	hook_api(closure, hookid, comapi, "class_provide", "{si ss?}",  "status", result, "name", name);
}

static void hook_api_class_require(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result, const char *name)
{
	hook_api(closure, hookid, comapi, "class_require", "{si ss?}",  "status", result, "name", name);
}

static void hook_api_delete_api(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result)
{
	hook_api(closure, hookid, comapi, "delete_api", "{si}",  "status", result);
}

static void hook_api_on_event_handler_before(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *event, int event_x2, unsigned nparams, struct afb_data * const params[], const char *pattern)
{
	hook_api(closure, hookid, comapi, "on_event_handler.before",
		"{ss ss sO?}", "pattern", pattern, "event", event, "data", json_object_of_dataset(nparams, params));
}

static void hook_api_on_event_handler_after(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *event, int event_x2, unsigned nparams, struct afb_data * const params[], const char *pattern)
{
	hook_api(closure, hookid, comapi, "on_event_handler.after",
		"{ss ss sO?}", "pattern", pattern, "event", event, "data", json_object_of_dataset(nparams, params));
}

static void hook_api_settings(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, struct json_object *object)
{
	hook_api(closure, hookid, comapi, "settings", "{sO}", "settings", object);
}

static struct afb_hook_api_itf hook_api_itf = {
	.hook_api_event_broadcast_before = hook_api_event_broadcast_before,
	.hook_api_event_broadcast_after = hook_api_event_broadcast_after,
	.hook_api_get_event_loop = hook_api_get_event_loop,
	.hook_api_get_user_bus = hook_api_get_user_bus,
	.hook_api_get_system_bus = hook_api_get_system_bus,
	.hook_api_vverbose = hook_api_vverbose,
	.hook_api_event_make = hook_api_event_make,
	.hook_api_rootdir_get_fd = hook_api_rootdir_get_fd,
	.hook_api_rootdir_open_locale = hook_api_rootdir_open_locale,
	.hook_api_post_job = hook_api_post_job,
	.hook_api_require_api = hook_api_require_api,
	.hook_api_require_api_result = hook_api_require_api_result,
	.hook_api_add_alias = hook_api_add_alias_cb,
	.hook_api_start_before = hook_api_start_before,
	.hook_api_start_after = hook_api_start_after,
	.hook_api_on_event_before = hook_api_on_event_before,
	.hook_api_on_event_after = hook_api_on_event_after,
	.hook_api_call = hook_api_call,
	.hook_api_call_result = hook_api_call_result,
	.hook_api_callsync = hook_api_callsync,
	.hook_api_callsync_result = hook_api_callsync_result,
	.hook_api_new_api_before = hook_api_new_api_before,
	.hook_api_new_api_after = hook_api_new_api_after,
	.hook_api_api_set_verbs_v3 = hook_api_api_set_verbs_v3,
	.hook_api_api_set_verbs_v4 = hook_api_api_set_verbs_v4,
	.hook_api_api_add_verb = hook_api_api_add_verb,
	.hook_api_api_del_verb = hook_api_api_del_verb,
	.hook_api_api_set_on_event = hook_api_api_set_on_event,
	.hook_api_api_set_on_init = hook_api_api_set_on_init,
	.hook_api_api_seal = hook_api_api_seal,
	.hook_api_event_handler_add = hook_api_event_handler_add,
	.hook_api_event_handler_del = hook_api_event_handler_del,
	.hook_api_class_provide = hook_api_class_provide,
	.hook_api_class_require = hook_api_class_require,
	.hook_api_delete_api = hook_api_delete_api,
	.hook_api_on_event_handler_before = hook_api_on_event_handler_before,
	.hook_api_on_event_handler_after = hook_api_on_event_handler_after,
	.hook_api_settings = hook_api_settings,
};

/*******************************************************************************/
/*****  trace the events                                                   *****/
/*******************************************************************************/

static void hook_evt(void *closure, const struct afb_hookid *hookid, const char *evt, int id, const char *action, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	emit(closure, hookid, "event", "{si ss ss}", format, ap,
					"id", id,
					"name", evt,
					"action", action);
	va_end(ap);
}

static void hook_evt_create(void *closure, const struct afb_hookid *hookid, const char *evt, int id)
{
	hook_evt(closure, hookid, evt, id, "create", NULL);
}

static void hook_evt_push_before(void *closure, const struct afb_hookid *hookid, const char *evt, int id, unsigned nparams, struct afb_data * const params[])
{
	hook_evt(closure, hookid, evt, id, "push_before", "{sO*}", "data", json_object_of_dataset(nparams, params));
}


static void hook_evt_push_after(void *closure, const struct afb_hookid *hookid, const char *evt, int id, unsigned nparams, struct afb_data * const params[], int result)
{
	hook_evt(closure, hookid, evt, id, "push_after", "{sO* si}", "data", json_object_of_dataset(nparams, params), "result", result);
}

static void hook_evt_broadcast_before(void *closure, const struct afb_hookid *hookid, const char *evt, int id, unsigned nparams, struct afb_data * const params[])
{
	hook_evt(closure, hookid, evt, id, "broadcast_before", "{sO*}", "data", json_object_of_dataset(nparams, params));
}

static void hook_evt_broadcast_after(void *closure, const struct afb_hookid *hookid, const char *evt, int id, unsigned nparams, struct afb_data * const params[], int result)
{
	hook_evt(closure, hookid, evt, id, "broadcast_after", "{sO* si}", "data", json_object_of_dataset(nparams, params), "result", result);
}

static void hook_evt_name(void *closure, const struct afb_hookid *hookid, const char *evt, int id, const char *result)
{
	hook_evt(closure, hookid, evt, id, "name", "{ss}", "result", result);
}

static void hook_evt_addref(void *closure, const struct afb_hookid *hookid, const char *evt, int id)
{
	hook_evt(closure, hookid, evt, id, "addref", NULL);
}

static void hook_evt_unref(void *closure, const struct afb_hookid *hookid, const char *evt, int id)
{
	hook_evt(closure, hookid, evt, id, "unref", NULL);
}

static struct afb_hook_evt_itf hook_evt_itf = {
	.hook_evt_create = hook_evt_create,
	.hook_evt_push_before = hook_evt_push_before,
	.hook_evt_push_after = hook_evt_push_after,
	.hook_evt_broadcast_before = hook_evt_broadcast_before,
	.hook_evt_broadcast_after = hook_evt_broadcast_after,
	.hook_evt_name = hook_evt_name,
	.hook_evt_addref = hook_evt_addref,
	.hook_evt_unref = hook_evt_unref
};

/*******************************************************************************/
/*****  trace the sessions                                                 *****/
/*******************************************************************************/

static void hook_session(void *closure, const struct afb_hookid *hookid, struct afb_session *session, const char *action, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	emit(closure, hookid, "session", "{ss ss}", format, ap,
					"uuid", afb_session_uuid(session),
					"action", action);
	va_end(ap);
}

static void hook_session_create(void *closure, const struct afb_hookid *hookid, struct afb_session *session)
{
	hook_session(closure, hookid, session, "create", NULL);
}

static void hook_session_close(void *closure, const struct afb_hookid *hookid, struct afb_session *session)
{
	hook_session(closure, hookid, session, "close", NULL);
}

static void hook_session_destroy(void *closure, const struct afb_hookid *hookid, struct afb_session *session)
{
	hook_session(closure, hookid, session, "destroy", NULL);
}

static void hook_session_addref(void *closure, const struct afb_hookid *hookid, struct afb_session *session)
{
	hook_session(closure, hookid, session, "addref", NULL);
}

static void hook_session_unref(void *closure, const struct afb_hookid *hookid, struct afb_session *session)
{
	hook_session(closure, hookid, session, "unref", NULL);
}

static struct afb_hook_session_itf hook_session_itf = {
	.hook_session_create = hook_session_create,
	.hook_session_close = hook_session_close,
	.hook_session_destroy = hook_session_destroy,
	.hook_session_addref = hook_session_addref,
	.hook_session_unref = hook_session_unref
};

/*******************************************************************************/
/*****  trace the globals                                                  *****/
/*******************************************************************************/

static void hook_global(void *closure, const struct afb_hookid *hookid, const char *action, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	emit(closure, hookid, "global", "{ss}", format, ap, "action", action);
	va_end(ap);
}

static void hook_global_vverbose(void *closure, const struct afb_hookid *hookid, int level, const char *file, int line, const char *function, const char *fmt, va_list args)
{
	struct json_object *pos;
	int len;
	char *msg;
	va_list ap;

	pos = NULL;
	msg = NULL;

	va_copy(ap, args);
	len = vasprintf(&msg, fmt, ap);
	va_end(ap);

	if (file)
		wrap_json_pack(&pos, "{ss si ss*}", "file", file, "line", line, "function", function);

	hook_global(closure, hookid, "vverbose", "{si ss* ss? so*}",
					"level", level,
 					"type", verbosity_level_name(level),
					len < 0 ? "format" : "message", len < 0 ? fmt : msg,
					"position", pos);

	free(msg);
}

static struct afb_hook_global_itf hook_global_itf = {
	.hook_global_vverbose = hook_global_vverbose,
};

/*******************************************************************************/
/*****  abstract types                                                     *****/
/*******************************************************************************/

static
struct
{
	const char *name;
	void (*unref)(void*);
	int (*get_flag)(const char*, unsigned*);
}
abstracting[Trace_Type_Count] =
{
	[Trace_Type_Req] =
	{
		.name = "request",
		.unref =  (void(*)(void*))afb_hook_unref_req,
		.get_flag = afb_hook_flags_req_from_text
	},
	[Trace_Type_Api] =
	{
		.name = "api",
		.unref =  (void(*)(void*))afb_hook_unref_api,
		.get_flag = afb_hook_flags_api_from_text
	},
	[Trace_Type_Evt] =
	{
		.name = "event",
		.unref =  (void(*)(void*))afb_hook_unref_evt,
		.get_flag = afb_hook_flags_evt_from_text
	},
	[Trace_Type_Session] =
	{
		.name = "session",
		.unref =  (void(*)(void*))afb_hook_unref_session,
		.get_flag = afb_hook_flags_session_from_text
	},
	[Trace_Type_Global] =
	{
		.name = "global",
		.unref =  (void(*)(void*))afb_hook_unref_global,
		.get_flag = afb_hook_flags_global_from_text
	},
};

/*******************************************************************************/
/*****  handle trace data                                                  *****/
/*******************************************************************************/

/* drop hooks of 'trace' matching 'tag' and 'event' and 'session' */
static void trace_unhook(struct afb_trace *trace, struct tag *tag, struct event *event, struct afb_session *session)
{
	int i;
	struct hook *hook, **prev;

	/* remove any event */
	for (i = 0 ; i < Trace_Type_Count ; i++) {
		prev = &trace->hooks[i];
		while ((hook = *prev)) {
			if ((tag && tag != hook->tag)
			 || (event && event != hook->event)
			 || (session && session != hook->session))
				prev = &hook->next;
			else {
				*prev = hook->next;
				abstracting[i].unref(hook->handler);
				free(hook);
			}
		}
	}
}

/* cleanup: removes unused tags, events and sessions of the 'trace' */
static void trace_cleanup(struct afb_trace *trace)
{
	int i;
	struct hook *hook;
	struct tag *tag, **ptag;
	struct event *event, **pevent;

	/* clean tags */
	ptag = &trace->tags;
	while ((tag = *ptag)) {
		/* search for tag */
		for (hook = NULL, i = 0 ; !hook && i < Trace_Type_Count ; i++)
			for (hook = trace->hooks[i] ; hook && hook->tag != tag ; hook = hook->next);
		/* keep or free whether used or not */
		if (hook)
			ptag = &tag->next;
		else {
			*ptag = tag->next;
			free(tag);
		}
	}
	/* clean events */
	pevent = &trace->events;
	while ((event = *pevent)) {
		/* search for event */
		for (hook = NULL, i = 0 ; !hook && i < Trace_Type_Count ; i++)
			for (hook = trace->hooks[i] ; hook && hook->event != event ; hook = hook->next);
		/* keep or free whether used or not */
		if (hook)
			pevent = &event->next;
		else {
			*pevent = event->next;
			afb_evt_unref(event->evt);
			free(event);
		}
	}
}

/*
 * Get the tag of 'name' within 'trace'.
 * If 'alloc' isn't zero, create the tag and add it.
 */
static struct tag *trace_get_tag(struct afb_trace *trace, const char *name, int alloc)
{
	struct tag *tag;

	/* search the tag of 'name' */
	tag = trace->tags;
	while (tag && strcmp(name, tag->tag))
		tag = tag->next;

	if (!tag && alloc) {
		/* creation if needed */
		tag = malloc(sizeof * tag + 1 + strlen(name));
		if (tag) {
			strcpy(tag->tag, name);
			tag->next = trace->tags;
			trace->tags = tag;
		}
	}
	return tag;
}

/*
 * Get the event of 'name' within 'trace'.
 * If 'alloc' isn't zero, create the event and add it.
 */
static struct event *trace_get_event(struct afb_trace *trace, const char *name, int alloc)
{
	struct event *event;

	/* search the event */
	event = trace->events;
	while (event && strcmp(afb_evt_name(event->evt), name))
		event = event->next;

	if (!event && alloc) {
		event = malloc(sizeof * event);
		if (event) {
			if (afb_evt_create2(&event->evt, trace->apiname, name) >= 0) {
				event->next = trace->events;
				trace->events = event;
			} else {
				free(event);
				event = NULL;
			}
		}
	}
	return event;
}

/*
 * called on session closing
 */
static void session_closed(void *item)
{
	struct cookie *cookie = item;

	x_mutex_lock(&cookie->trace->mutex);
	trace_unhook(cookie->trace, NULL, NULL, cookie->session);
	x_mutex_unlock(&cookie->trace->mutex);
	free(cookie);
}

/*
 * records the cookie of session for tracking close
 */
static int session_open(void *closure, void **value, void (**freecb)(void*), void **freeclo)
{
	struct cookie *param = closure, *cookie;

	cookie = malloc(sizeof *cookie);
	if (!cookie)
		return X_ENOMEM;

	*cookie = *param;
	*value = *freeclo = cookie;
	*freecb = session_closed;
	return 0;
}

/*
 * Get the session of 'uuid' within 'trace'.
 * If 'alloc' isn't zero, create the session and add it.
 */
static struct afb_session *trace_get_session_by_uuid(struct afb_trace *trace, const char *uuid, int alloc)
{
	struct cookie cookie;
	int rc;

	if (!alloc)
		cookie.session = afb_session_search(uuid);
	else {
		rc = afb_session_get(&cookie.session, uuid, AFB_SESSION_TIMEOUT_DEFAULT, NULL);
		if (rc >= 0) {
			cookie.trace = trace;
			afb_session_cookie_getinit(cookie.session, cookie.trace, NULL, session_open, &cookie);
		}
	}
	return cookie.session;
}

static struct hook *trace_make_detached_hook(struct afb_trace *trace, const char *event, const char *tag)
{
	struct hook *hook;

	tag = tag ?: DEFAULT_TAG_NAME;
	event = event ?: DEFAULT_EVENT_NAME;
	hook = malloc(sizeof *hook);
	if (hook) {
		hook->tag = trace_get_tag(trace, tag, 1);
		hook->event = trace_get_event(trace, event, 1);
		hook->session = NULL;
		hook->handler = NULL;
	}
	return hook;
}

static void trace_attach_hook(struct afb_trace *trace, struct hook *hook, enum trace_type type)
{
	hook->next = trace->hooks[type];
	trace->hooks[type] = hook;
}

/*******************************************************************************/
/*****  handle client requests                                             *****/
/*******************************************************************************/

struct context
{
	struct afb_trace *trace;
	struct afb_req_common *req;
	char *errors;
};

struct desc
{
	struct context *context;
	const char *name;
	const char *tag;
	const char *uuid;
	const char *apiname;
	const char *verbname;
	const char *pattern;
	unsigned flags[Trace_Type_Count];
};

static void addhook(struct desc *desc, enum trace_type type)
{
	struct hook *hook;
	struct afb_session *session;
	struct afb_session *bind;
	struct afb_trace *trace = desc->context->trace;

	/* check permission for bound traces */
	bind = trace->bound;
	if (bind != NULL) {
		if (type != Trace_Type_Req) {
			ctxt_error(&desc->context->errors, "tracing %s is forbidden", abstracting[type].name);
			return;
		}
		if (desc->uuid) {
			ctxt_error(&desc->context->errors, "setting session is forbidden");
			return;
		}
	}

	/* allocate the hook */
	hook = trace_make_detached_hook(trace, desc->name, desc->tag);
	if (!hook) {
		ctxt_error(&desc->context->errors, "allocation of hook failed");
		return;
	}

	/* create the hook handler */
	switch (type) {
	case Trace_Type_Req:
		if (!desc->uuid)
			session = afb_session_addref(bind);
		else {
			session = trace_get_session_by_uuid(trace, desc->uuid, 1);
			if (!session) {
				ctxt_error(&desc->context->errors, "allocation of session failed");
				free(hook);
				return;
			}
		}
		hook->handler = afb_hook_create_req(desc->apiname, desc->verbname, session,
				desc->flags[type], &hook_req_itf, hook);
		afb_session_unref(session);
		break;
	case Trace_Type_Api:
		hook->handler = afb_hook_create_api(desc->apiname, desc->flags[type], &hook_api_itf, hook);
		break;
	case Trace_Type_Evt:
		hook->handler = afb_hook_create_evt(desc->pattern, desc->flags[type], &hook_evt_itf, hook);
		break;
	case Trace_Type_Session:
		hook->handler = afb_hook_create_session(desc->uuid, desc->flags[type], &hook_session_itf, hook);
		break;
	case Trace_Type_Global:
		hook->handler = afb_hook_create_global(desc->flags[type], &hook_global_itf, hook);
		break;
	default:
		break;
	}
	if (!hook->handler) {
		ctxt_error(&desc->context->errors, "creation of hook failed");
		free(hook);
		return;
	}

	/* attach and activate the hook */
	afb_req_common_subscribe(desc->context->req, hook->event->evt);
	trace_attach_hook(trace, hook, type);
}

static void addhooks(struct desc *desc)
{
	int i;

	for (i = 0 ; i < Trace_Type_Count ; i++) {
		if (desc->flags[i])
			addhook(desc, i);
	}
}

static void add_flags(void *closure, struct json_object *object, enum trace_type type)
{
	int rc;
	unsigned flags;
	const char *name, *queried;
	struct desc *desc = closure;

	if (wrap_json_unpack(object, "s", &name))
		ctxt_error(&desc->context->errors, "unexpected %s value %s",
					abstracting[type].name,
					json_object_to_json_string_ext(object, JSON_C_TO_STRING_NOSLASHESCAPE));
	else {
		queried = (name[0] == '*' && !name[1]) ? "all" : name;
		rc = abstracting[type].get_flag(queried, &flags);
		if (rc >= 0)
			desc->flags[type] |= flags;
		else
			ctxt_error(&desc->context->errors, "unknown %s name %s",
					abstracting[type].name, name);
	}
}

static void add_req_flags(void *closure, struct json_object *object)
{
	add_flags(closure, object, Trace_Type_Req);
}

static void add_api_flags(void *closure, struct json_object *object)
{
	add_flags(closure, object, Trace_Type_Api);
}

static void add_evt_flags(void *closure, struct json_object *object)
{
	add_flags(closure, object, Trace_Type_Evt);
}

static void add_session_flags(void *closure, struct json_object *object)
{
	add_flags(closure, object, Trace_Type_Session);
}

static void add_global_flags(void *closure, struct json_object *object)
{
	add_flags(closure, object, Trace_Type_Global);
}

/* add hooks */
static void add(void *closure, struct json_object *object)
{
	int rc;
	struct desc desc;
	struct json_object *request, *event, *sub, *global, *session, *api;

	memcpy (&desc, closure, sizeof desc);
	request = event = sub = global = session = api = NULL;

	rc = wrap_json_unpack(object, "{s?s s?s s?s s?s s?s s?s s?o s?o s?o s?o s?o s?o}",
			"name", &desc.name,
			"tag", &desc.tag,
			"apiname", &desc.apiname,
			"verbname", &desc.verbname,
			"uuid", &desc.uuid,
			"pattern", &desc.pattern,
			"api", &api,
			"request", &request,
			"event", &event,
			"session", &session,
			"global", &global,
			"for", &sub);

	if (!rc) {
		/* replace stars */
		if (desc.apiname && desc.apiname[0] == '*' && !desc.apiname[1])
			desc.apiname = NULL;

		if (desc.verbname && desc.verbname[0] == '*' && !desc.verbname[1])
			desc.verbname = NULL;

		if (desc.uuid && desc.uuid[0] == '*' && !desc.uuid[1])
			desc.uuid = NULL;

		/* get what is expected */
		if (request)
			wrap_json_optarray_for_all(request, add_req_flags, &desc);

		if (api)
			wrap_json_optarray_for_all(api, add_api_flags, &desc);

		if (event)
			wrap_json_optarray_for_all(event, add_evt_flags, &desc);

		if (session)
			wrap_json_optarray_for_all(session, add_session_flags, &desc);

		if (global)
			wrap_json_optarray_for_all(global, add_global_flags, &desc);

		/* apply */
		if (sub)
			wrap_json_optarray_for_all(sub, add, &desc);
		else
			addhooks(&desc);
	}
	else {
		wrap_json_optarray_for_all(object, add_req_flags, &desc);
		addhooks(&desc);
	}
}

/* drop hooks of given tag */
static void drop_tag(void *closure, struct json_object *object)
{
	int rc;
	struct context *context = closure;
	struct tag *tag;
	const char *name;

	rc = wrap_json_unpack(object, "s", &name);
	if (rc)
		ctxt_error(&context->errors, "unexpected tag value %s", json_object_to_json_string_ext(object, JSON_C_TO_STRING_NOSLASHESCAPE));
	else {
		tag = trace_get_tag(context->trace, name, 0);
		if (!tag)
			ctxt_error(&context->errors, "tag %s not found", name);
		else
			trace_unhook(context->trace, tag, NULL, NULL);
	}
}

/* drop hooks of given event */
static void drop_event(void *closure, struct json_object *object)
{
	int rc;
	struct context *context = closure;
	struct event *event;
	const char *name;

	rc = wrap_json_unpack(object, "s", &name);
	if (rc)
		ctxt_error(&context->errors, "unexpected event value %s", json_object_to_json_string_ext(object, JSON_C_TO_STRING_NOSLASHESCAPE));
	else {
		event = trace_get_event(context->trace, name, 0);
		if (!event)
			ctxt_error(&context->errors, "event %s not found", name);
		else
			trace_unhook(context->trace, NULL, event, NULL);
	}
}

/* drop hooks of given session */
static void drop_session(void *closure, struct json_object *object)
{
	int rc;
	struct context *context = closure;
	struct afb_session *session;
	const char *uuid;

	rc = wrap_json_unpack(object, "s", &uuid);
	if (rc)
		ctxt_error(&context->errors, "unexpected session value %s", json_object_to_json_string_ext(object, JSON_C_TO_STRING_NOSLASHESCAPE));
	else {
		session = trace_get_session_by_uuid(context->trace, uuid, 0);
		if (!session)
			ctxt_error(&context->errors, "session %s not found", uuid);
		else {
			trace_unhook(context->trace, NULL, NULL, session);
			afb_session_unref(session);
		}
	}
}

/*******************************************************************************/
/*****  public interface                                                   *****/
/*******************************************************************************/

/* allocates an afb_trace instance */
struct afb_trace *afb_trace_create(const char *apiname, struct afb_session *bound)
{
	struct afb_trace *trace;

	assert(apiname);

	trace = calloc(1, sizeof *trace);
	if (trace) {
		trace->refcount = 1;
		trace->bound = bound;
		trace->apiname = apiname;
		x_mutex_init(&trace->mutex);
	}
	return trace;
}

/* add a reference to the trace */
void afb_trace_addref(struct afb_trace *trace)
{
	__atomic_add_fetch(&trace->refcount, 1, __ATOMIC_RELAXED);
}

/* drop one reference to the trace */
void afb_trace_unref(struct afb_trace *trace)
{
	if (trace && !__atomic_sub_fetch(&trace->refcount, 1, __ATOMIC_RELAXED)) {
		/* clean hooks */
		trace_unhook(trace, NULL, NULL, NULL);
		trace_cleanup(trace);
		x_mutex_destroy(&trace->mutex);
		free(trace);
	}
}

/* add traces */
int afb_trace_add(struct afb_req_common *req, struct json_object *args, struct afb_trace *trace)
{
	struct context context;
	struct desc desc;

	memset(&context, 0, sizeof context);
	context.trace = trace;
	context.req = req;

	memset(&desc, 0, sizeof desc);
	desc.context = &context;

	x_mutex_lock(&trace->mutex);
	wrap_json_optarray_for_all(args, add, &desc);
	x_mutex_unlock(&trace->mutex);

	if (!context.errors)
		return 0;

	afb_json_legacy_req_reply_hookable(req, NULL, "error-detected", context.errors);
	free(context.errors);
	return -1;
}

/* drop traces */
int afb_trace_drop(struct afb_req_common *req, struct json_object *args, struct afb_trace *trace)
{
	int rc;
	struct context context;
	struct json_object *tags, *events, *uuids;

	memset(&context, 0, sizeof context);
	context.trace = trace;
	context.req = req;

	/* special: boolean value */
	if (!wrap_json_unpack(args, "b", &rc)) {
		if (rc) {
			x_mutex_lock(&trace->mutex);
			trace_unhook(trace, NULL, NULL, NULL);
			trace_cleanup(trace);
			x_mutex_unlock(&trace->mutex);
		}
		return 0;
	}

	tags = events = uuids = NULL;
	rc = wrap_json_unpack(args, "{s?o s?o s?o}",
			"event", &events,
			"tag", &tags,
			"uuid", &uuids);

	if (rc < 0 || !(events || tags || uuids)) {
		afb_json_legacy_req_reply_hookable(req, NULL, "error-detected", "bad drop arguments");
		return -1;
	}

	x_mutex_lock(&trace->mutex);

	if (tags)
		wrap_json_optarray_for_all(tags, drop_tag, &context);

	if (events)
		wrap_json_optarray_for_all(events, drop_event, &context);

	if (uuids)
		wrap_json_optarray_for_all(uuids, drop_session, &context);

	trace_cleanup(trace);

	x_mutex_unlock(&trace->mutex);

	if (!context.errors)
		return 0;

	afb_json_legacy_req_reply_hookable(req, NULL, "error-detected", context.errors);
	free(context.errors);
	return -1;
}

#endif
