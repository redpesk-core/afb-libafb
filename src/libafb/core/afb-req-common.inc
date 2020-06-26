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
void common_req_vverbose(
	CLOSURE_T closure,
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
void *
common_req_cookie(
	CLOSURE_T closure,
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
common_req_session_set_LOA(
	CLOSURE_T closure,
	unsigned level
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	return afb_req_common_session_set_LOA(comreq, level);
}

static
void
common_req_session_close(
	CLOSURE_T closure
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	afb_req_common_session_close(comreq);
}

static
struct json_object *
common_req_get_client_info(
	CLOSURE_T closure
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	return afb_req_common_get_client_info(comreq);
}

/******************************************************************************/
#if WITH_AFB_HOOK

static
void common_req_hooked_vverbose(
	CLOSURE_T closure,
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
void *
common_req_hooked_cookie(
	CLOSURE_T closure,
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
common_req_hooked_session_set_LOA(
	CLOSURE_T closure,
	unsigned level
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	return afb_req_common_session_set_LOA_hookable(comreq, level);
}

static
void
common_req_hooked_session_close(
	CLOSURE_T closure
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	afb_req_common_session_close_hookable(comreq);
}

static
struct json_object *
common_req_hooked_get_client_info(
	CLOSURE_T closure
) {
	struct afb_req_common *comreq = CLOSURE_TO_REQ_COMMON(closure);
	return afb_req_common_get_client_info_hookable(comreq);
}

#endif
