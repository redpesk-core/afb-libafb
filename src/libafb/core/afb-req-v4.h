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

#pragma once

#include "afb-req-common.h"

struct afb_api_v4;
struct afb_verb_v4;
struct afb_req_v4;

/**
 * Checks whether the request 'req' is valid or not.
 *
 * @param req the request to check
 *
 * @return 0 if not valid or 1 if valid.
 */
static inline
int
afb_req_v4_is_valid(
	struct afb_req_v4 *reqv4
) {
	return !!reqv4;
}

/**
 * Get the common request linked to reqv4
 *
 * @param reqv4 the req to query
 *
 * @return the common request attached to the request
 */
extern
struct afb_req_common *
afb_req_v4_get_common(
	struct afb_req_v4 *reqv4
);

extern
void
afb_req_v4_process(
	struct afb_req_common *comreq,
	struct afb_api_v4 *api,
	const struct afb_verb_v4 *verbv4
);


extern
struct afb_req_v4 *
afb_req_v4_addref_hookable(
	struct afb_req_v4 *reqv4
);

extern
void
afb_req_v4_unref_hookable(
	struct afb_req_v4 *reqv4
);

extern
void
afb_req_v4_vverbose_hookable(
	struct afb_req_v4 *reqv4,
	int level,
	const char *file,
	int line,
	const char *func,
	const char *fmt,
	va_list args
);

/**
 * Send associated to 'req' a message described by 'fmt' and following parameters
 * to the journal for the verbosity 'level'.
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
 * @param req the request
 * @param level the level of the message
 * @param file the source filename that emits the message or NULL
 * @param line the line number in the source filename that emits the message
 * @param func the name of the function that emits the message or NULL
 * @param fmt the message format as for printf
 * @param ... the arguments of the format 'fmt'
 *
 * @see printf
 * @see afb_req_vverbose
 */
__attribute__((format(printf, 6, 7)))
void
afb_req_v4_verbose_hookable(
	struct afb_req_v4 *reqv4,
	int level, const char *file,
	int line,
	const char * func,
	const char *fmt,
	...
);

extern
void *
afb_req_v4_LEGACY_cookie_hookable(
	struct afb_req_v4 *reqv4,
	int replace,
	void *(*create_value)(void*),
	void (*free_value)(void*),
	void *create_closure
);

/* set the cookie of the api getting the request */
extern
int
afb_req_v4_cookie_set_hookable(
	struct afb_req_v4 *reqv4,
	void *value,
	void (*freecb)(void*),
	void *freeclo
);

/* get the cookie of the api getting the request */
extern
int
afb_req_v4_cookie_get_hookable(
	struct afb_req_v4 *reqv4,
	void **value
);

/* get the cookie of the api getting the request */
extern
int
afb_req_v4_cookie_getinit_hookable(
	struct afb_req_v4 *reqv4,
	void **value,
	int (*initcb)(void *closure, void **value, void (**freecb)(void*), void **freeclo),
	void *closure
);

/* set the cookie of the api getting the request */
extern
int
afb_req_v4_cookie_drop_hookable(
	struct afb_req_v4 *reqv4
);

extern
int
afb_req_v4_session_set_LOA_hookable(
	struct afb_req_v4 *reqv4,
	unsigned level
);

extern
unsigned
afb_req_v4_session_get_LOA_hookable(
	struct afb_req_v4 *reqv4
);

extern
void
afb_req_v4_session_close_hookable(
	struct afb_req_v4 *reqv4
);

extern
struct json_object *
afb_req_v4_get_client_info_hookable(
	struct afb_req_v4 *reqv4
);

extern
int
afb_req_v4_logmask(
	struct afb_req_v4 *reqv4
);

extern
struct afb_api_v4 *
afb_req_v4_api(
	struct afb_req_v4 *reqv4
);

extern
void *
afb_req_v4_vcbdata(
	struct afb_req_v4 *reqv4
);

extern
const char *
afb_req_v4_called_api(
	struct afb_req_v4 *reqv4
);

extern
const char *
afb_req_v4_called_verb(
	struct afb_req_v4 *reqv4
);

extern
int
afb_req_v4_subscribe_hookable(
	struct afb_req_v4 *reqv4,
	struct afb_evt *event
);

extern
int
afb_req_v4_unsubscribe_hookable(
	struct afb_req_v4 *reqv4,
	struct afb_evt *event
);

extern
void
afb_req_v4_check_permission_hookable(
	struct afb_req_v4 *reqv4,
	const char *permission,
	void (*callback)(void*,int,struct afb_req_v4*),
	void *closure
);

extern
unsigned
afb_req_v4_parameters(
	struct afb_req_v4 *reqv4,
	struct afb_data * const **params
);

/**
 * Convert the parameter of the request of the given index
 * to a given type and return it.
 *
 * The converted result is substituted to the previous parameter.
 * There is no need to unreference the returned data as it becomes
 * part of the request and will be released .
 *
 * Previous value of the parameter is automatically unreferenced.
 * If you want keep it, you have to first reference it using afb_data_addref.
 *
 * @param reqv4 the request
 * @param index index of the parameter to convert
 * @param type  target type of the conversion
 * @param result where to store the result (can be NULL)
 *
 * @return 0 in case of success, a negative code on error
 */
extern
int
afb_req_v4_param_convert(
	struct afb_req_v4 *reqv4,
	unsigned index,
	struct afb_type *type,
	struct afb_data **result
);

extern
void
afb_req_v4_reply_hookable(
	struct afb_req_v4 *reqv4,
	int status,
	unsigned nparams,
	struct afb_data * const params[]
);

extern
void
afb_req_v4_subcall_hookable(
	struct afb_req_v4 *reqv4,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	struct afb_data * const params[],
	int flags,
	void (*callback)(void *closure, int status, unsigned, struct afb_data * const[], struct afb_req_v4 *reqv4),
	void *closure
);

extern
int
afb_req_v4_subcall_sync_hookable(
	struct afb_req_v4 *reqv4,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	struct afb_data * const params[],
	int flags,
	int *status,
	unsigned *nreplies,
	struct afb_data *replies[]
);

/** get a specialized interface for the request req */
int afb_req_v4_interface_by_id(
	struct afb_req_v4 *reqv4,
	int id,
	void **result
);

/** get a specialized interface for the request req */
int afb_req_v4_interface_by_name(
	struct afb_req_v4 *reqv4,
	const char *name,
	void **result
);

/** get a specialized interface for the request req */
int afb_req_v4_interface_by_id_hookable(
	struct afb_req_v4 *reqv4,
	int id,
	void **result
);

/** get a specialized interface for the request req */
int afb_req_v4_interface_by_name_hookable(
	struct afb_req_v4 *reqv4,
	const char *name,
	void **result
);

/** Get the user data associated to the request */
extern
void *
afb_req_v4_get_userdata(
	struct afb_req_v4 *reqv4
);

/** set (associate) the user data to the request */
extern
void
afb_req_v4_set_userdata(
	struct afb_req_v4 *reqv4,
	void *userdata,
	void (*freecb)(void*)
);

/** Get the user data associated to the request (hookable) */
extern
void *
afb_req_v4_get_userdata_hookable(
	struct afb_req_v4 *reqv4
);

/** set (associate) the user data to the request (hookable) */
extern
void
afb_req_v4_set_userdata_hookable(
	struct afb_req_v4 *reqv4,
	void *userdata,
	void (*freecb)(void*)
);

