/*
 * Copyright (C) 2015-2025 IoT.bzh Company
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

#include "core/afb-info.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "sys/x-errno.h"

#include "core/afb-v4-itf.h"
#include "core/afb-data.h"
#include "core/afb-type-predefined.h"

/*
* states:
*
*               |
*          init |
*               V
*              INIT
*               |
*       sed-api |
*               |
*               +---------------+
*               |               |
*  add-verb     V               V
* +--------> GOTSPEC          GOTAPI
* |             |               |
* |             |               |   add-verb
* +-------------+               +------------+
*               |               |            |
*               |               |            V     add-verb
*               |               |         GOTVERB <--------+
*               |               |            |             |
*               |               |            +-------------+
*               |               |            |
*               +---------------+------------+
*                               |
*                           end |
*                               V
*                              INIT
*
* Other cases leads to error until init.
*/
#define INIT    0
#define GOTSPEC 1
#define GOTAPI  2
#define GOTVERB 3

const char afb_info_verbname[] = "info";
static const char schema[] = "afb-api-info/v2";

/************************************************************************
 ** manage info internals
 ***********************************************************************/

/* record the first error and return it until reset */
static int ret(struct afb_info *info, int rc)
{
	if (info->state < 0)
		return info->state;
	if (rc < 0)
		info->state = rc;
	return rc;
}

/* copy the string to the buffer */
static void putstr(struct afb_info *info, const char *str, bool escape)
{
	for(;;) {
		char c = *str++;
		if (c == 0) {
			if (info->pos < info->size)
				info->buffer[info->pos] = c;
			return;
		}
		if (escape && (c == '"' || c == '\\')) {
			if (info->pos < info->size)
				info->buffer[info->pos] = '\\';
			info->pos++;
		}
		if (info->pos < info->size)
			info->buffer[info->pos] = c;
		info->pos++;
	}
}

/* copy the string to the buffer */
static void putuint(struct afb_info *info, unsigned val)
{
	unsigned quot = val / 10, rem = val % 10;
	if (quot > 0)
		putuint(info, quot);
	if (info->pos < info->size)
		info->buffer[info->pos] = '0' + (char)rem;
	info->pos++;
}

/************************************************************************
 ** manage authorisation
 ***********************************************************************/

struct unfold
{
	const struct afb_auth *auth;
	const struct unfold *previous;
};

struct unfold_next
{
	const struct afb_auth *auth;
	void (*done)(void *closure, unsigned session, const struct unfold *list);
	void *closure;
};

/* validate the auth */
static bool auth_valid(
	const struct afb_auth *auth,
	const struct unfold *previous
) {
	struct unfold ufo;
	const struct unfold *iter;

	if (auth == NULL)
		return false;

	switch (auth->type) {
	case afb_auth_No:
	case afb_auth_Token:
	case afb_auth_Yes:
		return true;
	case afb_auth_LOA:
		return auth->loa == (auth->loa & AFB_SESSION_LOA_MASK);
	case afb_auth_Permission:
		return auth->text != NULL;
	case afb_auth_Or:
	case afb_auth_And:
	case afb_auth_Not:
		/* detect recursion */
		for (iter = previous ; iter != NULL ; iter = iter->previous)
			if (iter->auth == auth)
				return false;
		ufo = (struct unfold){ auth, previous };
		/* validate children */
		return auth_valid(auth->first, &ufo)
			&& (auth->type == afb_auth_Not
				|| auth_valid(auth->next, &ufo));
	default:
		return false;
	}
}

/* compute the minimum value of loa */
static unsigned auth_minloa(
	const struct afb_auth *auth
) {
	unsigned a, b;

	if (auth == NULL)
		return 0;

	switch (auth->type) {
	case afb_auth_LOA:
		return auth->loa & AFB_SESSION_LOA_MASK;

	case afb_auth_Or:
		a = auth_minloa(auth->first);
		b = auth_minloa(auth->next);
		return a < b ? a : b;

	case afb_auth_And:
		a = auth_minloa(auth->first);
		b = auth_minloa(auth->next);
		return a > b ? a : b;

	default:
		return 0;
	}
}

/* compute if session check is required*/
static unsigned auth_check_token(
	const struct afb_auth *auth
) {
	unsigned a, b;

	if (auth == NULL)
		return 0;

	switch (auth->type) {
	case afb_auth_Token:
	case afb_auth_Permission:
		return AFB_SESSION_CHECK;

	case afb_auth_Or:
		a = auth_check_token(auth->first);
		b = auth_check_token(auth->next);
		return a & b;

	case afb_auth_And:
		a = auth_check_token(auth->first);
		b = auth_check_token(auth->next);
		return a | b;

	case afb_auth_Not:
		return auth_check_token(auth->first);

	default:
		return 0;
	}
}

