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

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <json-c/json.h>
#include <afb/afb-event-x2-itf.h>

#include "core/afb-evt.h"
#include "core/afb-hook.h"
#include "sys/verbose.h"
#include "core/afb-jobs.h"
#include "utils/uuid.h"
#include "sys/x-mutex.h"
#include "sys/x-rwlock.h"
#include "sys/x-errno.h"

struct afb_evt_watch;

/*
 * Structure for event listeners
 */
struct afb_evt_listener {

	/* chaining listeners */
	struct afb_evt_listener *next;

	/* interface for callbacks */
	const struct afb_evt_itf *itf;

	/* closure for the callback */
	void *closure;

	/* head of the list of events listened */
	struct afb_evt_watch *watchs;

	/* rwlock of the listener */
	x_rwlock_t rwlock;

	/* count of reference to the listener */
	uint16_t refcount;
};

/*
 * Structure for describing events
 */
struct afb_evt {

	/* interface */
	struct afb_event_x2 eventx2;

	/* next event */
	struct afb_evt *next;

	/* head of the list of listeners watching the event */
	struct afb_evt_watch *watchs;

	/* rwlock of the event */
	x_rwlock_t rwlock;

#if WITH_AFB_HOOK
	/* hooking */
	int hookflags;
#endif

	/* refcount */
	uint16_t refcount;

	/* id of the event */
	uint16_t id;

	/* fullname of the event */
	char fullname[];
};

/*
 * Structure for associating events and listeners
 */
struct afb_evt_watch {

	/* the evt */
	struct afb_evt *evt;

	/* link to the next watcher for the same evt */
	struct afb_evt_watch *next_by_evt;

	/* the listener */
	struct afb_evt_listener *listener;

	/* link to the next watcher for the same listener */
	struct afb_evt_watch *next_by_listener;
};

/*
 * structure for job of broadcasting events
 */
struct job_broadcast
{
	/** object atached to the event */
	struct json_object *object;

	/** the uuid of the event */
	uuid_binary_t  uuid;

	/** remaining hop */
	uint8_t hop;

	/** name of the event to broadcast */
	char event[];
};

/*
 * structure for job of broadcasting or pushing events
 */
struct job_evt
{
	/** the event to broadcast */
	struct afb_evt *evt;

	/** object atached to the event */
	struct json_object *object;
};

/* the interface for events */
static struct afb_event_x2_itf afb_evt_event_x2_itf = {
	.broadcast = (void*)afb_evt_broadcast,
	.push = (void*)afb_evt_push,
	.unref = (void*)afb_evt_unref,
	.name = (void*)afb_evt_name,
	.addref = (void*)afb_evt_addref
};

#if WITH_AFB_HOOK
/* the interface for events */
static struct afb_event_x2_itf afb_evt_hooked_event_x2_itf = {
	.broadcast = (void*)afb_evt_hooked_broadcast,
	.push = (void*)afb_evt_hooked_push,
	.unref = (void*)afb_evt_hooked_unref,
	.name = (void*)afb_evt_hooked_name,
	.addref = (void*)afb_evt_hooked_addref
};
#endif

/* job groups for events push/broadcast */
#define BROADCAST_JOB_GROUP  (&afb_evt_event_x2_itf)
#define PUSH_JOB_GROUP       (&afb_evt_event_x2_itf)

/* head of the list of listeners */
static x_rwlock_t listeners_rwlock = X_RWLOCK_INITIALIZER;
static struct afb_evt_listener *listeners = NULL;

/* handling id of events */
static x_rwlock_t events_rwlock = X_RWLOCK_INITIALIZER;
static struct afb_evt *evt_list_head = NULL;
static uint16_t event_genid = 0;
static uint16_t event_count = 0;

/* head of uniqueness of events */
#if !defined(EVENT_BROADCAST_HOP_MAX)
#  define EVENT_BROADCAST_HOP_MAX  10
#endif
#if !defined(EVENT_BROADCAST_MEMORY_COUNT)
#  define EVENT_BROADCAST_MEMORY_COUNT  8
#endif

