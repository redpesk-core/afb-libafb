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

#include "../libafb-config.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#if __ZEPHYR__
#  include <sys/fcntl.h>
#endif

#include <rp-utils/rp-verbose.h>

#include "misc/afb-socket.h"

#include "sys/x-socket.h"
#include "sys/x-errno.h"
#include "sys/x-alloca.h"

/******************************************************************************/
#define BACKLOG  5

/******************************************************************************/

/**
 * known types
 */
enum type {
	/** type internet */
	Type_Inet,

	/** type systemd */
	Type_Systemd,

	/** type virtual socket of L4 */
	Type_L4,

	/** type Unix */
	Type_Unix,

	/** type char */
	Type_Char
};

/**
 * Structure for known entries
 */
struct entry
{
	/** the known prefix */
	const char *prefix;

	/** the type of the entry */
	unsigned type: 3;

	/** should not set SO_REUSEADDR for servers */
	unsigned noreuseaddr: 1;

	/** should not call listen for servers */
	unsigned nolisten: 1;
};

/**
 * The known entries with the default one at the first place
 */
static struct entry entries[] = {
	{
		.prefix = "tcp:",
		.type = Type_Inet
	},
	{
		.prefix = "sd:",
		.type = Type_Systemd,
		.noreuseaddr = 1,
		.nolisten = 1
	},
	{
		.prefix = "l4vsock:",
		.type = Type_L4
	},
	{
		.prefix = "unix:",
		.type = Type_Unix
	},
	{
		.prefix = "char:",
		.type = Type_Char
	}
};

/**
 * It is possible to set explicit api name instead of using the
 * default one.
 */
static const char as_api[] = "?as-api=";

/******************************************************************************/
#if WITH_UNIX_SOCKET
/**
 * open a unix domain socket for client or server
 *
 * @param spec the specification of the path (prefix with @ for abstract)
 * @param server 0 for client, server otherwise
 *
 * @return the file descriptor number of the socket or -1 in case of error
 */
static int open_unix(const char *spec, int server)
{
	int fd, rc, abstract;
	struct sockaddr_un addr;
	size_t length;

	abstract = spec[0] == '@';

	/* check the length */
	length = strlen(spec);
	if (length >= 108)
		return X_ENAMETOOLONG;

	/* create a  socket */
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return fd;

	/* remove the file on need */
	if (server && !abstract)
		unlink(spec);

	/* prepare address  */
	memset(&addr, 0, sizeof addr);
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, spec);
	if (abstract)
		addr.sun_path[0] = 0; /* implement abstract sockets */

	length += offsetof(struct sockaddr_un, sun_path) + !abstract;
	if (server) {
		rc = bind(fd, (struct sockaddr *) &addr, (socklen_t)length);
	} else {
		rc = connect(fd, (struct sockaddr *) &addr, (socklen_t)length);
	}
	if (rc < 0) {
		close(fd);
		return rc;
	}
	return fd;
}
#endif

/******************************************************************************/
#if WITH_TCP_SOCKET
/**
 * open a tcp socket for client or server
 *
 * @param spec the specification of the host:port/...
 * @param server 0 for client, server otherwise
 *
 * @return the file descriptor number of the socket or -1 in case of error
 */
