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
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <microhttpd.h>
#include <json-c/json.h>
#include <rp-utils/rp-verbose.h>

#if HAVE_LIBMAGIC
#include <magic.h>
#endif

#include <afb/afb-arg.h>
#include <afb/afb-errno.h>

#include "core/afb-session.h"
#include "utils/locale-root.h"
#include "core/afb-token.h"
#include "core/afb-req-common.h"
#include "core/afb-json-legacy.h"
#include "core/containerof.h"
#include "core/afb-data.h"
#include "core/afb-type-predefined.h"

#include "http/afb-method.h"
#include "http/afb-hreq.h"
#include "http/afb-hsrv.h"
#include "sys/x-errno.h"
#include "utils/namecmp.h"

#define SIZE_RESPONSE_BUFFER   8192
#define SIZE_COOKIE_BUFFER     250

static int global_reqids = 0;

static char empty_string[] = "";

static const char long_key_for_uuid[] = "x-afb-uuid";
static const char short_key_for_uuid[] = "uuid";

static const char long_key_for_token[] = "x-afb-token";
static const char short_key_for_token[] = "token";

static const char key_for_bearer[] = "Bearer";
static const char key_for_access_token[] = "access_token";

static char *cookie_name = NULL;
static char *cookie_attr = NULL;
static char *tmp_pattern = NULL;

/**
 * Structure for storing key/values read from POST requests
 */
struct hreq_data {
	/** link to next data */
	struct hreq_data *next;
	/* length of the value (used for appending) */
	size_t length;
	/* the value (or original filename) */
	char *value;
	/* path of the file saved */
	char *path;
	/** key name */
	char key[];
};

/**
 * implementation of the afb_req_common interface
 */
static void req_reply(struct afb_req_common *comreq, int status, unsigned nreplies, struct afb_data * const replies[]);
static void req_destroy(struct afb_req_common *comreq);
static int  req_interface(struct afb_req_common *req, int id, const char *name, void **result);

const struct afb_req_common_query_itf afb_hreq_req_common_query_itf = {
	.reply = req_reply,
	.unref = req_destroy,
	.subscribe = NULL,
	.unsubscribe = NULL,
	.interface = req_interface
};

/**
 * Get from the request 'hreq' the data structure of name 'key'.
 *
 * @param hreq the request
 * @param key  the key to search
 * @param create create the structure if not existing
 *
 * @return the found structure if found or NULL if not found when create is zero
 * or on allocation error if create isn't zero
 */
static struct hreq_data *get_data(struct afb_hreq *hreq, const char *key, int create)
{
	size_t sz;
	struct hreq_data *data;

	/* search the existing data */
	data = hreq->data;
	while (data != NULL && namecmp(data->key, key))
		data = data->next;

	/* create a new data on need */
	if (data == NULL && create) {
		sz = 1 + strlen(key);
		data = malloc(sz + sizeof *data);
		if (data != NULL) {
			memcpy(data->key, key, sz);
			data->length = 0;
			data->value = NULL;
			data->path = NULL;
			data->next = hreq->data;
			hreq->data = data;
		}
	}
	return data;
}

/* a valid subpath is a relative path not looking deeper than root using .. */
static int validsubpath(const char *subpath)
{
	int l = 0, i = 0;

	while (subpath[i]) {
		switch (subpath[i++]) {
		case '.':
			if (!subpath[i])
				break;
			if (subpath[i] == '/') {
				i++;
				break;
			}
			if (subpath[i++] == '.') {
				if (!subpath[i]) {
					if (--l < 0)
						return 0;
					break;
				}
				if (subpath[i++] == '/') {
					if (--l < 0)
						return 0;
					break;
				}
			}
		default:
			while (subpath[i] && subpath[i] != '/')
				i++;
			l++;
		case '/':
			break;
		}
	}
	return 1;
}

/**
 * Add cookie header to the response
 *
 * @param hreq the request
 * @param response the response
 */
static void set_response_cookie(struct afb_hreq *hreq, struct MHD_Response *response)
{
	int rc;
	char cookie[SIZE_COOKIE_BUFFER + 1];
	const char *uuid;
	int res;

	/* search the uuid of the session if any */
	uuid = hreq->comreq.session ? afb_session_uuid(hreq->comreq.session) : NULL;
	if (uuid != NULL) {
		/* set a cookie for the the session */
		rc = snprintf(cookie, sizeof cookie, "%s=%s%s", cookie_name, uuid, cookie_attr);
		if (rc <= 0 || rc > SIZE_COOKIE_BUFFER)
			res = MHD_NO;
		else
			res = MHD_add_response_header(response, MHD_HTTP_HEADER_SET_COOKIE, cookie);
		if (res == MHD_NO)
			RP_ERROR("unable to set cookie");
	}
}

