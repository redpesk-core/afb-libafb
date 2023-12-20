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

struct afb_stub_rpc;
struct afb_apiset;
struct afb_api_item;
struct afb_rpc;

extern struct afb_stub_rpc *afb_stub_rpc_create(const char *apiname, struct afb_apiset *call_set);

/**
 * Decrement the count of reference to the stub and drop its used resources
 * if the count reaches zero.
 *
 * @param stub the stub object
 */
extern void afb_stub_rpc_unref(struct afb_stub_rpc *stub);

/**
 * Increment the count of reference to the stub
 *
 * @param stub the stub object
 */
extern struct afb_stub_rpc *afb_stub_rpc_addref(struct afb_stub_rpc *stub);

/**
 * Apiname of the stub
 *
 * @param stub the stub object
 *
 * @return the apiname of the stub
 */
extern const char *afb_stub_rpc_apiname(struct afb_stub_rpc *stub);

extern void afb_stub_rpc_set_unpack(struct afb_stub_rpc *stub, int unpack);


extern int afb_stub_rpc_client_add(struct afb_stub_rpc *stub, struct afb_apiset *declare_set);

extern int afb_stub_rpc_can_receive(struct afb_stub_rpc *stub);
extern int afb_stub_rpc_receive(struct afb_stub_rpc *stub, void *data, size_t size);
extern void afb_stub_rpc_receive_set_dispose(struct afb_stub_rpc *stub, void (*dispose)(void*, void*, size_t), void *closure);

extern int afb_stub_rpc_emit_is_ready(struct afb_stub_rpc *stub);
extern struct afb_rpc_coder *afb_stub_rpc_emit_coder(struct afb_stub_rpc *stub);
extern void afb_stub_rpc_emit_set_notify(struct afb_stub_rpc *stub, void (*notify)(void*, struct afb_stub_rpc*), void *closure);

extern int afb_stub_rpc_offer_version(struct afb_stub_rpc *stub);

#if WITH_CRED
extern void afb_stub_rpc_set_cred(struct afb_stub_rpc *stub, struct afb_cred *cred);
#endif
