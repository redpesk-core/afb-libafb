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

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>

#include "core/afb-apiname.h"
#include "core/afb-apiset.h"
#include "apis/afb-api-ws.h"
#include "misc/afb-socket.h"
#include "misc/afb-monitor.h"
#include "core/afb-ev-mgr.h"
#include "wsapi/afb-stub-ws.h"
#include "sys/verbose.h"
#include "sys/x-socket.h"
#include "sys/x-errno.h"

struct api_ws_server
{
	struct afb_apiset *apiset;	/* the apiset for calling */
	struct ev_fd *efd;		/* ev_fd handler */
	uint16_t offapi;		/* api name of the interface */
	char uri[];			/* the uri of the server socket */
};

/******************************************************************************/
/***       C L I E N T                                                      ***/
/******************************************************************************/

static void client_on_hangup(struct afb_stub_ws *client)
{
	const char *apiname = afb_stub_ws_apiname(client);
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

int afb_api_ws_add_client(const char *uri, struct afb_apiset *declare_set, struct afb_apiset *call_set, int strong)
{
	struct afb_stub_ws *stubws;
	const char *api;
	int rc, fd;

	/* check the api name */
	api = afb_socket_api(uri);
	if (api == NULL || !afb_apiname_is_valid(api)) {
		ERROR("invalid (too long) ws client uri %s", uri);
		rc = X_EINVAL;
		goto error;
	}

	/* open the socket */
	rc = afb_socket_open(uri, 0);
	if (rc >= 0) {
		/* create the client stub */
		fd = rc;
		stubws = afb_stub_ws_create_client(fd, api, call_set);
		if (!stubws) {
			ERROR("can't setup client ws service to %s", uri);
			close(fd);
			rc = X_ENOMEM;
		} else {
			if (afb_stub_ws_client_add(stubws, declare_set) >= 0) {
#if WITH_WSCLIENT_URI_COPY
				/* it is asserted here that uri is released, so use a copy */
				afb_stub_ws_client_robustify(stubws, reopen_client, strdup(uri), free);
#else
				/* it is asserted here that uri is never released */
				afb_stub_ws_client_robustify(stubws, reopen_client, (void*)uri, NULL);
#endif
				afb_stub_ws_set_on_hangup(stubws, client_on_hangup);
				return 0;
			}
			ERROR("can't add the client to the apiset for service %s", uri);
			afb_stub_ws_unref(stubws);
			rc = X_ENOMEM;
		}
	}
error:
	return strong ? rc : 0;
}

int afb_api_ws_add_client_strong(const char *uri, struct afb_apiset *declare_set, struct afb_apiset *call_set)
{
	return afb_api_ws_add_client(uri, declare_set, call_set, 1);
}

int afb_api_ws_add_client_weak(const char *uri, struct afb_apiset *declare_set, struct afb_apiset *call_set)
{
	return afb_api_ws_add_client(uri, declare_set, call_set, 0);
}

/*****************************************************************************/
/***       S E R V E R                                                      ***/
/******************************************************************************/

static void server_on_hangup(struct afb_stub_ws *server)
{
	const char *apiname = afb_stub_ws_apiname(server);
	INFO("Disconnection of client of API %s", apiname);
	afb_stub_ws_unref(server);
}

static void api_ws_server_accept(struct api_ws_server *apiws, int fd)
{
	int fdc;
	struct sockaddr addr;
	socklen_t lenaddr;
	struct afb_stub_ws *server;

	lenaddr = (socklen_t)sizeof addr;
	fdc = accept(fd, &addr, &lenaddr);
	if (fdc < 0) {
		ERROR("can't accept connection to %s: %m", apiws->uri);
	} else {
		server = afb_stub_ws_create_server(fdc, &apiws->uri[apiws->offapi], apiws->apiset);
		if (server)
			afb_stub_ws_set_on_hangup(server, server_on_hangup);
		else
			ERROR("can't serve accepted connection to %s", apiws->uri);
	}
}

static int api_ws_server_connect(struct api_ws_server *apiws);

static void api_ws_server_listen_callback(struct ev_fd *efd, int fd, uint32_t revents, void *closure)
{
	struct api_ws_server *apiws = closure;

	if ((revents & EPOLLHUP) != 0)
		api_ws_server_connect(apiws);
	else if ((revents & EPOLLIN) != 0)
		api_ws_server_accept(apiws, fd);
}

static void api_ws_server_disconnect(struct api_ws_server *apiws)
{
	ev_fd_unref(apiws->efd);
	apiws->efd = 0;
}

static int api_ws_server_connect(struct api_ws_server *apiws)
{
	int fd, rc;

	/* ensure disconnected */
	api_ws_server_disconnect(apiws);

	/* request the service object name */
	rc = afb_socket_open(apiws->uri, 1);
	if (rc < 0)
		ERROR("can't create socket %s", apiws->uri);
	else {
		/* listen for service */
		fd = rc;
		rc = afb_ev_mgr_add_fd(&apiws->efd, fd, EPOLLIN, api_ws_server_listen_callback, apiws, 0, 1);
		if (rc < 0) {
			close(fd);
			ERROR("can't connect socket %s", apiws->uri);
		}
	}
	return rc;
}

/* create the service */
int afb_api_ws_add_server(const char *uri, struct afb_apiset *declare_set, struct afb_apiset *call_set)
{
	int rc;
	const char *api;
	struct api_ws_server *apiws;
	size_t luri, lapi, extra;

	/* check the size */
	luri = strlen(uri);
	if (luri > 4000) {
		ERROR("can't create socket %s", uri);
		rc = X_E2BIG;
		goto error;
	}

	/* check the api name */
	api = afb_socket_api(uri);
	if (api == NULL || !afb_apiname_is_valid(api)) {
		ERROR("invalid api name in ws uri %s", uri);
		rc = X_EINVAL;
		goto error;
	}

	/* check api name */
	rc = afb_apiset_get_api(call_set, api, 1, 0, NULL);
	if (rc < 0) {
		ERROR("Can't provide ws-server for URI %s API %s", uri, api);
		goto error;
	}

	/* make the structure */
	lapi = strlen(api);
	extra = luri == (size_t)(api - uri) + lapi ? 0 : lapi + 1;
	apiws = malloc(sizeof * apiws + 1 + luri + extra);
	if (!apiws) {
		ERROR("out of memory");
		rc = X_ENOMEM;
		goto error;
	}

	apiws->apiset = afb_apiset_addref(call_set);
	apiws->efd = 0;
	strcpy(apiws->uri, uri);
	if (!extra)
		apiws->offapi = (uint16_t)(api - uri);
	else {
		apiws->offapi = (uint16_t)(luri + 1);
		strcpy(&apiws->uri[apiws->offapi], api);
	}

	/* connect for serving */
	rc = api_ws_server_connect(apiws);
	if (rc >= 0)
		return 0;

	afb_apiset_unref(apiws->apiset);
	free(apiws);
error:
	return rc;
}
