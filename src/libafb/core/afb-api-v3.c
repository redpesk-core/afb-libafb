/*
 * Copyright (C) 2015-2024 IoT.bzh Company
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

#include "../libafb-config.h"

#if WITH_BINDINGS_V3

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define AFB_BINDING_VERSION 0
#include <afb/afb-binding.h>
#if !WITHOUT_JSON_C
#include <rp-utils/rp-jsonc.h>
#endif
#include <rp-utils/rp-verbose.h>

#include "containerof.h"

#include "core/afb-apiname.h"
#include "core/afb-apiset.h"
#include "core/afb-api-common.h"
#include "core/afb-api-v3.h"
#include "core/afb-auth.h"
#include "core/afb-common.h"
#include "core/afb-evt.h"
#include "core/afb-hook.h"
#include "core/afb-data.h"
#include "core/afb-data-array.h"
#include "core/afb-json-legacy.h"
#include "core/afb-session.h"
#include "core/afb-req-common.h"
#include "core/afb-req-v3.h"
#include "core/afb-calls.h"
#include "core/afb-error-text.h"
#include "core/afb-string-mode.h"

#if WITH_SYSTEMD
#include "misc/afb-systemd.h"
#endif
#include "utils/globmatch.h"
#include "utils/globset.h"
#include "core/afb-sig-monitor.h"
#include "utils/namecmp.h"
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

	/* interface with remainers */
	struct afb_api_x3 xapi;

	/* start function */
	int (*init)(struct afb_api_x3 *apix3);

	/* event handling */
	void (*on_any_event_v3)(struct afb_api_x3 *apix3, const char *event, struct json_object *object);

	/* settings */
	struct json_object *settings;

	/* verbs */
	struct {
		const struct afb_verb_v3 *statics;
		struct afb_verb_v3 **dynamics;
	} verbs;
	uint16_t dyn_verb_count;

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
	return (struct afb_api_x3*)&apiv3->xapi; /* remove const on purpose */
}

static inline struct afb_api_common *api_v3_to_api_common(const struct afb_api_v3 *apiv3)
{
	return (struct afb_api_common*)&apiv3->comapi; /* remove const on purpose */
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
	return afb_api_common_is_sealed(&apiv3->comapi);
}

static
void
x3_api_vverbose_hookable(
	struct afb_api_x3 *apix3,
	int level,
	const char *file,
	int line,
	const char *function,
	const char *fmt,
	va_list args
) {
	struct afb_api_common *comapi = api_x3_to_api_common(apix3);
	afb_api_common_vverbose_hookable(comapi, level, file, line, function, fmt, args);
}

static
int
x3_api_queue_job_hookable(
	struct afb_api_x3 * apix3,
	void (*callback)(int signum, void *arg),
	void *argument,
	void *group,
	int timeout
) {
	struct afb_api_common *comapi = api_x3_to_api_common(apix3);
	return timeout < 0
		? afb_api_common_post_job_hookable(comapi, -(long)timeout, 0, callback, argument, group)
		: afb_api_common_post_job_hookable(comapi, 0, timeout, callback, argument, group);
}

static
int
x3_api_require_api_hookable(
	struct afb_api_x3 * apix3,
	const char *name,
	int initialized
) {
	struct afb_api_common *comapi = api_x3_to_api_common(apix3);
	return afb_api_common_require_api_hookable(comapi, name, initialized);
}

static
int
x3_api_add_alias_hookable(
	struct afb_api_x3 * apix3,
	const char *apiname,
	const char *aliasname
) {
	struct afb_api_common *comapi = api_x3_to_api_common(apix3);
	return afb_api_common_add_alias_hookable(comapi, apiname, aliasname);
}

static
void
x3_api_seal_hookable(
	struct afb_api_x3 * apix3
) {
	struct afb_api_common *comapi = api_x3_to_api_common(apix3);
	afb_api_common_api_seal_hookable(comapi);
}

static
int
x3_api_class_provide_hookable(
	struct afb_api_x3 * apix3,
	const char *name
) {
	struct afb_api_common *comapi = api_x3_to_api_common(apix3);
	return afb_api_common_class_provide_hookable(comapi, name);
}

