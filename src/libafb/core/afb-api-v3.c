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
#include <stdint.h>

#include <json-c/json.h>

#define AFB_BINDING_VERSION 0
#include <afb/afb-binding.h>

#include "containerof.h"

#include "core/afb-apiname.h"
#include "core/afb-apiset.h"
#include "core/afb-api-common.h"
#include "core/afb-api-v3.h"
#include "core/afb-auth.h"
#include "core/afb-common.h"
#include "core/afb-evt.h"
#include "core/afb-hook.h"
#include "core/afb-msg-json.h"
#include "core/afb-session.h"
#include "core/afb-req-common.h"
#include "core/afb-req-v3.h"
#include "core/afb-sched.h"
#include "core/afb-calls.h"
#include "core/afb-error-text.h"
#include "core/afb-string-mode.h"

#if WITH_SYSTEMD
#include "sys/systemd.h"
#endif
#include "sys/verbose.h"
#include "utils/globmatch.h"
#include "utils/globset.h"
#include "core/afb-sig-monitor.h"
#include "utils/wrap-json.h"
#include "sys/x-realpath.h"
#include "sys/x-errno.h"

/*************************************************************************
 * internal types
 ************************************************************************/

/*
 * structure of the exported API
 */
struct afb_api_v3
{
	/* the common api */
	struct afb_api_common comapi;

	/* start function */
	int (*init)(struct afb_api_x3 *api);

	/* event handling */
	void (*on_any_event_v3)(struct afb_api_x3 *api, const char *event, struct json_object *object);

	/* settings */
	struct json_object *settings;

	/* verbs */
	struct {
		const struct afb_verb_v3 *statics;
		struct afb_verb_v3 **dynamics;
	} verbs;
	uint16_t dyn_verb_count;

	/* interface with remainers */
	struct afb_api_x3 xapi;

	/* strings */
	char strings[];
};

/*****************************************************************************/

static inline struct afb_api_v3 *api_common_to_afb_api_v3(const struct afb_api_common *comapi)
{
	return containerof(struct afb_api_v3, comapi, comapi);
}

static inline struct afb_api_v3 *api_x3_to_api_v3(const struct afb_api_x3 *apix3)
{
	return containerof(struct afb_api_v3, xapi, apix3);
}

static inline struct afb_api_x3 *api_v3_to_api_x3(const struct afb_api_v3 *apiv3)
{
	return (struct afb_api_x3*)&apiv3->xapi; /* remove const on pupose */
}

static inline struct afb_api_common *api_v3_to_api_common(const struct afb_api_v3 *apiv3)
{
	return (struct afb_api_common*)&apiv3->comapi; /* remove const on pupose */
}

static inline struct afb_api_common *api_x3_to_api_common(const struct afb_api_x3 *apix3)
{
	return api_v3_to_api_common(api_x3_to_api_v3(apix3));
}

static inline struct afb_api_x3 *api_common_to_api_x3(const struct afb_api_common *comapi)
{
	return api_v3_to_api_x3(api_common_to_afb_api_v3(comapi));
}

/*****************************************************************************/

static inline int is_sealed(const struct afb_api_v3 *apiv3)
{
	return afb_api_common_is_sealed(api_v3_to_api_common(apiv3));
}

/******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
                                           C O M M O N
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/

#define CLOSURE_T                      struct afb_api_x3
#define CLOSURE_TO_COMMON_API(closure) api_x3_to_api_common(closure)
#include "afb-api-common.inc"
#undef CLOSURE_TO_COMMON_API
#undef CLOSURE_T

/******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
                                           V 3    S P E C I F I C
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/

/**********************************************
* normal flow
**********************************************/
static struct sd_event *get_event_loop(struct afb_api_x3 *closure)
{
#if WITH_SYSTEMD
	afb_sched_acquire_event_manager();
	return systemd_get_event_loop();
#else
	return NULL;
#endif
}

static struct sd_bus *get_user_bus(struct afb_api_x3 *closure)
{
#if WITH_SYSTEMD
	afb_sched_acquire_event_manager();
	return systemd_get_user_bus();
#else
	return NULL;
#endif
}

static struct sd_bus *get_system_bus(struct afb_api_x3 *closure)
{
#if WITH_SYSTEMD
	afb_sched_acquire_event_manager();
	return systemd_get_system_bus();
#else
	return NULL;
#endif
}

static int rootdir_get_fd_cb(struct afb_api_x3 *closure)
{
#if WITH_OPENAT
	return afb_common_rootdir_get_fd();
#else
	return X_ENOTSUP;
#endif
}

