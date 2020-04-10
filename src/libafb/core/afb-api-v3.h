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
struct afb_api_v3;
struct afb_api_x3;
struct afb_auth;
struct afb_req_x2;
struct afb_verb_v3;
struct afb_binding_v3;
struct afb_xreq;
struct json_object;
struct afb_export;

extern struct afb_api_v3 *afb_api_v3_create(struct afb_apiset *declare_set,
		struct afb_apiset *call_set,
		const char *apiname,
		const char *info,
		int noconcurrency,
		int (*preinit)(void*, struct afb_api_x3 *),
		void *closure,
		int copy_info,
		struct afb_export* creator,
		const char* path);

extern struct afb_api_v3 *afb_api_v3_from_binding(
		const struct afb_binding_v3 *desc,
		struct afb_apiset *declare_set,
		struct afb_apiset * call_set);

extern int afb_api_v3_safe_preinit(
		struct afb_api_x3 *api,
		int (*preinit)(struct afb_api_x3 *));

extern int afb_api_v3_set_binding_fields(const struct afb_binding_v3 *desc, struct afb_api_x3 *api);

extern struct afb_api_v3 *afb_api_v3_addref(struct afb_api_v3 *api);
extern void afb_api_v3_unref(struct afb_api_v3 *api);

extern struct afb_export *afb_api_v3_export(struct afb_api_v3 *api);

extern void afb_api_v3_set_verbs_v3(
		struct afb_api_v3 *api,
		const struct afb_verb_v3 *verbs);

extern int afb_api_v3_add_verb(
		struct afb_api_v3 *api,
		const char *verb,
		const char *info,
		void (*callback)(struct afb_req_x2 *req),
		void *vcbdata,
		const struct afb_auth *auth,
		uint16_t session,
		int glob);

extern int afb_api_v3_del_verb(
		struct afb_api_v3 *api,
		const char *verb,
		void **vcbdata);

extern void afb_api_v3_process_call(struct afb_api_v3 *api, struct afb_xreq *xreq);
extern struct json_object *afb_api_v3_make_description_openAPIv3(struct afb_api_v3 *api, const char *apiname);