static
int
x3_api_class_require_hookable(
	struct afb_api_x3 * apix3,
	const char *name
) {
	struct afb_api_common *comapi = api_x3_to_api_common(apix3);
	return afb_api_common_class_require_hookable(comapi, name);
}

static
struct json_object *
x3_api_settings_hookable(
	struct afb_api_x3 * apix3
) {
	struct afb_api_common *comapi = api_x3_to_api_common(apix3);
	return afb_api_common_settings_hookable(comapi);
}

static
struct sd_event *
x3_api_get_event_loop_hookable(
	struct afb_api_x3 *apix3
) {
	struct sd_event *r;

#if WITH_SYSTEMD
	r = afb_systemd_get_event_loop();
#else
	r = NULL;
#endif
#if WITH_AFB_HOOK
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(apix3);
	if (apiv3->comapi.hookflags & afb_hook_flag_api_get_event_loop)
		r = afb_hook_api_get_event_loop(&apiv3->comapi, r);
#endif
	return r;
}

static
struct sd_bus *
x3_api_get_user_bus_hookable(
	struct afb_api_x3 *apix3
) {
	struct sd_bus *r;

#if WITH_SYSTEMD
	r = afb_systemd_get_user_bus();
#else
	r = NULL;
#endif
#if WITH_AFB_HOOK
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(apix3);
	if (apiv3->comapi.hookflags & afb_hook_flag_api_get_user_bus)
		r = afb_hook_api_get_user_bus(&apiv3->comapi, r);
#endif
	return r;
}

static
struct sd_bus *
x3_api_get_system_bus_hookable(
	struct afb_api_x3 *apix3
) {
	struct sd_bus *r;

#if WITH_SYSTEMD
	r = afb_systemd_get_system_bus();
#else
	r = NULL;
#endif
#if WITH_AFB_HOOK
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(apix3);
	if (apiv3->comapi.hookflags & afb_hook_flag_api_get_system_bus)
		r = afb_hook_api_get_system_bus(&apiv3->comapi, r);
#endif
	return r;
}

static
int
x3_api_rootdir_get_fd_hookable(
	struct afb_api_x3 *apix3
) {
	int r;

#if WITH_OPENAT
	r = afb_common_rootdir_get_fd();
#else
	r = X_ENOTSUP;
#endif
#if WITH_AFB_HOOK
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(apix3);
	if (apiv3->comapi.hookflags & afb_hook_flag_api_rootdir_get_fd)
		r = afb_hook_api_rootdir_get_fd(&apiv3->comapi, r);
#endif
	return r;
}

static
int
x3_api_rootdir_open_locale_hookable(
	struct afb_api_x3 *apix3,
	const char *filename,
	int flags,
	const char *locale
) {
	int r;

#if WITH_OPENAT
	r = afb_common_rootdir_open_locale(filename, flags, locale);
#else
	r = X_ENOTSUP;
#endif
#if WITH_AFB_HOOK
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(apix3);
	if (apiv3->comapi.hookflags & afb_hook_flag_api_rootdir_open_locale)
		r = afb_hook_api_rootdir_open_locale(&apiv3->comapi, filename, flags, locale, r);
#endif
	return r;
}

static struct afb_event_x2 *x3_api_new_event_x2_hookable(
	struct afb_api_x3 *apix3,
	const char *name
) {
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(apix3);
	struct afb_evt *evt;
	int rc;

	rc = afb_api_common_new_event_hookable(&apiv3->comapi, name, &evt);
	return rc < 0 ? NULL : afb_evt_make_x2(evt);
}

static int x3_api_event_broadcast_hookable(
	struct afb_api_x3 *apix3,
	const char *name,
	struct json_object *object
) {
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(apix3);
	struct afb_data *data;
	int rc;

	rc = afb_json_legacy_make_data_json_c(&data, object);
	return rc < 0 ? rc : afb_api_common_event_broadcast_hookable(&apiv3->comapi, name, 1, &data);
}

struct x3callcb {
	struct afb_api_x3 *apix3;
	void (*callback)(void*, struct json_object*, const char*, const char*, struct afb_api_x3*);
	void *closure;
};