static int rootdir_open_locale_cb(struct afb_api_x3 *closure, const char *filename, int flags, const char *locale)
{
	return afb_common_rootdir_open_locale(filename, flags, locale);
}

static
struct afb_api_x3 *
api_new_api_cb(
	struct afb_api_x3 *closure,
	const char *name,
	const char *info,
	int noconcurrency,
	int (*preinit)(void*, struct afb_api_x3 *),
	void *preinit_closure
) {
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(closure);
	struct afb_api_v3 *newapi;
	int rc;

	rc = afb_api_v3_create(
		&newapi,
		apiv3->comapi.declare_set,
		apiv3->comapi.call_set,
		name, Afb_String_Copy,
		info, Afb_String_Copy,
		noconcurrency,
		NULL, NULL,
		apiv3->comapi.path, Afb_String_Const);

	if (rc >= 0 && preinit != NULL) {
		rc = preinit(preinit_closure, api_v3_to_api_x3(newapi));
		if (rc < 0)
			afb_api_v3_unref(newapi);
	}

	return rc >= 0 ? api_v3_to_api_x3(newapi) : NULL;
}

/**********************************************
* hooked flow
**********************************************/
#if WITH_AFB_HOOK
static struct sd_event *hooked_get_event_loop(struct afb_api_x3 *closure)
{
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(closure);
	struct sd_event *r;

	r = get_event_loop(closure);
	return afb_hook_api_get_event_loop(api_v3_to_api_common(apiv3), r);
}

static struct sd_bus *hooked_get_user_bus(struct afb_api_x3 *closure)
{
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(closure);
	struct sd_bus *r;

	r = get_user_bus(closure);
	return afb_hook_api_get_user_bus(api_v3_to_api_common(apiv3), r);
}

static struct sd_bus *hooked_get_system_bus(struct afb_api_x3 *closure)
{
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(closure);
	struct sd_bus *r;

	r = get_system_bus(closure);
	return afb_hook_api_get_system_bus(api_v3_to_api_common(apiv3), r);
}

static int hooked_rootdir_get_fd(struct afb_api_x3 *closure)
{
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(closure);
	int r = rootdir_get_fd_cb(closure);
	return afb_hook_api_rootdir_get_fd(api_v3_to_api_common(apiv3), r);
}

static int hooked_rootdir_open_locale_cb(struct afb_api_x3 *closure, const char *filename, int flags, const char *locale)
{
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(closure);
	int r = rootdir_open_locale_cb(closure, filename, flags, locale);
	return afb_hook_api_rootdir_open_locale(api_v3_to_api_common(apiv3), filename, flags, locale, r);
}

static struct afb_api_x3 *hooked_api_new_api_cb(
		struct afb_api_x3 *closure,
		const char *api,
		const char *info,
		int noconcurrency,
		int (*preinit)(void*, struct afb_api_x3 *),
		void *preinit_closure)
{
	struct afb_api_x3 *result;
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(closure);
	afb_hook_api_new_api_before(api_v3_to_api_common(apiv3), api, info, noconcurrency);
	result = api_new_api_cb(closure, api, info, noconcurrency, preinit, preinit_closure);
	afb_hook_api_new_api_after(api_v3_to_api_common(apiv3), result ? 0 : X_ENOMEM, api);
	return result;
}

#endif

static void call_x3_cb(
	void *closure1,
	void *closure2,
	void *closure3,
	const struct afb_req_reply *reply
) {
	struct afb_api_x3 *apix3 = closure1;
	void (*callback)(void*, struct json_object*, const char*, const char*, struct afb_api_x3*) = closure2;
	callback(closure3, reply->object, reply->error, reply->info, apix3);
}

static void call_x3(
	struct afb_api_x3 *apix3,
	const char *api,
	const char *verb,
	struct json_object *args,
	void (*callback)(void*, struct json_object*, const char *error, const char *info, struct afb_api_x3*),
	void *closure)
{
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(apix3);
	return afb_calls_call(api_v3_to_api_common(apiv3), api, verb, args, call_x3_cb, apix3, callback, closure);
}

static int call_sync_x3(
		struct afb_api_x3 *apix3,
		const char *api,
		const char *verb,
		struct json_object *args,
		struct json_object **object,
		char **error,
		char **info)
{
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(apix3);
	struct afb_req_reply reply;
	int result;

	result = afb_calls_call_sync(api_v3_to_api_common(apiv3), api, verb, args, &reply);
	afb_req_reply_move_splitted(&reply, object, error, info);
	return result;
}

