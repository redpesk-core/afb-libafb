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
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <rp-utils/rp-verbose.h>

#include "core/afb-apiname.h"
#include "core/afb-apiset.h"
#include "apis/afb-api-rpc.h"
#include "core/afb-cred.h"
#include "misc/afb-socket.h"
#include "misc/afb-ws.h"
#include "misc/afb-monitor.h"
#include "core/afb-ev-mgr.h"
#include "rpc/afb-wrap-rpc.h"
#include "rpc/afb-rpc-coder.h"
#include "sys/x-socket.h"
#include "sys/x-errno.h"
#include "sys/x-uio.h"

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

#if 0
static void client_on_hangup(struct afb_wrap_rpc *client)
{
	const char *apiname = afb_wrap_rpc_apiname(client);
	RP_WARNING("Disconnected of API %s", apiname);
	afb_monitor_api_disconnected(apiname);
}
#endif

#if 0 /* TODO manage reopening */
static int reopen_client(void *closure)
{
	const char *uri = closure;
	const char *apiname = afb_socket_api(uri);
	int fd = afb_socket_open(uri, 0);
	if (fd >= 0)
		RP_INFO("Reconnected to API %s", apiname);
	return fd;
}
#endif

int afb_api_rpc_add_client(const char *uri, struct afb_apiset *declare_set, struct afb_apiset *call_set, int strong)
{
	struct afb_wrap_rpc *wrap;
	const char *apiname, *turi;
	int rc, fd, websock;

	/* check the api name */
	turi = prefix_ws_remove(uri);
	websock = turi != uri;
	apiname = afb_socket_api(turi);
	if (apiname == NULL || !afb_apiname_is_valid(apiname)) {
		RP_ERROR("invalid (too long) rpc client uri %s", uri);
		rc = X_EINVAL;
		goto error;
	}

	/* open the socket */
	rc = afb_socket_open(turi, 0);
	if (rc >= 0) {
		/* create the client wrap */
		fd = rc;
		rc = afb_wrap_rpc_create(&wrap, fd, 1, websock, apiname, call_set);
		if (rc >= 0) {
			rc = afb_wrap_rpc_start_client(wrap, declare_set);
			if (rc < 0)
				{/*TODO afb_wrap_rpc_unref(wrap); */}
		}
		if (rc < 0 && strong)
			RP_ERROR("can't create client rpc service to %s", uri);
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

#if 0
static void server_on_hangup(struct afb_wrap_rpc *server)
{
	const char *apiname = afb_wrap_rpc_apiname(server);
	RP_INFO("Disconnection of client of API %s", apiname ?: "*");
	afb_wrap_rpc_unref(server);
}
#endif

static void server_accept(struct server *server, int fd)
{
	int fdc, rc, websock;
	struct sockaddr addr;
	socklen_t lenaddr;
	struct afb_wrap_rpc *wrap;
	const char *apiname;

	lenaddr = (socklen_t)sizeof addr;
	fdc = accept(fd, &addr, &lenaddr);
	if (fdc < 0) {
		RP_ERROR("can't accept connection to %s: %m", server->uri);
	} else {
		rc = 1;
		setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &rc, (socklen_t)sizeof rc);
		websock = server->uri != prefix_ws_remove(server->uri);
		apiname = &server->uri[server->offapi];
		if (apiname[0] == 0)
			apiname = NULL;
		rc = afb_wrap_rpc_create(&wrap, fdc, 1, websock, apiname, server->apiset);
		if (rc < 0) {
			RP_ERROR("can't serve accepted connection to %s", server->uri);
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
			afb_wrap_rpc_set_cred(wrap, cred);
#endif
		}
	}
}

static void server_disconnect(struct server *server);
static int server_connect(struct server *server);

static int server_hangup(struct server *server)
{
	RP_ERROR("disconnection of server %s", server->uri);
	server_disconnect(server);
	RP_NOTICE("reconnection of server %s", server->uri);
	return server_connect(server);
}

static void server_listen_callback(struct ev_fd *efd, int fd, uint32_t revents, void *closure)
{
	struct server *server = closure;

	if ((revents & EPOLLHUP) != 0)
		server_hangup(server);
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

	/* request the service object name */
	rc = afb_socket_open(prefix_ws_remove(server->uri), 1);
	if (rc < 0)
		RP_ERROR("can't create socket %s", server->uri);
	else {
		/* listen for service */
		fd = rc;
		rc = afb_ev_mgr_add_fd(&server->efd, fd, EPOLLIN, server_listen_callback, server, 0, 1);
		if (rc < 0) {
			close(fd);
			RP_ERROR("can't connect socket %s", server->uri);
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
		RP_ERROR("can't create socket %s", uri);
		rc = X_E2BIG;
		goto error;
	}

	/* check the api name */
	api = afb_socket_api(prefix_ws_remove(uri));
	if (api == NULL || !afb_apiname_is_valid(api)) {
		RP_ERROR("invalid api name in rpc uri %s", uri);
		rc = X_EINVAL;
		goto error;
	}

	/* check api name */
	rc = afb_apiset_get_api(call_set, api, 1, 0, NULL);
	if (rc < 0) {
		RP_ERROR("Can't provide rpc-server for URI %s API %s", uri, api);
		goto error;
	}

	/* make the structure */
	lapi = strlen(api);
	extra = luri == (size_t)(api - uri) + lapi ? 0 : lapi + 1;
	server = malloc(sizeof * server + 1 + luri + extra);
	if (!server) {
		RP_ERROR("out of memory");
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
