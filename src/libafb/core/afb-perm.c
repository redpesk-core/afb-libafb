/*
 * Copyright (C) 2015-2020 IoT.bzh Company
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

#include "afb-config.h"

#if WITH_CRED

#include <stdint.h>

#include "core/afb-perm.h"
#include "core/afb-context.h"
#include "core/afb-cred.h"
#include "core/afb-token.h"
#include "core/afb-session.h"
#include "sys/verbose.h"

/*********************************************************************************/

static inline const char *session_of_context(struct afb_context *context)
{
	return context->token ? afb_token_string(context->token)
                : context->session ? afb_session_uuid(context->session)
                : "";
}

/*********************************************************************************/
#if BACKEND_PERMISSION_IS_CYNAGORA

#include <stdint.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <errno.h>
#include <string.h>

#include <cynagora.h>

#include "misc/afb-fdev.h"
#include "sys/fdev.h"


static cynagora_t *cynagora;
static struct fdev *fdev;
static pthread_mutex_t mutex;
static pthread_once_t once = PTHREAD_ONCE_INIT;

static void mkmutex()
{
	pthread_mutexattr_t a;
	pthread_mutexattr_init(&a);
	pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&mutex, &a);
	pthread_mutexattr_destroy(&a);
}


static void fdev_cb(void *closure, uint32_t events, struct fdev *fdev)
{
	pthread_mutex_lock(&mutex);
	cynagora_async_process(cynagora);
	pthread_mutex_unlock(&mutex);
}

static int cynagora_async_ctl_cb(
	void *closure,
	int op,
	int fd,
	uint32_t events)
{
	if ((op == EPOLL_CTL_DEL || op == EPOLL_CTL_ADD) && fdev) {
		fdev_unref(fdev);
		fdev = NULL;
	}
	if (op == EPOLL_CTL_ADD) {
		fdev = afb_fdev_create(fd);
		if (!fdev)
			return -errno;
		fdev_set_autoclose(fdev, 0);
		fdev_set_callback(fdev, fdev_cb, NULL);
	}
	if (op == EPOLL_CTL_ADD || op == EPOLL_CTL_MOD)
		fdev_set_events(fdev, events);
	return 0;
}

static int cynagora_acquire()
{
	int rc;

	/* cynagora isn't reentrant */
	pthread_once(&once, mkmutex);
	pthread_mutex_lock(&mutex);

	if (cynagora)
		rc = 0;
	else {
		/* lazy initialisation */
		rc = cynagora_create(&cynagora, cynagora_Check, 1000, NULL);
		if (rc < 0) {
			cynagora = NULL;
			ERROR("cynagora initialisation failed with code %d, %s", rc, strerror(-rc));
			pthread_mutex_unlock(&mutex);
		} else {
			rc = cynagora_async_setup(cynagora, cynagora_async_ctl_cb, NULL);
			if (rc < 0) {
				ERROR("cynagora initialisation of async failed with code %d, %s", rc, strerror(-rc));
				cynagora_destroy(cynagora);
				cynagora = NULL;
				pthread_mutex_unlock(&mutex);
			}
		}
	}
	return rc;
}

static void cynagora_release()
{
	/* cynagora isn't reentrant */
	pthread_mutex_unlock(&mutex);
}

#if SYNCHRONOUS_CHECKS
int afb_perm_check(struct afb_context *context, const char *permission)
{
	int rc;
	cynagora_key_t key;

#if WITH_CRED
	if (!context->credentials) {
		/* case of permission for self */
		return 1;
	}
#endif
	if (!permission) {
		ERROR("Got a null permission!");
		return 0;
	}

	rc = cynagora_acquire();
	if (rc < 0)
		return 0;

#if WITH_CRED
	key.client = context->credentials->label;
	key.user = context->credentials->user;
#else
	key.client = "";
	key.user = "";
#endif
	key.session = session_of_context(context);
	key.permission = permission;
	rc = cynagora_check(cynagora, &key, 0);
	cynagora_release();
	return rc > 0;
}
#endif

void afb_perm_check_async(
	struct afb_context *context,
	const char *permission,
	void (*callback)(void *closure, int status),
	void *closure
)
{
	int rc;
	cynagora_key_t key;

#if WITH_CRED
	if (!context->credentials)
		/* case of permission for self */
		rc = 1;

	else
#endif
	if (!permission) {
		ERROR("Got a null permission!");
		rc = 0;
	}
	else {
		rc = cynagora_acquire();
		if (rc >= 0) {
#if WITH_CRED
			key.client = context->credentials->label;
			key.user = context->credentials->user;
#else
			key.client = "";
			key.user = "";
#endif
			key.session = session_of_context(context);
			key.permission = permission;
			rc = cynagora_async_check(cynagora, &key, 0, 0, callback, closure);
			cynagora_release();
			if (rc >= 0)
				return;
			ERROR("Can't query cynagora: %s", strerror(-rc));
		}
	}
	callback(closure, rc);
}

/*********************************************************************************/
#elif BACKEND_PERMISSION_IS_CYNARA

#include <pthread.h>
#include <cynara-client.h>

static cynara *handle;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

int afb_perm_check(struct afb_context *context, const char *permission)
{
	int rc;

#if WITH_CRED
	if (!context->credentials) {
		/* case of permission for self */
		return 1;
	}
#endif
	if (!permission) {
		ERROR("Got a null permission!");
		return 0;
	}

	/* cynara isn't reentrant */
	pthread_mutex_lock(&mutex);

	/* lazy initialisation */
	if (!handle) {
		rc = cynara_initialize(&handle, NULL);
		if (rc != CYNARA_API_SUCCESS) {
			handle = NULL;
			ERROR("cynara initialisation failed with code %d", rc);
			return 0;
		}
	}

	/* query cynara permission */
#if WITH_CRED
	rc = cynara_check(handle, context->credentials->label, session_of_context(context), context->credentials->user, permission);
#else
	rc = cynara_check(handle, "", session_of_context(context), "", permission);
#endif

	pthread_mutex_unlock(&mutex);
	return rc == CYNARA_API_ACCESS_ALLOWED;
}

void afb_perm_check_async(
	struct afb_context *context,
	const char *permission,
	void (*callback)(void *closure, int status),
	void *closure
)
{
	callback(closure, afb_perm_check(context, permission));
}

/*********************************************************************************/
#else

int afb_perm_check(struct afb_context *context, const char *permission)
{
	NOTICE("Granting permission %s by default of backend", permission ?: "(null)");
	return !!permission;
}

void afb_perm_check_async(
	struct afb_context *context,
	const char *permission,
	void (*callback)(void *closure, int status),
	void *closure
)
{
	callback(closure, afb_perm_check(context, permission));
}

#endif

#endif /* WITH_CRED */

