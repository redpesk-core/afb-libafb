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

#if WITH_AFB_HOOK  /***********************************************************/


#include <limits.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include <json-c/json.h>
#if !defined(JSON_C_TO_STRING_NOSLASHESCAPE)
#define JSON_C_TO_STRING_NOSLASHESCAPE 0
#endif

#include <afb/afb-arg.h>
#include <afb/afb-event-x2.h>
#include <afb/afb-binding-x4.h>

#include "core/afb-hook.h"
#include "core/afb-session.h"
#include "core/afb-cred.h"
#include "core/afb-req-common.h"
#include "core/afb-api-common.h"
#include "core/afb-evt.h"
#include "core/afb-apiname.h"

#include "utils/globmatch.h"
#include "utils/namecmp.h"
#include "sys/verbose.h"
#include "sys/x-uio.h"
#include "sys/x-mutex.h"
#include "sys/x-rwlock.h"

#define MATCHNAME(pattern,string)  !fnmatch(pattern,string,NAME_FOLD_FNM|FNM_EXTMATCH|FNM_PERIOD)
#define MATCHVALUE(pattern,string) !fnmatch(pattern,string,FNM_EXTMATCH|FNM_PERIOD)

#define MATCHN(pattern,string,def)   ((pattern) ? MATCHNAME(pattern,string) : (def))
#define MATCHV(pattern,string,def)   ((pattern) ? MATCHVALUE(pattern,string) : (def))

#define MATCH_APINN(pattern,string)	MATCHN(pattern,string,afb_apiname_is_public(string))
#define MATCH_API(pattern,string)	MATCH_APINN(pattern,(string)?:"(null)")
#define MATCH_VERB(pattern,string)	MATCHN(pattern,string,1)
#define MATCH_EVENT(pattern,string)	MATCHN(pattern,string,1)
#define MATCH_SESSION(pattern,string)	MATCHV(pattern,string,1)

/**
 * Definition of a hook for req
 */
struct afb_hook_req {
	struct afb_hook_req *next; /**< next hook */
	unsigned refcount; /**< reference count */
	unsigned flags; /**< hook flags */
	char *api; /**< api hooked or NULL for any */
	char *verb; /**< verb hooked or NULL for any */
	struct afb_session *session; /**< session hooked or NULL if any */
	struct afb_hook_req_itf *itf; /**< interface of hook */
	void *closure; /**< closure for callbacks */
};

/**
 * Definition of a hook for comapi
 */
struct afb_hook_api {
	struct afb_hook_api *next; /**< next hook */
	unsigned refcount; /**< reference count */
	unsigned flags; /**< hook flags */
	char *api; /**< api hooked or NULL for any */
	struct afb_hook_api_itf *itf; /**< interface of hook */
	void *closure; /**< closure for callbacks */
};

/**
 * Definition of a hook for evt
 */
struct afb_hook_evt {
	struct afb_hook_evt *next; /**< next hook */
	unsigned refcount; /**< reference count */
	unsigned flags; /**< hook flags */
	char *pattern; /**< event pattern name hooked or NULL for any */
	struct afb_hook_evt_itf *itf; /**< interface of hook */
	void *closure; /**< closure for callbacks */
};

/**
 * Definition of a hook for session
 */
struct afb_hook_session {
	struct afb_hook_session *next; /**< next hook */
	unsigned refcount; /**< reference count */
	unsigned flags; /**< hook flags */
	char *pattern; /**< event pattern name hooked or NULL for any */
	struct afb_hook_session_itf *itf; /**< interface of hook */
	void *closure; /**< closure for callbacks */
};

/**
 * Definition of a hook for global
 */
struct afb_hook_global {
	struct afb_hook_global *next; /**< next hook */
	unsigned refcount; /**< reference count */
	unsigned flags; /**< hook flags */
	struct afb_hook_global_itf *itf; /**< interface of hook */
	void *closure; /**< closure for callbacks */
};

/* synchronization across threads */
static x_rwlock_t rwlock = X_RWLOCK_INITIALIZER;
static x_mutex_t mutex = X_MUTEX_INITIALIZER;

/* list of hooks for req */
static struct afb_hook_req *list_of_req_hooks = NULL;

/* list of hooks for comapi */
static struct afb_hook_api *list_of_api_hooks = NULL;

/* list of hooks for evt */
static struct afb_hook_evt *list_of_evt_hooks = NULL;

/* list of hooks for session */
static struct afb_hook_session *list_of_session_hooks = NULL;

/* list of hooks for global */
static struct afb_hook_global *list_of_global_hooks = NULL;

/* hook id */
static unsigned next_hookid = 0;

/******************************************************************************
 * section: hook id
 *****************************************************************************/
static void init_hookid(struct afb_hookid *hookid)
{
	hookid->id = __atomic_add_fetch(&next_hookid, 1, __ATOMIC_RELAXED);
	clock_gettime(CLOCK_REALTIME, &hookid->time);
}

/******************************************************************************
 * section: default callbacks for tracing requests
 *****************************************************************************/

static char *_pbuf_(const char *fmt, va_list args, char **palloc, char *sbuf, size_t szsbuf, size_t *outlen)
{
	int rc;
	va_list cp;

	*palloc = NULL;
	va_copy(cp, args);
	rc = vsnprintf(sbuf, szsbuf, fmt, args);
	if ((size_t)rc >= szsbuf) {
		sbuf[szsbuf-1] = 0;
		sbuf[szsbuf-2] = sbuf[szsbuf-3] = sbuf[szsbuf-4] = '.';
		rc = vasprintf(palloc, fmt, cp);
		if (rc >= 0)
			sbuf = *palloc;
	}
	va_end(cp);
	if (rc >= 0 && outlen)
		*outlen = (size_t)rc;
	return sbuf;
}

static void _hook_(const char *fmt1, const char *fmt2, va_list arg2, ...)
{
	static const char chars[] = "HOOK: [] \n";
	char *mem1, *mem2, buf1[256], buf2[2000];
	struct iovec iov[5];
	va_list arg1;

	/* "HOOK: [" */
	iov[0].iov_base = (void*)&chars[0];
	iov[0].iov_len = 7;

	/* fmt1 ... */
	va_start(arg1, arg2);
	iov[1].iov_base = _pbuf_(fmt1, arg1, &mem1, buf1, sizeof buf1, &iov[1].iov_len);
	va_end(arg1);

	/* "] " */
	iov[2].iov_base = (void*)&chars[7];
	iov[2].iov_len = 2;

	/* fmt2 arg2 */
	iov[3].iov_base = _pbuf_(fmt2, arg2, &mem2, buf2, sizeof buf2, &iov[3].iov_len);

	/* "\n" */
	iov[4].iov_base = (void*)&chars[9];
	iov[4].iov_len = 1;

	(void)writev(2, iov, 5);

	free(mem1);
	free(mem2);
}

static void _hook_req_(const struct afb_req_common *req, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	_hook_("req-%06d:%s/%s", format, ap, req->hookindex, req->apiname, req->verbname);
	va_end(ap);
}

static void hook_req_begin_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req)
{
#if WITH_CRED
	struct afb_cred *cred = req->credentials;

	if (!cred)
		_hook_req_(req, "BEGIN");
	else
		_hook_req_(req, "BEGIN uid=%d=%s gid=%d pid=%d label=%s id=%s",
			(int)cred->uid,
			cred->user,
			(int)cred->gid,
			(int)cred->pid,
			cred->label?:"(null)",
			cred->id?:"(null)"
		);
#else
	_hook_req_(req, "BEGIN");
#endif
}

static void hook_req_end_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req)
{
	_hook_req_(req, "END");
}

static void hook_req_json_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, struct json_object *obj)
{
	_hook_req_(req, "json() -> %s", json_object_to_json_string_ext(obj, JSON_C_TO_STRING_NOSLASHESCAPE));
}

