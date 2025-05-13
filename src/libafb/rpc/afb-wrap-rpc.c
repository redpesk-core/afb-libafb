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

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>

#include <rp-utils/rp-escape.h>
#include <rp-utils/rp-verbose.h>

#include "sys/x-uio.h"
#include "sys/x-socket.h"

#include "misc/afb-uri.h"
#include "misc/afb-ws.h"
#include "misc/afb-vcomm.h"
#include "rpc/afb-rpc-coder.h"
#include "core/afb-ev-mgr.h"
#include "core/afb-cred.h"
#include "rpc/afb-stub-rpc.h"
#include "rpc/afb-wrap-rpc.h"

#if WITH_TLS
#  include "tls/tls.h"
#  ifndef TLS_SENDBUF_SIZE
#    define TLS_SENDBUF_SIZE 2048
#  endif
#endif

#ifndef RECEIVE_BLOCK_LENGTH
#  define RECEIVE_BLOCK_LENGTH 4080
#endif
#if __ZEPHYR__
#  undef USE_SND_RCV
#  undef QUERY_RCV_SIZE
#  define USE_SND_RCV          1
#  define QUERY_RCV_SIZE       0
#endif
#ifndef USE_SND_RCV
#  define USE_SND_RCV          0          /* TODO make a mix, use what is possible rcv/snd if possible */
#endif
#ifndef QUERY_RCV_SIZE
#  define QUERY_RCV_SIZE       1          /* TODO is it to be continued ? */
#endif

#if QUERY_RCV_SIZE
#  include <sys/ioctl.h>
#endif
#if USE_SND_RCV
# include <sys/socket.h>
#endif

/*
* structure for wrapping RPC
*/
struct afb_wrap_rpc
{
	/** the protocol stub handler */
	struct afb_stub_rpc *stub;

	/** the websocket handler or NULL */
	struct afb_ws *ws;

	/** the FD event handler or NULL */
	struct ev_fd *efd;
#if WITH_VCOMM
	/** the COM handler or NULL */
	struct afb_vcomm *vcomm;
#endif
	/** receiving handler */
	struct {
		/** the receiving buffer */
		uint8_t *buffer;
		/** the received size */
		size_t size;
		/** detection of in callback release */
		uint8_t dropped;
	}
		mem;

#if WITH_TLS
	/* Is TLS active? */
	bool use_tls;

	/* IP or hostname; necessary for handshakes.
	 * It must live as long as the session lives.
	 * Can be null but in that case no host check is performed */
	char *host;

	/** the TLS session data */
	tls_session_t tls_session;
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
#if WITH_VCOMM
	if (wrap->vcomm != NULL)
		afb_vcomm_close(wrap->vcomm);
#endif
#if WITH_TLS
	if (wrap->use_tls)
		tls_release(&wrap->tls_session);
	free(wrap->host);
#endif
	free(wrap->mem.buffer);
	free(wrap);
}