static void legacy_call_x3(
		struct afb_api_x3 *apix3,
		const char *api,
		const char *verb,
		struct json_object *args,
		void (*callback)(void*, int, struct json_object*, struct afb_api_x3*),
		void *closure)
{
	ERROR("Legacy calls are not supported");
	if (callback)
		callback(closure, X_ENOTSUP, NULL, apix3);
}

static int legacy_call_sync(
		struct afb_api_x3 *apix3,
		const char *api,
		const char *verb,
		struct json_object *args,
		struct json_object **result)
{
	ERROR("Legacy calls are not supported");
	if (result)
		*result = NULL;
	return X_ENOTSUP;
}

static
int
api_set_verbs_v2_cb(
	struct afb_api_x3 *api,
	const struct afb_verb_v2 *verbs
) {
	return X_ENOTSUP;
}

static
int
api_set_verbs_v3_cb(
	struct afb_api_x3 *api,
	const struct afb_verb_v3 *verbs
) {
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(api);

	if (is_sealed(apiv3))
		return X_EPERM;

	afb_api_v3_set_verbs_v3(apiv3, verbs);
	return 0;
}

static
int
api_add_verb_cb(
	struct afb_api_x3 *api,
	const char *verb,
	const char *info,
	void (*callback)(struct afb_req_x2 *req),
	void *vcbdata,
	const struct afb_auth *auth,
	uint32_t session,
	int glob
) {
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(api);

	if (is_sealed(apiv3))
		return X_EPERM;

	return afb_api_v3_add_verb(apiv3, verb, info, callback, vcbdata, auth, (uint16_t)session, glob);
}

static
int
api_del_verb_cb(
	struct afb_api_x3 *api,
	const char *verb,
	void **vcbdata
) {
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(api);

	if (is_sealed(apiv3))
		return X_EPERM;

	return afb_api_v3_del_verb(apiv3, verb, vcbdata);
}

static
int
api_set_on_event_cb(
	struct afb_api_x3 *api,
	void (*onevent)(struct afb_api_x3 *api, const char *event, struct json_object *object)
) {
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(api);
	apiv3->on_any_event_v3 = onevent;
	return 0;
}

static
int
api_set_on_init_cb(
	struct afb_api_x3 *api,
	int (*oninit)(struct afb_api_x3 *api)
) {
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(api);

	if (apiv3->comapi.state != Api_State_Pre_Init) {
		ERROR("[API %s] Bad call to 'afb_api_x3_on_init', must be in PreInit", apiv3->comapi.name);
		return X_EINVAL;
	}

	apiv3->init  = oninit;
	return 0;
}

static
int
event_handler_add_cb(
	struct afb_api_x3 *api,
	const char *pattern,
	void (*callback)(void *, const char*, struct json_object*, struct afb_api_x3*),
	void *closure
) {
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(api);

	return afb_api_common_event_handler_add(api_v3_to_api_common(apiv3), pattern, callback, closure);
}

static
int
event_handler_del_cb(
	struct afb_api_x3 *api,
	const char *pattern,
	void **closure
) {
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(api);

	return afb_api_common_event_handler_del(api_v3_to_api_common(apiv3), pattern, closure);
}

static
int
delete_api_cb(
	struct afb_api_x3 *api
) {
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(api);

	if (is_sealed(apiv3))
		return X_EPERM;

	afb_api_v3_unref(apiv3);
	return 0;
}

static const struct afb_api_x3_itf api_x3_itf = {

	.vverbose = (void*)vverbose_cb,

	.get_event_loop = get_event_loop,
	.get_user_bus = get_user_bus,
	.get_system_bus = get_system_bus,
	.rootdir_get_fd = rootdir_get_fd_cb,
	.rootdir_open_locale = rootdir_open_locale_cb,
	.queue_job = queue_job_cb,

	.require_api = require_api_cb,
	.add_alias = add_alias_cb,

	.event_broadcast = event_broadcast_cb,
	.event_make = event_x2_make_cb,

	.legacy_call = legacy_call_x3,
	.legacy_call_sync = legacy_call_sync,

	.api_new_api = api_new_api_cb,
	.api_set_verbs_v2 = api_set_verbs_v2_cb,
	.api_add_verb = api_add_verb_cb,
	.api_del_verb = api_del_verb_cb,
	.api_set_on_event = api_set_on_event_cb,
	.api_set_on_init = api_set_on_init_cb,
	.api_seal = api_seal_cb,
	.api_set_verbs_v3 = api_set_verbs_v3_cb,
	.event_handler_add = event_handler_add_cb,
	.event_handler_del = event_handler_del_cb,

	.call = call_x3,
	.call_sync = call_sync_x3,

	.class_provide = class_provide_cb,
	.class_require = class_require_cb,

	.delete_api = delete_api_cb,
	.settings = settings_cb,
};

