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

/******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
                                  R E Q         C O M M O N


 Define CLOSURE_T the type of the received closure first parameter

 Define CLOSURE_TO_COMMON_REQ(closure) the macro that returns the
 address of the related afb_common_req knowing closure

 Exemples:

    #define CLOSURE_T                        struct afb_req_x2
    #define CLOSURE_TO_COMMON_REQ(closure)   req_x2_to_req_common(closure)

 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/

static
void req_vverbose_cb(
	CLOSURE_T *closure,
	int level,
	const char *file,
	int line,
	const char *func,
	const char *fmt,
	va_list args
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	afb_req_common_vverbose(comreq, level, file, line, func, fmt, args);
}

static
struct json_object *
req_json_cb(
	CLOSURE_T *closure
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	return afb_req_common_json(comreq);
}

static
struct afb_arg
req_get_cb(
	CLOSURE_T *closure,
	const char *name
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	return afb_req_common_get(comreq, name);
}

static
void
req_reply_cb(
	CLOSURE_T *closure,
	struct json_object *obj,
	const char *error,
	const char *info
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	afb_req_common_reply(comreq, obj, error, info);
}

static
void
req_vreply_cb(
	CLOSURE_T *closure,
	struct json_object *obj,
	const char *error,
	const char *fmt,
	va_list args
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	afb_req_common_vreply(comreq, obj, error, fmt, args);
}

static
int
req_subscribe_event_x2_cb(
	CLOSURE_T *closure,
	struct afb_event_x2 *event
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	return afb_req_common_subscribe_event_x2(comreq, event);
}

static
int
req_unsubscribe_event_x2_cb(
	CLOSURE_T *closure,
	struct afb_event_x2 *event
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	return afb_req_common_unsubscribe_event_x2(comreq, event);
}

static
void *
req_cookie_cb(
	CLOSURE_T *closure,
	int replace,
	void *(*create_value)(void*),
	void (*free_value)(void*),
	void *create_closure
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	return afb_req_common_cookie(comreq, create_value, free_value, create_closure, replace);
}

static
int
req_session_set_LOA_cb(
	CLOSURE_T *closure,
	unsigned level
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	return afb_req_common_session_set_LOA(comreq, level);
}

static
void
req_session_close_cb(
	CLOSURE_T *closure
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	afb_req_common_session_close(comreq);
}

static
struct json_object *
req_get_client_info_cb(
	CLOSURE_T *closure
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	return afb_req_common_get_client_info(comreq);
}

static
int
req_has_permission_cb(
	CLOSURE_T *closure,
	const char *permission
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	return afb_req_common_has_permission(comreq, permission);
}

/******************************************************************************/
#if WITH_AFB_HOOK

static
void req_vverbose_hookable_cb(
	CLOSURE_T *closure,
	int level,
	const char *file,
	int line,
	const char *func,
	const char *fmt,
	va_list args
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	afb_req_common_vverbose_hookable(comreq, level, file, line, func, fmt, args);
}

static
struct json_object *
req_json_hookable_cb(
	CLOSURE_T *closure
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	return afb_req_common_json_hookable(comreq);
}

static
struct afb_arg
req_get_hookable_cb(
	CLOSURE_T *closure,
	const char *name
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	return afb_req_common_get_hookable(comreq, name);
}

static
void
req_reply_hookable_cb(
	CLOSURE_T *closure,
	struct json_object *obj,
	const char *error,
	const char *info
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	afb_req_common_reply_hookable(comreq, obj, error, info);
}

static
void
req_vreply_hookable_cb(
	CLOSURE_T *closure,
	struct json_object *obj,
	const char *error,
	const char *fmt,
	va_list args
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	afb_req_common_vreply_hookable(comreq, obj, error, fmt, args);
}

static
int
req_subscribe_event_x2_hookable_cb(
	CLOSURE_T *closure,
	struct afb_event_x2 *event
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	return afb_req_common_subscribe_event_x2_hookable(comreq, event);
}

static
int
req_unsubscribe_event_x2_hookable_cb(
	CLOSURE_T *closure,
	struct afb_event_x2 *event
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	return afb_req_common_unsubscribe_event_x2_hookable(comreq, event);
}

static
void *
req_cookie_hookable_cb(
	CLOSURE_T *closure,
	int replace,
	void *(*create_value)(void*),
	void (*free_value)(void*),
	void *create_closure
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	return afb_req_common_cookie_hookable(comreq, create_value, free_value, create_closure, replace);
}

static
int
req_session_set_LOA_hookable_cb(
	CLOSURE_T *closure,
	unsigned level
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	return afb_req_common_session_set_LOA_hookable(comreq, level);
}

static
void
req_session_close_hookable_cb(
	CLOSURE_T *closure
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	afb_req_common_session_close_hookable(comreq);
}

static
struct json_object *
req_get_client_info_hookable_cb(
	CLOSURE_T *closure
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	return afb_req_common_get_client_info_hookable(comreq);
}

static
int
req_has_permission_hookable_cb(
	CLOSURE_T *closure,
	const char *permission
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	return afb_req_common_has_permission_hookable(comreq, permission);
}

#endif
