/*
 * Copyright (C) 2015-2022 IoT.bzh Company
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

#include "libafb-config.h"

#if WITH_DYNAMIC_BINDING

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>

#include <json-c/json.h>
#include <afb/afb-binding-v3.h>
#include <rp-utils/rp-verbose.h>

#include "sys/x-dynlib.h"
#include "sys/x-errno.h"
#include "core/afb-apiname.h"
#include "core/afb-string-mode.h"
#include "apis/afb-api-so-v3.h"
#include "core/afb-api-v3.h"
#include "core/afb-apiset.h"
#include "sys/x-realpath.h"

/*
 * names of symbols
 */
static const char afb_api_so_v3_desc[] = "afbBindingV3";
static const char afb_api_so_v3_root[] = "afbBindingV3root";
static const char afb_api_so_v3_entry[] = "afbBindingV3entry";

struct args
{
	struct afb_api_x3 **root;
	const struct afb_binding_v3 *desc;
	int (*entry)(struct afb_api_x3 *);
};

static int init(void *closure, struct afb_api_v3 *api)
{
	const struct args *a = closure;
	int rc = 0;

	*a->root = afb_api_v3_get_api_x3(api);
	if (a->desc) {
		rc = afb_api_v3_set_binding_fields(api, a->desc);
	}

	if (rc >= 0 && a->entry)
		rc = afb_api_v3_safe_preinit_x3(api, a->entry);

	if (rc >= 0)
		afb_api_v3_seal(api);

	return rc;
}

int afb_api_so_v3_add(const char *path, x_dynlib_t *dynlib, struct afb_apiset *declare_set, struct afb_apiset * call_set)
{
	int rc;
	struct args a;
	struct afb_api_v3 *api;
	char rpath[PATH_MAX];

	/* retrieves important exported symbols */
	x_dynlib_symbol(dynlib, afb_api_so_v3_desc, (void**)&a.desc);
	x_dynlib_symbol(dynlib, afb_api_so_v3_entry, (void**)&a.entry);
	if (!a.desc && !a.entry)
		return 0;

	RP_INFO("binding [%s] looks like an AFB binding V3", path);

	/* basic checks */
	x_dynlib_symbol(dynlib, afb_api_so_v3_root, (void**)&a.root);
	if (!a.root) {
		RP_ERROR("binding [%s] incomplete symbol set: %s is missing",
			path, afb_api_so_v3_root);
		rc = X_EINVAL;
		goto error;
	}
	if (a.desc) {
		if (a.desc->api == NULL || *a.desc->api == 0) {
			RP_ERROR("binding [%s] bad api name...", path);
			rc = X_EINVAL;
			goto error;
		}
		if (!afb_apiname_is_valid(a.desc->api)) {
			RP_ERROR("binding [%s] invalid api name...", path);
			rc = X_EINVAL;
			goto error;
		}
		if (!a.entry)
			a.entry = a.desc->preinit;
		else if (a.desc->preinit && a.desc->preinit != a.entry) {
			RP_ERROR("binding [%s] clash: you can't define %s and %s.preinit, choose only one",
				path, afb_api_so_v3_entry, afb_api_so_v3_desc);
			rc = X_EINVAL;
			goto error;
		}

		realpath(path, rpath);
		rc = afb_api_v3_create(
			&api, declare_set, call_set,
			a.desc->api, Afb_String_Const,
			a.desc->info, Afb_String_Const,
			a.desc->noconcurrency,
			init, &a,
			rpath, Afb_String_Copy);
	} else {
		if (!a.entry) {
			RP_ERROR("binding [%s] incomplete symbol set: %s is missing",
				path, afb_api_so_v3_entry);
			rc = X_EINVAL;
			goto error;
		}

		realpath(path, rpath);
		rc = afb_api_v3_create(
			&api, declare_set, call_set,
			NULL, Afb_String_Const,
			NULL, Afb_String_Const,
			0,
			init, &a,
			rpath, Afb_String_Copy);
	}
	if (rc >= 0)
		return 1;

	RP_ERROR("binding [%s] initialisation failed", path);

error:
	return rc;
}

int afb_api_so_v3_add_config(const char *path, x_dynlib_t *dynlib, struct afb_apiset *declare_set, struct afb_apiset * call_set, struct json_object *config)
{
	return afb_api_so_v3_add(path, dynlib, declare_set, call_set);
}

#endif