static int open_tcp(const char *spec, int server, int reuseaddr)
{
	int rc, fd, isport;
	unsigned len;
	long port;
	const char *service, *host, *tail;
	char *tmp;
	struct addrinfo hints[2], *rai, *iai;
	struct sockaddr_in addr4;
	struct sockaddr_in6 addr6;

	/* scan the uri */
	tail = strchrnul(spec, '/');
	service = strchr(spec, ':');
	if (tail == NULL || service == NULL || tail < service)
		return X_EINVAL;
        len = (unsigned)(service++ - spec);
        tmp = alloca(len + 1);
        memcpy(tmp, spec, len);
        tmp[len] = 0;
        host = tmp;
        len = (unsigned)(tail - service);
        tmp = alloca(len + 1);
        memcpy(tmp, service, len);
        tmp[len] = 0;
        service = tmp;
	port = strtol(service, &tmp, 10);
	isport = (tmp != service) && *tmp == '\0' && port > 0 && port <= 65535;

	/* get addr */
	memset(&hints, 0, sizeof hints);
	hints[0].ai_family = AF_INET;
	hints[0].ai_socktype = SOCK_STREAM;
	if (server) {
		hints[0].ai_flags = AI_PASSIVE;
		if (host[0] == 0 || (host[0] == '*' && host[1] == 0))
			host = NULL;
	}
	if (host == NULL && isport && server) {
		hints[0].ai_addrlen = sizeof addr4;
		hints[0].ai_addr = (void*)&addr4;
		hints[0].ai_canonname = "*";
		hints[0].ai_next = &hints[1];
		hints[1].ai_family = AF_INET6;
		hints[1].ai_socktype = SOCK_STREAM;
		hints[1].ai_addrlen = sizeof addr6;
		hints[1].ai_addr = (void*)&addr6;
		hints[1].ai_canonname = "*";
		hints[1].ai_next = NULL;
		memset(&addr4, 0, sizeof addr4);
		addr4.sin_family = AF_INET;
		addr4.sin_port = (in_port_t)htons((in_port_t)port);
		memset(&addr6, 0, sizeof addr6);
		addr6.sin6_family = AF_INET6;
		addr6.sin6_port = (in_port_t)htons((in_port_t)port);
		rai = NULL;
		iai = hints;
	}
	else {
		rc = getaddrinfo(host, service, &hints[0], &rai);
		if (rc != 0) {
			switch(rc) {
			case EAI_MEMORY:
				return X_ENOMEM;
			default:
				return X_ECANCELED;
			}
		}
		/* check emptiness */
		if (!rai)
			return X_ENOENT;
		iai = rai;
	}

	/* get the socket */
	while (iai != NULL) {
		fd = socket(iai->ai_family, iai->ai_socktype, iai->ai_protocol);
		if (fd >= 0) {
			if (server) {
				if (reuseaddr) {
					rc = 1;
					setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &rc, sizeof rc);
				}
				rc = bind(fd, iai->ai_addr, iai->ai_addrlen);
			} else {
				rc = 1;
				setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &rc, (socklen_t)sizeof rc);
				rc = connect(fd, iai->ai_addr, iai->ai_addrlen);
			}
			if (rc == 0) {
				if (rai != NULL)
					freeaddrinfo(rai);
				return fd;
			}
			close(fd);
		}
		iai = iai->ai_next;
	}
	if (rai != NULL)
		freeaddrinfo(rai);
	return -errno;
}
#endif

/******************************************************************************/
#if WITH_SYSD_SOCKET

#include "sys/systemd.h"

/**
 * open a systemd socket for server
 *
 * @param spec the specification of the systemd name
 *
 * @return the file descriptor number of the socket or -1 in case of error
 */
static int open_systemd(const char *spec)
{
	return systemd_fds_for(spec);
}
#endif
/******************************************************************************/
#if WITH_L4VSOCK

struct sockaddr_l4
{
  unsigned short sl4_family;
  unsigned short port;
  char name[8];
  char _pad[4];
};

enum address_families_l4
{
  AF_VIO_SOCK = 50,  /* virtio-based sockets, must match the number in Linux */
  PF_VIO_SOCK = AF_VIO_SOCK,
};

#define DEFAULT_L4VSOCK_PORT 7777

/**
 * open a L4 VSOCK socket for client or server
 *
 * @param spec the specification of the path (prefix with @ for abstract)
 * @param server 0 for client, server otherwise
 *
 * @return the file descriptor number of the socket or -1 in case of error
 */
static int open_l4(const char *spec, int server)
{
	int fd, rc;
	struct sockaddr_l4 addr;
	const char *port, *slash;
	unsigned short portnum;
	size_t length;

	/* scan the spec */
	port = strchr(spec, ':');
	slash = strchr(spec, '/');
	if (port && slash && slash < port) {
		return X_EINVAL;
	}
	if (port) {
		rc = atoi(port + 1);
		if (rc <= 0 && rc > UINT16_MAX) {
			return X_EINVAL;
		}
		portnum = (unsigned short)rc;
		length = port - spec;
	} else {
		portnum = DEFAULT_L4VSOCK_PORT;
		length = slash ? slash - spec : strlen(spec);
	}

	/* check the length */
	if (length >= sizeof addr.name) {
		return X_ENAMETOOLONG;
	}

	/* create a  socket */
	fd = socket(PF_VIO_SOCK, SOCK_STREAM, 0);
	if (fd < 0)
		return fd;

	/* prepare address  */
	memset(&addr, 0, sizeof addr);
	addr.sl4_family = AF_VIO_SOCK;
	addr.port = portnum;
	memcpy(addr.name, spec, length);

	if (server) {
		rc = bind(fd, (struct sockaddr *) &addr, (socklen_t)(sizeof addr));
	} else {
		rc = connect(fd, (struct sockaddr *) &addr, (socklen_t)(sizeof addr));
	}
	if (rc < 0) {
		close(fd);
		return rc;
	}
	return fd;
}
#endif

