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

#pragma once

#include "../libafb-config.h"

#if WITH_LIBMICROHTTPD

#include <stddef.h>

struct json_object;
struct json_tokener;

#include "../core/afb-req-common.h"

struct afb_session;
struct hreq_data;
struct afb_hsrv;
struct locale_search;

/**
 * Record of an HTTP query
 */
struct afb_hreq {
	struct afb_req_common comreq;
	struct afb_hsrv *hsrv;
	const char *cacheTimeout;
	struct MHD_Connection *connection;
	int method;
	int reqid;
	int scanned;
	int suspended;
	int replied;
	const char *version;
	const char *lang;
	const char *url;
	size_t lenurl;
	const char *tail;
	size_t lentail;
	struct MHD_PostProcessor *postform;
	struct hreq_data *data;
	struct json_object *json;
	struct json_tokener *tokener;
};

extern int afb_hreq_unprefix(struct afb_hreq *request, const char *prefix, size_t length);

extern int afb_hreq_valid_tail(struct afb_hreq *request);

extern void afb_hreq_reply_error(struct afb_hreq *request, unsigned int status);

extern int afb_hreq_reply_file(struct afb_hreq *hreq, const char *filename, int relax);

#if WITH_OPENAT
extern int afb_hreq_reply_file_at_if_exist(struct afb_hreq *request, int dirfd, const char *filename);

extern int afb_hreq_reply_file_at(struct afb_hreq *request, int dirfd, const char *filename);
#endif

extern int afb_hreq_reply_locale_file_if_exist(struct afb_hreq *hreq, struct locale_search *search, const char *filename);

extern int afb_hreq_reply_locale_file(struct afb_hreq *hreq, struct locale_search *search, const char *filename);

/**
 * Send a REDIRECT HTTP response
 *
 * @param hreq the HTTP request handler
 * @param url the URL to redirect to
 * @param add_query_part if not zero add the query argument to the url
 * @param permanent if not the the redirect is permanent otherwise temporary
 */
extern void afb_hreq_redirect_to(struct afb_hreq *request, const char *url, int add_query_part, int permanent);

/**
 * Make an url to the path for the client, can be used for redirect to local path.
 * It behaves like return snprintf(buffer,size,"%s://%s/%s", protocol, host, path)
 * where protocol and host are those of the request hreq.
 *
 * @param hreq the HTTP request of reference
 * @param path the path to add after the host (if NULL then empty string)
 * @param buffer the buffer where to store the result
 * @param size sizeof the buffer
 *
 * @return the length of the computed path. If greater or equal than given size, the
 * result is truncated to fit the buffer's size
 */
extern int afb_hreq_make_here_url(struct afb_hreq *hreq, const char *path, char *buffer, size_t size);

extern int afb_hreq_redirect_to_ending_slash_if_needed(struct afb_hreq *hreq);

extern const char *afb_hreq_get_cookie(struct afb_hreq *hreq, const char *name);

extern const char *afb_hreq_get_header(struct afb_hreq *hreq, const char *name);

extern const char *afb_hreq_get_argument(struct afb_hreq *hreq, const char *name);

extern int afb_hreq_post_add_file(struct afb_hreq *hreq, const char *name, const char *file, const char *data, size_t size);

extern int afb_hreq_post_add(struct afb_hreq *hreq, const char *name, const char *data, size_t size);

extern void afb_hreq_call(struct afb_hreq *hreq, struct afb_apiset *apiset, const char *api, size_t lenapi, const char *verb, size_t lenverb);

extern int afb_hreq_init_context(struct afb_hreq *hreq);

extern int afb_hreq_init_cookie(int port, const char *path, int maxage);

extern void afb_hreq_reply_static(struct afb_hreq *hreq, unsigned status, size_t size, const char *buffer, ...);

extern void afb_hreq_reply_copy(struct afb_hreq *hreq, unsigned status, size_t size, const char *buffer, ...);

extern void afb_hreq_reply_free(struct afb_hreq *hreq, unsigned status, size_t size, char *buffer, ...);

extern void afb_hreq_reply_empty(struct afb_hreq *hreq, unsigned status, ...);

extern int afb_hreq_init_download_path(const char *directory);

extern void afb_hreq_addref(struct afb_hreq *hreq);

extern void afb_hreq_unref(struct afb_hreq *hreq);

extern struct afb_hreq *afb_hreq_create();

#endif