#if EVENT_BROADCAST_MEMORY_COUNT
static struct {
	x_mutex_t mutex;
	uint8_t base;
	uint8_t count;
	uuid_binary_t uuids[EVENT_BROADCAST_MEMORY_COUNT];
} uniqueness = {
	.mutex = X_MUTEX_INITIALIZER,
	.base = 0,
	.count = 0
};
#endif

/*
 * Create structure for job of broadcasting string 'event' with 'object'
 * Returns the created structure or NULL if out of memory
 */
static struct job_broadcast *make_job_broadcast(const char *event, struct json_object *object, const uuid_binary_t uuid, uint8_t hop)
{
	size_t sz = 1 + strlen(event);
	struct job_broadcast *jb = malloc(sz + sizeof *jb);
	if (jb) {
		jb->object = object;
		memcpy(jb->uuid, uuid, sizeof jb->uuid);
		jb->hop = hop;
		memcpy(jb->event, event, sz);
	}
	return jb;
}

/*
 * Destroy structure 'jb' for job of broadcasting string events
 */
static void destroy_job_broadcast(struct job_broadcast *jb)
{
	json_object_put(jb->object);
	free(jb);
}

/*
 * Create structure for job of broadcasting or pushing 'evt' with 'object'
 * Returns the created structure or NULL if out of memory
 */
static struct job_evt *make_job_evt(struct afb_evt *evt, struct json_object *object)
{
	struct job_evt *je = malloc(sizeof *je);
	if (je) {
		je->evt = afb_evt_addref(evt);
		je->object = object;
	}
	return je;
}

/*
 * Destroy structure for job of broadcasting or pushing evt
 */
static void destroy_job_evt(struct job_evt *je)
{
	afb_evt_unref(je->evt);
	json_object_put(je->object);
	free(je);
}

/*
 * Broadcasts the 'event' of 'id' with its 'object'
 */
static void broadcast(struct job_broadcast *jb)
{
	struct afb_evt_listener *listener;

	x_rwlock_rdlock(&listeners_rwlock);
	listener = listeners;
	while(listener) {
		if (listener->itf->broadcast != NULL)
			listener->itf->broadcast(listener->closure, jb->event, json_object_get(jb->object), jb->uuid, jb->hop);
		listener = listener->next;
	}
	x_rwlock_unlock(&listeners_rwlock);
}

/*
 * Jobs callback for broadcasting string asynchronously
 */
static void broadcast_job(int signum, void *closure)
{
	struct job_broadcast *jb = closure;

	if (signum == 0)
		broadcast(jb);
	destroy_job_broadcast(jb);
}

/*
 * Broadcasts the string 'event' with its 'object'
 */
static int unhooked_broadcast_name(const char *event, struct json_object *object, const uuid_binary_t uuid, uint8_t hop)
{
	uuid_binary_t local_uuid;
	struct job_broadcast *jb;
	int rc;
#if EVENT_BROADCAST_MEMORY_COUNT
	int iter, count;
#endif

	/* check if lately sent */
	if (!uuid) {
		uuid_new_binary(local_uuid);
		uuid = local_uuid;
		hop = EVENT_BROADCAST_HOP_MAX;
#if EVENT_BROADCAST_MEMORY_COUNT
		x_mutex_lock(&uniqueness.mutex);
	} else {
		x_mutex_lock(&uniqueness.mutex);
		iter = (int)uniqueness.base;
		count = (int)uniqueness.count;
		while (count) {
			if (0 == memcmp(uuid, uniqueness.uuids[iter], sizeof(uuid_binary_t))) {
				x_mutex_unlock(&uniqueness.mutex);
				return 0;
			}
			if (++iter == EVENT_BROADCAST_MEMORY_COUNT)
				iter = 0;
			count--;
		}
	}
	iter = (int)uniqueness.base;
	if (uniqueness.count < EVENT_BROADCAST_MEMORY_COUNT)
		iter += (int)(uniqueness.count++);
	else if (++uniqueness.base == EVENT_BROADCAST_MEMORY_COUNT)
		uniqueness.base = 0;
	memcpy(uniqueness.uuids[iter], uuid, sizeof(uuid_binary_t));
	x_mutex_unlock(&uniqueness.mutex);
#else
	}
