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

struct afb_data;
struct afb_auth;

/** name of the verb info that superseeds description */
extern const char afb_info_verbname[];

/********************************************************************
 * Remaining functions are using a state item of type 'struct afb_info'
 * (aka afb_info_t).
 * The goal of that structure is to compute the content of info result
 * two steps:
 *    STEP 1: compute the size
 *    STEP 2: fill the content
 * The code filling the info verb then looks as below:
 *
 *   int rc;
 *   struct afb_data *data = NULL;
 *   struct afb_info info;
 *
 *   afb_info_init(&info);
 *   do {
 *      rc = afb_info_set_api(&info, apiname, apiinfo, apispec);
 *      while (rc >= 0 && ...verb iterator not at end...) {
 *         rc = afb_info_add_verb(&info, ...verb info values of iterator...);
 *   	   ...iterator advance...;
 *      }
 *      rc = afb_info_end(&info, &data);
 *   } while(rc > 0);
 */

/** structure for computing info */
typedef
struct afb_info
{
	int state;
	const char *spec;
	char *buffer;
	size_t pos;
	size_t size;
}
	afb_info_t;

extern
void afb_info_init(
	struct afb_info *info);

extern
int afb_info_set_api(
	struct afb_info *info,
	const char *apiname,
	const char *apiinfo,
	const char *specification);

extern
int afb_info_add_verb(
	struct afb_info *info,
	const char *verbname,
	const char *verbinfo,
	unsigned session,
	const struct afb_auth *auth,
	unsigned glob);

extern
int afb_info_end(
	struct afb_info *info,
	struct afb_data **data);

