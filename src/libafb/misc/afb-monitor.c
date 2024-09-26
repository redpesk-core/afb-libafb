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

#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#if !WITHOUT_JSON_C
#include <json-c/json.h>
#include <rp-utils/rp-jsonc.h>
#endif
#include <rp-utils/rp-verbose.h>

#define AFB_BINDING_VERSION 0
#include <afb/afb-binding.h>

#include "core/afb-apiset.h"
#include "core/afb-api-common.h"
#include "core/afb-evt.h"
#include "core/afb-req-common.h"
#if WITH_AFB_TRACE
#include "misc/afb-trace.h"
#endif
#include "core/afb-session.h"
#include "core/afb-json-legacy.h"
#include "core/afb-data.h"
#include "core/afb-type-predefined.h"
#include "sys/x-errno.h"


#define Verbosity_Level_Error	0
#define Verbosity_Level_Warning	1
#define Verbosity_Level_Notice	2
#define Verbosity_Level_Info	3
#define Verbosity_Level_Debug	4
#define _DEVERBOSITY_(vlvl)      ((vlvl) + rp_Log_Level_Error)
#define _VERBOSITY_(llvl)        ((llvl) - rp_Log_Level_Error)
#define verbosity_to_mask(verbo) ((1 << (_DEVERBOSITY_(verbo) + 1)) - 1)
#define verbosity_from_mask(mask) ((mask) <= _RP_LOGMASK_(rp_Log_Level_Error) ? Verbosity_Level_Error \
                                   : (mask) <= _RP_LOGMASK_(rp_Log_Level_Warning) ? Verbosity_Level_Warning \
                                   : (mask) <= _RP_LOGMASK_(rp_Log_Level_Notice) ? Verbosity_Level_Notice \
                                   : (mask) <= _RP_LOGMASK_(rp_Log_Level_Info) ? Verbosity_Level_Info \
                                   : Verbosity_Level_Debug)
#define verbosity_set(verbo) rp_set_logmask(verbosity_to_mask(verbo))
#define verbosity_get()      verbosity_from_mask(rp_logmask)


static const char _disconnected_[] = "disconnected";
static const char _get_[] = "get";
static const char _monitor_[] = "monitor";
static const char _session_[] = "session";
static const char _set_[] = "set";
static const char _subscribe_[] = "subscribe";
static const char _trace_[] = "trace";
static const char _unsubscribe_[] = "unsubscribe";

#if !WITHOUT_JSON_C

static const char _apis_[] = "apis";
static const char _verbosity_[] = "verbosity";

static const char _debug_[] = "debug";
static const char _info_[] = "info";
static const char _notice_[] = "notice";
static const char _warning_[] = "warning";
static const char _error_[] = "error";

#endif

static struct afb_api_common *monitor_api;
static struct afb_evt *evt_disconnected;

static void monitor_process(void *closure, struct afb_req_common *req);
static void monitor_describe(void *closure, void (*describecb)(void *, struct json_object *), void *clocb);

static struct afb_api_itf monitor_itf =
{
	.process = monitor_process,
	.describe = monitor_describe
};

int afb_monitor_init(struct afb_apiset *declare_set, struct afb_apiset *call_set)
{
	int rc;
	struct afb_api_item item;

	if (monitor_api != NULL) {
		rc = 0;
	}
	else {
		monitor_api = malloc(sizeof *monitor_api);
		if (monitor_api == NULL) {
			rc = X_ENOMEM;
		}
		else {
			afb_api_common_init(monitor_api, declare_set, call_set, _monitor_, 0, NULL, 0, NULL, 0, NULL);
			item.closure = NULL;
			item.group = NULL;
			item.itf = &monitor_itf;
			rc = afb_apiset_add(declare_set, _monitor_, item);
			afb_evt_create2(&evt_disconnected, _monitor_, _disconnected_);
		}
	}

	return rc;
}

void afb_monitor_api_disconnected(const char *apiname)
{
	struct afb_data *param;
	if (evt_disconnected) {
		afb_data_create_copy(&param, &afb_type_predefined_stringz, apiname, 1 + strlen(apiname));
		afb_evt_push_hookable(evt_disconnected, 1, &param);
	}
}

