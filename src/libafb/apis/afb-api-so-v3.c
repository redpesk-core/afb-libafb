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

#include "libafb-config.h"

#if WITH_DYNAMIC_BINDING

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>

#include <json-c/json.h>
#include <afb/afb-binding-v3.h>

#include "sys/x-dynlib.h"
#include "core/afb-apiname.h"
#include "apis/afb-api-so-v3.h"
#include "core/afb-api-v3.h"
#include "core/afb-apiset.h"
#include "core/afb-export.h"
#include "sys/verbose.h"

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

static int init(void *closure, struct afb_api_x3 *api)
{
	const struct args *a = closure;
	int rc = 0;

	*a->root = api;
	if (a->desc) {
		api->userdata = a->desc->userdata;
		rc = afb_api_v3_set_binding_fields(a->desc, api);
	}

	if (rc >= 0 && a->entry)
		rc = afb_api_v3_safe_preinit(api, a->entry);

	if (rc >= 0)
		afb_api_x3_seal(api);

	return rc;
}

int afb_api_so_v3_add(const char *path, x_dynlib_t *dynlib, struct afb_apiset *declare_set, struct afb_apiset * call_set)
{
	int rc;
	struct args a;
	struct afb_api_v3 *api;
	struct afb_export *export;

	/* retrieves important exported symbols */
	x_dynlib_symbol(dynlib, afb_api_so_v3_desc, (void**)&a.desc);
	x_dynlib_symbol(dynlib, afb_api_so_v3_entry, (void**)&a.entry);
	if (!a.desc && !a.entry)
		return 0;

	INFO("binding [%s] looks like an AFB binding V3", path);

	/* basic checks */
	x_dynlib_symbol(dynlib, afb_api_so_v3_root, (void**)&a.root);
	if (!a.root) {
		ERROR("binding [%s] incomplete symbol set: %s is missing",
			path, afb_api_so_v3_root);
		rc = X_EINVAL;
		goto error;
	}
	if (a.desc) {
		if (a.desc->api == NULL || *a.desc->api == 0) {
			ERROR("binding [%s] bad api name...", path);
			rc = X_EINVAL;
			goto error;
		}
		if (!afb_apiname_is_valid(a.desc->api)) {
			ERROR("binding [%s] invalid api name...", path);
			rc = X_EINVAL;
			goto error;
		}
		if (!a.entry)
			a.entry = a.desc->preinit;
		else if (a.desc->preinit) {
			ERROR("binding [%s] clash: you can't define %s and %s.preinit, choose only one",
				path, afb_api_so_v3_entry, afb_api_so_v3_desc);
			rc = X_EINVAL;
			goto error;
		}

		api = afb_api_v3_create(declare_set, call_set, a.desc->api, a.desc->info, a.desc->noconcurrency, init, &a, 0, NULL, path);
		if (api)
			return 1;
		rc = X_ENOMEM;
	} else {
		if (!a.entry) {
			ERROR("binding [%s] incomplete symbol set: %s is missing",
				path, afb_api_so_v3_entry);
			rc = X_EINVAL;
			goto error;
		}

		export = afb_export_create_none_for_path(declare_set, call_set, path, init, &a);
		if (export) {
			/*
			 *  No call is done to afb_export_unref(export) because:
			 *   - legacy applications may use the root API emitting messages
			 *   - it allows writting applications like bindings without API
			 *  But this has the sad effect to introduce a kind of leak.
			 *  To avoid this, if necessary further developement should list bindings
			 *  and their data.
			 */
			return 1;
		}
		rc = X_ENOMEM;
	}

	ERROR("binding [%s] initialisation failed", path);

error:
	return rc;
}

#endif
