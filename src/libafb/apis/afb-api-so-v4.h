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

#include "../libafb-config.h"

#if WITH_DYNAMIC_BINDING

#include "../sys/x-dynlib.h"

struct afb_apiset;
struct json_object;

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
extern int afb_api_so_v4_add(const char *path, x_dynlib_t *dynlib, struct afb_apiset *declare_set, struct afb_apiset * call_set);

/**
 * Inspect the loaded shared object 'dynlib' to check if it is a binding V4
 * If yes try to load and pre initiialize it.
 *
 * @param path    path of the loaded library
 * @param dynlib  handle of the dynamic library
 * @param declare_set the apiset where the binding APIS are to be declared
 * @param call_set the apiset to use when invoking other apis
 * @param config  a configuration object
 *
 * @return 0 if not a binding v4, 1 if valid binding V4 correctly pre-initialized,
 *         a negative number if invalid binding V4 or error when initializing.
 */
extern
int
afb_api_so_v4_add_config(
	const char *path,
	x_dynlib_t *dynlib,
	struct afb_apiset *declare_set,
	struct afb_apiset * call_set,
	struct json_object *config
);

#endif
