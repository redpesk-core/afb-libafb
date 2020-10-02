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
#include "core/afb-api-common.h"
#include "core/afb-api-v4.h"
#include "core/afb-apiset.h"
#include "sys/x-realpath.h"
#include "sys/verbose.h"

/*********************************************************
 * names of symbols
 ********************************************************/

/**
 * Name of the structure describing statically the binding: "afbBindingV4"
 */
static const char afb_api_so_v4_desc[] = "afbBindingV4";

/**
 * Name of the pointer for the root api: "afbBindingV4root"
 */
static const char afb_api_so_v4_root[] = "afbBindingV4root";

/**
 * Name of the entry function for dynamic bindings: "afbBindingV4entry"
 */
static const char afb_api_so_v4_entry[] = "afbBindingV4entry";

/**
 * Name of the structure to init for callbacks: "afbBindingV4itf"
 */
static const char afb_api_so_v4_itf[] = "afbBindingV4itf";

/**
 * Name of the pointer to init to structure of callbacks: "afbBindingV4itfptr"
 */
static const char afb_api_so_v4_itfptr[] = "afbBindingV4itfptr";

/**
 * tiny structure for handling arguments of callbacks
 */
struct args
{
	/** root api of the loaded binding */
	struct afb_api_v4 **root;

	/** descriptor of the binding for static api */
	const struct afb_binding_v4 *desc;

	/** main control routine */
	int (*mainctl)(const struct afb_api_v4 *, afb_ctlid_t, afb_ctlarg_t);
};

/**
 * Initialisation of the binding when a description
 * of the root api is given: afbBindingV4 exists.
 */
static int init_for_desc(struct afb_api_v4 *api, void *closure)
{
	const struct args *a = closure;
	union afb_ctlarg ctlarg;
	int rc;

	/* set the root of the binding */
	*a->root = api;

	/* record the description */
	rc = afb_api_v4_set_binding_fields(api, a->desc);
	if (rc >= 0 && a->mainctl) {
		/* call the pre init routine safely */
		ctlarg.pre_init.closure = 0;
		ctlarg.pre_init.path = afb_api_v4_path(api);
		rc = afb_api_v4_safe_ctlproc(api, a->mainctl, afb_ctlid_Pre_Init, &ctlarg);
	}
	/* seal after init allows the pre init to add things */
	afb_api_v4_seal(api);
	return rc;
}

/**
 * Initialisation of the binding when no description
 * of the root api is given but only a function entry:
 * only afbBindingV4entry exists.
 */
static int init_for_root(struct afb_api_v4 *api, void *closure)
{
	const struct args *a = closure;
	union afb_ctlarg ctlarg;

	/* set the root of the binding */
	*a->root = api;
	/* seal after init */
	afb_api_v4_seal(api);
	/* call the root entry routine safely */
	ctlarg.root_entry.path = afb_api_v4_path(api);
	return afb_api_v4_safe_ctlproc(api, a->mainctl, afb_ctlid_Root_Entry, &ctlarg);
}

/**
 * Inspect the loaded shared object to check if it is a binding V4
 * If yes try to load and pre initiialize it.
 *
 * @param path    path of the loaded library
 * @param dynlib  handle of the dynamic library
 * @param declare_set the apiset where the binding APIS are to be declared
 * @param call_set the apiset to use when invoking other apis
 *
 * @return 0 if not a binding v4, 1 if valid binding V4 correctly pre-initialized,
 *         a negative number if invalid binding V4 or error when initializing.
 */
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

	/* retrieves interfaces */
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

	/* retrieves the root api */
	x_dynlib_symbol(dynlib, afb_api_so_v4_root, (void**)&a.root);
	if (!a.root) {
		ERROR("binding [%s] incomplete symbol set: %s is missing",
			path, afb_api_so_v4_root);
		rc = X_EINVAL;
		goto error;
	}

	if (a.desc) {
		/* case where a main API is described */
		/* check validity */
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
		/* get the main routine */
		if (!a.mainctl)
			a.mainctl = a.desc->mainctl;
		else if (a.desc->mainctl) {
			ERROR("binding [%s] clash: you can't define %s and %s.preinit, choose only one",
				path, afb_api_so_v4_entry, afb_api_so_v4_desc);
			rc = X_EINVAL;
			goto error;
		}
	} else {
		/* check validity of the root routine */
		if (!a.mainctl) {
			ERROR("binding [%s] incomplete symbol set: %s is missing",
				path, afb_api_so_v4_entry);
			rc = X_EINVAL;
			goto error;
		}
	}

	/* extract the real path of the binding and start the api */
	realpath(path, rpath);
	if (a.desc)
		rc = afb_api_v4_create(
			&api, declare_set, call_set,
			a.desc->api, Afb_String_Const,
			a.desc->info, Afb_String_Const,
			a.desc->noconcurrency,
			init_for_desc, &a,
			rpath, Afb_String_Copy);
	else
		rc = afb_api_v4_create(
			&api, declare_set, call_set,
			NULL, Afb_String_Const,
			NULL, Afb_String_Const,
			0,
			init_for_root, &a,
			rpath, Afb_String_Copy);

	if (rc >= 0)
		return 1;

	ERROR("binding [%s] initialisation failed", path);

error:
	return rc;
}

#endif
