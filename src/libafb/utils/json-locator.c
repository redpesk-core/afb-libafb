/*
 * Copyright (C) 2015-2022 IoT.bzh Company
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
#include <stdio.h>
#include <limits.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>

#include "json-locator.h"

#if JSON_C_MINOR_VERSION < 13 /************* DONT IMPLEMENT LOCATOR *********/

int json_locator_from_file(struct json_object **jso, const char *filename)
{
	*jso = json_object_from_file(filename);
	return *jso ? 0 : -ENOMEM;
}

const char *json_locator_locate(struct json_object *jso, unsigned *linenum)
{
	return NULL;
}

void json_locator_copy(struct json_object *from, struct json_object *to)
{
}

#else /************* IMPLEMENT LOCATOR *************************/

#define COUNT 2000

struct block
{
	uintptr_t begin;
	uintptr_t end;
	void *tag;
};

struct group
{
	int top;
	struct group *next;
	struct block blocks[COUNT];
};

static struct group *groups_head = NULL;
static void *hooktag;

static struct block *getblocktag(void *ptr, struct group **group)
{
	int idx;
	struct group *grp;
	struct block *blk;

	for (grp = groups_head ; grp ; grp = grp->next) {
		blk = grp->blocks;
		for (idx = grp->top ; idx ; idx--, blk++) {
			if ((uintptr_t)ptr >= blk->begin && (uintptr_t)ptr < blk->end) {
				*group = grp;
				return blk;
			}
		}
	}
	return NULL;
}


static void *searchtag(void *ptr)
{
	struct group *group;
	struct block *block = getblocktag(ptr, &group);

	return block ? block->tag : NULL;
}

static void cleartags()
{
	struct group *group;
	while ((group = groups_head) != NULL) {
		groups_head = group->next;
		free(group);
	}
}


#if __GLIBC_PREREQ(2,34)

static void hook_on(void *tag)
{
}

static void hook_off()
{
}

#else

static void deltag(void *ptr)
{
	struct group *group;
	struct block *block = getblocktag(ptr, &group);

	if (block)
		*block = group->blocks[--group->top];
}


static void addtag(void *ptr, size_t size, void *(*alloc)(size_t))
{
	struct block *block;
	struct group *group;

	group = groups_head;
	if (group && group->top >= (int)(sizeof group->blocks / sizeof *group->blocks))
		group = NULL;
	if (group == NULL) {
		group = alloc(sizeof *group);
		if (group != NULL) {
			group->top = 0;
			group->next = groups_head;
			groups_head = group;
		}
	}
	if (group != NULL) {
		block = &group->blocks[group->top++];
		block->begin = (uintptr_t)ptr;
		block->end = (uintptr_t)ptr + (uintptr_t)size;
		block->tag = hooktag;
	}
}


#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

static void *(*memo_malloc)(size_t size, const void *caller);
static void *(*memo_realloc)(void *ptr, size_t size, const void *caller);
static void *(*memo_memalign)(size_t alignment, size_t size, const void *caller);
static void  (*memo_free)(void *ptr, const void *caller);

static void *my_malloc(size_t size, const void *caller);
static void *my_realloc(void *ptr, size_t size, const void *caller);
static void *my_memalign(size_t alignment, size_t size, const void *caller);
static void  my_free(void *ptr, const void *caller);

static void memorize_hooks()
{
	memo_malloc = __malloc_hook;
	memo_realloc = __realloc_hook;
	memo_memalign = __memalign_hook;
	memo_free = __free_hook;
}

static void restore_hooks()
{
	__malloc_hook = memo_malloc;
	__realloc_hook = memo_realloc;
	__memalign_hook = memo_memalign;
	__free_hook = memo_free;
}

static void set_my_hooks()
{
	__malloc_hook = my_malloc;
	__realloc_hook = my_realloc;
	__memalign_hook = my_memalign;
	__free_hook = my_free;
}

static void *my_malloc(size_t size, const void *caller)
{
	void *result;

	restore_hooks();
	result = malloc(size);
	if (result)
		addtag(result, size, malloc);
	set_my_hooks();
	return result;
}