#endif

	/* create the structure for the job */
	jb = make_job_broadcast(event, object, uuid, hop);
	if (jb == NULL) {
		ERROR("Cant't create broadcast string job item for %s(%s)",
			event, json_object_to_json_string(object));
		json_object_put(object);
		return X_ENOMEM;
	}

	/* queue the job */
	rc = afb_jobs_queue(BROADCAST_JOB_GROUP, 0, broadcast_job, jb);
	if (rc < 0) {
		ERROR("cant't queue broadcast string job item for %s(%s)",
			event, json_object_to_json_string(object));
		destroy_job_broadcast(jb);
	}
	return rc;
}

/*
 * Broadcasts the event 'evt' with its 'object'
 * 'object' is released (like json_object_put)
 * Returns the count of listener that received the event.
 */
int afb_evt_broadcast(struct afb_evt *evt, struct json_object *object)
{
	return unhooked_broadcast_name(evt->fullname, object, NULL, 0);
}

#if WITH_AFB_HOOK
/*
 * Broadcasts the event 'evt' with its 'object'
 * 'object' is released (like json_object_put)
 * Returns the count of listener that received the event.
 */
int afb_evt_hooked_broadcast(struct afb_evt *evt, struct json_object *object)
{
	int result;

	json_object_get(object);

	if (evt->hookflags & afb_hook_flag_evt_broadcast_before)
		afb_hook_evt_broadcast_before(evt->fullname, evt->id, object);

	result = afb_evt_broadcast(evt, object);

	if (evt->hookflags & afb_hook_flag_evt_broadcast_after)
		afb_hook_evt_broadcast_after(evt->fullname, evt->id, object, result);

	json_object_put(object);

	return result;
}
#endif

int afb_evt_rebroadcast_name(const char *event, struct json_object *object, const uuid_binary_t uuid, uint8_t hop)
{
	int result;

#if WITH_AFB_HOOK
	json_object_get(object);
	afb_hook_evt_broadcast_before(event, 0, object);
#endif

	result = unhooked_broadcast_name(event, object, uuid, hop);

#if WITH_AFB_HOOK
	afb_hook_evt_broadcast_after(event, 0, object, result);
	json_object_put(object);
#endif
	return result;
}

/*
 * Broadcasts the 'event' with its 'object'
 * 'object' is released (like json_object_put)
 * Returns the count of listener having receive the event.
 */
int afb_evt_broadcast_name(const char *event, struct json_object *object)
{
	return afb_evt_rebroadcast_name(event, object, NULL, 0);
}

/*
 * Pushes the event 'evt' with 'obj' to its listeners
 * Returns the count of listener that received the event.
 */
static void push_evt(struct afb_evt *evt, struct json_object *object)
{
	struct afb_evt_watch *watch;
	struct afb_evt_listener *listener;

	x_rwlock_rdlock(&evt->rwlock);
	watch = evt->watchs;
	while(watch) {
		listener = watch->listener;
		assert(listener->itf->push != NULL);
		listener->itf->push(listener->closure, evt->fullname, evt->id, json_object_get(object));
		watch = watch->next_by_evt;
	}
	x_rwlock_unlock(&evt->rwlock);
}

/*
 * Jobs callback for pushing evt asynchronously
 */
static void push_job_evt(int signum, void *closure)
{
	struct job_evt *je = closure;

	if (signum == 0)
		push_evt(je->evt, je->object);
	destroy_job_evt(je);
}

/*
 * Pushes the event 'evt' with 'obj' to its listeners
 * 'obj' is released (like json_object_put)
 * Returns 1 if at least one listener exists or 0 if no listener exists or
 * -1 in case of error and the event can't be delivered
 */
