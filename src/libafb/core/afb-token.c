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


#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#include "core/afb-token.h"

/**
 * structure for recording a token
 */
struct afb_token
{
	/** link to the next token of the list */
	struct afb_token *next;

	/** reference of the token */
	uint16_t refcount;

	/** local numeric id of the token */
	uint16_t id;

	/** string value of the token */
	char text[];
};

struct tokenset
{
	struct afb_token *first;
	pthread_mutex_t mutex;
	uint16_t idgen;
};

static struct tokenset tokenset = {
	.first = 0,
	.mutex = PTHREAD_MUTEX_INITIALIZER,
	.idgen = 0
};

static struct afb_token *searchid(uint16_t id)
{
	struct afb_token *r = tokenset.first;
	while (r && r->id != id)
		r = r->next;
	return r;
}

/**
 * Get a token for the given value
 *
 * @param token address to return the pointer to the gotten token
 * @param tokenstring string value of the token to get
 * @return 0 in case of success or a -errno like negative code
 */
int afb_token_get(struct afb_token **token, const char *tokenstring)
{
	int rc;
	struct afb_token *tok;
	size_t length;

	/* get length of the token string */
	length = strlen(tokenstring);

	/* concurrency */
	pthread_mutex_lock(&tokenset.mutex);

	/* search the token */
	tok = tokenset.first;
	while (tok && (memcmp(tokenstring, tok->text, length) || tokenstring[length]))
		tok = tok->next;

	/* search done */
	if (tok) {
		/* found */
		tok = afb_token_addref(tok);
		rc = 0;
	} else {
		/* not found, create */
		tok = malloc(length + 1 + sizeof *tok);
		if (!tok)
			/* creation failed */
			rc = -ENOMEM;
		else {
			while(!++tokenset.idgen || searchid(tokenset.idgen));
			tok->next = tokenset.first;
			tokenset.first = tok;
			tok->id = tokenset.idgen;
			tok->refcount = 1;
			memcpy(tok->text, tokenstring, length + 1);
			rc = 0;
		}
	}
	pthread_mutex_unlock(&tokenset.mutex);
	*token = tok;
	return rc;
}

/**
 * Add a reference count to the given token
 *
 * @param token the token to reference
 * @return the token with the reference added
 */
struct afb_token *afb_token_addref(struct afb_token *token)
{
	if (token)
		__atomic_add_fetch(&token->refcount, 1, __ATOMIC_RELAXED);
	return token;
}

/**
 * Remove a reference to the given token and clean the memory if needed
 *
 * @param token the token that is unreferenced
 */
void afb_token_unref(struct afb_token *token)
{
	struct afb_token **pt;
	if (token && !__atomic_sub_fetch(&token->refcount, 1, __ATOMIC_RELAXED)) {
		pthread_mutex_lock(&tokenset.mutex);
		pt = &tokenset.first;
		while (*pt && *pt != token)
			pt = &(*pt)->next;
		if (*pt)
			*pt = token->next;
		pthread_mutex_unlock(&tokenset.mutex);
		free(token);
	}
}

/**
 * Get the string value of the token
 *
 * @param token the token whose string value is queried
 * @return the string value of the token
 */
const char *afb_token_string(const struct afb_token *token)
{
	return token->text;
}

/**
 * Get the "local" numeric id of the token
 *
 * @param token the token whose id is queried
 * @return the numeric id of the token
 */
uint16_t afb_token_id(const struct afb_token *token)
{
	return token->id;
}
