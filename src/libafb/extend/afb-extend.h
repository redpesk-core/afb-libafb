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

#pragma once

#include "../libafb-config.h"

#if WITH_EXTENSION

struct json_object;
struct afb_apiset;
struct argp_option;
struct afb_hsrv;

/**
 * Load one extension.
 *
 * @param path   path of the extension to be loaded
 * @param uid    if not NULL replace the name of the extension
 * @param config if not NULL, an object for setting default configuration
 *
 * @return 0 on success or an negative number
 */
extern int afb_extend_load_extension(const char *path, const char *uid, struct json_object *config);

/**
 * load extensions found at the given directories and sub directories
 *
 * @param extpath Colon separated list of directories to search
 *
 * @return 0 on success or an negative number
 */
extern int afb_extend_load_extpath(const char *extpath);

/**
 * Load a set of extensions listed in the given set.
 *
 * The set can be a single value or an array of values.
 *
 * Each value of the set can be either a string for the path
 * of the extension to be loaded or a structured object
 * with 3 known fields:
 *  - "path": MANDATORY, path of the extension to be loaded
 *  - "uid": if present replace the name of the extension
 *  - "config": an object that is passed at configuration

 * @param set a json object describing the set of extensions to load
 *
 * @return 0 on success or an negative number
 */
extern int afb_extend_load_set_of_extensions(struct json_object *set);

/**
 * Load the extensions found in the given set of path.
 *
 * The set can be a single value or an array of values.
 *
 * Each value of the set must be a string made of
 * colon separated list of directories to search.

 * @param set a json object listing the paths to search
 *
 * @return 0 on success or an negative number
 */
extern int afb_extend_load_set_of_extpaths(struct json_object *set);

/**
 * Configure the extensions. The extensions that have
 * a config function will receive the configuration
 * of the object config[uid] merged with
 * the configuration given at load.
 *
 * @param config the json object containing the extensions keyid by extension uid
 *
 * @return a positive or nul value on success or the negative
 * value of the first extension that returned a negative value
 */
extern int afb_extend_configure(struct json_object *config);

/**
 * Get the options declared by the loaded extensions
 *
 * @param options pointer where to store the allocated null
 *                terminated array of array of options
 * @param names pointer where to store the allocated null
 *              terminated array of names
 * @return the count of options declared by extensions
 * or a negative value if allocations failed
 */
extern int afb_extend_get_options(const struct argp_option ***options, const char ***names);

/**
 * Invoke API setup of extensions
 *
 * @param declare_set the apiset for declaring APIs
 * @param call_set the apiset for calling APIs
 *
 * @return a positive or nul value on success or the negative
 * value of the first extension that returned a negative value
 */
extern int afb_extend_declare(struct afb_apiset *declare_set, struct afb_apiset *call_set);

/**
 * Invoke HTTP setup of extensions
 *
 * @param hsrv handler of the HTTP server to setup
 *
 * @return a positive or nul value on success or the negative
 * value of the first extension that returned a negative value
 */
extern int afb_extend_http(struct afb_hsrv *hsrv);

/**
 * Invoke service start of extensions
 *
 * @param call_set the apiset for calling APIs
 *
 * @return a positive or nul value on success or the negative
 * value of the first extension that returned a negative value
 */
extern int afb_extend_serve(struct afb_apiset *call_set);

/**
 * Invoke exit of extensions
 *
 * @param declare_set the apiset for removing APIs
 *
 * @return a positive or nul value on success or the negative
 * value of the first extension that returned a negative value
 */
extern int afb_extend_exit(struct afb_apiset *declare_set);

/* DEPRECATED */
#undef WITH_DEPRECATED_OLDER_THAN_4_1
#define WITH_DEPRECATED_OLDER_THAN_4_1   1
#if WITH_DEPRECATED_OLDER_THAN_4_1
/**
 * Load the extensions listed in the given config.
 * The following fields of config are used:
 *
 * - "extension": If the json object config has a field
 *        of name "extension", that field must list the
 *        extension to load (see below)
 *
 * - "extpaths":  If the json object config has a field
 *        of name "extpaths", that field must list the
 *        directories that contain extensions to load
 *
 * Items of "extension" can be either a string for the path
 * of the extension to be loaded or a structured object
 * with 3 known fields:
 *  - "path": MANDATORY, path of the extension to be loaded
 *  - "uid": if present replace the name of the extension
 *  - "config": an object that is passed at configuration

 * @param config a json object telling what to load
 *
 * @return 0 on success or an negative number
 */
extern int afb_extend_load(struct json_object *config);

/**
 * Configure the extensions. The extensions that have
 * a config function will receive the configuration
 * of the object config[uid] merged with
 * the configuration of the extesion given in
 * config[extension][.uid=uid].config
 *
 * @param config the json object containing the extensions
 *
 * @return a positive or nul value on success or the negative
 * value of the first extension that returned a negative value
 */
extern int afb_extend_config(struct json_object *config);
#endif

#endif