/**
 * Send a reply to the request
 *
 * @param hreq the request
 * @param status HTTP status code of the reply
 * @param response content of the response
 * @param args variable arguments list of key/value pairs of companion header
 */
static void afb_hreq_reply_v(struct afb_hreq *hreq, unsigned status, struct MHD_Response *response, va_list args)
{
	const char *k, *v;

	/* Check replying status */
	if (hreq->replied != 0) {
		RP_ERROR("Already replied HTTP request");
		MHD_destroy_response(response);
		return;
	}
	hreq->replied = 1;

	/* add extra headers */
	k = va_arg(args, const char *);
	while (k != NULL) {
		v = va_arg(args, const char *);
		MHD_add_response_header(response, k, v);
		k = va_arg(args, const char *);
	}
	set_response_cookie(hreq, response);

	/* send the response */
	MHD_queue_response(hreq->connection, status, response);
	MHD_destroy_response(response);

	/* hack if suspended ! */
	if (hreq->suspended != 0) {
		MHD_resume_connection (hreq->connection);
		hreq->suspended = 0;
		afb_hsrv_run(hreq->hsrv);
	}
}

void afb_hreq_reply(struct afb_hreq *hreq, unsigned status, struct MHD_Response *response, ...)
{
	va_list args;
	va_start(args, response);
	afb_hreq_reply_v(hreq, status, response, args);
	va_end(args);
}

void afb_hreq_reply_empty(struct afb_hreq *hreq, unsigned status, ...)
{
	va_list args;
	va_start(args, status);
	afb_hreq_reply_v(hreq, status, MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT), args);
	va_end(args);
}

void afb_hreq_reply_static(struct afb_hreq *hreq, unsigned status, size_t size, const char *buffer, ...)
{
	va_list args;
	va_start(args, buffer);
	afb_hreq_reply_v(hreq, status, MHD_create_response_from_buffer((unsigned)size, (char*)buffer, MHD_RESPMEM_PERSISTENT), args);
	va_end(args);
}

void afb_hreq_reply_copy(struct afb_hreq *hreq, unsigned status, size_t size, const char *buffer, ...)
{
	va_list args;
	va_start(args, buffer);
	afb_hreq_reply_v(hreq, status, MHD_create_response_from_buffer((unsigned)size, (char*)buffer, MHD_RESPMEM_MUST_COPY), args);
	va_end(args);
}

void afb_hreq_reply_free(struct afb_hreq *hreq, unsigned status, size_t size, char *buffer, ...)
{
	va_list args;
	va_start(args, buffer);
	afb_hreq_reply_v(hreq, status, MHD_create_response_from_buffer((unsigned)size, buffer, MHD_RESPMEM_MUST_FREE), args);
	va_end(args);
}

#if HAVE_LIBMAGIC

#if !defined(MAGIC_DB)
#define MAGIC_DB "/usr/share/misc/magic.mgc"
#endif

static magic_t lazy_libmagic()
{
	static int done = 0;
	static magic_t result = NULL;

	if (!done) {
		done = 1;
		/* MAGIC_MIME tells magic to return a mime of the file,
			 but you can specify different things */
		RP_INFO("Loading mimetype default magic database");
		result = magic_open(MAGIC_MIME_TYPE);
		if (result == NULL) {
			RP_ERROR("unable to initialize magic library");
		}
		/* Warning: should not use NULL for DB
				[libmagic bug wont pass efence check] */
		else if (magic_load(result, MAGIC_DB) != 0) {
			RP_ERROR("cannot load magic database: %s", magic_error(result));
			magic_close(result);
			result = NULL;
		}
	}

	return result;
}

static const char *magic_mimetype_fd(int fd)
{
	magic_t lib = lazy_libmagic();
	return lib ? magic_descriptor(lib, fd) : NULL;
}

#endif

