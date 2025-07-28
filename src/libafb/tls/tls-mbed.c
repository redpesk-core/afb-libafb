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
#include <zephyr/random/random.h>
#else
#include <unistd.h>
#include <sys/random.h>
#endif
#include <sys/socket.h>

#include <mbedtls/ssl.h>

#include <rp-utils/rp-verbose.h>

#include <sys/x-errno.h>

#ifndef WITH_MBEDTLS_DEBUG
# define WITH_MBEDTLS_DEBUG 1
#endif

#ifndef DEFAULT_CA_DIR
#  define DEFAULT_CA_DIR "/etc/ssl/certs"
#endif

#ifndef RESTRICT_MBEDTLS_CYPHER_SUITE
#  define RESTRICT_MBEDTLS_CYPHER_SUITE 1
#endif

static bool cert_set  = false;
static bool key_set   = false;
static bool trust_set = false;

static mbedtls_x509_crt   cert_data;
static mbedtls_pk_context key_data;
static mbedtls_x509_crt   trust_data;

#if RESTRICT_MBEDTLS_CYPHER_SUITE
/* ciphersuites with hardware acceleration on STM B-U585I-IOT02A */
static const int cyphersuites[] = {
	MBEDTLS_TLS1_3_AES_128_GCM_SHA256,
	MBEDTLS_TLS1_3_AES_128_CCM_SHA256,
	MBEDTLS_TLS1_3_AES_128_CCM_8_SHA256,
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_CCM,
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_CCM_8,
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CCM,
	MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8,
	MBEDTLS_CIPHERSUITE_NODTLS,
	0
};
static const int tls13_key_exchange_modes = MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL;
#endif

