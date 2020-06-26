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

#include "libafb-config.h"

#if WITH_DBUS_TRANSPARENCY

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <systemd/sd-bus.h>

#include <afb/afb-event-x2.h>
#include <afb/afb-data-x4.h>
#include <afb/afb-type-x4.h>

#include "core/afb-session.h"
#include "core/afb-json-legacy.h"
#include "core/afb-apiname.h"
#include "core/afb-apiset.h"
#include "apis/afb-api-dbus.h"
#include "core/afb-cred.h"
#include "core/afb-data.h"
#include "core/afb-evt.h"
#include "core/afb-req-common.h"
#include "core/containerof.h"

#include "sys/verbose.h"
#include "sys/systemd.h"
#include "core/afb-sched.h"
#include "sys/x-errno.h"

#if !defined(AFB_API_DBUS_PATH_PREFIX)
#  define AFB_API_DBUS_PATH_PREFIX "/org/agl/afb/api/"
#endif
static const char DEFAULT_PATH_PREFIX[] = AFB_API_DBUS_PATH_PREFIX;

struct dbus_memo;
struct dbus_event;
struct origin;

/*
 * The path given are of the form
 *     "system:" DEFAULT_PATH_PREFIX ...
 * or
 *     "user:" DEFAULT_PATH_PREFIX ...
 */
struct api_dbus
{
	struct sd_bus *sdbus;	/* the bus */
	char *path;		/* path of the object for the API */
	char *name;		/* name/interface of the object */
	char *api;		/* api name of the interface */
	union {
		struct {
			struct sd_bus_slot *slot_broadcast;
			struct sd_bus_slot *slot_event;
			struct dbus_event *events;
			struct dbus_memo *memos;
		} client;
		struct {
			struct sd_bus_slot *slot_call;
			struct afb_evt_listener *listener; /* listener for events */
			struct origin *origins;
			struct afb_apiset *apiset;
		} server;
	};
};

/******************* common part **********************************/

/*
 * create a structure api_dbus connected on either the system
 * bus if 'system' is not null or on the user bus. The connection
 * is established for either emiting/receiving on 'path' being of length
 * 'pathlen'.
 */
static struct api_dbus *make_api_dbus_3(int system, const char *path, size_t pathlen)
{
	struct api_dbus *api;
	struct sd_bus *sdbus;
	char *ptr;

	/* allocates the structure */
	api = calloc(1, sizeof *api + 1 + pathlen + pathlen);
	if (api == NULL) {
		errno = ENOMEM;
		goto error;
	}

	/* init the structure's strings */

	/* path is copied after the struct */
	api->path = (void*)(api+1);
	strcpy(api->path, path);

	/* api name is at the end of the path */
	api->api = strrchr(api->path, '/');
	if (api->api == NULL) {
		errno = EINVAL;
		goto error2;
	}
	api->api++;
	if (!afb_apiname_is_valid(api->api)) {
		errno = EINVAL;
		goto error2;
	}

	/* the name/interface is copied after the path */
	api->name = &api->path[pathlen + 1];
	strcpy(api->name, &path[1]);
	ptr = strchr(api->name, '/');
	while(ptr != NULL) {
		*ptr = '.';
		ptr = strchr(ptr, '/');
	}

	/* choose the bus */
	afb_sched_acquire_event_manager();
	sdbus = (system ? systemd_get_system_bus : systemd_get_user_bus)();
	if (sdbus == NULL)
		goto error2;

	api->sdbus = sdbus;
	return api;

error2:
	free(api);
error:
	return NULL;
}

/*
 * create a structure api_dbus connected on either the system
 * bus if 'system' is not null or on the user bus. The connection
 * is established for either emiting/receiving on 'path'.
 * If 'path' is not absolute, it is prefixed with DEFAULT_PATH_PREFIX.
 */
static struct api_dbus *make_api_dbus_2(int system, const char *path)
{
	size_t len;
	char *ptr;

	/* check the length of the path */
	len = strlen(path);
	if (len == 0) {
		errno = EINVAL;
		return NULL;
	}

	/* if the path is absolute, creation now */
	if (path[0] == '/')
		return make_api_dbus_3(system, path, len);