/******************************************************************************
**** Monitoring events
******************************************************************************/

#if !WITHOUT_JSON_C

/**
 * Subscribe or unsubscribe events
 * @param spec specification of the verbosity to set
 */
static void set_sub_unsub(
		struct afb_req_common *req,
		struct json_object *list,
		int (*action)(struct afb_req_common *req, struct afb_evt *evt)
) {
	int i, n;
	struct afb_evt *evt;
	struct json_object *obj;
	const char *evname;

	if (json_object_is_type(list, json_type_array)) {
		n = (int)json_object_array_length(list);
		if (n == 0)
			return;
		obj = json_object_array_get_idx(list, 0);
	}
	else  {
		n = 1;
		obj = list;
	}
	i = 0;
	for(;;) {
		if (json_object_is_type(obj, json_type_string)) {
			evname = json_object_get_string(obj);
			evt = 0;
			if (0 == strcmp(evname, _disconnected_))
				evt = evt_disconnected;
			if (evt)
				action(req, evt);
		}
		if (++i >= n)
			return;
		obj = json_object_array_get_idx(list, (rp_jsonc_index_t)i);
	}
}

/******************************************************************************
**** Monitoring verbosity
******************************************************************************/

/**
 * Translate verbosity indication to an integer value.
 * @param v the verbosity indication
 * @return the verbosity level (0, 1, 2 or 3) or -1 in case of error
 */
static int decode_verbosity(struct json_object *v)
{
	const char *s;
	int level = -1;

	if (!rp_jsonc_unpack(v, "i", &level)) {
		level = level < _VERBOSITY_(rp_Log_Level_Error) ? _VERBOSITY_(rp_Log_Level_Error) : level > _VERBOSITY_(rp_Log_Level_Debug) ? _VERBOSITY_(rp_Log_Level_Debug) : level;
	} else if (!rp_jsonc_unpack(v, "s", &s)) {
		level = _VERBOSITY_(rp_verbose_level_of_name(s));
	}
	return level;
}

/**
 * callback for setting verbosity on all apis
 * @param set the apiset
 * @param the name of the api to set
 * @param closure the verbosity to set as an integer casted to a pointer
 */
static void set_verbosity_to_all_cb(void *closure, struct afb_apiset *set, const char *name, const char *aliasto)
{
	if (!aliasto)
		afb_apiset_set_logmask(set, name, (int)(intptr_t)closure);
}

/**
 * set the verbosity 'level' of the api of 'name'
 * @param name the api name or "*" for any api or NULL or "" for global verbosity
 * @param level the verbosity level to set
 */
static void set_verbosity_to(const char *name, int level)
{
	int mask = verbosity_to_mask(level);
	if (!name || !name[0])
		verbosity_set(level);
	else if (name[0] == '*' && !name[1])
		afb_apiset_enum(monitor_api->call_set, 1, set_verbosity_to_all_cb, (void*)(intptr_t)mask);
	else
		afb_apiset_set_logmask(monitor_api->call_set, name, mask);
}

/**
 * Set verbosities accordling to specification in 'spec'
 * @param spec specification of the verbosity to set
 */
static void set_verbosity(struct json_object *spec)
{
	int l;
	struct json_object_iterator it, end;

	if (json_object_is_type(spec, json_type_object)) {
		it = json_object_iter_begin(spec);
		end = json_object_iter_end(spec);
		while (!json_object_iter_equal(&it, &end)) {
			l = decode_verbosity(json_object_iter_peek_value(&it));
			if (l >= 0)
				set_verbosity_to(json_object_iter_peek_name(&it), l);
			json_object_iter_next(&it);
		}
	} else {
		l = decode_verbosity(spec);
		if (l >= 0) {
			set_verbosity_to("", l);
			set_verbosity_to("*", l);
		}
	}
}

/**
 * Translate verbosity level to a protocol indication.
 * @param level the verbosity
 * @return the encoded verbosity
 */
