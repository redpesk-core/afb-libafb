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

#include "core/afb-v4.h"

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
	afb_api_callback_x4_t mainctl;

	/* userdata */
	void *userdata;

	/* verbs */
	struct {
		const struct afb_verb_v4 *statics;
		struct afb_verb_v4 **dynamics;
	} verbs;

	uint16_t dyn_verb_count;

	/** mask of loging */
	int16_t logmask;

	/* strings */
	char strings[];
};

/*****************************************************************************/

static inline struct afb_api_v4 *api_common_to_afb_api_v4(const struct afb_api_common *comapi)
{
	return containerof(struct afb_api_v4, comapi, comapi);
}

/*****************************************************************************/

static inline int is_sealed(struct afb_api_v4 *apiv4)
{
	return apiv4->comapi.sealed;
}

/*****************************************************************************/
#if !defined(APIV4_SAFE_CTLPROC_TIME)
# define APIV4_SAFE_CTLPROC_TIME 60
#endif

/**
 * structure used to safely call control proc (mostly mainctl).
 */
struct safe_ctlproc_s
{
	/** api of the call */
	struct afb_api_v4 *apiv4;

	/** the identification of the control */
	afb_ctlid_t ctlid;

	/** the argument of the control */
	afb_ctlarg_t ctlarg;

	/** the userdata */
	void *userdata;

	/** the control proc */
	afb_api_callback_x4_t ctlproc;

	/** the result of the call */
	int result;
};

/**
 * The secured callback (@see afb_sig_monitor_run)
 *
 * @param sig      0 on normal flow or the signal number if interrupted
 * @param closure  a pointer to a safe_ctlproc_s structure
 */
static void safe_ctlproc_call_cb(int sig, void *closure)
{
	struct safe_ctlproc_s *scp = closure;

	scp->result = sig ? X_EFAULT
		: scp->ctlproc(scp->apiv4, scp->ctlid, scp->ctlarg, scp->userdata);
}

/**
 * Wrapper for calling afb_sig_monitor_run and returning the result
 *
 * @param scp  description of the safe call to perform
 *
 * @result the result of the call
 */
static int safe_ctlproc_call(struct safe_ctlproc_s *scp)
{
	afb_sig_monitor_run(APIV4_SAFE_CTLPROC_TIME, safe_ctlproc_call_cb, scp);
	return scp->result;
}

/* Call safely the ctlproc with the given parameters */
int
afb_api_v4_safe_ctlproc(
	struct afb_api_v4 *apiv4,
	afb_api_callback_x4_t ctlproc,
	afb_ctlid_t ctlid,
	afb_ctlarg_t ctlarg
) {
	struct safe_ctlproc_s scp;

	if (!ctlproc)
		return 0;

	scp.apiv4 = apiv4;
	scp.ctlid = ctlid;
	scp.ctlarg = ctlarg;
	scp.userdata = apiv4->userdata;
	scp.ctlproc = ctlproc;
	return safe_ctlproc_call(&scp);
}

/**********************************************
* direct flow
**********************************************/

int
afb_api_v4_logmask(
	struct afb_api_v4 *apiv4
) {
	return apiv4->logmask;
}

const char *
afb_api_v4_name(
	struct afb_api_v4 *apiv4
) {
	return apiv4->comapi.name;
}

const char *
afb_api_v4_info(
	struct afb_api_v4 *apiv4
) {
	return apiv4->comapi.info;
}

const char *
afb_api_v4_path(
	struct afb_api_v4 *apiv4
) {
	return apiv4->comapi.path;
}

void *
afb_api_v4_get_userdata(
	struct afb_api_v4 *apiv4
) {
	return apiv4->userdata;
}

void *
afb_api_v4_set_userdata(
	struct afb_api_v4 *apiv4,
	void *value
) {
	void *previous = apiv4->userdata;
	apiv4->userdata = value;
	return previous;
}

/**********************************************
* hookable flow
**********************************************/

void
afb_api_v4_vverbose_hookable(
	struct afb_api_v4 *apiv4,
	int level,
	const char *file,
	int line,
	const char *function,
	const char *fmt,
	va_list args
) {
	afb_api_common_vverbose_hookable(&apiv4->comapi, level, file, line, function, fmt, args);
}

int
afb_api_v4_queue_job_hookable(
	struct afb_api_v4 *apiv4,
	void (*callback)(int signum, void *arg),
	void *argument,
	void *group,
	int timeout
) {
	return afb_api_common_queue_job_hookable(&apiv4->comapi, callback, argument, group, timeout);
}

