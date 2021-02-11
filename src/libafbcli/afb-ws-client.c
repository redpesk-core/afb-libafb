/*
 * Copyright (C) 2015-2021 IoT.bzh Company
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
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/un.h>

#include <systemd/sd-event.h>

#include "afb-ws-client.h"

#include "misc/afb-socket.h"
#include "core/afb-ev-mgr.h"
#include "wsapi/afb-proto-ws.h"
#include "wsapi/afb-wsapi.h"
#include "wsj1/afb-wsj1.h"
#include "tls/tls.h"
#include "sys/ev-mgr.h"
#include "sys/x-socket.h"
#include "sys/x-errno.h"

/**************** ev mgr ****************************/

struct ev_mgr *afb_sched_acquire_event_manager()
{
	static struct ev_mgr *result;
	if (!result)
		ev_mgr_create(&result);
	return result;
}

/*****************************************************************************************************************************/

static struct sd_event_source *current_sd_event_source_prepare;
static struct sd_event_source *current_sd_event_source_io;

static int onprepare(struct sd_event_source *s, void *userdata)
{
	afb_ev_mgr_prepare();
	return 0;
}

static int onevent(struct sd_event_source *s, int fd, uint32_t revents, void *userdata)
{
	afb_ev_mgr_wait(0);
	afb_ev_mgr_dispatch();
	return 0;
}

/*
 * Attaches the internal event loop to the given sd_event
 */
int afb_ws_client_connect_to_sd_event(struct sd_event *eloop)
{
	int rc;
	if (current_sd_event_source_io) {
		if (sd_event_source_get_event(current_sd_event_source_io) == eloop)
			return 0;
		sd_event_source_unref(current_sd_event_source_io);
		sd_event_source_unref(current_sd_event_source_prepare);
		current_sd_event_source_io = current_sd_event_source_prepare = 0;
	}
	rc = sd_event_add_defer(eloop, &current_sd_event_source_prepare, onprepare, 0);
	if (rc >= 0) {
		rc = sd_event_add_post(eloop, &current_sd_event_source_prepare, onprepare, 0);
		if (rc >= 0) {
			rc = sd_event_add_io(eloop, &current_sd_event_source_io, afb_ev_mgr_get_fd(), EPOLLIN, onevent, 0);
			if (rc < 0) {
				sd_event_source_unref(current_sd_event_source_prepare);
				current_sd_event_source_io = current_sd_event_source_prepare = 0;
			}
		}
	}
	if (rc >= 0) {
		rc = sd_event_prepare(eloop);
		while (rc > 0) {
			rc = sd_event_dispatch(eloop);
			rc = rc <= 0 ? -1 : sd_event_prepare(eloop);
		}
		if (sd_event_get_state(eloop) == SD_EVENT_ARMED) {
			rc = sd_event_wait(eloop, 0);
			if (rc > 0)
				rc = sd_event_dispatch(eloop);
		}
	}
	return rc;
}

/**************** WebSocket handshake ****************************/

static const char *compkeys[32] = {
	"lYKr2sn9+ILcLpkqdrE2VQ==", "G5J7ncQnmS/MubIYcqKWM+E6k8I=",
	"gjN6eOU/6Yy7dBTJ+EaQSw==", "P5QzN7mRt4DeRWxKdG7s4/NCEwk=",
	"ziLin6OQ0/a1+cGaI9Mupg==", "yvpxcFJAGam6huL77vz34CdShyU=",
	"KMfd2bHKah0U5mk2Kg/LIg==", "lyYxfDP5YunhkBF+nAWb/w6K4yg=",
	"fQ/ISF1mNCPRMyAj3ucqNg==", "91YY1EUelb4eMU24Z8WHhJ9cHmc=",
	"RHlfiVVE1lM1AJnErI8dFg==", "UdZQc0JaihQJV5ETCZ84Av88pxQ=",
	"NVy3L2ujXN7v3KEJwK92ww==", "+dE7iITxhExjBtf06VYNWChHqx8=",
	"cCNAgttlgELfbDDIfhujww==", "W2JiswqbTAXx5u84EtjbtqAW2Bg=",
	"K+oQvEDWJP+WXzRS5BJDFw==", "szgW10a9AuD+HtfS4ylaqWfzWAs=",
	"nmg43S4DpVaxye+oQv9KTw==", "8XK74jB9xFfTzzl0wTqW04k3tPE=",
	"LIqZ23sEppbF4YJR9LQ4/w==", "f8lJBQEbR8QmmvPHZpA0smlIeeA=",
	"WY1vvvY2j/3V9DAGW3ZZcA==", "lROlE4vL4cjU1Vnk6rISc9gVKN0=",
	"Ia+dgHnA9QaBrbxuqh4wgQ==", "GiGjxFdSaF0EGTl2cjvFsVmJnfM=",
	"MfpIVG082jFTV7SxTNNijQ==", "f5I2h53hBsT5ES3EHhnxAJ2nqsw=",
	"kFumnAw5d/WctG0yAUHPiQ==", "aQQmOjoABl7mrbliTPS1bOkndOs=",
	"MHiEc+Qc8w/SJ3zMHEM8pA==", "FVCxLBmoil3gY0jSX3aNJ6kR/t4="
};

