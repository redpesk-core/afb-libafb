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

#pragma once

#if WITH_LIBMICROHTTPD

#include "core/afb-xreq.h"

struct json_object;
struct json_tokener;

struct afb_session;
struct hreq_data;
struct afb_hsrv;
struct locale_search;

struct afb_hreq {
	struct afb_xreq xreq;
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

#if WITH_OPENAT
extern int afb_hreq_reply_file_if_exist(struct afb_hreq *request, int dirfd, const char *filename);

extern int afb_hreq_reply_file(struct afb_hreq *request, int dirfd, const char *filename);
#endif

extern int afb_hreq_reply_locale_file_if_exist(struct afb_hreq *hreq, struct locale_search *search, const char *filename);

extern int afb_hreq_reply_locale_file(struct afb_hreq *hreq, struct locale_search *search, const char *filename);

extern void afb_hreq_redirect_to(struct afb_hreq *request, const char *url, int add_query_part);

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
