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


extern int afb_session_init(int max_session_count, int timeout);
extern void afb_session_purge();
extern void afb_session_foreach(void (*callback)(void *closure, struct afb_session *session), void *closure);

extern int afb_session_create (struct afb_session **session, int timeout);
extern int afb_session_get (struct afb_session **session, const char *uuid, int timeout, int *created);
extern struct afb_session *afb_session_search (const char *uuid);
extern const char *afb_session_uuid (struct afb_session *session);
extern uint16_t afb_session_id (struct afb_session *session);

extern struct afb_session *afb_session_addref(struct afb_session *session);
extern void afb_session_unref(struct afb_session *session);
extern void afb_session_set_autoclose(struct afb_session *session, int autoclose);

extern void afb_session_close(struct afb_session *session);
extern int afb_session_is_closed (struct afb_session *session);

extern int afb_session_timeout(struct afb_session *session);
extern int afb_session_what_remains(struct afb_session *session);

extern int afb_session_set_language(struct afb_session *session, const char *lang);
extern const char *afb_session_get_language(struct afb_session *session, const char *lang);

extern int afb_session_cookie(struct afb_session *session, const void *key, void **cookie, void *(*makecb)(void *closure), void (*freecb)(void *item), void *closure, enum afb_session_cookie_operator oper);
extern int afb_session_get_cookie(struct afb_session *session, const void *key, void **cookie);
extern int afb_session_set_cookie(struct afb_session *session, const void *key, void *value, void (*freecb)(void*));

extern int afb_session_get_loa(struct afb_session *session, const void *key);
extern int afb_session_set_loa(struct afb_session *session, const void *key, int loa);

extern void afb_session_drop_key(struct afb_session *session, const void *key);

