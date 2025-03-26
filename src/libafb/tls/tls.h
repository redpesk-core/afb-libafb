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
/*******************************************************************************************/
#if WITH_GNUTLS

#include "tls-gnu.h"

typedef struct
	{
		gnutls_session_t session;
		gnutls_certificate_credentials_t creds;
	}
	tls_session_t;

static inline
void tls_init(tls_session_t *session)
{
	session->session = NULL;
	session->creds = NULL;
}

static inline
void tls_release(tls_session_t *session)
{
	if (session->session != NULL)
		gnutls_deinit(session->session);
	if (session->creds != NULL)
		gnutls_certificate_free_credentials(session->creds);
	tls_init(session);
}

static inline
ssize_t tls_recv(tls_session_t *session, void *buffer, size_t length)
{
	return tls_gnu_recv(session->session, buffer, length);
}

static inline
ssize_t tls_send(tls_session_t *session, const void *buffer, size_t length)
{
	return tls_gnu_send(session->session, buffer, length);
}

static inline
int tls_creds_init(
	tls_session_t *session,
	bool           server,
	const char    *cert_path,
	const char    *key_path,
	const char    *trust_path
) {
	(void)server;
	return tls_gnu_creds_init(&session->creds, cert_path, key_path, trust_path);
}

static inline
int tls_session_init(tls_session_t *session, bool server, int fd, const char *host)
{
	return tls_gnu_session_init(&session->session, session->creds, server, fd, host);
}
#endif
#endif
/*******************************************************************************************/
#endif

