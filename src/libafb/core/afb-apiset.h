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

struct afb_apiset;
struct afb_req_common;
struct json_object;

struct afb_api_itf
{
	void (*process)(void *closure, struct afb_req_common *req);
	int (*service_start)(void *closure);
#if WITH_AFB_HOOK
	void (*update_hooks)(void *closure);
#endif
	int (*get_logmask)(void *closure);
	void (*set_logmask)(void *closure, int level);
	void (*describe)(void *closure, void (*describecb)(void *, struct json_object *), void *clocb);
	void (*unref)(void *closure);
};

struct afb_api_item
{
	void *closure;
	struct afb_api_itf *itf;
	const void *group;
};

extern struct afb_apiset *afb_apiset_create(const char *name, int timeout);
extern struct afb_apiset *afb_apiset_create_subset_last(struct afb_apiset *set, const char *name, int timeout);
extern struct afb_apiset *afb_apiset_create_subset_first(struct afb_apiset *set, const char *name, int timeout);
extern struct afb_apiset *afb_apiset_addref(struct afb_apiset *set);
extern void afb_apiset_unref(struct afb_apiset *set);

extern const char *afb_apiset_name(struct afb_apiset *set);

extern int afb_apiset_timeout_get(struct afb_apiset *set);
extern void afb_apiset_timeout_set(struct afb_apiset *set, int to);

extern void afb_apiset_onlack_set(
		struct afb_apiset *set,
		int (*callback)(void *closure, struct afb_apiset *set, const char *name),
		void *closure,
		void (*cleanup)(void*closure));

extern int afb_apiset_subset_set(struct afb_apiset *set, struct afb_apiset *subset);
extern struct afb_apiset *afb_apiset_subset_get(struct afb_apiset *set);

extern int afb_apiset_add(struct afb_apiset *set, const char *name, struct afb_api_item api);
extern int afb_apiset_del(struct afb_apiset *set, const char *name);

extern int afb_apiset_add_alias(struct afb_apiset *set, const char *name, const char *alias);
extern int afb_apiset_is_alias(struct afb_apiset *set, const char *name);
extern const char *afb_apiset_unalias(struct afb_apiset *set, const char *name);

extern int afb_apiset_get_api(struct afb_apiset *set, const char *name, int rec, int started, const struct afb_api_item **api);

extern int afb_apiset_start_service(struct afb_apiset *set, const char *name);
extern int afb_apiset_start_all_services(struct afb_apiset *set);

#if WITH_AFB_HOOK
extern void afb_apiset_update_hooks(struct afb_apiset *set, const char *name);
#endif
extern void afb_apiset_set_logmask(struct afb_apiset *set, const char *name, int mask);
extern int afb_apiset_get_logmask(struct afb_apiset *set, const char *name);

extern void afb_apiset_describe(struct afb_apiset *set, const char *name, void (*describecb)(void *, struct json_object *), void *closure);

extern const char **afb_apiset_get_names(struct afb_apiset *set, int rec, int type);
extern void afb_apiset_enum(
			struct afb_apiset *set,
			int rec,
			void (*callback)(void *closure, struct afb_apiset *set, const char *name, const char *aliasto),
			void *closure);

extern int afb_apiset_require(struct afb_apiset *set, const char *name, const char *required);
extern int afb_apiset_require_class(struct afb_apiset *set, const char *apiname, const char *classname);
extern int afb_apiset_provide_class(struct afb_apiset *set, const char *apiname, const char *classname);
extern int afb_apiset_class_start(const char *classname);