static const char *mimetype_fd_name(int fd, const char *filename)
{
	const char *result = NULL;

#if INFER_EXTENSION
	/*
	 * Set some well-known extensions
	 * Note that it is mandatory for example for css files in order to provide
	 * right mimetype that must be text/css (otherwise chrome browser will not
	 * load correctly css file) while libmagic returns text/plain.
	 */
	const char *extension = strrchr(filename, '.');
	if (extension) {
		static const char *const known[][2] = {
			/* keep it sorted for dichotomic search */
			{ ".css",	"text/css" },
			{ ".gif",	"image/gif" },
			{ ".html",	"text/html" },
			{ ".htm",	"text/html" },
			{ ".ico",	"image/x-icon"},
			{ ".jpeg",	"image/jpeg" },
			{ ".jpg",	"image/jpeg" },
			{ ".js",	"text/javascript" },
			{ ".json",	"application/json" },
			{ ".mp3",	"audio/mpeg" },
			{ ".png",	"image/png" },
			{ ".svg",	"image/svg+xml" },
			{ ".ttf",	"application/x-font-ttf"},
			{ ".txt",	"text/plain" },
			{ ".wav",	"audio/x-wav" },
			{ ".xht",	"application/xhtml+xml" },
			{ ".xhtml",	"application/xhtml+xml" },
			{ ".xml",	"application/xml" }
		};
		int i, c, l = 0, u = sizeof known / sizeof *known;
		while (l < u) {
			i = (l + u) >> 1;
			c = strcasecmp(extension, known[i][0]);
			if (!c) {
				result = known[i][1];
				break;
			}
			if (c < 0)
				u = i;
			else
				l = i + 1;
		}
	}
#endif
#if HAVE_LIBMAGIC
	if (result == NULL)
		result = magic_mimetype_fd(fd);
#endif
	return result;
}

static void req_destroy(struct afb_req_common *comreq)
{
	struct afb_hreq *hreq = containerof(struct afb_hreq, comreq, comreq);
	struct hreq_data *data;

	if (hreq->postform != NULL)
		MHD_destroy_post_processor(hreq->postform);
	if (hreq->tokener != NULL)
		json_tokener_free(hreq->tokener);

	for (data = hreq->data; data; data = hreq->data) {
		hreq->data = data->next;
		if (data->path) {
			unlink(data->path);
			free(data->path);
		}
		free(data->value);
		free(data);
	}
	afb_req_common_cleanup(&hreq->comreq);
	json_object_put(hreq->json);
	free((char*)hreq->comreq.apiname);
	free((char*)hreq->comreq.verbname);
	free(hreq);
}

void afb_hreq_addref(struct afb_hreq *hreq)
{
	afb_req_common_addref(&hreq->comreq);
}

void afb_hreq_unref(struct afb_hreq *hreq)
{
	if (hreq->replied)
		hreq->comreq.replied = 1;
	afb_req_common_unref(&hreq->comreq);
}

/*
 * Removes the 'prefix' of 'length' from the tail of 'hreq'
 * if and only if the prefix exists and is terminated by a leading
 * slash
 */
int afb_hreq_unprefix(struct afb_hreq *hreq, const char *prefix, size_t length)
{
	/* check the prefix ? */
	if (length > hreq->lentail || (hreq->tail[length] && hreq->tail[length] != '/')
	    || namencmp(prefix, hreq->tail, length))
		return 0;

	/* removes successives / */
	while (length < hreq->lentail && hreq->tail[length + 1] == '/')
		length++;

	/* update the tail */
	hreq->lentail -= length;
	hreq->tail += length;
	return 1;
}

int afb_hreq_valid_tail(struct afb_hreq *hreq)
{
	return validsubpath(hreq->tail);
}

void afb_hreq_reply_error(struct afb_hreq *hreq, unsigned int status)
{
	afb_hreq_reply_empty(hreq, status, NULL);
}

int afb_hreq_redirect_to_ending_slash_if_needed(struct afb_hreq *hreq)
{
	char *tourl;

	if (hreq->url[hreq->lenurl - 1] == '/')
		return 0;

	/* the redirect is needed for reliability of relative path */
	tourl = alloca(hreq->lenurl + 2);
	memcpy(tourl, hreq->url, hreq->lenurl);
	tourl[hreq->lenurl] = '/';
	tourl[hreq->lenurl + 1] = 0;
	afb_hreq_redirect_to(hreq, tourl, 1, 1);
	return 1;
}

