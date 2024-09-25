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

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#if !WITHOUT_JSON_C
#include <json-c/json.h>
#include <rp-utils/rp-jsonc.h>
#endif
#include <rp-utils/rp-verbose.h>

#include "core/afb-v4-itf.h"

#include "containerof.h"

#include "core/afb-apiname.h"
#include "core/afb-apiset.h"
#include "core/afb-api-common.h"
#include "core/afb-api-v4.h"
#include "core/afb-auth.h"
#include "core/afb-common.h"
#include "core/afb-data.h"
#include "core/afb-evt.h"
#include "core/afb-global.h"
#include "core/afb-hook.h"
#include "core/afb-session.h"
#include "core/afb-req-common.h"
#include "core/afb-req-v4.h"
#include "core/afb-sched.h"
#include "core/afb-type.h"
#include "core/afb-calls.h"
#include "core/afb-string-mode.h"

#if WITH_SYSTEMD
#include "sys/systemd.h"
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

typedef uint16_t verb_count_t;

/*
 * structure of the exported API
 */
struct afb_api_v4
{
	/* the common api */
	struct afb_api_common comapi;

	/* control function */
	afb_api_callback_x4_t mainctl;

	/* userdata */
	void *userdata;

	/* verbs */
	struct {
		const struct afb_verb_v4 *statics;
		struct afb_verb_v4 **dynamics;
	} verbs;

