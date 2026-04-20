/*
 * Copyright (C) 2015-2026 IoT.bzh Company
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

#include "core/afb-auth.h"

#include <stdlib.h>
#include <stdbool.h>

#include "core/afb-v4-itf.h"

/*
 * This structure is used for creating list of auth items.
 * It is used by unfold and for is the input object
 * of functions printing lists.
 * This structure is also used by validation for detection of recursivity.
 * The linking field is named previous to record fact that auth values
 * are recorded in the reverse order
 */
struct authlst
{
	/* auth value */
	const struct afb_auth *auth;
	/* previous item of the list */
	const struct authlst *previous;
};

/*
 * This structure is used for processing next value of an
 * auth that has 2 children (types afb_auth_Or and afb_auth_And)
 */
struct unfold_next
{
	/* the auth that hold the next auth to process in auth->next */
	const struct afb_auth *auth;
	/* record of the completion function to call */
	void (*done)(void *closure, unsigned session, const struct authlst *list);
	/* closure of done */
	void *closure;
};

/*
 * This structure is used for writing the string representation of auths
 */
struct writer
{
	/* putter of strings */
	void (*put)(void *closure, const char *text);
	/* closure of put */
	void *closure;
};

/* pre-declarations */

static void unfold_next(
	void *closure,
	unsigned session,
	const struct authlst *list
);

static void write_any(
	struct writer *writer,
	const struct afb_auth *auth,
	unsigned session,
	afb_auth_type_t type
);

/*
 * Validate the auth.
 * This validation includes the detection of recursivity
 * though usage of authlst.
 * Returns true if auth is valid, false if invalid.
 */
static bool auth_valid(
	const struct afb_auth *auth,
	const struct authlst *previous
) {
	struct authlst ufo;
	const struct authlst *iter;

	/* not NULL */
	if (auth == NULL)
		return false;

	switch (auth->type) {
	case afb_auth_No:
	case afb_auth_Token:
	case afb_auth_Yes:
	case afb_auth_LOA:
		return true;
	case afb_auth_Permission:
		return auth->text != NULL;
	case afb_auth_Or:
	case afb_auth_And:
	case afb_auth_Not:
		/* detect recursion */
		for (iter = previous ; iter != NULL ; iter = iter->previous)
			if (iter->auth == auth)
				return false;
		ufo = (struct authlst){ auth, previous };
		/* validate children */
		return auth_valid(auth->first, &ufo)
			&& (auth->type == afb_auth_Not
				|| auth_valid(auth->next, &ufo));
	default:
		/* bad type */
		return false;
	}
}

/*
 * create reverse list of content of nodes of the given type
 */
static void unfold(
	const struct afb_auth *auth,
	unsigned session,
	const struct authlst *list,
	afb_auth_type_t type,
	void (*done)(void *closure, unsigned session, const struct authlst *list),
	void *closure
) {
	/* check if needed */
	bool needed = false;
	if (auth != NULL)
		switch (auth->type) {
		case afb_auth_Token:
			needed = (session & AFB_SESSION_CHECK) == 0;
			break;
		case afb_auth_LOA:
			needed = (session & AFB_SESSION_LOA_MASK) < auth->loa;
			break;
		default:
			needed = true;
		}

	if (!needed )
		/* done if not needed */
		done(closure, session, list);
	else if (auth->type != type) {
		/* add to the auth in list and done */
		struct authlst ufo = { auth, list };
		done(closure, session, &ufo);
	}
	else {
		/* unfold content */
		struct unfold_next nxt = { auth, done, closure };
		unfold(auth->first, session, list, type, unfold_next, &nxt);
	}
}

/* auxilary function for unfolding */
static void unfold_next(
	void *closure,
	unsigned session,
	const struct authlst *list
) {
	const struct unfold_next *nxt = closure;
	unfold(nxt->auth->next, session, list, nxt->auth->type, nxt->done, nxt->closure);
}

/* helper for reverse write of a list of auth values */
static void write_list_previous(
	struct writer *writer,
	unsigned session,
	const struct authlst *list,
	afb_auth_type_t type
) {
	if (list != NULL) {
		write_list_previous(writer, session, list->previous, type);
		write_any(writer, list->auth, session, type);
		writer->put(writer->closure, type == afb_auth_And ? " and " : " or ");
	}
}

/* reverse write of a list of auth values */
static void write_list(
	struct writer *writer,
	unsigned session,
	const struct authlst *list,
	afb_auth_type_t type,
	bool par
) {
	if (list == NULL)
		writer->put(writer->closure, type == afb_auth_And ? "yes" : "no");
	else {
		par = par && list->previous != NULL;
		if (par)
			writer->put(writer->closure, "(");
		write_list_previous(writer, session, list->previous, type);
		write_any(writer, list->auth, session, type);
		if (par)
			writer->put(writer->closure, ")");
	}
}

/* unfold callback for writing AND */
static void write_and_no_par(
	void *closure,
	unsigned session,
	const struct authlst *list
) {
	write_list(closure, session, list, afb_auth_And, false);
}

