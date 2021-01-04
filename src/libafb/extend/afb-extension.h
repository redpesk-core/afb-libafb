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

#pragma once

#define AFB_EXTENSION_MAGIC    78612
#define AFB_EXTENSION_VERSION  1

struct afb_extension_manifest
{
	unsigned magic;
	unsigned version;
	const char *name;
};

#define AFB_EXTENSION(somename) \
	struct afb_extension_manifest AfbExtensionManifest = { \
		.magic = AFB_EXTENSION_MAGIC, \
		.version = AFB_EXTENSION_VERSION, \
		.name = #somename \
	};

struct json_object;
struct afb_apiset;

#include <argp.h>

extern const struct argp_option AfbExtensionOptionsV1[];

extern int AfbExtensionConfigV1(void **data, struct json_object *config);
extern int AfbExtensionDeclareV1(void *data, struct afb_apiset *declare_set, struct afb_apiset *call_set);
extern int AfbExtensionServeV1(void *data, struct afb_apiset *call_set);
extern int AfbExtensionExitV1(void *data, struct afb_apiset *declare_set);
