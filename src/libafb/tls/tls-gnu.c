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

#include "../libafb-config.h"

#if WITH_GNUTLS

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

#include <rp-utils/rp-verbose.h>

#include "sys/x-errno.h"
#include "sys/ev-mgr.h"

enum state
{
	state_handshake,
	state_established,
	state_bye,
	state_dead
};

#define BUFSZ 1024

struct tls_flow
{
	struct ev_fd *efd;
	int fd;
	unsigned clen;
	char buffer[BUFSZ];
};

struct tls
{
        gnutls_session_t session;
	enum state state;
	struct tls_flow crypt;
	struct tls_flow plain;
	char hostname[];
};

static void bye_cb(struct ev_fd *efd, int fd, uint32_t revents, void *closure);
static void crypt_cb(struct ev_fd *efd, int fd, uint32_t revents, void *closure);
static void plain_cb(struct ev_fd *efd, int fd, uint32_t revents, void *closure);
static void handshake_cb(struct ev_fd *efd, int fd, uint32_t revents, void *closure);

static int initialized;

static gnutls_certificate_credentials_t xcred;

static gnutls_priority_t priority_cache;

/* disable DTLS and all TLS versions before TLS 1.3 */
#define CIPHER_PRIORITY "SECURE128:-VERS-DTLS-ALL:-VERS-SSL3.0:-VERS-TLS1.0:-VERS-TLS1.1:-VERS-TLS1.2"

static int initialize()
{
	int rc;

	/* lazy initialization */
	rc = initialized;
	if (rc != 0)
		return rc;

	/* check version */
	if (gnutls_check_version("3.4.6") == NULL) {
		RP_ERROR("GnuTLS 3.4.6 or later is required");
		return initialized = X_ENOTSUP;
	}

        /* X509 stuff */
        rc = gnutls_certificate_allocate_credentials(&xcred);
	if (rc < 0)
		return initialized = X_ENOMEM;

        /* sets the system trusted CAs for Internet PKI */
        rc = gnutls_certificate_set_x509_system_trust(xcred);
	if (rc < 0)
		return initialized = X_ECANCELED;

        /* If client holds a certificate it can be set using the following:
         *
         gnutls_certificate_set_x509_key_file (xcred, "cert.pem", "key.pem",
         GNUTLS_X509_FMT_PEM);
         */

	return initialized = 1;
}

static void terminate(struct tls *tls, const char *error)
{
	if (tls->state == state_dead)
		return;

	if (tls->state == state_established) {
		tls->state = state_bye;
		ev_fd_set_events(tls->crypt.efd, EV_FD_IN);
		ev_fd_set_handler(tls->crypt.efd, bye_cb, tls);
		gnutls_bye (tls->session, GNUTLS_SHUT_WR);
		return;
	}

	tls->state = state_dead;
	ev_fd_unref(tls->crypt.efd);
	ev_fd_unref(tls->plain.efd);
        gnutls_deinit(tls->session);
	free(tls);
}

static void do_write(struct tls *tls, struct tls_flow *in, struct tls_flow *out)
{
	unsigned len, off;
	ssize_t ssz;

	len = in->clen;
	ssz = !len
		? 0
		: out == &tls->crypt
			? gnutls_record_send(tls->session, in->buffer, len)
			: write(out->fd, in->buffer, len);
	if (ssz > 0) {
		off = (unsigned)ssz;
		len -= off;
		in->clen = len;
		if (len)
			memmove(in->buffer, &in->buffer[off], len);
	}
	ev_fd_set_events(out->efd, len ? EV_FD_IN|EV_FD_OUT : EV_FD_IN);
}

static void do_read_write(struct tls *tls, struct tls_flow *in, struct tls_flow *out)
{
	ssize_t ssz;
	unsigned len, loop = 1;

	while(loop) {
		len = in->clen;
		if (len < sizeof in->buffer) {
			if (in == &tls->crypt) {
				ssz = gnutls_record_recv(tls->session, &in->buffer[len], sizeof in->buffer - len);
				loop = ssz > 0 || ssz == GNUTLS_E_INTERRUPTED;
			}
			else {
				ssz = read(in->fd, &in->buffer[len], sizeof in->buffer - len);
				loop = ssz > 0 || (ssz < 0 && errno == EINTR);
			}
			if (ssz > 0) {
				len += (unsigned)ssz;
				in->clen = len;
			}
			else
				len += loop; /* ensure an extra loop if INTR */
		}
		if (in->clen)
			do_write(tls, in, out);
		loop = loop && len > in->clen;
	}
}

static void do_decrypt(struct tls *tls)
{
	do_read_write(tls, &tls->crypt, &tls->plain);
}

static void do_crypt(struct tls *tls)
{
	do_read_write(tls, &tls->plain, &tls->crypt);
}

