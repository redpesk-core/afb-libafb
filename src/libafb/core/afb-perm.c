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

#include "../libafb-config.h"

#if WITH_CRED

#include <stdlib.h>
#include <stdint.h>

#include <rp-utils/rp-verbose.h>

#include "core/afb-perm.h"
#include "core/afb-cred.h"
#include "core/afb-token.h"
#include "core/afb-sched.h"
#include "core/afb-session.h"
#include "core/afb-req-common.h"

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
#include <errno.h>
#include <string.h>

#include <cynagora.h>

#include "core/afb-ev-mgr.h"
#include "sys/ev-mgr.h"
#include "sys/x-epoll.h"

struct memo_check
{
	int status;
	void *closure;
	void (*checkcb)(void *closure, int status);
};

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

static void lock()
{
	pthread_mutex_lock(&mutex);
}

static void unlock()
{
	pthread_mutex_unlock(&mutex);
}

static void async_job_cb(int status, void *closure)
{
	struct memo_check *memo = closure;
	memo->checkcb(memo->closure, memo->status);
	free(memo);
}

static void async_check_cb(void *closure, int status)
{
	int rc;
	struct memo_check *memo = closure;
	memo->status = status;
	rc = afb_sched_post_job(NULL, 0, 0, async_job_cb, memo, Afb_Sched_Mode_Normal);
	if (rc < 0)
		RP_ERROR("cynagora encountered error when queing job");
}

static void evfdcb(struct ev_fd *evfd, int fd, uint32_t events, void *closure)
{
	lock();
	cynagora_async_process(cynagora);
	unlock();
}

static int cynagora_async_ctl_cb(
	void *closure,
	int op,
	int fd,
	uint32_t events
) {
	int rc = 0;

	if ((op == EPOLL_CTL_DEL || op == EPOLL_CTL_ADD) && evfd) {
		ev_fd_unref(evfd);
		evfd = NULL;
	}
	if (op == EPOLL_CTL_ADD) {
		rc = afb_ev_mgr_add_fd(&evfd, fd, ev_fd_from_epoll(events), evfdcb, 0, 1, 0);
	}
	if (op == EPOLL_CTL_MOD) {
		ev_fd_set_events(evfd, ev_fd_from_epoll(events));
	}
	return rc;
}

static int cynagora_acquire()
{
	int rc;

	/* cynagora isn't reentrant */
	pthread_once(&once, mkmutex);
	lock();

	if (cynagora)
		rc = 0;
	else {
		/* lazy initialisation */
		rc = cynagora_create(&cynagora, cynagora_Check, 1000, NULL);
		if (rc < 0) {
			cynagora = NULL;
			RP_ERROR("cynagora initialisation failed with code %d, %s", rc, strerror(-rc));
			unlock();
		} else {
			rc = cynagora_async_setup(cynagora, cynagora_async_ctl_cb, NULL);
			if (rc < 0) {
				RP_ERROR("cynagora initialisation of async failed with code %d, %s", rc, strerror(-rc));
				cynagora_destroy(cynagora);
				cynagora = NULL;
				unlock();
			}
		}
	}
	return rc;
}

void afb_perm_check_async(
	const char *client,
	const char *user,
	const char *session,
	const char *permission,
	void (*callback)(void *closure, int status),
	void *closure
)
{
	int rc;
	cynagora_key_t key;
	struct memo_check *memo;

	rc = cynagora_acquire();
	if (rc >= 0) {
		memo = malloc(sizeof *memo);
		if (memo == NULL) {
			rc = -ENOMEM;
			RP_ERROR("Can't query cynagora: %s", strerror(-rc));
		}
		else {
			memo->status = -EFAULT;
			memo->closure = closure;
			memo->checkcb = callback;
			key.client = client;
			key.user = user;
			key.session = session;
			key.permission = permission;
			rc = cynagora_async_check(cynagora, &key, 0, 0, async_check_cb, memo);
			unlock();
			if (rc >= 0)
				return;
			RP_ERROR("Can't query cynagora: %s", strerror(-rc));
		}
	}
	callback(closure, rc);
}

int afb_perm_check_perm_check_api(
	const char *api
) {
	return 0;
}

/*********************************************************************************/
#elif BACKEND_PERMISSION_IS_API_PERM

#ifndef API_PERM_API_NAME
#define API_PERM_API_NAME "perm"
#endif

#ifndef API_PERM_VERB_NAME
#define API_PERM_VERB_NAME "check"
#endif

#include <string.h>

#include "core/afb-global.h"
#include "core/afb-type-predefined.h"
#include "core/afb-data.h"
#include "core/afb-calls.h"

static void checkcb(void *closure1, void *closure2, void *closure3, int status, unsigned nvals, struct afb_data * const vals[])
{
	/* the called verb should reply 1 to grant, 0 to deny or a negative value for error */
	void (*callback)(void *_closure, int _status) = closure1;
	callback(closure2, status);
}

static int mkstrdata(struct afb_data **result, const char *str)
{
	size_t len = str == NULL ? 0 : 1 + strlen(str);
	return afb_data_create_raw(result,
			&afb_type_predefined_stringz, str, len, NULL, NULL);
}

void afb_perm_check_async(
	const char *client,
	const char *user,
	const char *session,
	const char *permission,
	void (*callback)(void *_closure, int _status),
	void *closure
) {
	struct afb_data * params[4];

	mkstrdata(&params[0], client);
	mkstrdata(&params[1], user);
	mkstrdata(&params[2], session);
	mkstrdata(&params[3], permission);

	afb_calls_call(
		afb_global_api(),
		API_PERM_API_NAME,
		API_PERM_VERB_NAME,
		4,
		params,
		checkcb,
		callback,
		closure,
		NULL
	);
}

int afb_perm_check_perm_check_api(
	const char *api
) {
	return -!strcmp(api, API_PERM_API_NAME);
}

/*********************************************************************************/
#else

void afb_perm_check_async(
	const char *client,
	const char *user,
	const char *session,
	const char *permission,
	void (*callback)(void *_closure, int _status),
	void *closure
) {
	RP_NOTICE("Granting permission %s by default of backend", permission);
	callback(closure, 1);
}

int afb_perm_check_perm_check_api(
	const char *api
) {
	return 0;
}

#endif

/*********************************************************************************/
/* common entry */

void afb_perm_check_req_async(
	struct afb_req_common *req,
	const char *permission,
	void (*callback)(void *_closure, int _status),
	void *closure
) {
	if (!req->credentials) {
		/* case of permission for self */
		callback(closure, 1);
	}
	else if (!permission) {
		RP_ERROR("Got a null permission!");
		callback(closure, 0);
	}
	else {
		afb_perm_check_async(
			req->credentials->label,
			req->credentials->user,
			session_of_req(req),
			permission,
			callback,
			closure);
	}
}

#endif /* WITH_CRED */
