/*
 Copyright (C) 2015-2022 IoT.bzh Company

 Author: José Bollo <jose.bollo@iot.bzh>

 $RP_BEGIN_LICENSE$
 Commercial License Usage
  Licensees holding valid commercial IoT.bzh licenses may use this file in
  accordance with the commercial license agreement provided with the
  Software or, alternatively, in accordance with the terms contained in
  a written agreement between you and The IoT.bzh Company. For licensing terms
  and conditions see https://www.iot.bzh/terms-conditions. For further
  information use the contact form at https://www.iot.bzh/contact.

 GNU General Public License Usage
  Alternatively, this file may be used under the terms of the GNU General
  Public license version 3. This license is as published by the Free Software
  Foundation and appearing in the file LICENSE.GPLv3 included in the packaging
  of this file. Please review the following information to ensure the GNU
  General Public License requirements will be met
  https://www.gnu.org/licenses/gpl-3.0.html.
 $RP_END_LICENSE$
*/

#include "libafb-config.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "utils/locale-root.h"
#include "sys/subpath.h"
#include "sys/x-errno.h"

/*
 * Implementation of folder based localisation as described here:
 *
 *    https://www.w3.org/TR/widgets/#folder-based-localization
 */

#define LRU_COUNT         3
#define DEFAULT_IMMEDIATE 0
#define LIST_LENGTH       200

static const char _locales_[] = "locales/";

/***************************************************************/

struct locale_search {
	struct locale_root *root;
	int refcount;
	int llen;
	char list[];
};

struct locale_root {
#if LRU_COUNT
	struct locale_search *lru[LRU_COUNT];
#endif
	struct locale_search *default_search;
	int refcount;
	int intcount;
#if WITH_OPENAT
	int rootfd;
#endif
	char path[1];
};

/***************************************************************/

/*
 */
static struct locale_root *locale_root_alloc_for_path(const char *path, size_t length)
{
	struct locale_root *root;

	length -= (length && path[length - 1] == '/');
	root = malloc(1 + length + sizeof *root);
	if (root) {
		memset(root, 0, sizeof *root);
		root->refcount = 1;
		root->intcount = 1;
		memcpy(root->path, path, length);
		root->path[length] = '/';
		root->path[length + 1] = 0;
	}
	return root;
}

#if WITH_OPENAT
/*
 */
static struct locale_root *locale_root_alloc_for_dirfd(int dirfd)
{
	ssize_t sz;
	char fdp[100];
	char buffer[PATH_MAX];
	struct locale_root *root;

	sprintf(fdp, "/proc/self/fd/%d", dirfd);
	sz = readlink(fdp, buffer, sizeof buffer);
	if (sz < 0 || (size_t)sz >= sizeof buffer - 1)
		root = NULL;
	else {
		root = locale_root_alloc_for_path(buffer, (size_t)sz);
		if (root)
			root->rootfd = dirfd;
	}
	return root;
}

/*
 * Creates a locale root handler and returns it or return NULL
 * in case of memory depletion.
 */
struct locale_root *locale_root_create(int dirfd)
{
	struct locale_root *root;

	root = locale_root_alloc_for_dirfd(dirfd);
	return root;
}

/*
 * Creates a locale root handler and returns it or return NULL
 * in case of memory depletion.
 */
struct locale_root *locale_root_create_at(int dirfd, const char *path)
{
	int fd;
	struct locale_root *root;

	fd = openat(dirfd, path, O_PATH|O_DIRECTORY);
	if (fd < 0)
		root =  NULL;
	else {
		root = locale_root_create(fd);
		if (root == NULL)
			close(fd);
	}
	return root;
}

/*
 * Creates a locale root handler and returns it or return NULL
 * in case of memory depletion.
 */
struct locale_root *locale_root_create_path(const char *path)
{
	return locale_root_create_at(AT_FDCWD, path);
}
#else
/*
 * Creates a locale root handler and returns it or return NULL
 * in case of memory depletion.
 */
struct locale_root *locale_root_create_path(const char *path)
{
	struct locale_root *root;

	root = locale_root_alloc_for_path(path, strlen(path));
	return root;
}
#endif

/*
 * Adds a reference to 'root'
 */
