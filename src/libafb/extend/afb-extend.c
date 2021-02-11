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

#include "libafb-config.h"

#if WITH_EXTENSION

#include <string.h>

#include <json-c/json.h>

#include "afb-extension.h"
#include "afb-extend.h"

#include "sys/verbose.h"
#include "sys/x-dynlib.h"
#include "utils/wrap-json.h"
#include "core/afb-v4.h"

#if WITH_DIRENT
#include "utils/path-search.h"
#endif

#define MANIFEST	"AfbExtensionManifest"
#define OPTIONS_V1	"AfbExtensionOptionsV1"
#define CONFIG_V1	"AfbExtensionConfigV1"
#define DECLARE_V1	"AfbExtensionDeclareV1"
#define SERVE_V1	"AfbExtensionServeV1"
#define HTTP_V1		"AfbExtensionHTTPV1"
#define EXIT_V1		"AfbExtensionExitV1"

/**
 * Record for an extension
 */
struct extension
{
	/** link to the next extension */
	struct extension *next;

	/** pointer to the manifest of the extension */
	struct afb_extension_manifest *manifest;

	/** handle of the library */
	x_dynlib_t handle;

	/** data of the extension */
	void *data;

	/** path of the loaded library */
	char path[];
};

/** head of the linked list of the loaded extensions */
static struct extension *extensions;

static int load_extension(const char *path, int failstops)
{
	struct afb_extension_manifest *manifest;
	struct extension *ext;
	x_dynlib_t handle;
	int rc;
	struct afb_v4_dynlib_info infov4;

	/* try to load */
	DEBUG("Trying extension %s", path);
	rc = x_dynlib_open(path, &handle, 1, 0);
	if (rc < 0) {
		if (failstops) {
			ERROR("Unloadable extension %s: %s", path, x_dynlib_error(&handle));
			return rc;
		}
		DEBUG("can't load extension %s", path);
		return 0;
	}

	/* search the symbol */
	rc = x_dynlib_symbol(&handle, MANIFEST, (void**)&manifest);
	if (rc < 0) {
		if (failstops) {
			ERROR("Not an extension %s: %s", path, x_dynlib_error(&handle));
		}
		else {
			DEBUG("Not an extension %s", path);
			rc = 0;
		}
	} else if (!manifest || manifest->magic != AFB_EXTENSION_MAGIC
			|| !manifest->name) {
		ERROR("Manifest error of extension %s", path);
		rc = X_EINVAL;
	} else if (manifest->version != 1) {
		ERROR("Unsupported version %d of extension %s: %s", manifest->version, manifest->name, path);
		rc = X_ENOTSUP;
	} else {
		afb_v4_connect_dynlib(&handle, &infov4, 0);
		if (infov4.root || infov4.desc || infov4.mainctl) {
			ERROR("CAUTION!!! Binding in extension must be compiled without global symbols!");
			ERROR("  ...  Please recompile extension %s (%s)", manifest->name, path);
			if (infov4.root)
				ERROR(" ... with AFB_BINDING_NO_ROOT defined (or option -D)");
			if (infov4.desc)
				ERROR(" ... without defining a main structure (afbBindingRoot or afbBindingV4)");
			if (infov4.mainctl)
				ERROR(" ... without defining an entry function (afbBindingEntry or afbBindingV4Entry)");
			rc = X_ENOTSUP;
		}
		else {
			ext = malloc(strlen(path) + 1 + sizeof *ext);
			if (!ext)
				rc = X_ENOMEM;
			else {
				NOTICE("Adding extension %s of %s", manifest->name, path);
				ext->next = extensions;
				ext->handle = handle;
				ext->manifest = manifest;
				ext->data = 0;
				strcpy(ext->path, path);
				extensions = ext;
				return 1;
			}
		}
	}
	x_dynlib_close(&handle);
	if (!failstops && rc < 0) {
		if (rc < 0)
			NOTICE("Ignoring extension %s", path);
		rc = 0;
	}
	return rc;
}

static void load_extension_cb(void *closure, struct json_object *value)
{
	int rc, *ret = closure;

	if (*ret >= 0 && json_object_is_type(value, json_type_string)) {
		rc = load_extension(json_object_get_string(value), 1);
		if (rc < *ret)
			*ret = rc;
	}
}

#if WITH_DIRENT
/**
 * callback of files
 */
static int try_extension(void *closure, struct path_search_item *item)
{
	static char extension[] = ".so";
	int rc, *ret = closure;

	/* only try files having ".so" extension */
	if (item->namelen < (short)(sizeof extension - 1)
	 || memcmp(&item->name[item->namelen - (short)(sizeof extension - 1)], extension, sizeof extension))
		return 0;

	/* try to get it as a binding */
	rc = load_extension(item->path, 0);
	if (rc >= 0)
		return 0; /* got it */

	/* report the error and tell to stop exploration of files */
	*ret = rc;
	return 1;
}

/**
 * function to filter out the directories that must not be entered
 */
static int filterdirs(void *closure, struct path_search_item *item)
{
	int result = item->name[0] != '.';
	if (result)
		INFO("Scanning dir=[%s] for extensions", item->path);
	return result;
}

