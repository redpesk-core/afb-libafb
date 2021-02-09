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

struct afb_req_common;

#if WITH_CRED

/**
 * Check if the credential associated with the request 'req'
 * are including a granted access to the permission.
 *
 * This check is by nature asynchronous because the permission
 * is granted by an foreign process (an authority).
 *
 * The result of the check is given to the callback. Precisely,
 * the callback receives 2 values: the closure given at the call
 * and the status of the check. The values of the check status are:
 *
 *      - 0 if the permission is denied
 *      - 1 if the permission is granted
 *      - negative if some error occurred during the processus
 *
 * Optimization allows to cache requested permission if possible.
 * If the answer is cached, the callback can be called immediately
 * before returning.
 *
 * @param req request whose credential are to be used for the check
 * @param permission the permission to be checked
 * @param callback the callback that receives the status
 * @param closure the closure passed back to the callback
 */
extern void afb_perm_check_req_async(
	struct afb_req_common *req,
	const char *permission,
	void (*callback)(void *closure, int status),
	void *closure
);

#else

static inline void afb_perm_check_req_async(
	struct afb_req_common *req,
	const char *permission,
	void (*callback)(void *closure, int status),
	void *closure
) {
	callback(closure, 1);
}

#endif
