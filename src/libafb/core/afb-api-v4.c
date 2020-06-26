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
#include <afb/afb-binding-v4.h>

#include "containerof.h"

#include "core/afb-apiname.h"
#include "core/afb-apiset.h"
#include "core/afb-api-common.h"
#include "core/afb-api-v4.h"
#include "core/afb-auth.h"
#include "core/afb-common.h"
#include "core/afb-data.h"
#include "core/afb-evt.h"
#include "core/afb-hook.h"
#include "core/afb-session.h"
#include "core/afb-req-common.h"
#include "core/afb-req-v4.h"
#include "core/afb-sched.h"
#include "core/afb-type.h"
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
#include "utils/namecmp.h"
#include "sys/x-realpath.h"
#include "sys/x-errno.h"

/*************************************************************************
 * internal types
 ************************************************************************/

/*
 * structure of the exported API
 */
struct afb_api_v4
{
	/* the common api */
	struct afb_api_common comapi;

	/* settings */
	struct json_object *settings;

	/* control function */
	int (*mainctl)(afb_api_x4_t apix4, afb_ctlid_t id, afb_ctlarg_t arg);

	/* verbs */
	struct {
		const struct afb_verb_v4 *statics;
		struct afb_verb_v4 **dynamics;
	} verbs;
	uint16_t dyn_verb_count;

	/* interface with remainers */
	struct afb_api_x4 xapi;

	/* userdata */
	void *userdata;

	/* strings */
	char strings[];
};

/*****************************************************************************/

static inline struct afb_api_v4 *api_common_to_afb_api_v4(const struct afb_api_common *comapi)
{
	return containerof(struct afb_api_v4, comapi, comapi);
}

static inline struct afb_api_v4 *api_x4_to_api_v4(afb_api_x4_t apix4)
{
	return containerof(struct afb_api_v4, xapi, apix4);
}

static inline afb_api_x4_t api_v4_to_api_x4(const struct afb_api_v4 *apiv4)
{
	return (afb_api_x4_t)&apiv4->xapi;
}

static inline struct afb_api_common *api_v4_to_api_common(const struct afb_api_v4 *apiv4)
{
	return (struct afb_api_common*)&apiv4->comapi; /* remove const on pupose */
}

static inline struct afb_api_common *api_x4_to_api_common(const afb_api_x4_t apix4)
{
	return api_v4_to_api_common(api_x4_to_api_v4(apix4));
}

static inline afb_api_x4_t api_common_to_api_x4(const struct afb_api_common *comapi)
{
	return api_v4_to_api_x4(api_common_to_afb_api_v4(comapi));
}

/*****************************************************************************/

static inline int is_sealed(const struct afb_api_v4 *apiv4)
{
	return afb_api_common_is_sealed(api_v4_to_api_common(apiv4));
}

/*****************************************************************************/

struct safe_ctlproc_s
{
	struct afb_api_v4 *apiv4;
	int (*ctlproc)(afb_api_x4_t, afb_ctlid_t, afb_ctlarg_t);
	afb_ctlid_t ctlid;
	afb_ctlarg_t ctlarg;
	int result;
};

static void safe_ctlproc_call_cb(int sig, void *closure)
{
	struct safe_ctlproc_s *scp = closure;

	scp->result = sig ? X_EFAULT
		: scp->ctlproc(api_v4_to_api_x4(scp->apiv4), scp->ctlid, scp->ctlarg);
}

static int safe_ctlproc_call(struct afb_api_v4 *apiv4, struct safe_ctlproc_s *scp)
{
	if (!scp->ctlproc)
		return 0;

	scp->apiv4 = apiv4;
	afb_sig_monitor_run(60, safe_ctlproc_call_cb, scp);
	return scp->result;
}