int afb_evt_push(struct afb_evt *evt, struct json_object *object)
{
	struct job_evt *je;
	int rc;

	if (!evt->watchs) {
		json_object_put(object);
		return 0;
	}

	je = make_job_evt(evt, object);
	if (je == NULL) {
		ERROR("Cant't create push evt job item for %s(%s)",
			evt->fullname, json_object_to_json_string(object));
		json_object_put(object);
		return X_ENOMEM;
	}

	rc = afb_jobs_queue(PUSH_JOB_GROUP, 0, push_job_evt, je);
	if (rc == 0)
		rc = 1;
	else {
		ERROR("cant't queue push evt job item for %s(%s)",
			evt->fullname, json_object_to_json_string(object));
		destroy_job_evt(je);
	}

	return rc;
}

#if WITH_AFB_HOOK
/*
 * Pushes the event 'evt' with 'obj' to its listeners
 * 'obj' is released (like json_object_put)
 * Emits calls to hooks.
 * Returns the count of listener taht received the event.
 */
int afb_evt_hooked_push(struct afb_evt *evt, struct json_object *obj)
{

	int result;

	/* lease the object */
	json_object_get(obj);

	/* hook before push */
	if (evt->hookflags & afb_hook_flag_evt_push_before)
		afb_hook_evt_push_before(evt->fullname, evt->id, obj);

	/* push */
	result = afb_evt_push(evt, obj);

	/* hook after push */
	if (evt->hookflags & afb_hook_flag_evt_push_after)
		afb_hook_evt_push_after(evt->fullname, evt->id, obj, result);

	/* release the object */
	json_object_put(obj);
	return result;
}
#endif

static void unwatch(struct afb_evt_listener *listener, struct afb_evt *evt, int remove)
{
	/* notify listener if needed */
	if (remove && listener->itf->remove != NULL)
		listener->itf->remove(listener->closure, evt->fullname, evt->id);
}

static void evt_unwatch(struct afb_evt *evt, struct afb_evt_listener *listener, struct afb_evt_watch *watch, int remove)
{
	struct afb_evt_watch **prv;

	/* notify listener if needed */
	unwatch(listener, evt, remove);

	/* unlink the watch for its event */
	x_rwlock_wrlock(&listener->rwlock);
	prv = &listener->watchs;
	while(*prv) {
		if (*prv == watch) {
			*prv = watch->next_by_listener;
			break;
		}
		prv = &(*prv)->next_by_listener;
	}
	x_rwlock_unlock(&listener->rwlock);

	/* recycle memory */
	free(watch);
}

static void listener_unwatch(struct afb_evt_listener *listener, struct afb_evt *evt, struct afb_evt_watch *watch, int remove)
{
	struct afb_evt_watch **prv;

	/* notify listener if needed */
	unwatch(listener, evt, remove);

	/* unlink the watch for its event */
	x_rwlock_wrlock(&evt->rwlock);
	prv = &evt->watchs;
	while(*prv) {
		if (*prv == watch) {
			*prv = watch->next_by_evt;
			break;
		}
		prv = &(*prv)->next_by_evt;
	}
	x_rwlock_unlock(&evt->rwlock);

	/* recycle memory */
	free(watch);
}

/*
 * Creates an event of name 'fullname' and returns it or NULL on error.
 */
struct afb_evt *afb_evt_create(const char *fullname)
{
	size_t len;
	struct afb_evt *evt, *oevt;
	uint16_t id;

	/* allocates the event */
	len = strlen(fullname);
	evt = malloc(len + 1 + sizeof * evt);
	if (evt == NULL)
		goto error;

