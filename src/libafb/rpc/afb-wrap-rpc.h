/*
 * Copyright (C) 2015-2024 IoT.bzh Company
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

struct afb_apiset;
struct afb_session;
struct afb_token;
struct afb_wrap_rpc;

/**
 *
 */
extern int afb_wrap_rpc_create(struct afb_wrap_rpc **wrap, int fd, int autoclose, int websock, const char *apiname, struct afb_apiset *callset);

/**
 *
 */
extern int afb_wrap_rpc_start_client(struct afb_wrap_rpc *wrap, struct afb_apiset *declare_set);

/**
 * Function for implementing upgrade from HTTP or Websocket over HTTP.
 */
extern int afb_wrap_rpc_upgrade(
		void *closure,
		int fd,
		int autoclose,
		struct afb_apiset *apiset,
		struct afb_session *session,
		struct afb_token *token,
		void (*cleanup)(void*),
		void *cleanup_closure,
		int websock);

#if WITH_CRED
struct afb_cred;
extern void afb_wrap_rpc_set_cred(struct afb_wrap_rpc *wrap, struct afb_cred *cred);
#endif