static int try_reply_file(struct afb_hreq *hreq, const char *filename, int relax, int (*opencb)(void*,const char*), void *closure)
{
	static const char *indexes[] = { "index.html", NULL };

	int fd;
	unsigned int status;
	struct stat st;
	char etag[1 + 2 * 8];
	const char *inm;
	struct MHD_Response *response;
	const char *mimetype;
	int i;
	size_t length;
	char *extname;

	/* Opens the file or directory */
	fd = opencb(closure, filename[0] ? filename : ".");
	if (fd < 0) {
		switch (-fd) {
			case ENOENT:
				if (relax)
					return 0;
				afb_hreq_reply_error(hreq, MHD_HTTP_NOT_FOUND);
				break;
			case EACCES:
			case EPERM:
				afb_hreq_reply_error(hreq, MHD_HTTP_FORBIDDEN);
				break;
			default:
				afb_hreq_reply_error(hreq, MHD_HTTP_INTERNAL_SERVER_ERROR);
				break;
		}
		return 1;
	}

	/* Retrieves file's status */
	if (fstat(fd, &st) != 0) {
		close(fd);
		afb_hreq_reply_error(hreq, MHD_HTTP_INTERNAL_SERVER_ERROR);
		return 1;
	}

	/* serve directory */
	if (S_ISDIR(st.st_mode)) {
		close(fd);
		if (afb_hreq_redirect_to_ending_slash_if_needed(hreq) == 1)
			return 1;
		i = 0;
		length = strlen(filename);
		extname = alloca(length + 40); /* 40 is enough to old data of indexes */
		memcpy(extname, filename, length);
		if (length && extname[length - 1] != '/')
			extname[length++] = '/';

		fd = -1;
		while (fd < 0 && indexes[i] != NULL) {
			strcpy(extname + length, indexes[i++]);
			fd = opencb(closure, extname);
		}
		if (fd < 0) {
			if (relax)
				return 0;
			afb_hreq_reply_error(hreq, MHD_HTTP_NOT_FOUND);
			return 1;
		}
		if (fstat(fd, &st) != 0) {
			close(fd);
			afb_hreq_reply_error(hreq, MHD_HTTP_INTERNAL_SERVER_ERROR);
			return 1;
		}
	}

	/* Don't serve special files */
	if (!S_ISREG(st.st_mode)) {
		close(fd);
		afb_hreq_reply_error(hreq, MHD_HTTP_FORBIDDEN);
		return 1;
	}

	/* Check the method */
	if ((hreq->method & (afb_method_get | afb_method_head)) == 0) {
		close(fd);
		afb_hreq_reply_error(hreq, MHD_HTTP_METHOD_NOT_ALLOWED);
		return 1;
	}

	/* computes the etag */
	sprintf(etag, "%08X%08X", ((int)(st.st_mtim.tv_sec) ^ (int)(st.st_mtim.tv_nsec)), (int)(st.st_size));

	/* checks the etag */
	inm = MHD_lookup_connection_value(hreq->connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_IF_NONE_MATCH);
	if (inm && 0 == strcmp(inm, etag)) {
		/* etag ok, return NOT MODIFIED */
		close(fd);
		RP_DEBUG("Not Modified: [%s]", filename);
		response = MHD_create_response_from_buffer(0, empty_string, MHD_RESPMEM_PERSISTENT);
		status = MHD_HTTP_NOT_MODIFIED;
	} else {
		/* check the size */
		if (st.st_size != (off_t) (size_t) st.st_size) {
			close(fd);
			afb_hreq_reply_error(hreq, MHD_HTTP_INTERNAL_SERVER_ERROR);
			return 1;
		}

		/* create the response */
		response = MHD_create_response_from_fd((size_t) st.st_size, fd);
		status = MHD_HTTP_OK;

		/* set the type */
		mimetype = mimetype_fd_name(fd, filename);
		if (mimetype != NULL)
			MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, mimetype);
	}

	/* fills the value and send */
	afb_hreq_reply(hreq, status, response,
			MHD_HTTP_HEADER_CACHE_CONTROL, hreq->cacheTimeout,
			MHD_HTTP_HEADER_ETAG, etag,
			NULL);
	return 1;
}

static int open_file(void *closure, const char *filename)
{
	int fd = open(filename, O_RDONLY);
	return fd < 0 ? -errno : fd;
}

int afb_hreq_reply_file(struct afb_hreq *hreq, const char *filename, int relax)
{
	return try_reply_file(hreq, filename, relax, open_file, 0);
}

static int open_locale_file(void *closure, const char *filename)
{
	struct locale_search *search = closure;
	int fd = locale_search_open(search, filename[0] ? filename : ".", O_RDONLY);
	return fd < 0 ? -errno : fd;
}

int afb_hreq_reply_locale_file_if_exist(struct afb_hreq *hreq, struct locale_search *search, const char *filename)
{
	return try_reply_file(hreq, filename, 1, open_locale_file, search);
}

int afb_hreq_reply_locale_file(struct afb_hreq *hreq, struct locale_search *search, const char *filename)
{
	return try_reply_file(hreq, filename, 0, open_locale_file, search);
}

#if WITH_OPENAT

static int open_file_at(void *closure, const char *filename)
{
	int dirfd = (int)(intptr_t)closure;
	int fd = openat(dirfd, filename, O_RDONLY);
	return fd < 0 ? -errno : fd;
}

int afb_hreq_reply_file_at_if_exist(struct afb_hreq *hreq, int dirfd, const char *filename)
{
	return try_reply_file(hreq, filename, 1, open_file_at, (void*)(intptr_t)dirfd);
}