static struct json_object *encode_verbosity(int level)
{
	switch(_DEVERBOSITY_(level)) {
	case rp_Log_Level_Error:	return json_object_new_string(_error_);
	case rp_Log_Level_Warning:	return json_object_new_string(_warning_);
	case rp_Log_Level_Notice:	return json_object_new_string(_notice_);
	case rp_Log_Level_Info:	return json_object_new_string(_info_);
	case rp_Log_Level_Debug:	return json_object_new_string(_debug_);
	default: return json_object_new_int(level);
	}
}

/**
 * callback for getting verbosity of all apis
 * @param set the apiset
 * @param the name of the api to set
 * @param closure the json object to build
 */
static void get_verbosity_of_all_cb(void *closure, struct afb_apiset *set, const char *name, const char *aliasto)
{
	struct json_object *resu = closure;
	int m = afb_apiset_get_logmask(set, name);
	if (m >= 0)
		json_object_object_add(resu, name, encode_verbosity(verbosity_from_mask(m)));
}

/**
 * get in resu the verbosity of the api of 'name'
 * @param resu the json object to build
 * @param name the api name or "*" for any api or NULL or "" for global verbosity
 */
static void get_verbosity_of(struct json_object *resu, const char *name)
{
	int m;
	if (!name || !name[0])
		json_object_object_add(resu, "", encode_verbosity(verbosity_get()));
	else if (name[0] == '*' && !name[1])
		afb_apiset_enum(monitor_api->call_set, 1, get_verbosity_of_all_cb, resu);
	else {
		m = afb_apiset_get_logmask(monitor_api->call_set, name);
		if (m >= 0)
			json_object_object_add(resu, name, encode_verbosity(verbosity_from_mask(m)));
	}
}

/**
 * get verbosities accordling to specification in 'spec'
 * @param resu the json object to build
 * @param spec specification of the verbosity to set
 */
static struct json_object *get_verbosity(struct json_object *spec)
{
	int i, n;
	struct json_object *resu;
	struct json_object_iterator it, end;

	resu = json_object_new_object();
	if (json_object_is_type(spec, json_type_object)) {
		it = json_object_iter_begin(spec);
		end = json_object_iter_end(spec);
		while (!json_object_iter_equal(&it, &end)) {
			if (json_object_get_boolean(json_object_iter_peek_value(&it)))
				get_verbosity_of(resu, json_object_iter_peek_name(&it));
			json_object_iter_next(&it);
		}
	} else if (json_object_is_type(spec, json_type_array)) {
		n = (int)json_object_array_length(spec);
		for (i = 0 ; i < n ; i++)
			get_verbosity_of(resu, json_object_get_string(json_object_array_get_idx(spec, (rp_jsonc_index_t)i)));
	} else if (json_object_is_type(spec, json_type_string)) {
		get_verbosity_of(resu, json_object_get_string(spec));
	} else if (json_object_get_boolean(spec)) {
		get_verbosity_of(resu, "");
		get_verbosity_of(resu, "*");
	}
	return resu;
}

/******************************************************************************
**** Manage namelist of api names
******************************************************************************/

struct namelist {
	struct namelist *next;
	char name[];
};

static struct namelist *reverse_namelist(struct namelist *head)
{
	struct namelist *previous, *next;

	previous = NULL;
	while(head) {
		next = head->next;
		head->next = previous;
		previous = head;
		head = next;
	}
	return previous;
}

static void add_one_name_to_namelist(struct namelist **head, const char *name)
{
	size_t length = strlen(name) + 1;
	struct namelist *item = malloc(length + sizeof *item);
	if (!item)
		RP_ERROR("out of memory");
	else {
		item->next = *head;
		memcpy(item->name, name, length);
		*head = item;
	}
}

static void get_apis_namelist_of_all_cb(void *closure, struct afb_apiset *set, const char *name, const char *aliasto)
{
	struct namelist **head = closure;
	add_one_name_to_namelist(head, name);
}

/**
 * get apis names as a list accordling to specification in 'spec'
 * @param spec specification of the apis to get
 */
