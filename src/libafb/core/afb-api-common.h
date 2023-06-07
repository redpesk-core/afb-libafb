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

#pragma once

#include "../libafb-config.h"

#include <stdint.h>
#include <stdarg.h>

struct json_object;

struct afb_apiset;
struct afb_session;
struct afb_evt_listener;
struct afb_req_common;

struct afb_evt;
struct afb_evt_data;
struct afb_data;

struct afb_event_x2;

struct globset;

/**
 * Set the global configconfiguration of all apis
 * This is a json object whose keys are the name of
 * the apis. The special key '*' is used for all apis.
 * @param config the config to be recorded
 */
extern
void
afb_api_common_set_config(
	struct json_object *config
);

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

	/* internal dirty flag */
	uint16_t dirty: 1;

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
	void (*onevent)(void *callback, void *closure, const struct afb_evt_data *event, struct afb_api_common *comapi);

	/* settings */
	struct json_object *settings;

	/* session for service */
	struct afb_session *session;

#if WITH_AFB_HOOK
	/* hooking flags */
	unsigned hookflags; /* historical Daemon InTerFace */
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
 *
 * @see afb_api_common_cleanup
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
 *
 * @see afb_api_common_decref
 */
extern
void
afb_api_common_cleanup(
	struct afb_api_common *comapi
);

/**
 * Call the starting function 'startcb' with the
 * given closure. Manages the state. If the api
 * is already started, do nothing. If the api
 * is currently starting, returns an error.
 * Otherwise run the start callback in a protected
 * environment.
 *
 * @param api the api
 * @param startcb the start function to call
 * @param closure the closure of the start function
 *
 * @return 0 on success or a negative error code
 */
extern
int
afb_api_common_start(
	struct afb_api_common *comapi,
	int (*startcb)(void *closure),
	void *closure
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
 * Get the visible name of the api
 *
 * @param comapi the api
 *
 * @return the name of the pai or its path
 */
static inline
const char *
afb_api_common_visible_name(
	const struct afb_api_common *comapi
) {
	return comapi->name ?: comapi->path;
}

/**
 * Get the apiset for calling
 *
 * @param comapi the api
 *
 * @return the apiset for issues calls
 */
static inline
struct afb_apiset *
afb_api_common_call_set(
	struct afb_api_common *comapi
) {
	return comapi->call_set;
}

/**
 * Get the apiset of declaration
 *
 * @param comapi the api
 *
 * @return the apiset handling declaration of the current api
 */
static inline
struct afb_apiset *
afb_api_common_declare_set(
	struct afb_api_common *comapi
) {
	return comapi->declare_set;
}

/**
 * Get the session of the api
 * @param comapi the api
 * @return its session (no add ref is done, the caller
 * should issue one if it keeps the session but not the api)
 */
extern
struct afb_session *
afb_api_common_session_get(
	struct afb_api_common *comapi
);

/**
 * Get the common session of APIs
 * @return the common session of APIs
 */
extern
struct afb_session *
afb_api_common_get_common_session();

/**
 * Set the common UID of the API's session
 * @param uuid the uid of the session or NULL for a random one
 * @return 0 on success or a negative error code
 */
extern
int
afb_api_common_set_common_session_uuid(
	const char *uuid
);

/**
 * Tell the api to use its own session
 *
 * @param comapi the api
 * @return 0 on success or a negative error code
 */
extern
int
afb_api_common_unshare_session(
	struct afb_api_common *comapi
);

/**
 * Get the settings of the api
 * @param comapi the api
 * @return its settings
 */
extern
struct json_object *
afb_api_common_settings(
	const struct afb_api_common *comapi
);

/**
 * Tells the api requires the given names
 * @param comapi  the api
 * @param name specification of required apis (a space separated list)
 * @return 0 on success or a negative error code
 */
extern
int
afb_api_common_require_api(
	const struct afb_api_common *comapi,
	const char *name,
	int initialized
);

/**
 * Tells the api requires the given names
 * @param comapi  the api
 * @param name specification of required apis (a space separated list)
 * @return 0 on success or a negative error code
 */
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

/**
 * add an alias in the callset of the api
 * @param comapi  the api
 * @param apiname the name to alias (if null the name of the api is used)
 * @param aliasname the aliased name to declare
 * @return 0 on success or a negative error code
 */
extern
int
afb_api_common_add_alias(
	const struct afb_api_common *comapi,
	const char *apiname,
	const char *aliasname
);

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
 * Seal the api. No verb can be added or removed on sealed api.
 *
 * @param comapi the api to seal
 */
extern
void
afb_api_common_api_seal(
	struct afb_api_common *comapi
);


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
afb_api_common_subscribe(
	struct afb_api_common *comapi,
	struct afb_evt *evt
);

extern
int
afb_api_common_unsubscribe(
	struct afb_api_common *comapi,
	struct afb_evt *evt
);

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
int
afb_api_common_new_event(
	const struct afb_api_common *comapi,
	const char *name,
	struct afb_evt **evt
);

extern
int
afb_api_common_event_broadcast(
	const struct afb_api_common *comapi,
	const char *name,
	unsigned nparams,
	struct afb_data * const params[]
);

extern
int
afb_api_common_post_job(
	const struct afb_api_common *comapi,
	long delayms,
	int timeout,
	void (*callback)(int signum, void *arg),
	void *argument,
	void *group
);

extern
int
afb_api_common_abort_job(
	const struct afb_api_common *comapi,
	int jobid
);

/***************************************************************************
* SECTION of HOOKABLES
* the functions belaow are the same than the ones above but may be hooked
***************************************************************************/
#if WITH_AFB_HOOK
/**
 * When hooks are available, that function update the hooksflags
 * based on the name of the api and returns the new value
 * @param comapi the api
 * @return the computed hook flags (0 if not hooked)
 */
extern
unsigned
afb_api_common_update_hook(
	struct afb_api_common *comapi
);
#endif

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
int
afb_api_common_new_event_hookable(
	const struct afb_api_common *comapi,
	const char *name,
	struct afb_evt **evt
);

extern
int
afb_api_common_event_broadcast_hookable(
	const struct afb_api_common *comapi,
	const char *name,
	unsigned nparams,
	struct afb_data * const params[]
);

extern
int
afb_api_common_post_job_hookable(
	const struct afb_api_common *comapi,
	long delayms,
	int timeout,
	void (*callback)(int signum, void *arg),
	void *argument,
	void *group
);

extern
int
afb_api_common_abort_job_hookable(
	const struct afb_api_common *comapi,
	int jobid
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

