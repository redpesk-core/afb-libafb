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

#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "core/afb-session.h"
#include "core/afb-context.h"
#include "core/afb-token.h"
#include "core/afb-cred.h"
#include "core/afb-perm.h"
#include "core/afb-permission-text.h"
#include "sys/verbose.h"
#include "sys/x-errno.h"

static void init_context(struct afb_context *context, struct afb_session *session, struct afb_token *token)
{
	assert(session != NULL);

	/* reset the context for the session */
	context->session = session;
	context->flags = 0;
	context->super = NULL;
	context->api_key = NULL;
	context->token = afb_token_addref(token);
#if WITH_CRED
	context->credentials = NULL;
#endif
}

void afb_context_subinit(struct afb_context *context, struct afb_context *super)
{
	context->session = afb_session_addref(super->session);
	context->flags = 0;
	context->super = super;
	context->api_key = NULL;
	context->token = afb_token_addref(super->token);
#if WITH_CRED
	context->credentials = afb_cred_addref(super->credentials);
#endif
}

void afb_context_init(struct afb_context *context, struct afb_session *session, struct afb_token *token)
{
	init_context(context, afb_session_addref(session), token);
}

int afb_context_connect(struct afb_context *context, const char *uuid, struct afb_token *token)
{
	int created;
	struct afb_session *session;

	session = afb_session_get (uuid, AFB_SESSION_TIMEOUT_DEFAULT, &created);
	if (session == NULL)
		return -1;
	init_context(context, session, token);
	if (created) {
		context->created = 1;
	}
	return 0;
}

int afb_context_connect_validated(struct afb_context *context, const char *uuid, struct afb_token *token)
{
	int rc = afb_context_connect(context, uuid, token);
	if (!rc)
		context->validated = 1;
	return rc;
}

void afb_context_init_validated(struct afb_context *context, struct afb_session *session, struct afb_token *token)
{
	afb_context_init(context, session, token);
	context->validated = 1;
}

void afb_context_disconnect(struct afb_context *context)
{
	if (context->session && !context->super && context->closing && !context->closed) {
		afb_context_force_loa(context, 0);
		afb_context_set(context, NULL, NULL);
		context->closed = 1;
	}
	afb_session_unref(context->session);
	context->session = NULL;
#if WITH_CRED
	afb_cred_unref(context->credentials);
	context->credentials = NULL;
#endif
	afb_token_unref(context->token);
	context->token = NULL;
}

#if WITH_CRED
void afb_context_change_cred(struct afb_context *context, struct afb_cred *cred)
{
	struct afb_cred *ocred = context->credentials;
	if (ocred != cred) {
		context->credentials = afb_cred_addref(cred);
		afb_cred_unref(ocred);
	}
}
#endif

void afb_context_change_token(struct afb_context *context, struct afb_token *token)
{
	struct afb_token *otoken = context->token;
	if (otoken != token) {
		context->token = afb_token_addref(token);
		afb_token_unref(otoken);
	}
}

const char *afb_context_on_behalf_export(struct afb_context *context)
{
#if WITH_CRED
	return context->credentials ? afb_cred_export(context->credentials) : NULL;
#else
	return NULL;
#endif
}

#if WITH_CRED
#if SYNCHRONOUS_CHECKS
int afb_context_on_behalf_import(struct afb_context *context, const char *exported)
{
	int rc;
	struct afb_cred *imported, *ocred;

	if (!exported || !*exported)
		rc = 0;
	else {
		if (afb_context_has_permission(context, afb_permission_on_behalf_credential)) {
			rc = afb_cred_import(&imported, exported);
			if (rc < 0)
				ERROR("Can't import on behalf credentials: %m");
			else {
				ocred = context->credentials;
				context->credentials = imported;
				afb_cred_unref(ocred);
				rc = 0;
			}
		} else {
			ERROR("On behalf credentials refused");
			rc = X_EPERM;
		}
	}
	return rc;
}
#endif

struct import_async
{
	struct afb_context *context;
	void (*callback)(void *_closure, int _status);
	void *closure;
	char exported[];
};

static void on_behalf_import_async(void *closure, int status)
{
	int rc;
	struct import_async *ia = closure;
	struct afb_cred *imported, *ocred;

	if (status > 0) {
		rc = afb_cred_import(&imported, ia->exported);
		if (rc < 0) {
			ERROR("Can't import on behalf credentials: %m");
		} else {
			ocred = ia->context->credentials;
			ia->context->credentials = imported;
			afb_cred_unref(ocred);
			rc = 0;
		}
	} else {
		ERROR("On behalf credentials refused");
		rc = X_EPERM;
	}
	ia->callback(ia->closure, rc);
	free(ia);
}

void afb_context_on_behalf_import_async(
	struct afb_context *context,
	const char *exported,
	void (*callback)(void *_closure, int _status),
	void *closure
)
{
	struct import_async *ia;

	if (!exported || !*exported)
		callback(closure, 0);
	else {
		ia = malloc(sizeof *ia + 1 + strlen(exported));
		if (!ia)
			callback(closure, -1);
		else {
			ia->context = context;
			strcpy(ia->exported, exported);
			ia->callback = callback;
			ia->closure = closure;
			afb_context_has_permission_async(
				context,
				afb_permission_on_behalf_credential,
				on_behalf_import_async,
				ia);
		}
	}
}
#else

#if SYNCHRONOUS_CHECKS
int afb_context_on_behalf_import(struct afb_context *context, const char *exported)
{
	return 0;
}
#endif

