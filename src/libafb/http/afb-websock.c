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

#include "libafb-config.h"

#if WITH_LIBMICROHTTPD

#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <microhttpd.h>

#include "wsj1/afb-ws-json1.h"
#include "utils/sha1.h"

#include "http/afb-method.h"
#include "http/afb-hreq.h"
#include "http/afb-websock.h"

/**************** WebSocket connection upgrade ****************************/

static const char websocket_s[] = "websocket";
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

static const char vseparators[] = " \t,";

static int headerhas(const char *header, const char *needle)
{
	size_t len, n;

	n = strlen(needle);
	for(;;) {
		header += strspn(header, vseparators);
		if (!*header)
			return 0;
		len = strcspn(header, vseparators);
		if (n == len && 0 == strncasecmp(needle, header, n))
			return 1;
		header += len;
	}
}

typedef
	void *(*wscreator_t)(
		int fd,
		struct afb_apiset *apiset,
		struct afb_session *session,
		struct afb_token *token,
		void (*cleanup)(void*),
		void *cleanup_closure);

struct protodef
{
	const char *name;
	wscreator_t create;
};

static const struct protodef *search_proto(const struct protodef *protodefs, const char *protocols)
{
	int i;
	size_t len;

	if (protocols == NULL) {
		/* return NULL; */
		return protodefs != NULL && protodefs->name != NULL ? protodefs : NULL;
	}
	for(;;) {
		protocols += strspn(protocols, vseparators);
		if (!*protocols)
			return NULL;
		len = strcspn(protocols, vseparators);
		for (i = 0 ; protodefs[i].name != NULL ; i++)
			if (!strncasecmp(protodefs[i].name, protocols, len)
			 && !protodefs[i].name[len])
				return &protodefs[i];
		protocols += len;
	}
}

struct memo_websocket {
	const struct protodef *proto;
	struct afb_hreq *hreq;
	struct afb_apiset *apiset;
};

static void close_websocket(void *closure)
{
	struct MHD_UpgradeResponseHandle *urh = closure;
	MHD_upgrade_action (urh, MHD_UPGRADE_ACTION_CLOSE);
}

static void upgrade_to_websocket(
			void *cls,
			struct MHD_Connection *connection,
			void *con_cls,
			const char *extra_in,
			size_t extra_in_size,
			MHD_socket sock,
			struct MHD_UpgradeResponseHandle *urh)
{
	struct memo_websocket *memo = cls;
	void *ws;

	ws = memo->proto->create(dup(sock), memo->apiset, memo->hreq->comreq.session, memo->hreq->comreq.token, close_websocket, urh);
	if (ws == NULL) {
		/* TODO */
		close_websocket(urh);
	}
#if MHD_VERSION <= 0x00095900
	afb_hreq_unref(memo->hreq);
#endif
	free(memo);
}

static int check_websocket_upgrade(struct MHD_Connection *con, const struct protodef *protodefs, struct afb_hreq *hreq, struct afb_apiset *apiset)
{
	struct memo_websocket *memo;
	struct MHD_Response *response;
	const char *connection, *upgrade, *key, *version, *protocols;
	char acceptval[29];
	int vernum;
	const struct protodef *proto;

	/* is an upgrade to websocket ? */
	upgrade = MHD_lookup_connection_value(con, MHD_HEADER_KIND, MHD_HTTP_HEADER_UPGRADE);
	if (upgrade == NULL || strcasecmp(upgrade, websocket_s))
		return 0;

	/* is a connection for upgrade ? */
	connection = MHD_lookup_connection_value(con, MHD_HEADER_KIND, MHD_HTTP_HEADER_CONNECTION);
	if (connection == NULL
	 || !headerhas (connection, MHD_HTTP_HEADER_UPGRADE))
		return 0;

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
	proto = search_proto(protodefs, protocols);
	if (proto == NULL) {
		response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
		MHD_queue_response(con, MHD_HTTP_PRECONDITION_FAILED, response);
		MHD_destroy_response(response);
		return 1;
	}

	/* record context */
	memo = malloc(sizeof *memo);
	if (memo == NULL) {
		response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
		MHD_queue_response(con, MHD_HTTP_INTERNAL_SERVER_ERROR, response);
		MHD_destroy_response(response);
		return 1;
	}
	memo->proto = proto;
	memo->hreq = hreq;
	memo->apiset = apiset;

	/* send the accept connection */
	response = MHD_create_response_for_upgrade(upgrade_to_websocket, memo);
	make_accept_value(key, acceptval);
	MHD_add_response_header(response, sec_websocket_accept_s, acceptval);
	MHD_add_response_header(response, sec_websocket_protocol_s, proto->name);
	MHD_add_response_header(response, MHD_HTTP_HEADER_UPGRADE, websocket_s);
	MHD_queue_response(con, MHD_HTTP_SWITCHING_PROTOCOLS, response);
	MHD_destroy_response(response);

	return 1;
}

static const struct protodef protodefs[] = {
	{ "x-afb-ws-json1",	(void*)afb_ws_json1_create },
	{ NULL, NULL }
};

int afb_websock_check_upgrade(struct afb_hreq *hreq, struct afb_apiset *apiset)
{
	int rc;

	/* is a get ? */
	if (hreq->method != afb_method_get
	 || strcasecmp(hreq->version, MHD_HTTP_VERSION_1_1))
		return 0;

	rc = check_websocket_upgrade(hreq->connection, protodefs, hreq, apiset);
	if (rc == 1) {
		hreq->replied = 1;
	}
	return rc;
}

#endif