/**********************************************
* hooked flow
**********************************************/
#if WITH_AFB_HOOK
static void hooked_call_x3(
		struct afb_api_x3 *apix3,
		const char *api,
		const char *verb,
		struct json_object *args,
		void (*callback)(void*, struct json_object*, const char*, const char*, struct afb_api_x3*),
		void *closure)
{
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(apix3);
	return afb_calls_call_hookable(api_v3_to_api_common(apiv3), api, verb, args, call_x3_cb, apix3, callback, closure);
}

static int hooked_call_sync_x3(
		struct afb_api_x3 *apix3,
		const char *api,
		const char *verb,
		struct json_object *args,
		struct json_object **object,
		char **error,
		char **info)
{
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(apix3);
	struct afb_req_reply reply;
	int result;

	result = afb_calls_call_sync(api_v3_to_api_common(apiv3), api, verb, args, &reply);
	afb_req_reply_move_splitted(&reply, object, error, info);
	return result;
}

static void legacy_hooked_call_x3(
		struct afb_api_x3 *apix3,
		const char *api,
		const char *verb,
		struct json_object *args,
		void (*callback)(void*, int, struct json_object*, struct afb_api_x3*),
		void *closure)
{
	return legacy_call_x3(apix3, api, verb, args, callback, closure);
}

static int legacy_hooked_call_sync(
		struct afb_api_x3 *apix3,
		const char *api,
		const char *verb,
		struct json_object *args,
		struct json_object **result)
{
	return legacy_call_sync(apix3, api, verb, args, result);
}

static int hooked_api_set_verbs_v2_cb(
		struct afb_api_x3 *api,
		const struct afb_verb_v2 *verbs)
{
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(api);
	int result = api_set_verbs_v2_cb(api, verbs);
	return afb_hook_api_api_set_verbs_v2(api_v3_to_api_common(apiv3), result, verbs);
}

static int hooked_api_set_verbs_v3_cb(
		struct afb_api_x3 *api,
		const struct afb_verb_v3 *verbs)
{
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(api);
	int result = api_set_verbs_v3_cb(api, verbs);
	return afb_hook_api_api_set_verbs_v3(api_v3_to_api_common(apiv3), result, verbs);
}

static int hooked_api_add_verb_cb(
		struct afb_api_x3 *api,
		const char *verb,
		const char *info,
		void (*callback)(struct afb_req_x2 *req),
		void *vcbdata,
		const struct afb_auth *auth,
		uint32_t session,
		int glob)
{
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(api);
	int result = api_add_verb_cb(api, verb, info, callback, vcbdata, auth, session, glob);
	return afb_hook_api_api_add_verb(api_v3_to_api_common(apiv3), result, verb, info, glob);
}

static int hooked_api_del_verb_cb(
		struct afb_api_x3 *api,
		const char *verb,
		void **vcbdata)
{
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(api);
	int result = api_del_verb_cb(api, verb, vcbdata);
	return afb_hook_api_api_del_verb(api_v3_to_api_common(apiv3), result, verb);
}

static int hooked_api_set_on_event_cb(
		struct afb_api_x3 *api,
		void (*onevent)(struct afb_api_x3 *api, const char *event, struct json_object *object))
{
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(api);
	int result = api_set_on_event_cb(api, onevent);
	return afb_hook_api_api_set_on_event(api_v3_to_api_common(apiv3), result);
}

static int hooked_api_set_on_init_cb(
		struct afb_api_x3 *api,
		int (*oninit)(struct afb_api_x3 *api))
{
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(api);
	int result = api_set_on_init_cb(api, oninit);
	return afb_hook_api_api_set_on_init(api_v3_to_api_common(apiv3), result);
}

static int hooked_event_handler_add_cb(
		struct afb_api_x3 *api,
		const char *pattern,
		void (*callback)(void *, const char*, struct json_object*, struct afb_api_x3*),
		void *closure)
{
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(api);
	int result = event_handler_add_cb(api, pattern, callback, closure);
	return afb_hook_api_event_handler_add(api_v3_to_api_common(apiv3), result, pattern);
}

static int hooked_event_handler_del_cb(
		struct afb_api_x3 *api,
		const char *pattern,
		void **closure)
{
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(api);
	int result = event_handler_del_cb(api, pattern, closure);
	return afb_hook_api_event_handler_del(api_v3_to_api_common(apiv3), result, pattern);
}

static int hooked_delete_api_cb(struct afb_api_x3 *api)
{
	struct afb_api_v3 *apiv3 = afb_api_v3_addref(api_x3_to_api_v3(api));
	int result = delete_api_cb(api);
	result = afb_hook_api_delete_api(api_v3_to_api_common(apiv3), result);
	afb_api_v3_unref(apiv3);
	return result;
}