static void hook_req_get_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, const char *name, struct afb_arg arg)
{
	_hook_req_(req, "get(%s) -> { name: %s, value: %s, path: %s }", name, arg.name, arg.value, arg.path);
}

static void hook_req_reply_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, int status, unsigned nparams, struct afb_data * const params[])
{
	_hook_req_(req, "reply[%s: %d]", status >= 0 ? "success" : "error", status); /* TODO */
}

static void hook_req_addref_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req)
{
	_hook_req_(req, "addref()");
}

static void hook_req_unref_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req)
{
	_hook_req_(req, "unref()");
}

static void hook_req_session_close_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req)
{
	_hook_req_(req, "session_close()");
}

static void hook_req_session_set_LOA_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, unsigned level, int result)
{
	_hook_req_(req, "session_set_LOA(%u) -> %d", level, result);
}

static void hook_req_session_get_LOA_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, unsigned result)
{
	_hook_req_(req, "session_get_LOA -> %u", result);
}

static void hook_req_subscribe_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, struct afb_evt *evt, int result)
{
	_hook_req_(req, "subscribe(%s:%d) -> %d", afb_evt_fullname(evt), afb_evt_id(evt), result);
}

static void hook_req_unsubscribe_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, struct afb_evt *evt, int result)
{
	_hook_req_(req, "unsubscribe(%s:%d) -> %d", afb_evt_fullname(evt), afb_evt_id(evt), result);
}

static void hook_req_subcall_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, const char *api, const char *verb, unsigned nparams, struct afb_data * const params[])
{
	_hook_req_(req, "subcall(%s/%s) ...", api, verb);
}

static void hook_req_subcall_result_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, int status, unsigned nreplies, struct afb_data * const replies[])
{
	_hook_req_(req, "    ...subcall... [%s: %d]", status < 0 ? "error" : "success", status); /* TODO */
}

static void hook_req_subcallsync_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, const char *api, const char *verb, unsigned nparams, struct afb_data * const params[])
{
	_hook_req_(req, "subcallsync(%s/%s) ...", api, verb);
}

static void hook_req_subcallsync_result_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, int result, int *status, unsigned *nreplies, struct afb_data * const replies[])
{
	_hook_req_(req, "    ...subcallsync... [%s: %d]", !status ? "?" : *status < 0 ? "error" : "success", status ? *status : 0); /* TODO */
}

static void hook_req_vverbose_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, int level, const char *file, int line, const char *func, const char *fmt, va_list args)
{
	int len;
	char *msg;
	va_list ap;

	va_copy(ap, args);
	len = vasprintf(&msg, fmt, ap);
	va_end(ap);

	if (len < 0)
		_hook_req_(req, "vverbose(%d:%s, %s, %d, %s) -> %s ? ? ?", level, verbose_name_of_level(level), file, line, func, fmt);
	else {
		_hook_req_(req, "vverbose(%d:%s, %s, %d, %s) -> %s", level, verbose_name_of_level(level), file, line, func, msg);
		free(msg);
	}
}

static void hook_req_has_permission_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, const char *permission, int result)
{
	_hook_req_(req, "has_permission(%s) -> %d", permission, result);
}

static void hook_req_get_application_id_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, char *result)
{
	_hook_req_(req, "get_application_id() -> %s", result);
}

static void hook_req_context_make_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, int replace, void *(*create_value)(void*), void (*free_value)(void*), void *create_closure, void *result)
{
	_hook_req_(req, "context_make(replace=%s, %p, %p, %p) -> %p", replace?"yes":"no", create_value, free_value, create_closure, result);
}

static void hook_req_get_uid_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, int result)
{
	_hook_req_(req, "get_uid() -> %d", result);
}

static void hook_req_get_client_info_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, struct json_object *result)
{
	_hook_req_(req, "get_client_info() -> %s", json_object_to_json_string_ext(result, JSON_C_TO_STRING_NOSLASHESCAPE));
}

static void hook_req_context_set_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, void *value, void (*freecb)(void*), void *freeclo, int result)
{
	_hook_req_(req, "context_set(%p, %p, %p) -> %d", value, freecb, freeclo, result);
}

static void hook_req_context_get_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, void *value, int result)
{
	_hook_req_(req, "context_get -> %d, %p", result, value);
}

static void hook_req_context_getinit_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, void *value, int (*initcb)(void*,void**,void(**)(void*),void**), void *initclo, int result)
{
	_hook_req_(req, "context_getinit(%p, %p) -> %d, %p", initcb, initclo, result, value);
}

static void hook_req_context_drop_cb(void *closure, const struct afb_hookid *hookid, const struct afb_req_common *req, int result)
{
	_hook_req_(req, "context_drop -> %d", result);
}

static struct afb_hook_req_itf hook_req_default_itf = {
	.hook_req_begin = hook_req_begin_cb,
	.hook_req_end = hook_req_end_cb,
	.hook_req_json = hook_req_json_cb,
	.hook_req_get = hook_req_get_cb,
	.hook_req_reply = hook_req_reply_cb,
	.hook_req_addref = hook_req_addref_cb,
	.hook_req_unref = hook_req_unref_cb,
	.hook_req_session_close = hook_req_session_close_cb,
	.hook_req_session_set_LOA = hook_req_session_set_LOA_cb,
	.hook_req_session_get_LOA = hook_req_session_get_LOA_cb,
	.hook_req_subscribe = hook_req_subscribe_cb,
	.hook_req_unsubscribe = hook_req_unsubscribe_cb,
	.hook_req_subcall = hook_req_subcall_cb,
	.hook_req_subcall_result = hook_req_subcall_result_cb,
	.hook_req_subcallsync = hook_req_subcallsync_cb,
	.hook_req_subcallsync_result = hook_req_subcallsync_result_cb,
	.hook_req_vverbose = hook_req_vverbose_cb,
	.hook_req_has_permission = hook_req_has_permission_cb,
	.hook_req_get_application_id = hook_req_get_application_id_cb,
	.hook_req_context_make = hook_req_context_make_cb,
	.hook_req_get_uid = hook_req_get_uid_cb,
	.hook_req_get_client_info = hook_req_get_client_info_cb,
	.hook_req_context_set = hook_req_context_set_cb,
	.hook_req_context_get = hook_req_context_get_cb,
	.hook_req_context_getinit = hook_req_context_getinit_cb,
	.hook_req_context_drop = hook_req_context_drop_cb,
};

/******************************************************************************
 * section: hooks for tracing requests
 *****************************************************************************/

