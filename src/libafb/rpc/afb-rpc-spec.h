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

#include <stddef.h>
#include <stdbool.h>

struct afb_rpc_spec;

extern
void afb_rpc_spec_unref(
	struct afb_rpc_spec *spec);

extern
struct afb_rpc_spec *afb_rpc_spec_addref(
	struct afb_rpc_spec *spec);

extern
int afb_rpc_spec_make(
	struct afb_rpc_spec **spec,
	const char *imports,
	const char *exports);

extern
int afb_rpc_spec_for_api(
	struct afb_rpc_spec **spec,
	const char *api,
	bool client);

extern
int afb_rpc_spec_from_uri(
	struct afb_rpc_spec **spec,
	const char *uri,
	bool client);

extern
int afb_rpc_spec_search(
	const struct afb_rpc_spec *spec,
	const char *api,
	bool client,
	const char **result);

extern
int afb_rpc_spec_for_each(
	const struct afb_rpc_spec *spec,
	bool client,
	int (*callback)(void *closure, const char *locname, const char *remname),
	void *closure);

extern
char *afb_rpc_spec_dump(
	const struct afb_rpc_spec *spec,
	size_t *length);