static const struct afb_api_x3_itf hooked_api_x3_itf = {

	.vverbose = hooked_vverbose_cb,

	.get_event_loop = hooked_get_event_loop,
	.get_user_bus = hooked_get_user_bus,
	.get_system_bus = hooked_get_system_bus,
	.rootdir_get_fd = hooked_rootdir_get_fd,
	.rootdir_open_locale = hooked_rootdir_open_locale_cb,
	.queue_job = hooked_queue_job_cb,

	.require_api = hooked_require_api_cb,
	.add_alias = hooked_add_alias_cb,

	.event_broadcast = hooked_event_broadcast_cb,
	.event_make = hooked_event_x2_make_cb,

	.legacy_call = legacy_hooked_call_x3,
	.legacy_call_sync = legacy_hooked_call_sync,

	.api_new_api = hooked_api_new_api_cb,
	.api_set_verbs_v2 = hooked_api_set_verbs_v2_cb,
	.api_add_verb = hooked_api_add_verb_cb,
	.api_del_verb = hooked_api_del_verb_cb,
	.api_set_on_event = hooked_api_set_on_event_cb,
	.api_set_on_init = hooked_api_set_on_init_cb,
	.api_seal = hooked_api_seal_cb,
	.api_set_verbs_v3 = hooked_api_set_verbs_v3_cb,
	.event_handler_add = hooked_event_handler_add_cb,
	.event_handler_del = hooked_event_handler_del_cb,

	.call = hooked_call_x3,
	.call_sync = hooked_call_sync_x3,

	.class_provide = hooked_class_provide_cb,
	.class_require = hooked_class_require_cb,

	.delete_api = hooked_delete_api_cb,
	.settings = hooked_settings_cb,
};
#endif

/******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
                                  H A N D L I N G   O F   E V E N T S
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/

/* handler of events */
static
void
handle_events(
	void *callback,
	void *closure,
	const char *event,
	struct json_object *object,
	struct afb_api_common *comapi
) {
	void (*cb)(void *, const char*, struct json_object*, struct afb_api_x3*) = callback;
	struct afb_api_v3 *apiv3 = api_common_to_afb_api_v3(comapi);
	struct afb_api_x3 *apix3 = api_v3_to_api_x3(apiv3);

	if (cb != NULL)
		cb(closure, event, object, apix3);
	else if (apiv3->on_any_event_v3 != NULL)
		apiv3->on_any_event_v3(apix3, event, object);
}

/******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
                                          I N T E R F A C E    A P I S E T
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/

static
int
api_service_start_cb(
	void *closure
) {
	struct afb_api_v3 *apiv3 = closure;

	return afb_api_common_start(
		api_v3_to_api_common(apiv3),
		(int(*)(void*))apiv3->init,
		api_v3_to_api_x3(apiv3));
}

static void api_process_cb(void *closure, struct afb_req_common *req)
	__attribute__((alias("afb_api_v3_process_call")));

int afb_api_v3_logmask_get(const struct afb_api_v3 *apiv3)
{
	return apiv3->xapi.logmask;
}

void afb_api_v3_logmask_set(struct afb_api_v3 *apiv3, int mask)
{
	apiv3->xapi.logmask = mask;
}

#if WITH_AFB_HOOK
void
afb_api_v3_update_hooks(
	struct afb_api_v3 *apiv3
) {
	apiv3->xapi.itf = afb_api_common_update_hook(api_v3_to_api_common(apiv3)) ? &hooked_api_x3_itf : &api_x3_itf;
}

static void api_update_hooks_cb(void *closure)
	__attribute__((alias("afb_api_v3_update_hooks")));
#endif

static int api_get_logmask_cb(void *closure)
	__attribute__((alias("afb_api_v3_logmask_get")));

static void api_set_logmask_cb(void *closure, int level)
	__attribute__((alias("afb_api_v3_logmask_set")));

static void api_describe_cb(void *closure, void (*describecb)(void *, struct json_object *), void *clocb)
{
	struct afb_api_v3 *apiv3 = closure;
	describecb(clocb, afb_api_v3_make_description_openAPIv3(apiv3));
}

static void api_unref_cb(void *closure)
	__attribute__((alias("afb_api_v3_unref")));

static struct afb_api_itf export_api_itf =
{
	.process = api_process_cb,
	.service_start = api_service_start_cb,
#if WITH_AFB_HOOK
	.update_hooks = api_update_hooks_cb,
#endif
	.get_logmask = api_get_logmask_cb,
	.set_logmask = api_set_logmask_cb,
	.describe = api_describe_cb,
	.unref = api_unref_cb
};

/******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
                                          I N T E R F A C E    A P I S E T
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/

static int verb_name_compare(const struct afb_verb_v3 *verb, const char *name)
{
	return verb->glob
		? fnmatch(verb->verb, name, FNM_NOESCAPE|FNM_PATHNAME|FNM_CASEFOLD|FNM_PERIOD)
		: strcasecmp(verb->verb, name);
}

static struct afb_verb_v3 *search_dynamic_verb(struct afb_api_v3 *api, const char *name)
{
	struct afb_verb_v3 **v, **e, *i;

	v = api->verbs.dynamics;
	e = &v[api->dyn_verb_count];
	while (v != e) {
		i = *v;
		if (!verb_name_compare(i, name))
			return i;
		v++;
	}
	return 0;
}

void
afb_api_v3_process_call(
	struct afb_api_v3 *api,
	struct afb_req_common *req
) {
	const struct afb_verb_v3 *verbsv3;
	const char *name;

	name = req->verbname;

	/* look first in dynamic set */
	verbsv3 = search_dynamic_verb(api, name);
	if (!verbsv3) {
		/* look then in static set */
		verbsv3 = api->verbs.statics;
		while (verbsv3) {
			if (!verbsv3->verb)
				verbsv3 = 0;
			else if (!verb_name_compare(verbsv3, name))
				break;
			else
				verbsv3++;
		}
	}
	/* is it a v3 verb ? */
	if (verbsv3) {
		/* yes */
		afb_req_v3_process(req, api, api_v3_to_api_x3(api), verbsv3);
		return;
	}

	afb_req_common_reply_verb_unknown(req);
}

