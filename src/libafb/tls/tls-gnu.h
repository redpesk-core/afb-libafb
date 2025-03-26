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

/**
 * @brief initializes a GnuTLS credentials object
 *
 * All certificates and keys provided must be in PEM format
 *
 * @param creds pointer where to store the address of the allocated credentials object
 * @param cert_path path to the certificate
 * @param key_path path to the private key matching the certificate
 * @param trust_path path to the directory containing the trusted certificates, NULL to use system trust dir
 *
 * @return 0 if OK, <0 if KO (in which case creds is freed for you)
 */
extern int tls_gnu_creds_init(gnutls_certificate_credentials_t *creds, const char *cert_path, const char *key_path, const char *trust_path);

/**
 * @brief initializes a GnuTLS session object
 *
 * @param session pointer where to store the address of the allocated session object
 * @param creds credentials to use for the session (see tls_gnu_creds_init)
 * @param server true if server, false if client
 * @param fd socket file descriptor to receive and transmit TLS data
 * @param host hostname or IP address of the peer
 *
 * @return 0 if OK, <0 if KO (in which case session is freed for you)
 */
extern int tls_gnu_session_init(gnutls_session_t *session, gnutls_certificate_credentials_t creds, bool server, int fd, const char *host);

#endif