	/* allocates the id */
	x_rwlock_wrlock(&events_rwlock);
	if (event_count == UINT16_MAX) {
		x_rwlock_unlock(&events_rwlock);
		free(evt);
		ERROR("Can't create more events");
		return NULL;
	}
	event_count++;
	do {
		/* TODO add a guard (counting number of event created) */
		id = ++event_genid;
		if (!id)
			id = event_genid = 1;
		oevt = evt_list_head;
		while(oevt != NULL && oevt->id != id)
			oevt = oevt->next;
	} while (oevt != NULL);

	/* initialize the event */
	memcpy(evt->fullname, fullname, len + 1);
	evt->next = evt_list_head;
	evt->refcount = 1;
	evt->watchs = NULL;
	evt->id = id;
	x_rwlock_init(&evt->rwlock);
	evt_list_head = evt;
#if WITH_AFB_HOOK
	evt->hookflags = afb_hook_flags_evt(evt->fullname);
	evt->eventx2.itf = evt->hookflags ? &afb_evt_hooked_event_x2_itf : &afb_evt_event_x2_itf;
	if (evt->hookflags & afb_hook_flag_evt_create)
		afb_hook_evt_create(evt->fullname, evt->id);
#else
	evt->eventx2.itf = &afb_evt_event_x2_itf;
#endif
	x_rwlock_unlock(&events_rwlock);

	/* returns the event */
	return evt;
error:
	return NULL;
}

/*
 * Creates an event of name 'prefix'/'name' and returns it or NULL on error.
 */
struct afb_evt *afb_evt_create2(const char *prefix, const char *name)
{
	size_t prelen, postlen;
	char *fullname;

	/* makes the event fullname */
	prelen = strlen(prefix);
	postlen = strlen(name);
	fullname = alloca(prelen + postlen + 2);
	memcpy(fullname, prefix, prelen);
	fullname[prelen] = '/';
	memcpy(fullname + prelen + 1, name, postlen + 1);

	/* create the event */
	return afb_evt_create(fullname);
}

/*
 * increment the reference count of the event 'evt'
 */
struct afb_evt *afb_evt_addref(struct afb_evt *evt)
{
	__atomic_add_fetch(&evt->refcount, 1, __ATOMIC_RELAXED);
	return evt;
}

#if WITH_AFB_HOOK
/*
 * increment the reference count of the event 'evt'
 */
struct afb_evt *afb_evt_hooked_addref(struct afb_evt *evt)
{
	if (evt->hookflags & afb_hook_flag_evt_addref)
		afb_hook_evt_addref(evt->fullname, evt->id);
	return afb_evt_addref(evt);
}
#endif

/*
 * decrement the reference count of the event 'evt'
 * and destroy it when the count reachs zero
 */
void afb_evt_unref(struct afb_evt *evt)
{
	struct afb_evt **prv, *oev;
	struct afb_evt_watch *watch, *nwatch;

	if (!__atomic_sub_fetch(&evt->refcount, 1, __ATOMIC_RELAXED)) {
		/* unlinks the event if valid! */
		x_rwlock_wrlock(&events_rwlock);
		prv = &evt_list_head;
		for(;;) {
			oev = *prv;
			if (oev == evt)
				break;
			if (!oev) {
				ERROR("unexpected event");
				x_rwlock_unlock(&events_rwlock);
				return;
			}
			prv = &oev->next;
		}
		event_count--;
		*prv = evt->next;
		x_rwlock_unlock(&events_rwlock);

		/* removes all watchers */
		x_rwlock_wrlock(&evt->rwlock);
		watch = evt->watchs;
		evt->watchs = NULL;
		x_rwlock_unlock(&evt->rwlock);
		while(watch) {
			nwatch = watch->next_by_evt;
			evt_unwatch(evt, watch->listener, watch, 1);
			watch = nwatch;
		}

		/* free */
		x_rwlock_destroy(&evt->rwlock);
		free(evt);
	}
}

#if WITH_AFB_HOOK
/*
 * decrement the reference count of the event 'evt'
 * and destroy it when the count reachs zero
 */
void afb_evt_hooked_unref(struct afb_evt *evt)
{
	if (evt->hookflags & afb_hook_flag_evt_unref)
		afb_hook_evt_unref(evt->fullname, evt->id);
	afb_evt_unref(evt);
}
#endif