/* check if auth is needed */
static bool auth_needed(
	const struct afb_auth *auth,
	unsigned session
) {
	if (auth == NULL)
		return false;
	switch (auth->type) {
	case afb_auth_Token:
		return (session & AFB_SESSION_CHECK) == 0;
	case afb_auth_LOA:
		return (session & AFB_SESSION_LOA_MASK) < auth->loa;
	default:
		return true;
	}
}

static void auth_unfold_next(void *closure, unsigned session, const struct unfold *list);

/* */
static void auth_unfold(
	const struct afb_auth *auth,
	unsigned session,
	const struct unfold *list,
	afb_auth_type_t type,
	void (*done)(void *closure, unsigned session, const struct unfold *list),
	void *closure
) {
	if (!auth_needed(auth, session))
		done(closure, session, list);
	else if (auth->type != type) {
		struct unfold ufo = { auth, list };
		done(closure, session, &ufo);
	}
	else {
		struct unfold_next nxt = { auth, done, closure };
		auth_unfold(auth->first, session, list, type, auth_unfold_next, &nxt);
	}
}

/* */
static void auth_unfold_next(
	void *closure,
	unsigned session,
	const struct unfold *list
) {
	const struct unfold_next *nxt = closure;
	auth_unfold(nxt->auth->next, session, list, nxt->auth->type, nxt->done, nxt->closure);
}

/* */
static void auth_any(
	struct afb_info *info,
	const struct afb_auth *auth,
	unsigned session,
	afb_auth_type_t type
);

/* */
static void auth_list_previous(
	struct afb_info *info,
	unsigned session,
	const struct unfold *list,
	afb_auth_type_t type
) {
	if (list != NULL) {
		auth_list_previous(info, session, list->previous, type);
		auth_any(info, list->auth, session, type);
		putstr(info, type == afb_auth_And ? " and " : " or ", false);
	}
}

/* */
static void auth_list(
	struct afb_info *info,
	unsigned session,
	const struct unfold *list,
	afb_auth_type_t type,
	bool par
) {
	if (list != NULL) {
		par = par && list->previous != NULL;
		if (par)
			putstr(info, "(", false);
		auth_list_previous(info, session, list->previous, type);
		auth_any(info, list->auth, session, type);
		if (par)
			putstr(info, ")", false);
	}
}

/* */
static void auth_and(
	void *closure,
	unsigned session,
	const struct unfold *list
) {
	auth_list(closure, session, list, afb_auth_And, false);
}

/* */
static void auth_and_par(
	void *closure,
	unsigned session,
	const struct unfold *list
) {
	auth_list(closure, session, list, afb_auth_And, true);
}

/* */
static void auth_or_par(
	void *closure,
	unsigned session,
	const struct unfold *list
) {
	auth_list(closure, session, list, afb_auth_Or, true);
}

/* */
static void auth_any(
	struct afb_info *info,
	const struct afb_auth *auth,
	unsigned session,
	afb_auth_type_t type
) {
	switch (auth->type) {
	case afb_auth_No:
		putstr(info, "no", false);
		break;
	case afb_auth_Token:
		putstr(info, "check-token", false);
		break;
	case afb_auth_LOA:
		putstr(info, "loa>=", false);
		putuint(info, auth->loa);
		break;
	case afb_auth_Permission:
		putstr(info, auth->text, true);
		break;
	case afb_auth_Or:
		auth_unfold(auth, session, NULL, afb_auth_Or, auth_or_par, info);
		break;
	case afb_auth_And:
		if ((session & AFB_SESSION_CHECK) != 0 || !auth_check_token(auth))
			auth_unfold(auth, session, NULL, afb_auth_And, auth_and_par, info);
		else {
			struct afb_auth auth_check = { .type = afb_auth_Token };
			struct unfold ufo_check = { .auth = &auth_check, .previous = NULL };

			auth_unfold(auth, session | AFB_SESSION_CHECK, &ufo_check,
					afb_auth_And, auth_and_par, info);
		}
		break;
	case afb_auth_Not:
		if (auth->first->type == afb_auth_Not)
			auth = auth->first;
		else
			putstr(info, "not ", false);
		auth_any(info, auth->first, session, afb_auth_Not);
		break;
	case afb_auth_Yes:
		putstr(info, "yes", false);
		break;
	}
}

/* put the auth string */
static void putauth(
	struct afb_info *info,
	const struct afb_auth *auth,
	unsigned session,
	afb_auth_type_t type
) {
	struct afb_auth auth_loa, auth_check;
	struct unfold ufo_loa, ufo_check;
	const struct unfold *pufo = NULL;

	/* consolidate session with content of auth */
	if (auth != NULL) {
		unsigned loa, check;
		loa = auth_minloa(auth);
		if (loa < (session & AFB_SESSION_LOA_MASK))
			loa = session & AFB_SESSION_LOA_MASK;
		check = auth_check_token(auth) | (session & AFB_SESSION_CHECK);
		session = check | loa;
		if (!auth_valid(auth, NULL))
			auth = NULL;
	}

	/* set loa */
	if (session & AFB_SESSION_LOA_MASK) {
		auth_loa.type = afb_auth_LOA;
		auth_loa.loa = session & AFB_SESSION_LOA_MASK;
		ufo_loa.auth = &auth_loa;
		ufo_loa.previous = pufo;
		pufo = &ufo_loa;
	}

	/* set check */
	if (session & AFB_SESSION_CHECK) {
		auth_check.type = afb_auth_Token;
		ufo_check.auth = &auth_check;
		ufo_check.previous = pufo;
		pufo = &ufo_check;
	}

	/* put as and now */
	putstr(info, "\"", false);
	if (auth != NULL)
		auth_unfold(auth, session, pufo, afb_auth_And, auth_and, info);
	else
		auth_and(info, session, pufo);
	putstr(info, "\"", false);
}