static int load_extpath(const char *value)
{
	int rc;
	struct path_search *ps;

	rc = path_search_make_dirs(&ps, value);
	if (rc >= 0) {
		path_search_filter(ps, PATH_SEARCH_FILE|PATH_SEARCH_RECURSIVE|PATH_SEARCH_FLEXIBLE,
			try_extension, &rc, filterdirs);
		path_search_unref(ps);
	}
	return rc;
}

static void load_extpath_cb(void *closure, struct json_object *value)
{
	int *ret = closure;
	int rc;

	if (*ret >= 0 && json_object_is_type(value, json_type_string)) {
		rc = load_extpath(json_object_get_string(value));
		if (rc < *ret)
			*ret = rc;
	}
}

#endif

int afb_extend_load(struct json_object *config)
{
	struct extension *ext, *head;
	struct json_object *array;
	int rc;

	/* load extensions */
	rc = 1;
	if (json_object_object_get_ex(config, "extension", &array))
		wrap_json_optarray_for_all(array, load_extension_cb, &rc);
#if WITH_DIRENT
	/* search extensions */
	if (rc >= 0 && json_object_object_get_ex(config, "extpaths", &array))
		wrap_json_optarray_for_all(array, load_extpath_cb, &rc);
#endif

	/* revert list of extensions */
	head = 0;
	while ((ext = extensions)) {
		extensions = ext->next;
		ext->next = head;
		head = ext;
	}
	extensions = head;

	return rc;
}

int afb_extend_get_options(const struct argp_option ***options_array_result, const char ***names)
{
	int rc, n, s;
	struct extension *ext;
	const struct argp_option *options;
	const struct argp_option **oar;
	const char **enam;

	/* allocates enough for the result */
	for (n = 0, ext = extensions ; ext ; ext = ext->next, n++);
	*options_array_result = oar = malloc((unsigned)n * sizeof *oar);
	*names = enam = malloc((unsigned)n * sizeof *enam);
	if (!oar || !enam) {
		if (oar) {
			free(oar);
			*options_array_result = 0;
		}
		if (enam) {
			free(enam);
			*names = 0;
		}
		rc = X_ENOMEM;
	}
	else {
		/* initialize the results */
		n = 0;
		for (ext = extensions ; ext ; ext = ext->next) {
			s = x_dynlib_symbol(&ext->handle, OPTIONS_V1, (void**)&options);
			if (s >= 0) {
				oar[n] = options;
				enam[n] = ext->manifest->name;
				n++;
			}
		}
		rc = n;
	}
	return rc;
}

int afb_extend_config(struct json_object *config)
{
	int rc, s;
	struct extension *ext;
	int (*config_v1)(void **data, struct json_object *config);
	struct json_object *root, *obj;

	root = 0;
	json_object_object_get_ex(config, "@extconfig", &root);
	rc = 0;
	for (ext = extensions ; ext ; ext = ext->next) {
		s = x_dynlib_symbol(&ext->handle, CONFIG_V1, (void**)&config_v1);
		if (s >= 0) {
			obj = 0;
			json_object_object_get_ex(root, ext->manifest->name, &obj);
			s = config_v1(&ext->data, obj);
			if (s < 0)
				rc = s;
		}
	}
	return rc;
}

int afb_extend_declare(struct afb_apiset *declare_set, struct afb_apiset *call_set)
{
	int rc, s;
	struct extension *ext;
	int (*declare_v1)(void *data, struct afb_apiset *declare_set, struct afb_apiset *call_set);

	rc = 0;
	for (ext = extensions ; ext ; ext = ext->next) {
		s = x_dynlib_symbol(&ext->handle, DECLARE_V1, (void**)&declare_v1);
		if (s >= 0) {
			s = declare_v1(ext->data, declare_set, call_set);
			if (s < 0)
				rc = s;
		}
	}
	return rc;
}

int afb_extend_http(struct afb_hsrv *hsrv)
{
#if WITH_LIBMICROHTTPD
	int rc, s;
	struct extension *ext;
	int (*http_v1)(void *data, struct afb_hsrv *hsrv);

	rc = 0;
	for (ext = extensions ; ext ; ext = ext->next) {
		s = x_dynlib_symbol(&ext->handle, HTTP_V1, (void**)&http_v1);
		if (s >= 0) {
			s = http_v1(ext->data, hsrv);
			if (s < 0)
				rc = s;
		}
	}
	return rc;
#else
	return X_ENOTSUP;
#endif
}

int afb_extend_serve(struct afb_apiset *call_set)
{
	int rc, s;
	struct extension *ext;
	int (*serve_v1)(void *data, struct afb_apiset *call_set);

	rc = 0;
	for (ext = extensions ; ext ; ext = ext->next) {
		s = x_dynlib_symbol(&ext->handle, SERVE_V1, (void**)&serve_v1);
		if (s >= 0) {
			s = serve_v1(ext->data, call_set);
			if (s < 0)
				rc = s;
		}
	}
	return rc;
}

int afb_extend_exit(struct afb_apiset *declare_set)
{
	int rc, s;
	struct extension *ext;
	int (*exit_v1)(void *data, struct afb_apiset *declare_set);

	rc = 0;
	while ((ext = extensions)) {
		extensions = ext->next;
		s = x_dynlib_symbol(&ext->handle, EXIT_V1, (void**)&exit_v1);
		if (s >= 0) {
			s = exit_v1(ext->data, declare_set);
			if (s < 0)
				rc = s;
		}
		x_dynlib_close(&ext->handle);
		free(ext);
	}
	return rc;
}

#endif