static void x3_api_call_cb2(
	void *closure,
	struct json_object *object,
	const char *error,
	const char *info
) {
	struct x3callcb *cc = closure;

	cc->callback(cc->closure, object, error, info, cc->apix3);
}

static void x3_api_call_cb(
	void *closure1,
	void *closure2,
	void *closure3,
	int status,
	unsigned nreplies,
	struct afb_data * const replies[]
) {
	struct x3callcb cc;

	cc.apix3 = closure1;
	cc.callback = closure2;
	cc.closure = closure3;

	afb_json_legacy_do_reply_json_c(&cc, status, nreplies, replies, x3_api_call_cb2);
}

static void x3_api_call_hookable(
		struct afb_api_x3 *apix3,
		const char *api,
		const char *verb,
		struct json_object *args,
		void (*callback)(void*, struct json_object*, const char*, const char*, struct afb_api_x3*),
		void *closure)
{
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(apix3);
	struct afb_data *data;
	int rc;
	void *handler = callback ? x3_api_call_cb : NULL;

	rc = afb_json_legacy_make_data_json_c(&data, args);
	if (rc >= 0) {
#if WITH_AFB_HOOK
		if (apiv3->comapi.hookflags & afb_hook_flag_api_callsync)
			afb_calls_call_hooking(&apiv3->comapi, api, verb, 1, &data, handler, apix3, callback, closure);
		else
#endif
			afb_calls_call(&apiv3->comapi, api, verb, 1, &data, handler, apix3, callback, closure);
	}
	else if (callback)
		callback(closure, NULL, "error", NULL, apix3);
}

static int x3_api_call_sync_hookable(
		struct afb_api_x3 *apix3,
		const char *api,
		const char *verb,
		struct json_object *args,
		struct json_object **object,
		char **error,
		char **info)
{
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(apix3);
	struct afb_data *data;
	struct afb_data *replies[3];
	unsigned nreplies;
	int rc, status;

	rc = afb_json_legacy_make_data_json_c(&data, args);
	if (rc < 0) {
		*object = 0;
		*error = strdup("error");
		*info = 0;
	}
	else {
		nreplies = 3;
#if WITH_AFB_HOOK
		if (apiv3->comapi.hookflags & afb_hook_flag_api_callsync)
		{
			rc = afb_calls_call_sync_hooking(&apiv3->comapi, api, verb, 1, &data, &status, &nreplies, replies);
		}
		else
#endif
		{
			rc = afb_calls_call_sync(&apiv3->comapi, api, verb, 1, &data, &status, &nreplies, replies);
		}
		afb_json_legacy_get_reply_sync(status, nreplies, replies, object, error, info);
		afb_data_array_unref(nreplies, replies);
	}
	return rc;
}

static void x3_api_legacy_call_hookable(
		struct afb_api_x3 *apix3,
		const char *api,
		const char *verb,
		struct json_object *args,
		void (*callback)(void*, int, struct json_object*, struct afb_api_x3*),
		void *closure)
{
	RP_ERROR("Legacy calls are not supported");
	if (callback)
		callback(closure, X_ENOTSUP, NULL, apix3);
}

static int x3_api_legacy_call_sync_hookable(
		struct afb_api_x3 *apix3,
		const char *api,
		const char *verb,
		struct json_object *args,
		struct json_object **result)
{
	RP_ERROR("Legacy calls are not supported");
	if (result)
		*result = NULL;
	return X_ENOTSUP;
}

struct preinit_s
{
	int (*preinit)(void*, struct afb_api_x3 *);
	void *closure;
	struct afb_api_x3 *apix3;
	int result;
};

static
void
safe_preinit_for_new_api(int signum, void *closure)
{
	struct preinit_s *ps = closure;
	if (signum)
		ps->result = X_EINTR;
	else
		ps->result = ps->preinit(ps->closure, ps->apix3);
}

static
int
preinit_for_new_api(
	void *closure,
	struct afb_api_v3 *apiv3
) {
	struct preinit_s *ps = closure;

	ps->result = 0;
	if (ps->preinit) {
		ps->apix3 = api_v3_to_api_x3(apiv3);
		afb_sig_monitor_run(0, safe_preinit_for_new_api, ps);
	}
	return ps->result;
}

