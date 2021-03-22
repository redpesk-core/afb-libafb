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

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>

#include "core/afb-session.h"
#include "core/afb-hook.h"
#include "sys/verbose.h"
#include "utils/pearson.h"
#include "utils/uuid.h"
#include "sys/x-mutex.h"
#include "sys/x-errno.h"

#define SESSION_COUNT_MIN 5
#define SESSION_COUNT_MAX 1000

/*
 * Handling of  cookies.
 * Cookies are stored by session.
 * The cookie count COOKIECOUNT must be a power of 2, possible values: 1, 2, 4, 8, 16, 32, 64, ...
 * For low memory profile, small values are better, 1 is possible.
 */
#define COOKIECOUNT	8
#define COOKIEMASK	(COOKIECOUNT - 1)

#define _MAXEXP_	((time_t)(~(time_t)0))
#define _MAXEXP2_	((time_t)((((unsigned long long)_MAXEXP_) >> 1)))
#define MAX_EXPIRATION	(_MAXEXP_ >= 0 ? _MAXEXP_ : _MAXEXP2_)
#define NOW		(time_now())

#define MASKLOA         7
#define COOKIESET       8

/**
 * structure for a cookie added to sessions
 */
struct cookie
{
	struct cookie *next;	/**< link to next cookie */
	const void *key;	/**< pointer key */
	void *value;		/**< value */
	void (*freecb)(void*);	/**< function to call when session is closed */
	int loa;                /**< loa for the key */
};

/**
 * structure for session
 */
struct afb_session
{
	struct afb_session *next; /**< link to the next */
	uint16_t refcount;      /**< count of reference to the session */
	uint16_t id;		/**< local id of the session */
	int timeout;            /**< timeout of the session */
	time_t expiration;	/**< expiration time of the session */
	x_mutex_t mutex;	/**< mutex of the session */
	struct cookie *cookies[COOKIECOUNT]; /**< cookies of the session */
	char *lang;		/**< current language setting for the session */
	uint8_t closed: 1;      /**< is the session closed ? */
	uint8_t autoclose: 1;   /**< close the session when unreferenced */
	uint8_t notinset: 1;	/**< session removed from the set of sessions */
	uint8_t hash;		/**< hash value of the uuid */
	uuid_stringz_t uuid;	/**< identification of client session */
};

/**
 * structure for managing sessions
 */
static struct {
	uint16_t count;		/**< current number of sessions */
	uint16_t max;		/**< maximum count of sessions */
	uint16_t genid;		/**< for generating ids */
	int timeout;		/**< common initial timeout */
	struct afb_session *first; /**< sessions */
	x_mutex_t mutex;	/**< declare a mutex to protect hash table */
} sessions = {
	.count = 0,
	.max = 10,
	.genid = 1,
	.timeout = 3600,
	.first = 0,
	.mutex = X_MUTEX_INITIALIZER
};

/**
 * Get the actual raw time
 */
static inline time_t time_now()
{
#if WITH_CLOCK_GETTIME
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
	return ts.tv_sec;
#else
	return time(NULL);
#endif
}

/* lock the set of sessions for exclusive access */
static inline void sessionset_lock()
{
	x_mutex_lock(&sessions.mutex);
}

/* unlock the set of sessions of exclusive access */
static inline void sessionset_unlock()
{
	x_mutex_unlock(&sessions.mutex);
}

/*
 * search within the set of sessions the session of 'uuid'.
 * 'hashidx' is the precomputed hash for 'uuid'
 * return the session or NULL
 */
static struct afb_session *sessionset_search(const char *uuid, uint8_t hashidx)
{
	struct afb_session *session;

	session = sessions.first;
	while (session && (hashidx != session->hash || strcmp(uuid, session->uuid)))
		session = session->next;

	return session;
}

/*
 * search within the set of sessions the session of 'id'.
 * return the session or NULL
 */
static struct afb_session *sessionset_search_id(uint16_t id)
{
	struct afb_session *session;

	session = sessions.first;
	while (session && id != session->id)
		session = session->next;

	return session;
}

/* add 'session' to the set of sessions */
static int sessionset_add(struct afb_session *session, uint8_t hashidx)
{
	/* check availability */
	if (sessions.max && sessions.count >= sessions.max)
		return X_EBUSY;

	/* add the session */
	session->hash = hashidx;
	session->next = sessions.first;
	sessions.first = session;
	sessions.count++;
	return 0;
}