/******************************************************************************/

/**
 * Get the entry of the uri by searching to its prefix
 *
 * @param uri the searched uri
 * @param offset where to store the prefix length
 * @param scheme the default scheme to use if none is set in uri (can be NULL)
 *
 * @return the found entry or the default one
 */
static struct entry *get_entry(const char *uri, int *offset, const char *scheme)
{
	int len, i, deflen;

	/* search as prefix of URI */
	i = (int)(sizeof entries / sizeof * entries);
	while (i) {
		i--;
		len = (int)strlen(entries[i].prefix);
		if (!strncmp(uri, entries[i].prefix, (size_t)len))
			goto end; /* found */
	}

	/* not a prefix of uri */
	len = 0;

	/* search default scheme if given and valid */
	if (scheme && *scheme) {
		deflen = (int)strlen(scheme);
		deflen += (scheme[deflen - 1] != ':'); /* add virtual trailing colon */
		i = (int)(sizeof entries / sizeof * entries);
		while (i) {
			i--;
			if (deflen == (int)strlen(entries[i].prefix)
			 && !strncmp(scheme, entries[i].prefix, (size_t)(deflen - 1)))
				goto end; /* found */
		}
	}

end:
	*offset = len;
	return &entries[i];
}

/**
 * open socket for client or server
 *
 * @param uri the specification of the socket
 * @param server 0 for client, server otherwise
 * @param scheme the default scheme to use if none is set in uri (can be NULL)
 *
 * @return the file descriptor number of the socket or -1 in case of error
 */
static int open_uri(const char *uri, int server, const char *scheme)
{
	int fd, offset, rc;
	struct entry *e;
	const char *api;

	/* search for the entry */
	e = get_entry(uri, &offset, scheme);

	/* get the names */
	uri += offset;
	api = strchr(uri, '?');
	if (api) {
		unsigned len = (unsigned)(api - uri);
		char *tmp = alloca(len + 1);
		memcpy(tmp, uri, len);
		tmp[len] = 0;
		uri = tmp;
	}

	/* open the socket */
	switch (e->type) {
#if WITH_UNIX_SOCKET
	case Type_Unix:
		fd = open_unix(uri, server);
		break;
#endif
#if WITH_TCP_SOCKET
	case Type_Inet:
		fd = open_tcp(uri, server, !e->noreuseaddr);
		break;
#endif
#if WITH_SYSD_SOCKET
	case Type_Systemd:
		if (server)
			fd = open_systemd(uri);
		else
			fd = X_EINVAL;
		break;
#endif
#if WITH_L4VSOCK
	case Type_L4:
		fd = open_l4(uri, server);
		break;
#endif
	case Type_Char:
		fd = open(uri, O_RDWR);
		break;
	default:
		fd = X_ENOTSUP;
		break;
	}
	if (fd < 0)
		return fd;

	/* set it up */
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	fcntl(fd, F_SETFL, O_NONBLOCK);
	if (server) {
		if (!e->nolisten) {
			rc = listen(fd, BACKLOG);
			if (rc < 0) {
				close(fd);
				fd = rc;
			}
		}
	}
	return fd;
}

/**
 * open socket for client or server
 *
 * @param uri the specification of the socket
 * @param server 0 for client, server otherwise
 * @param scheme the default scheme to use if none is set in uri (can be NULL)
 *
 * @return the file descriptor number of the socket or -1 in case of error
 */
int afb_socket_open_scheme(const char *uri, int server, const char *scheme)
{
	int fd = open_uri(uri, server, scheme);
	if (fd < 0)
		RP_ERROR("can't open %s socket for %s: %s", server ? "server" : "client", uri, strerror(-fd));
	return fd;
}

/**
 * Get the api name of the uri
 *
 * @deprecated only works when API name is the very last part of the URI
 *
 * @param uri the specification of the socket
 *
 * @return the api name or NULL if none can be deduced
 */
const char *afb_socket_api(const char *uri)
{
	int offset;
	const char *api;
	struct entry *entry;

	entry = get_entry(uri, &offset, NULL);
	uri += offset;
	uri += (entry->type == Type_Unix && *uri == '@');
	api = strstr(uri, as_api);
	if (api)
		api += sizeof as_api - 1;
	else {
		api = strrchr(uri, '/');
		if (api)
			api++;
		else
			api = uri;
		if (strchr(api, ':'))
			api = NULL;
	}
	return api;
}