	/* compute the path prefixed with DEFAULT_PATH_PREFIX */
	assert(strlen(DEFAULT_PATH_PREFIX) > 0);
	assert(DEFAULT_PATH_PREFIX[strlen(DEFAULT_PATH_PREFIX) - 1] == '/');
	len += strlen(DEFAULT_PATH_PREFIX);
	ptr = alloca(len + 1);
	strcpy(stpcpy(ptr, DEFAULT_PATH_PREFIX), path);

	/* creation for prefixed path */
	return make_api_dbus_3(system, ptr, len);
}

/*
 * create a structure api_dbus connected either emiting/receiving
 * on 'path'.
 * The path can be prefixed with "system:" or "user:" to select
 * either the user or the system D-Bus. If none is set then user's
 * bus is selected.
 * If remaining 'path' is not absolute, it is prefixed with
 * DEFAULT_PATH_PREFIX.
 */
static struct api_dbus *make_api_dbus(const char *path)
{
	const char *ptr;
	size_t preflen;

	/* retrieves the prefix "scheme-like" part */
	ptr = strchr(path, ':');
	if (ptr == NULL)
		return make_api_dbus_2(0, path);

	/* check the prefix part */
	preflen = (size_t)(ptr - path);
	if (strncmp(path, "system", preflen) == 0)
		return make_api_dbus_2(1, ptr + 1);

	if (strncmp(path, "user", preflen) == 0)
		return make_api_dbus_2(0, ptr + 1);

	/* TODO: connect to a foreign D-Bus? */
	errno = EINVAL;
	return NULL;
}

static void destroy_api_dbus(struct api_dbus *api)
{
	free(api);
}

/******************* client part **********************************/

/*
 * structure for recording query data
 */
struct dbus_memo {
	struct dbus_memo *next;		/* the next memo */
	struct api_dbus *api;		/* the dbus api */
	struct afb_req_common *comreq;	/* the request */
	uint64_t msgid;			/* the message identifier */
};

struct dbus_event
{
	struct dbus_event *next;
	struct afb_evt *event;
	int id;
	int refcount;
};

/* allocates and init the memorizing data */
static struct dbus_memo *api_dbus_client_memo_make(struct api_dbus *api, struct afb_req_common *comreq)
{
	struct dbus_memo *memo;

	memo = malloc(sizeof *memo);
	if (memo != NULL) {
		afb_req_common_addref(comreq);
		memo->comreq = comreq;
		memo->msgid = 0;
		memo->api = api;
		memo->next = api->client.memos;
		api->client.memos = memo;
	}
	return memo;
}

/* free and release the memorizing data */
static void api_dbus_client_memo_destroy(struct dbus_memo *memo)
{
	struct dbus_memo **prv;

	prv = &memo->api->client.memos;
	while (*prv != NULL) {
		if (*prv == memo) {
			*prv = memo->next;
			break;
		}
		prv = &(*prv)->next;
	}

	afb_req_common_unref(memo->comreq);
	free(memo);
}

/* search a memorized request */
static struct dbus_memo *api_dbus_client_memo_search(struct api_dbus *api, uint64_t msgid)
{
	struct dbus_memo *memo;

	memo = api->client.memos;
	while (memo != NULL && memo->msgid != msgid)
		memo = memo->next;

	return memo;
}

/* callback when received answer */
static int api_dbus_client_on_reply(sd_bus_message *message, void *userdata, sd_bus_error *ret_error)
{
	int rc;
	struct dbus_memo *memo;
	const char *json, *error, *info;
	const struct afb_data_x4 *params[3];

	/* retrieve the recorded data */
	memo = userdata;

	/* get the answer */
	rc = sd_bus_message_read(message, "sss", &json, &error, &info);
	if (rc < 0) {
		/* failing to have the answer */
		afb_req_common_reply_internal_error(memo->comreq);
	} else {
		/* build the reply */
		json = *json ? json : 0;
		error = *error ? error : 0;
		info = *info ? info : 0;
		sd_bus_message_ref(sd_bus_message_ref(sd_bus_message_ref(message)));
		rc = afb_json_legacy_make_reply_json_string_x4(params,
				json, (void*)sd_bus_message_unref, message,
				error, (void*)sd_bus_message_unref, message,
				info, (void*)sd_bus_message_unref, message);
		if (rc < 0) {
			/* failing to have the answer */
			afb_req_common_reply_internal_error(memo->comreq);
		} else {
			afb_req_common_reply(memo->comreq, LEGACY_STATUS(error), 3, params);
		}
	}
	api_dbus_client_memo_destroy(memo);
	return 1;
}

