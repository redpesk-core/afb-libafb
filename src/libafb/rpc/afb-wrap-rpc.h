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

/**
 * RPC wrap connection mode, TLS and WebSocket are mutually exclusive
 */
enum afb_wrap_rpc_mode {
	/* bits */
	Wrap_Rpc_Mode_Server_Bit = 1,
	Wrap_Rpc_Mode_Tls_Bit    = 2,
	Wrap_Rpc_Mode_Mutual_Bit = 4,
	Wrap_Rpc_Mode_WS_Bit     = 8,

	/* file descriptor */
	Wrap_Rpc_Mode_FD = 0,
#if WITH_TLS
	/* file descriptor for TLS (client) */
	Wrap_Rpc_Mode_FD_Tls_Client
		= Wrap_Rpc_Mode_Tls_Bit,
	/* file descriptor for TLS (server) */
	Wrap_Rpc_Mode_FD_Tls_Server
		= Wrap_Rpc_Mode_Tls_Bit | Wrap_Rpc_Mode_Server_Bit,
	/* file descriptor for Mutual TLS (client) */
	Wrap_Rpc_Mode_FD_Mutual_Tls_Client
		= Wrap_Rpc_Mode_Mutual_Bit | Wrap_Rpc_Mode_Tls_Bit,
	/* file descriptor for Mutal TLS (server) */
	Wrap_Rpc_Mode_FD_Mutual_Tls_Server
		= Wrap_Rpc_Mode_Mutual_Bit | Wrap_Rpc_Mode_Tls_Bit | Wrap_Rpc_Mode_Server_Bit,
#endif
	/* WebSocket */
	Wrap_Rpc_Mode_Websocket = Wrap_Rpc_Mode_WS_Bit
};

struct afb_apiset;
struct afb_session;
struct afb_token;
struct afb_wrap_rpc;

/**
 * Creates an RPC wrapper for the socket 'fd'.
 * The wrapper links to event loop and dispatches incoming messages.
 *
 * @param wrap      pointer receiving the created wrapper
 * @param fd        file descriptor of the socket
 * @param autoclose if not zero, the socket is closed at end
 * @param websock   if not zero, initiate a websocket upgrading process
 * @param uri       sockspec URI specified by the user
 * @param apiname   the default API name, can be NULL except for clients
 * @param callset   the call set for received calls
 *
 * @returns 0 on success or a negative error code
 */
extern
int afb_wrap_rpc_create_fd(
		struct afb_wrap_rpc **wrap,
		int fd,
		int autoclose,
		enum afb_wrap_rpc_mode mode,
		const char *uri,
		const char *apiname,
		struct afb_apiset *callset);

/**
 * Declare the wrapper as serving a remote api
 *
 * @param wrap the wrapper
 * @param declare_set the set where api is exported
 *
 * @return 0 on success, X_EINVAL when apiname wasn't set,
 *         X_EEXIST when already registered to a set
 */
extern
int afb_wrap_rpc_start_client(
		struct afb_wrap_rpc *wrap,
		struct afb_apiset *declare_set);

/**
 * Function for implementing upgrade from HTTP to RPC on Websocket over HTTP.
 */
extern
int afb_wrap_rpc_websocket_upgrade(
		void *closure,
		int fd,
		int autoclose,
		struct afb_apiset *apiset,
		struct afb_session *session,
		struct afb_token *token,
		void (*cleanup)(void*),
		void *cleanup_closure,
		int websock);

/**
 * Function for automatic reconnection in case of disconnection
 *
 * @param wrap    the wrapper to robustify
 * @param reopen  function called with closure for reopening the fd
 * @param closure closure for reopen
 * @param release function for releasing the closure at end
 */
extern
void afb_wrap_rpc_fd_robustify(
		struct afb_wrap_rpc *wrap,
		int (*reopen)(void*),
		void *closure,
		void (*release)(void*));

#if WITH_CRED
struct afb_cred;

/**
 * Attach the credentials to the wrapped connection
 *
 * @param wrap the connection wrapper
 * @param cred credentials to attach
 */
extern
void afb_wrap_rpc_set_cred(
		struct afb_wrap_rpc *wrap,
		struct afb_cred *cred);
#endif

#if WITH_VCOMM
struct afb_vcomm;

/**
 * Creates an RPC wrapper for the 'com'.
 *
 * @param wrap      pointer receiving the created wrapper
 * @param com       communication object
 * @param apiname   the default API name, can be NULL except for clients
 * @param callset   the call set for received calls
 *
 * @returns 0 on success or a negative error code
 */
extern
int afb_wrap_rpc_create_vcomm(
		struct afb_wrap_rpc **wrap,
		struct afb_vcomm *vcomm,
		const char *apiname,
		struct afb_apiset *callset
);
#endif