/* make a new uuid not used in the set of sessions */
static uint8_t sessionset_make_uuid (uuid_stringz_t uuid)
{
	uint8_t hashidx;

	do {
		uuid_new_stringz(uuid);
		hashidx = pearson4(uuid);
	} while(sessionset_search(uuid, hashidx));
	return hashidx;
}

/* lock the 'session' for exclusive access */
static inline void session_lock(struct afb_session *session)
{
	x_mutex_lock(&session->mutex);
}

/* unlock the 'session' of exclusive access */
static inline void session_unlock(struct afb_session *session)
{
	x_mutex_unlock(&session->mutex);
}

/* close the 'session' */
static void session_close(struct afb_session *session)
{
	int idx;
	struct cookie *cookie;

	/* close only one time */
	if (!session->closed) {
		/* close it now */
		session->closed = 1;

#if WITH_AFB_HOOK
		/* emit the hook */
		afb_hook_session_close(session);
#endif

		/* release cookies */
		for (idx = 0 ; idx < COOKIECOUNT ; idx++) {
			while ((cookie = session->cookies[idx])) {
				session->cookies[idx] = cookie->next;
				if (cookie->freecb != NULL)
					cookie->freecb(cookie->value);
				free(cookie);
			}
		}
	}
}

/* destroy the 'session' */
static void session_destroy (struct afb_session *session)
{
#if WITH_AFB_HOOK
	afb_hook_session_destroy(session);
#endif
	x_mutex_destroy(&session->mutex);
	free(session->lang);
	free(session);
}

/* update expiration of 'session' according to 'now' */
static void session_update_expiration(struct afb_session *session, time_t now)
{
	time_t expiration;

	/* compute expiration */
	expiration = now + afb_session_timeout(session);
	if (expiration < 0)
		expiration = MAX_EXPIRATION;

	/* record the expiration */
	session->expiration = expiration;
}

/*
 * Add a new session with the 'uuid' (of 'hashidx')
 * and the 'timeout' starting from 'now'.
 * Add it to the set of sessions
 * Return the created session
 */
static int session_add(struct afb_session **res, const char *uuid, int timeout, time_t now, uint8_t hashidx)
{
	struct afb_session *session;
	int rc;

	/* check arguments */
	if (!AFB_SESSION_TIMEOUT_IS_VALID(timeout)
	 || (uuid && strlen(uuid) >= sizeof session->uuid)) {
		*res = NULL;
		return X_EINVAL;
	}

	/* allocates a new one */
	session = calloc(1, sizeof *session);
	if (session == NULL) {
		*res = NULL;
		return X_ENOMEM;
	}

	/* initialize */
	x_mutex_init(&session->mutex);
	session->refcount = 1;
	strcpy(session->uuid, uuid);
	session->timeout = timeout;
	session_update_expiration(session, now);
	session->id = ++sessions.genid;
	while (session->id == 0 || sessionset_search_id(session->id) != NULL)
		session->id = ++sessions.genid;

	/* add */
	rc = sessionset_add(session, hashidx);
	if (rc < 0) {
		free(session);
		*res = NULL;
		return rc;
	}

#if WITH_AFB_HOOK
	afb_hook_session_create(session);
#endif

	*res = session;
	return 0;
}

/* Remove expired sessions and return current time (now) */
static time_t sessionset_cleanup (int force)
{
	struct afb_session *session, **prv;
	time_t now;

	/* Loop on Sessions Table and remove anything that is older than timeout */
	now = NOW;
	prv = &sessions.first;
	while ((session = *prv)) {
		session_lock(session);
		if (force || session->expiration < now)
			session_close(session);
		if (!session->closed) {
			prv = &session->next;
			session_unlock(session);
		} else {
			*prv = session->next;
			sessions.count--;
			session->notinset = 1;
			if (session->refcount)
				session_unlock(session);
			else
				session_destroy(session);
		}
	}
	return now;
}

