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

struct afb_stub_ws;
struct afb_apiset;
struct afb_api_item;

extern struct afb_stub_ws *afb_stub_ws_create_client(int fd, const char *apiname, struct afb_apiset *apiset);

extern struct afb_stub_ws *afb_stub_ws_create_server(int fd, const char *apiname, struct afb_apiset *apiset);

/**
 * Decrement the count of reference to the stub and drop its used resources
 * if the count reaches zero.
 *
 * @param stubws the stub object
 */
extern void afb_stub_ws_unref(struct afb_stub_ws *stubws);

/**
 * Increment the count of reference to the stub
 *
 * @param stubws the stub object
 */
extern void afb_stub_ws_addref(struct afb_stub_ws *stubws);

/**
 * Apiname of the stub
 *
 * @param stubws the stub object
 *
 * @return the apiname of the stub
 */
extern const char *afb_stub_ws_apiname(struct afb_stub_ws *stubws);

extern void afb_stub_ws_set_on_hangup(struct afb_stub_ws *stubws, void (*on_hangup)(struct afb_stub_ws*));


extern struct afb_api_item afb_stub_ws_client_api(struct afb_stub_ws *stubws);

extern int afb_stub_ws_client_add(struct afb_stub_ws *stubws, struct afb_apiset *apiset);

extern void afb_stub_ws_client_robustify(struct afb_stub_ws *stubws, int (*reopen)(void*), void *closure, void (*release)(void*));

