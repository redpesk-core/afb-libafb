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

#include <libafb/libafb-config.h>

#include <stdint.h>
#include <stdarg.h>

struct json_object;
struct afb_req_common;
struct afb_evt;
struct afb_cred;
struct afb_apiset;
struct afb_api_item;
struct afb_data;
struct afb_type;

struct afb_auth;
struct afb_event_x2;

/**
 * Interface of the requests
 */
struct afb_req_common_query_itf
{
	/**
	 * callback receiving the reply to the request
	 */
	void (*reply)(struct afb_req_common *req, int status, unsigned nreplies, struct afb_data * const replies[]);

	/**
	 * callback receiving the unreferenced event
	 */
	void (*unref)(struct afb_req_common *req);

	/**
	 * callback receiving subscribe requests
	 */
	int (*subscribe)(struct afb_req_common *req, struct afb_evt *event);

	/**
	 * callback receiving unsubscribe requests
	 */
	int (*unsubscribe)(struct afb_req_common *req, struct afb_evt *event);
};

/**
 * Default count of statically allocated data in structure
 */
#if !defined(REQ_COMMON_NDATA_DEF)
# define REQ_COMMON_NDATA_DEF  8
#endif

/**
 * Internal data holder for arguments and replies
 */
struct afb_req_common_arg
{
	/** count of data */
	unsigned ndata;

	/** current data */
	struct afb_data **data;

	/** preallocated local data */
	struct afb_data *local[REQ_COMMON_NDATA_DEF];
};

/**
 * Default count of stack of asynchronous
 */
#if !defined(REQ_COMMON_NASYNC)
# define REQ_COMMON_NASYNC  7
#endif
#if REQ_COMMON_NASYNC > 15 /* only 4 bits for asyncount */
# error "REQ_COMMON_NASYNC greater than 15"
#endif

/**
 * Internal data for requests
 */
struct afb_req_common
{
	/** current ref count */
	uint16_t refcount;

	uint16_t replied: 1,		/**< is replied? */
	         created: 1,            /**< session created */
	         validated: 1,          /**< validated token */
	         invalidated: 1,        /**< invalidated token */
	         closing: 1,            /**< closing the session */
	         closed: 1,             /**< session closed */
	         asyncount: 4;          /**< count of async items */

#if WITH_AFB_HOOK
	/** flags for hooking */
	unsigned hookflags;
	/** hook index of the request if hooked */
	unsigned hookindex;
#endif
	/** preallocated stack for asynchronous processing */
	void *asyncitems[REQ_COMMON_NASYNC];

	/** session */
	struct afb_session *session;

	/** token */
	struct afb_token *token;

#if WITH_CRED
	/** credential */
	struct afb_cred *credentials;
#endif
	/** request api name */
	const char *apiname;

	/** request verb name */
	const char *verbname;

	/** api item of the request */
	const struct afb_api_item *api;

	/** interface of req implementation functions */
	const struct afb_req_common_query_itf *queryitf;

	/** the parameters (arguments) of the request */
	struct afb_req_common_arg params;

#if WITH_REPLY_JOB
	/** the reply status */
	int status;

	/** the reply data */
	struct afb_req_common_arg replies;
#endif
};

/* reply of errors */

extern int afb_req_common_reply_out_of_memory_error_hookable(struct afb_req_common *req);

extern int afb_req_common_reply_internal_error_hookable(struct afb_req_common *req, int error);

extern int afb_req_common_reply_unavailable_error_hookable(struct afb_req_common *req);

extern int afb_req_common_reply_api_unknown_error_hookable(struct afb_req_common *req);

extern int afb_req_common_reply_api_bad_state_error_hookable(struct afb_req_common *req);

extern int afb_req_common_reply_verb_unknown_error_hookable(struct afb_req_common *req);

extern int afb_req_common_reply_invalid_token_error_hookable(struct afb_req_common *req);

extern int afb_req_common_reply_insufficient_scope_error_hookable(struct afb_req_common *req, const char *scope);

/* initialisation and processing of req */

extern const char *afb_req_common_on_behalf_cred_export(struct afb_req_common *req);

extern
void
afb_req_common_init(
	struct afb_req_common *req,
	const struct afb_req_common_query_itf *queryitf,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	struct afb_data * const params[]
);

extern
void
afb_req_common_set_params(
	struct afb_req_common *req,
	unsigned nparams,
	struct afb_data * const params[]
);

extern
void
afb_req_common_prepare_forwarding(
	struct afb_req_common *req,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	struct afb_data * const params[]
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
afb_req_common_set_token(
	struct afb_req_common *req,
	struct afb_token *token
);

extern
int
afb_req_common_set_session_string(
	struct afb_req_common *req,
	const char *uuid
);

extern
int
afb_req_common_set_token_string(
	struct afb_req_common *req,
	const char *token
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

/******************************************************************************/

/**
 * Convert a parameter of the request to a given type and return it.
 *
 * The converted result is substituted to the previous parameter.
 * There is no need to unreference the returned data as it becomes
 * part of the request and will be released .
 *
 * Previous value of the parameter is automatically unreferenced.
 * If you want keep it, you have to first reference it using afb_data_addref.
 *
 * @param req the request
 * @param index index of the parameter to convert
 * @param type  target type of the conversion
 * @param result where to store the result (can be NULL)
 *
 * @return 0 in case of success, a negative code on error
 */
extern
int
afb_req_common_param_convert(
	struct afb_req_common *req,
	unsigned index,
	struct afb_type *type,
	struct afb_data **result
);

/******************************************************************************/

extern
void
afb_req_common_reply_hookable(
	struct afb_req_common *req,
	int status,
	unsigned nparams,
	struct afb_data * const params[]
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
unsigned
afb_req_common_session_get_LOA_hookable(
	struct afb_req_common *req
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
void
afb_req_common_check_permission_hookable(
	struct afb_req_common *req,
	const char *permission,
	void (*callback)(void*,int,void*,void*),
	void *closure1,
	void *closure2,
	void *closure3
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

/* set the cookie of the api getting the request */
extern
int
afb_req_common_cookie_set_hookable(
	struct afb_req_common *req,
	void *value,
	void (*freecb)(void*),
	void *freeclo
);

/* get the cookie of the api getting the request */
extern
int
afb_req_common_cookie_get_hookable(
	struct afb_req_common *req,
	void **value
);

/* get the cookie of the api getting the request */
extern
int
afb_req_common_cookie_getinit_hookable(
	struct afb_req_common *req,
	void **value,
	int (*initcb)(void *closure, void **value, void (**freecb)(void*), void **freeclo),
	void *closure
);

/* set the cookie of the api getting the request */
extern
int
afb_req_common_cookie_drop_hookable(
	struct afb_req_common *req
);

extern
struct json_object *
afb_req_common_get_client_info_hookable(
	struct afb_req_common *req
);

/******************************************************************************/
