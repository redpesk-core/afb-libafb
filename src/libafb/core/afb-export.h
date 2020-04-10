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
struct afb_apiset;
struct afb_context;
struct afb_xreq;

struct afb_api_v3;
struct afb_api_x3;
struct afb_event_x2;

extern void afb_export_set_config(struct json_object *config);

extern struct afb_export *afb_export_create_none_for_path(
				struct afb_apiset *declare_set,
				struct afb_apiset *call_set,
				const char *path,
				int (*creator)(void*, struct afb_api_x3*),
				void *closure);

extern struct afb_export *afb_export_create_v3(struct afb_apiset *declare_set,
				struct afb_apiset *call_set,
				const char *apiname,
				struct afb_api_v3 *api,
				struct afb_export* creator,
				const char* path);

extern struct afb_export *afb_export_addref(struct afb_export *export);
extern void afb_export_unref(struct afb_export *export);

extern int afb_export_declare(struct afb_export *export, int noconcurrency);
extern void afb_export_undeclare(struct afb_export *export);

extern const char *afb_export_apiname(const struct afb_export *export);
extern int afb_export_add_alias(struct afb_export *export, const char *apiname, const char *aliasname);
extern int afb_export_rename(struct afb_export *export, const char *apiname);

extern int afb_export_unshare_session(struct afb_export *export);

extern int afb_export_preinit_x3(
				struct afb_export *export,
				int (*preinit)(void *,struct afb_api_x3*),
				void *closure);

extern int afb_export_handle_events_v3(
				struct afb_export *export,
				void (*on_event)(struct afb_api_x3 *api, const char *event, struct json_object *object));


extern int afb_export_handle_init_v3(
				struct afb_export *export,
				int (*oninit)(struct afb_api_x3 *api));

extern int afb_export_start(struct afb_export *export);

extern int afb_export_logmask_get(const struct afb_export *export);
extern void afb_export_logmask_set(struct afb_export *export, int mask);

extern void *afb_export_userdata_get(const struct afb_export *export);
extern void afb_export_userdata_set(struct afb_export *export, void *data);

extern int afb_export_event_handler_add(
			struct afb_export *export,
			const char *pattern,
			void (*callback)(void *, const char*, struct json_object*, struct afb_api_x3*),
			void *closure);

extern int afb_export_event_handler_del(
			struct afb_export *export,
			const char *pattern,
			void **closure);

extern int afb_export_subscribe(struct afb_export *export, struct afb_event_x2 *event);
extern int afb_export_unsubscribe(struct afb_export *export, struct afb_event_x2 *event);
extern void afb_export_process_xreq(struct afb_export *export, struct afb_xreq *xreq);
extern void afb_export_context_init(struct afb_export *export, struct afb_context *context);
extern struct afb_export *afb_export_from_api_x3(struct afb_api_x3 *api);
extern struct afb_api_x3 *afb_export_to_api_x3(struct afb_export *export);

#if WITH_AFB_HOOK
extern void afb_export_update_hooks(struct afb_export *export);
#endif

