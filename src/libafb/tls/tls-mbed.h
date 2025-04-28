/*
 * Copyright (C) 2015-2025 IoT.bzh Company
 * Author: Louis-Baptiste Sobolewski <lb.sobolewski@iot.bzh>
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

#if WITH_MBEDTLS

#include <stdbool.h>
#include <errno.h>

#include <mbedtls/ssl.h>
#include <rp-utils/rp-verbose.h>

static inline ssize_t tls_mbed_recv(mbedtls_ssl_context *sslctx, void *buffer, size_t length)
{
	int ssz = mbedtls_ssl_read(sslctx, buffer, length);
	if (ssz >= 0)
		return (ssize_t)ssz;
	switch(ssz) {
	case MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY:
		return 0;
	case MBEDTLS_ERR_SSL_ASYNC_IN_PROGRESS:
	case MBEDTLS_ERR_SSL_WANT_READ:
	case MBEDTLS_ERR_SSL_WANT_WRITE:
	case MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS:
	case MBEDTLS_ERR_SSL_UNEXPECTED_MESSAGE:
		errno = EAGAIN;
		break;
	default:
		RP_ERROR("got mbed read error %d", (int)ssz);
		errno = EINVAL;
		break;
	}
	return -1;
}

static inline ssize_t tls_mbed_send(mbedtls_ssl_context *sslctx, const void *buffer, size_t length)
{
	for (;;) {
		int ssz = mbedtls_ssl_write(sslctx, buffer, length);
		if (ssz >= 0)
			return (ssize_t)ssz;
		switch(ssz) {
		case MBEDTLS_ERR_SSL_ASYNC_IN_PROGRESS:
		case MBEDTLS_ERR_SSL_WANT_READ:
		case MBEDTLS_ERR_SSL_WANT_WRITE:
		case MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS:
			break;
		default:
			RP_ERROR("got mbed write error %d", (int)ssz);
			errno = EINVAL;
			return -1;
		}
	}
}

extern
int tls_mbed_session_create(
	mbedtls_ssl_context *context,
	mbedtls_ssl_config  *config,
	int fd,
	bool server,
	bool mtls,
	const char *host);

extern int tls_mbed_has_cert();
extern int tls_mbed_has_key();
extern int tls_mbed_has_trust();

extern int tls_mbed_set_cert(const void *cert, size_t size);
extern int tls_mbed_set_key(const void *key, size_t size);
extern int tls_mbed_add_trust(const void *trust, size_t size);

#if !WITHOUT_FILESYSTEM
extern int tls_mbed_load_cert(const char *path);
extern int tls_mbed_load_key(const char *path);
extern int tls_mbed_load_trust(const char *path);
#endif

#endif