static
struct afb_api_x3 *
x3_api_new_api_hookable(
	struct afb_api_x3 *apix3,
	const char *name,
	const char *info,
	int noconcurrency,
	int (*preinit)(void*, struct afb_api_x3 *),
	void *preinit_closure
) {
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(apix3);
	struct afb_api_v3 *newapi;
	int rc;
	struct preinit_s ps;

#if WITH_AFB_HOOK
	if (apiv3->comapi.hookflags & afb_hook_flag_api_new_api)
		afb_hook_api_new_api_before(&apiv3->comapi, name, info, noconcurrency);
#endif

	ps.preinit = preinit;
	ps.closure = preinit_closure;
	rc = afb_api_v3_create(
		&newapi,
		apiv3->comapi.declare_set,
		apiv3->comapi.call_set,
		name, Afb_String_Copy,
		info, Afb_String_Copy,
		noconcurrency,
		preinit_for_new_api, &ps,
		apiv3->comapi.path, Afb_String_Const);

#if WITH_AFB_HOOK
	if (apiv3->comapi.hookflags & afb_hook_flag_api_new_api)
		afb_hook_api_new_api_after(&apiv3->comapi, rc, name);
#endif

	return rc >= 0 ? api_v3_to_api_x3(newapi) : NULL;
}

static
int
x3_api_set_verbs_v2_hookable(
	struct afb_api_x3 *apix3,
	const struct afb_verb_v2 *verbs
) {
	RP_ERROR("Set verbs v2 is not supported");
	return X_ENOTSUP;
}

static
int
x3_api_set_verbs_hookable(
	struct afb_api_x3 *apix3,
	const struct afb_verb_v3 *verbs
) {
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(apix3);
	int r;

	if (is_sealed(apiv3))
		r = X_EPERM;
	else {
		afb_api_v3_set_verbs_v3(apiv3, verbs);
		r = 0;
	}

#if WITH_AFB_HOOK
	if (apiv3->comapi.hookflags & afb_hook_flag_api_api_set_verbs)
		r = afb_hook_api_api_set_verbs_v3(&apiv3->comapi, r, verbs);
#endif
	return r;
}


static
int
x3_api_add_verb_hookable(
	struct afb_api_x3 *apix3,
	const char *verb,
	const char *info,
	void (*callback)(struct afb_req_x2 *req),
	void *vcbdata,
	const struct afb_auth *auth,
	uint32_t session,
	int glob
) {
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(apix3);
	int r;

	if (is_sealed(apiv3))
		r = X_EPERM;
	else
		r = afb_api_v3_add_verb(apiv3, verb, info, callback, vcbdata, auth, (uint16_t)session, glob);

#if WITH_AFB_HOOK
	if (apiv3->comapi.hookflags & afb_hook_flag_api_api_add_verb)
		r = afb_hook_api_api_add_verb(&apiv3->comapi, r, verb, info, glob);
#endif
	return r;
}

static
int
x3_api_del_verb_hookable(
	struct afb_api_x3 *apix3,
	const char *verb,
	void **vcbdata
) {
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(apix3);
	int r;

	if (is_sealed(apiv3))
		r = X_EPERM;
	else
		r = afb_api_v3_del_verb(apiv3, verb, vcbdata);

#if WITH_AFB_HOOK
	if (apiv3->comapi.hookflags & afb_hook_flag_api_api_del_verb)
		r = afb_hook_api_api_del_verb(&apiv3->comapi, r, verb);
#endif
	return r;
}

static
int
x3_api_set_on_event_hookable(
	struct afb_api_x3 *apix3,
	void (*onevent)(struct afb_api_x3 *apix3, const char *event, struct json_object *object)
) {
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(apix3);
	int r = 0;

	apiv3->on_any_event_v3 = onevent;
#if WITH_AFB_HOOK
	if (apiv3->comapi.hookflags & afb_hook_flag_api_api_set_on_event)
		r = afb_hook_api_api_set_on_event(&apiv3->comapi, r);
#endif
	return r;
}