int afb_hreq_reply_file_at(struct afb_hreq *hreq, int dirfd, const char *filename)
{
	return try_reply_file(hreq, filename, 0, open_file_at, (void*)(intptr_t)dirfd);
}

#endif

struct _mkq_ {
	int count;
	size_t length;
	size_t alloc;
	char *text;
};

static void _mkq_add_(struct _mkq_ *mkq, char value)
{
	char *text = mkq->text;
	if (text != NULL) {
		if (mkq->length == mkq->alloc) {
			mkq->alloc += 100;
			text = realloc(text, mkq->alloc);
			if (text == NULL) {
				free(mkq->text);
				mkq->text = NULL;
				return;
			}
			mkq->text = text;
		}
		text[mkq->length++] = value;
	}
}

static void _mkq_add_hex_(struct _mkq_ *mkq, char value)
{
	_mkq_add_(mkq, (char)(value < 10 ? value + '0' : value + 'A' - 10));
}

static void _mkq_add_esc_(struct _mkq_ *mkq, char value)
{
	_mkq_add_(mkq, '%');
	_mkq_add_hex_(mkq, (char)((value >> 4) & 15));
	_mkq_add_hex_(mkq, (char)(value & 15));
}

static void _mkq_add_char_(struct _mkq_ *mkq, char value)
{
	if (value <= ' ' || value >= 127)
		_mkq_add_esc_(mkq, value);
	else
		switch(value) {
		case '=':
		case '&':
		case '%':
			_mkq_add_esc_(mkq, value);
			break;
		default:
			_mkq_add_(mkq, value);
		}
}

static void _mkq_append_(struct _mkq_ *mkq, const char *value)
{
	while(*value)
		_mkq_add_char_(mkq, *value++);
}

static int _mkquery_(struct _mkq_ *mkq, enum MHD_ValueKind kind, const char *key, const char *value)
{
	_mkq_add_(mkq, mkq->count++ ? '&' : '?');
	_mkq_append_(mkq, key);
	if (value != NULL) {
		_mkq_add_(mkq, '=');
		_mkq_append_(mkq, value);
	}
	return 1;
}

static char *url_with_query(struct afb_hreq *hreq, const char *url)
{
	struct _mkq_ mkq;

	mkq.count = 0;
	mkq.length = strlen(url);
	mkq.alloc = mkq.length + 1000;
	mkq.text = malloc(mkq.alloc);
	if (mkq.text != NULL) {
		strcpy(mkq.text, url);
		MHD_get_connection_values(hreq->connection, MHD_GET_ARGUMENT_KIND, (void*)_mkquery_, &mkq);
		_mkq_add_(&mkq, 0);
	}
	return mkq.text;
}

void afb_hreq_redirect_to(struct afb_hreq *hreq, const char *url, int add_query_part, int permanent)
{
	const char *to;
	char *wqp;
	unsigned int redir = permanent
			? MHD_HTTP_MOVED_PERMANENTLY /* TODO MHD_HTTP_PERMANENT_REDIRECT */
			: MHD_HTTP_TEMPORARY_REDIRECT;

	wqp = add_query_part ? url_with_query(hreq, url) : NULL;
	to = wqp ? : url;
	RP_DEBUG("%s redirect from [%s] to [%s]", permanent ? "permanent" : "temporary", hreq->url, url);
	afb_hreq_reply_static(hreq, redir, 0, NULL,
			MHD_HTTP_HEADER_LOCATION, to, NULL);
	free(wqp);
}

int afb_hreq_make_here_url(struct afb_hreq *hreq, const char *path, char *buffer, size_t size)
{
	const char *host;
	const union MHD_ConnectionInfo *info;

	info = MHD_get_connection_info(hreq->connection, MHD_CONNECTION_INFO_PROTOCOL);
	host = afb_hreq_get_header(hreq, MHD_HTTP_HEADER_HOST);

	return snprintf(buffer, size, "%s://%s/%s",
			info && info->tls_session ? "https" : "http",
			host ?: "0.0.0.0",
			!path ? "" : &path[path[0] == '/']);
}

const char *afb_hreq_get_cookie(struct afb_hreq *hreq, const char *name)
{
	return MHD_lookup_connection_value(hreq->connection, MHD_COOKIE_KIND, name);
}

const char *afb_hreq_get_argument(struct afb_hreq *hreq, const char *name)
{
	struct hreq_data *data = get_data(hreq, name, 0);
	return data ? data->value : MHD_lookup_connection_value(hreq->connection, MHD_GET_ARGUMENT_KIND, name);
}

const char *afb_hreq_get_header(struct afb_hreq *hreq, const char *name)
{
	return MHD_lookup_connection_value(hreq->connection, MHD_HEADER_KIND, name);
}