/* unfold callback for writing in bracket AND */
static void write_and(
	void *closure,
	unsigned session,
	const struct authlst *list
) {
	write_list(closure, session, list, afb_auth_And, true);
}

/* unfold callback for writing OR */
static void write_or_no_par(
	void *closure,
	unsigned session,
	const struct authlst *list
) {
	write_list(closure, session, list, afb_auth_Or, false);
}

/* unfold callback for writing in bracket OR */
static void write_or(
	void *closure,
	unsigned session,
	const struct authlst *list
) {
	write_list(closure, session, list, afb_auth_Or, true);
}

/* write a uint */
static void write_uint(
	struct writer *writer,
	unsigned val
) {
	unsigned quot = val / 10, rem = val % 10;
	char buffer[2] = { (char)('0' + rem), 0 };
	if (quot)
		write_uint(writer, quot);
	writer->put(writer->closure, buffer);
}

/* write any value */
static void write_any(
	struct writer *writer,
	const struct afb_auth *auth,
	unsigned session,
	afb_auth_type_t type
) {
	switch (auth->type) {
	case afb_auth_No:
		writer->put(writer->closure, "no");
		break;
	case afb_auth_Token:
		writer->put(writer->closure, "check-token");
		break;
	case afb_auth_LOA:
		writer->put(writer->closure, "loa>=");
		write_uint(writer, auth->loa);
		break;
	case afb_auth_Permission:
		writer->put(writer->closure, auth->text);
		break;
	case afb_auth_Or:
		unfold(auth, session, NULL, afb_auth_Or, write_or, writer);
		break;
	case afb_auth_And:
		if ((session & AFB_SESSION_CHECK) != 0 || !afb_auth_check_token(auth))
			unfold(auth, session, NULL, afb_auth_And, write_and, writer);
		else {
			struct afb_auth auth_check = { .type = afb_auth_Token };
			struct authlst ufo_check = { .auth = &auth_check, .previous = NULL };

			unfold(auth, session | AFB_SESSION_CHECK, &ufo_check,
					afb_auth_And, write_and, writer);
		}
		break;
	case afb_auth_Not:
		if (auth->first->type == afb_auth_Not)
			auth = auth->first;
		else
			writer->put(writer->closure, "not ");
		write_any(writer, auth->first, session, afb_auth_Not);
		break;
	case afb_auth_Yes:
		writer->put(writer->closure, "yes");
		break;
	}
}

/* put the auth string */
void afb_auth_put_string(
	const struct afb_auth *auth,
	unsigned session,
	void (*put)(void *closure, const char *text),
	void *closure
) {
	struct afb_auth auth_loa, auth_check;
	struct authlst ufo_loa, ufo_check;
	const struct authlst *pufo = NULL;
	struct writer writer = { put, closure };

	session &= AFB_SESSION_LOA_MASK | AFB_SESSION_CHECK;

	/* consolidate session with content of auth */
	if (auth != NULL) {
		unsigned loa, check;
		loa = afb_auth_minloa(auth);
		if (loa < (session & AFB_SESSION_LOA_MASK))
			loa = session & AFB_SESSION_LOA_MASK;
		check = afb_auth_check_token(auth) | (session & AFB_SESSION_CHECK);
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
	if (auth == NULL)
		write_and_no_par(&writer, session, pufo);
	else if (pufo != NULL || auth->type != afb_auth_Or)
		unfold(auth, session, pufo, afb_auth_And, write_and_no_par, &writer);
	else
		unfold(auth, session, pufo, afb_auth_Or, write_or_no_par, &writer);
}

/* compute if auth is valid */
int afb_auth_is_valid(const struct afb_auth *auth)
{
	return (int)auth_valid(auth, NULL);
}

/* compute the minimum value of loa */
unsigned afb_auth_minloa(const struct afb_auth *auth)
{
	unsigned a, b;

	if (auth == NULL)
		return 0;

	switch (auth->type) {
	case afb_auth_LOA:
		return auth->loa;

	case afb_auth_Or:
		a = afb_auth_minloa(auth->first);
		b = afb_auth_minloa(auth->next);
		return a < b ? a : b;

	case afb_auth_And:
		a = afb_auth_minloa(auth->first);
		b = afb_auth_minloa(auth->next);
		return a > b ? a : b;

	default:
		return 0;
	}
}

/* compute if session check is required*/
unsigned afb_auth_check_token(const struct afb_auth *auth)
{
	unsigned a, b;

	if (auth == NULL)
		return 0;

	switch (auth->type) {
	case afb_auth_Token:
	case afb_auth_Permission:
		return AFB_SESSION_CHECK;

	case afb_auth_Or:
		a = afb_auth_check_token(auth->first);
		b = afb_auth_check_token(auth->next);
		return a & b;

	case afb_auth_And:
		a = afb_auth_check_token(auth->first);
		b = afb_auth_check_token(auth->next);
		return a | b;

	case afb_auth_Not:
		return afb_auth_check_token(auth->first);

	default:
		return 0;
	}
}

