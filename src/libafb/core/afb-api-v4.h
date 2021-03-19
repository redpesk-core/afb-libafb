/*
 * Copyright (C) 2015-2021 IoT.bzh Company
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

#include "afb-api-common.h"
#include "afb-v4-itf.h"

struct afb_apiset;
struct afb_api_v4;
struct afb_auth;
struct afb_req_v4;
struct afb_verb_v4;
struct afb_binding_v4;
struct afb_req_common;
struct afb_data;
struct afb_evt;
struct json_object;

struct afb_api_common;
enum afb_string_mode;

/**
 * Creates an instance of given name and add it to the declare_set.
 *
 * If a preinit callback is given, it will be called at end of
 * the creation if every thing went right. It will receive its
 * closure and the created api. If it returns a negative number,
 * the creation is cancelled.
 *
 * @param api pointer where is stored the result
 * @param declare_set the apiset for declaring the new api
 * @param call_set the apiset to use for calls
 * @param name the name of the name
 * @param mode_name mode of use of the name
 * @param info info about the api, can be NULL
 * @param mode_info mode of use of the info
 * @param noconcurrency set the concurrency mode: 0 means concurrent, not zero means serial
 * @param preinit callback for preinitialisation (can be NULL)
 * @param closure closure of the preinit
 * @param path path of the binding shared object (can be NULL)
 * @param mode_path mode of use of the path
 *
 * @return 0 in case of success or a negative error code
 */
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
	afb_api_callback_x4_t ctlproc,
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
	void (*callback)(struct afb_req_v4 *req, unsigned nparams, struct afb_data * const params[]),
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

/**
 * Send to the journal with the log 'level' a message described
 * by 'fmt' and following parameters.
 *
 * 'file', 'line' and 'func' are indicators of position of the code in source files
 * (see macros __FILE__, __LINE__ and __func__).
 *
 * 'level' is defined by syslog standard:
 *      EMERGENCY         0        System is unusable
 *      ALERT             1        Action must be taken immediately
 *      CRITICAL          2        Critical conditions
 *      ERROR             3        Error conditions
 *      WARNING           4        Warning conditions
 *      NOTICE            5        Normal but significant condition
 *      INFO              6        Informational
 *      DEBUG             7        Debug-level messages
 *
 * @param api the api that collects the logging message
 * @param level the level of the message
 * @param file the source file that logs the messages or NULL
 * @param line the line in the source file that logs the message
 * @param func the name of the function in the source file that logs (or NULL)
 * @param fmt the format of the message as in printf
 * @param ... the arguments to the format string of the message
 *
 * @see syslog
 * @see printf
 */
extern
void
afb_api_verbose(
	struct afb_api_v4 *apiv4,
	int level,
	const char *file,
	int line,
	const char *func,
	const char *fmt,
	...
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
	afb_api_callback_x4_t mainctl,
	void *userdata
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
	void (*callback)(struct afb_req_v4 *req, unsigned nparams, struct afb_data * const params[]),
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
afb_api_v4_post_job_hookable(
	struct afb_api_v4 *apiv4,
	long delayms,
	int timeout,
	void (*callback)(int signum, void *arg),
	void *argument,
	void *group
);

extern
int
afb_api_v4_add_alias_hookable(
	struct afb_api_v4 *apiv4,
	const char *apiname,
	const char *aliasname
);