/* Initialize the session manager */
int afb_session_init (int max_session_count, int timeout)
{
	/* init the sessionset (after cleanup) */
	sessionset_lock();
	sessionset_cleanup(1);
	if (max_session_count > SESSION_COUNT_MAX)
		sessions.max = SESSION_COUNT_MAX;
	else if (max_session_count < SESSION_COUNT_MIN)
		sessions.max = SESSION_COUNT_MIN;
	else
		sessions.max = (uint16_t)max_session_count;
	sessions.timeout = timeout;
	sessionset_unlock();
	return 0;
}

/* Iterate the sessions */
void afb_session_foreach(void (*callback)(void *closure, struct afb_session *session), void *closure)
{
	struct afb_session *session;

	/* Loop on Sessions Table and remove anything that is older than timeout */
	sessionset_lock();
	session = sessions.first;
	while (session) {
		if (!session->closed)
			callback(closure, session);
		session = session->next;
	}
	sessionset_unlock();
}

/* Cleanup the sessionset of its closed or expired sessions */
void afb_session_purge()
{
	sessionset_lock();
	sessionset_cleanup(0);
	sessionset_unlock();
}

/* Searchs the session of 'uuid' */
struct afb_session *afb_session_search (const char *uuid)
{
	struct afb_session *session;

	sessionset_lock();
	sessionset_cleanup(0);
	session = sessionset_search(uuid, pearson4(uuid));
	session = afb_session_addref(session);
	sessionset_unlock();
	return session;

}

/* Creates a new session with 'timeout' */
int afb_session_create (struct afb_session **session, int timeout)
{
	return afb_session_get(session, NULL, timeout, NULL);
}

/* Returns the timeout of 'session' in seconds */
int afb_session_timeout(struct afb_session *session)
{
	int timeout;

	/* compute timeout */
	timeout = session->timeout;
	if (timeout == AFB_SESSION_TIMEOUT_DEFAULT)
		timeout = sessions.timeout;
	if (timeout < 0)
		timeout = INT_MAX;
	return timeout;
}

/* Set the timeout of 'session' in seconds */
int afb_session_set_timeout(struct afb_session *session, int timeout)
{
	if (AFB_SESSION_TIMEOUT_IS_VALID(timeout))
		return X_EINVAL;

	session->timeout = timeout;
	return 0;
}

/* update the expiration of the session */
void afb_session_touch(struct afb_session *session)
{
	session_update_expiration(session, NOW);
}

/* Returns the second remaining before expiration of 'session' */
int afb_session_what_remains(struct afb_session *session)
{
	int diff = (int)(session->expiration - NOW);
	return diff < 0 ? 0 : diff;
}

/* This function will return exiting session or newly created session */
int afb_session_get (struct afb_session **psession, const char *uuid, int timeout, int *created)
{
	uuid_stringz_t _uuid_;
	uint8_t hashidx;
	struct afb_session *session;
	time_t now;
	int c, rc;

	/* cleaning */
	sessionset_lock();
	now = sessionset_cleanup(0);

	/* search for an existing one not too old */
	c = 1;
	if (!uuid) {
		hashidx = sessionset_make_uuid(_uuid_);
		uuid = _uuid_;
	} else {
		hashidx = pearson4(uuid);
		session = sessionset_search(uuid, hashidx);
		if (session) {
			/* session found */
			afb_session_addref(session);
			rc = c = 0;
		}
	}

	/* create the session if needed */
	if (c) {
		rc = session_add(&session, uuid, timeout, now, hashidx);
		if (rc < 0)
			c = 0;
	}

	sessionset_unlock();
	if (created)
		*created = c;
	*psession = session;
	return rc;
}

/* increase the use count on 'session' (can be NULL) */
struct afb_session *afb_session_addref(struct afb_session *session)
{
	if (session != NULL) {
#if WITH_AFB_HOOK
		afb_hook_session_addref(session);
#endif
		session_lock(session);
		session->refcount++;
		session_unlock(session);
	}
	return session;
}

/* decrease the use count of 'session' (can be NULL) */
void afb_session_unref(struct afb_session *session)
{
	if (session == NULL)
		return;

#if WITH_AFB_HOOK
	afb_hook_session_unref(session);
#endif
	session_lock(session);
	if (!--session->refcount) {
		if (session->autoclose)
			session_close(session);
		if (session->notinset) {
			session_destroy(session);
			return;
		}
	}
	session_unlock(session);
}

