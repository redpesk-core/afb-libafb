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

#include "libafb-config.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "core/afb-apiname.h"
#include "core/afb-apiset.h"
#include "apis/afb-api-rpc.h"
#include "core/afb-cred.h"
#include "misc/afb-socket.h"
#include "misc/afb-ws.h"
#include "misc/afb-monitor.h"
#include "core/afb-ev-mgr.h"
#include "wsapi/afb-stub-rpc.h"
#include "rpc/afb-rpc-coder.h"
#include "sys/verbose.h"
#include "sys/x-socket.h"
#include "sys/x-errno.h"
#include "sys/x-uio.h"

#define RECEIVE_BLOCK_LENGTH 4080
#define USE_SND_RCV          0          /* TODO make a mix, use what is possible rcv/snd if possible */
#define QUERY_RCV_SIZE       1          /* TODO is it to be continued ? */

/**
 * Structure holding server data
 */
struct server
{
	/** the apiset for calling */
	struct afb_apiset *apiset;

	/** ev_fd handler */
	struct ev_fd *efd;

	/** api name of the interface */
	uint16_t offapi;

	/** the uri of the server socket */
	char uri[];
};

/******************************************************************************/
/***       U R I   PREFIX                                                   ***/
/******************************************************************************/

static const char *prefix_ws_remove(const char *uri)
{
	return (uri[0] == 'w' && uri[1] == 's' && uri[2] == '+') ? &uri[3] : uri;
}

/******************************************************************************/
/***       C L I E N T                                                      ***/
/******************************************************************************/

#if 0 /* TODO manage reopening */
static void client_on_hangup(struct afb_stub_rpc *client)
{
	const char *apiname = afb_stub_rpc_apiname(client);
	WARNING("Disconnected of API %s", apiname);
	afb_monitor_api_disconnected(apiname);
}

static int reopen_client(void *closure)
{
	const char *uri = closure;
	const char *apiname = afb_socket_api(uri);
	int fd = afb_socket_open(uri, 0);
	if (fd >= 0)
		INFO("Reconnected to API %s", apiname);
	return fd;
}
#endif

static void onevent(struct ev_fd *efd, int fd, uint32_t revents, void *closure)
{
	struct afb_stub_rpc *stub = closure;
	void *buffer;
	size_t esz;
	ssize_t sz;
	int rc, avail;

	if (revents & EPOLLHUP) {
		afb_stub_rpc_unref(stub);
		ev_fd_unref(efd);
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
#if USE_SND_RCV
			sz = recv(fd, buffer, esz, MSG_DONTWAIT);
#else
			sz = read(fd, buffer, esz);
#endif
			if (sz >= 0) {
				if (esz > (size_t)sz) {
					void *newbuffer = realloc(buffer, (size_t)sz);
					if (newbuffer != NULL)
						buffer = newbuffer;
				}
				afb_stub_rpc_receive(stub, buffer, (size_t)sz);
			}
			else {
				free(buffer);
				buffer = NULL;
			}
		}
		if (buffer == NULL) {
			afb_stub_rpc_unref(stub);
			ev_fd_unref(efd);
		}
	}
}

static void notify(void *closure, struct afb_stub_rpc *stub)
{
	struct iovec iovs[AFB_RPC_OUTPUT_BUFFER_COUNT_MAX];
	afb_rpc_coder_t *coder = afb_stub_rpc_emit_coder(stub);
	int rc = afb_rpc_coder_output_get_iovec(coder, iovs, AFB_RPC_OUTPUT_BUFFER_COUNT_MAX);
	if (rc > 0) {
		int fd = (int)(intptr_t)closure;
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
		sendmsg(fd, &msg, 0);
#else
		writev(fd, iovs, rc);
#endif
		afb_rpc_coder_output_dispose(coder);
	}
}

static void disposebufs(void *closure, void *buffer, size_t size)
{
	free(buffer);
}

static void notify_ws(void *closure, struct afb_stub_rpc *stub)
{
	struct afb_ws *ws = closure;
	struct iovec iovs[AFB_RPC_OUTPUT_BUFFER_COUNT_MAX];
	afb_rpc_coder_t *coder = afb_stub_rpc_emit_coder(stub);
	int rc = afb_rpc_coder_output_get_iovec(coder, iovs, AFB_RPC_OUTPUT_BUFFER_COUNT_MAX);
	if (rc > 0) {
		afb_ws_binary_v(ws, iovs, rc);
		afb_rpc_coder_output_dispose(coder);
	}
}