const char *afb_hreq_get_authorization_bearer(struct afb_hreq *hreq)
{
	const char *value = afb_hreq_get_header(hreq, MHD_HTTP_HEADER_AUTHORIZATION);
	if (value) {
		if (strncasecmp(value, key_for_bearer, sizeof key_for_bearer - 1) == 0) {
			value += sizeof key_for_bearer - 1;
			if (isblank(*value++)) {
				while (isblank(*value))
					value++;
				if (*value)
					return value;
			}
		}
	}
	return NULL;
}

int afb_hreq_post_add(struct afb_hreq *hreq, const char *key, const char *data, size_t size)
{
	void *p;
	struct hreq_data *hdat = get_data(hreq, key, 1);
	if (hdat->path != NULL) {
		return 0;
	}
	p = realloc(hdat->value, hdat->length + size + 1);
	if (p == NULL) {
		return 0;
	}
	hdat->value = p;
	memcpy(&hdat->value[hdat->length], data, size);
	hdat->length += size;
	hdat->value[hdat->length] = 0;
	return 1;
}

int afb_hreq_init_download_path(const char *directory)
{
	struct stat st;
	size_t n;
	char *p;

	if (access(directory, R_OK|W_OK))
		/* no read/write access */
		return -errno;

	if (stat(directory, &st))
		/* can't get info */
		return -errno;

	if (!S_ISDIR(st.st_mode))
		/* not a directory */
		return X_ENOTDIR;

	n = strlen(directory);
	while(n > 1 && directory[n-1] == '/') n--;
	p = malloc(n + 8);
	if (p == NULL)
		/* can't allocate memory */
		return X_ENOMEM;

	memcpy(p, directory, n);
	p[n++] = '/';
	p[n++] = 'X';
	p[n++] = 'X';
	p[n++] = 'X';
	p[n++] = 'X';
	p[n++] = 'X';
	p[n++] = 'X';
	p[n] = 0;
	free(tmp_pattern);
	tmp_pattern = p;
	return 0;
}

static int opentempfile(char **path)
{
	int fd;
	char *fname;

	fname = strdup(tmp_pattern ? : "XXXXXX"); /* TODO improve the path */
	if (fname == NULL)
		return -1;

	fd = mkostemp(fname, O_CLOEXEC|O_WRONLY);
	if (fd < 0)
		free(fname);
	else
		*path = fname;
	return fd;
}

int afb_hreq_post_add_file(struct afb_hreq *hreq, const char *key, const char *file, const char *data, size_t size)
{
	int fd;
	ssize_t sz;
	struct hreq_data *hdat = get_data(hreq, key, 1);

	if (hdat->value == NULL) {
		hdat->value = strdup(file);
		if (hdat->value == NULL)
			return 0;
		fd = opentempfile(&hdat->path);
	} else if (strcmp(hdat->value, file) || hdat->path == NULL) {
		return 0;
	} else {
		fd = open(hdat->path, O_WRONLY|O_APPEND);
	}
	if (fd < 0)
		return 0;
	while (size) {
		sz = write(fd, data, size);
		if (sz >= 0) {
			hdat->length += (size_t)sz;
			size -= (size_t)sz;
			data += sz;
		} else if (errno != EINTR)
			break;
	}
	close(fd);
	return !size;
}

#if MHD_VERSION < 0x00097302
static ssize_t data_reader(void *cls, uint64_t pos, char *buf, size_t max)
{
	struct afb_data *data = cls;
	void *head;
	size_t size;
	afb_data_get_constant(data, &head, &size);
	if (pos >= size)
		return (ssize_t)MHD_CONTENT_READER_END_OF_STREAM;
	size -= pos;
	if (size > max)
		size = max;
	memcpy(buf, pos+(char*)head, size);
	return (ssize_t)size;
}
#endif

static void do_reply(struct afb_hreq *hreq, unsigned int code, struct afb_data *data, const char *type, const char **headers)
{
	struct MHD_Response *response;

	/* create the reply */
#if MHD_VERSION < 0x00097302
	response = MHD_create_response_from_callback(
			afb_data_size(data),
			afb_data_size(data),
			data_reader,
			data,
			(MHD_ContentReaderFreeCallback)afb_data_unref);
#else
	response = MHD_create_response_from_buffer_with_free_callback_cls(
			afb_data_size(data),
			afb_data_ro_pointer(data),
			(MHD_ContentReaderFreeCallback)afb_data_unref,
			data);
#endif
	if (type != NULL)
		MHD_add_response_header(response, MHD_HTTP_HEADER_CONTENT_TYPE, type);

	for ( ; headers && headers[0] && headers[1] ; headers += 2)
		MHD_add_response_header(response, headers[0], headers[1]);

	afb_hreq_reply(hreq, code, response, NULL);
}

