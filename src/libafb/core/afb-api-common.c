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

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define ISSPACE(x) (isspace((int)(unsigned char)(x)))

#include <json-c/json.h>
#if !defined(JSON_C_TO_STRING_NOSLASHESCAPE)
#define JSON_C_TO_STRING_NOSLASHESCAPE 0
#endif

#include "core/afb-apiname.h"
#include "core/afb-apiset.h"
#include "core/afb-api-common.h"
#include "core/afb-api-v3.h"
#include "core/afb-common.h"
#include "core/afb-evt.h"
#include "core/afb-api-common.h"
#include "core/afb-hook.h"
#include "core/afb-msg-json.h"
#include "core/afb-session.h"
#include "core/afb-req-common.h"
#include "core/afb-calls.h"
#include "core/afb-error-text.h"
#include "core/afb-string-mode.h"

#include "core/afb-jobs.h"
#include "core/afb-sched.h"
#include "sys/verbose.h"
#include "utils/globset.h"
#include "core/afb-sig-monitor.h"
#include "utils/wrap-json.h"
#include "sys/x-realpath.h"
#include "sys/x-errno.h"

/*************************************************************************
 * internal types
 ************************************************************************/

/*****************************************************************************/

static inline struct afb_api_x3 *to_api_x3(struct afb_api_common *comapi)
{
	return (struct afb_api_x3*)comapi;
}

static inline struct afb_api_common *from_api_x3(struct afb_api_x3 *api)
{
	return (struct afb_api_common*)api;
}

/******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
	SETTINGS
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/

static struct json_object *configuration;

void afb_api_common_set_config(struct json_object *config)
{
	struct json_object *save = configuration;
	configuration = json_object_get(config);
	json_object_put(save);
}

static struct json_object *make_settings(struct afb_api_common *comapi)
{
	struct json_object *result;
	struct json_object *obj;

	/* clone the globals */
	if (json_object_object_get_ex(configuration, "*", &obj))
		result = wrap_json_clone(obj);
	else
		result = json_object_new_object();

	/* add locals */
	if (json_object_object_get_ex(configuration, comapi->name, &obj))
		wrap_json_object_add(result, obj);

	/* add library path */
	if (comapi->path)
		json_object_object_add(result, "binding-path", json_object_new_string(comapi->path));

	comapi->settings = result;
	return result;
}

/******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
	SESSION
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/
/*****************************************************************************/

/* the common session for services sharing their session */
static struct afb_session *common_session;

static
struct afb_session *
session_get_common()
{
	struct afb_session *result;

	/* session shared with other exports */
	result = common_session;
	if (result == NULL) {
		result = afb_session_create (0);
		common_session = result;
	}

	return result;
}

static
void
session_cleanup(
	struct afb_api_common *comapi
) {
#if WITH_API_SESSIONS
	afb_session_unref(comapi->session);
#endif
}

struct afb_session *
afb_api_common_session_get(
	struct afb_api_common *comapi
) {
#if WITH_API_SESSIONS
	return comapi->session;
#else
	return session_get_common();
#endif
}

#if WITH_API_SESSIONS
int
afb_api_common_unshare_session(
	struct afb_api_common *comapi
) {
	struct afb_session *common = session_get_common();
	struct afb_session *current = comapi->session;

	if (current == common) {
		current = afb_session_create (0);
		if (!current)
			return X_ENOMEM;
		comapi->session = current;
		afb_session_unref(common);
	}
	return 0;
}
#endif

/******************************************************************************
 ******************************************************************************
                      COMMON IMPLEMENTATIONS
 ******************************************************************************
 ******************************************************************************/

/**********************************************
* normal flow
**********************************************/
void
afb_api_common_vverbose(
	const struct afb_api_common *comapi,
	int         level,
	const char *file,
	int         line,
	const char *function,
	const char *fmt,
	va_list     args
) {
	char *p;

	if (!fmt || vasprintf(&p, fmt, args) < 0)
		vverbose(level, file, line, function, fmt, args);
	else {
		verbose(level, file, line, function, (verbose_is_colorized() == 0 ? "[API %s] %s" : COLOR_API "[API %s]" COLOR_DEFAULT " %s"), comapi->name, p);
		free(p);
	}
}

