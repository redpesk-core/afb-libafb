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

#if WITH_MBEDTLS

/*
****
**** note Zephyr OS has the configuration value NET_SOCKETS_SOCKOPT_TLS
**** that selects a special PROTO implementation for having TLS as in:
****
****     sock = socket(family, SOCK_STREAM, IPPROTO_TLS_1_3);
****
**** unfortunately, Linux doesn't provide such facility, the best being
**** a bit different and requiring user code:
**** https://www.kernel.org/doc/html/v6.14-rc7/networking/tls-offload.html
**** https://www.kernel.org/doc/html/v6.14-rc7/networking/tls.html
****
*/

#include "tls-mbed.h"


#if __ZEPHYR__
#else
#include <unistd.h>
#include <sys/random.h>
#endif

#include <mbedtls/ssl.h>

#ifndef WITH_MBEDTLS_DEBUG
# define WITH_MBEDTLS_DEBUG 1
#endif

static const int cyphersuites[] = {
	MBEDTLS_TLS1_3_AES_128_GCM_SHA256,
	MBEDTLS_TLS1_3_AES_256_GCM_SHA384,
	MBEDTLS_TLS1_3_CHACHA20_POLY1305_SHA256,
	MBEDTLS_TLS1_3_AES_128_CCM_SHA256,
	MBEDTLS_TLS1_3_AES_128_CCM_8_SHA256,
	MBEDTLS_CIPHERSUITE_NODTLS,
	0
};

#if WITH_MBEDTLS_DEBUG
#include <mbedtls/debug.h>
#include <rp-utils/rp-verbose.h>
static void debug_cb(void *ctx, int level, const char *file, int line, const char *str)
{
	int lvl = level;

	rp_verbose(lvl, file, line, NULL, "%s", str);
}
#endif

static int get_random_bytes(void *ctx, unsigned char *buf, size_t len)
{
#if __ZEPHYR__
	sys_rand_get(buf, len);
#else
	getrandom(buf, len, 0);
#endif
	return 0;
}

/**
* callback for MBED to write the physical stream
*/
static int send_cb(void *ctx, const unsigned char *buf, size_t len)
{
	for (;;) {
		ssize_t ssz = write((int)(uintptr_t)ctx, buf, len);
		if (ssz >= 0)
			return (int)ssz;
		if (errno == EAGAIN)
			return MBEDTLS_ERR_SSL_WANT_WRITE;
		if (errno != EINTR)
			return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
	}
}

/**
* callback for MBED to read from physical stream
*/
static int recv_cb(void *ctx, unsigned char *buf, size_t len)
{
	for (;;) {
		ssize_t ssz = read((int)(uintptr_t)ctx, buf, len);
		if (ssz >= 0)
			return (int)ssz;
		if (errno == EAGAIN)
			return MBEDTLS_ERR_SSL_WANT_READ;
		if (errno != EINTR)
			return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
	}
}

int tls_mbed_creds_init(
	mbedtls_ssl_config *config,
	mbedtls_x509_crt   *cacert,
	mbedtls_x509_crt   *cert,
	mbedtls_pk_context *key,
	bool                server,
	const char         *cert_path,
	const char         *key_path,
	const char         *trust_path
) {
	int rc;

	psa_crypto_init();

	rc = mbedtls_ssl_config_defaults(
			config,
			server ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT,
			MBEDTLS_SSL_TRANSPORT_STREAM,
			MBEDTLS_SSL_PRESET_DEFAULT);
	if (rc != 0)
		return -1;

	mbedtls_ssl_conf_rng(config, get_random_bytes, NULL);
	mbedtls_ssl_conf_ciphersuites(config, cyphersuites);

#if WITH_MBEDTLS_DEBUG
	mbedtls_ssl_conf_dbg(config, debug_cb, NULL);
#endif

	if (trust_path != NULL) {
		rc = mbedtls_x509_crt_parse_file(cacert, trust_path);
		if (rc != 0)
			return -1;
		mbedtls_ssl_conf_ca_chain(config, cacert, NULL);
	}

	if (cert_path != NULL && key_path != NULL) {
		rc = mbedtls_x509_crt_parse_file(cert, cert_path);
		if (rc != 0)
			return -1;
		rc = mbedtls_pk_parse_keyfile(key, key_path, NULL, get_random_bytes, NULL);
		if (rc != 0)
			return -1;
		rc = mbedtls_ssl_conf_own_cert(config, cert, key);
		if (rc != 0)
			return -1;
	}
	return 0;
}

int tls_mbed_session_init(
	mbedtls_ssl_context *context,
	mbedtls_ssl_config  *config,
	bool server,
	int fd,
	const char *host
) {
	int rc;

	rc = mbedtls_ssl_set_hostname(context, host);
	if (rc)
		return -1;
	mbedtls_ssl_set_bio(context, (void*)(intptr_t)fd, send_cb, recv_cb, NULL );
	mbedtls_ssl_setup(context, config);
	return 0;
}





#endif
