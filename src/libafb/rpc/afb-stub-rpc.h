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

struct afb_stub_rpc;
struct afb_apiset;
struct afb_session;
struct afb_token;
struct afb_rpc_coder;

/**
 * Creates a stub rpc object dispatching incoming requests
 * to the given call set, for the given apiname by default
 *
 * @param stub pointer for storing the created stub
 * @param apiname default apiname, NULL if none
 * @param callset the apiset for serving requests
 *
 * @return 0 on success, a negative error code on failure
 */
extern int afb_stub_rpc_create(struct afb_stub_rpc **stub, const char *apiname, struct afb_apiset *callset);

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
 * @return the apiname of the stub, might be NULL
 */
extern const char *afb_stub_rpc_apiname(struct afb_stub_rpc *stub);

/**
 * Set the default pair session of the stub
 * The reference count of session is incremented on set and is decremented
 * on release.
 *
 * @param stub the stub object
 * @param session the session object to set
 */
extern void afb_stub_rpc_set_session(struct afb_stub_rpc *stub, struct afb_session *session);

/**
 * Set the default pair token of the stub
 * The reference count of token is incremented on set and is decremented
 * on release.
 *
 * @param stub the stub object
 * @param token the token object to set
 */
extern void afb_stub_rpc_set_token(struct afb_stub_rpc *stub, struct afb_token *token);

#if WITH_CRED
struct afb_cred;
/**
 * Set the pair credentials of the stub.
 * The reference count of cred is not incremented but is decremented
 * on release.
 *
 * @param stub the stub object
 * @param cred the credential object to set
 */
extern void afb_stub_rpc_set_cred(struct afb_stub_rpc *stub, struct afb_cred *cred);
#endif

/**
 * Set packing/unpacking
 * The stub is packing when it groups messages together before sending it.
 * Packing is required when sending over websockets (don't remember why but
 * it is linked to the length of messages that are encoded by websocket layer)
 *
 * @param stub the stub object
 * @param unpack if not zero then don't pack
 */
extern void afb_stub_rpc_set_unpack(struct afb_stub_rpc *stub, int unpack);

/**
 * Adds in the declare_set the api of the stub apiname that is calling the API
 * will invoke the remote
 *
 * @param stub the stub object
 * @param declare_set the apiset for declaring the API
 *
 * @returns 0 in case of success or a negative value on failure
 */
extern int afb_stub_rpc_client_add(struct afb_stub_rpc *stub, struct afb_apiset *declare_set);

/**
 * Tells the stub that it received the buffer of data of size and ask to
 * process it.
 * Buffers given that way are kept hold by the stub while needed. When these
 * buffers can be released because not more of use, stub will call the dispose
 * function recorded by afb_stub_rpc_receive_set_dispose
 *
 * @param stub the stub object
 * @param data the received buffer to be processed
 * @param size size of the received buffer in bytes
 *
 * @return the size used in case of success (zero or less than 'size' if incomplete message)
 *         or else a negative error code
 */
extern ssize_t afb_stub_rpc_receive(struct afb_stub_rpc *stub, void *data, size_t size);

/**
 * Record the function to call for disposing of received buffers
 *
 * The function 'dispose' receives 3 parameters:
 *  - the closure given here
 *  - the data buffer to release
 *  - the size to release
 *
 * @param stub the stub object
 * @param dispose the disposal function
 * @param closure closure to the disposal function
 */
extern void afb_stub_rpc_receive_set_dispose(
		struct afb_stub_rpc *stub,
		void (*dispose)(void*, void*, size_t),
		void *closure);

/**
 * Test if the stub has data to be emitted
 *
 * @param stub the stub object
 *
 * @return 0 in if no data is ready of else returns a not null value
 */
extern int afb_stub_rpc_emit_is_ready(struct afb_stub_rpc *stub);

/**
 * Get the encoder of the stub
 *
 * @param stub the stub object
 *
 * @return the encoder of the stub
 */
extern struct afb_rpc_coder *afb_stub_rpc_emit_coder(struct afb_stub_rpc *stub);

/**
 * Record the function to call for sending available data
 *
 * The function 'notify' receives 2 parameters:
 *  - the closure given here
 *  - the stub object
 *
 * @param stub the stub object
 * @param notify the notify function
 * @param closure closure to the notify function
 */

extern void afb_stub_rpc_emit_set_notify(
		struct afb_stub_rpc *stub,
		int (*notify)(void*, struct afb_rpc_coder*),
		void *closure);

/**
 * Sends version offering.
 *
 * @param stub the stub object
 *
 * @return 0 in case of success or else a negative error code
 */
extern int afb_stub_rpc_offer_version(struct afb_stub_rpc *stub);