static void req_reply(struct afb_req_common *comreq, int status, unsigned nreplies, struct afb_data *const replies[])
{
	struct afb_hreq *hreq = containerof(struct afb_hreq, comreq, comreq);
	char *message;
	size_t length;
	struct afb_data *data;
	const char *headers[3] = { NULL, NULL, NULL };
	unsigned int code;

	/* handle authorisation feedback */
	switch (status) {
	case AFB_ERRNO_INTERNAL_ERROR:
	case AFB_ERRNO_OUT_OF_MEMORY:
		code = MHD_HTTP_INTERNAL_SERVER_ERROR;
		break;
	case AFB_ERRNO_UNKNOWN_API:
	case AFB_ERRNO_UNKNOWN_VERB:
		code = MHD_HTTP_NOT_FOUND;
		break;
	case AFB_ERRNO_NOT_AVAILABLE:
		code = MHD_HTTP_NOT_IMPLEMENTED;
		break;
	case AFB_ERRNO_UNAUTHORIZED:
	case AFB_ERRNO_INVALID_TOKEN:
		headers[0] = MHD_HTTP_HEADER_WWW_AUTHENTICATE;
		headers[1] = "error=\"invalid_token\"";
		code = MHD_HTTP_UNAUTHORIZED;
		break;
	case AFB_ERRNO_FORBIDDEN:
	case AFB_ERRNO_INSUFFICIENT_SCOPE:
	case AFB_ERRNO_BAD_API_STATE:
		headers[0] = MHD_HTTP_HEADER_WWW_AUTHENTICATE;
		headers[1] = "error=\"insufficient_scope\"";
		code = MHD_HTTP_FORBIDDEN;
		break;
	default:
		code = MHD_HTTP_OK;
		break;
	}

	/* create the reply */
	if (nreplies == 1 && afb_data_type(replies[0]) == &afb_type_predefined_bytearray)
		data = afb_data_addref(replies[0]);
	else {
		afb_json_legacy_make_msg_string_reply(&message, &length, status, nreplies, replies);
		afb_data_create_raw(&data, &afb_type_predefined_bytearray, message, length, free, message);
	}

	do_reply(hreq, code, data, NULL, headers);
}

static int _iterargs_(struct json_object *obj, enum MHD_ValueKind kind, const char *key, const char *value)
{
	json_object_object_add(obj, key, value ? json_object_new_string(value) : NULL);
	return 1;
}

static void make_params(struct afb_hreq *hreq)
{
	struct hreq_data *hdat;
	struct json_object *obj, *val;
	struct afb_data *data;

	if (hreq->comreq.params.ndata == 0 && hreq->json == NULL) {
		obj = json_object_new_object();
		if (obj != NULL) {
			MHD_get_connection_values (hreq->connection, MHD_GET_ARGUMENT_KIND, (void*)_iterargs_, obj);
			for (hdat = hreq->data ; hdat ; hdat = hdat->next) {
				if (hdat->path == NULL)
					val = hdat->value ? json_object_new_string(hdat->value) : NULL;
				else {
					val = json_object_new_object();
					if (val == NULL) {
					} else {
						json_object_object_add(val, "file", json_object_new_string(hdat->value));
						json_object_object_add(val, "path", json_object_new_string(hdat->path));
					}
				}
				json_object_object_add(obj, hdat->key, val);
			}
		}
		hreq->json = obj;
	}
	if (hreq->comreq.params.ndata == 0) {
		afb_json_legacy_make_data_json_c(&data, json_object_get(hreq->json));
		afb_req_common_set_params(&hreq->comreq, 1, &data);
	}
}

void afb_hreq_call(struct afb_hreq *hreq, struct afb_apiset *apiset, const char *api, size_t lenapi, const char *verb, size_t lenverb)
{
	hreq->comreq.apiname = strndup(api, lenapi);
	hreq->comreq.verbname = strndup(verb, lenverb);
	if (hreq->comreq.apiname == NULL || hreq->comreq.verbname == NULL) {
		RP_ERROR("Out of memory");
		afb_hreq_reply_error(hreq, MHD_HTTP_INTERNAL_SERVER_ERROR);
	} else if (afb_hreq_init_context(hreq) < 0) {
		afb_hreq_reply_error(hreq, MHD_HTTP_INTERNAL_SERVER_ERROR);
	} else {
		make_params(hreq);
		afb_req_common_addref(&hreq->comreq);
		afb_req_common_process(&hreq->comreq, apiset);
	}
}

