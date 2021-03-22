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

#pragma once

#include <stdint.h>

struct afb_session;

#define AFB_SESSION_TIMEOUT_INFINITE  -1
#define AFB_SESSION_TIMEOUT_DEFAULT   -2
#define AFB_SESSION_TIMEOUT_IS_VALID(x) ((x) >= AFB_SESSION_TIMEOUT_DEFAULT)

enum afb_session_cookie_operator
{
	Afb_Session_Cookie_Init,
	Afb_Session_Cookie_Set,
	Afb_Session_Cookie_Get,
	Afb_Session_Cookie_Delete,
	Afb_Session_Cookie_Exists
};

/**
 * Initialize the session manager with a 'max_session_count',
 * an initial common 'timeout'
 *
 * @param max_session_count  maximum allowed session count in the same time
 * @param timeout            the initial default timeout of sessions
 */
extern int afb_session_init(int max_session_count, int timeout);

/**
 * Cleanup the sessions, compute closed and expired sessions
 */
extern void afb_session_purge();

/**
 * Iterate the sessions and call 'callback' with
 * the 'closure' for each session.
 *
 * @param callback function to call for each session
 * @param closure the closure of the callback
 */
extern void afb_session_foreach(void (*callback)(void *closure, struct afb_session *session), void *closure);

/**
 * Creates a new session with 'timeout'
 *
 * @param session for storing created session
 * @param timeout timeout for the session in seconds
 *
 * @return 0 on success or a negative error code
 */
extern int afb_session_create (struct afb_session **session, int timeout);

/**
 * Get an exiting session or created  a new one
 *
 * @param session where to store the session
 * @param uuid the uuid to search or NULL for creation
 * @param timeout the timeout when creating the session
 * @param created for storing the creation status (1 created, 0 not created)
 *
 * @return 0 in case of success or a negative value on error
 */
extern int afb_session_get (struct afb_session **session, const char *uuid, int timeout, int *created);

/**
 * Searchs the session of 'uuid'. Return it with reference count incremented.
 *
 * @param uuid the uuid whose session is to retrieve
 *
 * @return the found session with reference count incremented or NULL if not found
 */
extern struct afb_session *afb_session_search (const char *uuid);

/**
 * increase the use count on 'session' (can be NULL)
 *
 * @param session the session
 *
 * @return the session
 */
extern struct afb_session *afb_session_addref(struct afb_session *session);

/**
 * decrease the use count on 'session' (can be NULL)
 *
 * @param session the session
 */
extern void afb_session_unref(struct afb_session *session);

/**
 * Returns the uuid of 'session'
 *
 * @param session the session
 *
 * @return the uuid of session
 */
extern const char *afb_session_uuid (struct afb_session *session);

/**
 * Returns the local id of 'session'
 *
 * @param session the session
 *
 * @return the local id
 */
extern uint16_t afb_session_id (struct afb_session *session);

/**
 * Set the 'autoclose' flag of the 'session'
 *
 * A session whose autoclose flag is true will close as
 * soon as it is no more referenced.
 *
 * @param session    the session to set
 * @param autoclose  the value to set
 */
extern void afb_session_set_autoclose(struct afb_session *session, int autoclose);

/**
 * Close the 'session'
 *
 * @param session the session
 */
extern void afb_session_close(struct afb_session *session);

/**
 * Check if 'session' is closed
 *
 * @param session the session
 *
 * @return 1 if closed or 0 if live
 */
extern int afb_session_is_closed (struct afb_session *session);

/**
 * Returns the timeout of 'session' in seconds
 *
 * @param session the session
 *
 * @return the timeout in seconds
 */
extern int afb_session_timeout(struct afb_session *session);

/**
 * Returns the second remaining before expiration of 'session'
 *
 * @param session the session
 *
 * @return remaining time in seconds
 */
extern int afb_session_what_remains(struct afb_session *session);

/**
 * Update the expiration of the session from now
 *
 * @param session the session
 */
extern void afb_session_touch(struct afb_session *session);

/**
 * Set the timeout of 'session' in seconds.
 * Doesn't update the expiration.
 *
 * @param session the session
 * @param timeout the timeout
 *
 * @return 0 in case of success or X_EINVAL if timeout is wrong
 */
extern int afb_session_set_timeout(struct afb_session *session, int timeout);

/**
 * Set the language attached to the session
 *
 * @param session the session to set
 * @param lang    the language specifiction to set to session
 *
 * @return 0 in case of success or -1 in case of error
 */
extern int afb_session_set_language(struct afb_session *session, const char *lang);

/**
 * Get the language attached to the session
 *
 * @param session the session to query
 * @param lang    a default language specifiction
 *
 * @return the language specification to use for session
 */
extern const char *afb_session_get_language(struct afb_session *session, const char *lang);

/**
 * Set, get, replace, remove a cookie of 'key' for the 'session'
 *
 * The behaviour of this function depends on its parameters:
 *
 * @param session	the session
 * @param key		the key of the cookie
 * @param cookieval     where to store the cookie value
 * @param makecb	the creation function or NULL
 * @param freecb	the release function or NULL
 * @param closure	an argument for makecb or the value if makecb==NULL
 * @param operation	operation to perform
 *
 * @return 0 if cookie existed, 1 if created, a negative number on error
 *
 * The 'key' is a pointer and compared as pointers.
 *
 * For getting the current value of the cookie:
 *
 *   afb_session_cookie(session, key, &value, NULL, NULL, NULL, 0)
 *
 * For storing the value of the cookie
 *
 *   afb_session_cookie(session, key, NULL, NULL, NULL, value, 1)
 */
extern int afb_session_cookie(struct afb_session *session, const void *key, void **cookie, void *(*makecb)(void *closure), void (*freecb)(void *item), void *closure, enum afb_session_cookie_operator oper);

/**
 * Get the cookie of 'key' in the 'session'.
 *
 * @param session  the session to search in
 * @param key      the key of the data to retrieve
 *
 * @return the data staored for the key or NULL if the key isn't found
 */
extern int afb_session_get_cookie(struct afb_session *session, const void *key, void **cookie);

/**
 * Set the cookie of 'key' in the 'session' to the 'value' that can be
 * cleaned using 'freecb' (if not null).
 *
 * @param session  the session to set
 * @param key      the key of the data to store
 * @param value    the value to store at key
 * @param freecb   a function to use when the cookie value is to remove (or null)
 *
 * @return 0 in case of success or -1 in case of error
 */
extern int afb_session_set_cookie(struct afb_session *session, const void *key, void *value, void (*freecb)(void*));

/**
 * Get the LOA value associated to session for the key
 *
 * @param session	the session
 * @param key		the key of the cookie
 *
 * @return the loa value for the key
 */
extern int afb_session_get_loa(struct afb_session *session, const void *key);

/**
 * Set the LOA value associated to session for the key
 *
 * @param session	the session
 * @param key		the key of the cookie
 * @param loa           the loa to set
 *
 * @return the loa value for the key or a negative number if error
 */
extern int afb_session_set_loa(struct afb_session *session, const void *key, int loa);

/**
 * drop loa and cookie of the given key
 *
 * @param session	the session
 * @param key		the key of the cookie
 */
extern void afb_session_drop_key(struct afb_session *session, const void *key);