/* on call, propagate it to the dbus service */
static void api_dbus_client_process(void *closure, struct afb_req_common *comreq)
{
	struct api_dbus *api = closure;
	int rc;
	struct dbus_memo *memo;
	struct sd_bus_message *msg;
	const char *creds;
	const char *uuid;
	const char *json;
	struct afb_data *arg = NULL;

	/* create the recording data */
	memo = api_dbus_client_memo_make(api, comreq);
	if (memo == NULL) {
		afb_req_common_reply_out_of_memory(memo->comreq);
		return;
	}

	/* creates the message */
	msg = NULL;
	rc = sd_bus_message_new_method_call(api->sdbus, &msg, api->name, api->path, api->name, comreq->verbname);
	if (rc < 0)
		goto error;

	creds = afb_req_common_on_behalf_cred_export(comreq) ?: "";
	uuid = afb_session_uuid(comreq->session);
	if (comreq->nparams < 1
	 || afb_data_convert_to_x4(afb_data_of_data_x4(comreq->params[0]), AFB_TYPE_X4_JSON, &arg) < 0) {
		 json = "null";
	}
	else {
		json = afb_data_pointer(arg);
	}
	rc = sd_bus_message_append(msg, "ssus", json, uuid, 0, creds);
	if (rc < 0)
		goto error;

	/* makes the call */
	rc = sd_bus_call_async(api->sdbus, NULL, msg, api_dbus_client_on_reply, memo, (uint64_t)-1);
	if (rc < 0)
		goto error;

	rc = sd_bus_message_get_cookie(msg, &memo->msgid);
	if (rc >= 0)
		goto end;

error:
	/* if there was an error report it directly */
	errno = -rc;
	afb_req_common_reply_internal_error(memo->comreq);
	api_dbus_client_memo_destroy(memo);
end:
	afb_data_unref(arg);
	sd_bus_message_unref(msg);
}

/* receives broadcasted events */
static int api_dbus_client_on_broadcast_event(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
	const struct afb_data_x4 *param;
	const char *event, *data;
	const unsigned char *uuid;
	size_t szuuid;
	uint8_t hop;

	int rc = sd_bus_message_read(m, "ssayy", &event, &data, &uuid, &szuuid, &hop);
	if (rc < 0)
		ERROR("unreadable broadcasted event");
	else {
		data = *data ? data : "null";
		rc = afb_data_x4_create_set_x4(&param, AFB_TYPE_X4_JSON, data, 1+strlen(data),
						(void*)sd_bus_message_unref, sd_bus_message_ref(m));
		if (rc >= 0)
			afb_evt_rebroadcast_name_x4(event, 1, &param, uuid, hop);
	}
	return 1;
}

/* search the eventid */
static struct dbus_event *api_dbus_client_event_search(struct api_dbus *api, int id, const char *name)
{
	struct dbus_event *ev;

	ev = api->client.events;
	while (ev != NULL && (ev->id != id || 0 != strcmp(afb_evt_fullname(ev->event), name)))
		ev = ev->next;

	return ev;
}

/* adds an eventid */
static void api_dbus_client_event_create(struct api_dbus *api, int id, const char *name)
{
	struct dbus_event *ev;

	/* check conflicts */
	ev = api_dbus_client_event_search(api, id, name);
	if (ev != NULL) {
		ev->refcount++;
		return;
	}

	/* no conflict, try to add it */
	ev = malloc(sizeof *ev);
	if (ev != NULL) {
		if (afb_evt_create(&ev->event, name) < 0)
			free(ev);
		else {
			ev->refcount = 1;
			ev->id = id;
			ev->next = api->client.events;
			api->client.events = ev;
			return;
		}
	}
	ERROR("can't create event %s, out of memory", name);
}