int
afb_api_v4_require_api_hookable(
	struct afb_api_v4 *apiv4,
	const char *name,
	int initialized
) {
	return afb_api_common_require_api_hookable(&apiv4->comapi, name, initialized);
}

int
afb_api_v4_add_alias_hookable(
	struct afb_api_v4 *apiv4,
	const char *apiname,
	const char *aliasname
) {
	return afb_api_common_add_alias_hookable(&apiv4->comapi, apiname, aliasname);
}

void
afb_api_v4_seal_hookable(
	struct afb_api_v4 *apiv4
) {
	afb_api_common_api_seal_hookable(&apiv4->comapi);
}

int
afb_api_v4_class_provide_hookable(
	struct afb_api_v4 *apiv4,
	const char *name
) {
	return afb_api_common_class_provide_hookable(&apiv4->comapi, name);
}


int
afb_api_v4_class_require_hookable(
	struct afb_api_v4 *apiv4,
	const char *name
) {
	return afb_api_common_class_require_hookable(&apiv4->comapi, name);
}

struct json_object *
afb_api_v4_settings_hookable(
	struct afb_api_v4 *apiv4
) {
	return afb_api_common_settings_hookable(&apiv4->comapi);
}

int
afb_api_v4_event_broadcast_hookable(
	struct afb_api_v4 *apiv4,
	const char *name,
	unsigned nparams,
	struct afb_data * const params[]
) {
	return afb_api_common_event_broadcast_hookable(&apiv4->comapi, name, nparams, params);
}

int
afb_api_v4_new_event_hookable(
	struct afb_api_v4 *apiv4,
	const char *name,
	struct afb_evt **event)
{
	struct afb_evt *evt;
	int rc;

	rc = afb_api_common_new_event_hookable(&apiv4->comapi, name, &evt);
	*event = rc < 0 ? NULL : evt;
	return rc;
}

static
void
call_x4_cb(
	void *closure1,
	void *closure2,
	void *closure3,
	int status,
	unsigned nreplies,
	struct afb_data * const replies[]
) {
	struct afb_api_v4 *apiv4 = closure1;
	void (*callback)(void*, int, unsigned, struct afb_data * const[], struct afb_api_v4*) = closure2;
	callback(closure3, status, nreplies, replies, apiv4);
}

void
afb_api_v4_call_hookable(
	struct afb_api_v4 *apiv4,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	struct afb_data * const params[],
	void (*callback)(
		void *closure,
		int status,
		unsigned nreplies,
		struct afb_data * const replies[],
		struct afb_api_v4 *api),
	void *closure)
{
#if WITH_AFB_HOOK
	if (apiv4->comapi.hookflags & afb_hook_flag_api_call)
		return afb_calls_call_hooking(&apiv4->comapi,
				apiname, verbname, nparams, params,
				call_x4_cb, apiv4, callback, closure);
#endif
	return afb_calls_call(&apiv4->comapi,
				apiname, verbname, nparams, params,
				call_x4_cb, apiv4, callback, closure);
}

int
afb_api_v4_call_sync_hookable(
	struct afb_api_v4 *apiv4,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	struct afb_data * const params[],
	int *status,
	unsigned *nreplies,
	struct afb_data *replies[]
) {
#if WITH_AFB_HOOK
	if (apiv4->comapi.hookflags & afb_hook_flag_api_callsync)
		return afb_calls_call_sync_hooking(&apiv4->comapi,
					apiname, verbname, nparams, params,
					status, nreplies, replies);
#endif
	return afb_calls_call_sync(&apiv4->comapi,
					apiname, verbname, nparams, params,
					status, nreplies, replies);
}

/**
 * Callback for calling preinitialization function of a new api
 * The new api is received in parameter.
 *
 * @param apiv4    the new api
 * @param closure  a pointer to the safe_ctlproc_s structure to use
 *
 * @return a negative value on error or else a positive or null value
 */
static
int
preinit_new_api(
	struct afb_api_v4 *apiv4,
	void *closure
) {
	struct safe_ctlproc_s *scp = closure;

	/* set the mainctl of the fresh api to the one to call */
	apiv4->userdata = scp->userdata;
	apiv4->mainctl = scp->ctlproc;
	if (!scp->ctlproc)
		return 0;
	scp->apiv4 = apiv4;
	return safe_ctlproc_call(scp);
}

