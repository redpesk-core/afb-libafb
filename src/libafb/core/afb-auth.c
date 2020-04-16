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

#include <json-c/json.h>
#include <afb/afb-auth.h>
#include <afb/afb-session-x2.h>

#include "core/afb-auth.h"
#include "core/afb-context.h"
#include "core/afb-xreq.h"
#include "core/afb-error-text.h"
#include "sys/verbose.h"
#include "sys/x-errno.h"

/******************************************************************************/
/* synchronous checks                                                         */
/******************************************************************************/

#if SYNCHRONOUS_CHECKS
int afb_auth_check(struct afb_context *context, const struct afb_auth *auth)
{
	switch (auth->type) {
	default:
	case afb_auth_No:
		return 0;

	case afb_auth_Token:
		return afb_context_check(context);

	case afb_auth_LOA:
		return afb_context_check_loa(context, auth->loa);

	case afb_auth_Permission:
		return afb_context_has_permission(context, auth->text);

	case afb_auth_Or:
		return afb_auth_check(context, auth->first) || afb_auth_check(context, auth->next);

	case afb_auth_And:
		return afb_auth_check(context, auth->first) && afb_auth_check(context, auth->next);

	case afb_auth_Not:
		return !afb_auth_check(context, auth->first);

	case afb_auth_Yes:
		return 1;
	}
}
#endif

/******************************************************************************/
/* asynchronous checks                                                        */
/******************************************************************************/

/**
 * stacking structure for asynchronous checks
 */
struct async_stack
{
	struct afb_context *context;     /*< context to check */
	void (*callback)(void*,int);     /*< callback for delivering the result */
	void *closure;                   /*< closure for the callback */
	unsigned free: 1;                /*< boolean should the stack be freed? */
	unsigned index: 10;              /*< count of items stacked */
	const struct afb_auth *stack[];  /*< stack for checking complex expressions */
};

/**
 * compute the size of the stack need for asynchronous check
 */
static int async_stack_depth(const struct afb_auth *auth)
{
	int a, b;

	switch (auth->type) {
	case afb_auth_No:
	case afb_auth_Token:
	case afb_auth_LOA:
	case afb_auth_Yes:
	case afb_auth_Permission:
	default:
		return 0;
	case afb_auth_Not:
		return 1 + async_stack_depth(auth->first);
	case afb_auth_Or:
	case afb_auth_And:
		a = 1 + async_stack_depth(auth->first);
		b = async_stack_depth(auth->next);
		return a > b ? a : b;
	}
}

static void check_async(
	struct async_stack *stack,
	const struct afb_auth *auth
);

/**
 * callback receiving status checks
 */
static void check_async_cb(
	void *closure,
	int status
)
{
	struct async_stack *stack = closure;
	const struct afb_auth *auth;
	void (*cb)(void*,int);
	void *clo;

	/* pop the result as needed */
	while (stack->index) {
		auth = stack->stack[--stack->index];
		switch (auth->type) {
		case afb_auth_Or:
			if (status == 0) {
				/* query continuation of or */
				check_async(stack, auth->next);
				return;
			}
			break;

		case afb_auth_And:
			if (status > 0) {
				/* query continuation of and */
				check_async(stack, auth->next);
				return;
			}
			break;

		case afb_auth_Not:
			status = status < 0 ? status : !status;
			break;

		default:
			status = 0;
			break;
		}
	}

	/* emit the results */
	cb = stack->callback;
	clo = stack->closure;
	if (stack->free)
		free(stack);
	cb(clo, status);
}

/**
 * asynchronous check of auth in the context of stack
 */
static void check_async(
	struct async_stack *stack,
	const struct afb_auth *auth
)
{
	int status = 0;

	while (auth) {
		switch (auth->type) {
		default:
		case afb_auth_No:
			status = 0;
			auth = 0;
			break;

		case afb_auth_Token:
			afb_context_check_async(stack->context, check_async_cb, stack);
			return;

		case afb_auth_LOA:
			status = afb_context_check_loa(stack->context, auth->loa);
			auth = 0;
			break;

		case afb_auth_Permission:
			afb_context_has_permission_async(stack->context, auth->text, check_async_cb, stack);
			return;

		case afb_auth_Or:
		case afb_auth_And:
		case afb_auth_Not:
			stack->stack[stack->index++] = auth;
			auth = auth->first;
			break;

		case afb_auth_Yes:
			status = 1;
			auth = 0;
			break;
		}
	}
	check_async_cb(stack, status);
}

