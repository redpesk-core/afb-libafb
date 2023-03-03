/*
 * Copyright (C) 2015-2023 IoT.bzh Company
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

#include "../libafb-config.h"

#if WITH_DYNAMIC_BINDING

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>

#include <json-c/json.h>
#include <rp-utils/rp-verbose.h>

#include "core/afb-v4-itf.h"

#include "sys/x-dynlib.h"
#include "sys/x-errno.h"
#include "core/afb-apiname.h"
#include "core/afb-string-mode.h"
#include "apis/afb-api-so-v4.h"
#include "core/afb-api-common.h"
#include "core/afb-api-v4.h"
#include "core/afb-apiset.h"
#include "sys/x-realpath.h"

/**
 * Temporary structure holding important data used in initialisation callbacks
 */
struct iniv4
{
	/** uid from the config, if any, or NULL */
	const char *uid;

	/** configuration, if any, or NULL */
	struct json_object *config;

	/** v4 dynlib data */
	struct afb_v4_dynlib_info dlv4;
};

/**
 * Initialisation of the binding when a description
 * of the root api is given: afbBindingV4 exists.
 */
static int init_for_desc(struct afb_api_v4 *api, void *closure)
{
	const struct iniv4 *iniv4 = closure;
	union afb_ctlarg ctlarg;
	int rc;

	/* set the root of the binding */
	*iniv4->dlv4.root = api;

	/* record the description */
	afb_api_v4_set_userdata(api, iniv4->dlv4.desc->userdata);
	afb_api_v4_set_mainctl(api, iniv4->dlv4.mainctl);
	afb_api_v4_set_verbs(api, iniv4->dlv4.desc->verbs);
	rc = 0;
	if (iniv4->dlv4.desc->provide_class)
		rc =  afb_api_v4_class_provide(api, iniv4->dlv4.desc->provide_class);
	if (!rc && iniv4->dlv4.desc->require_class)
		rc =  afb_api_v4_class_require(api, iniv4->dlv4.desc->require_class);
	if (!rc && iniv4->dlv4.desc->require_api)
		rc =  afb_api_v4_require_api(api, iniv4->dlv4.desc->require_api, 0);
	if (rc >= 0 && iniv4->dlv4.mainctl) {
		/* call the pre init routine safely */
		memset(&ctlarg, 0, sizeof ctlarg);
		ctlarg.pre_init.path = afb_api_v4_path(api);
		ctlarg.pre_init.uid = iniv4->uid;
		ctlarg.pre_init.config = iniv4->config;
		rc = afb_api_v4_safe_ctlproc(api, iniv4->dlv4.mainctl, afb_ctlid_Pre_Init, &ctlarg);
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
	const struct iniv4 *iniv4 = closure;
	union afb_ctlarg ctlarg;

	/* set the root of the binding */
	*iniv4->dlv4.root = api;

	/* seal after init */
	afb_api_v4_seal(api);

	/* call the root entry routine safely */
	memset(&ctlarg, 0, sizeof ctlarg);
	ctlarg.root_entry.path = afb_api_v4_path(api);
	ctlarg.root_entry.uid = iniv4->uid;
	ctlarg.root_entry.config = iniv4->config;

	return afb_api_v4_safe_ctlproc(api, iniv4->dlv4.mainctl, afb_ctlid_Root_Entry, &ctlarg);
}

/* add the binding of dynlib for path */
int afb_api_so_v4_add(const char *path, x_dynlib_t *dynlib, struct afb_apiset *declare_set, struct afb_apiset * call_set)
{
	return afb_api_so_v4_add_config(path, dynlib, declare_set, call_set, NULL);
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
int afb_api_so_v4_add_config(const char *path, x_dynlib_t *dynlib, struct afb_apiset *declare_set, struct afb_apiset * call_set, struct json_object *config)
{
	int rc;
	struct iniv4 iniv4;
	struct afb_api_v4 *api;
	char rpath[PATH_MAX];
	struct json_object *obj;

	/* retrieves important exported symbols */
	afb_v4_connect_dynlib(dynlib, &iniv4.dlv4, 0);

	/* check if V4 compatible */
	if (!iniv4.dlv4.desc && !iniv4.dlv4.mainctl)
		return 0;

	RP_INFO("binding [%s] looks like an AFB binding V4", path);

	/* check interface */
	if (iniv4.dlv4.itfrev == 0) {
		RP_ERROR("binding [%s] incomplete symbol set: interface is missing", path);
		rc = X_EINVAL;
		goto error;
	}

	/* check root api */
	if (!iniv4.dlv4.root) {
		RP_ERROR("binding [%s] incomplete symbol set: root is missing", path);
		rc = X_EINVAL;
		goto error;
	}

	/* check the interface revision */
	if (iniv4.dlv4.itfrev > AFB_BINDING_X4R1_ITF_CURRENT_REVISION) {
		RP_ERROR("binding [%s] interface v4 revision %d isn't supported (greater than %d)",
				path, (int)iniv4.dlv4.itfrev, AFB_BINDING_X4R1_ITF_CURRENT_REVISION);
		RP_ERROR("HINT! for supporting older version, try: #define AFB_BINDING_X4R1_ITF_REVISION %d",
				AFB_BINDING_X4R1_ITF_CURRENT_REVISION);
		rc = X_EINVAL;
		goto error;
	}

	if (iniv4.dlv4.itfrev < AFB_BINDING_X4R1_ITF_CURRENT_REVISION)
		RP_NOTICE("binding [%s] interface v4 revision %d lesser than current %d",
				path, iniv4.dlv4.itfrev, AFB_BINDING_X4R1_ITF_CURRENT_REVISION);

	if (iniv4.dlv4.desc) {
		/* case where a main API is described */
		/* check validity */
		if (iniv4.dlv4.desc->api == NULL || *iniv4.dlv4.desc->api == 0) {
			RP_ERROR("binding [%s] bad api name...", path);
			rc = X_EINVAL;
			goto error;
		}
		if (!afb_apiname_is_valid(iniv4.dlv4.desc->api)) {
			RP_ERROR("binding [%s] invalid api name...", path);
			rc = X_EINVAL;
			goto error;
		}
		/* get the main routine */
		if (!iniv4.dlv4.mainctl)
			iniv4.dlv4.mainctl = iniv4.dlv4.desc->mainctl;
		else if (iniv4.dlv4.desc->mainctl
		      && iniv4.dlv4.desc->mainctl != iniv4.dlv4.mainctl) {
			RP_ERROR("binding [%s] clash of entries", path);
			rc = X_EINVAL;
			goto error;
		}
	} else {
		/* check validity of the root routine */
		if (!iniv4.dlv4.mainctl) {
			RP_ERROR("binding [%s] incomplete symbol set: root entry is missing", path);
			rc = X_EINVAL;
			goto error;
		}
	}

	/* interpret configuration */
	iniv4.uid = NULL;
	iniv4.config = config;
	if (json_object_object_get_ex(config, "uid", &obj))
		iniv4.uid = json_object_get_string(obj);
	if (json_object_object_get_ex(config, "config", &obj))
		iniv4.config = obj;

	/* extract the real path of the binding and start the api */
	realpath(path, rpath);
	if (iniv4.dlv4.desc)
		rc = afb_api_v4_create(
			&api, declare_set, call_set,
			iniv4.dlv4.desc->api, Afb_String_Const,
			iniv4.dlv4.desc->info, Afb_String_Const,
			iniv4.dlv4.desc->noconcurrency,
			init_for_desc, &iniv4,
			rpath, Afb_String_Copy);
	else
		rc = afb_api_v4_create(
			&api, declare_set, call_set,
			NULL, Afb_String_Const,
			NULL, Afb_String_Const,
			0,
			init_for_root, &iniv4,
			rpath, Afb_String_Copy);

	if (rc >= 0)
		return 1;

	RP_ERROR("binding [%s] initialisation failed", path);

error:
	return rc;
}

#endif