static struct namelist *get_apis_namelist(struct json_object *spec)
{
	int i, n;
	struct json_object_iterator it, end;
	struct namelist *head;

	head = NULL;
	if (json_object_is_type(spec, json_type_object)) {
		it = json_object_iter_begin(spec);
		end = json_object_iter_end(spec);
		while (!json_object_iter_equal(&it, &end)) {
			if (json_object_get_boolean(json_object_iter_peek_value(&it)))
				add_one_name_to_namelist(&head, json_object_iter_peek_name(&it));
			json_object_iter_next(&it);
		}
	} else if (json_object_is_type(spec, json_type_array)) {
		n = (int)json_object_array_length(spec);
		for (i = 0 ; i < n ; i++)
			add_one_name_to_namelist(&head,
						 json_object_get_string(
							 json_object_array_get_idx(spec, (rp_jsonc_index_t)i)));
	} else if (json_object_is_type(spec, json_type_string)) {
		add_one_name_to_namelist(&head, json_object_get_string(spec));
	} else if (json_object_get_boolean(spec)) {
		afb_apiset_enum(monitor_api->call_set, 1, get_apis_namelist_of_all_cb, &head);
	}
	return reverse_namelist(head);
}

/******************************************************************************
**** Monitoring apis
******************************************************************************/

struct desc_apis {
	struct namelist *names;
	struct json_object *resu;
	struct json_object *apis;
	struct afb_req_common *req;
};

static void describe_first_api(struct desc_apis *desc);

static void on_api_description(void *closure, struct json_object *apidesc)
{
	struct desc_apis *desc = closure;
	struct namelist *head = desc->names;

	if (apidesc || afb_apiset_get_api(monitor_api->call_set, head->name, 1, 0, NULL))
		json_object_object_add(desc->apis, head->name, apidesc);
	desc->names = head->next;
	free(head);
	describe_first_api(desc);
}

static void describe_first_api(struct desc_apis *desc)
{
	struct namelist *head = desc->names;

	if (head)
		afb_apiset_describe(monitor_api->call_set, head->name, on_api_description, desc);
	else {
		afb_json_legacy_req_reply_hookable(desc->req, desc->resu, NULL, NULL);
		afb_req_common_unref(desc->req);
		free(desc);
	}
}

static void describe_apis(struct afb_req_common *req, struct json_object *resu, struct json_object *spec)
{
	struct desc_apis *desc;

	desc = malloc(sizeof *desc);
	if (!desc)
		afb_req_common_reply_out_of_memory_error_hookable(req);
	else {
		desc->req = afb_req_common_addref(req);
		desc->resu = resu;
		desc->apis = json_object_new_object();
		json_object_object_add(desc->resu, _apis_, desc->apis);
		desc->names = get_apis_namelist(spec);
		describe_first_api(desc);
	}
}

static void list_apis_cb(void *closure, struct afb_apiset *set, const char *name, const char *aliasto)
{
	struct json_object *apis = closure;
	json_object_object_add(apis, name, aliasto ? json_object_new_string(aliasto) : json_object_new_boolean(1));
}

static void list_apis(struct afb_req_common *req, struct json_object *resu, struct json_object *spec)
{
	struct json_object *apis = json_object_new_object();
	json_object_object_add(resu, _apis_, apis);
	afb_apiset_enum(monitor_api->call_set, 1, list_apis_cb, apis);
	afb_json_legacy_req_reply_hookable(req, resu, NULL, NULL);
}

static void get_apis(struct afb_req_common *req, struct json_object *resu, struct json_object *spec)
{
	if ((json_object_get_type(spec) == json_type_boolean && !json_object_get_boolean(spec))
	 || (json_object_get_type(spec) == json_type_string && !strcmp(json_object_get_string(spec), "*")))
		list_apis(req, resu, spec);
	else
		describe_apis(req, resu, spec);
}

/******************************************************************************
**** Implementation monitoring verbs
******************************************************************************/

