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

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <poll.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/stat.h>

#include <json-c/json.h>
#include <microhttpd.h>
#if MHD_VERSION < 0x00095206
# define MHD_ALLOW_SUSPEND_RESUME MHD_USE_SUSPEND_RESUME
#endif
#if MHD_VERSION < 0x00097002
typedef int mhd_result_t;
#else
typedef enum MHD_Result mhd_result_t;
#endif

#include "core/afb-sched.h"
#include "core/afb-ev-mgr.h"
#include "utils/locale-root.h"
#include "misc/afb-socket.h"

#include "sys/x-errno.h"

#include "http/afb-method.h"
#include "http/afb-hreq.h"
#include "http/afb-hsrv.h"
#include "sys/x-socket.h"

#include "sys/verbose.h"

#define JSON_CONTENT  "application/json"
#define FORM_CONTENT  MHD_HTTP_POST_ENCODING_MULTIPART_FORMDATA

struct hsrv_itf {
	struct hsrv_itf *next;
	struct afb_hsrv *hsrv;
	struct ev_fd *efd;
	char uri[];
};

struct hsrv_handler {
	struct hsrv_handler *next;
	const char *prefix;
	size_t length;
	int (*handler) (struct afb_hreq *, void *);
	void *data;
	int priority;
};

struct hsrv_alias {
	struct locale_root *root;
	int relax;
};

struct afb_hsrv {
	unsigned refcount;
	struct hsrv_handler *handlers;
	struct hsrv_itf *interfaces;
	struct MHD_Daemon *httpd;
	struct ev_fd *efd;
	char *cache_to;
};

static void reply_error(struct MHD_Connection *connection, unsigned int status)
{
	struct MHD_Response *response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
	MHD_queue_response(connection, status, response);
	MHD_destroy_response(response);
}

static mhd_result_t postproc(void *cls,
                    enum MHD_ValueKind kind,
                    const char *key,
                    const char *filename,
                    const char *content_type,
                    const char *transfer_encoding,
                    const char *data,
		    uint64_t off,
		    size_t size)
{
	int rc;
	struct afb_hreq *hreq = cls;
	if (filename != NULL)
		rc = afb_hreq_post_add_file(hreq, key, filename, data, size);
	else
		rc = afb_hreq_post_add(hreq, key, data, size);
	return rc ? MHD_YES : MHD_NO;
}