	verb_count_t sta_verb_count;
	verb_count_t dyn_verb_count;

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

struct afb_api_v4 *
afb_api_v4_addref(
	struct afb_api_v4 *apiv4
) {
	if (apiv4)
		afb_api_common_incref(&apiv4->comapi);
	return apiv4;
}

static
void
destroy_api_v4(
	struct afb_api_v4 *apiv4
) {
	afb_api_common_cleanup(&apiv4->comapi);
	while (apiv4->dyn_verb_count)
		free(apiv4->verbs.dynamics[--apiv4->dyn_verb_count]);
	free(apiv4->verbs.dynamics);
	free(apiv4);
}


void
afb_api_v4_unref(
	struct afb_api_v4 *apiv4
) {
	if (apiv4) {
		if (apiv4->comapi.refcount == 1 && apiv4->comapi.name != NULL)
			afb_apiset_del(apiv4->comapi.declare_set, apiv4->comapi.name);
		else if (afb_api_common_decref(&apiv4->comapi))
			destroy_api_v4(apiv4);
	}
}

int
afb_api_v4_logmask(
	struct afb_api_v4 *apiv4
) {
	return apiv4->logmask;
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

void
afb_api_v4_set_mainctl(
	struct afb_api_v4 *apiv4,
	afb_api_callback_x4_t mainctl
) {
	apiv4->mainctl = mainctl;
}

static int verb_sort_cb(const void *pa, const void *pb)
{
	const struct afb_verb_v4 * const *va = pa, * const *vb = pb;
	return namecmp((*va)->verb, (*vb)->verb);
}

static int match_glob_pattern(const struct afb_verb_v4 *verb, const char *name)
{
	return fnmatch(verb->verb, name, FNM_NOESCAPE|FNM_PATHNAME|FNM_PERIOD|NAME_FOLD_FNM);
}

static struct afb_verb_v4 *search_dynamic_verb(struct afb_api_v4 *api, const char *name)
{
	struct afb_verb_v4 **base, *verb;
	unsigned low, up, mid;
	int cmp;

	base = api->verbs.dynamics;
	if (api->comapi.dirty) {
		qsort(base, api->dyn_verb_count, sizeof *base, verb_sort_cb);
		api->comapi.dirty = 0;
	}
	low = 0;
	up = api->dyn_verb_count;
	while(low < up) {
		mid = (low + up) >> 1;
		verb = base[mid];
		if (verb->glob && match_glob_pattern(verb, name) == 0)
			return verb;
		cmp = namecmp(verb->verb, name);
		if (cmp == 0)
			return verb;
		if (cmp < 0)
			low = mid + 1;
		else
			up = mid;
	}
	return 0;
}

const struct afb_verb_v4 *
afb_api_v4_verb_matching(
	struct afb_api_v4 *apiv4,
	const char *name
) {
	const struct afb_verb_v4 *verb;

	/* look first in dynamic set */
	verb = search_dynamic_verb(apiv4, name);
	if (!verb) {
		/* look then in static set */
		verb = apiv4->verbs.statics;
		while (verb) {
			if (!verb->verb)
				verb = 0;
			else if ((verb->glob ? match_glob_pattern(verb, name) : namecmp(verb->verb, name)) == 0)
				break;
			else
				verb++;
		}
	}
	return verb;
}

unsigned
afb_api_v4_verb_count(
	struct afb_api_v4 *apiv4
) {
	return (unsigned)apiv4->sta_verb_count + (unsigned)apiv4->dyn_verb_count;
}

const struct afb_verb_v4 *
afb_api_v4_verb_at(
	struct afb_api_v4 *apiv4,
	unsigned index
) {
	if (apiv4->dyn_verb_count > index)
		return apiv4->verbs.dynamics[index];
	index -= apiv4->dyn_verb_count;
	if (apiv4->sta_verb_count > index)
		return &apiv4->verbs.statics[index];
	return 0;
}

int
afb_api_v4_set_verbs(
	struct afb_api_v4 *apiv4,
	const struct afb_verb_v4 *verbs
) {
	int r;
	verb_count_t cnt;

	if (is_sealed(apiv4))
		r = X_EPERM;
	else {
		r = 0;
		cnt = 0;
		if (verbs != NULL) {
			while (verbs[cnt].verb)
				if (++cnt <= 0) {
					r = X_EOVERFLOW;
					break;
				}
		}
		if (r == 0) {
			apiv4->verbs.statics = verbs;
			apiv4->sta_verb_count = cnt;
		}
	}
	return r;
}


int
afb_api_v4_add_verb(
	struct afb_api_v4 *apiv4,
	const char *verb,
	const char *info,
	void (*callback)(struct afb_req_v4 *req, unsigned nparams, struct afb_data * const params[]),
	void *vcbdata,
	const struct afb_auth *auth,
	uint32_t session,
	int glob
) {
	struct afb_verb_v4 *verbrec, **vv;
	char *txt;
	verb_count_t i;

	/* check not sealed */
	if (is_sealed(apiv4))
		return X_EPERM;

	/* check the verb is not already existing */
	for (i = 0 ; i < apiv4->dyn_verb_count ; i++) {
		verbrec = apiv4->verbs.dynamics[i];
		if (!namecmp(verb, verbrec->verb)) {
			/* refuse to redefine a dynamic verb */
			return X_EEXIST;
		}
	}

	/* check no count overflow */
	if ((verb_count_t)(apiv4->dyn_verb_count + 1) <= 0)
		return X_EOVERFLOW;

	/* allocates room on need for the new verb */
	if ((apiv4->dyn_verb_count & (apiv4->dyn_verb_count - 1)) == 0) {
		size_t size = apiv4->dyn_verb_count;
		size = size < 8 ? 8 : size << 1; /* !! min size must be a power of 2 */
		size *= sizeof *vv;
		vv = realloc(apiv4->verbs.dynamics, size);
		if (!vv)
			return X_ENOMEM;
		apiv4->verbs.dynamics = vv;
	}

	/* allocate the verb record */
	verbrec = malloc(sizeof *verbrec + (1 + strlen(verb)) + (info ? 1 + strlen(info) : 0));
	if (!verbrec)
		return X_ENOMEM;

	/* initialize the record */
	verbrec->callback = callback;
	verbrec->vcbdata = vcbdata;
	verbrec->auth = auth;
	verbrec->session = (uint16_t)session;
	verbrec->glob = !!glob;
	txt = (char*)(verbrec + 1);
	verbrec->verb = txt;
	txt = stpcpy(txt, verb);
	if (!info)
		verbrec->info = NULL;
	else {
		verbrec->info = ++txt;
		strcpy(txt, info);
	}

	/* record the verb */
	apiv4->verbs.dynamics[apiv4->dyn_verb_count] = verbrec;
	apiv4->dyn_verb_count++;
	apiv4->comapi.dirty = 1;
	return 0;
}

int
afb_api_v4_del_verb(
	struct afb_api_v4 *apiv4,
	const char *verb,
	void **vcbdata
) {
	struct afb_verb_v4 *v;
	verb_count_t i;

	if (is_sealed(apiv4))
		return X_EPERM;

	for (i = 0 ; i < apiv4->dyn_verb_count ; i++) {
		v = apiv4->verbs.dynamics[i];
		if (!namecmp(verb, v->verb)) {
			apiv4->verbs.dynamics[i] = apiv4->verbs.dynamics[--apiv4->dyn_verb_count];
			if (vcbdata)
				*vcbdata = v->vcbdata;
			free(v);
			apiv4->comapi.dirty = 1;
			return 0;
		}
	}

	return X_ENOENT;
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

static
void
api_service_exit_cb(
	void *closure,
	int code
) {
	struct afb_api_v4 *apiv4 = closure;
	union afb_ctlarg arg = { .exiting = { .code = code }};
	if (apiv4->mainctl)
		apiv4->mainctl(apiv4, afb_ctlid_Exiting, &arg, apiv4->userdata);
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
{
	struct afb_api_v4 *apiv4 = closure;
	if (apiv4 && afb_api_common_decref(&apiv4->comapi))
		destroy_api_v4(apiv4);
}




static struct afb_api_itf export_api_itf =
{
	.process = api_process_cb,
	.service_start = api_service_start_cb,
	.service_exit = api_service_exit_cb,
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

int
afb_api_v4_event_handler_add(
	struct afb_api_v4 *api,
	const char *pattern,
	void (*callback)(void *, const char*, unsigned, struct afb_data * const[], struct afb_api_v4*),
	void *closure
) {
	return afb_api_common_event_handler_add(&api->comapi, pattern, callback, closure);
}

int
afb_api_v4_event_handler_del(
	struct afb_api_v4 *api,
	const char *pattern,
	void **closure
) {
	return afb_api_common_event_handler_del(&api->comapi, pattern, closure);
}

void
afb_api_v4_process_call(
	struct afb_api_v4 *api,
	struct afb_req_common *req
) {
	const struct afb_verb_v4 *verb;

	verb = afb_api_v4_verb_matching(api, req->verbname);
	if (verb)
		/* verb found */
		afb_req_v4_process(req, api, verb);
	else
		/* error no verb found */
		afb_req_common_reply_verb_unknown_error_hookable(req);
}

#if WITHOUT_JSON_C
struct json_object *
afb_api_v4_make_description_openAPIv3(
	struct afb_api_v4 *api
) {
	return NULL;
}
#else
static
struct json_object *
describe_verb_v4(
	const struct afb_verb_v4 *verb,
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
afb_api_v4_make_description_openAPIv3(
	struct afb_api_v4 *api
) {
	char buffer[256];
	struct afb_verb_v4 **iter, **end;
	const struct afb_verb_v4 *verb;
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
		json_object_object_add(p, buffer, describe_verb_v4(verb, tok));
	}
	verb = api->verbs.statics;
	if (verb)
		while(verb->verb) {
			strncpy(buffer + 1, verb->verb, sizeof buffer - 2);
			json_object_object_add(p, buffer, describe_verb_v4(verb, tok));
			verb++;
		}
	json_tokener_free(tok);
	return r;
}
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

int
afb_api_v4_set_binding_fields(
	struct afb_api_v4 *apiv4,
	const struct afb_binding_v4 *desc,
	afb_api_callback_x4_t mainctl
) {
	int rc;

	afb_api_v4_set_userdata(apiv4, desc->userdata);
	afb_api_v4_set_mainctl(apiv4, mainctl);
	afb_api_v4_set_verbs(apiv4, desc->verbs);

	rc = 0;
	if (desc->provide_class)
		rc =  afb_api_v4_class_provide(apiv4, desc->provide_class);
	if (!rc && desc->require_class)
		rc =  afb_api_v4_class_require(apiv4, desc->require_class);
	if (!rc && desc->require_api)
		rc =  afb_api_v4_require_api(apiv4, desc->require_api, 0);
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
	int rc, decl;
	struct afb_api_v4 *apiv4;
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
	apiv4 = calloc(1, strsz + sizeof *apiv4);
	if (!apiv4) {
		RP_ERROR("out of memory");
		rc = X_ENOMEM;
		goto error;
	}

	/* init the structure */
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
		&apiv4->comapi,
		declare_set, call_set,
		name, mode_name == Afb_String_Free,
		info, mode_info == Afb_String_Free,
		path, mode_path == Afb_String_Free,
		noconcurrency ? apiv4 : NULL
	);
	apiv4->comapi.onevent = handle_events;

	/* init xapi */
#if WITH_AFB_HOOK
	afb_api_v4_update_hooks(apiv4);
#endif
	afb_api_v4_logmask_set(apiv4, rp_logmask);

	/* declare the api */
	if (decl) {
		afb_api.closure = apiv4;
		afb_api.itf = &export_api_itf;
		afb_api.group = apiv4->comapi.group;
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
	if (decl) {
		afb_api_v4_addref(apiv4); /* avoid side-effect freeing the api */
		afb_apiset_del(apiv4->comapi.declare_set, apiv4->comapi.name);
	}
error2:
	afb_api_common_cleanup(&apiv4->comapi);
	free(apiv4);

error:
	*api = NULL;
	return rc;
}


/**********************************************
* direct flow
**********************************************/

struct afb_api_common *
afb_api_v4_get_api_common(
	struct afb_api_v4 *apiv4
) {
	return &apiv4->comapi;
}

int
afb_api_v4_class_provide(
	struct afb_api_v4 *apiv4,
	const char *name
) {
	return afb_api_common_class_provide(&apiv4->comapi, name);
}

int
afb_api_v4_require_api(
	struct afb_api_v4 *apiv4,
	const char *name,
	int initialized
) {
	return afb_api_common_require_api(&apiv4->comapi, name, initialized);
}

int
afb_api_v4_class_require(
	struct afb_api_v4 *apiv4,
	const char *name
) {
	return afb_api_common_class_require(&apiv4->comapi, name);
}

int
afb_api_v4_add_alias(
	struct afb_api_v4 *apiv4,
	const char *apiname,
	const char *aliasname
) {
	return afb_api_common_add_alias(&apiv4->comapi, apiname, aliasname);
}

void
afb_api_v4_seal(
	struct afb_api_v4 *apiv4
) {
	afb_api_common_api_seal(&apiv4->comapi);
}

struct json_object *
afb_api_v4_settings(
	struct afb_api_v4 *apiv4
) {
	return afb_api_common_settings(&apiv4->comapi);
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

void
afb_api_v4_vverbose(
	struct afb_api_v4 *apiv4,
	int level,
	const char *file,
	int line,
	const char *function,
	const char *fmt,
	va_list args
) {
	afb_api_common_vverbose(&apiv4->comapi, level, file, line, function, fmt, args);
}

void
afb_api_v4_verbose(
	struct afb_api_v4 *apiv4,
	int level,
	const char *file,
	int line,
	const char * func,
	const char *fmt,
	...
) {
	va_list args;
	va_start(args, fmt);
	afb_api_v4_vverbose(apiv4, level, file, line, func, fmt, args);
	va_end(args);
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

void
afb_api_v4_verbose_hookable(
	struct afb_api_v4 *apiv4,
	int level,
	const char *file,
	int line,
	const char * func,
	const char *fmt,
	...
) {
	va_list args;
	va_start(args, fmt);
	afb_api_v4_vverbose_hookable(apiv4, level, file, line, func, fmt, args);
	va_end(args);
}


int
afb_api_v4_post_job_hookable(
	struct afb_api_v4 *apiv4,
	long delayms,
	int timeout,
	void (*callback)(int signum, void *arg),
	void *argument,
	void *group
) {
	const struct afb_api_common *comapi = apiv4 ? &apiv4->comapi : afb_global_api();
	return afb_api_common_post_job_hookable(comapi, delayms, timeout, callback, argument, group);
}

int
afb_api_v4_abort_job_hookable(
	struct afb_api_v4 *apiv4,
	int jobid
) {
	const struct afb_api_common *comapi = apiv4 ? &apiv4->comapi : afb_global_api();
	return afb_api_common_abort_job_hookable(comapi, jobid);
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

int
afb_api_v4_unshare_session_hookable(
	struct afb_api_v4 *apiv4
) {
	return afb_api_common_unshare_session_hookable(&apiv4->comapi);
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
	void *handler = callback ? call_x4_cb : NULL;
#if WITH_AFB_HOOK
	if (apiv4->comapi.hookflags & afb_hook_flag_api_call)
		return afb_calls_call_hooking(&apiv4->comapi,
				apiname, verbname, nparams, params,
				handler, apiv4, callback, closure);
#endif
	return afb_calls_call(&apiv4->comapi,
				apiname, verbname, nparams, params,
				handler, apiv4, callback, closure);
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
	((union afb_ctlarg*)scp->ctlarg)->pre_init.config = afb_api_v4_settings(apiv4);
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
	ctlarg.pre_init.uid = NULL;
	ctlarg.pre_init.config = NULL;
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
	int r = afb_api_v4_set_verbs(apiv4, verbs);
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
	void (*callback)(struct afb_req_v4 *req, unsigned nparams, struct afb_data * const params[]),
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
	int r = afb_api_v4_event_handler_add(apiv4, pattern, callback, closure);

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
	int r = afb_api_v4_event_handler_del(apiv4, pattern, closure);

#if WITH_AFB_HOOK
	if (apiv4->comapi.hookflags & afb_hook_flag_api_event_handler_del)
		r = afb_hook_api_event_handler_del(&apiv4->comapi, r, pattern);
#endif

	return r;

}

