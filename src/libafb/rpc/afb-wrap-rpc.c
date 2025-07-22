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
#include "rpc/afb-rpc-spec.h"
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

	/** recorded mode */
	enum afb_wrap_rpc_mode mode;

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

	/* robustify */
	struct {
		int (*reopen)(void*);
		void *closure;
		void (*release)(void*);
	} robust;
};

/* for reconnection */
static int reconnect(struct afb_wrap_rpc *wrap);

/******************************************************************************/
/***       D I R E C T                                                      ***/
/******************************************************************************/

static void disconnect(struct afb_wrap_rpc *wrap)
{
	bool was_connected = false;
#if WITH_TLS
	if (wrap->use_tls) {
		tls_release(&wrap->tls_session);
		wrap->use_tls = false;
		was_connected = true;
	}
#endif
	if (wrap->efd != NULL) {
		ev_fd_unref(wrap->efd);
		wrap->efd = NULL;
		was_connected = true;
	}
	if (wrap->ws != NULL) {
		afb_ws_destroy(wrap->ws);
		wrap->ws = NULL;
		was_connected = true;
	}
#if WITH_VCOMM
	if (wrap->vcomm != NULL) {
		afb_vcomm_close(wrap->vcomm);
		wrap->vcomm = NULL;
		was_connected = true;
	}
#endif
	if (was_connected && wrap->stub != NULL)
		afb_stub_rpc_disconnected(wrap->stub);
}

static void destroy(struct afb_wrap_rpc *wrap)
{
	disconnect(wrap);
	afb_stub_rpc_unref(wrap->stub);
	if (wrap->robust.release != NULL)
		wrap->robust.release(wrap->robust.closure);
#if WITH_TLS
	free(wrap->host);
#endif
	free(wrap->mem.buffer);
	free(wrap);
}