static void f_get_cb(void *closure, struct json_object *args)
{
	struct afb_req_common *req = closure;
	struct json_object *r;
	struct json_object *apis = NULL;
	struct json_object *verbosity = NULL;

	rp_jsonc_unpack(args, "{s?:o,s?:o}", _verbosity_, &verbosity, _apis_, &apis);
	if (!verbosity && !apis)
		afb_json_legacy_req_reply_hookable(req, NULL, NULL, NULL);
	else {
		r = json_object_new_object();
		if (!r)
			afb_req_common_reply_out_of_memory_error_hookable(req);
		else {
			if (verbosity) {
				verbosity = get_verbosity(verbosity);
				json_object_object_add(r, _verbosity_, verbosity);
			}
			if (!apis)
				afb_json_legacy_req_reply_hookable(req, r, NULL, NULL);
			else
				get_apis(req, r, apis);
		}
	}
}

static void f_get(struct afb_req_common *req)
{
	afb_json_legacy_do_single_json_c(req->params.ndata, req->params.data, f_get_cb, req);
}

static void f_set_cb(void *closure, struct json_object *args)
{
	struct afb_req_common *req = closure;
	struct json_object *verbosity = NULL,  *subscribe = NULL,  *unsubscribe = NULL;

	rp_jsonc_unpack(args, "{s?o s?o s?o}",
				_verbosity_, &verbosity,
				_subscribe_, &subscribe,
				_unsubscribe_, &unsubscribe);
	if (verbosity)
		set_verbosity(verbosity);
	if (unsubscribe)
		set_sub_unsub(req, unsubscribe, afb_req_common_unsubscribe_hookable);
	if (subscribe)
		set_sub_unsub(req, subscribe, afb_req_common_subscribe_hookable);

	afb_json_legacy_req_reply_hookable(req, NULL, NULL, NULL);
}

static void f_set(struct afb_req_common *req)
{
	afb_json_legacy_do_single_json_c(req->params.ndata, req->params.data, f_set_cb, req);
}

static void f_subscribe_cb(void *closure, struct json_object *args)
{
	struct afb_req_common *req = closure;

	set_sub_unsub(req, args, afb_req_common_subscribe_hookable);
	afb_json_legacy_req_reply_hookable(req, NULL, NULL, NULL);
}

static void f_subscribe(struct afb_req_common *req)
{
	afb_json_legacy_do_single_json_c(req->params.ndata, req->params.data, f_subscribe_cb, req);
}

static void f_unsubscribe_cb(void *closure, struct json_object *args)
{
	struct afb_req_common *req = closure;

	set_sub_unsub(req, args, afb_req_common_unsubscribe_hookable);
	afb_json_legacy_req_reply_hookable(req, NULL, NULL, NULL);
}

static void f_unsubscribe(struct afb_req_common *req)
{
	afb_json_legacy_do_single_json_c(req->params.ndata, req->params.data, f_unsubscribe_cb, req);
}

static void f_session(struct afb_req_common *req)
{
	struct json_object *r = NULL;

	/* make the result */
	rp_jsonc_pack(&r, "{s:s,s:i,s:i}",
			"uuid", afb_session_uuid(req->session),
			"timeout", afb_session_timeout(req->session),
			"remain", afb_session_what_remains(req->session));
	afb_json_legacy_req_reply_hookable(req, r, NULL, NULL);
}
#else
static void f_set(struct afb_req_common *req)
{
	afb_req_common_reply_unavailable_error_hookable(req);
}
static void f_get(struct afb_req_common *req)
{
	afb_req_common_reply_unavailable_error_hookable(req);
}
static void f_session(struct afb_req_common *req)
{
	char *buffer;
	int rc = asprintf(&buffer, "{\"uuid\":\"%s\",\"timeout\":%d,\"remain\":%d}",
			afb_session_uuid(req->session),
			afb_session_timeout(req->session),
			afb_session_what_remains(req->session));
	if (rc < 0)
		afb_req_common_reply_out_of_memory_error_hookable(req);
	else {
		struct afb_data *data;
		rc = afb_data_create_raw(&data, &afb_type_predefined_json, buffer, 1 + (size_t)rc, free, buffer);
		if (rc < 0)
			afb_req_common_reply_internal_error_hookable(req, rc);
		else
			afb_req_common_reply_hookable(req, 0, 1, &data);
	}
}
static void f_subscribe(struct afb_req_common *req)
{
	afb_req_common_subscribe_hookable(req, evt_disconnected);
	afb_req_common_reply_hookable(req, 0, 0, NULL);
}
static void f_unsubscribe(struct afb_req_common *req)
{
	afb_req_common_unsubscribe_hookable(req, evt_disconnected);
	afb_req_common_reply_hookable(req, 0, 0, NULL);
}
#endif