static void onevent_fd(struct ev_fd *efd, int fd, uint32_t revents, void *closure)
{
	struct afb_wrap_rpc *wrap = closure;
	uint8_t *buffer;
	size_t esz;
	ssize_t ssz;
#if QUERY_RCV_SIZE
	int rc, avail;
#endif

	/* hangup event? */
	if (revents & EV_FD_HUP) {
		hangup(wrap);
		return;
	}

	/* something to read? */
	if ((revents & EV_FD_IN) == 0)
		return; /* no */

	/* get size to read */
#if QUERY_RCV_SIZE
	rc = ioctl(fd, FIONREAD, &avail);
	esz = rc < 0 ? RECEIVE_BLOCK_LENGTH : (size_t)(unsigned)avail;
#else
	esz = RECEIVE_BLOCK_LENGTH;
#endif

	/* allocate memory */
	buffer = realloc(wrap->mem.buffer, wrap->mem.size + esz);
	if (buffer == NULL) {
		/* allocation failed */
		if (wrap->mem.size + esz == 0)
			wrap->mem.buffer = NULL;
		hangup(wrap);
		return;
	}
	wrap->mem.buffer = buffer;
	buffer += wrap->mem.size;
	wrap->mem.size += esz;

	/* read in buffer */
	for (;;) {
		ssz =
#if WITH_TLS
			wrap->use_tls ? tls_recv(&wrap->tls_session, buffer, esz) :
#endif
#if USE_SND_RCV
			recv(fd, buffer, esz, MSG_DONTWAIT);
#else
			read(fd, buffer, esz);
#endif
		/* read error? */
		if (ssz < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN)
				ssz = 0;
			else {
				wrap->mem.size = 0;
				hangup(wrap);
				return;
			}
		}

		/* shrink buffer if too big */
		if (esz > (size_t)ssz) {
			wrap->mem.size -= esz - (size_t)ssz;
			if (wrap->mem.size > 0) {
				buffer = realloc(wrap->mem.buffer, wrap->mem.size);
				if (buffer != NULL)
					wrap->mem.buffer = buffer;
			}
			break;
		}

#if QUERY_RCV_SIZE
		/* available data as returned by ioctl were read */
		if (ssz == 0)
			break; /* no more data */
#endif

		/* allocate more for reading more */
		buffer = realloc(wrap->mem.buffer, wrap->mem.size + RECEIVE_BLOCK_LENGTH);
		if (buffer == NULL)
			break; /* not this time, maybe later... */
		wrap->mem.buffer = buffer;
		buffer += wrap->mem.size;
		esz = RECEIVE_BLOCK_LENGTH;
		wrap->mem.size += RECEIVE_BLOCK_LENGTH;
	}

	/* nothing in!? */
	if (wrap->mem.size == 0)
		return;

	/* process the buffer */
	wrap->mem.dropped = 0;

	ssz = afb_stub_rpc_receive(wrap->stub, wrap->mem.buffer, wrap->mem.size);

	if (ssz < 0) {
		/* processing error */
		if (!wrap->mem.dropped)
			wrap->mem.buffer = NULL; /* not released yet */
		hangup(wrap);
		return;
	}

	/* incomplete message, expects more bytes */
	if (ssz == 0)
		return;

	/* check processed size */
	wrap->mem.size -= (size_t)ssz;
	if (wrap->mem.size == 0) {
		/* fully complete */
		if (wrap->mem.dropped)
			free(wrap->mem.buffer);
		wrap->mem.buffer = NULL;
	}
	else if (wrap->mem.dropped)
		memmove(wrap->mem.buffer, &wrap->mem.buffer[ssz], wrap->mem.size);
	else {
		/*
		* partially incomplete
		* copy is preferred to avoid changing addresses of pointers
		* even if downsizing memory with realloc normally returns the
		* same address, it is no guarantied by its specs
		*/
		buffer = malloc(wrap->mem.size);
		if (buffer == NULL) {
			wrap->mem.buffer = NULL; /* not released yet */
			hangup(wrap);
			return;
		}
		memcpy(buffer, &wrap->mem.buffer[ssz], wrap->mem.size);
		wrap->mem.buffer = buffer;
	}
}

static void notify_fd(void *closure, struct afb_rpc_coder *coder)
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
	struct afb_wrap_rpc *wrap = closure;
	if (buffer == wrap->mem.buffer)
		wrap->mem.dropped = 1; /* in receiving callback, provision incomplete case */
	else
		free(buffer);
}

/******************************************************************************/
/***       T L S                                                            ***/
/******************************************************************************/

#if WITH_TLS