/* removes an eventid */
static void api_dbus_client_event_drop(struct api_dbus *api, int id, const char *name)
{
	struct dbus_event *ev, **prv;

	/* retrieves the event */
	ev = api_dbus_client_event_search(api, id, name);
	if (ev == NULL) {
		ERROR("event %s not found", name);
		return;
	}

	/* decrease the reference count */
	if (--ev->refcount)
		return;

	/* unlinks the event */
	prv = &api->client.events;
	while (*prv != ev)
		prv = &(*prv)->next;
	*prv = ev->next;

	/* destroys the event */
	afb_evt_unref(ev->event);
	free(ev);
}

/* pushs an event */
static void api_dbus_client_event_push(struct api_dbus *api, int id, const char *name, const char *data, sd_bus_message *m)
{
	int rc;
	const struct afb_data_x4 *param;
	struct dbus_event *ev;

	/* retrieves the event */
	ev = api_dbus_client_event_search(api, id, name);
	if (ev == NULL) {
		ERROR("event %s not found", name);
		return;
	}

	data = *data ? data : "null";
	rc = afb_data_x4_create_set_x4(&param, AFB_TYPE_X4_JSON, data, 1+strlen(data),
					(void*)sd_bus_message_unref, sd_bus_message_ref(m));
	if (rc >= 0)
		afb_evt_push_x4(ev->event, 1, &param);
}

/* subscribes an event */
static void api_dbus_client_event_subscribe(struct api_dbus *api, int id, const char *name, uint64_t msgid)
{
	int rc;
	struct dbus_event *ev;
	struct dbus_memo *memo;

	/* retrieves the event */
	ev = api_dbus_client_event_search(api, id, name);
	if (ev == NULL) {
		ERROR("event %s not found", name);
		return;
	}

	/* retrieves the memo */
	memo = api_dbus_client_memo_search(api, msgid);
	if (memo == NULL) {
		ERROR("message not found");
		return;
	}

	/* subscribe the request to the event */
	rc = afb_req_common_subscribe(memo->comreq, ev->event);
	if (rc < 0)
		ERROR("can't subscribe: %s", strerror(-rc));
}

/* unsubscribes an event */
static void api_dbus_client_event_unsubscribe(struct api_dbus *api, int id, const char *name, uint64_t msgid)
{
	int rc;
	struct dbus_event *ev;
	struct dbus_memo *memo;

	/* retrieves the event */
	ev = api_dbus_client_event_search(api, id, name);
	if (ev == NULL) {
		ERROR("event %s not found", name);
		return;
	}

	/* retrieves the memo */
	memo = api_dbus_client_memo_search(api, msgid);
	if (memo == NULL) {
		ERROR("message not found");
		return;
	}

	/* unsubscribe the request from the event */
	rc = afb_req_common_unsubscribe(memo->comreq, ev->event);
	if (rc < 0)
		ERROR("can't unsubscribe: %s", strerror(-rc));
}

/* receives calls for event */
static int api_dbus_client_on_manage_event(sd_bus_message *m, void *userdata, sd_bus_error *ret_error)
{
	const char *eventname, *data;
	int rc;
	int32_t eventid;
	uint8_t order;
	struct api_dbus *api;
	uint64_t msgid;

	/* check if expected message */
	api = userdata;
	if (0 != strcmp(api->name, sd_bus_message_get_interface(m)))
		return 0; /* not the expected interface */
	if (0 != strcmp("event", sd_bus_message_get_member(m)))
		return 0; /* not the expected member */
	if (sd_bus_message_get_expect_reply(m))
		return 0; /* not the expected type of message */

	/* reads the message */
	rc = sd_bus_message_read(m, "yisst", &order, &eventid, &eventname, &data, &msgid);
	if (rc < 0) {
		ERROR("unreadable event");
		return 1;
	}

	/* what is the order ? */
	switch ((char)order) {
	case '+': /* creates the event */
		api_dbus_client_event_create(api, eventid, eventname);
		break;
	case '-': /* drops the event */
		api_dbus_client_event_drop(api, eventid, eventname);
		break;
	case '!': /* pushs the event */
		api_dbus_client_event_push(api, eventid, eventname, data, m);
		break;
	case 'S': /* subscribe event for a request */
		api_dbus_client_event_subscribe(api, eventid, eventname, msgid);
		break;
	case 'U': /* unsubscribe event for a request */
		api_dbus_client_event_unsubscribe(api, eventid, eventname, msgid);
		break;
	default:
		/* unexpected order */
		ERROR("unexpected order '%c' received", (char)order);
		break;
	}
	return 1;
}