static mhd_result_t access_handler(
		void *cls,
		struct MHD_Connection *connection,
		const char *url,
		const char *methodstr,
		const char *version,
		const char *upload_data,
		size_t *upload_data_size,
		void **recordreq)
{
	int rc;
	struct afb_hreq *hreq;
	enum afb_method method;
	struct afb_hsrv *hsrv;
	struct hsrv_handler *iter;
	const char *type;
	enum json_tokener_error jerr;

	hsrv = cls;
	hreq = *recordreq;
	if (hreq == NULL) {
		/* get the method */
		method = get_method(methodstr);
		method &= afb_method_get | afb_method_post;
		if (method == afb_method_none) {
			WARNING("Unsupported HTTP operation %s", methodstr);
			reply_error(connection, MHD_HTTP_BAD_REQUEST);
			return MHD_YES;
		}

		/* create the request */
		hreq = afb_hreq_create();
		if (hreq == NULL) {
			ERROR("Can't allocate 'hreq'");
			reply_error(connection, MHD_HTTP_INTERNAL_SERVER_ERROR);
			return MHD_YES;
		}

		/* init the request */
		hreq->hsrv = hsrv;
		hreq->cacheTimeout = hsrv->cache_to;
		hreq->connection = connection;
		hreq->method = method;
		hreq->version = version;
		hreq->lang = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, MHD_HTTP_HEADER_ACCEPT_LANGUAGE);
		hreq->tail = hreq->url = url;
		hreq->lentail = hreq->lenurl = strlen(url);
		*recordreq = hreq;

		/* init the post processing */
		if (method == afb_method_post) {
			type = afb_hreq_get_header(hreq, MHD_HTTP_HEADER_CONTENT_TYPE);
			if (type == NULL) {
				/* an empty post, let's process it as a get */
				hreq->method = afb_method_get;
			} else if (strcasestr(type, FORM_CONTENT) != NULL) {
				hreq->postform = MHD_create_post_processor (connection, 65500, postproc, hreq);
				if (hreq->postform == NULL) {
					ERROR("Can't create POST processor");
					afb_hreq_reply_error(hreq, MHD_HTTP_INTERNAL_SERVER_ERROR);
				}
				return MHD_YES;
			} else if (strcasestr(type, JSON_CONTENT) != NULL) {
				hreq->tokener = json_tokener_new();
				if (hreq->tokener == NULL) {
					ERROR("Can't create tokener for POST");
					afb_hreq_reply_error(hreq, MHD_HTTP_INTERNAL_SERVER_ERROR);
				}
				return MHD_YES;
                        } else {
				WARNING("Unsupported media type %s", type);
				afb_hreq_reply_error(hreq, MHD_HTTP_UNSUPPORTED_MEDIA_TYPE);
				return MHD_YES;
			}
		}
	}

	/* process further data */
	if (*upload_data_size) {
		if (hreq->postform != NULL) {
			if (!MHD_post_process (hreq->postform, upload_data, *upload_data_size)) {
				ERROR("error in POST processor");
				afb_hreq_reply_error(hreq, MHD_HTTP_INTERNAL_SERVER_ERROR);
				return MHD_YES;
			}
		} else if (hreq->tokener) {
			hreq->json = json_tokener_parse_ex(hreq->tokener, upload_data, (int)*upload_data_size);
			jerr = json_tokener_get_error(hreq->tokener);
			if (jerr == json_tokener_continue) {
				hreq->json = json_tokener_parse_ex(hreq->tokener, "", 1);
				jerr = json_tokener_get_error(hreq->tokener);
			}
			if (jerr != json_tokener_success) {
				ERROR("error in POST json: %s", json_tokener_error_desc(jerr));
				afb_hreq_reply_error(hreq, MHD_HTTP_BAD_REQUEST);
				return MHD_YES;
			}
		}
		*upload_data_size = 0;
		return MHD_YES;
	}

	/* flush the data */
	if (hreq->postform != NULL) {
		rc = MHD_destroy_post_processor(hreq->postform);
		hreq->postform = NULL;
		if (rc == MHD_NO) {
			ERROR("error detected in POST processing");
			afb_hreq_reply_error(hreq, MHD_HTTP_BAD_REQUEST);
			return MHD_YES;
		}
	}
	if (hreq->tokener != NULL) {
		json_tokener_free(hreq->tokener);
		hreq->tokener = NULL;
	}

	if (hreq->scanned != 0) {
		if (hreq->replied == 0 && hreq->suspended == 0) {
			MHD_suspend_connection (connection);
			hreq->suspended = 1;
		}
		return MHD_YES;
	}

	/* search an handler for the request */
	hreq->scanned = 1;
	iter = hsrv->handlers;
	while (iter) {
		if (afb_hreq_unprefix(hreq, iter->prefix, iter->length)) {
			if (iter->handler(hreq, iter->data)) {
				if (hreq->replied == 0 && hreq->suspended == 0) {
					MHD_suspend_connection (connection);
					hreq->suspended = 1;
				}
				return MHD_YES;
			}
			hreq->tail = hreq->url;
			hreq->lentail = hreq->lenurl;
		}
		iter = iter->next;
	}

	/* no handler */
	NOTICE("Unhandled request to %s", hreq->url);
	afb_hreq_reply_error(hreq, MHD_HTTP_NOT_FOUND);
	return MHD_YES;
}

/* Because of POST call multiple time requestApi we need to free POST handle here */
static void end_handler(void *cls, struct MHD_Connection *connection, void **recordreq,
			enum MHD_RequestTerminationCode toe)
{
	struct afb_hreq *hreq;

	hreq = *recordreq;
	if (hreq) {
		afb_hreq_unref(hreq);
	}
}

void afb_hsrv_run(struct afb_hsrv *hsrv)
{
        MHD_UNSIGNED_LONG_LONG to;
        do { MHD_run(hsrv->httpd); } while(MHD_get_timeout(hsrv->httpd, &to) == MHD_YES && !to);
}

static void listen_callback(struct ev_fd *efd, int fd, uint32_t revents, void *hsrv)
{
	afb_hsrv_run(hsrv);
}

static mhd_result_t new_client_handler(void *cls, const struct sockaddr *addr, socklen_t addrlen)
{
	return MHD_YES;
}

static struct hsrv_handler *new_handler(
		struct hsrv_handler *head,
		const char *prefix,
		int (*handler) (struct afb_hreq *, void *),
		void *data,
		int priority)
{
	struct hsrv_handler *link, *iter, *previous;
	size_t length;

	/* get the length of the prefix without its leading / */
	length = strlen(prefix);
	while (length && prefix[length - 1] == '/')
		length--;

	/* allocates the new link */
	link = malloc(sizeof *link);
	if (link == NULL)
		return NULL;

	/* initialize it */
	link->prefix = prefix;
	link->length = length;
	link->handler = handler;
	link->data = data;
	link->priority = priority;

	/* adds it */
	previous = NULL;
	iter = head;
	while (iter && (priority < iter->priority || (priority == iter->priority && length <= iter->length))) {
		previous = iter;
		iter = iter->next;
	}
	link->next = iter;
	if (previous == NULL)
		return link;
	previous->next = link;
	return head;
}