static void on_ws_binary(void *closure, char *buffer, size_t size)
{
	struct afb_stub_rpc *stub = closure;
	afb_stub_rpc_receive(stub, buffer, size);
}

static void on_ws_hangup(void *closure)
{
	struct afb_stub_rpc *stub = closure;
	afb_stub_rpc_unref(stub);
	/* no way to remove afb-ws structure from here */
}

static struct afb_ws_itf wsitf =
{
	.on_close = 0,
	.on_text = 0,
	.on_binary = on_ws_binary,
	.on_error = 0,
	.on_hangup = on_ws_hangup
};

int afb_api_rpc_add_client(const char *uri, struct afb_apiset *declare_set, struct afb_apiset *call_set, int strong)
{
	struct afb_stub_rpc *stub;
	const char *api, *turi;
	int rc, fd;
	struct ev_fd *efd;

	/* check the api name */
	turi = prefix_ws_remove(uri);
	api = afb_socket_api(turi);
	if (api == NULL || !afb_apiname_is_valid(api)) {
		ERROR("invalid (too long) rpc client uri %s", uri);
		rc = X_EINVAL;
		goto error;
	}

	/* open the socket */
	rc = afb_socket_open(turi, 0);
	if (rc >= 0) {
		/* create the client stub */
		fd = rc;
		stub = afb_stub_rpc_create(api, call_set);
		if (!stub) {
			ERROR("can't create client rpc service to %s", uri);
			rc = X_ENOMEM;
		} else {
			if (uri == turi) {
				rc = afb_ev_mgr_add_fd(&efd, fd, EPOLLIN, onevent, stub, 0, 1);
				if (rc >= 0) {
					afb_stub_rpc_emit_set_notify(stub, notify, (void*)(intptr_t)fd);
					afb_stub_rpc_receive_set_dispose(stub, disposebufs, 0);
				}
			}
			else {
				struct afb_ws *ws = afb_ws_create(fd, &wsitf, stub);
				if (ws == NULL)
					rc = X_ENOMEM;
				else {
					afb_stub_rpc_emit_set_notify(stub, notify_ws, ws);
					afb_stub_rpc_receive_set_dispose(stub, disposebufs, 0);
					afb_stub_rpc_set_unpack(stub, 1);
					rc = 0;
				}
			}
			if (rc < 0)
				ERROR("can't setup client rpc service to %s", uri);
			else {
				rc = afb_stub_rpc_client_add(stub, declare_set);
				if (rc >= 0) {
					afb_stub_rpc_offer(stub);
					return 0;
				}
				ERROR("can't add the client to the apiset for service %s", uri);
			}
			afb_stub_rpc_unref(stub);
		}
		close(fd);
	}
error:
	return strong ? rc : 0;
}

int afb_api_rpc_add_client_strong(const char *uri, struct afb_apiset *declare_set, struct afb_apiset *call_set)
{
	return afb_api_rpc_add_client(uri, declare_set, call_set, 1);
}

int afb_api_rpc_add_client_weak(const char *uri, struct afb_apiset *declare_set, struct afb_apiset *call_set)
{
	return afb_api_rpc_add_client(uri, declare_set, call_set, 0);
}

/*****************************************************************************/
/***       S E R V E R                                                      ***/
/******************************************************************************/

#if JUNK
static void server_on_hangup(struct afb_stub_rpc *server)
{
	const char *apiname = afb_stub_rpc_apiname(server);
	INFO("Disconnection of client of API %s", apiname);
	afb_stub_rpc_unref(server);
}
#endif