int
afb_api_v4_new_api_hookable(
	struct afb_api_v4 *apiv4,
	struct afb_api_v4 **newapiv4,
	const char *apiname,
	const char *info,
	int noconcurrency,
	afb_api_callback_x4_t mainctl,
	void *userdata
) {
	union afb_ctlarg ctlarg;
	struct safe_ctlproc_s scp;
	int r;

#if WITH_AFB_HOOK
	if (apiv4->comapi.hookflags & afb_hook_flag_api_new_api)
		afb_hook_api_new_api_before(&apiv4->comapi, apiname, info, noconcurrency);
#endif

	ctlarg.pre_init.path = apiv4->comapi.path;
	scp.ctlid = afb_ctlid_Pre_Init;
	scp.ctlarg = &ctlarg;
	scp.userdata = userdata;
	scp.ctlproc = mainctl;
	r = afb_api_v4_create(
		newapiv4,
		apiv4->comapi.declare_set,
		apiv4->comapi.call_set,
		apiname, Afb_String_Copy,
		info, Afb_String_Copy,
		noconcurrency,
		preinit_new_api, &scp,
		apiv4->comapi.path, Afb_String_Const);

#if WITH_AFB_HOOK
	if (apiv4->comapi.hookflags & afb_hook_flag_api_new_api)
		r = afb_hook_api_new_api_after(&apiv4->comapi, r, apiname);
#endif

	return r;
}


int
afb_api_v4_set_verbs_hookable(
	struct afb_api_v4 *apiv4,
	const struct afb_verb_v4 *verbs
) {
	int r;

	if (is_sealed(apiv4))
		r = X_EPERM;
	else {
		apiv4->verbs.statics = verbs;
		r = 0;
	}
#if WITH_AFB_HOOK
	if (apiv4->comapi.hookflags & afb_hook_flag_api_api_set_verbs)
		r = afb_hook_api_api_set_verbs_v4(&apiv4->comapi, r, verbs);
#endif
	return r;
}

int
afb_api_v4_add_verb_hookable(
	struct afb_api_v4 *apiv4,
	const char *verb,
	const char *info,
	void (*callback)(const struct afb_req_v4 *req, unsigned nparams, const struct afb_data * const params[]),
	void *vcbdata,
	const struct afb_auth *auth,
	uint32_t session,
	int glob
) {
	int r = afb_api_v4_add_verb(apiv4, verb, info, callback, vcbdata, auth, session, glob);
#if WITH_AFB_HOOK
	if (apiv4->comapi.hookflags & afb_hook_flag_api_api_add_verb)
		r = afb_hook_api_api_add_verb(&apiv4->comapi, r, verb, info, glob);
#endif
	return r;
}

int
afb_api_v4_del_verb_hookable(
	struct afb_api_v4 *apiv4,
	const char *verb,
	void **vcbdata
) {
	int r = afb_api_v4_del_verb(apiv4, verb, vcbdata);
#if WITH_AFB_HOOK
	if (apiv4->comapi.hookflags & afb_hook_flag_api_api_del_verb)
		r = afb_hook_api_api_del_verb(&apiv4->comapi, r, verb);
#endif
	return r;
}

int
afb_api_v4_delete_api_hookable(
	struct afb_api_v4 *apiv4
) {
	int r;

	r = is_sealed(apiv4) ? X_EPERM : 0;

#if WITH_AFB_HOOK
	if (apiv4->comapi.hookflags & afb_hook_flag_api_delete_api)
		r = afb_hook_api_delete_api(&apiv4->comapi, r);
#endif
	if (r == 0)
		afb_api_v4_unref(apiv4);

	return r;
}

int
afb_api_v4_event_handler_add_hookable(
	struct afb_api_v4 *apiv4,
	const char *pattern,
	void (*callback)(void*,const char*,unsigned,struct afb_data * const[],struct afb_api_v4*),
	void *closure
) {
	int r = afb_api_common_event_handler_add(&apiv4->comapi, pattern, callback, closure);

#if WITH_AFB_HOOK
	if (apiv4->comapi.hookflags & afb_hook_flag_api_event_handler_add)
		r = afb_hook_api_event_handler_add(&apiv4->comapi, r, pattern);
#endif

	return r;
}

