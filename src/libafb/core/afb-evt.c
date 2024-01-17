/*
 * Copyright (C) 2015-2024 IoT.bzh Company
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

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#include <afb/afb-event-x2-itf.h>
#include <rp-utils/rp-uuid.h>
#include <rp-utils/rp-verbose.h>

#include "core/afb-evt.h"
#include "core/afb-hook.h"
#include "core/afb-data.h"
#include "core/afb-data-array.h"
#include "core/afb-sched.h"
#include "sys/x-mutex.h"
#include "sys/x-rwlock.h"
#include "sys/x-errno.h"
#include "core/containerof.h"

#if !defined(AFB_EVT_NPARAMS_MAX)
#define AFB_EVT_NPARAMS_MAX	8
#endif
#if AFB_EVT_NPARAMS_MAX > 255
#undef AFB_EVT_NPARAMS_MAX
#define AFB_EVT_NPARAMS_MAX	255
#endif


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
	struct afb_event_x2 x2;

	/* next event */
	struct afb_evt *next;

	/* head of the list of listeners watching the event */
	struct afb_evt_watch *watchs;

	/* rwlock of the event */
	x_rwlock_t rwlock;

#if WITH_AFB_HOOK
	/* hooking */
	unsigned hookflags;
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

/* the interface for events */
static struct afb_event_x2_itf afb_evt_event_x2_itf;

/* head of the list of listeners */
static x_rwlock_t listeners_rwlock = X_RWLOCK_INITIALIZER;
static struct afb_evt_listener *listeners = NULL;

/* handling id of events */
static x_rwlock_t events_rwlock = X_RWLOCK_INITIALIZER;
static struct afb_evt *evt_list_head = NULL;
static uint16_t event_genid = 0;
static uint16_t event_count = 0;

/* job groups for events push/broadcast */
#define BROADCAST_JOB_GROUP  (&afb_evt_event_x2_itf)

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
	rp_uuid_binary_t uuids[EVENT_BROADCAST_MEMORY_COUNT];
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
static
struct afb_evt_broadcasted *
make_evt_broadcasted(
	const char *event,
	unsigned nparams,
	struct afb_data * const params[],
	const rp_uuid_binary_t uuid,
	uint8_t hop
) {
	size_t sz;
	struct afb_evt_broadcasted *jb;
	char *name;

	sz = 1 + strlen(event);
	jb = malloc(sizeof *jb + nparams * sizeof jb->data.params[0] + sz);
	if (jb) {
		jb->data.nparams = (uint16_t)nparams;
		afb_data_array_copy(nparams, params, jb->data.params);
		jb->hop = hop;
		memcpy(jb->uuid, uuid, sizeof jb->uuid);
		jb->data.name = name = (char*)&jb->data.params[nparams];
		memcpy(name, event, sz);
		jb->data.eventid = 0;
		return jb;
	}
	afb_data_array_unref(nparams, params);
	return 0;
}

/*
 * Destroy structure 'jb' for job of broadcasting string events
 */
static
void
destroy_evt_broadcasted(
	struct afb_evt_broadcasted *jb
) {
	afb_data_array_unref(jb->data.nparams, jb->data.params);
	free(jb);
}

/*
 * Create structure for job of broadcasting or pushing 'evt' with 'object'
 * Returns the created structure or NULL if out of memory
 */
static
struct afb_evt_pushed *
make_evt_pushed(
	struct afb_evt *evt,
	unsigned nparams,
	struct afb_data * const params[]
) {
	struct afb_evt_pushed *je;

	je = malloc(sizeof *je + nparams * sizeof je->data.params[0]);
	if (je) {
		je->evt = afb_evt_addref(evt);
		je->data.nparams = (uint16_t)nparams;
		afb_data_array_copy(nparams, params, je->data.params);
		je->data.name = evt->fullname;
		je->data.eventid = evt->id;
		return je;
	}
	afb_data_array_unref(nparams, params);
	return 0;
}

