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

#pragma once

#include "../libafb-config.h"

#if WITH_TLS
/*****************************************************************************************/
/*
This file make abstraction of the TLS backend used by afb-libafb.

It declares the abstract type `tls_session_t` that holds a TLS sockect.

For that type, `tls_session_t`, the following methods are to be defined
by implementations:

  - void tls_init(tls_session_t*)

	Initialization of the session before any other usage

  - void tls_release(tls_session_t*)

	Release the session and all its data

  - ssize_t tls_recv(tls_session_t*, void*, size_t)

	For reading data, like read

  - ssize_t tls_send(tls_session_t*, const void*, size_t)

	For writing data, like write



It also defines functions for defining certificate, key and trust.
These functions are splitted in 2 groups: the group that pass the
DER or PEM value in a buffer and the group that loads it from the
filesystem. Functions laoding from file system are only defined if
WITHOUT_FILESYSTEM is either 0 or undefined.

Group of functions taking buffer:

  - int tls_set_cert(const void *, size_t)

	Set the certificate (DER or PEM, auto detect) if not already set

  - int tls_set_key(const void *, size_t)

	Set the private key (DER or PEM, auto detect) if not already set

  - int tls_add_trust(const void *, size_t)

	Add one or more certificate (DER or PEM, auto detect)
	to the list of trust

Group of functions taking path (if !WITHOUT_FILESYSTEM)

  - int tls_load_cert(const char *)

	Set the certificate (DER or PEM, auto detect) if not already set
	from the file of given path

  - int tls_load_key(const char *)

	Set the private key (DER or PEM, auto detect) if not already set
	from the file of given path

  - int tls_load_trust(const char *)

	Add one or more certificate (DER or PEM, auto detect)
	to the list of trust from the file of given path (directory
        or file, auto detect)

*/
/*****************************************************************************************/
#if WITH_GNUTLS

#include "tls-gnu.h"

typedef gnutls_session_t tls_session_t;

static inline
void tls_init(tls_session_t *session)
{
	*session = NULL;
}

static inline
void tls_release(tls_session_t *session)
{
	if (*session != NULL)
		gnutls_deinit(*session);
	tls_init(session);
}

static inline
ssize_t tls_recv(tls_session_t *session, void *buffer, size_t length)
{
	return tls_gnu_recv(*session, buffer, length);
}

static inline
ssize_t tls_send(tls_session_t *session, const void *buffer, size_t length)
{
	return tls_gnu_send(*session, buffer, length);
}

static inline
int tls_session_create(tls_session_t *session, int fd, bool server, bool mtls, const char *host)
{
	return tls_gnu_session_create(session, fd, server, mtls, host);
}

static inline
int tls_has_cert()
{
	return tls_gnu_has_cert();
}

static inline
int tls_has_key()
{
	return tls_gnu_has_key();
}

static inline
int tls_has_trust()
{
	return tls_gnu_has_trust();
}

static inline
int tls_set_cert(const void *cert, size_t size)
{
	return tls_gnu_set_cert(cert, size);
}

static inline
int tls_set_key(const void *key, size_t size)
{
	return tls_gnu_set_key(key, size);
}

static inline
int tls_add_trust(const void *trust, size_t size)
{
	return tls_gnu_add_trust(trust, size);
}

#if !WITHOUT_FILESYSTEM
static inline
int tls_load_cert(const char *path)
{
	return tls_gnu_load_cert(path);
}

static inline
int tls_load_key(const char *path)
{
	return tls_gnu_load_key(path);
}

static inline
int tls_load_trust(const char *path)
{
	return tls_gnu_load_trust(path);
}
#endif
#endif
/*****************************************************************************************/
#if WITH_MBEDTLS

#include "tls-mbed.h"

typedef struct
	{
		mbedtls_ssl_context context;
		mbedtls_ssl_config  config;
	}
	tls_session_t;

static inline
void tls_init(tls_session_t *session)
{
	mbedtls_ssl_init(&session->context);
	mbedtls_ssl_config_init(&session->config);
}

static inline
void tls_release(tls_session_t *session)
{
	mbedtls_ssl_free(&session->context);
	mbedtls_ssl_config_free(&session->config);
	tls_init(session);
}

static inline
ssize_t tls_recv(tls_session_t *session, void *buffer, size_t length)
{
	return tls_mbed_recv(&session->context, buffer, length);
}

static inline
ssize_t tls_send(tls_session_t *session, const void *buffer, size_t length)
{
	return tls_mbed_send(&session->context, buffer, length);
}

static inline
int tls_session_create(tls_session_t *session, int fd, bool server, bool mtls, const char *host)
{
	return tls_mbed_session_create(&session->context, &session->config, fd, server, mtls, host);
}

static inline
int tls_has_cert()
{
	return tls_mbed_has_cert();
}

static inline
int tls_has_key()
{
	return tls_mbed_has_key();
}

static inline
int tls_has_trust()
{
	return tls_mbed_has_trust();
}

static inline
int tls_set_cert(const void *cert, size_t size)
{
	return tls_mbed_set_cert(cert, size);
}

static inline
int tls_set_key(const void *key, size_t size)
{
	return tls_mbed_set_key(key, size);
}

static inline
int tls_add_trust(const void *trust, size_t size)
{
	return tls_mbed_add_trust(trust, size);
}

#if !WITHOUT_FILESYSTEM
static inline
int tls_load_cert(const char *path)
{
	return tls_mbed_load_cert(path);
}

static inline
int tls_load_key(const char *path)
{
	return tls_mbed_load_key(path);
}

static inline
int tls_load_trust(const char *path)
{
	return tls_mbed_load_trust(path);
}
#endif


#endif
/*****************************************************************************************/
#endif