#if WITH_AFB_TRACE
static void context_destroy(void *pointer)
{
	struct afb_trace *trace = pointer;
	afb_trace_unref(trace);
}

static int context_create(void *closure, void **value, void (**freecb)(void*), void **freeclo)
{
	struct afb_trace *trace;

	trace = afb_trace_create(_monitor_, NULL);
	if (trace == NULL)
		return X_ENOMEM;

	*value = *freeclo = trace;
	*freecb = context_destroy;
	return 0;
}

static void f_trace_cb(void *closure, struct json_object *args)
{
	struct afb_req_common *req = closure;
	int rc;
	struct json_object *add = NULL;
	struct json_object *drop = NULL;
	struct afb_trace *trace;

	afb_session_cookie_getinit(req->session, _monitor_, (void**)&trace, context_create, req);
	rp_jsonc_unpack(args, "{s?o s?o}", "add", &add, "drop", &drop);
	if (add) {
		rc = afb_trace_add(req, add, trace);
		if (rc)
			goto end;
	}
	if (drop) {
		rc = afb_trace_drop(req, drop, trace);
		if (rc)
			goto end;
	}
	afb_json_legacy_req_reply_hookable(req, NULL, NULL, NULL);
end:
	afb_apiset_update_hooks(monitor_api->call_set, NULL);
	afb_evt_update_hooks();
	return;
}

static void f_trace(struct afb_req_common *req)
{
	afb_json_legacy_do_single_json_c(req->params.ndata, req->params.data, f_trace_cb, req);
}
#else
static void f_trace(struct afb_req_common *req)
{
	afb_req_common_reply_unavailable_error_hookable(req);
}
#endif

void checkcb(void *closure, int status)
{
	struct afb_req_common *req = closure;
	void (*fun)(struct afb_req_common*);

	if (status > 0) {
		fun = afb_req_common_async_pop(req);
		fun(req);
	}
}

static void monitor_process(void *closure, struct afb_req_common *req)
{
	void (*fun)(struct afb_req_common*) = NULL;
	struct afb_auth *auth = NULL;

	switch (req->verbname[0]) {
	case 'g':
		if (0 == strcmp(req->verbname, _get_))
			fun = f_get;
		break;
	case 's':
		if (0 == strcmp(req->verbname, _set_))
			fun = f_set;
		else if (0 == strcmp(req->verbname, _session_))
			fun = f_session;
		else if (0 == strcmp(req->verbname, _subscribe_))
			fun = f_subscribe;
		break;
	case 't':
		if (0 == strcmp(req->verbname, _trace_))
			fun = f_trace;
		break;
	case 'u':
		if (0 == strcmp(req->verbname, _unsubscribe_))
			fun = f_unsubscribe;
		break;
	default:
		break;
	}
	if (fun == NULL) {
		afb_req_common_reply_verb_unknown_error_hookable(req);
	}
	else if (auth) {
		if (afb_req_common_async_push(req, fun) < 0)
			afb_req_common_reply_internal_error_hookable(req, X_EOVERFLOW);
		else
			afb_req_common_check_and_set_session_async(req, auth, AFB_SESSION_CHECK, checkcb, req);

	}
	else {
		fun(req);
	}
}

static void monitor_describe(void *closure, void (*describecb)(void *, struct json_object *), void *clocb)
{
	describecb(clocb, NULL /* TODO */);
}