static struct afb_api_itf dbus_api_itf = {
	.process = api_dbus_client_process
};

/* adds a afb-dbus-service client api */
int afb_api_dbus_add_client(const char *path, struct afb_apiset *declare_set, struct afb_apiset *call_set)
{
	int rc;
	struct api_dbus *api;
	struct afb_api_item afb_api;
	char *match;

	/* create the dbus client api */
	api = make_api_dbus(path);
	if (api == NULL) {
		rc = X_ENOMEM;
		goto error;
	}

	/* connect to broadcasted events */
	rc = asprintf(&match, "type='signal',path='%s',interface='%s',member='broadcast'", api->path, api->name);
	if (rc < 0) {
		ERROR("out of memory");
		rc = X_ENOMEM;
		goto error;
	}
	rc = sd_bus_add_match(api->sdbus, &api->client.slot_broadcast, match, api_dbus_client_on_broadcast_event, api);
	free(match);
	if (rc < 0) {
		ERROR("can't add dbus match %s for %s", api->path, api->name);
		goto error;
	}

	/* connect to event management */
	rc = sd_bus_add_object(api->sdbus, &api->client.slot_event, api->path, api_dbus_client_on_manage_event, api);
	if (rc < 0) {
		ERROR("can't add dbus object %s for %s", api->path, api->name);
		goto error;
	}

	/* record it as an API */
	afb_api.closure = api;
	afb_api.itf = &dbus_api_itf;
	afb_api.group = NULL;
	rc = afb_apiset_add(declare_set, api->api, afb_api);
	if (rc < 0)
		goto error2;

	return 0;

error2:
	destroy_api_dbus(api);
error:
	return rc;
}

/******************* event structures for server part **********************************/

static void afb_api_dbus_server_event_add(void *closure, const char *event, uint16_t eventid);
static void afb_api_dbus_server_event_remove(void *closure, const char *event, uint16_t eventid);
static void afb_api_dbus_server_event_push(void *closure, const struct afb_evt_pushed *event);
static void afb_api_dbus_server_event_broadcast(void *closure, const struct afb_evt_broadcasted *event);

/* the interface for events broadcasting */
static const struct afb_evt_itf evt_broadcast_itf = {
	.broadcast = afb_api_dbus_server_event_broadcast,
};

/* the interface for events pushing */
static const struct afb_evt_itf evt_push_itf = {
	.push = afb_api_dbus_server_event_push,
	.add = afb_api_dbus_server_event_add,
	.remove = afb_api_dbus_server_event_remove
};

/******************* origin description part for server *****************************/

struct origin
{
	/* link to next different origin */
	struct origin *next;

	/* the server dbus-api */
	struct api_dbus *api;

	/* count of references */
	int refcount;
#if WITH_CRED
	/* credentials of the origin */
	struct afb_cred *cred;
#endif
	/* the origin */
	char name[];
};

#if WITH_CRED
/* get the credentials for the message */
static void init_origin_creds(struct origin *origin)
{
	int rc;
	sd_bus_creds *c;
	uid_t uid;
	gid_t gid;
	pid_t pid;
	const char *context;

	rc = sd_bus_get_name_creds(origin->api->sdbus, origin->name,
			SD_BUS_CREDS_PID|SD_BUS_CREDS_UID|SD_BUS_CREDS_GID|SD_BUS_CREDS_SELINUX_CONTEXT,
			&c);
	if (rc < 0)
		origin->cred = NULL;
	else {
		sd_bus_creds_get_uid(c, &uid);
		sd_bus_creds_get_gid(c, &gid);
		sd_bus_creds_get_pid(c, &pid);
		sd_bus_creds_get_selinux_context(c, &context);
		afb_cred_create(&origin->cred, uid, gid, pid, context);
		sd_bus_creds_unref(c);
	}
}
#endif