static
struct json_object *
describe_verb_v3(
	const struct afb_verb_v3 *verb
) {
	struct json_object *f, *a, *g;

	f = json_object_new_object();

	g = json_object_new_object();
	json_object_object_add(f, "get", g);

	a = afb_auth_json_x2(verb->auth, verb->session);
	if (a)
		json_object_object_add(g, "x-permissions", a);

	a = json_object_new_object();
	json_object_object_add(g, "responses", a);
	g = json_object_new_object();
	json_object_object_add(a, "200", g);
	json_object_object_add(g, "description", json_object_new_string(verb->info?:verb->verb));

	return f;
}

struct json_object *
afb_api_v3_make_description_openAPIv3(
	struct afb_api_v3 *api
) {
	char buffer[256];
	struct afb_verb_v3 **iter, **end;
	const struct afb_verb_v3 *verb;
	struct json_object *r, *i, *p;

	r = json_object_new_object();
	json_object_object_add(r, "openapi", json_object_new_string("3.0.0"));

	i = json_object_new_object();
	json_object_object_add(r, "info", i);
	json_object_object_add(i, "title", json_object_new_string(api->comapi.name));
	json_object_object_add(i, "version", json_object_new_string("0.0.0"));
	if (api->comapi.info)
		json_object_object_add(i, "description", json_object_new_string(api->comapi.info));

	buffer[0] = '/';
	buffer[sizeof buffer - 1] = 0;

	p = json_object_new_object();
	json_object_object_add(r, "paths", p);
	iter = api->verbs.dynamics;
	end = iter + api->dyn_verb_count;
	while (iter != end) {
		verb = *iter++;
		strncpy(buffer + 1, verb->verb, sizeof buffer - 2);
		json_object_object_add(p, buffer, describe_verb_v3(verb));
	}
	verb = api->verbs.statics;
	if (verb)
		while(verb->verb) {
			strncpy(buffer + 1, verb->verb, sizeof buffer - 2);
			json_object_object_add(p, buffer, describe_verb_v3(verb));
			verb++;
		}
	return r;
}


struct afb_api_v3 *
afb_api_v3_addref(
	struct afb_api_v3 *apiv3
) {
	if (apiv3)
		afb_api_common_incref(api_v3_to_api_common(apiv3));
	return apiv3;
}

void
afb_api_v3_unref(
	struct afb_api_v3 *apiv3
) {
	if (apiv3 && afb_api_common_decref(api_v3_to_api_common(apiv3))) {
		if (apiv3->comapi.name != NULL)
			afb_apiset_del(apiv3->comapi.declare_set, apiv3->comapi.name);
		afb_api_common_cleanup(api_v3_to_api_common(apiv3));
		while (apiv3->dyn_verb_count)
			free(apiv3->verbs.dynamics[--apiv3->dyn_verb_count]);
		free(apiv3->verbs.dynamics);
		free(apiv3);
	}
}