static int handle_alias(struct afb_hreq *hreq, void *data)
{
	int rc;
	struct hsrv_alias *da = data;
	struct locale_search *search;

	if (hreq->method != afb_method_get) {
		if (da->relax)
			return 0;
		afb_hreq_reply_error(hreq, MHD_HTTP_METHOD_NOT_ALLOWED);
		return 1;
	}

	search = locale_root_search(da->root, hreq->lang, 0);
	rc = afb_hreq_reply_locale_file_if_exist(hreq, search, &hreq->tail[1]);
	locale_search_unref(search);
	if (rc == 0) {
		if (da->relax)
			return 0;
		afb_hreq_reply_error(hreq, MHD_HTTP_NOT_FOUND);
	}
	return 1;
}

int afb_hsrv_add_handler(
		struct afb_hsrv *hsrv,
		const char *prefix,
		int (*handler) (struct afb_hreq *, void *),
		void *data,
		int priority)
{
	struct hsrv_handler *head;

	head = new_handler(hsrv->handlers, prefix, handler, data, priority);
	if (head == NULL)
		return 0;
	hsrv->handlers = head;
	return 1;
}

int afb_hsrv_add_alias_root(struct afb_hsrv *hsrv, const char *prefix, struct locale_root *root, int priority, int relax)
{
	struct hsrv_alias *da;

	da = malloc(sizeof *da);
	if (da != NULL) {
		da->root = root;
		da->relax = relax;
		if (afb_hsrv_add_handler(hsrv, prefix, handle_alias, da, priority)) {
			locale_root_addref(root);
			return 1;
		}
		free(da);
	}
	return 0;
}

#if WITH_OPENAT
int afb_hsrv_add_alias(struct afb_hsrv *hsrv, const char *prefix, int dirfd, const char *alias, int priority, int relax)
{
	struct locale_root *root;
	int rc;

	root = locale_root_create_at(dirfd, alias);
	if (root == NULL) {
		ERROR("can't connect to directory %s: %m", alias);
		rc = 0;
	} else {
		rc = afb_hsrv_add_alias_root(hsrv, prefix, root, priority, relax);
		locale_root_unref(root);
	}
	return rc;
}
#endif

int afb_hsrv_add_alias_path(struct afb_hsrv *hsrv, const char *prefix, const char *basepath, const char *alias, int priority, int relax)
{
	char buffer[PATH_MAX];
	struct locale_root *root;
	int rc;

	rc = snprintf(buffer, sizeof buffer, "%s/%s", basepath, alias);
	if (rc < 0 || rc >= (int)(sizeof buffer)) {
		ERROR("can't make path %s/%s", basepath, alias);
		rc = 0;
	} else {
		root = locale_root_create_path(buffer);
		if (root == NULL) {
			ERROR("can't connect to directory %s: %m", alias);
			rc = 0;
		} else {
			rc = afb_hsrv_add_alias_root(hsrv, prefix, root, priority, relax);
			locale_root_unref(root);
		}
	}
	return rc;
}

int afb_hsrv_set_cache_timeout(struct afb_hsrv *hsrv, int duration)
{
	int rc;
	char *dur;

	rc = asprintf(&dur, "%d", duration);
	if (rc < 0)
		return 0;

	free(hsrv->cache_to);
	hsrv->cache_to = dur;
	return 1;
}

int afb_hsrv_start(struct afb_hsrv *hsrv, unsigned int connection_timeout)
{
	struct MHD_Daemon *httpd;
	const union MHD_DaemonInfo *info;

	httpd = MHD_start_daemon(
			MHD_USE_EPOLL
			| MHD_ALLOW_UPGRADE
			| MHD_USE_TCP_FASTOPEN
			| MHD_USE_NO_LISTEN_SOCKET
			| MHD_USE_DEBUG
			| MHD_ALLOW_SUSPEND_RESUME,
			0,				/* port */
			new_client_handler, NULL,	/* Tcp Accept call back + extra attribute */
			access_handler, hsrv,	/* Http Request Call back + extra attribute */
			MHD_OPTION_NOTIFY_COMPLETED, end_handler, hsrv,
			MHD_OPTION_CONNECTION_TIMEOUT, connection_timeout,
			MHD_OPTION_END);	/* options-end */

	if (httpd == NULL) {
		ERROR("httpStart invalid httpd");
		return 0;
	}

	info = MHD_get_daemon_info(httpd, MHD_DAEMON_INFO_EPOLL_FD);
	if (info == NULL) {
		MHD_stop_daemon(httpd);
		ERROR("httpStart no pollfd");
		return 0;
	}

	if (afb_ev_mgr_add_fd(&hsrv->efd, info->epoll_fd, EPOLLIN, listen_callback, hsrv, 0, 0) < 0) {
		MHD_stop_daemon(httpd);
		ERROR("connection to events for httpd failed");
		return 0;
	}

	hsrv->httpd = httpd;
	return 1;
}

