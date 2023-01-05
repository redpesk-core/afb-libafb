/*
 * Copyright (C) 2015-2023 IoT.bzh Company
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

#if WITH_SUPERVISION

#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/un.h>

#include <json-c/json.h>
#include <rp-utils/rp-jsonc.h>
#include <rp-utils/rp-verbose.h>

#define AFB_BINDING_VERSION 3
#define AFB_BINDING_NO_ROOT
#include <afb/afb-binding.h>

#include "core/afb-apiset.h"
#include "core/afb-req-common.h"
#include "core/afb-json-legacy.h"
#include "misc/afb-trace.h"
#include "core/afb-data-array.h"
#include "core/afb-session.h"
#include "misc/afb-supervision.h"
#include "misc/afb-supervisor.h"
#include "wsapi/afb-stub-ws.h"
#if WITH_AFB_DEBUG
#include "misc/afb-debug.h"
#endif
#include "core/afb-error-text.h"
#include "core/afb-sched.h"
#include "sys/x-socket.h"
#include "sys/x-mutex.h"
#include "sys/x-errno.h"
#include "utils/namecmp.h"

/* api and apiset name */
static const char supervision_apiname[] = AFB_SUPERVISION_APINAME;
#if WITH_AFB_TRACE
static const char supervisor_apiname[] = AFB_SUPERVISOR_APINAME;
#endif

/* path of the supervision socket */
static const char supervisor_socket_path[] = AFB_SUPERVISOR_SOCKET;

/* mutual exclusion */
static x_mutex_t mutex = X_MUTEX_INITIALIZER;

/* the standard apiset */
static struct {
	struct afb_apiset *apiset;
	struct json_object *config;
} global;

/* the supervision apiset (not exported) */
static struct afb_apiset *supervision_apiset;

/* local api implementation */
static void on_supervision_process(void *closure, struct afb_req_common *comreq);
static struct afb_api_itf supervision_api_itf =
{
	.process = on_supervision_process
};

/* the supervisor link */
static struct afb_stub_ws *supervisor;

#if WITH_AFB_TRACE
/* the trace api */
static struct afb_trace *trace;
#endif

/* open the socket */
static int open_supervisor_socket(const char *path)
{
	int fd, rc;
	struct sockaddr_un addr;
	size_t length;

	/* check path length */
	length = strlen(path);
	if (length >= 108)
		return X_ENAMETOOLONG;

	/* create the unix socket */
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return -errno;

	/* prepare the connection address */
	memset(&addr, 0, sizeof addr);
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, path);
	if (addr.sun_path[0] == '@')
		addr.sun_path[0] = 0; /* implement abstract sockets */

	/* connect the socket */
	rc = connect(fd, (struct sockaddr *) &addr, (socklen_t)(sizeof addr));
	if (rc < 0) {
		close(fd);
		return -errno;
	}
	return fd;
}

static void disconnect_supervisor()
{
	struct afb_stub_ws *s;

	RP_INFO("Disconnecting supervision");
	s = __atomic_exchange_n(&supervisor, NULL, __ATOMIC_RELAXED);
	if (s)
		afb_stub_ws_unref(s);

#if WITH_AFB_TRACE
	struct afb_trace *t = __atomic_exchange_n(&trace, NULL, __ATOMIC_RELAXED);
	if (t)
		afb_trace_unref(t);
#endif
}

static void on_supervisor_hangup(struct afb_stub_ws *s)
{
	if (s && s == supervisor) {
		RP_NOTICE("disconnecting from supervisor");
		disconnect_supervisor();
	}
}

