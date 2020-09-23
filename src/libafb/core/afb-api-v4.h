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

struct afb_apiset;
struct afb_api_v4;
struct afb_api_x4;
struct afb_auth;
struct afb_req_x4;
struct afb_verb_v4;
struct afb_binding_v4;
struct afb_req_common;
struct json_object;

struct afb_api_common;
enum afb_string_mode;

extern
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
);

/**
 * Call safely the ctlproc with the given parameters
 *
 * @param apiv4    the api to pass to the ctlproc
 * @param ctlproc  the ctlproc to call
 * @param ctlid    the ctlid to pass to the ctlproc
 * @param ctlarg   the ctlarg to pass to the ctlproc
 *
 * @return a negative value on error or else a positive or null value
 */
extern
int
afb_api_v4_safe_ctlproc(
	struct afb_api_v4 *apiv4,
	int (*ctlproc)(const struct afb_api_v4 *, afb_ctlid_t, afb_ctlarg_t),
	afb_ctlid_t ctlid,
	afb_ctlarg_t ctlarg
);

extern
int
afb_api_v4_set_binding_fields(
	struct afb_api_v4 *apiv4,
	const struct afb_binding_v4 *desc
);

extern
struct afb_api_v4 *
afb_api_v4_addref(
	struct afb_api_v4 *api
);

extern
void
afb_api_v4_unref(
	struct afb_api_v4 *api
);

extern
struct afb_api_common *
afb_api_v4_get_api_common(
	struct afb_api_v4 *api
);

extern
void
afb_api_v4_seal(
	struct afb_api_v4 *api
);

extern
int
afb_api_v4_add_verb(
	struct afb_api_v4 *api,
	const char *verb,
	const char *info,
	void (*callback)(const struct afb_req_v4 *req, unsigned nparams, const struct afb_data * const params[]),
	void *vcbdata,
	const struct afb_auth *auth,
	uint32_t session,
	int glob
);

extern
int
afb_api_v4_del_verb(
	struct afb_api_v4 *api,
	const char *verb,
	void **vcbdata
);

extern
void
afb_api_v4_process_call(
	struct afb_api_v4 *api,
	struct afb_req_common *req
);

extern
struct json_object *
afb_api_v4_make_description_openAPIv3(
	struct afb_api_v4 *api
);

/************************************************/

extern
int
afb_api_v4_logmask(
	struct afb_api_v4 *api
);

extern
const char *
afb_api_v4_name(
	struct afb_api_v4 *apiv4
);

extern
const char *
afb_api_v4_info(
	struct afb_api_v4 *apiv4
);

extern
const char *
afb_api_v4_path(
	struct afb_api_v4 *apiv4
);

extern
void *
afb_api_v4_get_userdata(
	struct afb_api_v4 *apiv4
);

extern
void *
afb_api_v4_set_userdata(
	struct afb_api_v4 *apiv4,
	void *value
);

/************************************************/

extern
void
afb_api_v4_vverbose_hookable(
	struct afb_api_v4 *apiv4,
	int level,
	const char *file,
	int line,
	const char *function,
	const char *fmt,
	va_list args
);

extern
struct json_object *
afb_api_v4_settings_hookable(
	struct afb_api_v4 *apiv4
);

extern
int
afb_api_v4_require_api_hookable(
	struct afb_api_v4 *apiv4,
	const char *name,
	int initialized
);

extern
int
afb_api_v4_class_provide_hookable(
	struct afb_api_v4 *apiv4,
	const char *name
);

extern
int
afb_api_v4_class_require_hookable(
	struct afb_api_v4 *apiv4,
	const char *name
);

extern
int
afb_api_v4_event_broadcast_hookable(
	struct afb_api_v4 *apiv4,
	const char *name,
	unsigned nparams,
	struct afb_data * const params[]
);

extern
int
afb_api_v4_new_event_hookable(
	struct afb_api_v4 *apiv4,
	const char *name,
	struct afb_evt **event
);

extern
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
	void *closure
);

extern
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
);

extern
int
afb_api_v4_new_api_hookable(
	struct afb_api_v4 *apiv4,
	struct afb_api_v4 **newapiv4,
	const char *apiname,
	const char *info,
	int noconcurrency,
	int (*mainctl)(const struct afb_api_v4*, afb_ctlid_t, afb_ctlarg_t),
	void *closure
);

extern
int
afb_api_v4_set_verbs_hookable(
	struct afb_api_v4 *apiv4,
	const struct afb_verb_v4 *verbs
);

extern
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
);

extern
void
afb_api_v4_seal_hookable(
	struct afb_api_v4 *apiv4
);

extern
int
afb_api_v4_del_verb_hookable(
	struct afb_api_v4 *apiv4,
	const char *verb,
	void **vcbdata
);

extern
int
afb_api_v4_delete_api_hookable(
	struct afb_api_v4 *apiv4
);

extern
int
afb_api_v4_event_handler_add_hookable(
	struct afb_api_v4 *apiv4,
	const char *pattern,
	void (*callback)(void*,const char*,unsigned,struct afb_data * const[],struct afb_api_v4*),
	void *closure
);

extern
int
afb_api_v4_event_handler_del_hookable(
	struct afb_api_v4 *apiv4,
	const char *pattern,
	void **closure
);

extern
int
afb_api_v4_queue_job_hookable(
	struct afb_api_v4 *apiv4,
	void (*callback)(int signum, void *arg),
	void *argument,
	void *group,
	int timeout
);

extern
int
afb_api_v4_add_alias_hookable(
	struct afb_api_v4 *apiv4,
	const char *apiname,
	const char *aliasname
);