struct locale_root *locale_root_addref(struct locale_root *root)
{
	__atomic_add_fetch(&root->refcount, 1, __ATOMIC_RELAXED);
	return root;
}

/*
 * Drops an internal reference to 'root' and destroys it
 * if not more referenced
 */
static void internal_unref(struct locale_root *root)
{
	if (!__atomic_sub_fetch(&root->intcount, 1, __ATOMIC_RELAXED)) {
#if WITH_OPENAT
		close(root->rootfd);
#endif
		free(root);
	}
}

/*
 * Drops a reference to 'root' and destroys it
 * if not more referenced
 */
void locale_root_unref(struct locale_root *root)
{

	if (root && !__atomic_sub_fetch(&root->refcount, 1, __ATOMIC_RELAXED)) {
#if LRU_COUNT
		size_t i;
		/* clear circular references through searchs */
		for (i = 0 ; i < LRU_COUNT ; i++)
			locale_search_unref(root->lru[i]);
#endif
		/* finalize if needed */
		internal_unref(root);
	}
}

#if WITH_OPENAT
/*
 * Get the filedescriptor for the 'root' directory
 */
int locale_root_get_dirfd(struct locale_root *root)
{
	return root->rootfd;
}
#endif

/*
 * Get the filedescriptor for the 'root' directory
 */
const char *locale_root_get_path(struct locale_root *root)
{
	return root->path;
}

/***************************************************************/

/*
 */
static int is_in_search_list(char *list, int llen, const char *item, int ilen)
{
	char c, d;
	int i, j, k;

	for (i = 0 ; i < llen ; i = k + 1) {
		for(k = i ; list[k] ; k++);
		if (ilen == k - i) {
			for(j = 0 ; j < ilen ; j++) {
				c = list[i + j];
				d = item[j];
				d = (char)tolower((int)d);
				if (d != c)
					return 0;
			}
			return 1;
		}
	}
	return 0;
}

/*
 */
static int add_search_list(char *list, int llen, int off, const char *item, int ilen, int rec)
{
	char c;
	int i, j;

	/* remove any tailing dash */
	while(ilen && item[ilen - 1] == '-')
		ilen--;

	/* ignore empty items */
	if (ilen == 0)
		return off;

	/* search latest dash */
	j = -1;
	if (rec > 0) {
		for (i = 0 ; i < ilen ; i++) {
			if (item[i] == '-')
				j = i;
		}
	}

	/* check if already set */
	if (is_in_search_list(list, off, item, ilen)) {
		if (j <= 0)
			return off;
	}

	/* check room for item */
	if (off + ilen < llen) {
		/* copy the item */
		for (i = 0 ; i < ilen ; i++) {
			c = item[i];
			c = (char)tolower((int)c);
			list[off++] = c;
		}
		list[off++] = 0;
	}

	/* append prefixes if required */
	if (j > 0)
		off = add_search_list(list, llen, off, item, j, rec - 1);

	return off;
}

/*
 */
static int make_search_list(char *list, int llen, const char *def, int immediate)
{
	int i, j, l, r, dlen;
	char c;

	/* build the search from the definition */
	dlen = (int)strlen(def);
	r = 0;
	for (i = 0 ; i < dlen ; ) {
		c = def[i];
		if (c <= ' ' || c == ',' || c == ';')
			i++;
		else {
			/* search end of the definition */
			for (j = i + 1 ; j < dlen ; j++) {
				c = def[j];
				if (c <= ' ' || c == ',' || c == ';')
					break;
			}
			l = j - i;
			r = add_search_list(list, llen, r, &def[i], l, immediate ? l  : 0);
			for (i = j ; i < dlen ;) {
				c = def[i++];
				if (c == ',')
					break;
			}
		}
	}

	/* fullfills the search */
	if (!immediate) {
		for (i = 0 ; i < r ; ) {
			for (j = i ; list[j++];);
			for (l = j - 2 ; l > i ; l--) {
				if (list[l] == '-') {
					l -= i;
					r = add_search_list(list, llen, r, &list[i], l, l);
					break;
				}
			}
			i = j;
		}
	}

	return r;
}

/*
 * construct a search for the given root and definition of length
 */
