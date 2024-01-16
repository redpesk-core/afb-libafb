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

#if WITH_LIBMICROHTTPD

#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <microhttpd.h>

#include "http/afb-method.h"
#include "http/afb-hreq.h"
#include "http/afb-hsrv.h"
#include "http/afb-upgrade.h"
#include "http/afb-websock.h"
#include "sys/x-errno.h"

/**************** management of lists of upgrader definitions ****************************/

/**
* definition of an upgrader
*/
struct upgradedef
{
	/** name of the upgrader */
	const char *name;

	/** link to the nex upgrader definition */
	struct upgradedef *next;

	/** the upgrader */
	afb_upgrader_t upgrader;

	/** closure of the upgrader */
	void *closure;
};

static const struct upgradedef default_upgraders[] = {
	{
		.name     = afb_websocket_protocol_name,
		.next     = NULL,
		.upgrader = afb_websock_upgrader,
		.closure  = NULL
	}
};

/**
* check if 'upgdef' is a default protocol
*/
static inline int is_default_upgradedef(const struct upgradedef *upgdef)
{
	const uintptr_t begin = (uintptr_t)default_upgraders;
	const uintptr_t end = begin + (uintptr_t)(sizeof default_upgraders);
	const uintptr_t ptr = (uintptr_t)upgdef;
	return begin <= ptr && ptr < end;
}

/* see afb-upgrade.h */
void afb_upgrade_init_with_defaults(struct upgradedef **head)
{
	*head = (struct upgradedef*)default_upgraders;
}

/* see afb-upgrade.h */
int afb_upgrade_add(
		struct upgradedef **head,
		const char *name,
		afb_upgrader_t upgrader,
		void *closure
) {
	struct upgradedef *upgdef = malloc(sizeof *upgdef);
	if (upgdef == NULL)
		return X_ENOMEM;
	upgdef->name = name;
	upgdef->upgrader = upgrader;
	upgdef->closure = closure;
	upgdef->next = *head;
	*head = upgdef;
	return 0;
}

/* see afb-upgrade.h */
int afb_upgrade_remove(
		struct upgradedef **head,
		const char *name
) {
	for (;;) {
		struct upgradedef *upgdef = *head;
		if (upgdef == NULL)
			return X_ENOENT;
		if (is_default_upgradedef(upgdef))
			return 0;
		if (name == NULL || strcmp(name, upgdef->name) == 0) {
			*head = upgdef->next;
			free(upgdef);
			if (name != NULL)
				return 0;
		}
		head = &upgdef->next;
	}
}

/**************** makes the upgrade ****************************/

/*
* LIBMICROHTTPD requires the upgrade to be done as a calback in
* called in the reply
*/

/**
* memorisation for the wrapper of the callback
*/
struct upgrading
{
	/** the upgrade callback */
	afb_upgrade_cb_t callback;
	/** the HTTP request */
	struct afb_hreq *hreq;
	/** the call set */
	struct afb_apiset *apiset;
	/** closure of the callback */
	void *closure;
};

/** cleanup routine */
static void upgrade_end(void *closure)
{
	struct MHD_UpgradeResponseHandle *urh = closure;
	MHD_upgrade_action (urh, MHD_UPGRADE_ACTION_CLOSE);
}

/** wrapper of the callback */
static void upgrade_begin(
		void *cls,
		struct MHD_Connection *connection,
		void *con_cls,
		const char *extra_in,
		size_t extra_in_size,
		MHD_socket sock,
		struct MHD_UpgradeResponseHandle *urh
) {
	struct upgrading *upgrading = cls;
	int rc = upgrading->callback(upgrading->closure, upgrading->hreq, upgrading->apiset, sock, upgrade_end, urh);
	if (rc < 0) {
		/* TODO */
		upgrade_end(urh);
	}
#if MHD_VERSION <= 0x00095900
	afb_hreq_unref(upgrading->hreq);
#endif
	free(upgrading);
}

/* see afb-upgrade.h */
int afb_upgrade_reply(
		afb_upgrade_cb_t upgrdcb,
		void *closure,
		struct afb_hreq *hreq,
		struct afb_apiset *apiset,
		const char *protocol,
		unsigned count,
		const char *headval[]
) {
	struct MHD_Response *response;
	struct upgrading *upgrading;
	unsigned idx;

	upgrading = malloc(sizeof *upgrading);
	if (upgrading == NULL) {
		response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
		MHD_queue_response(hreq->connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response);
		MHD_destroy_response(response);
		return X_ENOMEM;
	}

	upgrading->callback = upgrdcb;
	upgrading->hreq = hreq;
	upgrading->apiset = apiset;
	upgrading->closure = closure;
	response = MHD_create_response_for_upgrade(upgrade_begin, upgrading);

	if (protocol != NULL)
		MHD_add_response_header(response, MHD_HTTP_HEADER_UPGRADE, protocol);
	for(idx = 0; idx < count; idx += 2)
		MHD_add_response_header(response, headval[idx], headval[idx + 1]);

	MHD_queue_response(hreq->connection, MHD_HTTP_SWITCHING_PROTOCOLS, response);
	MHD_destroy_response(response);
	return 1;
}

/**************** check upgrade ****************************/

static int headerhas(const char *header, const char *needle)
{
	size_t len, n;
	const char *vseparators = " \t,";

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

/**
* Check if the request is an upgrade request and if it is one, if upgrading is possible
*/
int afb_upgrade_check_upgrade(struct afb_hreq *hreq, struct afb_apiset *apiset)
{
	int rc;
	const char *connection, *upgrade;
	const struct upgradedef *iter;

	/* is a get and HTTP1.1 ?  */
	if (hreq->method != afb_method_get
	 || strcasecmp(hreq->version, MHD_HTTP_VERSION_1_1))
		return 0;

	/* is a connection for upgrade ? */
	connection = MHD_lookup_connection_value(hreq->connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_CONNECTION);
	if (connection == NULL || !headerhas (connection, MHD_HTTP_HEADER_UPGRADE))
		return 0;

	/* is the upgrade tag set ? */
	upgrade = MHD_lookup_connection_value(hreq->connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_UPGRADE);
	if (upgrade == NULL)
		return 0;

	/* search the upgrader */
	iter = afb_hsrv_upgraders(hreq->hsrv);
	while (iter != NULL) {
		if (!strcasecmp(upgrade, iter->name)) {
			/* do upgrade */
			rc = iter->upgrader(iter->closure, hreq, apiset);
			if (rc != 0) {
				hreq->replied = 1;
				return rc;
			}
		}
		iter = iter->next;
	}
	/* let report default status if upgrader is not found */
	return 0;
}


#endif