static
int
x3_api_set_on_init_hookable(
	struct afb_api_x3 *apix3,
	int (*oninit)(struct afb_api_x3 *apix3)
) {
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(apix3);
	int r;

	if (apiv3->comapi.state != Api_State_Pre_Init) {
		RP_ERROR("[API %s] Bad call to 'afb_api_x3_on_init', must be in PreInit", apiv3->comapi.name);
		r = X_EINVAL;
	}
	else {
		apiv3->init  = oninit;
		r = 0;
	}
#if WITH_AFB_HOOK
	if (apiv3->comapi.hookflags & afb_hook_flag_api_api_set_on_init)
		r = afb_hook_api_api_set_on_init(&apiv3->comapi, r);
#endif
	return r;
}

static
int
x3_api_event_handler_add_hookable(
	struct afb_api_x3 *apix3,
	const char *pattern,
	void (*callback)(void *, const char*, struct json_object*, struct afb_api_x3*),
	void *closure
) {
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(apix3);
	int r = afb_api_common_event_handler_add(&apiv3->comapi, pattern, callback, closure);
#if WITH_AFB_HOOK
	if (apiv3->comapi.hookflags & afb_hook_flag_api_event_handler_add)
		r = afb_hook_api_event_handler_add(&apiv3->comapi, r, pattern);
#endif
	return r;
}

static
int
x3_api_event_handler_del_hookable(
	struct afb_api_x3 *apix3,
	const char *pattern,
	void **closure
) {
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(apix3);
	int r = afb_api_common_event_handler_del(&apiv3->comapi, pattern, closure);
#if WITH_AFB_HOOK
	if (apiv3->comapi.hookflags & afb_hook_flag_api_event_handler_del)
		r = afb_hook_api_event_handler_del(&apiv3->comapi, r, pattern);
#endif
	return r;
}
static
int
x3_api_delete_api_hookable(
	struct afb_api_x3 *apix3
) {
	struct afb_api_v3 *apiv3 = api_x3_to_api_v3(apix3);
	int r;

	if (is_sealed(apiv3))
		r = X_EPERM;
	else
		r = 0;
#if WITH_AFB_HOOK
	if (apiv3->comapi.hookflags & afb_hook_flag_api_delete_api)
		r = afb_hook_api_delete_api(&apiv3->comapi, r);
#endif
	if (r == 0)
		afb_api_v3_unref(apiv3);
	return r;
}

static const struct afb_api_x3_itf api_x3_itf = {

	.vverbose = (void*)x3_api_vverbose_hookable,

	.get_event_loop = x3_api_get_event_loop_hookable,
	.get_user_bus = x3_api_get_user_bus_hookable,
	.get_system_bus = x3_api_get_system_bus_hookable,
	.rootdir_get_fd = x3_api_rootdir_get_fd_hookable,
	.rootdir_open_locale = x3_api_rootdir_open_locale_hookable,
	.queue_job = x3_api_queue_job_hookable,

	.require_api = x3_api_require_api_hookable,
	.add_alias = x3_api_add_alias_hookable,

	.event_broadcast = x3_api_event_broadcast_hookable,
	.event_make = x3_api_new_event_x2_hookable,

	.legacy_call = x3_api_legacy_call_hookable,
	.legacy_call_sync = x3_api_legacy_call_sync_hookable,

	.api_new_api = x3_api_new_api_hookable,
	.api_set_verbs_v2 = x3_api_set_verbs_v2_hookable,
	.api_add_verb = x3_api_add_verb_hookable,
	.api_del_verb = x3_api_del_verb_hookable,
	.api_set_on_event = x3_api_set_on_event_hookable,
	.api_set_on_init = x3_api_set_on_init_hookable,
	.api_seal = x3_api_seal_hookable,
	.api_set_verbs_v3 = x3_api_set_verbs_hookable,
	.event_handler_add = x3_api_event_handler_add_hookable,
	.event_handler_del = x3_api_event_handler_del_hookable,

	.call = x3_api_call_hookable,
	.call_sync = x3_api_call_sync_hookable,

	.class_provide = x3_api_class_provide_hookable,
	.class_require = x3_api_class_require_hookable,

	.delete_api = x3_api_delete_api_hookable,
	.settings = x3_api_settings_hookable,
};