int
afb_api_common_new_event(
	const struct afb_api_common *comapi,
	const char *name,
	struct afb_evt **evt
) {
	int rc;
	struct afb_evt *e;

	/* check daemon state */
	if (comapi->state == Api_State_Pre_Init) {
		ERROR("[API %s] Bad call to 'afb_daemon_event_make(%s)', must not be in PreInit", comapi->name, name);
		rc = X_EINVAL;
		e = NULL;
	}
	else {
		e = afb_evt_create2(comapi->name, name); /* FIXME: use ret int */
		rc = e ? 0 : X_ENOMEM;
	}
	*evt = e;
	return rc;
}

int
afb_api_common_event_broadcast(
	const struct afb_api_common *comapi,
	const char *name,
	struct json_object *object
) {
	size_t plen, nlen;
	char *event;

	/* check daemon state */
	if (comapi->state == Api_State_Pre_Init) {
		ERROR("[API %s] Bad call to 'afb_daemon_event_broadcast(%s, %s)', must not be in PreInit",
			comapi->name, name, json_object_to_json_string_ext(object, JSON_C_TO_STRING_NOSLASHESCAPE));
		errno = X_EINVAL;
		return 0;
	}

	/* makes the event name */
	plen = strlen(comapi->name);
	nlen = strlen(name);
	event = alloca(nlen + plen + 2);
	memcpy(event, comapi->name, plen);
	event[plen] = '/';
	memcpy(event + plen + 1, name, nlen + 1);

	/* broadcast the event */
	return afb_evt_broadcast_name(event, object);
}

int
afb_api_common_queue_job(
	const struct afb_api_common *comapi,
	void (*callback)(int signum, void *arg),
	void *argument,
	void *group,
	int timeout
) {
	/* TODO: translate group ~ api */
	return afb_jobs_queue(group, timeout, callback, argument);
}

int
afb_api_common_require_api(
	const struct afb_api_common *comapi,
	const char *name,
	int initialized
) {
	int rc, rc2;
	char *iter, *end, save;

	/* emit a warning about unexpected require in preinit */
	if (comapi->state == Api_State_Pre_Init && initialized) {
		if (initialized) {
			ERROR("[API %s] requiring initialized apis in pre-init is forbiden", comapi->name);
			return X_EINVAL;
		}
		WARNING("[API %s] requiring apis pre-init may lead to unexpected result", comapi->name);
	}

	/* scan the names in a local copy */
	rc = 0;
	iter = strdupa(name);
	for(;;) {
		/* skip any space */
		save = *iter;
		while(ISSPACE(save))
			save = *++iter;
		if (!save) /* at end? */
			return rc;

		/* search for the end */
		end = iter;
		while (save && !ISSPACE(save))
			save = *++end;
		*end = 0;

		/* check the required api */
		if (comapi->state == Api_State_Pre_Init) {
			rc2 = afb_apiset_require(comapi->declare_set, comapi->name, iter);
			if (rc2 < 0) {
				ERROR("[API %s] requiring api %s in pre-init failed", comapi->name, iter);
			}
		} else {
			rc2 = afb_apiset_get_api(comapi->call_set, iter, 1, initialized, NULL);
			if (rc2 < 0) {
				ERROR("[API %s] requiring api %s%s failed", comapi->name,
					 iter, initialized ? " initialized" : "");
			}
		}
		if (rc2 < 0)
			rc = rc2;

		*end = save;
		iter = end;
	}
}

int
afb_api_common_add_alias(
	const struct afb_api_common *comapi,
	const char *apiname,
	const char *aliasname
) {
	if (!afb_apiname_is_valid(aliasname)) {
		ERROR("[API %s] Can't add alias to %s: bad API name", comapi->name, aliasname);
		return X_EINVAL;
	}
	if (!apiname)
		apiname = comapi->name;
	NOTICE("[API %s] aliasing [API %s] to [API %s]", comapi->name, apiname, aliasname);
	return afb_apiset_add_alias(comapi->declare_set, apiname, aliasname);
}

void
afb_api_common_api_seal(
	struct afb_api_common *comapi
) {
	comapi->sealed = 1;
}

