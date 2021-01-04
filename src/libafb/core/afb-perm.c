/*
 * Copyright (C) 2015-2021 IoT.bzh Company
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

#include "libafb-config.h"

#if WITH_CRED

#include <stdint.h>

#include "core/afb-perm.h"
#include "core/afb-cred.h"
#include "core/afb-token.h"
#include "core/afb-session.h"
#include "core/afb-req-common.h"
#include "sys/verbose.h"

/*********************************************************************************/

static inline const char *session_of_req(struct afb_req_common *req)
{
	return req->token ? afb_token_string(req->token)
                : req->session ? afb_session_uuid(req->session)
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

#include "core/afb-ev-mgr.h"
#include "sys/ev-mgr.h"

static cynagora_t *cynagora;
static struct ev_fd *evfd;
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


static void evfdcb(struct ev_fd *evfd, int fd, uint32_t events, void *closure)
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
	int rc = 0;

	if ((op == EPOLL_CTL_DEL || op == EPOLL_CTL_ADD) && evfd) {
		ev_fd_unref(evfd);
		evfd = NULL;
	}
	if (op == EPOLL_CTL_ADD) {
		rc = afb_ev_mgr_add_fd(&evfd, fd, events, evfdcb, 0, 1, 0);
	}
	if (op == EPOLL_CTL_MOD) {
		ev_fd_set_events(evfd, events);
	}
	return rc;
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

void afb_perm_check_req_async(
	struct afb_req_common *req,
	const char *permission,
	void (*callback)(void *closure, int status),
	void *closure
)
{
	int rc;
	cynagora_key_t key;

#if WITH_CRED
	if (!req->credentials)
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
			key.client = req->credentials->label;
			key.user = req->credentials->user;
#else
			key.client = "";
			key.user = "";
#endif
			key.session = session_of_req(req);
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
#else

void afb_perm_check_req_async(
	struct afb_req_common *req,
	const char *permission,
	void (*callback)(void *_closure, int _status),
	void *closure
) {
	NOTICE("Granting permission %s by default of backend", permission ?: "(null)");
	callback(closure, !!permission);
}

#endif

#endif /* WITH_CRED */
