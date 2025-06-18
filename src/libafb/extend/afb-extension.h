/*
 * Copyright (C) 2015-2025 IoT.bzh Company
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

/**
 * This file is the main header for defining extensions.
 *
 * For defining an extension you have to declare the name
 * of the extension using the macro AFB_EXTENSION as below
 *
 *       AFB_EXTENSION(my-name)
 *
 * This instanciate a description structure used for
 * identifying extensions. The binder, or any program
 * aware of libafb extensions tracks the definition
 * of name "AfbExtensionManifest" to identify extensions
 *
 * Once the extension is found, the following symbols,
 * if defined and exported, are used in specific
 * situations.
 *
 *     - AfbExtensionOptionsV1
 *     - AfbExtensionConfigV1
 *     - AfbExtensionDeclareV1
 *     - AfbExtensionHTTPV1
 *     - AfbExtensionServeV1
 *     - AfbExtensionExitV1
 *
 * Check below to get the context of their use.
 *
 * None of the above symbol is needed. An extension can exist
 * without any of it, just be relying on __attribute__((constructor))
 * and __attribute__((destructor)) of functions or on the older
 * and now deprecated couple  _init/_fini
 */

/**
 * Description of the extension
 */
struct afb_extension_manifest
{
	/** a magic number for describing the extension */
	unsigned magic;

	/** version of the extension interface */
	unsigned version;

	/** name of the extension */
	const char *name;
};

/**
 * The value of magic
 */
#define AFB_EXTENSION_MAGIC    78612

/**
 * Current version of the interface
 */
#define AFB_EXTENSION_VERSION  1

/**
 * Macro for defining the extension for the current
 * version and the given name.
 *
 * @param extname name to give to the extension (a string)
 */
#define AFB_EXTENSION(extname) \
	struct afb_extension_manifest AfbExtensionManifest = { \
		.magic = AFB_EXTENSION_MAGIC, \
		.version = AFB_EXTENSION_VERSION, \
		.name = extname \
	};

/* some forward declarations */
struct json_object;
struct afb_apiset;
struct afb_hsrv;
#include <argp.h>

/**
 * AfbExtensionOptionsV1
 * ---------------------
 *
 * When defined, this symbol must be an array of argp_options
 * (see links below). This is used to enable definition of
 * command line options that the binder can parse.
 *
 * http://www.gnu.org/software/libc/manual/html_node/Argp.html
 * http://www.gnu.org/software/libc/manual/html_node/Argp-Option-Vectors.html
 */
extern const struct argp_option AfbExtensionOptionsV1[];

/**
 * AfbExtensionGetOptionsV1
 * ------------------------
 *
 * When defined, this function must return an array of argp_options.
 * See above 'AfbExtensionOptionsV1'.
 * 
 * When the static version (the array) 'AfbExtensionOptionsV1' and the dynamic
 * version (the function) 'AfbExtensionGetOptionsV1' are both defined,
 * only the dynamic version will be called first and the static version (the
 * array) will be used only if the function returned NULL.
 */
extern const struct argp_option *AfbExtensionGetOptionsV1();

/**
 * AfbExtensionConfigV1
 * --------------------
 *
 * If defined and exported, this function is called for configuring the
 * extension. The configuration is given by the json object @p config
 * that is set for the given @p uid.
 *
 * The extension can store a handle in the given @p data pointer. This
 * handle will then be passed to function of the extension interface.
 *
 * By default, the @p uid is the extension name as declared by the
 * macro AFB_EXTENSION. However, on need, an extension can be renamed, in
 * such case, the @p uid is the renamed name. This behaviour allows designers
 * of extensions to handle multiple loads but with different names and config.
 *
 * @example:
 * @code{.c}
 * int AfbExtensionConfigV1(void **data, struct json_object *config, const char *uid)
 * {
 *	*data = json_object_get(config);
 *	return 0;
 * }
 * @endcode
 *
 * @param data a pointer void the extension can store a pointer.
 *             the stored pointer is given as the data parameter to other
 *             functions of the extension interface
 * @param config the json object handling specific extension values
 * @param uid name of the extension
 *
 * @return an @c int negative on error or positive or nul on success
 */
extern int AfbExtensionConfigV1(void **data, struct json_object *config, const char *uid);

/**
 * AfbExtensionDeclareV1
 * ---------------------
 *
 * If defined and exported, this function is called for declaring
 * things before the real start.
 *
 * @param data the data created in config (@see AfbExtensionConfigV1)
 * @param declare_set the apiset for declaring APIs
 * @param call_set the apiset for calling APIs
 *
 * @return an @c int negative on error or positive or nul on success
 */
extern int AfbExtensionDeclareV1(void *data, struct afb_apiset *declare_set, struct afb_apiset *call_set);

/**
 * AfbExtensionHTTPV1
 * ------------------
 *
 * If defined and exported, this function is called for declaring
 * handler in the http server before the real start.
 *
 * @param data the data created in config (@see AfbExtensionConfigV1)
 * @param hsrv handler of the HTTP server to setup
 *
 * @return an @c int negative on error or positive or nul on success
 */
extern int AfbExtensionHTTPV1(void *data, struct afb_hsrv *hsrv);

/**
 * AfbExtensionServeV1
 * -------------------
 *
 * If defined and exported, this function is called for starting
 * the service of the extension. This is the real start.
 *
 * @param data the data created in config (@see AfbExtensionConfigV1)
 * @param call_set the apiset for calling APIs
 *
 * @return an @c int negative on error or positive or nul on success
 */
extern int AfbExtensionServeV1(void *data, struct afb_apiset *call_set);

/**
 * AfbExtensionExitV1
 * ------------------
 *
 * If defined and exported, this function is called for exiting
 * the extension.
 *
 * @param data the data created in config (@see AfbExtensionConfigV1)
 * @param declare_set the apiset for removing APIs
 *
 * @return an @c int negative on error or positive or nul on success
 */
extern int AfbExtensionExitV1(void *data, struct afb_apiset *declare_set);