int
afb_api_common_class_provide(
	const struct afb_api_common *comapi,
	const char *name
) {
	int rc = 0, rc2;
	char *iter, *end, save;

	iter = strdupa(name);
	for(;;) {
		/* skip any space */
		save = *iter;
		while(ISSPACE(save))
			save = *++iter;
		if (!save) /* at end? */
			return rc;

		/* search for the end */
		end = iter;
		while (save && !ISSPACE(save))
			save = *++end;
		*end = 0;

		rc2 = afb_apiset_provide_class(comapi->declare_set, comapi->name, iter);
		if (rc2 < 0)
			rc = rc2;

		*end = save;
		iter = end;
	}
}

int
afb_api_common_class_require(
	const struct afb_api_common *comapi,
	const char *name
) {
	int rc = 0, rc2;
	char *iter, *end, save;

	iter = strdupa(name);
	for(;;) {
		/* skip any space */
		save = *iter;
		while(ISSPACE(save))
			save = *++iter;
		if (!save) /* at end? */
			return rc;

		/* search for the end */
		end = iter;
		while (save && !ISSPACE(save))
			save = *++end;
		*end = 0;

		rc2 = afb_apiset_require_class(comapi->declare_set, comapi->name, iter);
		if (rc2 < 0)
			rc = rc2;

		*end = save;
		iter = end;
	}
}

struct json_object *
afb_api_common_settings(
	const struct afb_api_common *comapi
) {
	struct json_object *result = comapi->settings;
	if (!result)
		result = make_settings((struct afb_api_common *)comapi);
	return result;
}

/**********************************************
* hooked flow
**********************************************/
#if WITH_AFB_HOOK
void afb_api_common_vverbose_hookable(
	const struct afb_api_common *comapi,
	int         level,
	const char *file,
	int         line,
	const char *function,
	const char *fmt,
	va_list     args
) {
	va_list ap;
	va_copy(ap, args);
	afb_api_common_vverbose(comapi, level, file, line, function, fmt, args);
	afb_hook_api_vverbose(comapi, level, file, line, function, fmt, ap);
	va_end(ap);
}

int
afb_api_common_new_event_hookable(
	const struct afb_api_common *comapi,
	const char *name,
	struct afb_evt **evt
) {
	int rc = afb_api_common_new_event(comapi, name, evt);
	afb_hook_api_event_make(comapi, name, *evt);
	return rc;
}

int
afb_api_common_event_broadcast_hookable(
	const struct afb_api_common *comapi,
	const char *name,
	struct json_object *object
) {
	int r;
	json_object_get(object);
	afb_hook_api_event_broadcast_before(comapi, name, object);
	r = afb_api_common_event_broadcast(comapi, name, object);
	afb_hook_api_event_broadcast_after(comapi, name, object, r);
	json_object_put(object);
	return r;
}

int
afb_api_common_queue_job_hookable(
	const struct afb_api_common *comapi,
	void (*callback)(int signum, void *arg),
	void *argument,
	void *group,
	int timeout
) {
	int r = afb_api_common_queue_job(comapi, callback, argument, group, timeout);
	return afb_hook_api_queue_job(comapi, callback, argument, group, timeout, r);
}

int
afb_api_common_require_api_hookable(
	const struct afb_api_common *comapi,
	const char *name,
	int initialized
) {
	int result;
	afb_hook_api_require_api(comapi, name, initialized);
	result = afb_api_common_require_api(comapi, name, initialized);
	return afb_hook_api_require_api_result(comapi, name, initialized, result);
}

int
afb_api_common_add_alias_hookable(
	const struct afb_api_common *comapi,
	const char *apiname,
	const char *aliasname
) {
	int result = afb_api_common_add_alias(comapi, apiname, aliasname);
	return afb_hook_api_add_alias(comapi, apiname, aliasname, result);
}

void
afb_api_common_api_seal_hookable(
	struct afb_api_common *comapi
) {
	afb_hook_api_api_seal(comapi);
	afb_api_common_api_seal(comapi);
}

int
afb_api_common_class_provide_hookable(
	const struct afb_api_common *comapi,
	const char *name
) {
	int result = afb_api_common_class_provide(comapi, name);
	return afb_hook_api_class_provide(comapi, result, name);
}

int
afb_api_common_class_require_hookable(
	const struct afb_api_common *comapi,
	const char *name
) {
	int result = afb_api_common_class_require(comapi, name);
	return afb_hook_api_class_require(comapi, result, name);
}

struct json_object *
afb_api_common_settings_hookable(
	const struct afb_api_common *comapi
) {
	struct json_object *result = afb_api_common_settings(comapi);
	return afb_hook_api_settings(comapi, result);
}

