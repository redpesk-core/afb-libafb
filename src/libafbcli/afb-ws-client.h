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

struct afb_wsj1;
struct afb_wsj1_itf;
struct afb_proto_ws;
struct afb_proto_ws_client_itf;
struct afb_wsapi;
struct afb_wsapi_itf;
struct sd_event;

/**
 * Makes the WebSocket handshake at the 'uri' and if successful
 * instantiate a wsj1 websocket for this connection using 'itf' and 'closure'.
 * (see afb_wsj1_create).
 * The systemd event loop 'eloop' is used to handle the websocket.
 * Returns NULL in case of failure with errno set appropriately.
 */
extern struct afb_wsj1 *afb_ws_client_connect_wsj1(struct sd_event *eloop, const char *uri, struct afb_wsj1_itf *itf, void *closure);

/**
 * Establish a websocket-like client connection to the API of 'uri' and if successful
 * instantiate a client afb_proto_ws websocket for this API using 'itf' and 'closure'.
 * (see afb_proto_ws_create_client).
 * The systemd event loop 'eloop' is used to handle the websocket.
 * Returns NULL in case of failure with errno set appropriately.
 */
extern struct afb_proto_ws *afb_ws_client_connect_api(struct sd_event *eloop, const char *uri, struct afb_proto_ws_client_itf *itf, void *closure);

/**
 * Establish a websocket-like client connection to the API of 'uri' and if successful
 * instantiate a client afb_wsapi websocket for this API using 'itf' and 'closure'.
 * (see afb_wsapi_create).
 * The systemd event loop 'eloop' is used to handle the websocket.
 * Returns NULL in case of failure with errno set appropriately.
 */
extern struct afb_wsapi *afb_ws_client_connect_wsapi(struct sd_event *eloop, const char *uri, struct afb_wsapi_itf *itf, void *closure);

/**
 * Establish a socket server waiting client connections.
 * Call 'onclient' for incoming connections.
 */
int afb_ws_client_serve(struct sd_event *eloop, const char *uri, int (*onclient)(void*,int), void *closure);

/**
 * Attaches the internal event loop to the given sd_event
 */
int afb_ws_client_connect_to_sd_event(struct sd_event *eloop);

