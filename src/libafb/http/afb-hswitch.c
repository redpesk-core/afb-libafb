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
#include <string.h>

#include <microhttpd.h>

#include "core/afb-context.h"
#include "core/afb-apiset.h"
#include "core/afb-session.h"
#include "http/afb-hreq.h"
#include "http/afb-websock.h"

int afb_hswitch_apis(struct afb_hreq *hreq, void *data)
{
	const char *api, *verb, *i;
	size_t lenapi, lenverb;
	struct afb_apiset *apiset = data;

	/* api is the first hierarchical item */
	i = hreq->tail;
	while (*i == '/')
		i++;
	if (!*i)
		return 0; /* no API */
	api = i;

	/* search end of the api and get its length */
	while (*++i && *i != '/');
	lenapi = (size_t)(i - api);

	/* search the verb */
	while (*i == '/')
		i++;
	if (!*i)
		return 0; /* no verb */
	verb = i;

	/* get the verb length */
	while (*++i);
	lenverb = (size_t)(i - verb);

	/* found api + verb so process the call */
	afb_hreq_call(hreq, apiset, api, lenapi, verb, lenverb);
	return 1;
}

int afb_hswitch_one_page_api_redirect(struct afb_hreq *hreq, void *data)
{
	size_t plen;
	char *url;

	if (hreq->lentail >= 2 && hreq->tail[1] == '#')
		return 0;
	/*
	 * Here we have for example:
	 *    url  = "/pre/dir/page"   lenurl = 13
	 *    tail =     "/dir/page"   lentail = 9
	 *
	 * We will produce "/pre/#!dir/page"
	 *
	 * Let compute plen that include the / at end (for "/pre/")
	 */
	plen = hreq->lenurl - hreq->lentail + 1;
	url = alloca(hreq->lenurl + 3);
	memcpy(url, hreq->url, plen);
	url[plen++] = '#';
	url[plen++] = '!';
	memcpy(&url[plen], &hreq->tail[1], hreq->lentail);
	afb_hreq_redirect_to(hreq, url, 1);
	return 1;
}

int afb_hswitch_websocket_switch(struct afb_hreq *hreq, void *data)
{
	struct afb_apiset *apiset = data;

	if (hreq->lentail != 0)
		return 0;

	if (afb_hreq_init_context(hreq) < 0) {
		afb_hreq_reply_error(hreq, MHD_HTTP_INTERNAL_SERVER_ERROR);
		return 1;
	}

	return afb_websock_check_upgrade(hreq, apiset);
}

#endif
