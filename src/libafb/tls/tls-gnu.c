/*
 * Copyright (C) 2015-2024 IoT.bzh Company
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

#include "../libafb-config.h"

#if WITH_GNUTLS

#include <stdbool.h>
#include <stdlib.h>

#include <gnutls/gnutls.h>
#include <rp-utils/rp-verbose.h>

#include "sys/x-errno.h"

static gnutls_priority_t priority_cache;

/* disable DTLS and all TLS versions before TLS 1.3 */
#define CIPHER_PRIORITY "SECURE128:-VERS-DTLS-ALL:-VERS-SSL3.0:-VERS-TLS1.0:-VERS-TLS1.1:-VERS-TLS1.2"

int tls_gnu_creds_init(gnutls_certificate_credentials_t *creds, const char *cert_path, const char *key_path, const char *trust_path)
{
    int rc;

    /* check version */
    if (gnutls_check_version("3.6.5") == NULL) {
        RP_ERROR("GnuTLS 3.6.5 or later is required");
        return X_ENOTSUP;
    }

    /* X509 stuff */
    rc = gnutls_certificate_allocate_credentials(creds);
    if (rc < 0) {
        RP_ERROR("out of memory");
        goto error;
    }

    if (trust_path) {
        /* use local trust dir */
        rc = gnutls_certificate_set_x509_trust_dir(*creds, trust_path, GNUTLS_X509_FMT_PEM);
        if (rc < 0) {
            RP_ERROR("couldn't set local trust directory");
            goto error;
        }
    }
    else {
        /* use the system's trusted CAs */
        rc = gnutls_certificate_set_x509_system_trust(*creds);
        if (rc < 0) {
            RP_ERROR("couldn't use system's trusted CAs");
            goto error;
        }
    }


    /* set certificate */
    rc = gnutls_certificate_set_x509_key_file(*creds, cert_path, key_path, GNUTLS_X509_FMT_PEM);
    if (rc < 0) {
        RP_ERROR("failed to set certificate/private key pair");
        goto error;
    }

    return 0;

error:
    gnutls_certificate_free_credentials(*creds);
    RP_ERROR("%s, %s", gnutls_strerror_name(rc), gnutls_strerror(rc));
    return rc;
}

int tls_gnu_session_init(gnutls_session_t *session, gnutls_certificate_credentials_t creds, bool server, int fd, const char *host)
{
    int rc;
    gnutls_init_flags_t flag = server ? GNUTLS_SERVER : GNUTLS_CLIENT;

    /* initialize session */
    rc = gnutls_init(session, flag);
    if (rc != GNUTLS_E_SUCCESS) {
        RP_ERROR("failed to initialize GnuTLS session");
        goto error;
    }

    /* set cipher priority cache if not done yet */
    if (priority_cache == NULL)
        rc = gnutls_priority_init(&priority_cache, CIPHER_PRIORITY, NULL);
    if (rc != GNUTLS_E_SUCCESS) {
        RP_ERROR("failed to set cipher preferences");
        goto error;
    }

    /* set cipher priority */
    rc = gnutls_priority_set(*session, priority_cache);
    if (rc != GNUTLS_E_SUCCESS) {
        RP_ERROR("failed to set GnuTLS session cipher priority");
        goto error;
    }

    /* set credentials */
    rc = gnutls_credentials_set(*session, GNUTLS_CRD_CERTIFICATE, creds);
    if (rc != GNUTLS_E_SUCCESS) {
        RP_ERROR("failed to set GnuTLS session credentials");
        goto error;
    }

    /* require client certificate */
    if (server)
        gnutls_certificate_server_set_request(*session, GNUTLS_CERT_REQUIRE);

    /* check server certificate */
    // TODO allow callback (whitelist use case), see gnutls_session_set_verify_function
    if (server)
        gnutls_session_set_verify_cert(*session, NULL, 0);
    else
        gnutls_session_set_verify_cert(*session, host, 0);

    /* set transport */
    gnutls_transport_set_int(*session, fd);

    /* handshake */
    gnutls_handshake_set_timeout(*session, 3000);
    do {
        rc = gnutls_handshake(*session);
    } while (rc != GNUTLS_E_SUCCESS && !gnutls_error_is_fatal(rc));
    if (rc != GNUTLS_E_SUCCESS) {
        RP_ERROR("GnuTLS handshake failed");
        goto error;
    }

    return 0;

error:
    gnutls_deinit(*session);
    RP_ERROR("%s, %s", gnutls_strerror_name(rc), gnutls_strerror(rc));
    return rc;
}

#endif