static void server_accept(struct server *server, int fd)
{
	int fdc, rc;
	struct sockaddr addr;
	socklen_t lenaddr;
	struct afb_stub_rpc *stub;
	struct ev_fd *efd;

	lenaddr = (socklen_t)sizeof addr;
	fdc = accept(fd, &addr, &lenaddr);
	if (fdc < 0) {
		ERROR("can't accept connection to %s: %m", server->uri);
	} else {
		stub = afb_stub_rpc_create(&server->uri[server->offapi], server->apiset);
		if (!stub) {
			ERROR("can't serve accepted connection to %s", server->uri);
			close(fdc);
		}
		else {
#if WITH_CRED
			/*
			* creds of the peer are not changing
			* except if passed to other processes
			* TODO check how to track changes if needed
			*/
			struct afb_cred *cred;
			afb_cred_create_for_socket(&cred, fdc); /* TODO: check retcode */
			afb_stub_rpc_set_cred(stub, cred);
#endif
			if (server->uri == prefix_ws_remove(server->uri)) {
				rc = afb_ev_mgr_add_fd(&efd, fdc, EPOLLIN, onevent, stub, 0, 1);
				if (rc >= 0) {
					afb_stub_rpc_emit_set_notify(stub, notify, (void*)(intptr_t)fdc);
					afb_stub_rpc_receive_set_dispose(stub, disposebufs, 0);
				}
			}
			else {
				struct afb_ws *ws = afb_ws_create(fdc, &wsitf, stub);
				if (ws == NULL)
					rc = X_ENOMEM;
				else {
					afb_stub_rpc_emit_set_notify(stub, notify_ws, ws);
					afb_stub_rpc_receive_set_dispose(stub, disposebufs, 0);
					afb_stub_rpc_set_unpack(stub, 1);
					rc = 0;
				}
			}
			if (rc < 0) {
				ERROR("can't serve connection to %s", server->uri);
				afb_stub_rpc_unref(stub);
				close(fdc);
			}
		}
	}
}

static int server_connect(struct server *server);

static void server_listen_callback(struct ev_fd *efd, int fd, uint32_t revents, void *closure)
{
	struct server *server = closure;

	if ((revents & EPOLLHUP) != 0)
		server_connect(server);
	else if ((revents & EPOLLIN) != 0)
		server_accept(server, fd);
}

static void server_disconnect(struct server *server)
{
	ev_fd_unref(server->efd);
	server->efd = 0;
}

static int server_connect(struct server *server)
{
	int fd, rc;

	/* ensure disconnected */
	server_disconnect(server);

	/* request the service object name */
	rc = afb_socket_open(prefix_ws_remove(server->uri), 1);
	if (rc < 0)
		ERROR("can't create socket %s", server->uri);
	else {
		/* listen for service */
		fd = rc;
		rc = afb_ev_mgr_add_fd(&server->efd, fd, EPOLLIN, server_listen_callback, server, 0, 1);
		if (rc < 0) {
			close(fd);
			ERROR("can't connect socket %s", server->uri);
		}
	}
	return rc;
}

/* create the service */
int afb_api_rpc_add_server(const char *uri, struct afb_apiset *declare_set, struct afb_apiset *call_set)
{
	int rc;
	const char *api;
	struct server *server;
	size_t luri, lapi, extra;

	/* check the size */
	luri = strlen(uri);
	if (luri > 4000) {
		ERROR("can't create socket %s", uri);
		rc = X_E2BIG;
		goto error;
	}

	/* check the api name */
	api = afb_socket_api(prefix_ws_remove(uri));
	if (api == NULL || !afb_apiname_is_valid(api)) {
		ERROR("invalid api name in rpc uri %s", uri);
		rc = X_EINVAL;
		goto error;
	}

	/* check api name */
	rc = afb_apiset_get_api(call_set, api, 1, 0, NULL);
	if (rc < 0) {
		ERROR("Can't provide rpc-server for URI %s API %s", uri, api);
		goto error;
	}

	/* make the structure */
	lapi = strlen(api);
	extra = luri == (size_t)(api - uri) + lapi ? 0 : lapi + 1;
	server = malloc(sizeof * server + 1 + luri + extra);
	if (!server) {
		ERROR("out of memory");
		rc = X_ENOMEM;
		goto error;
	}

	server->apiset = afb_apiset_addref(call_set);
	server->efd = 0;
	strcpy(server->uri, uri);
	if (!extra)
		server->offapi = (uint16_t)(api - uri);
	else {
		server->offapi = (uint16_t)(luri + 1);
		strcpy(&server->uri[server->offapi], api);
	}

	/* connect for serving */
	rc = server_connect(server);
	if (rc >= 0)
		return 0;

	afb_apiset_unref(server->apiset);
	free(server);
error:
	return rc;
}
