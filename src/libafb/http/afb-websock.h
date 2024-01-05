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

#include "../libafb-config.h"

#if WITH_LIBMICROHTTPD

struct afb_hreq;
struct afb_apiset;
struct afb_session;
struct afb_token;
struct wsprotodef;

/**
* create a websocket bound to the socket 'fd'.
* - 'closure' is as defined
* - 'fd' the socket
* - if 'autoclose' is set, the fd can be closed at end
* - 'apiset' is the callset for incoming requests
* - 'session' and 'token' are bound to some identification
* - 'cleanup' and 'cleanup_closure' are to be called at end
*/
typedef
	void *(*wscreator_t)(
		void *closure,
		int fd,
		int autoclose,
		struct afb_apiset *apiset,
		struct afb_session *session,
		struct afb_token *token,
		void (*cleanup)(void*),
		void *cleanup_closure);

/**
* initialize with the default definitions (otherwise, should be intialized to NULL)
*/
extern void afb_websock_init_with_defaults(struct wsprotodef **head);

/**
* Add a definition
*/
extern int afb_websock_add(
		struct wsprotodef **head,
		const char *name,
		wscreator_t creator,
		void *closure
);

/**
* Remove the definition of the given name
* if the name is NULL, all non default definitions are removed
*/
extern int afb_websock_remove(
		struct wsprotodef **head,
		const char *name
);

extern int afb_websock_upgrader(void *closure, struct afb_hreq *hreq, struct afb_apiset *apiset);

#endif