static struct origin *afb_api_dbus_server_origin_get(struct api_dbus *api, const char *sender)
{
	struct origin *origin;

	/* searchs for an existing origin */
	origin = api->server.origins;
	while (origin != NULL) {
		if (0 == strcmp(origin->name, sender)) {
			origin->refcount++;
			return origin;
		}
		origin = origin->next;
	}

	/* not found, create it */
	origin = malloc(strlen(sender) + 1 + sizeof *origin);
	if (origin == NULL)
		errno = ENOMEM;
	else {
		origin->api = api;
		origin->refcount = 1;
		strcpy(origin->name, sender);
#if WITH_CRED
		init_origin_creds(origin);
#endif
		origin->next = api->server.origins;
		api->server.origins = origin;
	}
	return origin;
}

static struct origin *afb_api_dbus_server_origin_addref(struct origin *origin)
{
	origin->refcount += 1;
	return origin;
}

static void afb_api_dbus_server_origin_unref(struct origin *origin)
{
	if (!--origin->refcount) {
		struct origin **prv;

		prv = &origin->api->server.origins;
		while(*prv != origin)
			prv = &(*prv)->next;
		*prv = origin->next;
#if WITH_CRED
		afb_cred_unref(origin->cred);
#endif
		free(origin);
	}
}

struct listener
{
	/* link to next different origin */
	struct origin *origin;

	/* the listener of events */
	struct afb_evt_listener *listener;
};

static void afb_api_dbus_server_listener_free(void *closure)
{
	struct listener *listener = closure;
	afb_evt_listener_unref(listener->listener);
	afb_api_dbus_server_origin_unref(listener->origin);
	free(listener);
}

static void *afb_api_dbus_server_listener_make(void *closure)
{
	struct origin *origin = closure;
	struct listener *listener;

	listener = malloc(sizeof *listener);
	if (listener != NULL) {
		listener->listener = afb_evt_listener_create(&evt_push_itf, origin);
		if (listener->listener != NULL) {
			listener->origin = afb_api_dbus_server_origin_addref(origin);
		}
		else {
			free(listener);
			listener = NULL;
		}
	}
	return listener;
}

static struct listener *afb_api_dbus_server_listener_get(struct api_dbus *api, const char *sender, struct afb_session *session)
{
	struct listener *listener;
	struct origin *origin;

	/* get the origin */
	origin = afb_api_dbus_server_origin_get(api, sender);
	if (origin == NULL)
		return NULL;

	listener = afb_session_cookie(session, origin, afb_api_dbus_server_listener_make, afb_api_dbus_server_listener_free, origin, Afb_Session_Cookie_Init);
	if (listener == NULL)
		errno = ENOMEM;
	afb_api_dbus_server_origin_unref(origin);
	return NULL;
}

/******************* dbus request part for server *****************/

/**
 * Structure for a dbus request
 */
struct dbus_req {
	struct afb_req_common comreq;		/**< the comreq of the request */
	sd_bus_message *message;	/**< the incoming request message */
	struct listener *listener;	/**< the listener for events */
	struct api_dbus *dbusapi;	/**< the api */
};

/* decrement the reference count of the request and free/release it on falling to null */
static void dbus_req_destroy(struct afb_req_common *comreq)
{
	struct dbus_req *dreq = containerof(struct dbus_req, comreq, comreq);

	afb_req_common_cleanup(comreq);
	sd_bus_message_unref(dreq->message);
	free(dreq);
}

void dbus_req_raw_reply_cb(void *closure, const char *object, const char *error, const char *info)
{
	struct afb_req_common *comreq = closure;
	struct dbus_req *dreq = containerof(struct dbus_req, comreq, comreq);
	int rc;

	rc = sd_bus_reply_method_return(dreq->message, "sss",
		object ?: "",
		error ?: "",
		info ?: "");
	if (rc < 0)
		ERROR("sending the reply failed");
}