struct afb_api_common *
afb_api_v3_get_api_common(
	struct afb_api_v3 *apiv3
) {
	return api_v3_to_api_common(apiv3);
}

struct afb_api_x3 *
afb_api_v3_get_api_x3(
	struct afb_api_v3 *apiv3
) {
	return api_v3_to_api_x3(apiv3);
}

void
afb_api_v3_seal(
	struct afb_api_v3 *apiv3
) {
	afb_api_common_api_seal(api_v3_to_api_common(apiv3));
}

void
afb_api_v3_set_verbs_v3(
	struct afb_api_v3 *api,
	const struct afb_verb_v3 *verbs
) {
	api->verbs.statics = verbs;
}

int
afb_api_v3_add_verb(
	struct afb_api_v3 *api,
	const char *verb,
	const char *info,
	void (*callback)(struct afb_req_x2 *req),
	void *vcbdata,
	const struct afb_auth *auth,
	uint16_t session,
	int glob
) {
	struct afb_verb_v3 *v, **vv;
	char *txt;
	int i;

	for (i = 0 ; i < api->dyn_verb_count ; i++) {
		v = api->verbs.dynamics[i];
		if (glob == v->glob && !strcasecmp(verb, v->verb)) {
			/* refuse to redefine a dynamic verb */
			return X_EEXIST;
		}
	}

	vv = realloc(api->verbs.dynamics, (1 + api->dyn_verb_count) * sizeof *vv);
	if (!vv)
		return X_ENOMEM;
	api->verbs.dynamics = vv;

	v = malloc(sizeof *v + (1 + strlen(verb)) + (info ? 1 + strlen(info) : 0));
	if (!v)
		return X_ENOMEM;

	v->callback = callback;
	v->vcbdata = vcbdata;
	v->auth = auth;
	v->session = session;
	v->glob = !!glob;

	txt = (char*)(v + 1);
	v->verb = txt;
	txt = stpcpy(txt, verb);
	if (!info)
		v->info = NULL;
	else {
		v->info = ++txt;
		strcpy(txt, info);
	}

	api->verbs.dynamics[api->dyn_verb_count++] = v;
	return 0;
}

int
afb_api_v3_del_verb(
	struct afb_api_v3 *api,
	const char *verb,
	void **vcbdata
) {
	struct afb_verb_v3 *v;
	int i;

	for (i = 0 ; i < api->dyn_verb_count ; i++) {
		v = api->verbs.dynamics[i];
		if (!strcasecmp(verb, v->verb)) {
			api->verbs.dynamics[i] = api->verbs.dynamics[--api->dyn_verb_count];
			if (vcbdata)
				*vcbdata = v->vcbdata;
			free(v);
			return 0;
		}
	}

	return X_ENOENT;
}

int
afb_api_v3_set_binding_fields(
	struct afb_api_v3 *apiv3,
	const struct afb_binding_v3 *desc
) {
	int rc = 0;
	apiv3->xapi.userdata = desc->userdata;
	apiv3->verbs.statics = desc->verbs;
	apiv3->on_any_event_v3 =  desc->onevent;
	apiv3->init = desc->init;
	if (desc->provide_class)
		rc =  afb_api_common_class_provide(api_v3_to_api_common(apiv3), desc->provide_class);
	if (!rc && desc->require_class)
		rc =  afb_api_common_class_require(api_v3_to_api_common(apiv3), desc->require_class);
	if (!rc && desc->require_api)
		rc =  afb_api_common_require_api(api_v3_to_api_common(apiv3), desc->require_api, 0);
	return rc;
}

struct safe_preinit_data
{
	int (*preinit)(struct afb_api_x3 *);
	struct afb_api_v3 *api;
	int result;
};

static void safe_preinit(int sig, void *closure)
{
	struct safe_preinit_data *spd = closure;
	if (!sig)
		spd->result = spd->preinit(api_v3_to_api_x3(spd->api));
	else {
		spd->result = X_EFAULT;
	}
}

int
afb_api_v3_safe_preinit_x3(
	struct afb_api_v3 *apiv3,
	int (*preinit)(struct afb_api_x3 *)
) {
	struct safe_preinit_data spd;

	spd.preinit = preinit;
	spd.api = apiv3;
	afb_sig_monitor_run(60, safe_preinit, &spd);
	return spd.result;
}

int
afb_api_v3_preinit_x3(
	struct afb_api_v3 *apiv3,
	int (*preinit)(void*, struct afb_api_x3*),
	void *closure
) {
	return preinit(closure, api_v3_to_api_x3(apiv3));
}