/* try to connect to supervisor */
static void try_connect_supervisor()
{
	int fd;
	ssize_t srd;
	struct afb_supervisor_initiator initiator;

	/* get the mutex */
	x_mutex_lock(&mutex);

	/* needs to connect? */
	if (supervisor || !supervision_apiset)
		goto end;

	/* check existing path */
	if (supervisor_socket_path[0] != '@' && access(supervisor_socket_path, F_OK)) {
		RP_INFO("Can't acces socket path %s: %m", supervisor_socket_path);
		goto end;
	}

	/* socket connection */
	fd = open_supervisor_socket(supervisor_socket_path);
	if (fd < 0) {
		RP_INFO("Can't connect supervision socket to %s: %m", supervisor_socket_path);
		goto end;
	}

	/* negotiation */
	RP_NOTICE("connecting to supervisor %s", supervisor_socket_path);
	do { srd = read(fd, &initiator, sizeof initiator); } while(srd < 0 && errno == EINTR);
	if (srd < 0) {
		RP_ERROR("Can't read supervisor %s: %m", supervisor_socket_path);
		goto end2;
	}
	if ((size_t)srd != sizeof initiator) {
		RP_ERROR("When reading supervisor %s: %m", supervisor_socket_path);
		goto end2;
	}
	if (strnlen(initiator.interface, sizeof initiator.interface) == sizeof initiator.interface) {
		RP_ERROR("Bad interface of supervisor %s", supervisor_socket_path);
		goto end2;
	}
	if (strcmp(initiator.interface, AFB_SUPERVISOR_INTERFACE_1)) {
		RP_ERROR("Unknown interface %s for supervisor %s", initiator.interface, supervisor_socket_path);
		goto end2;
	}
	if (strnlen(initiator.extra, sizeof initiator.extra) == sizeof initiator.extra) {
		RP_ERROR("Bad extra of supervisor %s", supervisor_socket_path);
		goto end2;
	}

	/* interprets extras */
	if (!strcmp(initiator.extra, "CLOSE")) {
		RP_NOTICE("Supervisor asks to CLOSE");
		goto end2;
	}
#if WITH_AFB_DEBUG
	if (!strcmp(initiator.extra, "WAIT")) {
		afb_debug_wait("supervisor");
	}
	if (!strcmp(initiator.extra, "BREAK")) {
		afb_debug_break("supervisor");
	}
#endif

	/* make the supervisor link */
	supervisor = afb_stub_ws_create_server(fd, 1, supervision_apiname, supervision_apiset);
	if (!supervisor) {
		RP_ERROR("Creation of supervisor failed: %m");
		goto end;
	}
	afb_stub_ws_set_on_hangup(supervisor, on_supervisor_hangup);

	/* successful termination */
	goto end;

end2:
	close(fd);
end:
	x_mutex_unlock(&mutex);
}

static void try_connect_supervisor_job(int signum, void *args)
{
	RP_INFO("Try to connect supervisor after SIGHUP");
	try_connect_supervisor();
}

static void on_sighup(int signum)
{
	RP_INFO("Supervision received a SIGHUP");
	afb_sched_post_job(NULL, 0, 0, try_connect_supervisor_job, NULL, Afb_Sched_Mode_Normal);
}

/**
 * initialize the supervision
 */
int afb_supervision_init(struct afb_apiset *apiset, struct json_object *config)
{
	int rc;
	struct sigaction sa;

	/* don't reinit */
	if (supervision_apiset)
		return 0;

	/* create the apiset */
	supervision_apiset = afb_apiset_create(supervision_apiname, 0);
	if (!supervision_apiset) {
		RP_ERROR("Can't create supervision's apiset");
		return -1;
	}

	/* init the apiset */
	rc = afb_apiset_add(supervision_apiset, supervision_apiname,
			(struct afb_api_item){ .itf = &supervision_api_itf, .closure = NULL});
	if (rc < 0) {
		RP_ERROR("Can't create supervision's apiset: %m");
		afb_apiset_unref(supervision_apiset);
		supervision_apiset = NULL;
		return rc;
	}

	/* init the globals */
	global.apiset = apiset;
	global.config = config;

	/* get SIGHUP */
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_sighup;
	rc = sigaction(SIGHUP, &sa, NULL);
	if (rc < 0)
		RP_ERROR("Can't connect supervision to SIGHUP: %m");

	/* connect to supervision */
	try_connect_supervisor();
	return 0;
}

/******************************************************************************
**** Implementation monitoring verbs
******************************************************************************/
static void slist(void *closure, struct afb_session *session)
{
	struct json_object *list = closure;

	json_object_object_add(list, afb_session_uuid(session), NULL);
}

/******************************************************************************
**** Implementation monitoring verbs
******************************************************************************/

static const char *verbs[] = {
	"break", "config", "do", "exit", "sclose", "slist", "trace", "wait" };
enum  {  Break ,  Config ,  Do ,  Exit ,  Sclose ,  Slist ,  Trace ,  Wait  };