static void hangup(struct afb_wrap_rpc *wrap)
{
	if (wrap->robust.reopen == NULL)
		destroy(wrap);
	else
		disconnect(wrap);
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

static int notify_fd(void *closure, struct afb_rpc_coder *coder)
{
	ssize_t ssz;
	struct iovec iovs[AFB_RPC_OUTPUT_BUFFER_COUNT_MAX];
	struct afb_wrap_rpc *wrap = closure;
	int rc = 0;

	if (wrap->efd == NULL)
		rc = reconnect(wrap);
	if (rc >= 0) {
		rc = afb_rpc_coder_output_get_iovec(coder, iovs, AFB_RPC_OUTPUT_BUFFER_COUNT_MAX);
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
			do {
				ssz = sendmsg(fd, &msg, 0);
			} while (ssz < 0 && errno == EINTR);
#else
			do {
				ssz = writev(fd, iovs, rc);
			} while (ssz < 0 && errno == EINTR);
#endif
			if (ssz < 0) {
				if (errno == EPIPE)
					hangup(wrap);
				rc = X_EPIPE;
			}
			afb_rpc_coder_output_dispose(coder);
		}
	}
	return rc;
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

static int notify_tls(void *closure, struct afb_rpc_coder *coder)
{
	struct afb_wrap_rpc *wrap = closure;
	ssize_t ssz;
	uint32_t length, sz, off, wrt;
	char buffer[TLS_SENDBUF_SIZE];
	int rc = 0;

	/* detect deconnection */
	if (wrap->efd == NULL) {
		/* try reconnection */
		rc = reconnect(wrap);
		if (rc >= 0 && wrap->efd == NULL) {
			hangup(wrap);
			rc = X_ENOTSUP;
		}
		if (rc < 0)
			return rc;
	}

	afb_rpc_coder_output_sizes(coder, &length);
	for (off = 0 ; off < length ; off += sz) {
		sz = afb_rpc_coder_output_get_subbuffer(coder, buffer,
						(uint32_t)sizeof buffer, off);
		for (wrt = 0 ; wrt < sz ; wrt += (uint32_t)ssz) {
			ssz = tls_send(&wrap->tls_session, &buffer[wrt], sz - wrt);
			if (ssz <= 0) {
				afb_rpc_coder_output_dispose(coder);
				return -1;
			}
		}
	}
	afb_rpc_coder_output_dispose(coder);
	return 0;
}

#endif

/******************************************************************************/
/***       W E B S O C K E T                                                ***/
/******************************************************************************/

static void disposews(void *closure, void *buffer, size_t size)
{
	free(buffer);
}

static int notify_ws(void *closure, struct afb_rpc_coder *coder)
{
	struct afb_wrap_rpc *wrap = closure;
	struct iovec iovs[AFB_RPC_OUTPUT_BUFFER_COUNT_MAX];
	int rc = afb_rpc_coder_output_get_iovec(coder, iovs, AFB_RPC_OUTPUT_BUFFER_COUNT_MAX);
	if (rc > 0) {
		afb_ws_binary_v(wrap->ws, iovs, rc);
		afb_rpc_coder_output_dispose(coder);
	}
	return rc;
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
/***       I N I T I A L I Z A T I O N                                      ***/
/******************************************************************************/

/* websocket initialisation */
static int init_ws(struct afb_wrap_rpc *wrap, int fd, int autoclose)
{
	/* unpacking is required for websockets */
	afb_stub_rpc_set_unpack(wrap->stub, 1);
	/* callback for emission */
	afb_stub_rpc_emit_set_notify(wrap->stub, notify_ws, wrap);
	/* callback for releasing reception */
	afb_stub_rpc_receive_set_dispose(wrap->stub, disposews, wrap);

	/* attach WebSocket */
	wrap->efd = NULL;
	wrap->ws = afb_ws_create(fd, autoclose, &wsitf, wrap);
	return wrap->ws == NULL ? X_ENOMEM : 0;
}

/* file descriptor initialisation */
static int init_fd(
		struct afb_wrap_rpc *wrap,
		int fd,
		int autoclose,
		int (*notify_cb)(void*, struct afb_rpc_coder*)
) {
	/* packing is possible */
	afb_stub_rpc_set_unpack(wrap->stub, 0);
	/* callback for emission */
	afb_stub_rpc_emit_set_notify(wrap->stub, notify_cb, wrap);
	/* callback for releasing reception */
	afb_stub_rpc_receive_set_dispose(wrap->stub, disposebufs, wrap);

	/* attach file desriptor */
	wrap->ws = NULL;
	wrap->efd = NULL;
	if (fd < 0) /* case of lazy init */
		return 0;
	return afb_ev_mgr_add_fd(&wrap->efd, fd, EV_FD_IN,
	                         onevent_fd, wrap, 0, autoclose);
}

#if WITH_TLS
static int init_tls(
		struct afb_wrap_rpc *wrap,
		int fd,
		int autoclose,
		enum afb_wrap_rpc_mode mode,
		const char *uri
) {
	int rc;
	bool server = !!(mode & Wrap_Rpc_Mode_Server_Bit);
	bool mutual = !!(mode & Wrap_Rpc_Mode_Mutual_Bit);

	if (uri != NULL) {
		const char *cert_path, *key_path, *trust_path;
		const char *hostname, *host, *host_end, *argsstr;
		const char **args = NULL;
		size_t host_len;

		/* get cert & key from uri query arguments */
		cert_path = key_path = trust_path = hostname = NULL;
		argsstr = strchr(uri, '?');
		if (argsstr != NULL && *argsstr != 0) {
			args = rp_unescape_args(argsstr + 1);
			cert_path = rp_unescaped_args_get(args, "cert");
			key_path = rp_unescaped_args_get(args, "key");
			trust_path = rp_unescaped_args_get(args, "trust");
			hostname = rp_unescaped_args_get(args, "host");
		}

		/* get the name of the host */
		if (hostname != NULL) {
			if (*hostname == '\0')
				wrap->host = NULL;
			else {
				wrap->host = strdup(hostname);
				if (wrap->host == NULL) {
oom_host:
					free(args);
					RP_ERROR("out of memory");
					return X_ENOMEM;
				}
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
		if ((!server || mutual) && !tls_has_trust())
			/* use default system trust */
			tls_load_trust(NULL);
#endif
		free(args);
	}

	/* setup TLS session */
	if (fd < 0)
		rc = 0;
	else {
		rc = tls_session_create(&wrap->tls_session, fd,
		                        server, mutual, wrap->host);
		wrap->use_tls = rc >= 0;
	}
	if (rc >= 0) {
		rc = init_fd(wrap, fd, autoclose, notify_tls);
		if (rc < 0 && wrap->use_tls) {
			wrap->use_tls = false;
			tls_release(&wrap->tls_session);
		}
	}

	/* log status */
	if (rc >= 0) {
		RP_INFO("Created %s %s session for %s",
					mutual ? "mTLS" : "TLS",
					server ? "server" : "client",
					uri ?: "(reopened)");
	}
	else {
		RP_ERROR("Can't create %s %s session for %s",
					mutual ? "mTLS" : "TLS",
					server ? "server" : "client",
					uri ?: "(reopened)");
		if (uri != NULL) {
			free(wrap->host);
			wrap->host = NULL;
		}
	}

	/* cleanup */
	return rc;

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
		const char *uri
) {
	int rc;
	if (mode == Wrap_Rpc_Mode_Websocket)
		rc = init_ws(wrap, fd, autoclose);
	else {
#if WITH_TLS
		if (mode & Wrap_Rpc_Mode_Tls_Bit)
			rc = init_tls(wrap, fd, autoclose, mode, uri);
		else
#endif
			rc = init_fd(wrap, fd, autoclose, notify_fd);
	}
	return rc;
}

static int reconnect(struct afb_wrap_rpc *wrap)
{
	int rc;
	if (wrap->robust.reopen == NULL)
		rc = X_EPIPE;
	else {
		rc = wrap->robust.reopen(wrap->robust.closure);
		if (rc >= 0)
			rc = init(wrap, rc, 1, wrap->mode, NULL);
	}
	return rc;
}

/* creation of the wrapper */
int afb_wrap_rpc_create_fd(
		struct afb_wrap_rpc **result,
		int fd,
		int autoclose,
		enum afb_wrap_rpc_mode mode,
		const char *uri,
		struct afb_rpc_spec *spec,
		struct afb_apiset *callset
) {
	int rc;
	struct afb_wrap_rpc *wrap;

	wrap = calloc(1, sizeof *wrap);
	if (wrap == NULL) {
		if (autoclose)
			close(fd);
		rc = X_ENOMEM;
	}
	else {
		rc = afb_stub_rpc_create(&wrap->stub, spec, callset);
		if (rc < 0) {
			if (autoclose)
				close(fd);
		}
		else {
			rc = init(wrap, fd, autoclose, mode, uri);
			if (rc >= 0) {
				wrap->mode = mode;
				*result = wrap;
				return rc;
			}
			afb_stub_rpc_unref(wrap->stub);
		}
		free(wrap);
	}
	*result = NULL;
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

/* set robustification functions */
void afb_wrap_rpc_fd_robustify(
	struct afb_wrap_rpc *wrap,
	int (*reopen)(void*),
	void *closure,
	void (*release)(void*)
) {
	if (wrap->robust.release)
		wrap->robust.release(wrap->robust.closure);

	wrap->robust.reopen = reopen;
	wrap->robust.closure = closure;
	wrap->robust.release = release;
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
static int notify_vcomm(void *closure, struct afb_rpc_coder *coder)
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
	return rc;
}

/**
* Initialize the wrapper
*/
static int init_vcomm(
		struct afb_wrap_rpc *wrap,
		struct afb_vcomm *vcomm,
		enum afb_wrap_rpc_mode mode,
		struct afb_rpc_spec *spec,
		struct afb_apiset *callset
) {
	/* create the stub */
	int rc = afb_stub_rpc_create(&wrap->stub, spec, callset);
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
		struct afb_rpc_spec *spec,
		struct afb_apiset *callset
) {
	int rc;
	*wrap = calloc(1, sizeof **wrap);
	if (*wrap == NULL)
		rc = X_ENOMEM;
	else {
		rc = init_vcomm(*wrap, vcomm, 0, spec, callset);
		if (rc < 0) {
			free(*wrap);
			*wrap = NULL;
		}
	}
	return rc;
}

#endif