/******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
                                  H A N D L I N G   O F   E V E N T S
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/

/* handler of any events */
static
void
handle_any_event_cb(
	void *closure1,
	struct json_object *object,
	const void *closure2
) {
	struct afb_api_v3 *apiv3 = closure1;
	const char *name = closure2;

	apiv3->on_any_event_v3(api_v3_to_api_x3(apiv3), name, object);
}

/* handler of specific events */

struct handle_specific_event_data
{
	struct afb_api_v3 *apiv3;
	void *closure;
	void (*callback)(void *, const char*, struct json_object*, struct afb_api_x3*);
};

static
void
handle_specific_event_cb(
	void *closure1,
	struct json_object *object,
	const void *closure2
) {
	struct handle_specific_event_data *hd = closure1;
	const char *name = closure2;

	hd->callback(hd->closure, name, object, api_v3_to_api_x3(hd->apiv3));
}

/* handler of events */
static
void
handle_events(
	void *callback,
	void *closure,
	const struct afb_evt_data *event,
	struct afb_api_common *comapi
) {
	struct afb_api_v3 *apiv3 = api_common_to_afb_api_v3(comapi);

	if (callback != NULL) {
		struct handle_specific_event_data hd;

		hd.apiv3 = apiv3;
		hd.closure = closure;
		hd.callback = callback;

		afb_json_legacy_do2_single_json_c(event->nparams, event->params, handle_specific_event_cb, &hd, event->name);
	}
	else if (apiv3->on_any_event_v3 != NULL) {
		afb_json_legacy_do2_single_json_c(event->nparams, event->params, handle_any_event_cb, apiv3, event->name);
	}
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
		&apiv3->comapi,
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
	afb_api_common_update_hook(&apiv3->comapi);
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

static
void
destroy_api_v3(
	struct afb_api_v3 *apiv3
) {
	afb_api_common_cleanup(&apiv3->comapi);
	while (apiv3->dyn_verb_count)
		free(apiv3->verbs.dynamics[--apiv3->dyn_verb_count]);
	free(apiv3->verbs.dynamics);
	free(apiv3);
}

static void api_unref_cb(void *closure)
{
	struct afb_api_v3 *apiv3 = closure;
	if (apiv3 && afb_api_common_decref(&apiv3->comapi))
		destroy_api_v3(apiv3);
}

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
		? fnmatch(verb->verb, name, FNM_NOESCAPE|FNM_PATHNAME|FNM_PERIOD|NAME_FOLD_FNM)
		: namecmp(verb->verb, name);
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

	afb_req_common_reply_verb_unknown_error_hookable(req);
}

#if WITHOUT_JSON_C
struct json_object *
afb_api_v3_make_description_openAPIv3(
	struct afb_api_v3 *api
) {
	return NULL;
}
#else
static
struct json_object *
describe_verb_v3(
	const struct afb_verb_v3 *verb,
	struct json_tokener *tok
) {
	struct json_object *f, *a, *g, *d;

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
	if (!verb->info)
		d = json_object_new_string(verb->verb);
	else {
		json_tokener_reset(tok);
		d = json_tokener_parse_ex(tok, verb->info, -1);
		if (json_tokener_get_error(tok) != json_tokener_success) {
			json_object_put(d);
			d = json_object_new_string(verb->info);
		}
	}
	json_object_object_add(g, "description", d);

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
	struct json_tokener *tok;
	struct json_object_iterator jit, jend;

	tok = json_tokener_new();
	r = json_object_new_object();
	json_object_object_add(r, "openapi", json_object_new_string("3.0.0"));

	i = json_object_new_object();
	json_object_object_add(r, "info", i);
	json_object_object_add(i, "version", json_object_new_string("0.0.0"));
	if (api->comapi.info) {
		json_tokener_reset(tok);
		p = json_tokener_parse_ex(tok, api->comapi.info, -1);
		if (json_tokener_get_error(tok) != json_tokener_success) {
			json_object_put(p);
			p = json_object_new_string(api->comapi.info);
			json_object_object_add(i, "description", p);
		}
		else if (!json_object_is_type(p, json_type_object)) {
			json_object_object_add(i, "description", p);
		}
		else {
			jit = json_object_iter_begin(p);
			jend = json_object_iter_end(p);
			while (!json_object_iter_equal(&jit, &jend)) {
				json_object_object_add(i,
					json_object_iter_peek_name(&jit),
					json_object_get(json_object_iter_peek_value(&jit)));
				json_object_iter_next(&jit);
			}
			json_object_put(p);
		}
	}
	json_object_object_add(i, "title", json_object_new_string(api->comapi.name));

	buffer[0] = '/';
	buffer[sizeof buffer - 1] = 0;

	p = json_object_new_object();
	json_object_object_add(r, "paths", p);
	iter = api->verbs.dynamics;
	end = iter + api->dyn_verb_count;
	while (iter != end) {
		verb = *iter++;
		strncpy(buffer + 1, verb->verb, sizeof buffer - 2);
		json_object_object_add(p, buffer, describe_verb_v3(verb, tok));
	}
	verb = api->verbs.statics;
	if (verb)
		while(verb->verb) {
			strncpy(buffer + 1, verb->verb, sizeof buffer - 2);
			json_object_object_add(p, buffer, describe_verb_v3(verb, tok));
			verb++;
		}
	json_tokener_free(tok);
	return r;
}
#endif