void afb_hsrv_stop(struct afb_hsrv *hsrv)
{
	if (hsrv->efd) {
		ev_fd_unref(hsrv->efd);
		hsrv->efd = 0;
	}
	if (hsrv->httpd != NULL)
		MHD_stop_daemon(hsrv->httpd);
	hsrv->httpd = NULL;
}

struct afb_hsrv *afb_hsrv_create()
{
	struct afb_hsrv *result = calloc(1, sizeof(struct afb_hsrv));
	if (result != NULL)
		result->refcount = 1;
	return result;
}

void afb_hsrv_put(struct afb_hsrv *hsrv)
{
	assert(hsrv->refcount != 0);
	if (!--hsrv->refcount) {
		afb_hsrv_stop(hsrv);
		free(hsrv);
	}
}

static int hsrv_itf_connect(struct hsrv_itf *itf);

static void hsrv_itf_callback(struct ev_fd *efd, int fd, uint32_t revents, void *closure)
{
	struct hsrv_itf *itf = closure;
	int fdc, sts;
	struct sockaddr addr;
	socklen_t lenaddr;

	if ((revents & EPOLLHUP) != 0) {
		ERROR("disconnection for server %s: %m", itf->uri);
		hsrv_itf_connect(itf);
		ev_fd_unref(efd);
	} else if ((revents & EPOLLIN) != 0) {
		lenaddr = (socklen_t)sizeof addr;
		fdc = accept(fd, &addr, &lenaddr);
		if (fdc < 0)
			ERROR("can't accept connection to %s: %m", itf->uri);
		else {
			sts = MHD_add_connection(itf->hsrv->httpd, fdc, &addr, lenaddr);
			if (sts != MHD_YES) {
				ERROR("can't add incoming connection to %s: %m", itf->uri);
				close(fdc);
			}
		}
	}
}

static int hsrv_itf_connect(struct hsrv_itf *itf)
{
	struct sockaddr addr;
	socklen_t lenaddr;
	char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
	int rgni, rc, fd;

	fd = afb_socket_open_scheme(itf->uri, 1, "tcp:");
	if (fd < 0) {
		ERROR("can't create socket %s", itf->uri);
		return -errno;
	}
	rc = afb_ev_mgr_add_fd(&itf->efd, fd, EPOLLIN, hsrv_itf_callback, itf, 0, 1);
	if (rc < 0) {
		ERROR("can't connect socket %s", itf->uri);
		return rc;
	}
	memset(&addr, 0, sizeof addr);
	lenaddr = (socklen_t)sizeof addr;
	getsockname(fd, &addr, &lenaddr);
	if (addr.sa_family == AF_INET && !((struct sockaddr_in*)&addr)->sin_addr.s_addr) {
		strncpy(hbuf, "*", NI_MAXHOST);
		sprintf(sbuf, "%d", (int)ntohs(((struct sockaddr_in*)&addr)->sin_port));
	} else {
		rgni = getnameinfo(&addr, lenaddr, hbuf, sizeof hbuf, sbuf, sizeof sbuf, NI_NUMERICSERV);
		if (rgni != 0) {
			ERROR("getnameinfo returned %d: %s", rgni, gai_strerror(rgni));
			hbuf[0] = sbuf[0] = '?';
			hbuf[1] = sbuf[1] = 0;
		}
	}
	NOTICE("Listening interface %s:%s", hbuf, sbuf);
	return 0;
}

int afb_hsrv_add_interface(struct afb_hsrv *hsrv, const char *uri)
{
	struct hsrv_itf *itf;

	itf = malloc(sizeof *itf + 1 + strlen(uri));
	if (itf == NULL)
		return X_ENOMEM;

	itf->hsrv = hsrv;
	itf->efd = 0;
	strcpy(itf->uri, uri);
	itf->next = hsrv->interfaces;
	hsrv->interfaces = itf;
	return hsrv_itf_connect(itf);
}

int afb_hsrv_add_interface_tcp(struct afb_hsrv *hsrv, const char *itf, uint16_t port)
{
	int rc;
	char buffer[1024];

	if (itf == NULL) {
		itf = "*"; /* awaiting getifaddrs impl */
	}
	rc = snprintf(buffer, sizeof buffer, "tcp:%s:%d", itf, (int)port);
	if (rc < 0)
		return -errno;
	if (rc >= (int)sizeof buffer)
		return X_EINVAL;
	return afb_hsrv_add_interface(hsrv, buffer);
}

#endif
