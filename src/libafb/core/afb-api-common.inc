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
                                  A P I         C O M M O N


 Define CLOSURE_T the type of the received closure first parameter

 Define CLOSURE_TO_COMMON_API(closure) the macro that returns the
 address of the related afb_common_api knowing closure

 Exemples:

    #define CLOSURE_T                        struct afb_api_x3
    #define CLOSURE_TO_COMMON_API(closure)   api_x3_to_api_common(closure)

 ******************************************************************************
 ******************************************************************************
 ******************************************************************************
 ******************************************************************************/

/**********************************************
* normal flow
**********************************************/
static
void
common_api_vverbose(
	CLOSURE_T closure,
	int level,
	const char *file,
	int line,
	const char *function,
	const char *fmt,
	va_list args
) {
	struct afb_api_common *comapi = CLOSURE_TO_COMMON_API(closure);
	afb_api_common_vverbose(comapi, level, file, line, function, fmt, args);
}

static
int
common_api_queue_job(
	CLOSURE_T closure,
	void (*callback)(int signum, void *arg),
	void *argument,
	void *group,
	int timeout
) {
	struct afb_api_common *comapi = CLOSURE_TO_COMMON_API(closure);
	return afb_api_common_queue_job(comapi, callback, argument, group, timeout);
}

static
int
common_api_require_api(
	CLOSURE_T closure,
	const char *name,
	int initialized
) {
	struct afb_api_common *comapi = CLOSURE_TO_COMMON_API(closure);
	return afb_api_common_require_api(comapi, name, initialized);
}

static
int
common_api_add_alias(
	CLOSURE_T closure,
	const char *apiname,
	const char *aliasname
) {
	struct afb_api_common *comapi = CLOSURE_TO_COMMON_API(closure);
	return afb_api_common_add_alias(comapi, apiname, aliasname);
}

static
void
common_api_seal(
	CLOSURE_T closure
) {
	struct afb_api_common *comapi = CLOSURE_TO_COMMON_API(closure);
	afb_api_common_api_seal(comapi);
}

static
int
common_api_class_provide(
	CLOSURE_T closure,
	const char *name
) {
	struct afb_api_common *comapi = CLOSURE_TO_COMMON_API(closure);
	return afb_api_common_class_provide(comapi, name);
}

static
int
common_api_class_require(
	CLOSURE_T closure,
	const char *name
) {
	struct afb_api_common *comapi = CLOSURE_TO_COMMON_API(closure);
	return afb_api_common_class_require(comapi, name);
}

static
struct json_object *
common_api_settings(
	CLOSURE_T closure
) {
	struct afb_api_common *comapi = CLOSURE_TO_COMMON_API(closure);
	return afb_api_common_settings(comapi);
}

/**********************************************
* hooked flow
**********************************************/
#if WITH_AFB_HOOK
static
void
common_api_hooked_vverbose(
	CLOSURE_T closure,
	int level,
	const char *file,
	int line,
	const char *function,
	const char *fmt,
	va_list args
) {
	struct afb_api_common *comapi = CLOSURE_TO_COMMON_API(closure);
	afb_api_common_vverbose_hookable(comapi, level, file, line, function, fmt, args);
}

static
int
common_api_hooked_queue_job(
	CLOSURE_T closure,
	void (*callback)(int signum, void *arg),
	void *argument,
	void *group,
	int timeout
) {
	struct afb_api_common *comapi = CLOSURE_TO_COMMON_API(closure);
	return afb_api_common_queue_job_hookable(comapi, callback, argument, group, timeout);
}

static
int
common_api_hooked_require_api(
	CLOSURE_T closure,
	const char *name,
	int initialized
) {
	struct afb_api_common *comapi = CLOSURE_TO_COMMON_API(closure);
	return afb_api_common_require_api_hookable(comapi, name, initialized);
}

static
int
common_api_hooked_add_alias(
	CLOSURE_T closure,
	const char *apiname,
	const char *aliasname
) {
	struct afb_api_common *comapi = CLOSURE_TO_COMMON_API(closure);
	return afb_api_common_add_alias_hookable(comapi, apiname, aliasname);
}

static
void
common_api_hooked_seal(
	CLOSURE_T closure
) {
	struct afb_api_common *comapi = CLOSURE_TO_COMMON_API(closure);
	afb_api_common_api_seal_hookable(comapi);
}

static
int
common_api_hooked_class_provide(
	CLOSURE_T closure,
	const char *name
) {
	struct afb_api_common *comapi = CLOSURE_TO_COMMON_API(closure);
	return afb_api_common_class_provide_hookable(comapi, name);
}

static
int
common_api_hooked_class_require(
	CLOSURE_T closure,
	const char *name
) {
	struct afb_api_common *comapi = CLOSURE_TO_COMMON_API(closure);
	return afb_api_common_class_require_hookable(comapi, name);
}

static
struct json_object *
common_api_hooked_settings(
	CLOSURE_T closure
) {
	struct afb_api_common *comapi = CLOSURE_TO_COMMON_API(closure);
	return afb_api_common_settings_hookable(comapi);
}
#endif

