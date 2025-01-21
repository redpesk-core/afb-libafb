/*
 * Copyright (C) 2015-2025 IoT.bzh Company
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
#if !__ZEPHYR__
#  include <sys/un.h>
#endif
#include <time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "afb-ws-connect.h"

#include "sys/ev-mgr.h"
#include "tls/tls.h"
#include "sys/x-socket.h"
#include "sys/x-errno.h"
#include "misc/afb-verbose.h"

/**************** WebSocket handshake ****************************/

static const char *compkeys[] = {
	"ziLin6OQ0/a1+cGaI9Mupg==", "yvpxcFJAGam6huL77vz34CdShyU=",
	"fQ/ISF1mNCPRMyAj3ucqNg==", "91YY1EUelb4eMU24Z8WHhJ9cHmc=",
	"RHlfiVVE1lM1AJnErI8dFg==", "UdZQc0JaihQJV5ETCZ84Av88pxQ=",
	"NVy3L2ujXN7v3KEJwK92ww==", "+dE7iITxhExjBtf06VYNWChHqx8=",
	"cCNAgttlgELfbDDIfhujww==", "W2JiswqbTAXx5u84EtjbtqAW2Bg=",
	"K+oQvEDWJP+WXzRS5BJDFw==", "szgW10a9AuD+HtfS4ylaqWfzWAs=",
	"Ia+dgHnA9QaBrbxuqh4wgQ==", "GiGjxFdSaF0EGTl2cjvFsVmJnfM=",
	"MfpIVG082jFTV7SxTNNijQ==", "f5I2h53hBsT5ES3EHhnxAJ2nqsw=",
};

/* get randomly a pair of key/accept value */
static void getkeypair(const char **key, const char **ack)
{
	static unsigned char r = 0;
	*key = compkeys[r++];
	*ack = compkeys[r++];
	if (r >= (unsigned char)(sizeof compkeys / sizeof *compkeys))
		r = 0;
}

/* joins the strings using the separator */
static char *strjoin(int count, const char **strings, const char *separ)
{
	char *result, *iter;
	size_t length;
	int idx;

	/* creates the count if needed */
	if (count < 0) {
		count = 0;
		if (strings != NULL)
			while (strings[count] != NULL)
				count++;
	}

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
			iter = stpcpy(result, strings[0]);
			for (idx = 1 ; idx < count ; idx++)
				iter = stpcpy(stpcpy(iter, separ), strings[idx]);
			// assert(iter - result == length);
		}
		result[length] = 0;
	}
	return result;
}

/* write the buffer of size to fd */
static int writeall(int fd, const void *buffer, size_t size)
{
	size_t offset;
	ssize_t ssz;
	struct timespec ts;

	offset = 0;
	while (size > offset) {
		ssz = write(fd, (const char*)buffer + offset, size - offset);
		if (ssz >= 0)
			offset += (size_t)ssz;
		else if (errno == EAGAIN) {
			ts.tv_sec = 0;
			ts.tv_nsec = 10000000; /* 10 ms */
			nanosleep(&ts, NULL);
		}
		else if (errno != EINTR)
			return -1;
	}
	return 0;
}

/* create the request and send it to fd, returns the expected accept string */
static int send_request(
		int fd,
		const char **protocols,
		const char *path,
		const char *host,
		const char **headers,
		const char **ack
) {
	const char *key;
	char *protolist, *request, *heads;
	int length, rc;

	/* make the list of accepted protocols */
	protolist = strjoin(-1, protocols, ", ");
	if (protolist == NULL)
		return X_ENOMEM;
	heads = strjoin(-1, headers, "\r\n");
	if (heads == NULL) {
		free(protolist);
		return X_ENOMEM;
	}

	/* create the request */
	getkeypair(&key, ack);
	length = asprintf(&request,
			"GET %s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"Upgrade: websocket\r\n"
			"Connection: Upgrade\r\n"
			"Sec-WebSocket-Version: 13\r\n"
			"Sec-WebSocket-Key: %s\r\n"
			"Sec-WebSocket-Protocol: %s\r\n"
			"Content-Length: 0\r\n"
			"%s%s\r\n"
			, path
			, host
			, key
			, protolist
			, heads, *heads ? "\r\n" : ""
		);
	free(protolist);
	free(heads);
	if (length < 0)
		return X_ENOMEM;

	/* send the request */
	rc = writeall(fd, request, (size_t)length);
	free(request);
	return rc;
}