static struct locale_search *create_search(struct locale_root *root, const char *list, int llen, int immediate)
{
	struct locale_search *search;

	/* allocate the structure */
	search = malloc(sizeof *search + (size_t)llen);
	if (search) {
		/* init */
		__atomic_add_fetch(&root->intcount, 1, __ATOMIC_RELAXED);
		search->root = root;
		search->refcount = 1;
		memcpy(search->list, list, (size_t)llen);
		search->llen = llen;
	}
	return search;
}

/***************************************************************/

/*
 * Check if a possibly NUUL search matches the definition of length
 */
static inline int search_matches(struct locale_search *search, const char *list, int llen)
{
	return search != NULL
	    && search->llen == llen
	    && !memcmp(search->list, list, (size_t)llen);
}

/*
 * Get an instance of search for the given root and definition
 * The flag immediate affects how the search is built.
 * For example, if the definition is "en-US,en-GB,en", the result differs depending on
 * immediate or not:
 *  when immediate==0 the search becomes "en-US,en-GB,en"
 *  when immediate==1 the search becomes "en-US,en,en-GB" because en-US is immediately downgraded to en
 */
struct locale_search *locale_root_search(struct locale_root *root, const char *definition, int immediate)
{
#if LRU_COUNT
	size_t i;
	struct locale_search *search;
#endif
	char list[LIST_LENGTH];
	int llen;

	/* normalize the definition */
	llen = definition ? make_search_list(list, (int)sizeof list, definition, immediate) : 0;

#if LRU_COUNT
	/* search lru entry */
	i = 0;
	while (i < LRU_COUNT && !search_matches(root->lru[i], list, llen))
		i++;

	/* get the entry */
	if (i < LRU_COUNT) {
		/* use an existing one */
		search = root->lru[i];
	} else {
		/* create a new one */
		search = create_search(root, list, llen, immediate);
		if (search == NULL)
			return NULL;
		/* drop the oldest reference and update i */
		locale_search_unref(root->lru[--i]);
	}

	/* manage the LRU */
	while (i > 0) {
		root->lru[i] = root->lru[i - 1];
		i = i - 1;
	}
	root->lru[0] = search;

	/* returns a new instance */
	return locale_search_addref(search);
#else
	return create_search(root, list, llen, immediate);
#endif
}

/*
 * Adds a reference to the search
 */
struct locale_search *locale_search_addref(struct locale_search *search)
{
	__atomic_add_fetch(&search->refcount, 1, __ATOMIC_RELAXED);
	return search;
}

/*
 * Removes a reference from the search
 */
void locale_search_unref(struct locale_search *search)
{
	if (search && !__atomic_sub_fetch(&search->refcount, 1, __ATOMIC_RELAXED)) {
		internal_unref(search->root);
		free(search);
	}
}

/*
 * Set the default search of 'root' to 'search'.
 * This search is used as fallback when other search are failing.
 */
void locale_root_set_default_search(struct locale_root *root, struct locale_search *search)
{
	struct locale_search *older;

	assert(search == NULL || search->root == root);

	older = root->default_search;
	root->default_search = search ? locale_search_addref(search) : NULL;
	locale_search_unref(older);
}

/***************************************************************/

#define GIVEN_LOCALE    0
#define DEFAULT_LOCALE  1
#define NO_LOCALE       2

struct iter
{
	struct locale_root *root;
	const char *filename;
	const char *list;
	int state;
	int llen;
	int offset;
	int flen;
	char path[PATH_MAX];
};

static int iter_init(struct iter *iter, const char *filename, struct locale_root *root)
{
	int offset;

	/* check the path and normalize it */
	filename = subpath_force(filename);
	if (filename == NULL)
		return X_EINVAL;

	iter->root = root;
	iter->filename = filename;
	iter->flen = (int)strlen(filename) + 1;
	iter->state = GIVEN_LOCALE;
#if WITH_OPENAT
	offset = 0;
#else
	offset = (int)(stpcpy(iter->path, root->path) - iter->path);
#endif
	memcpy(&iter->path[offset], _locales_, sizeof _locales_ - 1);
	iter->offset = offset + (int)(sizeof _locales_ - 1);
	iter->llen = 0;
	return 0;
}