#define _HOOK_XREQ_2_(flag,func,...)   \
	struct afb_hook_req *hook; \
	struct afb_hookid hookid; \
	x_rwlock_rdlock(&rwlock); \
	init_hookid(&hookid); \
	hook = list_of_req_hooks; \
	while (hook) { \
		if (hook->refcount \
		 && hook->itf->hook_req_##func \
		 && (hook->flags & afb_hook_flag_req_##flag) != 0 \
		 && (!hook->session || hook->session == req->session) \
		 && MATCH_API(hook->api, req->apiname) \
		 && MATCH_VERB(hook->verb, req->verbname)) { \
			hook->itf->hook_req_##func(hook->closure, &hookid, __VA_ARGS__); \
		} \
		hook = hook->next; \
	} \
	x_rwlock_unlock(&rwlock);

#define _HOOK_XREQ_(what,...)   _HOOK_XREQ_2_(what,what,__VA_ARGS__)

void afb_hook_req_begin(const struct afb_req_common *req)
{
	_HOOK_XREQ_(begin, req);
}

void afb_hook_req_end(const struct afb_req_common *req)
{
	_HOOK_XREQ_(end, req);
}

struct json_object *afb_hook_req_json(const struct afb_req_common *req, struct json_object *obj)
{
	_HOOK_XREQ_(json, req, obj);
	return obj;
}

struct afb_arg afb_hook_req_get(const struct afb_req_common *req, const char *name, struct afb_arg arg)
{
	_HOOK_XREQ_(get, req, name, arg);
	return arg;
}

void afb_hook_req_reply(const struct afb_req_common *req, int status, unsigned nparams, struct afb_data * const params[])
{
	_HOOK_XREQ_(reply, req, status, nparams, params);
}

void afb_hook_req_addref(const struct afb_req_common *req)
{
	_HOOK_XREQ_(addref, req);
}

void afb_hook_req_unref(const struct afb_req_common *req)
{
	_HOOK_XREQ_(unref, req);
}

void afb_hook_req_session_close(const struct afb_req_common *req)
{
	_HOOK_XREQ_(session_close, req);
}

int afb_hook_req_session_set_LOA(const struct afb_req_common *req, unsigned level, int result)
{
	_HOOK_XREQ_(session_set_LOA, req, level, result);
	return result;
}

unsigned afb_hook_req_session_get_LOA(const struct afb_req_common *req, unsigned result)
{
	_HOOK_XREQ_(session_get_LOA, req, result);
	return result;
}

int afb_hook_req_subscribe(const struct afb_req_common *req, struct afb_evt *evt, int result)
{
	_HOOK_XREQ_(subscribe, req, evt, result);
	return result;
}

int afb_hook_req_unsubscribe(const struct afb_req_common *req, struct afb_evt *evt, int result)
{
	_HOOK_XREQ_(unsubscribe, req, evt, result);
	return result;
}

void afb_hook_req_subcall(const struct afb_req_common *req, const char *api, const char *verb, unsigned nparams, struct afb_data * const params[], int flags)
{
	_HOOK_XREQ_(subcall, req, api, verb, nparams, params);
}

void afb_hook_req_subcall_result(const struct afb_req_common *req, int status, unsigned nreplies, struct afb_data * const replies[])
{
	_HOOK_XREQ_(subcall_result, req, status, nreplies, replies);
}

void afb_hook_req_subcallsync(const struct afb_req_common *req, const char *api, const char *verb, unsigned nparams, struct afb_data * const params[], int flags)
{
	_HOOK_XREQ_(subcallsync, req, api, verb, nparams, params);
}

int  afb_hook_req_subcallsync_result(const struct afb_req_common *req, int result, int *status, unsigned *nreplies, struct afb_data * const replies[])
{
	_HOOK_XREQ_(subcallsync_result, req, result, status, nreplies, replies);
	return result;
}

void afb_hook_req_vverbose(const struct afb_req_common *req, int level, const char *file, int line, const char *func, const char *fmt, va_list args)
{
	_HOOK_XREQ_(vverbose, req, level, file ?: "?", line, func ?: "?", fmt, args);
}

int afb_hook_req_has_permission(const struct afb_req_common *req, const char *permission, int result)
{
	_HOOK_XREQ_(has_permission, req, permission, result);
	return result;
}

char *afb_hook_req_get_application_id(const struct afb_req_common *req, char *result)
{
	_HOOK_XREQ_(get_application_id, req, result);
	return result;
}

void *afb_hook_req_context_make(const struct afb_req_common *req, int replace, void *(*create_value)(void*), void (*free_value)(void*), void *create_closure, void *result)
{
	_HOOK_XREQ_(context_make, req, replace, create_value, free_value, create_closure, result);
	return result;
}

int afb_hook_req_get_uid(const struct afb_req_common *req, int result)
{
	_HOOK_XREQ_(get_uid, req, result);
	return result;
}

struct json_object *afb_hook_req_get_client_info(const struct afb_req_common *req, struct json_object *result)
{
	_HOOK_XREQ_(get_client_info, req, result);
	return result;
}

int afb_hook_req_context_set(const struct afb_req_common *req, void *value, void (*freecb)(void*), void *freeclo, int result)
{
	_HOOK_XREQ_(context_set, req, value, freecb, freeclo, result);
	return result;
}

int afb_hook_req_context_get(const struct afb_req_common *req, void *value, int result)
{
	_HOOK_XREQ_(context_get, req, value, result);
	return result;
}

int afb_hook_req_context_getinit(const struct afb_req_common *req, void *value, int (*initcb)(void*, void**, void(**)(void*), void**), void *initclo, int result)
{
	_HOOK_XREQ_(context_getinit, req, value, initcb, initclo, result);
	return result;
}

int afb_hook_req_context_drop(const struct afb_req_common *req, int result)
{
	_HOOK_XREQ_(context_drop, req, result);
	return result;
}



/******************************************************************************
 * section: hooking reqs
 *****************************************************************************/

void afb_hook_init_req(struct afb_req_common *req)
{
	static unsigned reqindex = 0;

	unsigned int f, flags, x;
	int add;
	struct afb_hook_req *hook;

	/* scan hook list to get the expected flags */
	flags = 0;
	x_rwlock_rdlock(&rwlock);
	hook = list_of_req_hooks;
	while (hook) {
		f = hook->flags;
		add = f != 0
		   && (!hook->session || hook->session == req->session)
		   && MATCH_API(hook->api, req->apiname)
		   && MATCH_VERB(hook->verb, req->verbname);
		if (add)
			flags |= f;
		hook = hook->next;
	}
	x_rwlock_unlock(&rwlock);

	/* store the hooking data */
	req->hookflags = flags;
	if (flags) {
		do {
			x = __atomic_load_n(&reqindex, __ATOMIC_RELAXED);
			req->hookindex = (x + 1) % 1000000 ?: 1;
		} while (x != __atomic_exchange_n(&reqindex, req->hookindex, __ATOMIC_RELAXED));
	}
}

struct afb_hook_req *afb_hook_create_req(const char *api, const char *verb, struct afb_session *session, unsigned flags, struct afb_hook_req_itf *itf, void *closure)
{
	struct afb_hook_req *hook;

	/* alloc the result */
	hook = calloc(1, sizeof *hook);
	if (hook == NULL)
		return NULL;

	/* get a copy of the names */
	hook->api = api ? strdup(api) : NULL;
	hook->verb = verb ? strdup(verb) : NULL;
	if ((api && !hook->api) || (verb && !hook->verb)) {
		free(hook->api);
		free(hook->verb);
		free(hook);
		return NULL;
	}

	/* initialise the rest */
	hook->session = session;
	if (session)
		afb_session_addref(session);
	hook->refcount = 1;
	hook->flags = flags & afb_hook_flags_req_all;
	hook->itf = itf ? itf : &hook_req_default_itf;
	hook->closure = closure;

	/* record the hook */
	x_mutex_lock(&mutex);
	hook->next = list_of_req_hooks;
	list_of_req_hooks = hook;
	x_mutex_unlock(&mutex);

	/* returns it */
	return hook;
}

struct afb_hook_req *afb_hook_addref_req(struct afb_hook_req *hook)
{
	__atomic_add_fetch(&hook->refcount, 1, __ATOMIC_RELAXED);
	return hook;
}

static void hook_clean_req()
{
	struct afb_hook_req **prv, *hook, *head;

	if (x_rwlock_trywrlock(&rwlock) == 0) {
		/* unlink under mutex */
		head = NULL;
		x_mutex_lock(&mutex);
		prv = &list_of_req_hooks;
		while ((hook = *prv)) {
			if (hook->refcount)
				prv = &(*prv)->next;
			else {
				*prv = hook->next;
				hook->next = head;
				head = hook;
			}
		}
		x_mutex_unlock(&mutex);
		x_rwlock_unlock(&rwlock);

		/* free found hooks */
		while((hook = head)) {
			head = hook->next;
			free(hook->api);
			free(hook->verb);
			if (hook->session)
				afb_session_unref(hook->session);
			free(hook);
		}
	}
}

void afb_hook_unref_req(struct afb_hook_req *hook)
{
	if (hook && !__atomic_sub_fetch(&hook->refcount, 1, __ATOMIC_RELAXED))
		hook_clean_req();
}

/******************************************************************************
 * section: default callbacks for tracing daemon interface
 *****************************************************************************/

static void _hook_api_(const struct afb_api_common *comapi, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	_hook_("api-%s", format, ap, afb_api_common_visible_name(comapi));
	va_end(ap);
}

static void hook_api_event_broadcast_before_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *name, unsigned nparams, struct afb_data * const params[])
{
	_hook_api_(comapi, "event_broadcast.before(%s)....", name);
}

