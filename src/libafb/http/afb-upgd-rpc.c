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

#include "../libafb-config.h"

#if WITH_LIBMICROHTTPD

#include "http/afb-upgrade.h"
#include "http/afb-hreq.h"
#include "http/afb-upgd-rpc.h"
#include "rpc/afb-wrap-rpc.h"

const char afb_upgd_rpc_protocol_name[] = "x-afb-rpc";
const char afb_upgd_rpc_ws_protocol_name[] = "x-afb-ws-rpc";

static int upgrade_cb(
		void *closure,
		struct afb_hreq *hreq,
		struct afb_apiset *apiset,
		int fd,
		void (*cleanup)(void*),
		void *cleanup_closure
) {
	return afb_wrap_rpc_upgrade(
		closure,
		fd,
		0,
		apiset,
		hreq->comreq.session,
		hreq->comreq.token,
		cleanup,
		cleanup_closure,
		0);
}

int afb_rpc_upgd(void *closure, struct afb_hreq *hreq, struct afb_apiset *apiset)
{
	return afb_upgrade_reply(upgrade_cb, NULL, hreq, apiset, afb_upgd_rpc_protocol_name, 0, NULL);
}

void *afb_rpc_upgd_ws(
		void *closure,
		int fd,
		int autoclose,
		struct afb_apiset *apiset,
		struct afb_session *session,
		struct afb_token *token,
		void (*cleanup)(void*),
		void *cleanup_closure)
{
	int rc = afb_wrap_rpc_upgrade(
		closure,
		fd,
		autoclose,
		apiset,
		session,
		token,
		cleanup,
		cleanup_closure,
		1);
	return (void*)(intptr_t)(rc == 0); /* TODO remove that hack */
}

#endif