static void *my_realloc(void *ptr, size_t size, const void *caller)
{
	void *result;

	restore_hooks();
	if (ptr)
		deltag(ptr);
	result = realloc(ptr, size);
	if (result)
		addtag(result, size, malloc);
	else if (ptr)
		addtag(ptr, size, malloc);
	set_my_hooks();
	return result;
}

static void *my_memalign(size_t alignment, size_t size, const void *caller)
{
	void *result;

	restore_hooks();
	result = memalign(alignment, size);
	if (result)
		addtag(result, size, malloc);
	set_my_hooks();
	return result;
}
static void  my_free(void *ptr, const void *caller)
{
	deltag(ptr);
}

static void hook_on(void *tag)
{
	hooktag = tag;
	memorize_hooks();
	set_my_hooks();
}

static void hook_off()
{
	restore_hooks();
	hooktag = NULL;
}
#endif

/**
 * Records the line for a file
 * This record exists in two modes:
 *  - file: with line == 0 and filename0 defined to the filename
 *  - line: with line > 0 and name0 invalid
 */
struct tagline
{
	/** number of the line or 0 if filename */
	unsigned line;

	/** reference count of the structure */
	unsigned refcount;

	/** an other tagline or itself if line == 0 */
	struct tagline *other;

	/** the filename if line == 0, invalid otherwise */
	char filename0[];
};

static void tag_unref(struct tagline *tag)
{
	if (!--tag->refcount) {
		if (tag->other != NULL && tag->other != tag)
			tag_unref(tag->other);
		free(tag);
	}
}

/**
 * Callback deleter function for json's userdata
 *
 * @param jso the json object whose user data is released
 * @param userdata the userdata to release
 */
static void untag_object(struct json_object *jso, void *userdata)
{
	tag_unref((struct tagline *)userdata);
}

/**
 * Tag the objects with their recorded line recursively
 *
 * @param jso the object to tag
 */
static void tag_objects(struct json_object *jso)
{
#if JSON_C_VERSION_NUM >= 0x000d00
	size_t idx, len;
#else
	int idx, len;
#endif
	struct json_object_iterator it, end;
	struct tagline *tag;

	/* nothing to do for nulls */
	if (jso == NULL)
		return;

	/* search the object in tagged blocks */
	tag = searchtag(jso);
	if (tag) {
		/* found, tag the object */
		tag->refcount++;
		json_object_set_userdata(jso, tag, untag_object);
	}

	/* inspect type of the jso */
	switch (json_object_get_type(jso)) {
	case json_type_object:
		it = json_object_iter_begin(jso);
		end = json_object_iter_end(jso);
		while (!json_object_iter_equal(&it, &end)) {
			tag_objects(json_object_iter_peek_value(&it));
			json_object_iter_next(&it);
		}
		break;
	case json_type_array:
		len = json_object_array_length(jso);
		for (idx = 0 ; idx < len ; idx++) {
			tag_objects(json_object_array_get_idx(jso, idx));
		}
		break;
	default:
		break;
	}
}

/**
 * Reads the json file of filename and returns a json object whose userdata
 * records the line and file
 *
 * @param object where to store the read object if returning 0
 * @param filename filename of the file
 * @param file the file to read
 *
 * @return 0 in case of success or -errno
 */