/* read a line not efficiently but without buffering */
static int receive_one_line(struct ev_mgr *mgr, int fd, char *line, int size)
{
	int rc, length = 0, cr = 0;
	for(;;) {
		if (length >= size)
			return X_EMSGSIZE;
		for(;;) {
			rc = (int)read(fd, line + length, 1);
			if (rc == 1)
				break;
			else if (errno == EAGAIN) {
				ev_mgr_prepare(mgr);
				rc = ev_mgr_wait(mgr, 100); /* 100 ms */
				if (rc > 0)
					ev_mgr_dispatch(mgr);
			}
			else if (errno != EINTR)
				return -errno;
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
static int receive_response(struct ev_mgr *mgr, int fd, const char **protocols, const char *ack)
{
	char line[4096], *it;
	int rc, haserr, result = -1;
	size_t len, clen;

	/* check the header line to be something like: "HTTP/1.1 101 Switching Protocols" */
	rc = receive_one_line(mgr, fd, line, (int)sizeof(line));
	if (rc < 0)
		goto bad_read;
	len = strcspn(line, " ");
	if (len != 8 || 0 != strncmp(line, "HTTP/1.1", 8))
		goto bad_http;
	it = line + len;
	len = strspn(it, " ");
	if (len == 0)
		goto bad_http;
	it += len;
	len = strcspn(it, " ");
	if (len != 3 || 0 != strncmp(it, "101", 3)) {
		LIBAFB_ERROR("ws-connect, no upgrade: %s", it);
		goto abort;
	}

	/* reads the rest of the response until empty line */
	clen = 0;
	haserr = 0;
	for(;;) {
		rc = receive_one_line(mgr, fd, line, (int)sizeof(line));
		if (rc < 0)
			goto bad_read;
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
					haserr |= 1;
			} else if (isheader(line, len, "Sec-WebSocket-Protocol")) {
				result = 0;
				while(protocols[result] != NULL && strcmp(it, protocols[result]) != 0)
					result++;
			} else if (isheader(line, len, "Upgrade")) {
				if (strcmp(it, "websocket") != 0)
					haserr |= 2;
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
	while (clen > 0) {
		ssize_t rlen = read(fd, line, clen > sizeof line ? sizeof line : clen);
		if (rlen >= 0)
			clen -= (size_t)rlen;
		else if (errno != EINTR)
			goto bad_read;
	}
	if (haserr == 0 && result >= 0)
		return result;

	if (result < 0)
		LIBAFB_ERROR("ws-connect, no protocol given");
	if (haserr & 1)
		LIBAFB_ERROR("ws-connect, wrong accept");
	if (haserr & 2)
		LIBAFB_ERROR("ws-connect, no websocket");
	goto abort;

bad_read:
	LIBAFB_ERROR("ws-connect, read error: %s", strerror(-rc));
	goto error;

bad_http:
	LIBAFB_ERROR("ws-connect, bad HTTP: %s", line);

abort:
	rc = X_ECONNABORTED;
error:
	return rc;
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

static int negociate(
	struct ev_mgr *mgr,
	int fd,
	const char **protocols,
	const char *path,
	const char *host,
	const char **headers
) {
	const char *ack;
	int rc = send_request(fd, protocols, path, host, headers, &ack);
	if (rc >= 0)
		rc = receive_response(mgr, fd, protocols, ack);
	return rc;
}

int afb_ws_connect(
	struct ev_mgr *mgr,
	const char *uri,
	const char **protocols,
	int *idxproto,
	const char **headers
) {
	int rc, fd, tls;
	char *host, *service;
	const char *path;
	struct addrinfo hint, *rai, *iai;

	/* scan the uri */
	rc = parse_uri(uri, &host, &service, &path, &tls);
	if (rc < 0)
		return rc;

	/* get addr */
	memset(&hint, 0, sizeof hint);
	hint.ai_family = AF_INET;
	hint.ai_socktype = SOCK_STREAM;
	rc = getaddrinfo(host, service, &hint, &rai);
	free(service);
	if (rc != 0) {
		free(host);
		return rc;
	}

	/* get the socket */
	iai = rai;
	while (iai != NULL) {
		fd = socket(iai->ai_family, iai->ai_socktype, iai->ai_protocol);
		if (fd >= 0) {
			rc = 1;
			setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &rc, (socklen_t)sizeof rc);
			rc = connect(fd, iai->ai_addr, iai->ai_addrlen);
			if (rc == 0)
				fcntl(fd, F_SETFL, O_NONBLOCK);
#if WITH_GNUTLS
			if (rc == 0 && tls) {
				rc = tls_upgrade_client(mgr, fd, 0/*host*/);
				if (rc >= 0) {
					fd = rc;
					rc = 0;
				}
			}
#endif
			if (rc == 0) {
				rc = negociate(mgr, fd, protocols, path, host, headers);
				if (rc >= 0) {
					if (idxproto != NULL)
						*idxproto = rc;
					freeaddrinfo(rai);
					free(host);
					return fd;
				}
			}
			if (fd >= 0)
				close(fd);
		}
		iai = iai->ai_next;
	}
	freeaddrinfo(rai);
	free(host);
	return X_ENOENT;
}
