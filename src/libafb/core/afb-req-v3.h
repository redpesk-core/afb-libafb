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

struct afb_req_common;
struct afb_api_v3;
struct afb_api_x3;
struct afb_verb_v3;
struct afb_req_v3;

extern
void
afb_req_v3_process(
	struct afb_req_common *comreq,
	struct afb_api_v3 *api,
	struct afb_api_x3 *apix3,
	const struct afb_verb_v3 *verbv3
);

/**
 * Get the common request linked to reqv3
 *
 * @param reqv3 the req to query
 *
 * @return the common request attached to the request
 */
extern
struct afb_req_common *
afb_req_v3_get_common(
	struct afb_req_v3 *reqv3
);