int
afb_api_v3_create(
	struct afb_api_v3 **api,
	struct afb_apiset *declare_set,
	struct afb_apiset *call_set,
	const char *name,
	enum afb_string_mode mode_name,
	const char *info,
	enum afb_string_mode mode_info,
	int noconcurrency,
	int (*preinit)(void*, struct afb_api_v3 *),
	void *closure,
	const char* path,
	enum afb_string_mode mode_path
) {
	int rc;
	struct afb_api_v3 *apiv3;
	size_t strsz;
	char *ptr, *p;
	struct afb_api_item afb_api;
	strsz = 0;

	/* check the name */
	if (name == NULL) {
		mode_name = Afb_String_Const;
	}
	else {
		if (!afb_apiname_is_valid(name)) {
			rc = X_EINVAL;
			goto error;
		}
		if (afb_apiset_get_api(declare_set, name, 0, 0, NULL) == 0) {
			rc = X_EEXIST;
			goto error;
		}
		if (mode_name == Afb_String_Copy)
			strsz += 1 + strlen(name);
	}

	/* compute string size */
	if (info == NULL)
		mode_info = Afb_String_Const;
	else if (mode_info == Afb_String_Copy)
		strsz += 1 + strlen(info);
	if (path == NULL)
		mode_path = Afb_String_Const;
	else if (mode_path == Afb_String_Copy)
		strsz += 1 + strlen(path);

	/* allocates the description */
	apiv3 = malloc(strsz + sizeof *apiv3);
	if (!apiv3) {
		ERROR("out of memory");
		rc = X_ENOMEM;
		goto error;
	}

	/* init the structure */
	memset(apiv3, 0, sizeof *apiv3);
	ptr = apiv3->strings;
	if (mode_name == Afb_String_Copy) {
		p = ptr;
		ptr = stpcpy(ptr, name) + 1;
		name = p;
		mode_name = Afb_String_Const;
	}
	if (mode_info == Afb_String_Copy) {
		p = ptr;
		ptr = stpcpy(ptr, info) + 1;
		info = p;
		mode_info = Afb_String_Const;
	}
	if (mode_path == Afb_String_Copy) {
		p = ptr;
		ptr = stpcpy(ptr, path) + 1;
		path = p;
		mode_path = Afb_String_Const;
	}

	/* init comapi */
	afb_api_common_init(
		api_v3_to_api_common(apiv3),
		declare_set, call_set,
		name, mode_name == Afb_String_Free,
		info, mode_info == Afb_String_Free,
		path, mode_path == Afb_String_Free
	);
	apiv3->comapi.onevent = handle_events;

	/* init xapi */
	apiv3->xapi.apiname = apiv3->comapi.name;
#if WITH_AFB_HOOK
	afb_api_v3_update_hooks(apiv3);
#else
	apiv3->xapi.itf = &api_x3_itf;
#endif
	afb_api_v3_logmask_set(apiv3, logmask);

	/* declare the api */
	if (name != NULL) {
		afb_api.closure = afb_api_v3_addref(apiv3);
		afb_api.itf = &export_api_itf;
		afb_api.group = noconcurrency ? apiv3 : NULL;
		rc = afb_apiset_add(apiv3->comapi.declare_set, apiv3->comapi.name, afb_api);
		if (rc < 0) {
			goto error2;
		}
	}

	/* pre-init of the api */
	if (preinit) {
		rc = preinit(closure, apiv3);
		if (rc < 0)
			goto error3;
	}

	*api = apiv3;
	return 0;

error3:
	if (name != NULL) {
		afb_apiset_del(apiv3->comapi.declare_set, apiv3->comapi.name);
	}
error2:
	afb_api_common_cleanup(api_v3_to_api_common(apiv3));
	free(apiv3);

error:
	*api = NULL;
	return rc;
}

static int init_binding(void *closure, struct afb_api_v3 *apiv3)
{
	const struct afb_binding_v3 *desc = closure;
	int rc = afb_api_v3_set_binding_fields(apiv3, desc);
	if (!rc && desc->preinit)
		rc = afb_api_v3_safe_preinit_x3(apiv3, desc->preinit);
	return rc;
}

int
afb_api_v3_from_binding(
	struct afb_api_v3 **api,
	const struct afb_binding_v3 *desc,
	struct afb_apiset *declare_set,
	struct afb_apiset *call_set
) {
	return afb_api_v3_create(
			api,
			declare_set, call_set,
			desc->api, Afb_String_Const,
			desc->info, Afb_String_Const,
			desc->noconcurrency,
			init_binding, (void*)desc,
			NULL, Afb_String_Const
		);
}
