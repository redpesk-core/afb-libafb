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

#if WITH_EXTENSION

#include <string.h>

#include <json-c/json.h>
#include <rp-utils/rp-verbose.h>
#include <rp-utils/rp-jsonc.h>

#include "afb-extension.h"
#include "afb-extend.h"

#include "sys/x-dynlib.h"
#include "sys/x-errno.h"
#include "core/afb-v4-itf.h"

#if WITH_DIRENT
#include "utils/path-search.h"
#endif

#define MANIFEST	"AfbExtensionManifest"
#define OPTIONS_V1	"AfbExtensionOptionsV1"
#define GETOPTIONS_V1	"AfbExtensionGetOptionsV1"
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

	/** uid of the extension */
	const char *uid;

	/** config of the extension */
	struct json_object *config;

	/** data of the extension */
	void *data;

	/** path of the loaded library */
	char path[];
};

/** head of the linked list of the loaded extensions */
static struct extension *extensions;

static struct extension *search_extension_uid(const char *uid)
{
	struct extension *ext = extensions;
	while(ext != NULL && strcmp(ext->uid, uid))
		ext = ext->next;
	return ext;
}

static int load_extension(const char *path, int failstops, const char *uid, struct json_object *config)
{
	struct afb_extension_manifest *manifest;
	struct extension *ext, *it;
	x_dynlib_t handle;
	size_t pathlen, strsz;
	int rc;
	struct afb_v4_dynlib_info infov4;
	const char *name;

	/* try to load */
	RP_DEBUG("Trying extension %s", path);
	rc = x_dynlib_open(path, &handle, 1, 0);
	if (rc < 0) {
		if (failstops) {
			RP_ERROR("Unloadable extension %s: %s", path, x_dynlib_error(&handle));
			return rc;
		}
		RP_DEBUG("can't load extension %s", path);
		return 0;
	}

	/* search the symbol */
	rc = x_dynlib_symbol(&handle, MANIFEST, (void**)&manifest);
	if (rc < 0) {
		if (failstops) {
			RP_ERROR("Not an extension %s: %s", path, x_dynlib_error(&handle));
		}
		else {
			RP_DEBUG("Not an extension %s", path);
			rc = 0;
		}
	} else if (!manifest || manifest->magic != AFB_EXTENSION_MAGIC
			|| !manifest->name) {
		RP_ERROR("Manifest error of extension %s", path);
		rc = X_EINVAL;
	} else if (manifest->version != 1) {
		RP_ERROR("Unsupported version %d of extension %s: %s", manifest->version, manifest->name, path);
		rc = X_ENOTSUP;
	} else {
		name = uid == NULL ? manifest->name : uid;
		if (search_extension_uid(name) != NULL) {
			RP_ERROR("Duplicated extension name %s", name);
			rc = X_EEXIST;
		} else {
			afb_v4_connect_dynlib(&handle, &infov4, 0);
			if (infov4.root || infov4.desc || infov4.mainctl) {
				RP_ERROR("CAUTION!!! Binding in extension must be compiled without global symbols!");
				RP_ERROR("  ...  Please recompile extension %s (%s)", manifest->name, path);
				if (infov4.root)
					RP_ERROR(" ... with AFB_BINDING_NO_ROOT defined (or option -D)");
				if (infov4.desc)
					RP_ERROR(" ... without defining a main structure (afbBindingRoot or afbBindingV4)");
				if (infov4.mainctl)
					RP_ERROR(" ... without defining an entry function (afbBindingEntry or afbBindingV4Entry)");
				rc = X_ENOTSUP;
			}
			else {
				strsz = pathlen = 1 + strlen(path);
				if (uid != NULL)
					strsz += strlen(uid) + 1;
				ext = malloc(strsz + sizeof *ext);
				if (!ext)
					rc = X_ENOMEM;
				else {
					RP_NOTICE("Adding extension %s of %s", name, path);
					ext->next = 0;
					ext->handle = handle;
					ext->manifest = manifest;
					ext->config = config ? json_object_get(config) : NULL;
					ext->data = 0;
					memcpy(ext->path, path, pathlen);
					if (uid == NULL)
						ext->uid = manifest->name;
					else
						ext->uid = strcpy(&ext->path[pathlen], uid);
					/* append at end */
					it = extensions;
					if (!it)
						extensions = ext;
					else {
						while (it->next)
							it = it->next;
						it->next = ext;
					}
					return 1;
				}
			}
		}
	}
	x_dynlib_close(&handle);
	if (!failstops && rc < 0) {
		if (rc < 0)
			RP_NOTICE("Ignoring extension %s", path);
		rc = 0;
	}
	return rc;
}