static void iter_list_set(struct iter *iter, const char *list, int llen)
{
	iter->list = list;
	iter->llen = llen;
}

static void iter_list_next(struct iter *iter)
{
	int l = 1;
	while(iter->list[l++]);
	iter_list_set(iter, iter->list + l, iter->llen - l);
}

static int iter_set(struct iter *iter)
{
	const char *item;
	char *dest = &iter->path[iter->offset];
	int ilen;

	if (iter->llen)
		item = iter->list;
	else
		item = NULL;

	if (item) {
		ilen = (int)strlen(item);
		if (iter->offset + ilen + iter->flen >= (int)sizeof iter->path)
			return 0;
		memcpy(dest, item, (size_t)ilen);
		dest[ilen] = '/';
		dest = &dest[ilen + 1];
	} else {
		if (iter->offset + iter->flen >= (int)sizeof iter->path)
			return 0;
	}
	memcpy(dest, iter->filename, (size_t)iter->flen);
	return 1;
}

static int iter_next(struct iter *iter)
{
	struct locale_search *search;

	if (iter->llen != 0) {
		iter_list_next(iter);
		if (iter->llen != 0)
			return 1;
	}
	if (iter->state == GIVEN_LOCALE) {
		iter->state = DEFAULT_LOCALE;
		search = iter->root->default_search;
		if (search) {
			iter_list_set(iter, search->list, search->llen);
			if (iter->llen != 0)
				return 1;
		}
	}
	if (iter->state == DEFAULT_LOCALE) {
		iter->state = NO_LOCALE;
		iter->offset -= (int)(sizeof _locales_ - 1);
		iter->llen = 0;
		return 1;
	}

	return 0;
}

static int iter_list_init(struct iter *iter, const char *filename, struct locale_root *root, const char *list, int llen)
{
	int rc = iter_init(iter, filename, root);
	iter_list_set(iter, list, llen);
	if (iter->llen == 0)
		rc = iter_next(iter) ? 0 : X_EINVAL;
	return rc;
}

static int iter_search_init(struct iter *iter, const char *filename, struct locale_root *root, struct locale_search *search)
{
	int rc = iter_init(iter, filename, root);
	if (search)
		iter_list_set(iter, search->list, search->llen);
	if (iter->llen == 0)
		rc = iter_next(iter) ? 0 : X_EINVAL;
	return rc;
}

/***************************************************************/

/*
 */
static int iter_open(struct iter *iter, int flags)
{
	int fd;

	do {
		if (iter_set(iter)) {
#if WITH_OPENAT
			fd = openat(iter->root->rootfd, iter->path, flags);
#else
			fd = open(iter->path, flags);
#endif
			if (fd >= 0)
				return fd;
		}
	} while(iter_next(iter));
	return -1;
}

static int open_list(struct locale_root *root, const char *filename, int flags, const char *list, int llen)
{
	struct iter iter;
	int rc;

	/* no creation flags accepted */
	if ((flags & O_CREAT) != 0)
		rc = X_EINVAL;
	else {
		rc = iter_list_init(&iter, filename, root, list, llen);
		if (rc == 0)
			rc = iter_open(&iter, flags);
	}
	return rc;
}

static int open_search(struct locale_root *root, const char *filename, int flags, struct locale_search *search)
{
	struct iter iter;
	int rc;

	/* no creation flags accepted */
	if ((flags & O_CREAT) != 0)
		rc = X_EINVAL;
	else {
		rc = iter_search_init(&iter, filename, root, search);
		if (rc == 0)
			rc = iter_open(&iter, flags);
	}
	return rc;
}

/*
 * Opens 'filename' after search.
 *
 * Returns the file descriptor as returned by openat
 * system call or -1 in case of error.
 */
int locale_search_open(struct locale_search *search, const char *filename, int flags)
{
	return open_search(search->root, filename, flags, search);
}

/*
 * Opens 'filename' for root with default search.
 *
 * Returns the file descriptor as returned by openat
 * system call or -1 in case of error.
 */
