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

#if WITH_GNUTLS

#include <stdbool.h>
#include <errno.h>

#include <gnutls/gnutls.h>

struct ev_mgr;

extern int tls_gnu_upgrade_client(struct ev_mgr *mgr, int sd, const char *hostname);

static inline ssize_t tls_gnu_recv(gnutls_session_t session, void *buffer, size_t length)
{
	for (;;) {
		ssize_t ssz = gnutls_record_recv(session, buffer, length);
		if (ssz >= 0)
			return ssz;
		if (ssz != GNUTLS_E_INTERRUPTED) {
			if (ssz == GNUTLS_E_AGAIN)
				errno = EAGAIN;
			return -1;
		}
	}
}

static inline ssize_t tls_gnu_send(gnutls_session_t session, const void *buffer, size_t length)
{
	for (;;) {
		ssize_t ssz = gnutls_record_send(session, buffer, length);
		if (ssz >= 0)
			return ssz;
		if (ssz != GNUTLS_E_INTERRUPTED && ssz != GNUTLS_E_AGAIN)
			return -1;
	}
}

extern
int tls_gnu_session_create(
	gnutls_session_t *session,
	gnutls_certificate_credentials_t *creds,
	int fd,
	bool server,
	bool mtls,
	const char *host);

extern int tls_gnu_has_cert();
extern int tls_gnu_has_key();
extern int tls_gnu_has_trust();

extern int tls_gnu_set_cert(const void *cert, size_t size);
extern int tls_gnu_set_key(const void *key, size_t size);
extern int tls_gnu_add_trust(const void *trust, size_t size);

#if !WITHOUT_FILESYSTEM
extern int tls_gnu_load_cert(const char *path);
extern int tls_gnu_load_key(const char *path);
extern int tls_gnu_load_trust(const char *path);
#endif

#endif