/************************************************************************
 ** public interface
 ***********************************************************************/

void afb_info_init(
	struct afb_info *info
) {
	memset(info, 0, sizeof *info);
}

int afb_info_set_api(
	struct afb_info *info,
	const char *apiname,
	const char *apiinfo,
	const char *apispec
) {
	int rc = X_EINVAL;

	if (info->state != INIT)
		goto end;

	if (apispec != NULL) {
		info->state = GOTSPEC;
		info->spec = apispec;
		return ret(info, 0);
	}

	if (apiname == NULL)
		goto end;

	/* begin with schema */
	putstr(info, "{\"$schema\":\"", false);
	putstr(info, schema, true);
	putstr(info, "\"", false);

	/* name of the API (optional) */
	if (apiname != NULL) {
		putstr(info, ",\"name\":\"", false);
		putstr(info, apiname, true);
		putstr(info, "\"", false);
	}

	/* info of the API (optional) */
	if (apiinfo != NULL) {
		putstr(info, ",\"info\":\"", false);
		putstr(info, apiinfo, true);
		putstr(info, "\"", false);
	}

	/* verbs, mandatory */
	putstr(info, ",\"verbs\":[", false);

	info->state = GOTAPI;
	rc = 0;

end:
	return ret(info, rc);
}

int afb_info_add_verb(
	struct afb_info *info,
	const char *verbname,
	const char *verbinfo,
	unsigned session,
	const struct afb_auth *auth,
	unsigned glob
) {
	int rc = X_EINVAL;

	if (verbname == NULL)
		goto end;

	switch(info->state) {
	case GOTSPEC:
		/* do nothing if spec exists */
		return ret(info, 0);

	case GOTAPI:
		/* first verb */
		info->state = GOTVERB;
		break;

	case GOTVERB:
		/* other verbs */
		putstr(info, ",", false);
		break;

	case INIT:
	default:
		goto end;
	}

	/* enter dictionary */
	putstr(info, "{\"name\":\"", false);
	putstr(info, verbname, true);
	putstr(info, "\"", false);

	if (verbinfo != NULL) {
		putstr(info, ",\"info\":\"", false);
		putstr(info, verbinfo, true);
		putstr(info, "\"", false);
	}
	if (glob)
		putstr(info, ",\"glob\":true", false);
	if (session & AFB_SESSION_CLOSE)
		putstr(info, ",\"session-close\":true", false);

	session &= AFB_SESSION_LOA_MASK | AFB_SESSION_CHECK;
	if (auth != NULL || session != 0) {
		putstr(info, ",\"auth\":", false);
		putauth(info, auth, session, afb_auth_No);
	}
	putstr(info, "}", false);
	rc = 0;

end:
	return ret(info, rc);
}

int afb_info_end(
	struct afb_info *info,
	struct afb_data **data
) {
	int rc;
	switch(info->state) {
	case INIT:
		/* bad state */
		rc = X_EINVAL;
		break;

	case GOTSPEC:
		/* make the data from the spec */
		rc = afb_data_create_raw(data, &afb_type_predefined_json,
			info->spec, 1 + strlen(info->spec), NULL, NULL);
		if (rc > 0)
			rc = 0;
		break;

	case GOTAPI:
	case GOTVERB:
		putstr(info, "]}", false);
		if (info->pos >= info->size) {
			if (info->buffer != NULL)
				rc = X_EOVERFLOW;
			else {
				info->buffer = malloc(info->pos + 1);
				if (info->buffer == NULL)
					rc = X_ENOMEM;
				else {
					info->size = info->pos + 1;
					rc = 1; /* again */
				}
			}
		}
		else {
			if (info->buffer == NULL)
				rc = X_EINVAL;
			else {
				/* make the data from the spec */
				rc = afb_data_create_raw(data, &afb_type_predefined_json,
						info->buffer, 1 + info->pos,
						free, info->buffer);
				if (rc > 0)
					rc = 0;
				info->buffer = NULL;
				info->size = 0;
			}
		}
		break;

	default:
		/* had an error */
		rc = info->state;
		break;
	}
	info->state = INIT;
	info->pos = 0;
	if (rc <= 0) {
		free(info->buffer);
		info->buffer = NULL;
		info->size = 0;
	}
	return ret(info, rc);
}