/* close 'session' */
void afb_session_close (struct afb_session *session)
{
	session_lock(session);
	session_close(session);
	session_unlock(session);
}

/* Set the 'autoclose' flag of the 'session' */
void afb_session_set_autoclose(struct afb_session *session, int autoclose)
{
	session->autoclose = !!autoclose;
}

/* is 'session' closed? */
int afb_session_is_closed (struct afb_session *session)
{
	return session->closed;
}

/* Returns the uuid of 'session' */
const char *afb_session_uuid (struct afb_session *session)
{
	return session->uuid;
}

/* Returns the local id of 'session' */
uint16_t afb_session_id (struct afb_session *session)
{
	return session->id;
}

/**
 * Get the index of the 'key' in the cookies array.
 * @param key the key to scan
 * @return the index of the list for key within cookies
 */
#if COOKIEMASK
static int cookeyidx(const void *key)
{
	intptr_t x = (intptr_t)key;
	unsigned r = (unsigned)((x >> 5) ^ (x >> 15));
	return r & COOKIEMASK;
}
#else
#  define cookeyidx(key) 0
#endif

/**
 * Get the cookie structure for the given key. Create it if needed.
 *
 * @param session the session (should be locked)
 * @param key     the key of the cookie
 * @param create  create if needed (boolean)
 * @param result  where to store the found cookie
 * @param pprv    where to store pointer to the cookie
 *
 * @return 0 if found, 1 if found but created, X_ENOMEM if not able to create
 */
static int getcookie(struct afb_session *session, const void *key, int create, struct cookie **result, struct cookie ***pprv)
{
	int rc;
	int idx;
	struct cookie *cookie, **prv;

	/* get key hashed index */
	idx = cookeyidx(key);
	prv = &session->cookies[idx];
	for (;;) {
		cookie = *prv;
		if (!cookie) {
			if (!create) {
				rc = X_ENOENT;
			}
			else {
				/* 'key' not found, create it */
				cookie = malloc(sizeof *cookie);
				if (!cookie)
					rc = X_ENOMEM;
				else {
					cookie->key = key;
					cookie->value = NULL;
					cookie->freecb = NULL;
					cookie->next = NULL;
					cookie->loa = 0;
					*prv = cookie;
					rc = 1;
				}
			}
			break;
		} else if (cookie->key == key) {
			/* cookie of key found */
			rc = 0;
			break;
		} else {
			prv = &(cookie->next);
		}
	}
	*result = cookie;
	*pprv = prv;
	return rc;
}

/**
 * Check that the cookie is needed. If not, remove it.
 *
 * @param cookie  the cookie to check
 * @param prv     pointer to pointer to it
 */
static void checkcookie(struct cookie *cookie, struct cookie **prv)
{
	if (cookie->loa == 0) {
		*prv = cookie->next;
		free(cookie);
	}
}

