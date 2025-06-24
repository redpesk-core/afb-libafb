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


#include <stdlib.h>
#include <string.h>
#include <alloca.h>

#include "utils/globset.h"
#include "utils/globmatch.h"
#include "sys/x-errno.h"
#include "sys/x-alloca.h"

/*************************************************************************
 * internal types
 ************************************************************************/

/**
 * internal structure for handling patterns
 */
struct pathndl
{
	/* link to the next handler of the list */
	struct pathndl *next;

	/* the hash value for the pattern */
	size_t hash;

	/* the handler */
	struct globset_handler handler;
};

/**
 * Structure that handles a set of global pattern handlers
 */
struct globset
{
	/** linked list of global patterns */
	struct pathndl *globs;

	/** hash dictionary of exact matches */
	struct pathndl **exacts;

	/** mask used for accessing dictionary of exact matches */
	unsigned gmask;

	/** count of handlers stored in the dictionary of exact matches */
	unsigned count;
};

/**
 * Normalize the string 'from' to the string 'to' and computes the hash code.
 * The normalization translates upper characters to lower characters.
 * The returned hash code is greater than zero for exact patterns or zero
 * for global patterns.
 *
 * @param from string to normalize
 * @param to where to store the normalization
 * @return 0 if 'from' is a glob pattern or the hash code for exacts patterns
 */
static unsigned normhash(const char *from, char *to)
{
	unsigned i, hash;
	int isglob;
	char c;

	isglob = 0; /* is glob? */
	i = hash = 0; /* index and hash code */

	/* copy the normalized string and compute isglob and hash */
	while ((c = from[i])) {
		if (c == GLOB) {
			/* it is a glob pattern */
			isglob = 1;
			hash = 0;
		} else {
			/* normalize to lower */
			if (c >= 'A' && c <= 'Z')
				c = (char)(c + 'a' - 'A');
			/* compute hash if not glob */
			if (!isglob) {
				hash += (unsigned)(unsigned char)c;
				hash += hash << 10;
				hash ^= hash >> 6;
			}
		}
		to[i++] = c;
	}
	to[i] = c;
	if (!isglob) {
		/* finalize hash if not glob */
		hash += i;
		hash += hash << 3;
		hash ^= hash >> 11;
		hash += hash << 15;
		if (hash == 0)
			hash = 1 + i;
	}
	return hash;
}

/**
 * Ensure that there is enough place to store one more exact pattern
 * in the dictionary.
 *
 * @param set the set that has to grow if needed
 * @return 0 in case of success or -1 on case of error
 */
static int grow(struct globset *set)
{
	unsigned old_gmask, new_gmask, i;
	struct pathndl **new_exacts, **old_exacts, *ph, *next_ph;

	/* test if grow is needed */
	old_gmask = set->gmask;
	if (old_gmask - (old_gmask >> 2) > set->count + 1)
		return 0;

	/* compute the new mask */
	new_gmask = (old_gmask << 1) | 15;
	if (new_gmask + 1 <= new_gmask)
		return X_EOVERFLOW;

	/* allocates the new array */
	new_exacts = calloc(1 + (size_t)new_gmask, sizeof *new_exacts);
	if (!new_exacts)
		return X_ENOMEM;

	/* copy values */
	old_exacts = set->exacts;
	set->exacts = new_exacts;
	set->gmask = new_gmask;
	if (old_gmask) {
		for(i = 0 ; i <= old_gmask ; i++) {
			ph = old_exacts[i];
			while (ph) {
				next_ph = ph->next;
				ph->next = new_exacts[ph->hash & new_gmask];
				new_exacts[ph->hash & new_gmask] = ph;
				ph = next_ph;
			}
		}
		free(old_exacts);
	}

	/* release the previous values */
	return 0;
}

/**
 * Search in set the handler for the normalized pattern 'normal' of 'has' code.
 *
 * @param set the set to search
 * @param normal the normalized pattern
 * @param hash hash code of the normalized pattern
 * @param pprev pointer where to store the pointer pointing to the returned result
 * @return the handler found, can be NULL
 */
static struct pathndl *search(
		struct globset *set,
		const char *normal,
		unsigned hash,
		struct pathndl ***pprev)
{
	struct pathndl *ph, **pph;

	if (!hash)
		pph = &set->globs;
	else if (set->exacts)
		pph = &set->exacts[hash & set->gmask];
	else {
		*pprev = NULL;
		return NULL;
	}
	while ((ph = *pph) && strcmp(normal, ph->handler.pattern))
		pph = &ph->next;
	*pprev = pph;
	return ph;
}

