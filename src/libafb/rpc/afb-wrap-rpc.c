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

#include "../libafb-config.h"

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/param.h>

#include <rp-utils/rp-verbose.h>

#include "sys/x-uio.h"

#include "misc/afb-ws.h"
#include "rpc/afb-rpc-coder.h"
#include "core/afb-ev-mgr.h"
#include "core/afb-cred.h"
#include "rpc/afb-stub-rpc.h"
#include "rpc/afb-wrap-rpc.h"

#include "tls/tls-gnu.h"

#define RECEIVE_BLOCK_LENGTH 4080
#define USE_SND_RCV          0          /* TODO make a mix, use what is possible rcv/snd if possible */
#define QUERY_RCV_SIZE       1          /* TODO is it to be continued ? */

#if USE_SND_RCV
# include <sys/socket.h>
#endif
/*
*/
struct afb_wrap_rpc
{
	struct afb_stub_rpc *stub;
	struct afb_ws *ws;
	struct ev_fd *efd;
#if WITH_GNUTLS
	gnutls_certificate_credentials_t gnutls_creds;
	gnutls_session_t gnutls_session;
#endif
};

/******************************************************************************/
/***       D I R E C T                                                      ***/
/******************************************************************************/

#if 0 /* TODO manage reopening */
static void client_on_hangup(struct afb_stub_rpc *client)
{
	const char *apiname = afb_stub_rpc_apiname(client);
	RP_WARNING("Disconnected of API %s", apiname);
	afb_monitor_api_disconnected(apiname);
}

static int reopen_client(void *closure)
{
	const char *uri = closure;
	const char *apiname = afb_uri_api_name(uri);
	int fd = afb_socket_open(uri, 0);
	if (fd >= 0)
		RP_INFO("Reconnected to API %s", apiname);
	return fd;
}
#endif

static void hangup(struct afb_wrap_rpc *wrap)
{
	afb_stub_rpc_unref(wrap->stub);
	if (wrap->efd != NULL)
		ev_fd_unref(wrap->efd);
	if (wrap->ws != NULL)
		{/*TODO*/}
	free(wrap);
}

static void onevent(struct ev_fd *efd, int fd, uint32_t revents, void *closure)
{
	struct afb_wrap_rpc *wrap = closure;
	void *buffer;
	size_t esz;
	ssize_t ssz;
	int rc, avail;

	if (revents & EPOLLHUP) {
		hangup(wrap);
	}
	else if (revents & EPOLLIN) {
#if QUERY_RCV_SIZE
		rc = ioctl(fd, FIONREAD, &avail);
		esz = rc < 0 ? RECEIVE_BLOCK_LENGTH : (size_t)(unsigned)avail;
#else
		esz = RECEIVE_BLOCK_LENGTH;
#endif
		buffer = malloc(esz);
		if (buffer != NULL) {
#if WITH_GNUTLS
			if (wrap->gnutls_session)
				ssz = gnutls_record_recv(wrap->gnutls_session, buffer, esz);
			else
#endif
			{
#if USE_SND_RCV
				ssz = recv(fd, buffer, esz, MSG_DONTWAIT);
#else
				ssz = read(fd, buffer, esz);
#endif
			}
			if (ssz >= 0) {
				if (esz > (size_t)ssz) {
					void *newbuffer = realloc(buffer, (size_t)ssz);
					if (newbuffer != NULL)
						buffer = newbuffer;
				}
				afb_stub_rpc_receive(wrap->stub, buffer, (size_t)ssz);
			}
			else {
				free(buffer);
				buffer = NULL;
			}
		}
		if (buffer == NULL) {
			hangup(wrap);
		}
	}
}