/* get randomly a pair of key/accept value */
static void getkeypair(const char **key, const char **ack)
{
	int r;
	r = rand();
	while (r > 15)
		r = (r & 15) + (r >> 4);
	r = (r & 15) << 1;
	*key = compkeys[r];
	*ack = compkeys[r+1];
}

/* joins the strings using the separator */
static char *strjoin(int count, const char **strings, const char *separ)
{
	char *result, *iter;
	size_t length;
	int idx;

	/* creates the count if needed */
	if (count < 0)
		for(count = 0 ; strings[count] != NULL ; count++);

	/* compute the length of the result */
	if (count == 0)
		length = 0;
	else {
		length = (unsigned)(count - 1) * strlen(separ);
		for (idx = 0 ; idx < count ; idx ++)
			length += strlen(strings[idx]);
	}

	/* allocates the result */
	result = malloc(length + 1);
	if (result) {
		/* create the result */
		if (count != 0) {
			iter = stpcpy(result, strings[idx = 0]);
			while (++idx < count)
				iter = stpcpy(stpcpy(iter, separ), strings[idx]);
			// assert(iter - result == length);
		}
		result[length] = 0;
	}
	return result;
}

/* creates the http message for the request */
static int make_request(char **request, const char *path, const char *host, const char *key, const char *protocols)
{
	int rc = asprintf(request,
			"GET %s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"Upgrade: websocket\r\n"
			"Connection: Upgrade\r\n"
			"Sec-WebSocket-Version: 13\r\n"
			"Sec-WebSocket-Key: %s\r\n"
			"Sec-WebSocket-Protocol: %s\r\n"
			"Content-Length: 0\r\n"
			"\r\n"
			, path
			, host
			, key
			, protocols
		);
	if (rc < 0) {
		rc = -errno;
		*request = NULL;
	}
	return rc;
}

static int writeall(int fd, const void *buffer, size_t size)
{
	size_t offset;
	ssize_t ssz;

	offset = 0;
	while (size > offset) {
		ssz = write(fd, buffer + offset, size - offset);
		if (ssz >= 0)
			offset += (size_t)ssz;
		else if (errno == EAGAIN)
			usleep(10000);
		else if (errno != EINTR)
			return -1;
	}
	return 0;
}

/* create the request and send it to fd, returns the expected accept string */
static const char *send_request(int fd, const char **protocols, const char *path, const char *host)
{
	const char *key, *ack;
	char *protolist, *request;
	int length, rc;

	/* make the list of accepted protocols */
	protolist = strjoin(-1, protocols, ", ");
	if (protolist == NULL)
		return NULL;

	/* create the request */
	getkeypair(&key, &ack);
	length = make_request(&request, path, host, key, protolist);
	free(protolist);
	if (length < 0)
		return NULL;

	/* send the request */
	rc = writeall(fd, request, (size_t)length);
	free(request);
	return rc < 0 ? NULL : ack;
}

