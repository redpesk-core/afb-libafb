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

#pragma once

struct json_object;

struct afb_apiset;
struct afb_session;
struct afb_evt_listener;
struct afb_req_common;

struct afb_event_x2;

struct globset;

/******************************************************************************/
/**
 * The states of APIs
 */
enum afb_api_state
{
	Api_State_Pre_Init,
	Api_State_Init,
	Api_State_Class,
	Api_State_Run,
	Api_State_Error
};

/******************************************************************************/
/**
 * structure of the exported API
 */
struct afb_api_common
{
	/* reference count */
	uint16_t refcount;

	/* current state */
	uint16_t state: 4;

	/* unsealed */
	uint16_t sealed: 1;

	/* flags for freeing strings */
	uint16_t free_name: 1;
	uint16_t free_info: 1;
	uint16_t free_path: 1;

	/* initial name */
	const char *name;

	/* description */
	const char *info;

	/* path if any */
	const char *path;

	/* apiset the API is declared in */
	struct afb_apiset *declare_set;

	/* apiset for calls */
	struct afb_apiset *call_set;

	/* event listener for service or NULL */
	struct afb_evt_listener *listener;

	/* event handler list */
	struct globset *event_handlers;

	/* handler of events */
	void (*onevent)(void *callback, void *closure, const char *event, struct json_object *object, struct afb_api_common *comapi);

	/* settings */
	struct json_object *settings;

#if WITH_API_SESSIONS
	/* session for service */
	struct afb_session *session;
#endif

#if WITH_AFB_HOOK
	/* hooking flags */
	int hookditf; /* historical Daemon InTerFace */
	int hooksvc;  /* historical SerViCe interface */
#endif
};

/******************************************************************************/

/**
 * Initialisation of the common api structure
 * 
 * @param comapi        the api to initialize
 * @param declare_set   the apiset to declare the api
 * @param call_set      the apiset for calls of the api
 * @param name          name of the api
 * @param free_name     should free the name?
 * @param info          info of the api
 * @param free_info     should free the info?
 * @param path          path of the api
 * @param free_path     should free the path?
 */
extern
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
);

/**
 * Increment the reference count
 * 
 * @param comapi        the api
 */
extern
void
afb_api_common_incref(
	struct afb_api_common *comapi
);

/**
 * Decrement the reference count
 * 
 * @param comapi        the api
 * 
 * @return 1 if the reference count falled to 0 or otherwise,
 *         if the object is stiil referenced returns 0. In other
 *         words returns 1 if the api must be released.
 */
extern
int
afb_api_common_decref(
	struct afb_api_common *comapi
);

/**
 * Clean the content of the common api structure, release the
 * references it handles, free strings if required.
 * Must be called during destruction of api, after afb_api_common_decref
 * returned 1.
 * 
 * @param comapi        the api
 */
extern
void
afb_api_common_cleanup(
	struct afb_api_common *comapi
);

/**
 * Get the apiname (if any)
 * 
 * @param comapi the api
 * 
 * @return the name that can be NULL for anonymous api
 */
static inline
const char *
afb_api_common_apiname(
	const struct afb_api_common *comapi
) {
	return comapi->name;
}

/**
 * Is the api sealed?
 * 
 * @param comapi the api
 * 
 * @return 1 if sealed or 0 if not sealed
 */
static inline
int
afb_api_common_is_sealed(
	const struct afb_api_common *comapi
) {
	return comapi->sealed;
}

/**
 * Get the apiset for calling?
 * 
 * @param comapi the api
 * 
 * @return 
 */
static inline
struct afb_apiset *
afb_api_common_call_set(
	struct afb_api_common *comapi
) {
	return comapi->call_set;
}

extern
int
afb_api_common_start(
	struct afb_api_common *comapi,
	int (*startcb)(void *closure),
	void *closure
);


#if WITH_API_SESSIONS
extern
int
afb_api_common_unshare_session(
	struct afb_api_common *comapi
);
#endif

extern
struct afb_session *
afb_api_common_session_get(
	struct afb_api_common *comapi
);

/**
 * 
 */
extern
void
afb_api_common_set_config(
	struct json_object *config
);

#if WITH_AFB_HOOK
extern
int
afb_api_common_update_hook(
	struct afb_api_common *comapi
);
#endif

extern
int
afb_api_common_event_handler_add(
	struct afb_api_common *comapi,
	const char *pattern,
	void *callback,
	void *closure
);

extern
int
afb_api_common_event_handler_del(
	struct afb_api_common *comapi,
	const char *pattern,
	void **closure
);

extern
int
afb_api_common_subscribe_event_x2(
	struct afb_api_common *comapi,
	struct afb_event_x2 *event
);

extern
int
afb_api_common_unsubscribe_event_x2(
	struct afb_api_common *comapi,
	struct afb_event_x2 *event
);

/****************************************************************************/
/* functions below have helpers in afb-api-common.inc                       */
/****************************************************************************/

extern
void
afb_api_common_vverbose(
	const struct afb_api_common *comapi,
	int         level,
	const char *file,
	int         line,
	const char *function,
	const char *fmt,
	va_list     args
);

extern
struct afb_event_x2 *
afb_api_common_event_x2_make(
	const struct afb_api_common *comapi,
	const char *name
);

extern
int
afb_api_common_event_broadcast(
	const struct afb_api_common *comapi,
	const char *name,
	struct json_object *object
);

extern
int
afb_api_common_queue_job(
	const struct afb_api_common *comapi,
	void (*callback)(int signum, void *arg),
	void *argument,
	void *group,
	int timeout
);

extern
int
afb_api_common_require_api(
	const struct afb_api_common *comapi,
	const char *name,
	int initialized
);

extern
int
afb_api_common_add_alias(
	const struct afb_api_common *comapi,
	const char *apiname,
	const char *aliasname
);

extern
void
afb_api_common_api_seal(
	struct afb_api_common *comapi
);

extern
int
afb_api_common_class_provide(
	const struct afb_api_common *comapi,
	const char *name
);

extern
int
afb_api_common_class_require(
	const struct afb_api_common *comapi,
	const char *name
);

extern
struct json_object *
afb_api_common_settings(
	const struct afb_api_common *comapi
);

#if WITH_AFB_HOOK
extern
void
afb_api_common_vverbose_hookable(
	const struct afb_api_common *comapi,
	int         level,
	const char *file,
	int         line,
	const char *function,
	const char *fmt,
	va_list     args
);

extern
struct afb_event_x2 *
afb_api_common_event_x2_make_hookable(
	const struct afb_api_common *comapi,
	const char *name
);

extern
int
afb_api_common_event_broadcast_hookable(
	const struct afb_api_common *comapi,
	const char *name,
	struct json_object *object
);

extern
int
afb_api_common_queue_job_hookable(
	const struct afb_api_common *comapi,
	void (*callback)(int signum, void *arg),
	void *argument,
	void *group,
	int timeout
);

extern
int
afb_api_common_require_api_hookable(
	const struct afb_api_common *comapi,
	const char *name,
	int initialized
);

extern
int
afb_api_common_add_alias_hookable(
	const struct afb_api_common *comapi,
	const char *apiname,
	const char *aliasname
);

extern
void
afb_api_common_api_seal_hookable(
	struct afb_api_common *comapi
);

extern
int
afb_api_common_class_provide_hookable(
	const struct afb_api_common *comapi,
	const char *name
);

extern
int
afb_api_common_class_require_hookable(
	const struct afb_api_common *comapi,
	const char *name
);

extern
struct json_object *
afb_api_common_settings_hookable(
	const struct afb_api_common *comapi
);
#endif

