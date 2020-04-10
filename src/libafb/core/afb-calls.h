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

struct json_object;

struct afb_export;
struct afb_xreq;

struct afb_api_x3;
struct afb_req_x1;
struct afb_req_x2;

/******************************************************************************/
extern
void afb_calls_call(
		struct afb_export *export,
		const char *api,
		const char *verb,
		struct json_object *args,
		void (*callback)(void*, struct json_object*, const char *error, const char *info, struct afb_api_x3*),
		void *closure);

#if WITH_AFB_CALL_SYNC
extern
int afb_calls_call_sync(
		struct afb_export *export,
		const char *api,
		const char *verb,
		struct json_object *args,
		struct json_object **object,
		char **error,
		char **info);
#endif

extern
void afb_calls_subcall(
			struct afb_xreq *xreq,
			const char *api,
			const char *verb,
			struct json_object *args,
			int flags,
			void (*callback)(void *closure, struct json_object *object, const char *error, const char * info, struct afb_req_x2 *req),
			void *closure);

#if WITH_AFB_CALL_SYNC
extern
int afb_calls_subcall_sync(
			struct afb_xreq *xreq,
			const char *api,
			const char *verb,
			struct json_object *args,
			int flags,
			struct json_object **object,
			char **error,
			char **info);
#endif

/******************************************************************************/
#if WITH_AFB_HOOK

extern
void afb_calls_hooked_call(
		struct afb_export *export,
		const char *api,
		const char *verb,
		struct json_object *args,
		void (*callback)(void*, struct json_object*, const char *error, const char *info, struct afb_api_x3*),
		void *closure);

#if WITH_AFB_CALL_SYNC
extern
int afb_calls_hooked_call_sync(
		struct afb_export *export,
		const char *api,
		const char *verb,
		struct json_object *args,
		struct json_object **object,
		char **error,
		char **info);
#endif

extern
void afb_calls_hooked_subcall(
			struct afb_xreq *xreq,
			const char *api,
			const char *verb,
			struct json_object *args,
			int flags,
			void (*callback)(void *closure, struct json_object *object, const char *error, const char * info, struct afb_req_x2 *req),
			void *closure);

#if WITH_AFB_CALL_SYNC
extern
int afb_calls_hooked_subcall_sync(
			struct afb_xreq *xreq,
			const char *api,
			const char *verb,
			struct json_object *args,
			int flags,
			struct json_object **object,
			char **error,
			char **info);
#endif

#endif /* WITH_AFB_HOOK */
/******************************************************************************/
#if WITH_LEGACY_CALLS
extern
void afb_calls_legacy_call_v3(
		struct afb_export *export,
		const char *api,
		const char *verb,
		struct json_object *args,
		void (*callback)(void*, int, struct json_object*, struct afb_api_x3 *),
		void *closure);

#if WITH_AFB_CALL_SYNC
extern
int afb_calls_legacy_call_sync(
		struct afb_export *export,
		const char *api,
		const char *verb,
		struct json_object *args,
		struct json_object **result);
#endif

/******************************************************************************/
#if WITH_AFB_HOOK

extern
void afb_calls_legacy_hooked_call_v3(
		struct afb_export *export,
		const char *api,
		const char *verb,
		struct json_object *args,
		void (*callback)(void*, int, struct json_object*, struct afb_api_x3 *),
		void *closure);

#if WITH_AFB_CALL_SYNC
extern
int afb_calls_legacy_hooked_call_sync(
		struct afb_export *export,
		const char *api,
		const char *verb,
		struct json_object *args,
		struct json_object **result);
#endif

#endif /* WITH_AFB_HOOK */
/******************************************************************************/

extern
void afb_calls_legacy_subcall_v1(
		struct afb_xreq *caller,
		const char *api,
		const char *verb,
		struct json_object *args,
		void (*callback)(void*, int, struct json_object*),
		void *closure);

extern
void afb_calls_legacy_subcall_v2(
		struct afb_xreq *caller,
		const char *api,
		const char *verb,
		struct json_object *args,
		void (*callback)(void*, int, struct json_object*, struct afb_req_x1),
		void *closure);

extern
void afb_calls_legacy_subcall_v3(
		struct afb_xreq *caller,
		const char *api,
		const char *verb,
		struct json_object *args,
		void (*callback)(void*, int, struct json_object*, struct afb_req_x2 *),
		void *closure);

#if WITH_AFB_CALL_SYNC
extern
int afb_calls_legacy_subcall_sync(
		struct afb_xreq *caller,
		const char *api,
		const char *verb,
		struct json_object *args,
		struct json_object **result);
#endif

/******************************************************************************/
#if WITH_AFB_HOOK

extern
void afb_calls_legacy_hooked_subcall_v1(
		struct afb_xreq *caller,
		const char *api,
		const char *verb,
		struct json_object *args,
		void (*callback)(void*, int, struct json_object*),
		void *closure);

extern
void afb_calls_legacy_hooked_subcall_v2(
		struct afb_xreq *caller,
		const char *api,
		const char *verb,
		struct json_object *args,
		void (*callback)(void*, int, struct json_object*, struct afb_req_x1),
		void *closure);

extern
void afb_calls_legacy_hooked_subcall_v3(
		struct afb_xreq *caller,
		const char *api,
		const char *verb,
		struct json_object *args,
		void (*callback)(void*, int, struct json_object*, struct afb_req_x2 *),
		void *closure);

#if WITH_AFB_CALL_SYNC
extern
int afb_calls_legacy_hooked_subcall_sync(
		struct afb_xreq *caller,
		const char *api,
		const char *verb,
		struct json_object *args,
		struct json_object **result);
#endif

#endif /* WITH_AFB_HOOK */
#endif /* WITH_LEGACY_CALLS */
/******************************************************************************/