/*
 * Returns the true name of the 'event'
 */
const char *afb_evt_fullname(struct afb_evt *evt)
{
	return evt->fullname;
}

/*
 * Returns the name of the 'event'
 */
const char *afb_evt_name(struct afb_evt *evt)
{
	const char *name = strchr(evt->fullname, '/');
	return name ? name + 1 : evt->fullname;
}

#if WITH_AFB_HOOK
/*
 * Returns the name associated to the event 'evt'.
 */
const char *afb_evt_hooked_name(struct afb_evt *evt)
{
	const char *result = afb_evt_name(evt);
	if (evt->hookflags & afb_hook_flag_evt_name)
		afb_hook_evt_name(evt->fullname, evt->id, result);
	return result;
}
#endif

/*
 * Returns the id of the 'event'
 */
uint16_t afb_evt_id(struct afb_evt *evt)
{
	return evt->id;
}

/*
 * Returns an instance of the listener defined by the 'send' callback
 * and the 'closure'.
 * Returns NULL in case of memory depletion.
 */
struct afb_evt_listener *afb_evt_listener_create(const struct afb_evt_itf *itf, void *closure)
{
	struct afb_evt_listener *listener;

	/* search if an instance already exists */
	x_rwlock_wrlock(&listeners_rwlock);
	listener = listeners;
	while (listener != NULL) {
		if (listener->itf == itf && listener->closure == closure) {
			listener = afb_evt_listener_addref(listener);
			goto found;
		}
		listener = listener->next;
	}

	/* allocates */
	listener = calloc(1, sizeof *listener);
	if (listener != NULL) {
		/* init */
		listener->itf = itf;
		listener->closure = closure;
		listener->watchs = NULL;
		listener->refcount = 1;
		x_rwlock_init(&listener->rwlock);
		listener->next = listeners;
		listeners = listener;
	}
 found:
	x_rwlock_unlock(&listeners_rwlock);
	return listener;
}

/*
 * Increases the reference count of 'listener' and returns it
 */
struct afb_evt_listener *afb_evt_listener_addref(struct afb_evt_listener *listener)
{
	__atomic_add_fetch(&listener->refcount, 1, __ATOMIC_RELAXED);
	return listener;
}

/*
 * Decreases the reference count of the 'listener' and destroys it
 * when no more used.
 */
void afb_evt_listener_unref(struct afb_evt_listener *listener)
{
	struct afb_evt_listener **prv, *olis;

	if (listener && !__atomic_sub_fetch(&listener->refcount, 1, __ATOMIC_RELAXED)) {

		/* unlink the listener */
		x_rwlock_wrlock(&listeners_rwlock);
		prv = &listeners;
		for(;;) {
			olis = *prv;
			if (olis == listener)
				break;
			if (!olis) {
				ERROR("unexpected listener");
				x_rwlock_unlock(&listeners_rwlock);
				return;
			}
			prv = &olis->next;
		}
		*prv = listener->next;
		x_rwlock_unlock(&listeners_rwlock);

		/* remove the watchers */
		afb_evt_listener_unwatch_all(listener, 0);

		/* free the listener */
		x_rwlock_destroy(&listener->rwlock);
		free(listener);
	}
}

/*
 * Makes the 'listener' watching 'evt'
 * Returns 0 in case of success or else -1.
 */