#endif
/******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
                      L I S T E N E R S
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/

/*
 * Propagates the event to the service
 */
static void listener_of_events(void *closure, const char *event, uint16_t eventid, struct json_object *object)
{
	const struct globset_handler *handler;
	struct afb_api_common *comapi = closure;

#if WITH_AFB_HOOK
	/* hook the event before */
	if (comapi->hooksvc & afb_hook_flag_api_on_event)
		afb_hook_api_on_event_before(comapi, event, eventid, object);
#endif

	/* transmit to specific handlers */
	/* search the handler */
	handler = comapi->event_handlers ? globset_match(comapi->event_handlers, event) : NULL;
	if (handler) {
#if WITH_AFB_HOOK
		if (comapi->hooksvc & afb_hook_flag_api_on_event_handler)
			afb_hook_api_on_event_handler_before(comapi, event, eventid, object, handler->pattern);
#endif
		comapi->onevent(handler->callback, handler->closure, event, object, comapi);
#if WITH_AFB_HOOK
		if (comapi->hooksvc & afb_hook_flag_api_on_event_handler)
			afb_hook_api_on_event_handler_after(comapi, event, eventid, object, handler->pattern);
#endif
	} else {
		/* transmit to default handler */
		comapi->onevent(NULL, NULL, event, object, comapi);
	}

#if WITH_AFB_HOOK
	/* hook the event after */
	if (comapi->hooksvc & afb_hook_flag_api_on_event)
		afb_hook_api_on_event_after(comapi, event, eventid, object);
#endif
	json_object_put(object);
}

static void listener_of_pushed_events(void *closure, const char *event, uint16_t eventid, struct json_object *object)
{
	listener_of_events(closure, event, eventid, object);
}

static void listener_of_broadcasted_events(void *closure, const char *event, struct json_object *object, const uuid_binary_t uuid, uint8_t hop)
{
	listener_of_events(closure, event, 0, object);
}

/* the interface for events */
static const struct afb_evt_itf evt_itf = {
	.broadcast = listener_of_broadcasted_events,
	.push = listener_of_pushed_events
};

/* ensure an existing listener */
static int ensure_listener(struct afb_api_common *comapi)
{
	int rc = 0;
	if (comapi->listener == NULL) {
		comapi->listener = afb_evt_listener_create(&evt_itf, comapi);
		if (comapi->listener == NULL)
			rc = X_ENOMEM;
	}
	return rc;
}

int
afb_api_common_subscribe(
	struct afb_api_common *comapi,
	struct afb_evt *evt
) {
	int rc = ensure_listener(comapi);
	return rc < 0 ? rc : evt ? afb_evt_listener_watch_evt(comapi->listener, evt) : X_EINVAL;
}

int
afb_api_common_unsubscribe(
	struct afb_api_common *comapi,
	struct afb_evt *evt
) {
	int rc = ensure_listener(comapi);
	return rc < 0 ? rc : evt ? afb_evt_listener_unwatch_evt(comapi->listener, evt) : X_EINVAL;
}

int
afb_api_common_event_handler_add(
	struct afb_api_common *comapi,
	const char *pattern,
	void *callback,
	void *closure
) {
	int rc;

	/* ensure the listener */
	rc = ensure_listener(comapi);
	if (rc < 0)
		return rc;

	/* ensure the globset for event handling */
	if (!comapi->event_handlers) {
		comapi->event_handlers = globset_create();
		if (!comapi->event_handlers) {
			goto oom_error;
		}
	}

	/* add the handler */
	rc = globset_add(comapi->event_handlers, pattern, callback, closure);
	if (rc == 0)
		return 0;

	if (rc == X_EEXIST) {
		ERROR("[API %s] event handler %s already exists", comapi->name, pattern);
		return rc;
	}

oom_error:
	ERROR("[API %s] can't allocate event handler %s", comapi->name, pattern);
	return X_ENOMEM;
}

int
afb_api_common_event_handler_del(
	struct afb_api_common *comapi,
	const char *pattern,
	void **closure
) {
	if (comapi->event_handlers
	&& !globset_del(comapi->event_handlers, pattern, closure))
		return 0;

	ERROR("[API %s] event handler %s not found", comapi->name, pattern);
	return X_ENOENT;
}