void dbus_req_raw_reply(struct afb_req_common *comreq, int status, unsigned nreplies, const struct afb_data_x4 * const *replies)
{
	afb_json_legacy_do_reply_json_string(comreq, status, nreplies, replies, dbus_req_raw_reply_cb);
}

static void afb_api_dbus_server_event_send(struct origin *origin, char order, const char *event, int eventid, const char *data, uint64_t msgid);

static int dbus_req_subscribe(struct afb_req_common *comreq, struct afb_evt *event)
{
	struct dbus_req *dreq = containerof(struct dbus_req, comreq, comreq);
	uint64_t msgid;
	int rc;

	rc = afb_evt_listener_watch_evt(dreq->listener->listener, event);
	sd_bus_message_get_cookie(dreq->message, &msgid);
	afb_api_dbus_server_event_send(dreq->listener->origin, 'S', afb_evt_fullname(event), afb_evt_id(event), "", msgid);
	return rc;
}

static int dbus_req_unsubscribe(struct afb_req_common *comreq, struct afb_evt *event)
{
	struct dbus_req *dreq = containerof(struct dbus_req, comreq, comreq);
	uint64_t msgid;
	int rc;

	sd_bus_message_get_cookie(dreq->message, &msgid);
	afb_api_dbus_server_event_send(dreq->listener->origin, 'U', afb_evt_fullname(event), afb_evt_id(event), "", msgid);
	rc = afb_evt_listener_unwatch_evt(dreq->listener->listener, event);
	return rc;
}

const struct afb_req_common_query_itf afb_api_dbus_req_common_itf = {
	.reply = dbus_req_raw_reply,
	.unref = dbus_req_destroy,
	.subscribe = dbus_req_subscribe,
	.unsubscribe = dbus_req_unsubscribe,
};

/******************* server part **********************************/

static void afb_api_dbus_server_event_send(struct origin *origin, char order, const char *event, int eventid, const char *data, uint64_t msgid)
{
	int rc;
	struct api_dbus *api;
	struct sd_bus_message *msg;

	api = origin->api;
	msg = NULL;

	rc = sd_bus_message_new_method_call(api->sdbus, &msg, origin->name, api->path, api->name, "event");
	if (rc < 0)
		goto error;

	rc = sd_bus_message_append(msg, "yisst", (uint8_t)order, (int32_t)eventid, event, data, msgid);
	if (rc < 0)
		goto error;

	rc = sd_bus_send(api->sdbus, msg, NULL); /* NULL for cookie implies no expected reply */
	if (rc >= 0)
		goto end;

error:
	ERROR("error while send event %c%s(%d) to %s", order, event, eventid, origin->name);
end:
	sd_bus_message_unref(msg);
}

static void afb_api_dbus_server_event_add(void *closure, const char *event, uint16_t eventid)
{
	afb_api_dbus_server_event_send(closure, '+', event, eventid, "", 0);
}

static void afb_api_dbus_server_event_remove(void *closure, const char *event, uint16_t eventid)
{
	afb_api_dbus_server_event_send(closure, '-', event, eventid, "", 0);
}

static void server_event_push_cb(void *closure1, const char *json, const void *closure2)
{
	struct origin *origin = closure1;
	const struct afb_evt_pushed *event = closure2;

	afb_api_dbus_server_event_send(origin, '!', event->data.name, event->data.eventid, json, 0);
}

static void afb_api_dbus_server_event_push(void *closure, const struct afb_evt_pushed *event)
{
	afb_json_legacy_do2_single_json_string(event->data.nparams, event->data.params, server_event_push_cb, closure, event);
}

struct server_event_broadcast_cb_data
{
	struct api_dbus *api;
	const struct afb_evt_broadcasted *event;
};

static void server_event_broadcast_cb(void *closure1, const char *json, const void *closure2)
{
	struct api_dbus *api = closure1;
	const struct afb_evt_broadcasted *event = closure2;
	int rc = sd_bus_emit_signal(api->sdbus, api->path, api->name, "broadcast",
			"ssayy", event->data.name, json,
			event->uuid, UUID_BINARY_LENGTH, event->hop);
	if (rc < 0)
		ERROR("error while broadcasting event %s", event->data.name);
}