int
afb_api_v4_safe_ctlproc_x4(
	struct afb_api_v4 *apiv4,
	int (*ctlproc)(afb_api_x4_t, afb_ctlid_t, afb_ctlarg_t),
	afb_ctlid_t ctlid,
	afb_ctlarg_t ctlarg
) {
	struct safe_ctlproc_s scp;

	scp.ctlproc = ctlproc;
	scp.ctlid = ctlid;
	scp.ctlarg = ctlarg;
	return safe_ctlproc_call(apiv4, &scp);
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

#define CLOSURE_T                        afb_api_x4_t
#define CLOSURE_TO_COMMON_API(closure)   api_x4_to_api_common(closure)
#include "afb-api-common.inc"
#undef CLOSURE_TO_COMMON_API
#undef CLOSURE_T

/******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
                                           V 4    S P E C I F I C
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/

/**********************************************
* normal flow
**********************************************/

static
const char *
x4_api_name(
	afb_api_x4_t apix4
) {
	struct afb_api_v4 *apiv4 = api_x4_to_api_v4(apix4);
	return apiv4->comapi.name;
}

static
void *
x4_api_get_userdata(
	afb_api_x4_t apix4
) {
	struct afb_api_v4 *apiv4 = api_x4_to_api_v4(apix4);
	return apiv4->userdata;
}

static
void *
x4_api_set_userdata(
	afb_api_x4_t apix4,
	void *value
) {
	struct afb_api_v4 *apiv4 = api_x4_to_api_v4(apix4);
	void *previous = apiv4->userdata;
	apiv4->userdata = value;
	return previous;
}

static
int
x4_api_new_data_set(
	afb_api_x4_t api,
	afb_data_x4_t *data,
	afb_type_x4_t type,
	const void *pointer,
	size_t size,
	void (*dispose)(void*),
	void *closure
) {
	return afb_data_x4_create_set_x4(data, type, pointer, size, dispose, closure);
}

static
int
x4_api_new_data_copy(
	afb_api_x4_t api,
	afb_data_x4_t *data,
	afb_type_x4_t type,
	const void *pointer,
	size_t size
) {
	return afb_data_x4_create_copy_x4(data, type, pointer, size);
}

static
int
x4_api_new_data_alloc(
	afb_api_x4_t api,
	afb_data_x4_t *data,
	afb_type_x4_t type,
	void **pointer,
	size_t size,
	int zeroes
) {
	return afb_data_x4_create_alloc_x4(data, type, pointer, size, zeroes);
}

static
int
x4_api_event_broadcast(
	afb_api_x4_t apix4,
	const char *name,
	unsigned nparams,
	const struct afb_data_x4 **params
) {
	struct afb_api_common *comapi = api_x4_to_api_common(apix4);
	return afb_api_common_event_broadcast_x4(comapi, name, nparams, params);
}

static
int
x4_api_new_event(
	afb_api_x4_t apix4,
	const char *name,
	afb_event_x4_t *event)
{
	struct afb_api_common *comapi = api_x4_to_api_common(apix4);
	struct afb_evt *evt;
	int rc;

	rc = afb_api_common_new_event(comapi, name, &evt);
	*event = rc < 0 ? NULL : afb_evt_make_x4(evt);
	return rc;
}

static
int
preinit_new_api(
	struct afb_api_v4 *apiv4,
	void *closure
) {
	struct safe_ctlproc_s *scp = closure;
	apiv4->mainctl = scp->ctlproc;
	scp->ctlid = afb_ctlid_Pre_Init;
	return safe_ctlproc_call(apiv4, scp);
}

static
int
x4_api_new_api(
	afb_api_x4_t apix4,
	afb_api_x4_t *newapix4,
	const char *name,
	const char *info,
	int noconcurrency,
	int (*mainctl)(afb_api_x4_t, afb_ctlid_t, afb_ctlarg_t),
	void *closure
) {
	struct afb_api_v4 *apiv4 = api_x4_to_api_v4(apix4);
	struct afb_api_v4 *newapi;
	union afb_ctlarg ctlarg;
	struct safe_ctlproc_s scp;
	int rc;

	ctlarg.pre_init.closure = closure;
	scp.ctlproc = mainctl;
	scp.ctlarg = &ctlarg;
	rc = afb_api_v4_create(
		&newapi,
		apiv4->comapi.declare_set,
		apiv4->comapi.call_set,
		name, Afb_String_Copy,
		info, Afb_String_Copy,
		noconcurrency,
		mainctl ? preinit_new_api : NULL, &scp,
		apiv4->comapi.path, Afb_String_Const);

	*newapix4 = rc >= 0 ? api_v4_to_api_x4(newapi) : NULL;
	return rc;
}

static void call_x4_cb(
	void *closure1,
	void *closure2,
	void *closure3,
	int status,
	unsigned nreplies,
	const struct afb_data_x4 * const *replies
) {
	afb_api_x4_t apix4 = closure1;
	void (*callback)(void*, int, unsigned, const struct afb_data_x4 * const *, afb_api_x4_t) = closure2;
	callback(closure3, status, nreplies, replies, apix4);
}

static void x4_api_call(
	afb_api_x4_t apix4,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	const struct afb_data_x4 * const*params,
	void (*callback)(
		void *closure,
		int status,
		unsigned nreplies,
		const struct afb_data_x4 * const*replies,
		afb_api_x4_t api),
	void *closure)
{
	struct afb_api_v4 *apiv4 = api_x4_to_api_v4(apix4);
	return afb_calls_call_x4(api_v4_to_api_common(apiv4),
				apiname, verbname, nparams, params,
				call_x4_cb, (void*)apix4, callback, closure);
}

static int x4_api_call_sync(
	afb_api_x4_t apix4,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	const struct afb_data_x4 * const*params,
	int *status,
	unsigned *nreplies,
	const struct afb_data_x4 **replies
) {
	return afb_calls_call_sync_x4(api_x4_to_api_common(apix4),
					apiname, verbname, nparams, params,
					status, nreplies, replies);
}

static
int
x4_api_register_type(
	afb_api_x4_t api,
	const struct afb_type_x4 *type
) {
	return afb_type_register_type_x4(type);
}

static
int
x4_api_set_verbs(
	afb_api_x4_t apix4,
	const struct afb_verb_v4 *verbs
) {
	struct afb_api_v4 *apiv4 = api_x4_to_api_v4(apix4);

	if (is_sealed(apiv4))
		return X_EPERM;

	afb_api_v4_set_verbs_v4(apiv4, verbs);
	return 0;
}

static
int
x4_api_add_verb(
	afb_api_x4_t apix4,
	const char *verb,
	const char *info,
	void (*callback)(afb_req_x4_t req, unsigned nparams, const struct afb_data_x4 * const *params),
	void *vcbdata,
	const struct afb_auth *auth,
	uint32_t session,
	int glob
) {
	struct afb_api_v4 *apiv4 = api_x4_to_api_v4(apix4);

	if (is_sealed(apiv4))
		return X_EPERM;

	return afb_api_v4_add_verb(apiv4, verb, info, callback, vcbdata, auth, (uint16_t)session, glob);
}

static
int
x4_api_del_verb(
	afb_api_x4_t apix4,
	const char *verb,
	void **vcbdata
) {
	struct afb_api_v4 *apiv4 = api_x4_to_api_v4(apix4);

	if (is_sealed(apiv4))
		return X_EPERM;

	return afb_api_v4_del_verb(apiv4, verb, vcbdata);
}

static
int
x4_api_event_handler_add(
	afb_api_x4_t apix4,
	const char *pattern,
	void (*callback)(void*,const char*,unsigned,const struct afb_data_x4* const*,afb_api_x4_t),
	void *closure
) {
	return afb_api_common_event_handler_add(api_x4_to_api_common(apix4), pattern, callback, closure);
}

static
int
x4_api_event_handler_del(
	afb_api_x4_t apix4,
	const char *pattern,
	void **closure
) {
	return afb_api_common_event_handler_del(api_x4_to_api_common(apix4), pattern, closure);
}

static
int
x4_api_delete_api(
	afb_api_x4_t apix4
) {
	struct afb_api_v4 *apiv4 = api_x4_to_api_v4(apix4);

	if (is_sealed(apiv4))
		return X_EPERM;

	afb_api_v4_unref(apiv4);
	return 0;
}

static const struct afb_api_x4_itf api_x4_itf =
{
	.name = x4_api_name,
	.get_userdata = x4_api_get_userdata,
	.set_userdata = x4_api_set_userdata,

	.vverbose = common_api_vverbose,

	.settings = common_api_settings,

	.require_api = common_api_require_api,
	.class_provide = common_api_class_provide,
	.class_require = common_api_class_require,

	.queue_job = common_api_queue_job,

	.new_data_set = x4_api_new_data_set,
	.new_data_copy = x4_api_new_data_copy,
	.new_data_alloc = x4_api_new_data_alloc,

	.event_broadcast = x4_api_event_broadcast,
	.new_event = x4_api_new_event,
	.event_handler_add = x4_api_event_handler_add,
	.event_handler_del = x4_api_event_handler_del,

	.call = x4_api_call,
	.call_sync = x4_api_call_sync,

	.register_type = x4_api_register_type,
	.new_api = x4_api_new_api,
	.add_alias = common_api_add_alias,
	.delete_api = x4_api_delete_api,
	.set_verbs = x4_api_set_verbs,
	.add_verb = x4_api_add_verb,
	.del_verb = x4_api_del_verb,
	.seal = common_api_seal,
};

/**********************************************
* hooked flow
**********************************************/
#if WITH_AFB_HOOK
static int x4_api_hooked_new_api(
	afb_api_x4_t apix4,
	afb_api_x4_t *newapix4,
	const char *apiname,
	const char *info,
	int noconcurrency,
	int (*mainctl)(afb_api_x4_t, afb_ctlid_t, afb_ctlarg_t),
	void *closure)
{
	int result;
	struct afb_api_v4 *apiv4 = api_x4_to_api_v4(apix4);
	struct afb_api_common *comapi = api_v4_to_api_common(apiv4);
	afb_hook_api_new_api_before(comapi, apiname, info, noconcurrency);
	result = x4_api_new_api(apix4, newapix4, apiname, info, noconcurrency, mainctl, closure);
	afb_hook_api_new_api_after(comapi, result ? 0 : X_ENOMEM, apiname);
	return result;
}

static void x4_api_hooked_call(
	afb_api_x4_t apix4,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	const struct afb_data_x4 * const*params,
	void (*callback)(
		void *closure,
		int status,
		unsigned nparams,
		const struct afb_data_x4 * const*params,
		afb_api_x4_t api),
	void *closure)
{
	struct afb_api_v4 *apiv4 = api_x4_to_api_v4(apix4);
	return afb_calls_call_hookable_x4(api_v4_to_api_common(apiv4),
				apiname, verbname, nparams, params,
				call_x4_cb, (void*)apix4, callback, closure);
}

static int x4_api_hooked_call_sync(
	afb_api_x4_t apix4,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	const struct afb_data_x4 * const*params,
	int *status,
	unsigned *nreplies,
	const struct afb_data_x4 **replies
) {
	return afb_calls_call_sync_hookable_x4(api_x4_to_api_common(apix4),
					apiname, verbname, nparams, params,
					status, nreplies, replies);
}

static int x4_api_hooked_set_verbs(
	afb_api_x4_t apix4,
	const struct afb_verb_v4 *verbs)
{
	int result = x4_api_set_verbs(apix4, verbs);
	return afb_hook_api_api_set_verbs_v4(api_x4_to_api_common(apix4), result, verbs);
}

static int x4_api_hooked_add_verb(
	afb_api_x4_t apix4,
	const char *verb,
	const char *info,
	void (*callback)(afb_req_x4_t req, unsigned nparams, const struct afb_data_x4 * const *params),
	void *vcbdata,
	const struct afb_auth *auth,
	uint32_t session,
	int glob)
{
	struct afb_api_v4 *apiv4 = api_x4_to_api_v4(apix4);
	int result = x4_api_add_verb(apix4, verb, info, callback, vcbdata, auth, session, glob);
	return afb_hook_api_api_add_verb(api_v4_to_api_common(apiv4), result, verb, info, glob);
}

static int x4_api_hooked_del_verb(
	afb_api_x4_t apix4,
	const char *verb,
	void **vcbdata)
{
	struct afb_api_v4 *apiv4 = api_x4_to_api_v4(apix4);
	int result = x4_api_del_verb(apix4, verb, vcbdata);
	return afb_hook_api_api_del_verb(api_v4_to_api_common(apiv4), result, verb);
}

static int x4_api_hooked_event_handler_add(
	afb_api_x4_t apix4,
	const char *pattern,
	void (*callback)(void*,const char*,unsigned,const struct afb_data_x4* const*,afb_api_x4_t),
	void *closure)
{
	struct afb_api_v4 *apiv4 = api_x4_to_api_v4(apix4);
	int result = x4_api_event_handler_add(apix4, pattern, callback, closure);
	return afb_hook_api_event_handler_add(api_v4_to_api_common(apiv4), result, pattern);
}

static int x4_api_hooked_event_handler_del(
	afb_api_x4_t apix4,
	const char *pattern,
	void **closure)
{
	struct afb_api_v4 *apiv4 = api_x4_to_api_v4(apix4);
	int result = x4_api_event_handler_del(apix4, pattern, closure);
	return afb_hook_api_event_handler_del(api_v4_to_api_common(apiv4), result, pattern);
}

static int x4_api_hooked_delete_api(afb_api_x4_t api)
{
	struct afb_api_v4 *apiv4 = afb_api_v4_addref(api_x4_to_api_v4(api));
	int result = x4_api_delete_api(api);
	result = afb_hook_api_delete_api(api_v4_to_api_common(apiv4), result);
	afb_api_v4_unref(apiv4);
	return result;
}

static const struct afb_api_x4_itf hooked_api_x4_itf = {

	.name = x4_api_name, /* FIXME hooking */
	.get_userdata = x4_api_get_userdata, /* FIXME hooking */
	.set_userdata = x4_api_set_userdata, /* FIXME hooking */

	.vverbose = common_api_hooked_vverbose,

	.settings = common_api_hooked_settings,

	.require_api = common_api_hooked_require_api,
	.class_provide = common_api_hooked_class_provide,
	.class_require = common_api_hooked_class_require,

	.queue_job = common_api_hooked_queue_job,

	.register_type = x4_api_register_type, /* FIXME hooking */
	.new_data_set = x4_api_new_data_set, /* FIXME hooking */
	.new_data_copy = x4_api_new_data_copy, /* FIXME hooking */
	.new_data_alloc = x4_api_new_data_alloc, /* FIXME hooking */

	.event_broadcast = x4_api_event_broadcast, /* FIXME hooking */
	.new_event = x4_api_new_event, /* FIXME hooking */
	.event_handler_add = x4_api_hooked_event_handler_add,
	.event_handler_del = x4_api_hooked_event_handler_del,

	.call = x4_api_hooked_call,
	.call_sync = x4_api_hooked_call_sync,

	.new_api = x4_api_hooked_new_api,
	.add_alias = common_api_hooked_add_alias,
	.delete_api = x4_api_hooked_delete_api,
	.set_verbs = x4_api_hooked_set_verbs,
	.add_verb = x4_api_hooked_add_verb,
	.del_verb = x4_api_hooked_del_verb,
	.seal = common_api_hooked_seal,
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
	const struct afb_evt_data *event,
	struct afb_api_common *comapi
) {
	void (*cb)(void *, const char*, unsigned, const struct afb_data_x4 * const*, afb_api_x4_t) = callback;
	struct afb_api_v4 *apiv4 = api_common_to_afb_api_v4(comapi);
	afb_api_x4_t apix4 = api_v4_to_api_x4(apiv4);

	if (cb != NULL) {
		cb(closure, event->name, event->nparams, event->params, apix4);
	}
	else if (apiv4->mainctl != NULL) {
		union afb_ctlarg arg;
		arg.orphan_event.name = event->name;
		apiv4->mainctl(apix4, afb_ctlid_Orphan_Event, &arg);
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
start_cb(
	void *closure
) {
	struct afb_api_v4 *apiv4 = closure;
	if (apiv4->mainctl)
		return apiv4->mainctl(api_v4_to_api_x4(apiv4), afb_ctlid_Init, NULL);
	return 0;
}

static
int
api_service_start_cb(
	void *closure
) {
	struct afb_api_v4 *apiv4 = closure;

/* FIXME change api_common */
	return afb_api_common_start(
		api_v4_to_api_common(apiv4),
		start_cb,
		apiv4);
}

static void api_process_cb(void *closure, struct afb_req_common *req)
	__attribute__((alias("afb_api_v4_process_call")));

int afb_api_v4_logmask_get(const struct afb_api_v4 *apiv4)
{
	return apiv4->xapi.logmask;
}

void afb_api_v4_logmask_set(struct afb_api_v4 *apiv4, int mask)
{
	apiv4->xapi.logmask = mask;
}

#if WITH_AFB_HOOK
void
afb_api_v4_update_hooks(
	struct afb_api_v4 *apiv4
) {
	apiv4->xapi.itf = afb_api_common_update_hook(api_v4_to_api_common(apiv4)) ? &hooked_api_x4_itf : &api_x4_itf;
}

static void api_update_hooks_cb(void *closure)
	__attribute__((alias("afb_api_v4_update_hooks")));
#endif

static int api_get_logmask_cb(void *closure)
	__attribute__((alias("afb_api_v4_logmask_get")));

static void api_set_logmask_cb(void *closure, int level)
	__attribute__((alias("afb_api_v4_logmask_set")));

static void api_describe_cb(void *closure, void (*describecb)(void *, struct json_object *), void *clocb)
{
	struct afb_api_v4 *apiv4 = closure;
	describecb(clocb, afb_api_v4_make_description_openAPIv3(apiv4));
}

static void api_unref_cb(void *closure)
	__attribute__((alias("afb_api_v4_unref")));

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

static int verb_name_compare(const struct afb_verb_v4 *verb, const char *name)
{
	return verb->glob
		? fnmatch(verb->verb, name, FNM_NOESCAPE|FNM_PATHNAME|FNM_PERIOD|NAME_FOLD_FNM)
		: namecmp(verb->verb, name);
}

static struct afb_verb_v4 *search_dynamic_verb(struct afb_api_v4 *api, const char *name)
{
	struct afb_verb_v4 **v, **e, *i;

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
afb_api_v4_process_call(
	struct afb_api_v4 *api,
	struct afb_req_common *req
) {
	const struct afb_verb_v4 *verbsv4;
	const char *name;

	name = req->verbname;

	/* look first in dynamic set */
	verbsv4 = search_dynamic_verb(api, name);
	if (!verbsv4) {
		/* look then in static set */
		verbsv4 = api->verbs.statics;
		while (verbsv4) {
			if (!verbsv4->verb)
				verbsv4 = 0;
			else if (!verb_name_compare(verbsv4, name))
				break;
			else
				verbsv4++;
		}
	}
	/* is it a v3 verb ? */
	if (verbsv4) {
		/* yes */
		afb_req_v4_process(req, api, verbsv4);
		return;
	}

	afb_req_common_reply_verb_unknown(req);
}

static
struct json_object *
describe_verb_v4(
	const struct afb_verb_v4 *verb
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
afb_api_v4_make_description_openAPIv3(
	struct afb_api_v4 *api
) {
	char buffer[256];
	struct afb_verb_v4 **iter, **end;
	const struct afb_verb_v4 *verb;
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
		json_object_object_add(p, buffer, describe_verb_v4(verb));
	}
	verb = api->verbs.statics;
	if (verb)
		while(verb->verb) {
			strncpy(buffer + 1, verb->verb, sizeof buffer - 2);
			json_object_object_add(p, buffer, describe_verb_v4(verb));
			verb++;
		}
	return r;
}


struct afb_api_v4 *
afb_api_v4_addref(
	struct afb_api_v4 *apiv4
) {
	if (apiv4)
		afb_api_common_incref(api_v4_to_api_common(apiv4));
	return apiv4;
}

void
afb_api_v4_unref(
	struct afb_api_v4 *apiv4
) {
	if (apiv4 && afb_api_common_decref(api_v4_to_api_common(apiv4))) {
		if (apiv4->comapi.name != NULL)
			afb_apiset_del(apiv4->comapi.declare_set, apiv4->comapi.name);
		afb_api_common_cleanup(api_v4_to_api_common(apiv4));
		while (apiv4->dyn_verb_count)
			free(apiv4->verbs.dynamics[--apiv4->dyn_verb_count]);
		free(apiv4->verbs.dynamics);
		free(apiv4);
	}
}

struct afb_api_common *
afb_api_v4_get_api_common(
	struct afb_api_v4 *apiv4
) {
	return api_v4_to_api_common(apiv4);
}

afb_api_x4_t
afb_api_v4_get_api_x4(
	struct afb_api_v4 *apiv4
) {
	return api_v4_to_api_x4(apiv4);
}

void
afb_api_v4_seal(
	struct afb_api_v4 *apiv4
) {
	afb_api_common_api_seal(api_v4_to_api_common(apiv4));
}

void
afb_api_v4_set_verbs_v4(
	struct afb_api_v4 *api,
	const struct afb_verb_v4 *verbs
) {
	api->verbs.statics = verbs;
}

int
afb_api_v4_add_verb(
	struct afb_api_v4 *api,
	const char *verb,
	const char *info,
	void (*callback)(afb_req_x4_t req, unsigned nparams, const struct afb_data_x4 * const *params),
	void *vcbdata,
	const struct afb_auth *auth,
	uint16_t session,
	int glob
) {
	struct afb_verb_v4 *v, **vv;
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
afb_api_v4_del_verb(
	struct afb_api_v4 *api,
	const char *verb,
	void **vcbdata
) {
	struct afb_verb_v4 *v;
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
afb_api_v4_set_binding_fields(
	struct afb_api_v4 *apiv4,
	const struct afb_binding_v4 *desc
) {
	int rc;
	struct afb_api_common *comapi;

	apiv4->userdata = desc->userdata;
	apiv4->verbs.statics = desc->verbs;
	apiv4->mainctl = desc->mainctl;

	rc = 0;
	comapi = api_v4_to_api_common(apiv4);
	if (desc->provide_class)
		rc =  afb_api_common_class_provide(comapi, desc->provide_class);
	if (!rc && desc->require_class)
		rc =  afb_api_common_class_require(comapi, desc->require_class);
	if (!rc && desc->require_api)
		rc =  afb_api_common_require_api(comapi, desc->require_api, 0);
	return rc;
}

int
afb_api_v4_create(
	struct afb_api_v4 **api,
	struct afb_apiset *declare_set,
	struct afb_apiset *call_set,
	const char *name,
	enum afb_string_mode mode_name,
	const char *info,
	enum afb_string_mode mode_info,
	int noconcurrency,
	int (*preinit)(struct afb_api_v4*, void*),
	void *closure,
	const char* path,
	enum afb_string_mode mode_path
) {
	int rc;
	struct afb_api_v4 *apiv4;
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
	apiv4 = malloc(strsz + sizeof *apiv4);
	if (!apiv4) {
		ERROR("out of memory");
		rc = X_ENOMEM;
		goto error;
	}

	/* init the structure */
	memset(apiv4, 0, sizeof *apiv4);
	ptr = apiv4->strings;
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
		api_v4_to_api_common(apiv4),
		declare_set, call_set,
		name, mode_name == Afb_String_Free,
		info, mode_info == Afb_String_Free,
		path, mode_path == Afb_String_Free
	);
	apiv4->comapi.onevent = handle_events;

	/* init xapi */
#if WITH_AFB_HOOK
	afb_api_v4_update_hooks(apiv4);
#else
	apiv4->xapi.itf = &api_x4_itf;
#endif
	afb_api_v4_logmask_set(apiv4, logmask);

	/* declare the api */
	if (name != NULL) {
		afb_api.closure = afb_api_v4_addref(apiv4);
		afb_api.itf = &export_api_itf;
		afb_api.group = noconcurrency ? apiv4 : NULL;
		rc = afb_apiset_add(apiv4->comapi.declare_set, apiv4->comapi.name, afb_api);
		if (rc < 0) {
			goto error2;
		}
	}

	/* pre-init of the api */
	if (preinit) {
		rc = preinit(apiv4, closure);
		if (rc < 0)
			goto error3;
	}

	*api = apiv4;
	return 0;

error3:
	if (name != NULL) {
		afb_apiset_del(apiv4->comapi.declare_set, apiv4->comapi.name);
	}
error2:
	afb_api_common_cleanup(api_v4_to_api_common(apiv4));
	free(apiv4);

error:
	*api = NULL;
	return rc;
}