/******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
                                           M E R G E D
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/

void
afb_api_common_init(
	struct afb_api_common *comapi,
	struct afb_apiset *declare_set,
	struct afb_apiset *call_set,
	const char *name,
	int free_name,
	const char *info,
	int free_info,
	const char *path,
	int free_path
) {
	/* name */
	comapi->name = name;
	comapi->free_name = free_name ? 1 : 0;

	/* info */
	comapi->info = info;
	comapi->free_info = free_info ? 1 : 0;

	/* path */
	comapi->path = path;
	comapi->free_path = free_path ? 1 : 0;

	/* apiset the API is declared in */
	comapi->declare_set = afb_apiset_addref(declare_set);

	/* apiset for calls */
	comapi->call_set = afb_apiset_addref(call_set);

	/* reference count */
	comapi->refcount = 1;

	/* current state */
	comapi->state = Api_State_Pre_Init;

	/* not sealed */
	comapi->sealed = 0;

	/* event listener for service or NULL */
	comapi->listener = NULL;

	/* event handler list */
	comapi->event_handlers = NULL;

	/* handler of events */
	comapi->onevent = NULL;

#if WITH_API_SESSIONS
	comapi->session = afb_session_addref(session_get_common());
#endif

#if WITH_AFB_HOOK
	/* hooking flags */
	comapi->hookditf = 0; /* historical Daemon InTerFace */
	comapi->hooksvc = 0;  /* historical SerViCe interface */
#endif

	/* settings */
	comapi->settings = NULL;
}

void
afb_api_common_cleanup(
	struct afb_api_common *comapi
) {
	if (comapi->event_handlers)
		globset_destroy(comapi->event_handlers);
	if (comapi->listener != NULL)
		afb_evt_listener_unref(comapi->listener);
	afb_apiset_unref(comapi->declare_set);
	afb_apiset_unref(comapi->call_set);
	json_object_put(comapi->settings);
	session_cleanup(comapi);
	if (comapi->free_name)
		free((void*)comapi->name);
	if (comapi->free_info)
		free((void*)comapi->info);
	if (comapi->free_path)
		free((void*)comapi->path);
}

void
afb_api_common_incref(
	struct afb_api_common *comapi
) {
	__atomic_add_fetch(&comapi->refcount, 1, __ATOMIC_RELAXED);
}

int
afb_api_common_decref(
	struct afb_api_common *comapi
) {
	return !__atomic_sub_fetch(&comapi->refcount, 1, __ATOMIC_RELAXED);
}

#if WITH_AFB_HOOK
int
afb_api_common_update_hook(
	struct afb_api_common *comapi
) {
	comapi->hookditf = afb_hook_flags_api(comapi->name);
	comapi->hooksvc = afb_hook_flags_api(comapi->name);
	return comapi->hookditf | comapi->hooksvc;
}
#endif


/******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
                                           N E W
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/

struct start
{
	int rc;
	int (*startcb)(void *closure);
	void *closure;
};

static void do_start(int sig, void *closure)
{
	struct start *start = closure;

	start->rc = sig ? X_EFAULT : start->startcb ? start->startcb(start->closure) : 0;
};

int
afb_api_common_start(
	struct afb_api_common *comapi,
	int (*startcb)(void *closure),
	void *closure
) {
	struct start start;

	/* check state */
	switch (comapi->state) {
	case Api_State_Run:
		return 0;

	case Api_State_Init:
		/* starting in progress: it is an error */
		ERROR("Service of API %s required started while starting", comapi->name);
		return X_EBUSY;

	default:
		break;
	}
	comapi->state = Api_State_Init;

#if WITH_AFB_HOOK
	/* Starts the service */
	if (comapi->hooksvc & afb_hook_flag_api_start)
		afb_hook_api_start_before(comapi);
#endif

	start.startcb = startcb;
	start.closure = closure;
	afb_sig_monitor_run(0, do_start, &start);

#if WITH_AFB_HOOK
	if (comapi->hooksvc & afb_hook_flag_api_start)
		afb_hook_api_start_after(comapi, start.rc);
#endif

	if (start.rc < 0) {
		/* initialisation error */
		ERROR("Initialisation of service API %s failed (%d): %s", comapi->name, start.rc, strerror(-start.rc));
		comapi->state = Api_State_Error;
	}
	else {
		comapi->state = Api_State_Run;
	}

	return start.rc;
}