static int get_from_file(struct json_object **object, const char *filename, FILE *file)
{
	int rc;
	int length;
	int stop;
	char *line;
	size_t linesz;
	ssize_t linelen;
	json_tokener *tok;
	struct json_object *obj;
	struct tagline *tagfile, *tagiter, *tagnext;

	/* create the tokenizer */
	tok = json_tokener_new_ex(JSON_TOKENER_DEFAULT_DEPTH);
	if (tok == NULL) {
		rc = -ENOMEM;
		goto end;
	}

	/* create the file tag */
	tagfile = malloc(sizeof *tagfile + 1 + strlen(filename));
	if (tagfile == NULL) {
		rc = -ENOMEM;
		goto end2;
	}
	tagfile->line = 0;
	tagfile->refcount = 0;
	tagfile->other = tagfile;
	strcpy(tagfile->filename0, filename);
	tagiter = tagfile;

	/* read lines */
	line = NULL;
	linesz = 0;
	stop = 0;
	obj = NULL;
	while (!stop) {
		/* read one line */
		linelen = getline(&line, &linesz, file);
		if (linelen < 0) {
			if (!feof(file))
				rc = -errno;
			else {
				hook_on(tagiter);
				obj = json_tokener_parse_ex(tok, "", 1);
				hook_off();
				rc = obj != NULL ? 0 : -EBADMSG;
			}
			stop = 1;
		}
		else {
			/* allocates the tag for the line */
			tagnext = malloc(sizeof *tagnext);
			if (tagnext == NULL) {
				rc = -ENOMEM;
				stop = 1;
			}
			else {
				/* initialize the tagline */
				tagnext->refcount = 0;
				tagnext->other = tagiter;
				tagnext->line = tagiter->line + 1;
				tagiter = tagnext;
				/* scan the line */
				while (!stop && linelen > 0) {
					length = linelen > INT_MAX ? INT_MAX : (int)linelen;
					linelen -= (ssize_t)length;
					hook_on(tagiter);
					obj = json_tokener_parse_ex(tok, line, length);
					hook_off();
					if (obj != NULL) {
						/* tokenizer returned an object */
						rc = 0;
						stop = 1;
					}
					else if (json_tokener_get_error(tok) != json_tokener_continue) {
						/* tokenizer has an error */
						rc = -EBADMSG;
						stop = 1;
					}
				}
			}
		}
	}
	free(line);
	if (rc < 0) {
		/* on error release all tag lines */
		while (tagiter != NULL) {
			tagnext = tagiter->other;
			free(tagiter);
			tagiter = tagnext == tagfile ? NULL : tagnext;
		}
	}
	else {
		/* tag the created objects */
		tag_objects(obj);

		/* clean and update the list of lines */
		while (tagiter != tagfile) {
			tagnext = tagiter->other;
			if (tagiter->refcount == 0)
				free(tagiter);
			else {
				tagfile->refcount++;
				tagiter->other = tagfile;
			}
			tagiter = tagnext;
		}
		if (tagfile->refcount == 0)
			free(tagfile);

		/* record the result */
		*object = obj;
	}
end2:
	json_tokener_free(tok);
end:
	cleartags();
	return rc;
}

/* parse the file of filename and make its json object representation */
int json_locator_from_file(struct json_object **object, const char *filename)
{
	FILE *file;
	int rc;

	/* open */
	*object = NULL;
	file = fopen(filename, "r");
	if (file == NULL)
		rc = -errno;
	else {
		/* read */
		rc = get_from_file(object, filename, file);
		fclose(file);
	}
	return rc;
}

/* return the file and the line of the object jso */
static struct tagline *locator_file_check(struct tagline *tagline)
{
	struct tagline *tagfile;
	if (tagline != NULL && tagline->line > 0 && tagline->refcount > 0) {
		tagfile = tagline->other;
		if (tagfile != NULL && tagfile->line == 0 && tagfile->refcount > 0 && tagfile->other == tagfile)
			return tagfile;
	}
	return NULL;
}

/* return the file and the line of the object jso */
const char *json_locator_locate(struct json_object *jso, unsigned *linenum)
{
	struct tagline *tagfile, *tagline;
	const char *result;
	unsigned line;

	result = NULL;
	line = 0;

	/* read the userata and check it looks like a locator */
	if (jso != NULL) {
		tagline = json_object_get_userdata(jso);
		tagfile = locator_file_check(tagline);
		if (tagfile != NULL) {
			/* yes probably, use it */
			line = tagline->line;
			result = tagfile->filename0;
		}
	}
	if (linenum != NULL)
		*linenum = line;
	return result;
}