static void hook_api_event_broadcast_after_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *name, unsigned nparams, struct afb_data * const params[], int result)
{
	_hook_api_(comapi, "event_broadcast.after(%s) -> %d", name, result);
}

static void hook_api_get_event_loop_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, struct sd_event *result)
{
	_hook_api_(comapi, "get_event_loop() -> %p", result);
}

static void hook_api_get_user_bus_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, struct sd_bus *result)
{
	_hook_api_(comapi, "get_user_bus() -> %p", result);
}

static void hook_api_get_system_bus_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, struct sd_bus *result)
{
	_hook_api_(comapi, "get_system_bus() -> %p", result);
}

static void hook_api_vverbose_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int level, const char *file, int line, const char *function, const char *fmt, va_list args)
{
	int len;
	char *msg;
	va_list ap;

	va_copy(ap, args);
	len = vasprintf(&msg, fmt, ap);
	va_end(ap);

	if (len < 0)
		_hook_api_(comapi, "vverbose(%d:%s, %s, %d, %s) -> %s ? ? ?", level, verbose_name_of_level(level), file, line, function, fmt);
	else {
		_hook_api_(comapi, "vverbose(%d:%s, %s, %d, %s) -> %s", level, verbose_name_of_level(level), file, line, function, msg);
		free(msg);
	}
}

static void hook_api_event_make_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *name, struct afb_evt *result)
{
	_hook_api_(comapi, "event_make(%s) -> %s:%d", name, afb_evt_fullname(result), afb_evt_id(result));
}

static void hook_api_rootdir_get_fd_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result)
{
	char path[PATH_MAX], proc[100];
	ssize_t s;

	if (result < 0)
		_hook_api_(comapi, "rootdir_get_fd() -> %d, %m", result);
	else {
		snprintf(proc, sizeof proc, "/proc/self/fd/%d", result);
		s = readlink(proc, path, sizeof path);
		path[s < 0 ? 0 : (size_t)s >= sizeof path ? sizeof path - 1 : (size_t)s] = 0;
		_hook_api_(comapi, "rootdir_get_fd() -> %d = %s", result, path);
	}
}

static void hook_api_rootdir_open_locale_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *filename, int flags, const char *locale, int result)
{
	char path[PATH_MAX], proc[100];
	ssize_t s;

	if (!locale)
		locale = "(null)";
	if (result < 0)
		_hook_api_(comapi, "rootdir_open_locale(%s, %d, %s) -> %d, %m", filename, flags, locale, result);
	else {
		snprintf(proc, sizeof proc, "/proc/self/fd/%d", result);
		s = readlink(proc, path, sizeof path);
		path[s < 0 ? 0 : (size_t)s >= sizeof path ? sizeof path - 1 : (size_t)s] = 0;
		_hook_api_(comapi, "rootdir_open_locale(%s, %d, %s) -> %d = %s", filename, flags, locale, result, path);
	}
}

static void hook_api_post_job_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, long delayms, int timeout, void (*callback)(int signum, void *arg), void *argument, void *group, int result)
{
	_hook_api_(comapi, "post_job(%p, %p, %p, %d, %ld) -> %d", callback, argument, group, timeout, delayms, result);
}

static void hook_api_require_api_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *name, int initialized)
{
	_hook_api_(comapi, "require_api(%s, %d)...", name, initialized);
}

static void hook_api_require_api_result_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *name, int initialized, int result)
{
	_hook_api_(comapi, "...require_api(%s, %d) -> %d", name, initialized, result);
}

static void hook_api_add_alias_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *api, const char *alias, int result)
{
	_hook_api_(comapi, "add_alias(%s -> %s) -> %d", api, alias?:"<null>", result);
}

static void hook_api_start_before_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi)
{
	_hook_api_(comapi, "start.before");
}

static void hook_api_start_after_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int status)
{
	_hook_api_(comapi, "start.after -> %d", status);
}

static void hook_api_on_event_before_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *event, int evt, unsigned nparams, struct afb_data * const params[])
{
	_hook_api_(comapi, "on_event.before(%s, %d)", event, evt);
}

static void hook_api_on_event_after_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *event, int evt, unsigned nparams, struct afb_data * const params[])
{
	_hook_api_(comapi, "on_event.after(%s, %d)", event, evt);
}

static void hook_api_call_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *api, const char *verb, unsigned nparams, struct afb_data * const params[])
{
	_hook_api_(comapi, "call(%s/%s) ...", api, verb);
}

static void hook_api_call_result_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int status, unsigned nreplies, struct afb_data * const replies[])
{
	_hook_api_(comapi, "    ...call... [%s: %d]", status < 0 ? "error" : "success", status);
}

static void hook_api_callsync_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *api, const char *verb, unsigned nparams, struct afb_data * const params[])
{
	_hook_api_(comapi, "callsync(%s/%s) ...", api, verb);
}

static void hook_api_callsync_result_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result, int *status, unsigned *nreplies, struct afb_data * const replies[])
{
	_hook_api_(comapi, "    ...callsync... [%s: %d]", !status ? "?" : *status < 0 ? "error" : "success", status ? *status : 0); /* TODO */
}

static void hook_api_new_api_before_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *api, const char *info, int noconcurrency)
{
	_hook_api_(comapi, "new_api.before %s (%s)%s ...", api, info?:"", noconcurrency?" no-concurrency" : "");
}

static void hook_api_new_api_after_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result, const char *api)
{
	_hook_api_(comapi, "... new_api.after %s -> %s (%d)", api, result >= 0 ? "OK" : "ERROR", result);
}

static void hook_api_api_set_verbs_v3_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result, const struct afb_verb_v3 *verbs)
{
	_hook_api_(comapi, "set_verbs_v3 -> %s (%d)", result >= 0 ? "OK" : "ERROR", result);
}

static void hook_api_api_set_verbs_v4_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result, const struct afb_verb_v4 *verbs)
{
	_hook_api_(comapi, "set_verbs_v4 -> %s (%d)", result >= 0 ? "OK" : "ERROR", result);
}

static void hook_api_api_add_verb_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result, const char *verb, const char *info, int glob)
{
	_hook_api_(comapi, "add_verb(%s%s [%s]) -> %s (%d)", verb, glob?" (GLOB)":"", info?:"", result >= 0 ? "OK" : "ERROR", result);
}

static void hook_api_api_del_verb_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result, const char *verb)
{
	_hook_api_(comapi, "del_verb(%s) -> %s (%d)", verb, result >= 0 ? "OK" : "ERROR", result);
}

static void hook_api_api_set_on_event_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result)
{
	_hook_api_(comapi, "set_on_event -> %s (%d)", result >= 0 ? "OK" : "ERROR", result);
}

static void hook_api_api_set_on_init_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result)
{
	_hook_api_(comapi, "set_on_init -> %s (%d)", result >= 0 ? "OK" : "ERROR", result);
}

static void hook_api_api_seal_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi)
{
	_hook_api_(comapi, "seal");
}

static void hook_api_event_handler_add_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result, const char *pattern)
{
	_hook_api_(comapi, "event_handler_add(%s) -> %s (%d)", pattern, result >= 0 ? "OK" : "ERROR", result);
}

static void hook_api_event_handler_del_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result, const char *pattern)
{
	_hook_api_(comapi, "event_handler_del(%s) -> %s (%d)", pattern, result >= 0 ? "OK" : "ERROR", result);
}

static void hook_api_class_provide_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result, const char *name)
{
	_hook_api_(comapi, "class_provide(%s) -> %s (%d)", name, result >= 0 ? "OK" : "ERROR", result);
}