#if WITH_MBEDTLS_DEBUG
#include <mbedtls/debug.h>
#include <rp-utils/rp-verbose.h>
static void debug_cb(void *ctx, int level, const char *file, int line, const char *str)
{
	int lvl = rp_Log_Level_Error - 1 + level;
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

int tls_mbed_has_cert()
{
	return cert_set;
}

int tls_mbed_has_key()
{
	return key_set;
}

int tls_mbed_has_trust()
{
	return trust_set;
}

int tls_mbed_set_cert(const void *cert, size_t size)
{
	if (cert_set)
		return X_EEXIST;


	mbedtls_x509_crt_init(&cert_data);
	if (mbedtls_x509_crt_parse(&cert_data, cert, size) < 0) {
		RP_ERROR("can't import certificate");
		return X_EINVAL;
	}

	cert_set = true;
	return 0;
}

int tls_mbed_set_key(const void *key, size_t size)
{
	if (key_set)
		return X_EEXIST;

	mbedtls_pk_init(&key_data);
	if (mbedtls_pk_parse_key(&key_data, key, size,
			NULL, 0, get_random_bytes, NULL) < 0) {
		RP_ERROR("can't import key");
		return X_EINVAL;
	}

	key_set = true;
	return 0;
}

int tls_mbed_add_trust(const void *trust, size_t size)
{
	int rc;
	if (!trust_set) {
		mbedtls_x509_crt_init(&trust_data);
		trust_set = true;
	}
	rc = mbedtls_x509_crt_parse(&trust_data, trust, size);
	if (rc < 0) {
		RP_ERROR("can't import trust: %d", rc);
		return X_EINVAL;
	}
	return 0;
}

#if !WITHOUT_FILESYSTEM
#include <sys/stat.h>
/* check if a path is a directory */
static bool isdir(const char *path)
{
	struct stat st;
	int rc = stat(path, &st);
	return rc >= 0 && S_ISDIR(st.st_mode);
}

int tls_mbed_load_cert(const char *path)
{
	if (cert_set)
		return X_EEXIST;

	mbedtls_x509_crt_init(&cert_data);
	if (mbedtls_x509_crt_parse_file(&cert_data, path) < 0) {
		RP_ERROR("can't load certificate %s", path);
		return X_EINVAL;
	}

	cert_set = true;
	return 0;
}

int tls_mbed_load_key(const char *path)
{
	if (key_set)
		return X_EEXIST;

	mbedtls_pk_init(&key_data);
	if (mbedtls_pk_parse_keyfile(&key_data, path, NULL,
			get_random_bytes, NULL) < 0) {
		RP_ERROR("can't load key %s", path);
		return X_EINVAL;
	}

	key_set = true;
	return 0;
}

int tls_mbed_load_trust(const char *path)
{
	int rc;
	if (!trust_set) {
		mbedtls_x509_crt_init(&trust_data);
		trust_set = true;
	}
	if (path == NULL)
		path = DEFAULT_CA_DIR;
	if (isdir(path))
		rc = mbedtls_x509_crt_parse_path(&trust_data, path);
	else
		rc = mbedtls_x509_crt_parse_file(&trust_data, path);

	if (rc < 0) {
		RP_ERROR("can't load trust %s", path);
		return X_EINVAL;
	}

	return 0;
}

int tls_load_cert(const char *path)  __attribute__ ((alias ("tls_mbed_load_cert")));
int tls_load_key(const char *path)   __attribute__ ((alias ("tls_mbed_load_key")));
int tls_load_trust(const char *path) __attribute__ ((alias ("tls_mbed_load_trust")));
#endif

/**
* callback for MBED to write the physical stream
*/
static int send_cb(void *ctx, const unsigned char *buf, size_t len)
{
	for (;;) {
		ssize_t ssz = send((int)(uintptr_t)ctx, buf, len, 0);
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
		ssize_t ssz = recv((int)(uintptr_t)ctx, buf, len, 0);
		if (ssz >= 0)
			return (int)ssz;
		if (errno == EAGAIN)
			return MBEDTLS_ERR_SSL_WANT_READ;
		if (errno != EINTR)
			return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
	}
}

int tls_mbed_session_create(
	mbedtls_ssl_context *context,
	mbedtls_ssl_config  *config,
	int fd,
	bool server,
	bool mtls,
	const char *host
) {
	int rc;

	if (!(server ? (cert_set && key_set) : trust_set)
	  || (mtls && !(cert_set && key_set && trust_set))) {
		RP_ERROR("Some crypto material misses");
		return X_ENOENT;
	}

	mbedtls_ssl_init(context);
	mbedtls_ssl_config_init(config);

	rc = mbedtls_ssl_config_defaults(
			config,
			server ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT,
			MBEDTLS_SSL_TRANSPORT_STREAM,
			MBEDTLS_SSL_PRESET_DEFAULT);
	if (rc != 0) {
		RP_ERROR("Can't init default config");
		rc = X_ECANCELED;
		goto error;
	}

	mbedtls_ssl_conf_rng(config, get_random_bytes, NULL);
#if RESTRICT_MBEDTLS_CYPHER_SUITE
	mbedtls_ssl_conf_ciphersuites(config, cyphersuites);
	mbedtls_ssl_conf_tls13_key_exchange_modes(config, tls13_key_exchange_modes);
#endif
#if WITH_MBEDTLS_DEBUG
	mbedtls_ssl_conf_dbg(config, debug_cb, NULL);
#endif

	if (!server || mtls)
		mbedtls_ssl_conf_ca_chain(config, &trust_data, NULL);

	if (server || mtls) {
		rc = mbedtls_ssl_conf_own_cert(config, &cert_data, &key_data);
		if (rc != 0) {
			RP_ERROR("Can't set key");
			rc = X_ECANCELED;
			goto error;
		}
	}

	if (server && mtls)
		mbedtls_ssl_conf_authmode(config, MBEDTLS_SSL_VERIFY_REQUIRED);

	/*
	 * Set the hostname to check, can be NULL for no check.
	 */
	rc = mbedtls_ssl_set_hostname(context, host);
	if (rc) {
		RP_ERROR("Can't set hostname");
		rc = X_ECANCELED;
		goto error;
	}
	mbedtls_ssl_set_bio(context, (void*)(intptr_t)fd, send_cb, recv_cb, NULL );
	mbedtls_ssl_setup(context, config);
	return 0;

error:
	mbedtls_ssl_free(context);
	mbedtls_ssl_config_free(config);
	return rc;
}

#endif