static void notify_tls(void *closure, struct afb_rpc_coder *coder)
{
	struct afb_wrap_rpc *wrap = closure;
	ssize_t rc;
	uint32_t length, sz, off, wrt;
	char buffer[TLS_SENDBUF_SIZE];

	afb_rpc_coder_output_sizes(coder, &length);
	for (off = 0 ; off < length ; off += sz) {
		sz = afb_rpc_coder_output_get_subbuffer(coder, buffer,
						(uint32_t)sizeof buffer, off);
		wrt = 0;
		for (wrt = 0 ; wrt < sz ; wrt += (uint32_t)rc) {
			rc = tls_send(&wrap->tls_session, &buffer[wrt], sz - wrt);
			if (rc <= 0)
				goto end;
		}
	}
end:
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

/* websocket initialisation */
static int init_ws(struct afb_wrap_rpc *wrap, int fd, int autoclose)
{
	wrap->ws = afb_ws_create(fd, autoclose, &wsitf, wrap);
	if (wrap->ws == NULL)
		return X_ENOMEM;

	/* unpacking is required for websockets */
	afb_stub_rpc_set_unpack(wrap->stub, 1);
	/* callback for emission */
	afb_stub_rpc_emit_set_notify(wrap->stub, notify_ws, wrap);
	return 0;
}

static int init_fd(struct afb_wrap_rpc *wrap, int fd, int autoclose, void (*notify_cb)(void*, struct afb_rpc_coder*))
{
	int rc = afb_ev_mgr_add_fd(&wrap->efd, fd, EV_FD_IN, onevent_fd, wrap, 0, autoclose);
	if (rc >= 0)
		/* callback for emission */
		afb_stub_rpc_emit_set_notify(wrap->stub, notify_cb, wrap);

	return rc;
}

#if WITH_TLS
static int init_tls(struct afb_wrap_rpc *wrap, const char *uri, enum afb_wrap_rpc_mode mode, int fd, int autoclose)
{
	int rc;
	const char *cert_path, *key_path, *trust_path, *hostname, *host, *host_end, *argsstr, **args;
	size_t host_len;
	bool server = !!(mode & Wrap_Rpc_Mode_Server_Bit);
	bool mutual = !!(mode & Wrap_Rpc_Mode_Mutual_Bit);

	wrap->use_tls = false;

	/* get cert & key from uri query arguments */
	cert_path = key_path = trust_path = hostname = NULL;
	args = NULL;
	argsstr = strchr(uri, '?');
	if (argsstr != NULL && *argsstr != 0) {
		args = rp_unescape_args(argsstr + 1);
		cert_path = rp_unescaped_args_get(args, "cert");
		key_path = rp_unescaped_args_get(args, "key");
		trust_path = rp_unescaped_args_get(args, "trust");
		hostname = rp_unescaped_args_get(args, "host");
	}
	if (server && (cert_path == NULL || key_path == NULL)) {
		free(args);
		RP_ERROR("RPC server sockspec %s should have both cert and key parameter", uri);
		return X_EINVAL;
	}

	/* get the name of the host */
	if (hostname != NULL) {
		if (*hostname == '\0')
			wrap->host = NULL;
		else {
			wrap->host = strdup(hostname);
			if (wrap->host == NULL)
				goto oom_host;
		}
	}
	else {
		host = strchr(uri, ':') + 1;
		host_end = strchr(host, ':');
		host_len = (size_t)(host_end - host);
		wrap->host = malloc(host_len + 1);
		if (wrap->host == NULL)
			goto oom_host;
		strncpy(wrap->host, host, host_len);
		wrap->host[host_len] = '\0';
	}

	/* setup TLS crypto material */
#if !WITHOUT_FILESYSTEM
	if (cert_path != NULL)
		tls_load_cert(cert_path);
	if (key_path != NULL)
		tls_load_key(key_path);
	if (trust_path != NULL)
		tls_load_trust(trust_path);
#endif
	if ((!server || mutual) && !tls_has_trust())
		/* use default system trust */
		tls_load_trust(NULL);

	/* setup TLS session */
	rc = tls_session_create(&wrap->tls_session, fd, server, mutual, wrap->host);
	if (rc >= 0) {
		rc = init_fd(wrap, fd, autoclose, notify_tls);
		if (rc < 0)
			tls_release(&wrap->tls_session);
	}

	/* log status */
	if (rc >= 0) {
		wrap->use_tls = true;
		RP_INFO("Created %s %s session for %s",
					mutual ? "mTLS" : "TLS",
					server ? "server" : "client",
					uri);
	}
	else {
		RP_ERROR("Can't create %s %s session for %s",
					mutual ? "mTLS" : "TLS",
					server ? "server" : "client",
					uri);
		free(wrap->host);
		wrap->host = NULL;
	}

end:
	/* cleanup */
	free(args);
	return rc;

oom_host:
	RP_ERROR("out of memory");
	rc = X_ENOMEM;
	goto end;
}
#endif

/**
* Initialize the wrapper
*/
static int init(
		struct afb_wrap_rpc *wrap,
		int fd,
		int autoclose,
		enum afb_wrap_rpc_mode mode,
		const char *uri,
		const char *apiname,
		struct afb_apiset *callset
) {

	/* create the stub */
	int rc = afb_stub_rpc_create(&wrap->stub, apiname, callset);
	if (rc < 0) {
		if (autoclose)
			close(fd);
	}
	else {
		if (mode == Wrap_Rpc_Mode_Websocket) {
			wrap->efd = NULL;
			rc = init_ws(wrap, fd, autoclose);
		}
		else {
			wrap->ws = NULL;
#if WITH_TLS
			if (mode & Wrap_Rpc_Mode_Tls_Bit)
				rc = init_tls(wrap, uri, mode, fd, autoclose);
			else
#endif
				rc = init_fd(wrap, fd, autoclose, notify_fd);
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
int afb_wrap_rpc_create_fd(
		struct afb_wrap_rpc **wrap,
		int fd,
		int autoclose,
		enum afb_wrap_rpc_mode mode,
		const char *uri,
		const char *apiname,
		struct afb_apiset *callset
) {
	int rc;
	*wrap = calloc(1, sizeof **wrap);
	if (*wrap == NULL) {
		if (autoclose)
			close(fd);
		rc = X_ENOMEM;
	}
	else {
		rc = init(*wrap, fd, autoclose, mode, uri, apiname, callset);
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
int afb_wrap_rpc_websocket_upgrade(
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
	enum afb_wrap_rpc_mode mode = websock ? Wrap_Rpc_Mode_Websocket : Wrap_Rpc_Mode_FD;
	int rc = afb_wrap_rpc_create_fd(&wrap, fd, autoclose, mode, NULL, NULL, callset);
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


#if WITH_VCOMM

/******************************************************************************/
/***       V C O M M                                                        ***/
/******************************************************************************/

static void onevent_vcomm(void *closure, const void *data, size_t size)
{
	struct afb_wrap_rpc *wrap = closure;
	uint8_t *buffer;
	size_t esz;
	ssize_t ssz;

	/* allocate memory */
	esz = size;
	buffer = realloc(wrap->mem.buffer, wrap->mem.size + esz);
	if (buffer == NULL) {
		/* allocation failed */
		if (wrap->mem.size + esz == 0)
			wrap->mem.buffer = NULL;
		hangup(wrap);
		return;
	}
	wrap->mem.buffer = buffer;
	buffer += wrap->mem.size;
	wrap->mem.size += esz;

	/* read in buffer */
	memcpy(buffer, data, esz);

	/* nothing in!? */
	if (wrap->mem.size == 0)
		return;

	/* process the buffer */
	wrap->mem.dropped = 0;

	ssz = afb_stub_rpc_receive(wrap->stub, wrap->mem.buffer, wrap->mem.size);

	if (ssz < 0) {
		/* processing error */
		if (!wrap->mem.dropped)
			wrap->mem.buffer = NULL; /* not released yet */
		hangup(wrap);
		return;
	}

	/* fully incomplete */
	if (ssz == 0)
		return;

	/* check processed size */
	wrap->mem.size -= (size_t)ssz;
	if (wrap->mem.size == 0)
		/* fully complete */
		buffer = NULL;
	else if (wrap->mem.dropped) {
		memmove(wrap->mem.buffer, &wrap->mem.buffer[ssz], wrap->mem.size);
		wrap->mem.dropped = 0;
	}
	else {
		/*
		* partially incomplete
		* copy is preferred to avoid changing addresses of pointers
		* even if downsizing memory with realloc normally returns the
		* same address, it is no guarantied by its specs
		*/
		buffer = malloc(wrap->mem.size);
		if (buffer == NULL) {
			hangup(wrap);
			return;
		}
		memcpy(buffer, &wrap->mem.buffer[ssz], wrap->mem.size);
	}
	if (wrap->mem.dropped)
		free(wrap->mem.buffer);
	wrap->mem.buffer = buffer;
}

/**
* Send the content to the connection
*/
static void notify_vcomm(void *closure, struct afb_rpc_coder *coder)
{
	struct afb_wrap_rpc *wrap = closure;
	struct afb_vcomm *vcomm = wrap->vcomm;
	uint32_t size;
	void *buffer;
	int rc;

	afb_rpc_coder_output_sizes(coder, &size);
	rc = afb_vcomm_get_tx_buffer(vcomm, &buffer, size);
	if (rc < 0)
		RP_ERROR("Failed to get a send buffer for %u bytes", (unsigned)size);
	else {
		afb_rpc_coder_output_get_buffer(coder, buffer, size);
		rc = afb_vcomm_send_nocopy(vcomm, buffer, size);
		if (rc < 0) {
			RP_ERROR("Failed to send a buffer of %u bytes", (unsigned)size);
			afb_vcomm_drop_tx_buffer(vcomm, buffer);
		}
		afb_rpc_coder_output_dispose(coder);
	}
}

/**
* Initialize the wrapper
*/
static int init_vcomm(
		struct afb_wrap_rpc *wrap,
		struct afb_vcomm *vcomm,
		enum afb_wrap_rpc_mode mode,
		const char *apiname,
		struct afb_apiset *callset
) {
	/* create the stub */
	int rc = afb_stub_rpc_create(&wrap->stub, apiname, callset);
	if (rc >= 0) {
		wrap->vcomm = vcomm;
		rc = afb_vcomm_on_message(vcomm, onevent_vcomm, wrap);
		if (rc >= 0)
			/* callback for emission */
			afb_stub_rpc_emit_set_notify(wrap->stub, notify_vcomm, wrap);
	}
	return rc;
}

/* wrap a vcomm */
int afb_wrap_rpc_create_vcomm(
		struct afb_wrap_rpc **wrap,
		struct afb_vcomm *vcomm,
		const char *apiname,
		struct afb_apiset *callset
) {
	int rc;
	*wrap = calloc(1, sizeof **wrap);
	if (*wrap == NULL)
		rc = X_ENOMEM;
	else {
		rc = init_vcomm(*wrap, vcomm, 0, apiname, callset);
		if (rc < 0) {
			free(*wrap);
			*wrap = NULL;
		}
	}
	return rc;
}

#endif
