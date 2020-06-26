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

#pragma once

#include <stdarg.h>

#include "afb-req-reply.h"

struct json_object;
struct afb_req_common;
struct afb_evt;
struct afb_cred;
struct afb_apiset;
struct afb_api_item;

struct afb_auth;
struct afb_event_x2;

struct afb_req_common_query_itf
{
	struct json_object *(*json)(struct afb_req_common *req);
	struct afb_arg (*get)(struct afb_req_common *req, const char *name);
	void (*reply)(struct afb_req_common *req, const struct afb_req_reply *reply);
	void (*unref)(struct afb_req_common *req);
	int (*subscribe)(struct afb_req_common *req, struct afb_evt *event);
	int (*unsubscribe)(struct afb_req_common *req, struct afb_evt *event);
};

/**
 * Internal data for requests
 */
struct afb_req_common
{
	struct afb_session *session;	/**< session */
	struct afb_token *token;	/**< token */
#if WITH_CRED
	struct afb_cred *credentials;	/**< credential */
#endif

	uint16_t refcount;		/**< current ref count */

	uint16_t replied: 1,		/**< is replied? */
	         created: 1,            /**< session created */
	         validated: 1,          /**< validated token */
	         invalidated: 1,        /**< invalidated token */
	         closing: 1,            /**< closing the session */
	         closed: 1,             /**< session closed */
	         asyncount: 4;          /**< count of async items */

	void *asyncitems[7];

	const char *apiname;
	const char *verbname;

	const struct afb_api_item *api;	/**< api item of the request */

	const struct afb_req_common_query_itf *queryitf; /**< interface of req implementation functions */

	struct json_object *json;	/**< the json object (or NULL) */

#if WITH_AFB_HOOK
	int hookflags;			/**< flags for hooking */
	int hookindex;			/**< hook index of the request if hooked */
#endif

#if WITH_REPLY_JOB
	/** the reply */
	struct afb_req_reply reply;
#endif
};

/* initialisation and processing of req */

extern int afb_req_common_reply_out_of_memory(struct afb_req_common *req);

extern int afb_req_common_reply_internal_error(struct afb_req_common *req);

extern int afb_req_common_reply_unavailable(struct afb_req_common *req);

extern int afb_req_common_reply_api_unknown(struct afb_req_common *req);

extern int afb_req_common_reply_api_bad_state(struct afb_req_common *req);

extern int afb_req_common_reply_verb_unknown(struct afb_req_common *req);

extern int afb_req_common_reply_invalid_token(struct afb_req_common *req);

extern int afb_req_common_reply_insufficient_scope(struct afb_req_common *req, const char *scope);

extern const char *afb_req_common_on_behalf_cred_export(struct afb_req_common *req);

extern
void
afb_req_common_init(
	struct afb_req_common *req,
	const struct afb_req_common_query_itf *queryitf,
	const char *apiname,
	const char *verbname
);

extern
void
afb_req_common_process(
	struct afb_req_common *req,
	struct afb_apiset *apiset
);

extern
void
afb_req_common_process_on_behalf(
	struct afb_req_common *req,
	struct afb_apiset *apiset,
	const char *import
);

extern
void
afb_req_common_check_and_set_session_async(
	struct afb_req_common *req,
	const struct afb_auth *auth,
	uint32_t sessionflags,
	void (*callback)(void *_closure, int _status),
	void *closure
);

extern
void
afb_req_common_set_session(
	struct afb_req_common *req,
	struct afb_session *session
);

extern
void
afb_req_common_set_session_string(
	struct afb_req_common *req,
	const char *uuid
);

extern
void
afb_req_common_set_token_string(
	struct afb_req_common *req,
	const char *token
);

extern
void
afb_req_common_set_token(
	struct afb_req_common *req,
	struct afb_token *token
);

#if WITH_CRED
extern
void
afb_req_common_set_cred(
	struct afb_req_common *req,
	struct afb_cred *cred
);
#endif

extern
void
afb_req_common_cleanup(
	struct afb_req_common *req
);