int afb_evt_listener_watch_evt(struct afb_evt_listener *listener, struct afb_evt *evt)
{
	struct afb_evt_watch *watch;

	/* check parameter */
	if (listener->itf->push == NULL)
		return X_EINVAL;

	/* search the existing watch for the listener */
	x_rwlock_wrlock(&listener->rwlock);
	watch = listener->watchs;
	while(watch != NULL) {
		if (watch->evt == evt)
			goto end;
		watch = watch->next_by_listener;
	}

	/* not found, allocate a new */
	watch = malloc(sizeof *watch);
	if (watch == NULL) {
		x_rwlock_unlock(&listener->rwlock);
		return X_ENOMEM;
	}

	/* initialise and link */
	watch->evt = evt;
	watch->listener = listener;
	watch->next_by_listener = listener->watchs;
	listener->watchs = watch;
	x_rwlock_wrlock(&evt->rwlock);
	watch->next_by_evt = evt->watchs;
	evt->watchs = watch;
	x_rwlock_unlock(&evt->rwlock);

	if (listener->itf->add != NULL)
		listener->itf->add(listener->closure, evt->fullname, evt->id);
end:
	x_rwlock_unlock(&listener->rwlock);

	return 0;
}

/*
 * Avoids the 'listener' to watch 'evt'
 * Returns 0 in case of success or else -1.
 */
int afb_evt_listener_unwatch_evt(struct afb_evt_listener *listener, struct afb_evt *evt)
{
	struct afb_evt_watch *watch, **pwatch;

	/* search the existing watch */
	x_rwlock_wrlock(&listener->rwlock);
	pwatch = &listener->watchs;
	for (;;) {
		watch = *pwatch;
		if (!watch) {
			x_rwlock_unlock(&listener->rwlock);
			errno = ENOENT;
			return -1;
		}
		if (evt == watch->evt) {
			*pwatch = watch->next_by_listener;
			x_rwlock_unlock(&listener->rwlock);
			listener_unwatch(listener, evt, watch, 1);
			return 0;
		}
		pwatch = &watch->next_by_listener;
	}
}

/*
 * Avoids the 'listener' to watch 'eventid'
 * Returns 0 in case of success or else -1.
 */
int afb_evt_listener_unwatch_id(struct afb_evt_listener *listener, uint16_t eventid)
{
	struct afb_evt_watch *watch, **pwatch;
	struct afb_evt *evt;

	/* search the existing watch */
	x_rwlock_wrlock(&listener->rwlock);
	pwatch = &listener->watchs;
	for (;;) {
		watch = *pwatch;
		if (!watch) {
			x_rwlock_unlock(&listener->rwlock);
			errno = ENOENT;
			return -1;
		}
		evt = watch->evt;
		if (evt->id == eventid) {
			*pwatch = watch->next_by_listener;
			x_rwlock_unlock(&listener->rwlock);
			listener_unwatch(listener, evt, watch, 1);
			return 0;
		}
		pwatch = &watch->next_by_listener;
	}
}

/*
 * Avoids the 'listener' to watch any event, calling the callback
 * 'remove' of the interface if 'remoe' is not zero.
 */
void afb_evt_listener_unwatch_all(struct afb_evt_listener *listener, int remove)
{
	struct afb_evt_watch *watch, *nwatch;

	/* search the existing watch */
	x_rwlock_wrlock(&listener->rwlock);
	watch = listener->watchs;
	listener->watchs = NULL;
	x_rwlock_unlock(&listener->rwlock);
	while(watch) {
		nwatch = watch->next_by_listener;
		listener_unwatch(listener, watch->evt, watch, remove);
		watch = nwatch;
	}
}

#if WITH_AFB_HOOK
/*
 * update the hooks for events
 */
void afb_evt_update_hooks()
{
	struct afb_evt *evt;

	x_rwlock_rdlock(&events_rwlock);
	for (evt = evt_list_head ; evt ; evt = evt->next) {
		evt->hookflags = afb_hook_flags_evt(evt->fullname);
		evt->eventx2.itf = evt->hookflags ? &afb_evt_hooked_event_x2_itf : &afb_evt_event_x2_itf;
	}
	x_rwlock_unlock(&events_rwlock);
}
#endif

inline struct afb_evt *afb_evt_of_x2(struct afb_event_x2 *eventx2)
{
	return (struct afb_evt*)eventx2;
}

inline struct afb_event_x2 *afb_evt_as_x2(struct afb_evt *evt)
{
	return &evt->eventx2;
}