void afb_context_on_behalf_import_async(
	struct afb_context *context,
	const char *exported,
	void (*callback)(void *_closure, int _status),
	void *closure
)
{
	callback(closure, 0);
}
#endif

void afb_context_on_behalf_other_context(struct afb_context *context, struct afb_context *other)
{
#if WITH_CRED
	afb_context_change_cred(context, other->credentials);
#endif
	afb_context_change_token(context, other->token);
}

#if SYNCHRONOUS_CHECKS
int afb_context_has_permission(struct afb_context *context, const char *permission)
{
	return afb_perm_check(context, permission);
}
#endif

void afb_context_has_permission_async(
	struct afb_context *context,
	const char *permission,
	void (*callback)(void *_closure, int _status),
	void *closure
)
{
	return afb_perm_check_async(context, permission, callback, closure);
}

const char *afb_context_uuid(struct afb_context *context)
{
	return context->session ? afb_session_uuid(context->session) : NULL;
}

void *afb_context_make(struct afb_context *context, int replace, void *(*make_value)(void *closure), void (*free_value)(void *item), void *closure)
{
	assert(context->session != NULL);
	return afb_session_cookie(context->session, context->api_key, make_value, free_value, closure, replace);
}

void *afb_context_get(struct afb_context *context)
{
	assert(context->session != NULL);
	return afb_session_get_cookie(context->session, context->api_key);
}

int afb_context_set(struct afb_context *context, void *value, void (*free_value)(void*))
{
	assert(context->session != NULL);
	return afb_session_set_cookie(context->session, context->api_key, value, free_value);
}

void afb_context_close(struct afb_context *context)
{
	context->closing = 1;
}

struct chkctx {
	struct afb_context *context;
	void (*callback)(void *_closure, int _status);
	void *closure;
};

static void check_context_cb(void *closure_chkctx, int status)
{
	struct chkctx *cc = closure_chkctx;
	struct afb_context *context = cc->context;
	void (*callback)(void*,int) = cc->callback;
	void *closure = cc->closure;

	free(cc);
	if (status)
		context->validated = 1;
	else
		context->invalidated = 1;
	callback(closure, status);
}

static int check_context(
	struct afb_context *context,
	void (*callback)(void *_closure, int _status),
	void *closure
) {
	int r;
	struct chkctx *cc;

	if (context->validated)
		r = 1;
	else if (context->invalidated)
		r = 0;
	else {
		if (context->super)
			r = check_context(context->super, callback, closure);
		else if (callback) {
			cc = malloc(sizeof *cc);
			if (cc) {
				cc->context = context;
				cc->callback = callback;
				cc->closure = closure;
				afb_context_has_permission_async(context, afb_permission_token_valid, check_context_cb, cc);
				return -1;
			}
			ERROR("out-of-memory");
			r = 0;
		}
		else
#if SYNCHRONOUS_CHECKS
			r = afb_context_has_permission(context, afb_permission_token_valid);
#else
			r = 0;
#endif
		if (r)
			context->validated = 1;
		else
			context->invalidated = 1;
	}
	return r;
}

#if SYNCHRONOUS_CHECKS
int afb_context_check(struct afb_context *context)
{
	return check_context(context, 0, 0);
}
#endif

void afb_context_check_async(
	struct afb_context *context,
	void (*callback)(void *_closure, int _status),
	void *closure
) {
	int r = check_context(context, callback, closure);
	if (r >= 0)
		callback(closure, r);
}

static inline const void *loa_key(struct afb_context *context)
{
	return (const void*)(1+(intptr_t)(context->api_key));
}

static inline void *loa2ptr(unsigned loa)
{
	return (void*)(intptr_t)loa;
}

static inline unsigned ptr2loa(void *ptr)
{
	return (unsigned)(intptr_t)ptr;
}

struct setloa {
	struct afb_context *context;
	unsigned loa;
	void (*callback)(void *_closure, int _status);
	void *closure;
};

int afb_context_force_loa(struct afb_context *context, unsigned loa)
{
	return afb_session_set_loa(context->session, context->api_key, (int)loa);
}

static void set_loa_async(void *closure, int status)
{
	struct setloa *sl = closure;
	if (status > 1)
		status = afb_context_force_loa(sl->context, sl->loa);
	sl->callback(sl->closure, status);
	free(sl);
}

#if SYNCHRONOUS_CHECKS
int afb_context_change_loa(struct afb_context *context, unsigned loa)
{
	if (loa > 7)
		return X_EINVAL;
	if (!afb_context_check(context))
		return X_EPERM;

	return afb_context_force_loa(context, loa);
}
#endif

void afb_context_change_loa_async(struct afb_context *context, unsigned loa, void (*callback)(void *_closure, int _status), void *closure)
{
	struct setloa *sl;

	if (loa > 7) {
		errno = EINVAL;
		callback(closure, -1);
	}
	else if (context->validated)
		callback(closure, afb_context_force_loa(context, loa));
	else {
		sl = malloc(sizeof *sl);
		if (sl == NULL)
			callback(closure, -1);
		else {
			sl->context = context;
			sl->loa = loa;
			sl->callback = callback;
			sl->closure = closure;
			afb_context_check_async(context, set_loa_async, sl);
		}
	}
}

unsigned afb_context_get_loa(struct afb_context *context)
{
	assert(context->session != NULL);
	return (unsigned)afb_session_get_loa(context->session, context->api_key);
}

int afb_context_check_loa(struct afb_context *context, unsigned loa)
{
	return afb_context_get_loa(context) >= loa;
}