static void hook_api_class_require_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result, const char *name)
{
	_hook_api_(comapi, "class_require(%s) -> %s (%d)", name, result >= 0 ? "OK" : "ERROR", result);
}

static void hook_api_delete_api_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, int result)
{
	_hook_api_(comapi, "delete_api -> %s (%d)", result >= 0 ? "OK" : "ERROR", result);
}

static void hook_api_on_event_handler_before_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *event, int evt, unsigned nparams, struct afb_data * const params[], const char *pattern)
{
	_hook_api_(comapi, "on_event_handler[%s].before(%s, %d)", pattern, event, evt);
}

static void hook_api_on_event_handler_after_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, const char *event, int evt, unsigned nparams, struct afb_data * const params[], const char *pattern)
{
	_hook_api_(comapi, "on_event_handler[%s].after(%s, %d)", pattern, event, evt);
}

static void hook_api_settings_cb(void *closure, const struct afb_hookid *hookid, const struct afb_api_common *comapi, struct json_object *object)
{
	_hook_api_(comapi, "settings -> %s", json_object_to_json_string_ext(object, JSON_C_TO_STRING_NOSLASHESCAPE));
}

static struct afb_hook_api_itf hook_api_default_itf = {
	.hook_api_event_broadcast_before = hook_api_event_broadcast_before_cb,
	.hook_api_event_broadcast_after = hook_api_event_broadcast_after_cb,
	.hook_api_get_event_loop = hook_api_get_event_loop_cb,
	.hook_api_get_user_bus = hook_api_get_user_bus_cb,
	.hook_api_get_system_bus = hook_api_get_system_bus_cb,
	.hook_api_vverbose = hook_api_vverbose_cb,
	.hook_api_event_make = hook_api_event_make_cb,
	.hook_api_rootdir_get_fd = hook_api_rootdir_get_fd_cb,
	.hook_api_rootdir_open_locale = hook_api_rootdir_open_locale_cb,
	.hook_api_post_job = hook_api_post_job_cb,
	.hook_api_require_api = hook_api_require_api_cb,
	.hook_api_require_api_result = hook_api_require_api_result_cb,
	.hook_api_add_alias = hook_api_add_alias_cb,
	.hook_api_start_before = hook_api_start_before_cb,
	.hook_api_start_after = hook_api_start_after_cb,
	.hook_api_on_event_before = hook_api_on_event_before_cb,
	.hook_api_on_event_after = hook_api_on_event_after_cb,
	.hook_api_call = hook_api_call_cb,
	.hook_api_call_result = hook_api_call_result_cb,
	.hook_api_callsync = hook_api_callsync_cb,
	.hook_api_callsync_result = hook_api_callsync_result_cb,
	.hook_api_new_api_before = hook_api_new_api_before_cb,
	.hook_api_new_api_after = hook_api_new_api_after_cb,
	.hook_api_api_set_verbs_v3 = hook_api_api_set_verbs_v3_cb,
	.hook_api_api_set_verbs_v4 = hook_api_api_set_verbs_v4_cb,
	.hook_api_api_add_verb = hook_api_api_add_verb_cb,
	.hook_api_api_del_verb = hook_api_api_del_verb_cb,
	.hook_api_api_set_on_event = hook_api_api_set_on_event_cb,
	.hook_api_api_set_on_init = hook_api_api_set_on_init_cb,
	.hook_api_api_seal = hook_api_api_seal_cb,
	.hook_api_event_handler_add = hook_api_event_handler_add_cb,
	.hook_api_event_handler_del = hook_api_event_handler_del_cb,
	.hook_api_class_provide = hook_api_class_provide_cb,
	.hook_api_class_require = hook_api_class_require_cb,
	.hook_api_delete_api = hook_api_delete_api_cb,
	.hook_api_on_event_handler_before = hook_api_on_event_handler_before_cb,
	.hook_api_on_event_handler_after = hook_api_on_event_handler_after_cb,
	.hook_api_settings = hook_api_settings_cb,
};

/******************************************************************************
 * section: hooks for tracing daemon interface (comapi)
 *****************************************************************************/

