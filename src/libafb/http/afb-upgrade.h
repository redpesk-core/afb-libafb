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
struct upgradedef;


/**
* the upgrade callback that receives the socket 'fd'.
* - 'closure' is as defined
* - 'hreq' the HTTPR request (already replied)
* - 'apiset' is the callset for incoming requests
* - 'fd' the socket
* - 'cleanup' and 'cleanup_closure' are to be called at end
*/
typedef int (*afb_upgrade_cb_t)(
		void *closure,
		struct afb_hreq *hreq,
		struct afb_apiset *apiset,
		int fd,
		void (*cleanup)(void*),
		void *cleanup_closure);

/**
* send the upgrade reply
*
* @param upgrdcb upgrade callback that receives the socket
* @param closure closure for the callback
* @param hreq    the HTTP request
* @param apiset  the call set
* @param protocol the protocol to put in the HTTP header UPGRADE (or NULL)
* @param count   count of headval items, must be even
* @param headval header and value to add the the reply [ H1 V1 H2 V2 ... ]
*/
extern int afb_upgrade_reply(
		afb_upgrade_cb_t upgrdcb,
		void *closure,
		struct afb_hreq *hreq,
		struct afb_apiset *apiset,
		const char *protocol,
		unsigned count,
		const char **headval
);

/**
* The upgrader callback.
* Upgraders are called if the switching protocol matches the recorded name.
* An upgrader can make several checks on the request 'hreq' and if it fits
* the upgrader requirement, it must call the function 'afb_upgrade_reply'.
* Should return 0 if the upgrade is rejected otherwise, should send a reply
* and a non zero value.
*/
typedef int (*afb_upgrader_t)(void *closure, struct afb_hreq *hreq, struct afb_apiset *apiset);


/**
* check if the http request 'hreq' is an upgrade request
* matching an upgrader of the server of the request
* returns 0 if its does not match or an other value if the reply
* was sent
*/
extern int afb_upgrade_check_upgrade(struct afb_hreq *hreq, struct afb_apiset *apiset);

/**
* initialize with the default definitions (otherwise, should be intialized to NULL)
*/
extern void afb_upgrade_init_with_defaults(struct upgradedef **head);

/**
* Add a definition
*/
extern int afb_upgrade_add(
		struct upgradedef **head,
		const char *name,
		afb_upgrader_t upgrader,
		void *closure
);

/**
* Remove the definition of the given name
* if the name is NULL, all non default definitions are removed
*/
extern int afb_upgrade_remove(
		struct upgradedef **head,
		const char *name
);


#endif