static void afb_api_dbus_server_event_broadcast(void *closure, const struct afb_evt_broadcasted *event)
{
	afb_json_legacy_do2_single_json_string(event->data.nparams, event->data.params, server_event_broadcast_cb, closure, event);
}

/* called when the object for the service is called */
static int api_dbus_server_on_object_called(sd_bus_message *message, void *userdata, sd_bus_error *ret_error)
{
	int rc;
	const char *method;
	const char *request;
	const char *uuid;
	const char *creds;
	struct dbus_req *dreq;
	struct api_dbus *api = userdata;
	uint32_t flags;
	struct listener *listener;
	const struct afb_data_x4 *arg;
	struct afb_session *session;

	/* check the interface */
	if (strcmp(sd_bus_message_get_interface(message), api->name) != 0)
		return 0;

	/* get the method */
	method = sd_bus_message_get_member(message);

	/* create the request */
	dreq = calloc(1 , sizeof *dreq);
	if (dreq == NULL)
		goto out_of_memory;

	/* get the data */
	rc = sd_bus_message_read(message, "ssus", &request, &uuid, &flags, &creds);
	if (rc < 0) {
		sd_bus_reply_method_errorf(message, SD_BUS_ERROR_INVALID_SIGNATURE, "invalid signature");
		goto error;
	}
	session = afb_session_get(uuid, AFB_SESSION_TIMEOUT_DEFAULT, NULL);
	/* get the listener */
	listener = afb_api_dbus_server_listener_get(api, sd_bus_message_get_sender(message), dreq->comreq.session);
	if (listener == NULL)
		goto out_of_memory;

	rc = afb_data_x4_create_set_x4(&arg, AFB_TYPE_X4_JSON, request, 1+strlen(request),
					(void*)sd_bus_message_unref, sd_bus_message_ref(message));
	if (rc < 0)
		goto out_of_memory;

	/* connect to the context */
	afb_req_common_init(&dreq->comreq, &afb_api_dbus_req_common_itf, api->api, method, 1, &arg);
	afb_req_common_set_session(&dreq->comreq, session);

	/* fulfill the request and emit it */
	dreq->message = sd_bus_message_ref(message);
	dreq->listener = listener;
	dreq->dbusapi = api;

#if WITH_CRED
	afb_req_common_set_cred(&dreq->comreq, listener->origin->cred);
#endif
	afb_req_common_process_on_behalf(&dreq->comreq, dreq->dbusapi->server.apiset, creds);
	return 1;

out_of_memory:
	sd_bus_reply_method_errorf(message, SD_BUS_ERROR_NO_MEMORY, "out of memory");
error:
	free(dreq);
	return 1;
}

/* create the service */
int afb_api_dbus_add_server(const char *path, struct afb_apiset *declare_set, struct afb_apiset *call_set)
{
	int rc;
	struct api_dbus *api;

	/* get the dbus api object connected */
	api = make_api_dbus(path);
	if (api == NULL) {
		rc = X_ENOMEM;
		goto error;
	}

	/* request the service object name */
	rc = sd_bus_request_name(api->sdbus, api->name, 0);
	if (rc < 0) {
		ERROR("can't register name %s", api->name);
		goto error2;
	}

	/* connect the service to the dbus object */
	rc = sd_bus_add_object(api->sdbus, &api->server.slot_call, api->path, api_dbus_server_on_object_called, api);
	if (rc < 0) {
		ERROR("can't add dbus object %s for %s", api->path, api->name);
		goto error3;
	}
	INFO("afb service over dbus installed, name %s, path %s", api->name, api->path);

	api->server.listener = afb_evt_listener_create(&evt_broadcast_itf, api);
	api->server.apiset = afb_apiset_addref(call_set);
	return 0;
error3:
	sd_bus_release_name(api->sdbus, api->name);
error2:
	destroy_api_dbus(api);
error:
	return rc;
}

#endif