static void load_extension_cb(void *closure, struct json_object *value)
{
	int rc, *ret = closure;
	struct json_object *path, *uid, *config = NULL;
	const char *pathstr, *uidstr;

	pathstr = NULL;
	rc = X_EINVAL;
	if (json_object_is_type(value, json_type_string)) {
		pathstr = json_object_get_string(value);
		uidstr = NULL;
	}
	else if (json_object_is_type(value, json_type_object)
		&& json_object_object_get_ex(value, "path", &path)) {
		if (json_object_object_get_ex(value, "uid", &uid))
			uidstr = json_object_get_string(uid);
		else
			uidstr = NULL;
		pathstr = json_object_get_string(path);
		json_object_object_get_ex(value, "config", &config);
	}
	else {
		RP_ERROR("Invalid extension specifier %s", json_object_get_string(value));
		pathstr = NULL;
	}
	if (pathstr != NULL) {
		rc = load_extension(pathstr, 1, uidstr, config);
	}
	if (rc < 0 && *ret >= 0)
		*ret = rc;
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
	rc = load_extension(item->path, 0, NULL, NULL);
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
		RP_INFO("Scanning dir=[%s] for extensions", item->path);
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

/* load one extension by path */
int afb_extend_load_extension(const char *path, const char *uid, struct json_object *config)
{
	return load_extension(path, 1, uid, config);
}

/* load extensions at extpath */
int afb_extend_load_extpath(const char *extpath)
{
#if WITH_DIRENT
	return load_extpath(extpath);
#else
	return 1;
#endif
}

/* load extensions */
int afb_extend_load_set_of_extensions(struct json_object *set)
{
	int rc = 1;
	rp_jsonc_optarray_for_all(set, load_extension_cb, &rc);
	return rc;
}

/* load extensions by path */
int afb_extend_load_set_of_extpaths(struct json_object *set)
{
	int rc = 1;
#if WITH_DIRENT
	/* search extensions */
	rp_jsonc_optarray_for_all(set, load_extpath_cb, &rc);
#endif
	return rc;
}

/* get command line option description */
int afb_extend_get_options(const struct argp_option ***options_array_result, const char ***names)
{
	int rc, n, s;
	struct extension *ext;
	const struct argp_option *options;
	const struct argp_option **oar;
	const char **enam;
	const struct argp_option *(*getopt)();

	/* allocates enough for the result */
	for (n = 0, ext = extensions ; ext ; ext = ext->next, n++);
	*options_array_result = oar = malloc((unsigned)n * sizeof *oar);
	*names = enam = malloc((unsigned)n * sizeof *enam);
	if (!oar || !enam) {
		free(oar);
		free(enam);
		*options_array_result = 0;
		*names = 0;
		rc = X_ENOMEM;
	}
	else {
		/* initialize the results */
		n = 0;
		for (ext = extensions ; ext ; ext = ext->next) {
			s = x_dynlib_symbol(&ext->handle, GETOPTIONS_V1, (void**)&getopt);
			options = s < 0 ? NULL : getopt();
			if (options == NULL) {
				s = x_dynlib_symbol(&ext->handle, OPTIONS_V1, (void**)&options);
				if (s < 0)
					options = NULL;
			}
			if (options != NULL) {
				oar[n] = options;
				enam[n] = ext->uid;
				n++;
			}
		}
		rc = n;
	}
	return rc;
}

/* configure the extensions */
int afb_extend_configure(struct json_object *config)
{
	int rc, s;
	struct extension *ext;
	int (*config_v1)(void **data, struct json_object *config, const char *uid);
	struct json_object *obj;

	/* get the configuration root object */
	if (!json_object_is_type(config, json_type_object))
		config = NULL;

	/* iterate over extensions */
	rc = 0;
	for (ext = extensions ; ext ; ext = ext->next) {
		s = x_dynlib_symbol(&ext->handle, CONFIG_V1, (void**)&config_v1);
		if (s >= 0) {
			obj = NULL;
			if (config)
				json_object_object_get_ex(config, ext->uid, &obj);
			if (obj == NULL)
				obj = ext->config;
			else {
				if (ext->config) {
					rp_jsonc_object_merge(obj, ext->config, rp_jsonc_merge_option_join_or_keep);
					json_object_put(ext->config);
				}
				ext->config = json_object_get(obj);
			}
			s = config_v1(&ext->data, obj, ext->uid);
			if (s < 0)
				rc = s;
		}
	}
	return rc;
}

/* declare apis */
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

/* declare http */
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

/* start serving */
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

/* exit extensions */
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


#if WITH_DEPRECATED_OLDER_THAN_4_1
/* load extensions */
int afb_extend_load(struct json_object *config)
{
	struct json_object *set;
	int rc = 1;
	if (json_object_object_get_ex(config, "extension", &set))
		rc = afb_extend_load_set_of_extensions(set);
	if (rc >= 0 && json_object_object_get_ex(config, "extpaths", &set))
		rc = afb_extend_load_set_of_extpaths(set);
	return rc;
}

/* configure the extensions */
int afb_extend_config(struct json_object *config)
{
	struct json_object *root;

	/* get the configuration root object */
	if (!json_object_object_get_ex(config, "@extconfig", &root)
	|| !json_object_is_type(root, json_type_object))
		root = NULL;

	return afb_extend_configure(root);
}
#endif


#endif