int afb_hreq_init_context(struct afb_hreq *hreq)
{
	const char *uuid;
	const char *token;

	if (hreq->comreq.session != NULL)
		return 0;

	/* get the uuid of the session */
	uuid = afb_hreq_get_header(hreq, long_key_for_uuid);
	if (uuid == NULL) {
		uuid = afb_hreq_get_argument(hreq, long_key_for_uuid);
		if (uuid == NULL) {
			uuid = afb_hreq_get_cookie(hreq, cookie_name);
			if (uuid == NULL)
				uuid = afb_hreq_get_argument(hreq, short_key_for_uuid);
		}
	}

	/* get the authorisation token */
	token = afb_hreq_get_authorization_bearer(hreq);
	if (token == NULL) {
		token = afb_hreq_get_argument(hreq, key_for_access_token);
		if (token == NULL) {
			token = afb_hreq_get_header(hreq, long_key_for_token);
			if (token == NULL) {
				token = afb_hreq_get_argument(hreq, long_key_for_token);
				if (token == NULL)
					token = afb_hreq_get_argument(hreq, short_key_for_token);
			}
		}
	}

	afb_req_common_set_session_string(&hreq->comreq, uuid);
	if (token)
		afb_req_common_set_token_string(&hreq->comreq, token);
	return 0;
}

int afb_hreq_init_cookie(int port, const char *path, int maxage)
{
	int rc;

	free(cookie_name);
	free(cookie_attr);
	cookie_name = cookie_attr = NULL;

	rc = asprintf(&cookie_name, "%s-%d", long_key_for_uuid, port);
	if (rc < 0)
		return 0;
	rc = asprintf(&cookie_attr, "; Path=%s; Max-Age=%d; HttpOnly; SameSite=Lax;",
			path ?: "/", maxage);
	if (rc < 0)
		return 0;
	return 1;
}

struct afb_req_common *afb_hreq_to_req_common(struct afb_hreq *hreq)
{
	return &hreq->comreq;
}

struct afb_hreq *afb_hreq_create()
{
	struct afb_hreq *hreq = calloc(1, sizeof *hreq);
	if (hreq) {
		/* init the request */
		afb_req_common_init(&hreq->comreq, &afb_hreq_req_common_query_itf, NULL, NULL, 0, NULL, NULL);
		hreq->reqid = ++global_reqids;
	}
	return hreq;
}

/**
 * implementation of the specific afb_itf_req_http_x4 interface
 */
#include "core/afb-v4-itf.h"
#include "core/afb-req-v4.h"
#include <afb/interfaces/afb-itf-req-http.h>

static int itf_header(struct afb_req_v4 *req, const char *name, const char **value);
static int itf_redirect(struct afb_req_v4 *req, const char *url, int absolute, int permanent);
static int itf_reply(struct afb_req_v4 *req, int code, struct afb_data *data, const char *type, const char **headers);

static int hreq_of_req_v4(struct afb_req_v4 *reqv4, struct afb_hreq **hreq)
{
	struct afb_req_common *comreq;
	if (!reqv4 || !(comreq = afb_req_v4_get_common(reqv4)) || comreq->queryitf != &afb_hreq_req_common_query_itf)
		return X_EINVAL;
	*hreq = containerof(struct afb_hreq, comreq, comreq);
	return 0;
}

static int itf_header(struct afb_req_v4 *req, const char *name, const char **value)
{
	struct afb_hreq *hreq;
	int rc = hreq_of_req_v4(req, &hreq);
	if (rc == 0) {
		const char *v = afb_hreq_get_header(hreq, name);
		*value = v;
		if (!v)
			rc = X_ENOENT;
	}
	return rc;
}

static int itf_redirect(struct afb_req_v4 *req, const char *url, int add_query_part, int permanent)
{
	struct afb_hreq *hreq;
	int rc = hreq_of_req_v4(req, &hreq);
	if (rc == 0)
		afb_hreq_redirect_to(hreq, url, add_query_part, permanent);
	return rc;
}

static int itf_reply(struct afb_req_v4 *req, int code, struct afb_data *data, const char *type, const char **headers)
{
	struct afb_hreq *hreq;
	int rc = hreq_of_req_v4(req, &hreq);
	if (rc == 0)
		do_reply(hreq, (unsigned)code, data, type, headers);
	return rc;
}

static const struct afb_itf_req_http_x4v1 itf_req_http_x4v1 = {
	.header = itf_header,
	.redirect = itf_redirect,
	.reply = itf_reply
};

static int req_interface(struct afb_req_common *req, int id, const char *name, void **result)
{
	if (id != AFB_ITF_ID_REQ_HTTP_X4V1 && (!name || strcmp(name, AFB_ITF_NAME_REQ_HTTP_X4V1)))
		return X_ENOENT;
	*result = (void*)&itf_req_http_x4v1;
	return 0;
}

#endif