/* copy the locator */
void json_locator_copy(struct json_object *from, struct json_object *to)
{
	struct tagline *tagfile, *tagline;

	/* read the userata and check it looks like a locator */
	if (from != NULL && to != NULL) {
		tagline = json_object_get_userdata(from);
		tagfile = locator_file_check(tagline);
		if (tagfile != NULL) {
			/* yes probably, use it */
			tagline->refcount++;
			json_object_set_userdata(to, tagline, untag_object);
		}
	}
}

#endif /************* IMPLEMENT LOCATOR *************************/

/**
 * Structure recording the path path of the expansion
 */
struct path
{
	/** previous, aka parent, path */
	struct path *previous;

	/** key of expanded child if object is an object */
	const char *key;

	/** index of expanded child if object is an array */
	size_t index;
};

/**
 * Compute the length of the string representation of path,
 * add it to the given value and return the sum.
 *
 * @param path the path to compute
 * @param got  the length to be add
 *
 * @return the sum of got and length of the string representation of path
 */
static size_t pathlen(struct path *path, size_t got)
{
	size_t v;

	/* terminal */
	if (path == NULL)
		return got;

	/* add length of current path item */
	if (path->key)
		/* length of ".keyname" */
		got += 1 + strlen(path->key);
	else {
		/* length of "[index]" */
		v = path->index;
		got += 3;
		while (v >= 100) {
			v /= 100;
			got += 2;
		}
		got += v > 9;
	}

	/* recursive computation of the result */
	return pathlen(path->previous, got);
}

/**
 * Put in base the string representation of path and returns a pointer
 * to the end.
 *
 * @param path the path whose string representation is to be computed
 * @param base where to put the string
 *
 * @return a pointer to the character after the end
 */
static char *pathset(struct path *path, char *base)
{
	size_t v;
	char buffer[30]; /* enough for 70 bits (3 * (70 / 10) = 21) */
	int i;

	if (path != NULL) {
		base = pathset(path->previous, base);

		if (path->key) {
			/* put ".key" */
			*base++ = '.';
			base = stpcpy(base,path->key);
		}
		else {
			/* compute reverse string of index in buffer */
			v = path->index;
			i = 0;
			do {
				buffer[i++] = (char)('0' + v % 10);
				v /= 10;
			} while(v);

			/* put "[index]" */
			*base++ = '[';
			while (i)
				*base++ = buffer[--i];
			*base++ = ']';
		}
	}
	return base;
}

/**
 * Search the path to the object 'jso' starting from 'root' that is at path 'previous'
 *
 * @param root current root of the search
 * @param jso  the object whose path is searched
 * @param previous path of root
 *
 * @return an allocated string representation of the path found or NULL
 */
static char *search(struct json_object *root, struct json_object *jso, struct path *previous)
{
#if JSON_C_VERSION_NUM >= 0x000d00
	size_t idx, len;
#else
	int idx, len;
#endif
	struct json_object_iterator it, end;
	struct path path;
	char *result = NULL;

	if (root == jso) {
		result = malloc(pathlen(previous, 1));
		if (result)
			*pathset(previous, result) = 0;
	}
	else if (json_object_is_type(root, json_type_object)) {
		path.index = 0;
		path.previous = previous;
		it = json_object_iter_begin(root);
		end = json_object_iter_end(root);
		while (result == NULL && !json_object_iter_equal(&it, &end)) {
			path.key = json_object_iter_peek_name(&it);
			result = search(json_object_iter_peek_value(&it), jso, &path);
			json_object_iter_next(&it);
		}
	}
	else if (json_object_is_type(root, json_type_array)) {
		path.key = 0;
		path.previous = previous;
		len = json_object_array_length(root);
		for (idx = 0 ; result == NULL && idx < len ; idx++) {
			path.index = (size_t)idx;
			result = search(json_object_array_get_idx(root, idx), jso, &path);
		}
	}
	return result;
}

/* get the path  from root to json or NULL if none exists */
char *json_locator_search_path(struct json_object *root, struct json_object *jso)
{
	return search(root, jso, NULL);
}