/* read a line not efficiently but without buffering */
static int receive_line(struct sd_event *eloop, int fd, char *line, int size)
{
	int rc, length = 0, cr = 0, st;
	for(;;) {
		if (length >= size)
			return X_EMSGSIZE;
		for(;;) {
			rc = (int)read(fd, line + length, 1);
			if (rc == 1)
				break;
			else if (errno == EAGAIN) {
				afb_ev_mgr_prepare();
				st = sd_event_get_state(eloop);
				if (st == SD_EVENT_INITIAL) {
					rc = sd_event_prepare(eloop);
					st = rc == 0 ? SD_EVENT_ARMED : SD_EVENT_PENDING;
				}
				if (st == SD_EVENT_ARMED) {
					rc = sd_event_wait(eloop, 10000);
					st = rc == 0 ? SD_EVENT_INITIAL : SD_EVENT_PENDING;
				}
				if (st == SD_EVENT_PENDING) {
					rc = sd_event_dispatch(eloop);
					if (rc <= 0)
						return -1;
				}
			}
			else if (errno != EINTR)
				return -1;
		}
		if (line[length] == '\r')
			cr = 1;
		else if (cr != 0 && line[length] == '\n') {
			line[--length] = 0;
			return length;
		} else
			cr = 0;
		length++;
	}
}

/* check a header */
static inline int isheader(const char *head, size_t klen, const char *key)
{
	return strncasecmp(head, key, klen) == 0 && key[klen] == 0;
}

/* receives and scan the response */
static int receive_response(struct sd_event *eloop, int fd, const char **protocols, const char *ack)
{
	char line[4096], *it;
	int rc, haserr, result = -1;
	size_t len, clen;

	/* check the header line to be something like: "HTTP/1.1 101 Switching Protocols" */
	rc = receive_line(eloop, fd, line, (int)sizeof(line));
	if (rc < 0)
		goto error;
	len = strcspn(line, " ");
	if (len != 8 || 0 != strncmp(line, "HTTP/1.1", 8))
		goto abort;
	it = line + len;
	len = strspn(it, " ");
	if (len == 0)
		goto abort;
	it += len;
	len = strcspn(it, " ");
	if (len != 3 || 0 != strncmp(it, "101", 3))
		goto abort;

	/* reads the rest of the response until empty line */
	clen = 0;
	haserr = 0;
	for(;;) {
		rc = receive_line(eloop, fd, line, (int)sizeof(line));
		if (rc < 0)
			goto error;
		if (rc == 0)
			break;
		len = strcspn(line, ": ");
		if (len != 0 && line[len] == ':') {
			/* checks the headers values */
			it = line + len + 1;
			it += strspn(it, " ,");
			it[strcspn(it, " ,")] = 0;
			if (isheader(line, len, "Sec-WebSocket-Accept")) {
				if (strcmp(it, ack) != 0)
					haserr = 1;
			} else if (isheader(line, len, "Sec-WebSocket-Protocol")) {
				result = 0;
				while(protocols[result] != NULL && strcmp(it, protocols[result]) != 0)
					result++;
			} else if (isheader(line, len, "Upgrade")) {
				if (strcmp(it, "websocket") != 0)
					haserr = 1;
			} else if (isheader(line, len, "Content-Length")) {
				clen = (long unsigned)atol(it);
			}
		}
	}

	/* skips the remaining of the message */
	while (clen >= sizeof line) {
		while (read(fd, line, sizeof line) < 0 && errno == EINTR);
		clen -= sizeof line;
	}
	if (clen > 0) {
		while (read(fd, line, len) < 0 && errno == EINTR);
	}
	if (haserr != 0 || result < 0)
		goto abort;
	return result;
abort:
	rc = X_ECONNABORTED;
error:
	return rc;
}

static int negociate(struct sd_event *eloop, int fd, const char **protocols, const char *path, const char *host)
{
	const char *ack = send_request(fd, protocols, path, host);
	return ack == NULL ? -1 : receive_response(eloop, fd, protocols, ack);
}

/* tiny parse a "standard" websock uri ws://host:port/path... */
static int parse_uri(const char *uri, char **host, char **service, const char **path, int *secured)
{
	const char *h, *p;
	size_t hlen, plen;

	/* the scheme */
	*secured = 0;
#if WITH_GNUTLS
	if (strncmp(uri, "wss://", 6) == 0) {
		*secured = 1;
		uri += 6;
	}
	else if (strncmp(uri, "https://", 6) == 0) {
		*secured = 1;
		uri += 8;
	}
	else
#endif
	if (strncmp(uri, "ws://", 5) == 0)
		uri += 5;
	else if (strncmp(uri, "http://", 6) == 0) {
		uri += 7;
	}

	/* the host */
	h = uri;
	hlen = strcspn(h, ":/");
	if (hlen == 0)
		return X_EINVAL;
	uri += hlen;

	/* the port (optional) */
	if (*uri == ':') {
		p = ++uri;
		plen = strcspn(p, "/");
		if (plen == 0)
			return X_EINVAL;
		uri += plen;
	} else {
		p = NULL;
		plen = 0;
	}

	/* the path */
	if (*uri != '/')
		return X_EINVAL;

	/* make the result */
	*host = strndup(h, hlen);
	if (*host) {
		*service = plen ? strndup(p, plen) : strdup("http");
		if (*service != NULL) {
			*path = uri;
			return 0;
		}
		free(*host);
	}
	return X_ENOMEM;
}