/**
 * Allocates a new set of handlers
 *
 * @return the new allocated global pattern set of handlers or NULL on failure
 */
struct globset *globset_create()
{
	return calloc(1, sizeof(struct globset));
}

/**
 * Destroy the set
 * @param set the set to destroy
 */
void globset_destroy(struct globset *set)
{
	unsigned i;
	struct pathndl *ph, *next_ph;

	/* free exact pattern handlers */
	if (set->gmask) {
		for(i = 0 ; i <= set->gmask ; i++) {
			ph = set->exacts[i];
			while (ph) {
				next_ph = ph->next;
				free(ph);
				ph = next_ph;
			}
		}
		free(set->exacts);
	}

	/* free global pattern handlers */
	ph = set->globs;
	while (ph) {
		next_ph = ph->next;
		free(ph);
		ph = next_ph;
	}

	/* free the set */
	free(set);
}

/**
 * Add a handler for the given pattern
 * @param set the set
 * @param pattern the pattern of the handler
 * @param callback the handler's callback
 * @param closure the handler's closure
 * @return 0 in case of success or -1 in case of error (ENOMEM, EEXIST)
 */
int globset_add(
		struct globset *set,
		const char *pattern,
		void *callback,
		void *closure)
{
	struct pathndl *ph, **pph;
	unsigned hash;
	size_t len;
	char *pat;

	/* normalize */
	len = strlen(pattern);
	pat = alloca(1 + len);
	hash = normhash(pattern, pat);

	/* grows if needed */
	if (hash && grow(set))
		return -1;

	/* search */
	ph = search(set, pat, hash, &pph);
	if (ph)
		/* found */
		return X_EEXIST;

	/* not found, create it */
	ph = malloc(1 + len + sizeof *ph);
	if (!ph)
		return X_ENOMEM;

	/* initialize it */
	ph->next = NULL;
	ph->hash = hash;
	ph->handler.callback = callback;
	ph->handler.closure = closure;
	strcpy(ph->handler.pattern, pat);
	*pph = ph;
	if (hash)
		set->count++;
	return 0;
}

/**
 * Delete the handler for the pattern
 * @param set the set
 * @param pattern the pattern to delete
 * @param closure where to put the closure if not NULL
 * @return 0 in case of success or -1 in case of error (ENOENT)
 */
int globset_del(
			struct globset *set,
			const char *pattern,
			void **closure)
{
	struct pathndl *ph, **pph;
	unsigned hash;
	char *pat;

	/* normalize */
	pat = alloca(1 + strlen(pattern));
	hash = normhash(pattern, pat);

	/* search */
	ph = search(set, pat, hash, &pph);
	if (!ph)
		/* not found */
		return X_ENOENT;

	/* found, remove it */
	if (hash)
		set->count--;
	*pph = ph->next;

	/* store the closure back */
	if (closure)
		*closure = ph->handler.closure;

	/* free the memory */
	free(ph);
	return 0;
}

/**
 * Search the handler of 'pattern'
 * @param set the set
 * @param pattern the pattern to search
 * @return the handler found or NULL
 */
struct globset_handler *globset_search(
			struct globset *set,
			const char *pattern)
{
	struct pathndl *ph, **pph;
	unsigned hash;
	char *pat;

	/* local normalization */
	pat = alloca(1 + strlen(pattern));
	hash = normhash(pattern, pat);

	/* search */
	ph = search(set, pat, hash, &pph);
	return ph ? &ph->handler : NULL;
}

/**
 * Search a handler for the string 'text'
 * @param set the set
 * @param text the text to match
 * @return the handler found or NULL
 */
const struct globset_handler *globset_match(
			struct globset *set,
			const char *text)
{
	struct pathndl *ph, *iph;
	unsigned hash, g, s;
	char *txt;

	/* local normalization */
	txt = alloca(1 + strlen(text));
	hash = normhash(text, txt);

	/* first, look in dictionary of exact matches */
	ph = NULL;
	if (hash && set->exacts) {
		ph = set->exacts[hash & set->gmask];
		while(ph && (ph->hash != hash || strcmp(txt, ph->handler.pattern)))
			ph = ph->next;
	}

	/* then if not found */
	if (ph == NULL) {
		/* look in glob patterns for the best match */
		s = 0;
		iph = set->globs;
		while(iph) {
			g = globmatch(iph->handler.pattern, txt);
			if (g > s) {
				s = g;
				ph = iph;
			}
			iph = iph->next;
		}
	}
	return ph ? &ph->handler : NULL;
}