#define _HOOK_API_2_(flag,func,...)   \
	struct afb_hook_api *hook; \
	struct afb_hookid hookid; \
	const char *apiname = afb_api_common_apiname(comapi); \
	x_rwlock_rdlock(&rwlock); \
	init_hookid(&hookid); \
	hook = list_of_api_hooks; \
	while (hook) { \
		if (hook->refcount \
		 && hook->itf->hook_api_##func \
		 && (hook->flags & afb_hook_flag_api_##flag) != 0 \
		 && MATCH_API(hook->api, apiname)) { \
			hook->itf->hook_api_##func(hook->closure, &hookid, __VA_ARGS__); \
		} \
		hook = hook->next; \
	} \
	x_rwlock_unlock(&rwlock);

#define _HOOK_API_(what,...)   _HOOK_API_2_(what,what,__VA_ARGS__)

void afb_hook_api_event_broadcast_before(const struct afb_api_common *comapi, const char *name, unsigned nparams, struct afb_data * const params[])
{
	_HOOK_API_2_(event_broadcast, event_broadcast_before, comapi, name, nparams, params);
}

int afb_hook_api_event_broadcast_after(const struct afb_api_common *comapi, const char *name, unsigned nparams, struct afb_data * const params[], int result)
{
	_HOOK_API_2_(event_broadcast, event_broadcast_after, comapi, name, nparams, params, result);
	return result;
}

struct sd_event *afb_hook_api_get_event_loop(const struct afb_api_common *comapi, struct sd_event *result)
{
	_HOOK_API_(get_event_loop, comapi, result);
	return result;
}

struct sd_bus *afb_hook_api_get_user_bus(const struct afb_api_common *comapi, struct sd_bus *result)
{
	_HOOK_API_(get_user_bus, comapi, result);
	return result;
}

struct sd_bus *afb_hook_api_get_system_bus(const struct afb_api_common *comapi, struct sd_bus *result)
{
	_HOOK_API_(get_system_bus, comapi, result);
	return result;
}

void afb_hook_api_vverbose(const struct afb_api_common *comapi, int level, const char *file, int line, const char *function, const char *fmt, va_list args)
{
	_HOOK_API_(vverbose, comapi, level, file, line, function, fmt, args);
}

struct afb_evt *afb_hook_api_event_make(const struct afb_api_common *comapi, const char *name, struct afb_evt *result)
{
	_HOOK_API_(event_make, comapi, name, result);
	return result;
}

int afb_hook_api_rootdir_get_fd(const struct afb_api_common *comapi, int result)
{
	_HOOK_API_(rootdir_get_fd, comapi, result);
	return result;
}

int afb_hook_api_rootdir_open_locale(const struct afb_api_common *comapi, const char *filename, int flags, const char *locale, int result)
{
	_HOOK_API_(rootdir_open_locale, comapi, filename, flags, locale, result);
	return result;
}

int afb_hook_api_post_job(const struct afb_api_common *comapi, long delayms, int timeout, void (*callback)(int signum, void *arg), void *argument, void *group, int result)
{
	_HOOK_API_(post_job, comapi, delayms, timeout, callback, argument, group, result);
	return result;
}

void afb_hook_api_require_api(const struct afb_api_common *comapi, const char *name, int initialized)
{
	_HOOK_API_(require_api, comapi, name, initialized);
}

int afb_hook_api_require_api_result(const struct afb_api_common *comapi, const char *name, int initialized, int result)
{
	_HOOK_API_2_(require_api, require_api_result, comapi, name, initialized, result);
	return result;
}

int afb_hook_api_add_alias(const struct afb_api_common *comapi, const char *api, const char *alias, int result)
{
	_HOOK_API_(add_alias, comapi, api, alias, result);
	return result;
}

void afb_hook_api_start_before(const struct afb_api_common *comapi)
{
	_HOOK_API_2_(start, start_before, comapi);
}

int afb_hook_api_start_after(const struct afb_api_common *comapi, int status)
{
	_HOOK_API_2_(start, start_after, comapi, status);
	return status;
}

void afb_hook_api_on_event_before(const struct afb_api_common *comapi, const char *event, int evt, unsigned nparams, struct afb_data * const params[])
{
	_HOOK_API_2_(on_event, on_event_before, comapi, event, evt, nparams, params);
}

void afb_hook_api_on_event_after(const struct afb_api_common *comapi, const char *event, int evt, unsigned nparams, struct afb_data * const params[])
{
	_HOOK_API_2_(on_event, on_event_after, comapi, event, evt, nparams, params);
}

void afb_hook_api_call(const struct afb_api_common *comapi, const char *api, const char *verb, unsigned nparams, struct afb_data * const params[])
{
	_HOOK_API_(call, comapi, api, verb, nparams, params);
}

void afb_hook_api_call_result(const struct afb_api_common *comapi, int status, unsigned nreplies, struct afb_data * const replies[])
{
	_HOOK_API_2_(call, call_result, comapi, status, nreplies, replies);

}

void afb_hook_api_callsync(const struct afb_api_common *comapi, const char *api, const char *verb, unsigned nparams, struct afb_data * const params[])
{
	_HOOK_API_(callsync, comapi, api, verb, nparams, params);
}

int afb_hook_api_callsync_result(const struct afb_api_common *comapi, int result, int *status, unsigned *nreplies, struct afb_data * const replies[])
{
	_HOOK_API_2_(callsync, callsync_result, comapi, result, status, nreplies, replies);
	return result;
}

void afb_hook_api_new_api_before(const struct afb_api_common *comapi, const char *api, const char *info, int noconcurrency)
{
	_HOOK_API_2_(new_api, new_api_before, comapi, api, info, noconcurrency);
}

int afb_hook_api_new_api_after(const struct afb_api_common *comapi, int result, const char *api)
{
	_HOOK_API_2_(new_api, new_api_after, comapi, result, api);
	return result;
}

int afb_hook_api_api_set_verbs_v3(const struct afb_api_common *comapi, int result, const struct afb_verb_v3 *verbs)
{
	_HOOK_API_2_(api_set_verbs, api_set_verbs_v3, comapi, result, verbs);
	return result;
}

int afb_hook_api_api_set_verbs_v4(const struct afb_api_common *comapi, int result, const struct afb_verb_v4 *verbs)
{
	_HOOK_API_2_(api_set_verbs, api_set_verbs_v4, comapi, result, verbs);
	return result;
}

int afb_hook_api_api_add_verb(const struct afb_api_common *comapi, int result, const char *verb, const char *info, int glob)
{
	_HOOK_API_(api_add_verb, comapi, result, verb, info, glob);
	return result;
}

int afb_hook_api_api_del_verb(const struct afb_api_common *comapi, int result, const char *verb)
{
	_HOOK_API_(api_del_verb, comapi, result, verb);
	return result;
}

int afb_hook_api_api_set_on_event(const struct afb_api_common *comapi, int result)
{
	_HOOK_API_(api_set_on_event, comapi, result);
	return result;
}

int afb_hook_api_api_set_on_init(const struct afb_api_common *comapi, int result)
{
	_HOOK_API_(api_set_on_init, comapi, result);
	return result;
}

void afb_hook_api_api_seal(const struct afb_api_common *comapi)
{
	_HOOK_API_(api_seal, comapi);
}

int afb_hook_api_event_handler_add(const struct afb_api_common *comapi, int result, const char *pattern)
{
	_HOOK_API_(event_handler_add, comapi, result, pattern);
	return result;
}
int afb_hook_api_event_handler_del(const struct afb_api_common *comapi, int result, const char *pattern)
{
	_HOOK_API_(event_handler_del, comapi, result, pattern);
	return result;
}
int afb_hook_api_class_provide(const struct afb_api_common *comapi, int result, const char *name)
{
	_HOOK_API_(class_provide, comapi, result, name);
	return result;
}
int afb_hook_api_class_require(const struct afb_api_common *comapi, int result, const char *name)
{
	_HOOK_API_(class_require, comapi, result, name);
	return result;
}

int afb_hook_api_delete_api(const struct afb_api_common *comapi, int result)
{
	_HOOK_API_(delete_api, comapi, result);
	return result;
}

void afb_hook_api_on_event_handler_before(const struct afb_api_common *comapi, const char *event, int evt, unsigned nparams, struct afb_data * const params[], const char *pattern)
{
	_HOOK_API_2_(on_event_handler, on_event_handler_before, comapi, event, evt, nparams, params, pattern);
}

void afb_hook_api_on_event_handler_after(const struct afb_api_common *comapi, const char *event, int evt, unsigned nparams, struct afb_data * const params[], const char *pattern)
{
	_HOOK_API_2_(on_event_handler, on_event_handler_after, comapi, event, evt, nparams, params, pattern);
}

struct json_object *afb_hook_api_settings(const struct afb_api_common *comapi, struct json_object *object)
{
	_HOOK_API_(settings, comapi, object);
	return object;
}

/******************************************************************************
 * section: hooking comapi
 *****************************************************************************/

unsigned afb_hook_flags_api(const char *api)
{
	unsigned flags;
	struct afb_hook_api *hook;

	flags = 0;
	x_rwlock_rdlock(&rwlock);
	hook = list_of_api_hooks;
	while (hook) {
		if (!api || MATCH_API(hook->api, api))
			flags |= hook->flags;
		hook = hook->next;
	}
	x_rwlock_unlock(&rwlock);
	return flags;
}

struct afb_hook_api *afb_hook_create_api(const char *api, unsigned flags, struct afb_hook_api_itf *itf, void *closure)
{
	struct afb_hook_api *hook;

	/* alloc the result */
	hook = calloc(1, sizeof *hook);
	if (hook == NULL)
		return NULL;

	/* get a copy of the names */
	hook->api = api ? strdup(api) : NULL;
	if (api && !hook->api) {
		free(hook);
		return NULL;
	}

	/* initialise the rest */
	hook->refcount = 1;
	hook->flags = flags;
	hook->itf = itf ? itf : &hook_api_default_itf;
	hook->closure = closure;

	/* record the hook */
	x_mutex_lock(&mutex);
	hook->next = list_of_api_hooks;
	list_of_api_hooks = hook;
	x_mutex_unlock(&mutex);

	/* returns it */
	return hook;
}

struct afb_hook_api *afb_hook_addref_api(struct afb_hook_api *hook)
{
	__atomic_add_fetch(&hook->refcount, 1, __ATOMIC_RELAXED);
	return hook;
}

static void hook_clean_api()
{
	struct afb_hook_api **prv, *hook, *head;

	if (x_rwlock_trywrlock(&rwlock) == 0) {
		/* unlink under mutex */
		head = NULL;
		x_mutex_lock(&mutex);
		prv = &list_of_api_hooks;
		while ((hook = *prv)) {
			if (hook->refcount)
				prv = &(*prv)->next;
			else {
				*prv = hook->next;
				hook->next = head;
				head = hook;
			}
		}
		x_mutex_unlock(&mutex);
		x_rwlock_unlock(&rwlock);

		/* free found hooks */
		while((hook = head)) {
			head = hook->next;
			free(hook->api);
			free(hook);
		}
	}
}

void afb_hook_unref_api(struct afb_hook_api *hook)
{
	if (hook && !__atomic_sub_fetch(&hook->refcount, 1, __ATOMIC_RELAXED))
		hook_clean_api();
}

/******************************************************************************
 * section: default callbacks for tracing service interface (evt)
 *****************************************************************************/

static void _hook_evt_(const char *evt, int id, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	_hook_("evt-%s:%d", format, ap, evt, id);
	va_end(ap);
}

static void hook_evt_create_cb(void *closure, const struct afb_hookid *hookid, const char *evt, int id)
{
	_hook_evt_(evt, id, "create");
}

static void hook_evt_push_before_cb(void *closure, const struct afb_hookid *hookid, const char *evt, int id, unsigned nparams, struct afb_data * const params[])
{
	_hook_evt_(evt, id, "push.before");
}


static void hook_evt_push_after_cb(void *closure, const struct afb_hookid *hookid, const char *evt, int id, unsigned nparams, struct afb_data * const params[], int result)
{
	_hook_evt_(evt, id, "push.after -> %d", result);
}

static void hook_evt_broadcast_before_cb(void *closure, const struct afb_hookid *hookid, const char *evt, int id, unsigned nparams, struct afb_data * const params[])
{
	_hook_evt_(evt, id, "broadcast.before");
}

static void hook_evt_broadcast_after_cb(void *closure, const struct afb_hookid *hookid, const char *evt, int id, unsigned nparams, struct afb_data * const params[], int result)
{
	_hook_evt_(evt, id, "broadcast.after -> %d", result);
}

static void hook_evt_name_cb(void *closure, const struct afb_hookid *hookid, const char *evt, int id, const char *result)
{
	_hook_evt_(evt, id, "name -> %s", result);
}

static void hook_evt_addref_cb(void *closure, const struct afb_hookid *hookid, const char *evt, int id)
{
	_hook_evt_(evt, id, "addref");
}

static void hook_evt_unref_cb(void *closure, const struct afb_hookid *hookid, const char *evt, int id)
{
	_hook_evt_(evt, id, "unref");
}

static struct afb_hook_evt_itf hook_evt_default_itf = {
	.hook_evt_create = hook_evt_create_cb,
	.hook_evt_push_before = hook_evt_push_before_cb,
	.hook_evt_push_after = hook_evt_push_after_cb,
	.hook_evt_broadcast_before = hook_evt_broadcast_before_cb,
	.hook_evt_broadcast_after = hook_evt_broadcast_after_cb,
	.hook_evt_name = hook_evt_name_cb,
	.hook_evt_addref = hook_evt_addref_cb,
	.hook_evt_unref = hook_evt_unref_cb
};

/******************************************************************************
 * section: hooks for tracing events interface (evt)
 *****************************************************************************/

#define _HOOK_EVT_(what,...)   \
	struct afb_hook_evt *hook; \
	struct afb_hookid hookid; \
	x_rwlock_rdlock(&rwlock); \
	init_hookid(&hookid); \
	hook = list_of_evt_hooks; \
	while (hook) { \
		if (hook->refcount \
		 && hook->itf->hook_evt_##what \
		 && (hook->flags & afb_hook_flag_evt_##what) != 0 \
		 && MATCH_EVENT(hook->pattern, evt)) { \
			hook->itf->hook_evt_##what(hook->closure, &hookid, __VA_ARGS__); \
		} \
		hook = hook->next; \
	} \
	x_rwlock_unlock(&rwlock);

void afb_hook_evt_create(const char *evt, int id)
{
	_HOOK_EVT_(create, evt, id);
}

void afb_hook_evt_push_before(const char *evt, int id, unsigned nparams, struct afb_data * const params[])
{
	_HOOK_EVT_(push_before, evt, id, nparams, params);
}

int afb_hook_evt_push_after(const char *evt, int id, unsigned nparams, struct afb_data * const params[], int result)
{
	_HOOK_EVT_(push_after, evt, id, nparams, params, result);
	return result;
}

void afb_hook_evt_broadcast_before(const char *evt, int id, unsigned nparams, struct afb_data * const params[])
{
	_HOOK_EVT_(broadcast_before, evt, id, nparams, params);
}

int afb_hook_evt_broadcast_after(const char *evt, int id, unsigned nparams, struct afb_data * const params[], int result)
{
	_HOOK_EVT_(broadcast_after, evt, id, nparams, params, result);
	return result;
}

void afb_hook_evt_name(const char *evt, int id, const char *result)
{
	_HOOK_EVT_(name, evt, id, result);
}

void afb_hook_evt_addref(const char *evt, int id)
{
	_HOOK_EVT_(addref, evt, id);
}

void afb_hook_evt_unref(const char *evt, int id)
{
	_HOOK_EVT_(unref, evt, id);
}

/******************************************************************************
 * section: hooking services (evt)
 *****************************************************************************/

unsigned afb_hook_flags_evt(const char *name)
{
	unsigned flags;
	struct afb_hook_evt *hook;

	flags = 0;
	x_rwlock_rdlock(&rwlock);
	hook = list_of_evt_hooks;
	while (hook) {
		if (!name || MATCH_EVENT(hook->pattern, name))
			flags |= hook->flags;
		hook = hook->next;
	}
	x_rwlock_unlock(&rwlock);
	return flags;
}

struct afb_hook_evt *afb_hook_create_evt(const char *pattern, unsigned flags, struct afb_hook_evt_itf *itf, void *closure)
{
	struct afb_hook_evt *hook;

	/* alloc the result */
	hook = calloc(1, sizeof *hook);
	if (hook == NULL)
		return NULL;

	/* get a copy of the names */
	hook->pattern = pattern ? strdup(pattern) : NULL;
	if (pattern && !hook->pattern) {
		free(hook);
		return NULL;
	}

	/* initialise the rest */
	hook->refcount = 1;
	hook->flags = flags;
	hook->itf = itf ? itf : &hook_evt_default_itf;
	hook->closure = closure;

	/* record the hook */
	x_mutex_lock(&mutex);
	hook->next = list_of_evt_hooks;
	list_of_evt_hooks = hook;
	x_mutex_unlock(&mutex);

	/* returns it */
	return hook;
}

struct afb_hook_evt *afb_hook_addref_evt(struct afb_hook_evt *hook)
{
	__atomic_add_fetch(&hook->refcount, 1, __ATOMIC_RELAXED);
	return hook;
}

static void hook_clean_evt()
{
	struct afb_hook_evt **prv, *hook, *head;

	if (x_rwlock_trywrlock(&rwlock) == 0) {
		/* unlink under mutex */
		head = NULL;
		x_mutex_lock(&mutex);
		prv = &list_of_evt_hooks;
		while ((hook = *prv)) {
			if (hook->refcount)
				prv = &(*prv)->next;
			else {
				*prv = hook->next;
				hook->next = head;
				head = hook;
			}
		}
		x_mutex_unlock(&mutex);
		x_rwlock_unlock(&rwlock);

		/* free found hooks */
		while((hook = head)) {
			head = hook->next;
			free(hook->pattern);
			free(hook);
		}
	}
}

void afb_hook_unref_evt(struct afb_hook_evt *hook)
{
	if (hook && !__atomic_sub_fetch(&hook->refcount, 1, __ATOMIC_RELAXED))
		hook_clean_evt();
}

/******************************************************************************
 * section: default callbacks for sessions (session)
 *****************************************************************************/

static void _hook_session_(struct afb_session *session, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	_hook_("session-%s", format, ap, afb_session_uuid(session));
	va_end(ap);
}

static void hook_session_create_cb(void *closure, const struct afb_hookid *hookid, struct afb_session *session)
{
	_hook_session_(session, "create");
}

static void hook_session_close_cb(void *closure, const struct afb_hookid *hookid, struct afb_session *session)
{
	_hook_session_(session, "close");
}

static void hook_session_destroy_cb(void *closure, const struct afb_hookid *hookid, struct afb_session *session)
{
	_hook_session_(session, "destroy");
}

static void hook_session_addref_cb(void *closure, const struct afb_hookid *hookid, struct afb_session *session)
{
	_hook_session_(session, "addref");
}

static void hook_session_unref_cb(void *closure, const struct afb_hookid *hookid, struct afb_session *session)
{
	_hook_session_(session, "unref");
}

static struct afb_hook_session_itf hook_session_default_itf = {
	.hook_session_create = hook_session_create_cb,
	.hook_session_close = hook_session_close_cb,
	.hook_session_destroy = hook_session_destroy_cb,
	.hook_session_addref = hook_session_addref_cb,
	.hook_session_unref = hook_session_unref_cb
};

/******************************************************************************
 * section: hooks for tracing sessions (session)
 *****************************************************************************/

#define _HOOK_SESSION_(what,...)   \
	struct afb_hook_session *hook; \
	struct afb_hookid hookid; \
	const char *sessid = 0; \
	x_rwlock_rdlock(&rwlock); \
	init_hookid(&hookid); \
	hook = list_of_session_hooks; \
	while (hook) { \
		if (hook->refcount \
		 && hook->itf->hook_session_##what \
		 && (hook->flags & afb_hook_flag_session_##what) != 0 \
		 && MATCH_SESSION(hook->pattern, (sessid?:(sessid=afb_session_uuid(session))))) { \
			hook->itf->hook_session_##what(hook->closure, &hookid, __VA_ARGS__); \
		} \
		hook = hook->next; \
	} \
	x_rwlock_unlock(&rwlock);

void afb_hook_session_create(struct afb_session *session)
{
	_HOOK_SESSION_(create, session);
}

void afb_hook_session_close(struct afb_session *session)
{
	_HOOK_SESSION_(close, session);
}

void afb_hook_session_destroy(struct afb_session *session)
{
	_HOOK_SESSION_(destroy, session);
}

void afb_hook_session_addref(struct afb_session *session)
{
	_HOOK_SESSION_(addref, session);
}

void afb_hook_session_unref(struct afb_session *session)
{
	_HOOK_SESSION_(unref, session);
}


/******************************************************************************
 * section: hooking sessions (session)
 *****************************************************************************/

struct afb_hook_session *afb_hook_create_session(const char *pattern, unsigned flags, struct afb_hook_session_itf *itf, void *closure)
{
	struct afb_hook_session *hook;

	/* alloc the result */
	hook = calloc(1, sizeof *hook);
	if (hook == NULL)
		return NULL;

	/* get a copy of the names */
	hook->pattern = pattern ? strdup(pattern) : NULL;
	if (pattern && !hook->pattern) {
		free(hook);
		return NULL;
	}

	/* initialise the rest */
	hook->refcount = 1;
	hook->flags = flags;
	hook->itf = itf ? itf : &hook_session_default_itf;
	hook->closure = closure;

	/* record the hook */
	x_mutex_lock(&mutex);
	hook->next = list_of_session_hooks;
	list_of_session_hooks = hook;
	x_mutex_unlock(&mutex);

	/* returns it */
	return hook;
}

struct afb_hook_session *afb_hook_addref_session(struct afb_hook_session *hook)
{
	__atomic_add_fetch(&hook->refcount, 1, __ATOMIC_RELAXED);
	return hook;
}

static void hook_clean_session()
{
	struct afb_hook_session **prv, *hook, *head;

	if (x_rwlock_trywrlock(&rwlock) == 0) {
		/* unlink under mutex */
		head = NULL;
		x_mutex_lock(&mutex);
		prv = &list_of_session_hooks;
		while ((hook = *prv)) {
			if (hook->refcount)
				prv = &(*prv)->next;
			else {
				*prv = hook->next;
				hook->next = head;
				head = hook;
			}
		}
		x_mutex_unlock(&mutex);
		x_rwlock_unlock(&rwlock);

		/* free found hooks */
		while((hook = head)) {
			head = hook->next;
			free(hook->pattern);
			free(hook);
		}
	}
}

void afb_hook_unref_session(struct afb_hook_session *hook)
{
	if (hook && !__atomic_sub_fetch(&hook->refcount, 1, __ATOMIC_RELAXED))
		hook_clean_session();
}

/******************************************************************************
 * section: default callbacks for globals (global)
 *****************************************************************************/

static void _hook_global_(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	_hook_("global", format, ap);
	va_end(ap);
}

static void hook_global_vverbose_cb(void *closure, const struct afb_hookid *hookid, int level, const char *file, int line, const char *func, const char *fmt, va_list args)
{
	int len;
	char *msg;
	va_list ap;

	va_copy(ap, args);
	len = vasprintf(&msg, fmt, ap);
	va_end(ap);

	if (len < 0)
		_hook_global_("vverbose(%d:%s, %s, %d, %s) -> %s ? ? ?", level, verbose_name_of_level(level), file, line, func, fmt);
	else {
		_hook_global_("vverbose(%d:%s, %s, %d, %s) -> %s", level, verbose_name_of_level(level), file, line, func, msg);
		free(msg);
	}
}

static struct afb_hook_global_itf hook_global_default_itf = {
	.hook_global_vverbose = hook_global_vverbose_cb
};

/******************************************************************************
 * section: hooks for tracing globals (global)
 *****************************************************************************/

#define _HOOK_GLOBAL_(what,...)   \
	struct afb_hook_global *hook; \
	struct afb_hookid hookid; \
	x_rwlock_rdlock(&rwlock); \
	init_hookid(&hookid); \
	hook = list_of_global_hooks; \
	while (hook) { \
		if (hook->refcount \
		 && hook->itf->hook_global_##what \
		 && (hook->flags & afb_hook_flag_global_##what) != 0) { \
			hook->itf->hook_global_##what(hook->closure, &hookid, __VA_ARGS__); \
		} \
		hook = hook->next; \
	} \
	x_rwlock_unlock(&rwlock);

static void afb_hook_global_vverbose(int level, const char *file, int line, const char *func, const char *fmt, va_list args)
{
	_HOOK_GLOBAL_(vverbose, level, file ?: "?", line, func ?: "?", fmt ?: "", args);
}

/******************************************************************************
 * section: hooking globals (global)
 *****************************************************************************/

static void update_global()
{
	struct afb_hook_global *hook;
	unsigned flags = 0;

	x_rwlock_rdlock(&rwlock);
	hook = list_of_global_hooks;
	while (hook) {
		if (hook->refcount)
			flags = hook->flags;
		hook = hook->next;
	}
	verbose_observer = (flags & afb_hook_flag_global_vverbose) ? afb_hook_global_vverbose : NULL;
	x_rwlock_unlock(&rwlock);
}

struct afb_hook_global *afb_hook_create_global(unsigned flags, struct afb_hook_global_itf *itf, void *closure)
{
	struct afb_hook_global *hook;

	/* alloc the result */
	hook = calloc(1, sizeof *hook);
	if (hook == NULL)
		return NULL;

	/* initialise the rest */
	hook->refcount = 1;
	hook->flags = flags;
	hook->itf = itf ? itf : &hook_global_default_itf;
	hook->closure = closure;

	/* record the hook */
	x_mutex_lock(&mutex);
	hook->next = list_of_global_hooks;
	list_of_global_hooks = hook;
	x_mutex_unlock(&mutex);

	/* update hooking */
	update_global();

	/* returns it */
	return hook;
}

struct afb_hook_global *afb_hook_addref_global(struct afb_hook_global *hook)
{
	__atomic_add_fetch(&hook->refcount, 1, __ATOMIC_RELAXED);
	return hook;
}

static void hook_clean_global()
{
	struct afb_hook_global **prv, *hook, *head;

	if (x_rwlock_trywrlock(&rwlock) == 0) {
		/* unlink under mutex */
		head = NULL;
		x_mutex_lock(&mutex);
		prv = &list_of_global_hooks;
		while ((hook = *prv)) {
			if (hook->refcount)
				prv = &(*prv)->next;
			else {
				*prv = hook->next;
				hook->next = head;
				head = hook;
			}
		}
		x_mutex_unlock(&mutex);
		x_rwlock_unlock(&rwlock);

		/* free found hooks */
		while((hook = head)) {
			head = hook->next;
			free(hook);
		}
	}
}

void afb_hook_unref_global(struct afb_hook_global *hook)
{
	if (hook && !__atomic_sub_fetch(&hook->refcount, 1, __ATOMIC_RELAXED)) {
		/* update hooking */
		update_global();
		hook_clean_global();
	}
}

#endif /* WITH_AFB_HOOK *******************************************************/