static void process_cb(void *closure, struct json_object *args)
{
	int i;
	struct afb_req_common *comreq = closure;
	struct json_object *sub, *list;
	const char *api, *verb, *uuid;
	struct afb_session *session;
	const struct afb_api_item *xapi;
	struct afb_data *data;
	int rc;
#if WITH_AFB_TRACE
	struct json_object *drop, *add;
#endif

	/* search the verb */
	i = (int)(sizeof verbs / sizeof *verbs);
	while(--i >= 0 && namecmp(verbs[i], comreq->verbname));
	if (i < 0) {
		afb_req_common_reply_verb_unknown_error_hookable(comreq);
		return;
	}

	/* process */
	switch(i) {
	case Exit:
		i = 0;
		if (rp_jsonc_unpack(args, "i", &i))
			rp_jsonc_unpack(args, "{si}", "code", &i);
		RP_ERROR("exiting from supervision with code %d -> %d", i, i & 127);
		exit(i & 127);
		break;
	case Sclose:
		uuid = NULL;
		if (rp_jsonc_unpack(args, "s", &uuid))
			rp_jsonc_unpack(args, "{ss}", "uuid", &uuid);
		if (!uuid)
			afb_json_legacy_req_reply_hookable(comreq, NULL, afb_error_text(AFB_ERRNO_INVALID_REQUEST), NULL);
		else {
			session = afb_session_search(uuid);
			if (!session)
				afb_json_legacy_req_reply_hookable(comreq, NULL, afb_error_text(AFB_ERRNO_NO_ITEM), NULL);
			else {
				afb_session_close(session);
				afb_session_unref(session);
				afb_session_purge();
				afb_json_legacy_req_reply_hookable(comreq, NULL, NULL, NULL);
			}
		}
		break;
	case Slist:
		list = json_object_new_object();
		afb_session_foreach(slist, list);
		afb_json_legacy_req_reply_hookable(comreq, list, NULL, NULL);
		break;
	case Config:
		afb_json_legacy_req_reply_hookable(comreq, json_object_get(global.config), NULL, NULL);
		break;
	case Trace:
#if WITH_AFB_TRACE
		if (!trace)
			trace = afb_trace_create(supervisor_apiname, NULL /* not bound to any session */);

		add = drop = NULL;
		rp_jsonc_unpack(args, "{s?o s?o}", "add", &add, "drop", &drop);
		if (add) {
			rc = afb_trace_add(comreq, add, trace);
			if (rc)
				return;
		}
		if (drop) {
			rc = afb_trace_drop(comreq, drop, trace);
			if (rc)
				return;
		}
		afb_json_legacy_req_reply_hookable(comreq, NULL, NULL, NULL);
#else
		afb_req_common_reply_unavailable_error_hookable(comreq);
#endif
		break;
	case Do:
		sub = NULL;
		if (rp_jsonc_unpack(args, "{ss ss s?o*}", "api", &api, "verb", &verb, "args", &sub))
			afb_json_legacy_req_reply_hookable(comreq, NULL, afb_error_text(AFB_ERRNO_INVALID_REQUEST), NULL);
		else {
			rc = afb_apiset_get_api(global.apiset, api, 1, 1, &xapi);
			if (rc < 0)
				afb_req_common_reply_api_unknown_error_hookable(comreq);
			else {
				rc = afb_json_legacy_make_data_json_c(&data, json_object_get(sub));
				if (rc < 0)
					afb_req_common_reply_internal_error_hookable(comreq, rc);
				else {
#if WITH_CRED
					afb_req_common_set_cred(comreq, NULL);
#endif
					json_object_get(args);
					comreq->apiname = api;
					comreq->verbname = verb;
					afb_data_array_unref(comreq->params.ndata, comreq->params.data);
					comreq->params.data[0] = data;
					comreq->params.ndata = 1;
					xapi->itf->process(xapi->closure, comreq);
					json_object_put(args);
				}
			}
		}
		break;
#if WITH_AFB_DEBUG
	case Wait:
		afb_json_legacy_req_reply_hookable(comreq, NULL, NULL, NULL);
		afb_debug_wait("supervisor");
		break;
	case Break:
		afb_json_legacy_req_reply_hookable(comreq, NULL, NULL, NULL);
		afb_debug_break("supervisor");
		break;
#else
	case Wait:
	case Break:
		afb_req_common_reply_unavailable_error_hookable(comreq);
		break;
#endif
	}
}

static void on_supervision_process(void *closure, struct afb_req_common *comreq)
{
	afb_json_legacy_do_single_json_c(comreq->params.ndata, comreq->params.data, process_cb, comreq);
}

#endif
