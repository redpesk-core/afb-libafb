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
#include <stddef.h>
#include <afb/afb-req-x2-itf.h>
#include "../core/afb-context.h"

struct json_object;
struct afb_evt_listener;
struct afb_xreq;
struct afb_cred;
struct afb_apiset;
struct afb_event_x2;
struct afb_verb_desc_v1;
struct afb_verb_v2;
struct afb_verb_v3;
struct afb_req_x1;
struct afb_stored_req;

struct afb_xreq_query_itf {
	struct json_object *(*json)(struct afb_xreq *xreq);
	struct afb_arg (*get)(struct afb_xreq *xreq, const char *name);
	void (*reply)(struct afb_xreq *xreq, struct json_object *obj, const char *error, const char *info);
	void (*unref)(struct afb_xreq *xreq);
	int (*subscribe)(struct afb_xreq *xreq, struct afb_event_x2 *event);
	int (*unsubscribe)(struct afb_xreq *xreq, struct afb_event_x2 *event);
};

/**
 * Internal data for requests
 */
struct afb_xreq
{
	struct afb_req_x2 request;	/**< exported request */
	struct afb_context context;	/**< context of the request */
	struct afb_apiset *apiset;	/**< apiset of the xreq */
	struct json_object *json;	/**< the json object (or NULL) */
	const struct afb_xreq_query_itf *queryitf; /**< interface of xreq implementation functions */
	int refcount;			/**< current ref count */
	int replied;			/**< is replied? */
#if WITH_AFB_HOOK
	int hookflags;			/**< flags for hooking */
	int hookindex;			/**< hook index of the request if hooked */
#endif
	struct afb_xreq *caller;	/**< caller request if any */

#if WITH_REPLY_JOB
	/** the reply */
	struct {
		struct json_object *object; /**< the replied object if any */
		char *error;		/**< the replied error if any */
		char *info;		/**< the replied info if any */
	} reply;
#endif
};

/**
 * Macro for retrieve the pointer of a structure of 'type' having a field named 'field'
 * of address 'ptr'.
 * @param type the type that has the 'field' (ex: "struct mystruct")
 * @param field the name of the field within the structure 'type'
 * @param ptr the pointer to an element 'field'
 * @return the pointer to the structure that contains the 'field' at address 'ptr'
 */
#define CONTAINER_OF(type,field,ptr) ((type*) (((char*)(ptr)) - offsetof(type,field)))

/**
 * Macro for retrieve the pointer of a structure of 'type' having a field named "xreq"
 * of address 'x'.
 * @param type the type that has the field "xreq" (ex: "struct mystruct")
 * @param x the pointer to the field "xreq"
 * @return the pointer to the structure that contains the field "xreq" of address 'x'
 */
#define CONTAINER_OF_XREQ(type,x) CONTAINER_OF(type,xreq,x)

/* req wrappers for xreq */
extern struct afb_req_x1 afb_xreq_unstore(struct afb_stored_req *sreq);

extern void afb_xreq_addref(struct afb_xreq *xreq);
extern void afb_xreq_unref(struct afb_xreq *xreq);
extern void afb_xreq_unhooked_addref(struct afb_xreq *xreq);
extern void afb_xreq_unhooked_unref(struct afb_xreq *xreq);

extern struct json_object *afb_xreq_unhooked_json(struct afb_xreq *xreq);
extern struct json_object *afb_xreq_json(struct afb_xreq *xreq);

extern void afb_xreq_reply(struct afb_xreq *xreq, struct json_object *obj, const char *error, const char *info);
extern void afb_xreq_reply_v(struct afb_xreq *xreq, struct json_object *obj, const char *error, const char *info, va_list args);
extern void afb_xreq_reply_f(struct afb_xreq *xreq, struct json_object *obj, const char *error, const char *info, ...);

extern int afb_xreq_reply_unknown_api(struct afb_xreq *xreq);
extern int afb_xreq_reply_unknown_verb(struct afb_xreq *xreq);

extern int afb_xreq_reply_invalid_token(struct afb_xreq *xreq);
extern int afb_xreq_reply_insufficient_scope(struct afb_xreq *xreq, const char *scope);


extern const char *afb_xreq_raw(struct afb_xreq *xreq, size_t *size);

extern int afb_xreq_subscribe(struct afb_xreq *xreq, struct afb_event_x2 *event);
extern int afb_xreq_unsubscribe(struct afb_xreq *xreq, struct afb_event_x2 *event);

extern void afb_xreq_subcall(
		struct afb_xreq *xreq,
		const char *api,
		const char *verb,
		struct json_object *args,
		int flags,
		void (*callback)(void*, struct json_object*, const char*, const char*, struct afb_req_x2 *),
		void *closure);
extern void afb_xreq_unhooked_subcall(
		struct afb_xreq *xreq,
		const char *api,
		const char *verb,
		struct json_object *args,
		int flags,
		void (*callback)(void*, struct json_object*, const char*, const char*, struct afb_req_x2 *),
		void *closure);

/* initialisation and processing of xreq */
extern void afb_xreq_init(struct afb_xreq *xreq, const struct afb_xreq_query_itf *queryitf);

extern void afb_xreq_process(struct afb_xreq *xreq, struct afb_apiset *apiset);

extern void afb_xreq_call_verb_v3(struct afb_xreq *xreq, const struct afb_verb_v3 *verb);

extern const char *xreq_on_behalf_cred_export(struct afb_xreq *xreq);

/******************************************************************************/

static inline struct afb_req_x1 xreq_to_req_x1(struct afb_xreq *xreq)
{
	return (struct afb_req_x1){ .itf = xreq->request.itf, .closure = &xreq->request };
}

static inline struct afb_req_x2 *xreq_to_req_x2(struct afb_xreq *xreq)
{
	return &xreq->request;
}

static inline struct afb_xreq *xreq_from_req_x2(struct afb_req_x2 *req)
{
	return CONTAINER_OF(struct afb_xreq, request, req);
}
