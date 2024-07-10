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

#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <microhttpd.h>
#include <rp-utils/sha1.h>

#include "http/afb-method.h"
#include "http/afb-hreq.h"
#include "http/afb-hsrv.h"
#include "http/afb-websock.h"
#include "sys/x-errno.h"
#include "sys/x-alloca.h"

#include "wsj1/afb-ws-json1.h"
#include "http/afb-upgd-rpc.h"

const char afb_websocket_protocol_name[] = "websocket";

/**************** management of lists of protocol ****************************/

/**
* definition of a protocol
*/
struct wsprotodef
{
	/** name of the protocol */
	const char *name;

	/** link to the next definition */
	struct wsprotodef *next;

	/** creation function */
	wscreator_t creator;

	/** closure of the creator */
	void *closure;
};

/**
* default websocket protocols
*/
static const struct wsprotodef default_protocols[] = {
	{
		.name = "x-afb-ws-json1",
		.next = (struct wsprotodef*)&default_protocols[1],
		.creator = (wscreator_t)afb_ws_json1_create, /* cast needed to convert result to void* */
		.closure = NULL
	},
	{
		.name = afb_upgd_rpc_ws_protocol_name,
		.next = NULL,
		.creator = afb_rpc_upgd_ws,
		.closure = NULL
	}
};

/**
* check if 'protodef' is a default protocol
*/
static inline int is_default_protodef(const struct wsprotodef *protodef)
{
	const uintptr_t begin = (uintptr_t)default_protocols;
	const uintptr_t end = begin + (uintptr_t)(sizeof default_protocols);
	const uintptr_t ptr = (uintptr_t)protodef;
	return begin <= ptr && ptr < end;
}

/* see afb-websock.h */
void afb_websock_init_with_defaults(struct wsprotodef **head)
{
	*head = (struct wsprotodef*)default_protocols;
}

/* see afb-websock.h */
int afb_websock_add(
		struct wsprotodef **head,
		const char *name,
		wscreator_t creator,
		void *closure
) {
	struct wsprotodef *protodef = malloc(sizeof *protodef);
	if (protodef == NULL)
		return X_ENOMEM;
	protodef->name = name;
	protodef->creator = creator;
	protodef->closure = closure;
	protodef->next = *head;
	*head = protodef;
	return 0;
}

/* see afb-websock.h */
int afb_websock_remove(
		struct wsprotodef **head,
		const char *name
) {
	for (;;) {
		struct wsprotodef *protodef = *head;
		if (protodef == NULL)
			return X_ENOENT;
		if (is_default_protodef(protodef))
			return 0;
		if (name == NULL || strcmp(name, protodef->name) == 0) {
			*head = protodef->next;
			free(protodef);
			if (name != NULL)
				return 0;
		}
		head = &protodef->next;
	}
}

/**************** WebSocket connection upgrade ****************************/

static const char sec_websocket_key_s[] = "Sec-WebSocket-Key";
static const char sec_websocket_version_s[] = "Sec-WebSocket-Version";
static const char sec_websocket_accept_s[] = "Sec-WebSocket-Accept";
static const char sec_websocket_protocol_s[] = "Sec-WebSocket-Protocol";
static const char websocket_guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static void enc64(unsigned char *in, char *out)
{
	static const char tob64[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789+/";
	out[0] = tob64[in[0] >> 2];
	out[1] = tob64[((in[0] & 0x03) << 4) | ((in[1] & 0xf0) >> 4)];
	out[2] = tob64[((in[1] & 0x0f) << 2) | ((in[2] & 0xc0) >> 6)];
	out[3] = tob64[in[2] & 0x3f];
}

static void make_accept_value(const char *key, char result[29])
{
	SHA1_t sha1;
	unsigned char md[SHA1_DIGEST_LENGTH+1];
	size_t len = strlen(key);
	char *buffer = alloca(len + sizeof websocket_guid - 1);
	memcpy(buffer, key, len);
	memcpy(buffer + len, websocket_guid, sizeof websocket_guid - 1);
	SHA1_init(&sha1);
	SHA1_update(&sha1, buffer, len + sizeof websocket_guid - 1);
	SHA1_final(&sha1, md);
	assert(SHA1_DIGEST_LENGTH == 20);
	md[20] = 0;
	enc64(&md[0], &result[0]);
	enc64(&md[3], &result[4]);
	enc64(&md[6], &result[8]);
	enc64(&md[9], &result[12]);
	enc64(&md[12], &result[16]);
	enc64(&md[15], &result[20]);
	enc64(&md[18], &result[24]);
	result[27] = '=';
	result[28] = 0;
}

static const struct wsprotodef *search_proto(const struct wsprotodef *protodefs, const char *protocols)
{
	size_t len;
	const struct wsprotodef *iter;
	const char *vseparators = " \t,";


	if (protocols == NULL) {
		/* return NULL; */
		return protodefs != NULL ? protodefs : NULL;
	}
	for(;;) {
		protocols += strspn(protocols, vseparators);
		if (!*protocols)
			return NULL;
		len = strcspn(protocols, vseparators);
		for (iter = protodefs ; iter != NULL ; iter = iter->next)
			if (!strncasecmp(iter->name, protocols, len)
			 && !iter->name[len])
				return iter;
		protocols += len;
	}
}

/**************** WebSocket connection upgrade ****************************/

static int upgrading_cb(
		void *closure,
		struct afb_hreq *hreq,
		struct afb_apiset *apiset,
		int fd,
		void (*cleanup)(void*),
		void *cleanup_closure
) {
	const struct wsprotodef *proto = closure;

	void *ws = proto->creator(proto->closure, fd, 0, apiset,
				 hreq->comreq.session, hreq->comreq.token,
				 cleanup, cleanup_closure);
	return ws == NULL ? -1 : 0;
}

int afb_websock_upgrader(void *closure, struct afb_hreq *hreq, struct afb_apiset *apiset)
{
	struct MHD_Response *response;
	const char *key, *version, *protocols, *headval[4];
	const struct wsprotodef *protodefs;
	struct MHD_Connection *con = hreq->connection;
	char acceptval[29];
	int vernum;
	const struct wsprotodef *proto;

	/* has a key and a version ? */
	key = MHD_lookup_connection_value(con, MHD_HEADER_KIND, sec_websocket_key_s);
	version = MHD_lookup_connection_value(con, MHD_HEADER_KIND, sec_websocket_version_s);
	if (key == NULL || version == NULL)
		return 0;

	/* is a supported version ? */
	vernum = atoi(version);
	if (vernum != 13) {
		response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
		MHD_add_response_header(response, sec_websocket_version_s, "13");
		MHD_queue_response(con, MHD_HTTP_UPGRADE_REQUIRED, response);
		MHD_destroy_response(response);
		return 1;
	}

	/* is the protocol supported ? */
	protocols = MHD_lookup_connection_value(con, MHD_HEADER_KIND, sec_websocket_protocol_s);
	protodefs = afb_hsrv_ws_protocols(hreq->hsrv);
	proto = search_proto(protodefs, protocols);
	if (proto == NULL) {
		response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
		MHD_queue_response(con, MHD_HTTP_PRECONDITION_FAILED, response);
		MHD_destroy_response(response);
		return 1;
	}

	/* send the accept connection */
	make_accept_value(key, acceptval);
	headval[0] = sec_websocket_accept_s;
	headval[1] = acceptval;
	headval[2] = sec_websocket_protocol_s;
	headval[3] = proto->name;
	return afb_upgrade_reply(upgrading_cb, (void*)proto, hreq, apiset, afb_websocket_protocol_name, 4, headval);
}

#endif
