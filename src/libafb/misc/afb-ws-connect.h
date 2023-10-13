/*
 * Copyright (C) 2015-2023 IoT.bzh Company
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

struct ev_mgr;

/**
* connect to the server designated by 'uri' and negociates the websocket upgrade
*
* @param mgr the event manager
* @param uri the URI for connection
* @param protocols the list of protocols to negociate in the preferred order
* @param idxproto on success, the index in protocols of the accepted protocol (can be NULL)
* @param headers more headers to send in the upgrade request
*
* @return the file descriptor of the negociated websocket or a negative error code
*/
extern int afb_ws_connect(
		struct ev_mgr *mgr,
		const char *uri,
		const char **protocols,
		int *idxproto,
		const char **headers);