int locale_root_open(struct locale_root *root, const char *filename, int flags, const char *locale)
{
	char list[LIST_LENGTH];
	int llen;

	/* default search ? */
	if (!locale)
		return open_search(root, filename, flags, NULL);

	/* normalize the definition */
	llen = make_search_list(list, (int)sizeof list, locale, DEFAULT_IMMEDIATE);
	return open_list(root, filename, flags, list, llen);
}

/***************************************************************/

/*
 */
static char *iter_resolve(struct iter *iter)
{
	int rc;
	do {
		if (iter_set(iter)) {
#if WITH_OPENAT
			rc = faccessat(iter->root->rootfd, iter->path, F_OK, 0);
#else
			rc = access(iter->path, F_OK);
#endif
			if (rc == 0)
				return strdup(iter->path);
		}
	} while(iter_next(iter));
	return NULL;
}

static char *resolve_list(struct locale_root *root, const char *filename, const char *list, int llen)
{
	struct iter iter;

	return iter_list_init(&iter, filename, root, list, llen)
		? NULL : iter_resolve(&iter);
}

static char *resolve_search(struct locale_root *root, const char *filename, struct locale_search *search)
{
	struct iter iter;

	return iter_search_init(&iter, filename, root, search)
		? NULL : iter_resolve(&iter);
}

/*
 * Resolves 'filename' after 'search'.
 *
 * returns a copy of the filename after search or NULL if not found.
 * the returned string MUST be freed by the caller (using free).
 */
char *locale_search_resolve(struct locale_search *search, const char *filename)
{
	return resolve_search(search->root, filename, search);
}

/*
 * Resolves 'filename' at 'root' after default search.
 *
 * returns a copy of the filename after search or NULL if not found.
 * the returned string MUST be freed by the caller (using free).
 */
char *locale_root_resolve(struct locale_root *root, const char *filename, const char *locale)
{
	char list[LIST_LENGTH];
	int llen;

	/* default search ? */
	if (!locale)
		return resolve_search(root, filename, NULL);

	/* normalize the definition */
	llen = make_search_list(list, (int)sizeof list, locale, DEFAULT_IMMEDIATE);
	return resolve_list(root, filename, list, llen);
}

/***************************************************************/

#if defined(TEST_locale_root)
int main(int ac,char**av)
{
#if WITH_OPENAT
	struct locale_root *root = locale_root_create(AT_FDCWD);
#else
	struct locale_root *root = locale_root_create_path(".");
#endif
	struct locale_search *search = NULL;
	int fd, rc, i;
	char buffer[256];
	char *subpath;
	while (*++av) {
		if (**av == '@') {
			locale_search_unref(search);
			search = NULL;
			locale_root_unref(root);
#if WITH_OPENAT
			root = locale_root_create_at(AT_FDCWD, *av + 1);
#else
			root = locale_root_create_path(*av + 1);
#endif
			if (root == NULL)
				fprintf(stderr, "can't create root at %s: %m\n", *av + 1);
			else
				printf("root: %s\n", *av + 1);
		} else {
			if (root == NULL) {
				fprintf(stderr, "no valid root for %s\n", *av);
			} else if (**av == '-' || **av == '+') {
				locale_search_unref(search);
				search = locale_root_search(root, *av + 1, **av == '+');
				if (search == NULL)
					fprintf(stderr, "can't create search for %s: %m\n", *av + 1);
				else
					printf("search: %s\n", *av + 1);
			} else if (search == NULL) {
				fprintf(stderr, "no valid search for %s\n", *av);
			} else {
				fd = locale_search_open(search, *av, O_RDONLY);
				if (fd < 0)
					fprintf(stderr, "can't open file %s: %m\n", *av);
				else {
					subpath = locale_search_resolve(search, *av);
					if (subpath == NULL)
						fprintf(stderr, "can't resolve file %s: %m\n", *av);
					else {
						rc = (int)read(fd, buffer, sizeof buffer - 1);
						if (rc < 0)
							fprintf(stderr, "can't read file %s: %m\n", *av);
						else {
							buffer[rc] = 0;
							*strchrnul(buffer, '\n') = 0;
							printf("%s -> %s [%s]\n", *av, subpath, buffer);
						}
						free(subpath);
					}
					close(fd);
				}
			}
		}
	}
	locale_search_unref(search); search = NULL;
	locale_root_unref(root); root = NULL;
}
#endif