/******************************************************************************/

extern
int
afb_req_common_async_push(
	struct afb_req_common *req,
	void *value
);

extern
int
afb_req_common_async_push2(
	struct afb_req_common *req,
	void *value1,
	void *value2
);

extern
void*
afb_req_common_async_pop(
	struct afb_req_common *req
);

/******************************************************************************/

extern
struct afb_req_common *
afb_req_common_addref(
	struct afb_req_common *req
);

extern
void
afb_req_common_unref(
	struct afb_req_common *req
);

extern
void
afb_req_common_vverbose(
	struct afb_req_common *req,
	int level,
	const char *file,
	int line,
	const char *func,
	const char *fmt,
	va_list args
);

extern
struct json_object *
afb_req_common_json(
	struct afb_req_common *req
);

extern
struct afb_arg
afb_req_common_get(
	struct afb_req_common *req,
	const char *name
);

extern
void
afb_req_common_reply(
	struct afb_req_common *req,
	struct json_object *obj,
	const char *error,
	const char *info
);

extern
void afb_req_common_vreply(
	struct afb_req_common *req,
	struct json_object *obj,
	const char *error,
	const char *fmt,
	va_list args
);

extern
int
afb_req_common_subscribe(
	struct afb_req_common *req,
	struct afb_evt *evt
);

extern
int
afb_req_common_unsubscribe(
	struct afb_req_common *req,
	struct afb_evt *evt
);

extern
void *
afb_req_common_cookie(
	struct afb_req_common *req,
	void *(*maker)(void*),
	void (*freeer)(void*),
	void *closure,
	int replace
);

extern
int
afb_req_common_session_set_LOA(
	struct afb_req_common *req,
	unsigned level
);

extern
void
afb_req_common_session_close(
	struct afb_req_common *req
);

extern
struct json_object *
afb_req_common_get_client_info(
	struct afb_req_common *req
);

void
afb_req_common_check_permission(
	struct afb_req_common *req,
	const char *permission,
	void (*callback)(void *, int),
	void *closure
);

extern
int
afb_req_common_has_permission(
	struct afb_req_common *req,
	const char *permission
);

/******************************************************************************/

#if WITH_AFB_HOOK

extern
struct json_object *
afb_req_common_json_hookable(
	struct afb_req_common *req
);

extern
struct afb_arg
afb_req_common_get_hookable(
	struct afb_req_common *req,
	const char *name
);

extern
void
afb_req_common_reply_hookable(
	struct afb_req_common *req,
	struct json_object *obj,
	const char *error,
	const char *info
);

extern
void
afb_req_common_vreply_hookable(
	struct afb_req_common *req,
	struct json_object *obj,
	const char *error,
	const char *fmt,
	va_list args
);

extern
struct afb_req_common *
afb_req_common_addref_hookable(
	struct afb_req_common *req
);

extern
void
afb_req_common_unref_hookable(
	struct afb_req_common *req
);

extern
void
afb_req_common_session_close_hookable(
	struct afb_req_common *req
);

extern
int
afb_req_common_session_set_LOA_hookable(
	struct afb_req_common *req,
	unsigned level
);

extern
int
afb_req_common_subscribe_hookable(
	struct afb_req_common *req,
	struct afb_evt *evt
);

extern
int
afb_req_common_unsubscribe_hookable(
	struct afb_req_common *req,
	struct afb_evt *evt
);

extern
void
afb_req_common_vverbose_hookable(
	struct afb_req_common *req,
	int level, const char *file,
	int line,
	const char *func,
	const char *fmt,
	va_list args
);

extern
int
afb_req_common_has_permission_hookable(
	struct afb_req_common *req,
	const char *permission
);

extern
void *
afb_req_common_cookie_hookable(
	struct afb_req_common *req,
	void *(*maker)(void*),
	void (*freeer)(void*),
	void *closure,
	int replace
);

extern
struct json_object *
afb_req_common_get_client_info_hookable(
	struct afb_req_common *req
);

#endif

/******************************************************************************/