/*
 * Destroy structure for job of broadcasting or pushing evt
 */
static
void
destroy_evt_pushed(
	struct afb_evt_pushed *je
) {
	afb_evt_unref(je->evt);
	afb_data_array_unref(je->data.nparams, je->data.params);
	free(je);
}

/*
 * Broadcasts the 'event' of 'id' with its 'object'
 */
static void broadcast(struct afb_evt_broadcasted *jb, struct afb_evt_listener *listener)
{
	while (listener && listener->itf->broadcast == NULL)
		listener = listener->next;
	if (!listener)
		x_rwlock_unlock(&listeners_rwlock);
	else {
		void *closure = listener->closure;
		void (*callback)(void*, const struct afb_evt_broadcasted*) = listener->itf->broadcast;
		broadcast(jb, listener->next);
		callback(closure, jb);
	}
}

/*
 * Jobs callback for broadcasting string asynchronously
 */
static void broadcast_job(int signum, void *closure)
{
	struct afb_evt_broadcasted *jb = closure;

	if (signum == 0) {
		x_rwlock_rdlock(&listeners_rwlock);
		broadcast(jb, listeners);
	}
	destroy_evt_broadcasted(jb);
}

/*
 * Broadcasts the string 'event' with its 'object'
 */
static int broadcast_name(const char *event, unsigned nparams, struct afb_data * const params[], const rp_uuid_binary_t uuid, uint8_t hop)
{
	rp_uuid_binary_t local_uuid;
	struct afb_evt_broadcasted *jb;
	int rc;
#if EVENT_BROADCAST_MEMORY_COUNT
	int iter, count;
#endif

	/* check if lately sent */
	if (!uuid) {
		rp_uuid_new_binary(local_uuid);
		uuid = local_uuid;
		hop = EVENT_BROADCAST_HOP_MAX;
#if EVENT_BROADCAST_MEMORY_COUNT
		x_mutex_lock(&uniqueness.mutex);
	} else {
		x_mutex_lock(&uniqueness.mutex);
		iter = (int)uniqueness.base;
		count = (int)uniqueness.count;
		while (count) {
			if (0 == memcmp(uuid, uniqueness.uuids[iter], sizeof(rp_uuid_binary_t))) {
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
	memcpy(uniqueness.uuids[iter], uuid, sizeof(rp_uuid_binary_t));
	x_mutex_unlock(&uniqueness.mutex);
#else
	}
#endif

	/* create the structure for the job */
	jb = make_evt_broadcasted(event, nparams, params, uuid, hop);
	if (jb == NULL) {
		RP_ERROR("Can't create broadcast string job item for %s", event);
		return X_ENOMEM;
	}

	/* queue the job */
	rc = afb_sched_post_job(BROADCAST_JOB_GROUP, 0, 0, broadcast_job, jb, Afb_Sched_Mode_Normal);
	if (rc >= 0)
		rc = 0;
	else {
		RP_ERROR("Can't queue broadcast string job item for %s", event);
		destroy_evt_broadcasted(jb);
	}
	return rc;
}

/*
 * Broadcasts the event 'evt' with its 'object'
 * 'object' is released (like afb_dataset_unref)
 * Returns 0 on success or a negative error code
 */
int afb_evt_broadcast(struct afb_evt *evt, unsigned nparams, struct afb_data * const params[])
{
	return broadcast_name(evt->fullname, nparams, params, NULL, 0);
}

int afb_evt_rebroadcast_name(const char *event, unsigned nparams, struct afb_data * const params[], const rp_uuid_binary_t uuid, uint8_t hop)
{
	return broadcast_name(event, nparams, params, uuid, hop);
}

int afb_evt_rebroadcast_name_hookable(const char *event, unsigned nparams, struct afb_data * const params[], const rp_uuid_binary_t uuid, uint8_t hop)
#if !WITH_AFB_HOOK
	__attribute__((alias("afb_evt_rebroadcast_name")));
#else
{
	int result;

	afb_data_array_addref(nparams, params);
	afb_hook_evt_broadcast_before(event, 0, nparams, params);

	result = afb_evt_rebroadcast_name(event, nparams, params, uuid, hop);

	afb_hook_evt_broadcast_after(event, 0, nparams, params, result);
	afb_data_array_unref(nparams, params);

	return result;
}
#endif

/*
 * Broadcasts the 'event' with its 'object'
 * 'object' is released (like afb_dataset_unref)
 * Returns the count of listener having receive the event.
 */
int afb_evt_broadcast_name_hookable(const char *event, unsigned nparams, struct afb_data * const params[])
{
	return afb_evt_rebroadcast_name_hookable(event, nparams, params, NULL, 0);
}

/*
 * Pushes the event 'evt' with 'obj' to its listeners
 * Returns the count of listener that received the event.
 */
static void push_evt(struct afb_evt_watch *watch, struct afb_evt_pushed *je)
{
	if (!watch)
		x_rwlock_unlock(&je->evt->rwlock);
	else {
		void *closure = watch->listener->closure;
		void (*callback)(void*, const struct afb_evt_pushed*) = watch->listener->itf->push;
		assert(callback != NULL);
		push_evt(watch->next_by_evt, je);
		callback(closure, je);
	}
}

/*
 * Jobs callback for pushing evt asynchronously
 */
static void push_afb_evt_pushed(int signum, void *closure)
{
	struct afb_evt_pushed *je = closure;

	if (signum == 0) {
		x_rwlock_rdlock(&je->evt->rwlock);
		push_evt(je->evt->watchs, je);
	}
	destroy_evt_pushed(je);
}

/*
 * Pushes the event 'evt' with 'obj' to its listeners
 * 'obj' is released (like afb_dataset_unref)
 * Returns 1 if at least one listener exists or 0 if no listener exists or
 * -1 in case of error and the event can't be delivered
 */
int afb_evt_push(struct afb_evt *evt, unsigned nparams, struct afb_data * const params[])
{
	struct afb_evt_pushed *je;
	int rc;

	if (!evt->watchs) {
		afb_data_array_unref(nparams, params);
		return 0;
	}

	je = make_evt_pushed(evt, nparams, params);
	if (je == NULL) {
		RP_ERROR("Can't create push evt job item for %s", evt->fullname);
		return X_ENOMEM;
	}

	rc = afb_sched_post_job(evt, 0, 0, push_afb_evt_pushed, je, Afb_Sched_Mode_Normal);
	if (rc >= 0)
		rc = 1;
	else {
		RP_ERROR("Can't queue push evt job item for %s", evt->fullname);
		destroy_evt_pushed(je);
	}

	return rc;
}

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
 * Creates an event of name 'fullname'
 */
static int create_evt(struct afb_evt **evt, const char *fullname, size_t len)
{
	struct afb_evt *nevt, *oevt;
	uint16_t id;

	/* allocates the event */
	nevt = malloc(len + 1 + sizeof * nevt);
	if (nevt == NULL) {
		*evt = NULL;
		return X_ENOMEM;
	}

	memcpy(nevt->fullname, fullname, len + 1);
	nevt->refcount = 1;
	nevt->watchs = NULL;
	nevt->x2.itf = NULL;
#if WITH_AFB_HOOK
	nevt->hookflags = afb_hook_flags_evt(nevt->fullname);
#endif
	x_rwlock_init(&nevt->rwlock);

	/* allocates the id */
	x_rwlock_wrlock(&events_rwlock);
	if (event_count == UINT16_MAX) {
		x_rwlock_unlock(&events_rwlock);
		x_rwlock_destroy(&nevt->rwlock);
		free(nevt);
		RP_ERROR("Can't create more events");
		*evt = NULL;
		return X_ECANCELED;
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
	nevt->next = evt_list_head;
	nevt->id = id;
	evt_list_head = nevt;
	x_rwlock_unlock(&events_rwlock);

	/* returns the event */
#if WITH_AFB_HOOK
	if (nevt->hookflags & afb_hook_flag_evt_create)
		afb_hook_evt_create(nevt->fullname, nevt->id);
#endif
	*evt = nevt;
	return 0;
}

/*
 * Creates an event of name 'fullname'
 */
int afb_evt_create(struct afb_evt **evt, const char *fullname)
{
	return create_evt(evt, fullname, strlen(fullname));
}

/*
 * Creates an event of name 'prefix'/'name' and returns it or NULL on error.
 */
int afb_evt_create2(struct afb_evt **evt, const char *prefix, const char *name)
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
	return create_evt(evt, fullname, prelen + postlen + 1);
}

/*
 * increment the reference count of the event 'evt'
 */
struct afb_evt *afb_evt_addref(struct afb_evt *evt)
{
	__atomic_add_fetch(&evt->refcount, 1, __ATOMIC_RELAXED);
	return evt;
}

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
				RP_ERROR("unexpected event");
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

/*
 * Returns the id of the 'event'
 */
uint16_t afb_evt_id(struct afb_evt *evt)
{
	return evt->id;
}

/****************************************************************/
#if !WITH_AFB_HOOK
/****************************************************************/

struct afb_evt *afb_evt_addref_hookable(struct afb_evt *evt)
	__attribute__((alias("afb_evt_addref")));

void afb_evt_unref_hookable(struct afb_evt *evt)
	__attribute__((alias("afb_evt_unref")));

const char *afb_evt_name_hookable(struct afb_evt *evt)
	__attribute__((alias("afb_evt_name")));

int afb_evt_push_hookable(struct afb_evt *evt, unsigned nparams, struct afb_data * const params[])
	__attribute__((alias("afb_evt_push")));

int afb_evt_broadcast_hookable(struct afb_evt *evt, unsigned nparams, struct afb_data * const params[])
	__attribute__((alias("afb_evt_broadcast")));


/****************************************************************/
#else
/****************************************************************/

/*
 * increment the reference count of the event 'evt'
 */
struct afb_evt *afb_evt_addref_hookable(struct afb_evt *evt)
{
	if (evt->hookflags & afb_hook_flag_evt_addref)
		afb_hook_evt_addref(evt->fullname, evt->id);

	return afb_evt_addref(evt);
}

/*
 * decrement the reference count of the event 'evt'
 * and destroy it when the count reachs zero
 */
void afb_evt_unref_hookable(struct afb_evt *evt)
{
	if (evt->hookflags & afb_hook_flag_evt_unref)
		afb_hook_evt_unref(evt->fullname, evt->id);

	afb_evt_unref(evt);
}

/*
 * Returns the name associated to the event 'evt'.
 */
const char *afb_evt_name_hookable(struct afb_evt *evt)
{
	const char *result = afb_evt_name(evt);
	if (evt->hookflags & afb_hook_flag_evt_name)
		afb_hook_evt_name(evt->fullname, evt->id, result);
	return result;
}

/*
 * Pushes the event 'evt' with 'obj' to its listeners
 * 'obj' is released (like afb_dataset_unref)
 * Emits calls to hooks.
 * Returns the count of listener taht received the event.
 */
int afb_evt_push_hookable(struct afb_evt *evt, unsigned nparams, struct afb_data * const params[])
{
	int result;

	/* lease the parameters */
	if (evt->hookflags & afb_hook_flag_evt_push_after) {
		afb_data_array_addref(nparams, params);
	}

	/* hook before push */
	if (evt->hookflags & afb_hook_flag_evt_push_before) {
		afb_hook_evt_push_before(evt->fullname, evt->id, nparams, params);
	}

	/* push */
	result = afb_evt_push(evt, nparams, params);

	/* hook after push */
	if (evt->hookflags & afb_hook_flag_evt_push_after) {
		afb_hook_evt_push_after(evt->fullname, evt->id,  nparams, params, result);
		afb_data_array_unref(nparams, params);
	}

	return result;
}

/*
 * Broadcasts the event 'evt' with its 'object'
 * 'object' is released (like afb_dataset_unref)
 * Returns the count of listener that received the event.
 */
int afb_evt_broadcast_hookable(struct afb_evt *evt, unsigned nparams, struct afb_data * const params[])
{
	int result;

	/* lease the parameters if needed */
	if (evt->hookflags & afb_hook_flag_evt_broadcast_after) {
		afb_data_array_addref(nparams, params);
	}

	/* hook before broadcast */
	if (evt->hookflags & afb_hook_flag_evt_broadcast_before)
		afb_hook_evt_broadcast_before(evt->fullname, evt->id, nparams, params);

	/* broadcast */
	result = afb_evt_broadcast(evt, nparams, params);

	/* hook after broadcast */
	if (evt->hookflags & afb_hook_flag_evt_broadcast_after) {
		afb_hook_evt_broadcast_after(evt->fullname, evt->id, nparams, params, result);
		afb_data_array_unref(nparams, params);
	}

	return result;
}
/****************************************************************/
#endif
/****************************************************************/

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
				RP_ERROR("unexpected listener");
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
			return X_ENOENT;
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
			return X_ENOENT;
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
 * 'remove' of the interface if 'remove' is not zero.
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

/**************************************************************************/
/**************************************************************************/
/*************    X2                                    *******************/
/**************************************************************************/
/**************************************************************************/

#include <json-c/json.h>
#include "core/afb-json-legacy.h"

inline struct afb_evt *afb_evt_of_x2(struct afb_event_x2 *evtx2)
{
	return containerof(struct afb_evt, x2, evtx2);
}

inline struct afb_event_x2 *afb_evt_as_x2(struct afb_evt *evt)
{
	return &evt->x2;
}

static struct afb_event_x2 *x2_event_addref(struct afb_event_x2 *evtx2)
{
	return afb_evt_as_x2(afb_evt_addref_hookable(afb_evt_of_x2(evtx2)));
}

static void x2_event_unref(struct afb_event_x2 *evtx2)
{
	afb_evt_unref_hookable(afb_evt_of_x2(evtx2));
}

static const char *x2_event_name(struct afb_event_x2 *evtx2)
{
	return afb_evt_name_hookable(afb_evt_of_x2(evtx2));
}


static int x2_event_push(struct afb_event_x2 *evtx2, struct json_object *obj)
{
	return afb_json_legacy_event_push_hookable(afb_evt_of_x2(evtx2), obj);
}


static int x2_event_broadcast(struct afb_event_x2 *evtx2, struct json_object *obj)
{
	return afb_json_legacy_event_broadcast_hookable(afb_evt_of_x2(evtx2), obj);
}

/* the interface for events */
static struct afb_event_x2_itf afb_evt_event_x2_itf = {
	.broadcast = x2_event_broadcast,
	.push = x2_event_push,
	.unref = x2_event_unref,
	.name = x2_event_name,
	.addref = x2_event_addref
};

inline struct afb_event_x2 *afb_evt_make_x2(struct afb_evt *evt)
{
	evt->x2.itf = &afb_evt_event_x2_itf;
	return &evt->x2;
}

/**************************************************************************/

#if WITH_AFB_HOOK
/*
 * update the hooks for events
 */
void afb_evt_update_hooks()
{
	struct afb_evt *evt;

	x_rwlock_rdlock(&events_rwlock);
	for (evt = evt_list_head ; evt ; evt = evt->next)
		evt->hookflags = afb_hook_flags_evt(evt->fullname);
	x_rwlock_unlock(&events_rwlock);
}
#endif