/**
 * Asynchronous check of the authorization 'auth' for 'context'.
 * Calls 'callback' with the result and the given 'closure'.
 * The status is <0 for an error, 0 for a deny, 1 for a grant.
 */
void afb_auth_check_async(
	struct afb_context *context,
	const struct afb_auth *auth,
	void (*callback)(void *_closure, int _status),
	void *closure
)
{
	int depth;
	struct async_stack *stack;

	depth = async_stack_depth(auth);
	stack = malloc(sizeof *stack + depth * sizeof stack->stack[0]);
	if (!stack)
		callback(closure, -X_ENOMEM);

	stack->context = context;
	stack->callback = callback;
	stack->closure = closure;
	stack->free = 1;
	stack->index = 0;
	check_async(stack, auth);
}

/******************************************************************************/
/** Synchronous check and set                                                **/
/******************************************************************************/
#if SYNCHRONOUS_CHECKS

int afb_auth_check_and_set_session_x2(struct afb_xreq *xreq, const struct afb_auth *auth, uint32_t sessionflags)
{
	int loa;

	if (sessionflags != 0) {
		if (!afb_context_check(&xreq->context)) {
			afb_context_close(&xreq->context);
			return afb_xreq_reply_invalid_token(xreq);
		}
	}

	loa = (int)(sessionflags & AFB_SESSION_LOA_MASK_X2);
	if (loa && !afb_context_check_loa(&xreq->context, loa))
		return afb_xreq_reply_insufficient_scope(xreq, "invalid LOA");

	if (auth && !afb_auth_check(&xreq->context, auth))
		return afb_xreq_reply_insufficient_scope(xreq, NULL /* TODO */);

	if ((sessionflags & AFB_SESSION_CLOSE_X2) != 0)
		afb_context_close(&xreq->context);

	return 1;
}

#endif
/******************************************************************************/
/** Asynchronous check and set                                                **/
/******************************************************************************/

enum check_and_set_issue
{
	CSIssue_Ok,
	CSIssue_Scope,
	CSIssue_Token
};

struct async_check
{
	struct afb_xreq *xreq;
	uint32_t sessionflags;
	void (*callback)(struct afb_xreq *_xreq, int _status, void *_closure);
	void *closure;
	struct async_stack stack;
};

static void check_and_set_final(struct async_check *check, enum check_and_set_issue issue)
{
	int status, loa;

	switch (issue) {
	case CSIssue_Ok:
		loa = (int)(check->sessionflags & AFB_SESSION_LOA_MASK_X2);
		if (loa && !afb_context_check_loa(&check->xreq->context, loa))
			status = afb_xreq_reply_insufficient_scope(check->xreq, "invalid LOA");
		else
			status = 1;
		break;

	case CSIssue_Scope:
		status = afb_xreq_reply_insufficient_scope(check->xreq, NULL);
		break;

	case CSIssue_Token:
		status = afb_xreq_reply_invalid_token(check->xreq);
		break;
	}

	if (status <= 0 || (check->sessionflags & AFB_SESSION_CLOSE_X2)) {
		afb_context_force_loa(&check->xreq->context, 0);
		afb_context_close(&check->xreq->context);
	}

	check->callback(check->xreq, status, check->closure);
	afb_xreq_unhooked_unref(check->xreq);
	free(check);
}

static void check_and_set_auth_cb(void *closure, int status)
{
	struct async_check *check = closure;
	check_and_set_final(check, status ? CSIssue_Ok : CSIssue_Scope);
}

static void check_and_set_session_cb(void *closure, int status)
{
	struct async_check *check = closure;

	if (status <= 0)
		check_and_set_final(check, CSIssue_Token);
	else if (check->stack.stack[0])
		check_async(&check->stack, check->stack.stack[0]);
	else
		check_and_set_final(check, CSIssue_Ok);
}