/* Set, get, replace, remove a cookie of 'key' for the 'session' */
int afb_session_cookie(
	struct afb_session *session,
	const void *key,
	void **cookieval,
	void *(*makecb)(void *closure),
	void (*freecb)(void *item),
	void *closure,
	enum afb_session_cookie_operator operation
) {
	int rc, mk;
	void *value;
	struct cookie *cookie, **prv;

	/* lock session */
	session_lock(session);

	/* should create? */
	switch(operation) {
	case Afb_Session_Cookie_Init:
	case Afb_Session_Cookie_Set:
		mk = 1;
		break;
	case Afb_Session_Cookie_Get:
	case Afb_Session_Cookie_Delete:
	case Afb_Session_Cookie_Exists:
	default:
		mk = 0;
		break;
	}

	/* search for the cookie of 'key' and create it if needed */
	rc = getcookie(session, key, mk, &cookie, &prv);
	if (rc < 0) {
		/* no cookie found or creation impossible */
		value = NULL;
	}
	else if (rc) {
		/* created new cookie for the key */
		value = makecb ? makecb(closure) : closure;
		cookie->value = value;
		cookie->freecb = freecb;
		cookie->loa |= COOKIESET;
	}
	else {
		/* got existing cookie for the key */
		switch(operation) {
		case Afb_Session_Cookie_Init:
			if (cookie->loa & COOKIESET) {
				value = cookie->value;
			}
			else {
				/* create value using 'closure' and 'makecb' */
				value = makecb ? makecb(closure) : closure;

				/* store the new value */
				cookie->value = value;
				cookie->freecb = freecb;
				cookie->loa |= COOKIESET;
			}
			break;
		case Afb_Session_Cookie_Set:
			if (cookie->loa & COOKIESET) {
				/* free previous value is needed */
				if (cookie->freecb)
					cookie->freecb(cookie->value);
			}
			else {
				cookie->loa |= COOKIESET;
			}

			/* create value using 'closure' and 'makecb' */
			value = makecb ? makecb(closure) : closure;
			cookie->value = value;
			cookie->freecb = freecb;
			break;
		case Afb_Session_Cookie_Get:
			value = cookie->loa & COOKIESET ? cookie->value : NULL;
			break;
		case Afb_Session_Cookie_Delete:
			if (cookie->loa & COOKIESET) {
				/* free previous value is needed */
				if (cookie->freecb)
					cookie->freecb(cookie->value);
				cookie->loa &= MASKLOA;
			}
			checkcookie(cookie, prv);
			value = NULL;
			break;
		case Afb_Session_Cookie_Exists:
			value = (cookie->loa & COOKIESET) ? (void*)(intptr_t)1 : NULL;
			break;
		default:
			value = NULL;
			rc = X_EINVAL;
			break;
		}
	}

	/* unlock the session and return the value */
	session_unlock(session);
	if (cookieval)
		*cookieval = value;
	return rc;
}

/* Get the cookie of 'key' in the 'session' */
int afb_session_get_cookie(struct afb_session *session, const void *key, void **cookie)
{
	return afb_session_cookie(session, key, cookie, NULL, NULL, NULL, Afb_Session_Cookie_Get);
}

/* Set the cookie of 'key' */
int afb_session_set_cookie(struct afb_session *session, const void *key, void *value, void (*freecb)(void*))
{
	return afb_session_cookie(session, key, NULL, NULL, freecb, value, Afb_Session_Cookie_Set);
}

/* Set the language attached to the session */
int afb_session_set_language(struct afb_session *session, const char *lang)
{
	char *oldl, *newl;

	newl = strdup(lang);
	if (newl == NULL)
		return -1;

	oldl = session->lang;
	session->lang = newl;
	free(oldl);
	return 0;
}

/* Get the language attached to the session */
const char *afb_session_get_language(struct afb_session *session, const char *lang)
{
	return session->lang ?: lang;
}

/* Get the LOA value associated to session for the key */
int afb_session_get_loa(struct afb_session *session, const void *key)
{
	int rc;
	struct cookie *cookie, **prv;

	/* lock session */
	session_lock(session);

	/* search for the cookie of 'key' */
	rc = getcookie(session, key, 0, &cookie, &prv);
	if (rc < 0) {
		rc = 0;
	}
	else {
		rc = cookie->loa & MASKLOA;
	}

	/* unlock the session and return the value */
	session_unlock(session);
	return rc;
}

/* Set the LOA value associated to session for the key */
int afb_session_set_loa(struct afb_session *session, const void *key, int loa)
{
	int rc;
	struct cookie *cookie, **prv;

	if (loa != (loa & MASKLOA))
		return X_EINVAL;

	/* lock session */
	session_lock(session);

	/* search for the cookie of 'key' */
	rc = getcookie(session, key, loa != 0, &cookie, &prv);
	if (rc < 0) {
		if (loa == 0)
			rc = 0;
	}
	else {
		rc = loa;
		cookie->loa = (cookie->loa & COOKIESET) | loa;
		checkcookie(cookie, prv);
	}

	/* unlock the session and return the value */
	session_unlock(session);
	return rc;
}

/* drop loa and cookie of the given key */
void afb_session_drop_key(struct afb_session *session, const void *key)
{
	struct cookie *cookie, **prv;

	/* lock session */
	session_lock(session);

	/* search for the cookie of 'key' */
	if (getcookie(session, key, 0, &cookie, &prv) >= 0) {
		/* unlink it */
		*prv = cookie->next;
		/* free value is needed */
		if (cookie->freecb)
			cookie->freecb(cookie->value);
		free(cookie);
	}

	/* unlock the session and return the value */
	session_unlock(session);
}