static const char *proto_json1[2] = { "x-afb-ws-json1",	NULL };

struct afb_wsj1 *afb_ws_client_connect_wsj1(struct sd_event *eloop, const char *uri, struct afb_wsj1_itf *itf, void *closure)
{
	int rc, fd, tls;
	char *host, *service, xhost[32];
	const char *path;
	struct addrinfo hint, *rai, *iai;
	struct afb_wsj1 *result;

	/* ensure connected */
	rc = afb_ws_client_connect_to_sd_event(eloop);
	if (rc < 0)
		return NULL;

	/* scan the uri */
	rc = parse_uri(uri, &host, &service, &path, &tls);
	if (rc < 0)
		return NULL;

	/* get addr */
	memset(&hint, 0, sizeof hint);
	hint.ai_family = AF_INET;
	hint.ai_socktype = SOCK_STREAM;
	rc = getaddrinfo(host, service, &hint, &rai);
	free(host);
	free(service);
	if (rc != 0) {
		return NULL;
	}

	/* get the socket */
	result = NULL;
	iai = rai;
	while (iai != NULL) {
		struct sockaddr_in *a = (struct sockaddr_in*)(iai->ai_addr);
		unsigned char *ipv4 = (unsigned char*)&(a->sin_addr.s_addr);
		unsigned char *port = (unsigned char*)&(a->sin_port);
		sprintf(xhost, "%d.%d.%d.%d:%d",
			(int)ipv4[0], (int)ipv4[1], (int)ipv4[2], (int)ipv4[3],
			(((int)port[0]) << 8)|(int)port[1]);
		fd = socket(iai->ai_family, iai->ai_socktype, iai->ai_protocol);
		if (fd >= 0) {
			rc = connect(fd, iai->ai_addr, iai->ai_addrlen);
			if (rc == 0)
				fcntl(fd, F_SETFL, O_NONBLOCK);
#if WITH_GNUTLS
			if (rc == 0 && tls) {
				rc = tls_upgrade_client(afb_sched_acquire_event_manager(), fd, 0/*host*/);
				if (rc >= 0) {
					afb_ev_mgr_prepare();
					fd = rc;
					rc = 0;
				}
			}
#endif
			if (rc == 0) {
				rc = negociate(eloop, fd, proto_json1, path, xhost);
				if (rc == 0) {
					result = afb_wsj1_create(fd, itf, closure);
					if (result != NULL) {
						afb_ev_mgr_prepare();
						break;
					}
				}
			}
			close(fd);
		}
		iai = iai->ai_next;
	}
	freeaddrinfo(rai);
	return result;
}

/*****************************************************************************************************************************/

static int sockopenpref(const char *uri, int server, const char *prefix, const char *scheme)
{
	int mfd, fd;
	size_t len;

	len = strlen(prefix);
	if (strncasecmp(uri, prefix, len))
		return 0;

#if WITH_GNUTLS
	int tls = uri[len] == 's' || uri[len] == 'S';
	len += !!tls;
#endif
	if (uri[len] != ':')
		return 0;
	len += uri[len + 1] == '/' && uri[len + 2] == '/' ? 3 : 1;

	fd = afb_socket_open_scheme(&uri[len], server, scheme ?: prefix);
	if (fd == 0) {
		mfd = fd;
		fd = dup(mfd);
		close(mfd);
	}
#if WITH_GNUTLS
	if (fd > 0 && tls) {
		mfd = fd;
		fd = tls_upgrade_client(afb_sched_acquire_event_manager(), mfd, 0);
		if (fd < 0)
			close(mfd);
		else
			afb_ev_mgr_prepare();
	}
#endif
	return fd;
}

