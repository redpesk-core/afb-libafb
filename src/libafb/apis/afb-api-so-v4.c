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
 *  Foundation and appearing in the file LICENSE.GPLv4 included in the packaging
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

#include "core/afb-v4.h"

#include "sys/x-dynlib.h"
#include "core/afb-apiname.h"
#include "core/afb-string-mode.h"
#include "apis/afb-api-so-v4.h"
#include "core/afb-api-v4.h"
#include "core/afb-apiset.h"
#include "sys/x-realpath.h"
#include "sys/verbose.h"

/*
 * names of symbols
 */
static const char afb_api_so_v4_desc[] = "afbBindingV4";
static const char afb_api_so_v4_root[] = "afbBindingV4root";
static const char afb_api_so_v4_entry[] = "afbBindingV4entry";
static const char afb_api_so_v4_itf[] = "afbBindingV4itf";
static const char afb_api_so_v4_itfptr[] = "afbBindingV4itfptr";

struct args
{
	struct afb_api_v4 **root;
	const struct afb_binding_v4 *desc;
	int (*mainctl)(struct afb_api_v4 *, afb_ctlid_t, afb_ctlarg_t);
};

static int init_for_desc(struct afb_api_v4 *api, void *closure)
{
	const struct args *a = closure;
	union afb_ctlarg ctlarg;
	int rc;

	*a->root = api;
	rc = afb_api_v4_set_binding_fields(api, a->desc);
	if (rc >= 0) {
		ctlarg.pre_init.closure = 0;
		rc = afb_api_v4_safe_ctlproc(api, a->mainctl, afb_ctlid_Pre_Init, &ctlarg);
	}
	afb_api_v4_seal(api);
	return rc;
}

static int init_for_root(struct afb_api_v4 *api, void *closure)
{
	const struct args *a = closure;

	*a->root = api;
	afb_api_v4_seal(api);
	return afb_api_v4_safe_ctlproc(api, a->mainctl, afb_ctlid_Root_Entry, NULL);
}

int afb_api_so_v4_add(const char *path, x_dynlib_t *dynlib, struct afb_apiset *declare_set, struct afb_apiset * call_set)
{
	int rc;
	struct args a;
	struct afb_api_v4 *api;
	char rpath[PATH_MAX];
	struct afb_binding_x4_itf *itf;
	const struct afb_binding_x4_itf **itfptr;

	/* retrieves important exported symbols */
	x_dynlib_symbol(dynlib, afb_api_so_v4_desc, (void**)&a.desc);
	x_dynlib_symbol(dynlib, afb_api_so_v4_entry, (void**)&a.mainctl);
	if (!a.desc && !a.mainctl)
		return 0;

	INFO("binding [%s] looks like an AFB binding V4", path);

	/* basic checks */
	x_dynlib_symbol(dynlib, afb_api_so_v4_itf, (void**)&itf);
	x_dynlib_symbol(dynlib, afb_api_so_v4_itfptr, (void**)&itfptr);
	if (itf) {
		*itf = afb_v4_itf;
		if (itfptr)
			*itfptr = itf;
	}
	else if (itfptr) {
		*itfptr = &afb_v4_itf;
	}
	else {
		ERROR("binding [%s] incomplete symbol set: %s or %s is missing",
			path, afb_api_so_v4_itf, afb_api_so_v4_itfptr);
		rc = X_EINVAL;
		goto error;
	}
	x_dynlib_symbol(dynlib, afb_api_so_v4_root, (void**)&a.root);
	if (!a.root) {
		ERROR("binding [%s] incomplete symbol set: %s is missing",
			path, afb_api_so_v4_root);
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
		if (!a.mainctl)
			a.mainctl = a.desc->mainctl;
		else if (a.desc->mainctl) {
			ERROR("binding [%s] clash: you can't define %s and %s.preinit, choose only one",
				path, afb_api_so_v4_entry, afb_api_so_v4_desc);
			rc = X_EINVAL;
			goto error;
		}

		realpath(path, rpath);
		rc = afb_api_v4_create(
			&api, declare_set, call_set,
			a.desc->api, Afb_String_Const,
			a.desc->info, Afb_String_Const,
			a.desc->noconcurrency,
			init_for_desc, &a,
			rpath, Afb_String_Copy);
	} else {
		if (!a.mainctl) {
			ERROR("binding [%s] incomplete symbol set: %s is missing",
				path, afb_api_so_v4_entry);
			rc = X_EINVAL;
			goto error;
		}

		realpath(path, rpath);
		rc = afb_api_v4_create(
			&api, declare_set, call_set,
			NULL, Afb_String_Const,
			NULL, Afb_String_Const,
			0,
			init_for_root, &a,
			rpath, Afb_String_Copy);
	}
	if (rc >= 0)
		return 1;

	ERROR("binding [%s] initialisation failed", path);

error:
	return rc;
}

#endif