static void do_decrypt_next(struct tls *tls)
{
	do_write(tls, &tls->crypt, &tls->plain);
}

static void do_crypt_next(struct tls *tls)
{
	do_write(tls, &tls->plain, &tls->crypt);
}

static void bye_cb(struct ev_fd *efd, int fd, uint32_t revents, void *closure)
{
	struct tls *tls = closure;

	if (revents & EV_FD_IN) {
		do_decrypt(tls);
	}
}

/* callback of external crypt side */
static void crypt_cb(struct ev_fd *efd, int fd, uint32_t revents, void *closure)
{
	struct tls *tls = closure;

	if (revents & EV_FD_HUP) {
		terminate(tls, 0);
		return;
	}
	if (revents & EV_FD_OUT) {
		do_crypt_next(tls);
	}
	if (revents & EV_FD_IN) {
		do_decrypt(tls);
	}
}

/* callback of internal plain side */
static void plain_cb(struct ev_fd *efd, int fd, uint32_t revents, void *closure)
{
	struct tls *tls = closure;

	if (revents & EV_FD_HUP) {
		terminate(tls, 0);
		return;
	}
	if (revents & EV_FD_OUT) {
		do_decrypt_next(tls);
	}
	if (revents & EV_FD_IN) {
		do_crypt(tls);
	}
}

static int do_handshake(struct tls *tls)
{
	int rc;

        rc = gnutls_handshake(tls->session);
	if (rc != GNUTLS_E_SUCCESS) {
                if (!gnutls_error_is_fatal(rc))
                        return 0;
                terminate(tls, "fatal handshake");
                return X_ECANCELED;
	}

	tls->state = state_established;
	ev_fd_set_events(tls->crypt.efd, EV_FD_IN);
	ev_fd_set_events(tls->plain.efd, EV_FD_IN);
	ev_fd_set_handler(tls->crypt.efd, crypt_cb, tls);
        return 0;
}

static void handshake_cb(struct ev_fd *efd, int fd, uint32_t revents, void *closure)
{
	struct tls *tls = closure;

	if (revents & EV_FD_HUP) {
		terminate(tls, 0);
		return;
	}

	gnutls_handshake(tls->session);
}

int tls_gnu_upgrade_client(struct ev_mgr *mgr, int sd, const char *hostname)
{
	int rc, pairfd[2];
	struct tls *tls;

	/* initialization */
	rc = initialize();
	if (rc < 0)
		goto error;

	/* create the underlying socket pair */
	rc = socketpair(AF_UNIX, SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, 0, pairfd);
	if (rc < 0) {
		rc = X_EBUSY;
		goto error;
	}

	/* allocates the struct */
	tls = malloc(sizeof *tls + (hostname ? 1 + strlen(hostname) : 0));
	if (!tls) {
		rc = X_ENOMEM;
		goto error2;
	}

        /* Initialize TLS session */
	rc = gnutls_init(&tls->session, GNUTLS_CLIENT);
	if (rc != GNUTLS_E_SUCCESS) {
		rc = X_ECANCELED;
		goto error3;
	}
	rc = gnutls_set_default_priority(tls->session);
	if (rc == GNUTLS_E_SUCCESS) {
		rc = gnutls_credentials_set(tls->session, GNUTLS_CRD_CERTIFICATE, xcred);
		if (rc == GNUTLS_E_SUCCESS && hostname) {
			strcpy(tls->hostname, hostname);
			gnutls_session_set_verify_cert(tls->session, tls->hostname, 0);
		}
	}
	if (rc != GNUTLS_E_SUCCESS) {
		rc = X_ECANCELED;
		goto error4;
	}
        gnutls_handshake_set_timeout(tls->session, GNUTLS_DEFAULT_HANDSHAKE_TIMEOUT);
        gnutls_transport_set_int(tls->session, sd);

        /* Perform the TLS handshake */
	fcntl(sd, F_SETFL, O_NONBLOCK);
	tls->state = state_handshake;
	tls->crypt.clen = tls->plain.clen = 0;
	tls->crypt.fd = sd;
	tls->plain.fd = pairfd[1];
	rc = ev_mgr_add_fd(mgr, &tls->plain.efd, pairfd[1], 0, plain_cb, tls, 1, 1);
	if (rc >= 0) {
		rc = ev_mgr_add_fd(mgr, &tls->crypt.efd, sd, EV_FD_IN, handshake_cb, tls, 1, 1);
		if (rc >= 0) {
                        rc = do_handshake(tls);
			if (rc >= 0)
				return pairfd[0];
                        close(pairfd[0]);
                        return rc;
                }
		ev_fd_unref(tls->plain.efd);
	}

error4:
        gnutls_deinit(tls->session);
error3:
	free(tls);
error2:
	close(pairfd[1]);
	close(pairfd[0]);
error:
	return rc;
}


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