void afb_auth_check_and_set_session_x2_async(
	struct afb_xreq *xreq,
	const struct afb_auth *auth,
	uint32_t sessionflags,
	void (*callback)(struct afb_xreq *_xreq, int _status, void *_closure),
	void *closure
)
{
	int depth;
	struct async_check *check;

	if (!sessionflags && !auth)
		callback(xreq, 1, closure);
	else {
		depth = auth ? async_stack_depth(auth) : 1;
		check = malloc(sizeof *check + (depth?:1) * sizeof check->stack.stack[0]);
		if (!check) {
			afb_xreq_reply(xreq, NULL, afb_error_text_internal_error, NULL);
			callback(xreq, -X_ENOMEM, closure);
		}
		else {
			afb_xreq_unhooked_addref(xreq);
			check->xreq = xreq;
			check->callback = callback;
			check->closure = closure;
			check->stack.context = &xreq->context;
			check->stack.callback = check_and_set_auth_cb;
			check->stack.closure = check;
			check->stack.free = 0;
			check->stack.index = 0;
			check->sessionflags = sessionflags;
			if (sessionflags) {
				check->stack.stack[0] = auth;
				afb_context_check_async(&xreq->context, check_and_set_session_cb, check);
			} else {
				check_async(&check->stack, auth);
			}
		}
	}
}

/******************************************************************************/
/* json output format                                                         */
/******************************************************************************/

static struct json_object *addperm(struct json_object *o, struct json_object *x)
{
	struct json_object *a;

	if (!o)
		return x;

	if (!json_object_object_get_ex(o, "allOf", &a)) {
		a = json_object_new_array();
		json_object_array_add(a, o);
		o = json_object_new_object();
		json_object_object_add(o, "allOf", a);
	}
	json_object_array_add(a, x);
	return o;
}

static struct json_object *addperm_key_val(struct json_object *o, const char *key, struct json_object *val)
{
	struct json_object *x = json_object_new_object();
	json_object_object_add(x, key, val);
	return addperm(o, x);
}

static struct json_object *addperm_key_valstr(struct json_object *o, const char *key, const char *val)
{
	return addperm_key_val(o, key, json_object_new_string(val));
}

static struct json_object *addperm_key_valint(struct json_object *o, const char *key, int val)
{
	return addperm_key_val(o, key, json_object_new_int(val));
}

static struct json_object *addauth_or_array(struct json_object *o, const struct afb_auth *auth);

static struct json_object *addauth(struct json_object *o, const struct afb_auth *auth)
{
	switch(auth->type) {
	case afb_auth_No: return addperm(o, json_object_new_boolean(0));
	case afb_auth_Token: return addperm_key_valstr(o, "session", "check");
	case afb_auth_LOA: return addperm_key_valint(o, "LOA", auth->loa);
	case afb_auth_Permission: return addperm_key_valstr(o, "permission", auth->text);
	case afb_auth_Or: return addperm_key_val(o, "anyOf", addauth_or_array(json_object_new_array(), auth));
	case afb_auth_And: return addauth(addauth(o, auth->first), auth->next);
	case afb_auth_Not: return addperm_key_val(o, "not", addauth(NULL, auth->first));
	case afb_auth_Yes: return addperm(o, json_object_new_boolean(1));
	}
	return o;
}

static struct json_object *addauth_or_array(struct json_object *o, const struct afb_auth *auth)
{
	if (auth->type != afb_auth_Or)
		json_object_array_add(o, addauth(NULL, auth));
	else {
		addauth_or_array(o, auth->first);
		addauth_or_array(o, auth->next);
	}

	return o;
}

struct json_object *afb_auth_json_x2(const struct afb_auth *auth, uint32_t session)
{
	struct json_object *result = NULL;

	if (session & AFB_SESSION_CLOSE_X2)
		result = addperm_key_valstr(result, "session", "close");

	if (session & AFB_SESSION_CHECK_X2)
		result = addperm_key_valstr(result, "session", "check");

	if (session & AFB_SESSION_REFRESH_X2)
		result = addperm_key_valstr(result, "token", "refresh");

	if (session & AFB_SESSION_LOA_MASK_X2)
		result = addperm_key_valint(result, "LOA", session & AFB_SESSION_LOA_MASK_X2);

	if (auth)
		result = addauth(result, auth);

	return result;
}