struct afb_api_v3 *
afb_api_v3_addref(
	struct afb_api_v3 *apiv3
) {
	if (apiv3)
		afb_api_common_incref(&apiv3->comapi);
	return apiv3;
}

void
afb_api_v3_unref(
	struct afb_api_v3 *apiv3
) {
	if (apiv3) {
		if (apiv3->comapi.refcount == 1 && apiv3->comapi.name != NULL)
			afb_apiset_del(apiv3->comapi.declare_set, apiv3->comapi.name);
		else if (afb_api_common_decref(&apiv3->comapi))
			destroy_api_v3(apiv3);
	}
}

struct afb_api_common *
afb_api_v3_get_api_common(
	struct afb_api_v3 *apiv3
) {
	return &apiv3->comapi;
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
	afb_api_common_api_seal(&apiv3->comapi);
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
		if (glob == v->glob && !namecmp(verb, v->verb)) {
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
		if (!namecmp(verb, v->verb)) {
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
		rc =  afb_api_common_class_provide(&apiv3->comapi, desc->provide_class);
	if (!rc && desc->require_class)
		rc =  afb_api_common_class_require(&apiv3->comapi, desc->require_class);
	if (!rc && desc->require_api)
		rc =  afb_api_common_require_api(&apiv3->comapi, desc->require_api, 0);
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
	int rc, decl;
	struct afb_api_v3 *apiv3;
	size_t strsz;
	char *ptr, *p;
	struct afb_api_item afb_api;
	strsz = 0;

	/* check the name */
	if (name == NULL)
		mode_name = Afb_String_Const;
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
		RP_ERROR("out of memory");
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

	/* makes a name for root anonymous api */
	if (name != NULL)
		decl = 1;
	else {
		decl = 0;
		if (path == NULL)
			name = "<ROOT>";
		else {
			name = strrchr(path, '/');
			name = name == NULL ? path : &name[1];
		}
	}

	/* init comapi */
	afb_api_common_init(
		&apiv3->comapi,
		declare_set, call_set,
		name, mode_name == Afb_String_Free,
		info, mode_info == Afb_String_Free,
		path, mode_path == Afb_String_Free,
		noconcurrency ? apiv3 : NULL
	);
	apiv3->comapi.onevent = handle_events;

	/* init xapi */
	apiv3->xapi.apiname = apiv3->comapi.name;
	apiv3->xapi.itf = &api_x3_itf;
	afb_api_v3_logmask_set(apiv3, rp_logmask);

	/* declare the api */
	if (decl) {
		afb_api.closure = apiv3;
		afb_api.itf = &export_api_itf;
		afb_api.group = apiv3->comapi.group;
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
	if (decl) {
		afb_api_v3_addref(apiv3); /* avoid side-effect freeing the api */
		afb_apiset_del(apiv3->comapi.declare_set, apiv3->comapi.name);
	}
error2:
	afb_api_common_cleanup(&apiv3->comapi);
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

#endif