int
afb_api_v4_event_handler_del_hookable(
	struct afb_api_v4 *apiv4,
	const char *pattern,
	void **closure
) {
	int r = afb_api_common_event_handler_del(&apiv4->comapi, pattern, closure);

#if WITH_AFB_HOOK
	if (apiv4->comapi.hookflags & afb_hook_flag_api_event_handler_del)
		r = afb_hook_api_event_handler_del(&apiv4->comapi, r, pattern);
#endif

	return r;

}


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
	void (*cb)(void *, const char*, unsigned, struct afb_data * const[], struct afb_api_v4*) = callback;
	struct afb_api_v4 *apiv4 = api_common_to_afb_api_v4(comapi);

	if (cb != NULL) {
		cb(closure, event->name, event->nparams, event->params, apiv4);
	}
	else if (apiv4->mainctl != NULL) {
		union afb_ctlarg arg;
		arg.orphan_event.name = event->name;
		apiv4->mainctl(apiv4, afb_ctlid_Orphan_Event, &arg, apiv4->userdata);
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
		return apiv4->mainctl(apiv4, afb_ctlid_Init, NULL, apiv4->userdata);
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
		&apiv4->comapi,
		start_cb,
		apiv4);
}

static void api_process_cb(void *closure, struct afb_req_common *req)
	__attribute__((alias("afb_api_v4_process_call")));

int afb_api_v4_logmask_get(struct afb_api_v4 *apiv4)
{
	return apiv4->logmask;
}

void afb_api_v4_logmask_set(struct afb_api_v4 *apiv4, int mask)
{
	apiv4->logmask = (int16_t)mask;
}

#if WITH_AFB_HOOK
void
afb_api_v4_update_hooks(
	struct afb_api_v4 *apiv4
) {
	afb_api_common_update_hook(&apiv4->comapi);
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

	afb_req_common_reply_verb_unknown_error_hookable(req);
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
		afb_api_common_incref(&apiv4->comapi);
	return apiv4;
}

void
afb_api_v4_unref(
	struct afb_api_v4 *apiv4
) {
	if (apiv4 && afb_api_common_decref(&apiv4->comapi)) {
		if (apiv4->comapi.name != NULL)
			afb_apiset_del(apiv4->comapi.declare_set, apiv4->comapi.name);
		afb_api_common_cleanup(&apiv4->comapi);
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
	return &apiv4->comapi;
}

void
afb_api_v4_seal(
	struct afb_api_v4 *apiv4
) {
	afb_api_common_api_seal(&apiv4->comapi);
}

int
afb_api_v4_add_verb(
	struct afb_api_v4 *apiv4,
	const char *verb,
	const char *info,
	void (*callback)(const struct afb_req_v4 *req, unsigned nparams, const struct afb_data * const params[]),
	void *vcbdata,
	const struct afb_auth *auth,
	uint32_t session,
	int glob
) {
	struct afb_verb_v4 *v, **vv;
	char *txt;
	int i;

	if (is_sealed(apiv4))
		return X_EPERM;

	for (i = 0 ; i < apiv4->dyn_verb_count ; i++) {
		v = apiv4->verbs.dynamics[i];
		if (glob == v->glob && !namecmp(verb, v->verb)) {
			/* refuse to redefine a dynamic verb */
			return X_EEXIST;
		}
	}

	vv = realloc(apiv4->verbs.dynamics, (1 + apiv4->dyn_verb_count) * sizeof *vv);
	if (!vv)
		return X_ENOMEM;
	apiv4->verbs.dynamics = vv;

	v = malloc(sizeof *v + (1 + strlen(verb)) + (info ? 1 + strlen(info) : 0));
	if (!v)
		return X_ENOMEM;

	v->callback = callback;
	v->vcbdata = vcbdata;
	v->auth = auth;
	v->session = (uint16_t)session;
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

	apiv4->verbs.dynamics[apiv4->dyn_verb_count++] = v;
	return 0;
}

int
afb_api_v4_del_verb(
	struct afb_api_v4 *apiv4,
	const char *verb,
	void **vcbdata
) {
	struct afb_verb_v4 *v;
	int i;

	if (is_sealed(apiv4))
		return X_EPERM;

	for (i = 0 ; i < apiv4->dyn_verb_count ; i++) {
		v = apiv4->verbs.dynamics[i];
		if (!namecmp(verb, v->verb)) {
			apiv4->verbs.dynamics[i] = apiv4->verbs.dynamics[--apiv4->dyn_verb_count];
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
	comapi = &apiv4->comapi;
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
		&apiv4->comapi,
		declare_set, call_set,
		name, mode_name == Afb_String_Free,
		info, mode_info == Afb_String_Free,
		path, mode_path == Afb_String_Free
	);
	apiv4->comapi.onevent = handle_events;

	/* init xapi */
#if WITH_AFB_HOOK
	afb_api_v4_update_hooks(apiv4);
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
	afb_api_common_cleanup(&apiv4->comapi);
	free(apiv4);

error:
	*api = NULL;
	return rc;
}