static void notify(void *closure, struct afb_rpc_coder *coder)
{
	ssize_t ssz;
	struct iovec iovs[AFB_RPC_OUTPUT_BUFFER_COUNT_MAX];
	struct afb_wrap_rpc *wrap = closure;
	int rc = afb_rpc_coder_output_get_iovec(coder, iovs, AFB_RPC_OUTPUT_BUFFER_COUNT_MAX);
	if (rc > 0) {
		int fd = ev_fd_fd(wrap->efd);
#if USE_SND_RCV
		struct msghdr msg = {
			.msg_name       = NULL,
			.msg_namelen    = 0,
			.msg_iov        = iovs,
			.msg_iovlen     = (unsigned)rc,
			.msg_control    = NULL,
			.msg_controllen = 0,
			.msg_flags      = 0
		};
		ssz = sendmsg(fd, &msg, 0);
#else
		ssz = writev(fd, iovs, rc);
#endif
		(void)ssz; /* TODO: hold the write error !!! */
		afb_rpc_coder_output_dispose(coder);
	}
}

static void disposebufs(void *closure, void *buffer, size_t size)
{
	free(buffer);
}

/******************************************************************************/
/***       T L S                                                            ***/
/******************************************************************************/

#if WITH_GNUTLS

static void notify_tls(void *closure, struct afb_rpc_coder *coder)
{
	ssize_t rc;
	uint32_t sz;
	struct afb_wrap_rpc *wrap = closure;
	size_t maxsz = gnutls_record_get_max_size(wrap->gnutls_session);

	/* check size of data to send */
	afb_rpc_coder_output_sizes(coder, &sz);
	if (sz > maxsz) {
		RP_ERROR("there's more data (%u) than the maximum TLS record size (%lu), packet won't be sent", sz, maxsz);
	}
	else {
		/* send */
		char buffer[sz];
		afb_rpc_coder_output_get_buffer(coder, buffer, sz);
		do {
			rc = gnutls_record_send(wrap->gnutls_session, buffer, sz);
		} while (rc == GNUTLS_E_AGAIN || rc == GNUTLS_E_INTERRUPTED);
	}

	afb_rpc_coder_output_dispose(coder);
}

#endif

/******************************************************************************/
/***       W E B S O C K E T                                                ***/
/******************************************************************************/

static void notify_ws(void *closure, struct afb_rpc_coder *coder)
{
	struct afb_wrap_rpc *wrap = closure;
	struct iovec iovs[AFB_RPC_OUTPUT_BUFFER_COUNT_MAX];
	int rc = afb_rpc_coder_output_get_iovec(coder, iovs, AFB_RPC_OUTPUT_BUFFER_COUNT_MAX);
	if (rc > 0) {
		afb_ws_binary_v(wrap->ws, iovs, rc);
		afb_rpc_coder_output_dispose(coder);
	}
}

static void on_ws_binary(void *closure, char *buffer, size_t size)
{
	struct afb_wrap_rpc *wrap = closure;
	afb_stub_rpc_receive(wrap->stub, buffer, size);
}

static void on_ws_hangup(void *closure)
{
	struct afb_wrap_rpc *wrap = closure;
	hangup(wrap);
	/* TODO no way to remove afb-ws structure from here */
}

static struct afb_ws_itf wsitf =
{
	.on_close = 0,
	.on_text = 0,
	.on_binary = on_ws_binary,
	.on_error = 0,
	.on_hangup = on_ws_hangup
};

/******************************************************************************/
/***       W E B S O C K E T                                                ***/
/******************************************************************************/