static int sockopen(struct sd_event *eloop, const char *uri, int server)
{
	int rc;

	/* ensure connectable */
	rc = afb_ws_client_connect_to_sd_event(eloop);
	if (rc >= 0) {
		rc = sockopenpref(uri, server, "ws", "tcp");
		rc = rc ?: sockopenpref(uri, server, "http", "tcp");
		rc = rc ?: sockopenpref(uri, server, "tcp", 0);
		rc = rc ?: sockopenpref(uri, server, "unix", 0);
		rc = rc ?: sockopenpref(uri, server, "sd", 0);
		rc = rc ?: afb_socket_open_scheme(uri, server, 0);
	}
	return rc;
}

/*****************************************************************************************************************************/

/*
 * Establish a websocket-like client connection to the API of 'uri' and if successful
 * instantiate a client afb_proto_ws websocket for this API using 'itf' and 'closure'.
 * (see afb_proto_ws_create_client).
 * The systemd event loop 'eloop' is used to handle the websocket.
 * Returns NULL in case of failure with errno set appropriately.
 */
struct afb_proto_ws *afb_ws_client_connect_api(struct sd_event *eloop, const char *uri, struct afb_proto_ws_client_itf *itf, void *closure)
{
	struct afb_proto_ws *pws;
	int fd;

	fd = sockopen(eloop, uri, 0);
	if (fd) {
		pws = afb_proto_ws_create_client(fd, itf, closure);
		if (pws) {
			afb_ev_mgr_prepare();
			return pws;
		}
	}
	return NULL;
}

/*
 * Establish a websocket-like client connection to the API of 'uri' and if successful
 * instantiate a client afb_wsapi websocket for this API using 'itf' and 'closure'.
 * (see afb_wsapi_create).
 * The systemd event loop 'eloop' is used to handle the websocket.
 * Returns NULL in case of failure with errno set appropriately.
 */
struct afb_wsapi *afb_ws_client_connect_wsapi(struct sd_event *eloop, const char *uri, struct afb_wsapi_itf *itf, void *closure)
{
	int rc, fd;
	struct afb_wsapi *wsapi;

	fd = sockopen(eloop, uri, 0);
	if (fd >= 0) {
		rc = afb_wsapi_create(&wsapi, fd, itf, closure);
		if (rc >= 0) {
			afb_ev_mgr_prepare();
			rc = afb_wsapi_initiate(wsapi);
			if (rc >= 0)
				return wsapi;
			afb_wsapi_unref(wsapi);
		}
	}
	return NULL;
}

/*****************************************************************************/
/***       S E R V E R                                                      ***/
/******************************************************************************/

#include <systemd/sd-event.h>

struct loopcb
{
	int (*onclient)(void*,int);
	void *closure;
	char uri[];
};

static void server_listen_callback(struct ev_fd *efd, int fd, uint32_t revents, void *closure)
{
	int fdc;
	struct sockaddr addr;
	socklen_t lenaddr;
	struct loopcb *lcb = closure;

	if ((revents & EPOLLIN) != 0) {
		/* incmoing client */
		lenaddr = (socklen_t)sizeof addr;
		fdc = accept(fd, &addr, &lenaddr);
		if (fdc >= 0) {
			lcb->onclient(lcb->closure, fdc);
		}
	}
}

/* create the service */
int afb_ws_client_serve(struct sd_event *eloop, const char *uri, int (*onclient)(void*,int), void *closure)
{
	int fd, rc;
	struct ev_fd *efd;
	struct loopcb *lcb;

	rc = afb_ws_client_connect_to_sd_event(eloop);
	if (rc >= 0) {
		lcb = malloc(sizeof *lcb + 1 + strlen(uri));
		if (!lcb)
			rc = X_ENOMEM;
		else {
			rc = afb_socket_open_scheme(uri, 1, 0);
			if (rc >= 0) {
				fd = rc;
				lcb->onclient = onclient;
				lcb->closure = closure;
				strcpy(lcb->uri, uri);
				rc = afb_ev_mgr_add_fd(&efd, fd, EPOLLIN, server_listen_callback, lcb, 1, 1);
				if (rc >= 0)
					return 0;
				close(fd);
			}
			free(lcb);
		}
	}
	return rc;
}
