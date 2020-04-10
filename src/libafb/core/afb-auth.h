/*
 * Copyright (C) 2015-2020 IoT.bzh Company
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

struct afb_auth;
struct afb_context;
struct afb_xreq;
struct json_object;

#if SYNCHRONOUS_CHECKS
extern int afb_auth_check(struct afb_context *context, const struct afb_auth *auth);
#endif
extern void afb_auth_check_async(
	struct afb_context *context,
	const struct afb_auth *auth,
	void (*callback)(void *_closure, int _status),
	void *closure
);

#if SYNCHRONOUS_CHECKS
extern int afb_auth_check_and_set_session_x2(struct afb_xreq *xreq, const struct afb_auth *auth, uint32_t session);
#endif
extern void afb_auth_check_and_set_session_x2_async(
	struct afb_xreq *xreq,
	const struct afb_auth *auth,
	uint32_t sessionflags,
	void (*callback)(struct afb_xreq *_xreq, int _status, void *_closure),
	void *closure
);
extern struct json_object *afb_auth_json_x2(const struct afb_auth *auth, uint32_t session);