/**
* Initialize the wrapper
*/
static int init(
		struct afb_wrap_rpc *wrap,
		int fd,
		int autoclose,
		enum afb_wrap_rpc_mode mode,
		const char *apiname,
		struct afb_apiset *callset
) {
	ev_fd_cb_t onevent_cb = onevent;
	void (*notify_cb)(void*, struct afb_rpc_coder*) = notify;

	/* create the stub */
	int rc = afb_stub_rpc_create(&wrap->stub, apiname, callset);
	if (rc < 0) {
		if (autoclose)
			close(fd);
	} else {
		if (mode == Wrap_Rpc_Mode_Websocket) {
			/* websocket initialisation */
			wrap->efd = NULL;
			wrap->ws = afb_ws_create(fd, autoclose, &wsitf, wrap);
			if (wrap->ws == NULL)
				rc = X_ENOMEM;
			else {
				/* unpacking is required for websockets */
				afb_stub_rpc_set_unpack(wrap->stub, 1);
				/* callback for emission */
				afb_stub_rpc_emit_set_notify(wrap->stub, notify_ws, wrap);
				rc = 0;
			}
		}
		else {
			wrap->ws = NULL;

#if WITH_GNUTLS
			wrap->gnutls_session = NULL;
			if (mode == Wrap_Rpc_Mode_Tls_Client || mode == Wrap_Rpc_Mode_Tls_Server) {
				rc = tls_gnu_creds_init(&wrap->gnutls_creds);
				if (rc >= 0) {
					notify_cb = notify_tls;
					rc = tls_gnu_session_init(&wrap->gnutls_session, wrap->gnutls_creds, mode == Wrap_Rpc_Mode_Tls_Server, fd);
				}
			}
#endif

			/* direct initialisation */
			if (rc >= 0) {
				rc = afb_ev_mgr_add_fd(&wrap->efd, fd, EPOLLIN, onevent_cb, wrap, 0, autoclose);
				if (rc >= 0)
					/* callback for emission */
					afb_stub_rpc_emit_set_notify(wrap->stub, notify_cb, wrap);
			}
		}
		if (rc >= 0) {
			afb_stub_rpc_receive_set_dispose(wrap->stub, disposebufs, wrap);
			return 0;
		}
		afb_stub_rpc_unref(wrap->stub);
	}
	return rc;
}

/* creation of the wrapper */
int afb_wrap_rpc_create(
		struct afb_wrap_rpc **wrap,
		int fd,
		int autoclose,
		enum afb_wrap_rpc_mode mode,
		const char *apiname,
		struct afb_apiset *callset
) {
	int rc;
	*wrap = malloc(sizeof **wrap);
	if (*wrap == NULL) {
		if (autoclose)
			close(fd);
		rc = X_ENOMEM;
	}
	else {
		rc = init(*wrap, fd, autoclose, mode, apiname, callset);
		if (rc < 0) {
			free(*wrap);
			*wrap = NULL;
		}
	}
	return rc;
}

/* use the wrapper as client API */
int afb_wrap_rpc_start_client(struct afb_wrap_rpc *wrap, struct afb_apiset *declare_set)
{
	int rc = afb_stub_rpc_client_add(wrap->stub, declare_set);
	if (rc >= 0)
		afb_stub_rpc_offer_version(wrap->stub);
	return rc;
}

/* HTTP upgrade of the connection 'fd' to RPC/WS */
int afb_wrap_rpc_upgrade(
		void *closure,
		int fd,
		int autoclose,
		struct afb_apiset *callset,
		struct afb_session *session,
		struct afb_token *token,
		void (*cleanup)(void*),
		void *cleanup_closure,
		int websock
) {
	struct afb_wrap_rpc *wrap;
	enum afb_wrap_rpc_mode mode = websock ? Wrap_Rpc_Mode_Websocket : Wrap_Rpc_Mode_Raw;
	int rc = afb_wrap_rpc_create(&wrap, fd, autoclose, mode, NULL, callset);
	if (rc >= 0) {
		afb_stub_rpc_set_session(wrap->stub, session);
		afb_stub_rpc_set_token(wrap->stub, token);
	}
	return rc;
}

/* get apiname or NULL */
const char *afb_wrap_rpc_apiname(struct afb_wrap_rpc *wrap)
{
	return afb_stub_rpc_apiname(wrap->stub);
}

#if WITH_CRED
/* attach credentials to the wrapper */
void afb_wrap_rpc_set_cred(struct afb_wrap_rpc *wrap, struct afb_cred *cred)
{
	afb_stub_rpc_set_cred(wrap->stub, cred);
}
#endif

