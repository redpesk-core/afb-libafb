/*
 * Copyright (C) 2020-2022 IoT.bzh Company
 * Author: José Bollo <jose.bollo@iot.bzh>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, something express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "../libafb-config.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <rp-utils/rp-expand-vars.h>

#include "path-search.h"

#include "sys/x-errno.h"

/**
 * structure for searching files
 */
struct path_search
{
	/** if not nul the path to search before or after */
	struct path_search *parent;
	/** the reference count */
	unsigned short refcount;
	/**
	 * if positive length of the path or
	 * opposite of the length of the length
	 * When positive the path before
	 * When negative path after
	 * if zero it indicates a prefixing
	 * */
	short length;
	/** the path itself */
	char path[];
};

struct path_search *path_search_addref(struct path_search *paths) {
	if (paths)
		paths->refcount++;
	return paths;
}

static void unref(struct path_search *paths) {
	if (!--paths->refcount) {
		if (paths->parent) {
			unref(paths->parent);
		}
		free(paths);
	}
}

void path_search_unref(struct path_search *paths) {
	if (paths)
		unref(paths);
}

static int has(struct path_search *paths, const char *value, short length)
{
	for ( ; paths && paths->length ; paths = paths->parent) {
		short plen = paths->length >= 0 ? paths->length : (short)-paths->length;
		if (plen == length && !memcmp(paths->path, value, (size_t)length))
			return 1;
	}
	return 0;
}

static int add(struct path_search **paths, const char *value, int length)
{
	struct path_search *path;

	if (length > SHRT_MAX)
		return X_EINVAL;

	/* avoiding duplications */
	if (length <= 0 || has(*paths, value, (short)length))
		return 0;

	/* create a new path entry */
	path = malloc(sizeof *path + 1 + (size_t)length);
	if (!path)
		return X_ENOMEM;

	/* initialize */
	path->length = (short)length;
	memcpy(path->path, value, (size_t)length);
	path->path[length] = 0;
	path->parent = *paths;
	path->refcount = 1;
	*paths = path;
	return 0;
}

/**
 * make the search path from dirs.
 */
static int make(struct path_search **paths, const char *dirs, int reverse)
{
	int rc;
	char *where;

	/* search the first colon */
	where = strchr(dirs, ':');
	if (!where) {
		/* no colon just add it */
		rc = add(paths, dirs, (int)strlen(dirs));
	}
	else {
		/* create recursively */
		if (reverse) {
			/* reverse the order of the directories */
			rc = add(paths, dirs, (int)(where - dirs));
			if (rc >= 0) {
				rc = make(paths, &where[1], reverse);
			}
		}
		else {
			/* direct order of the directories */
			rc = make(paths, &where[1], reverse);
			if (rc >= 0) {
				rc = add(paths, dirs, (int)(where - dirs));
			}
		}
	}
	return rc;
}

/**
 * Add 'dirs' before or after 'other' and returns it in 'paths'.
 */
static int add_dir_expanded(struct path_search **paths, const char *dirs, int before, struct path_search *parent)
{
	int rc;
	struct path_search *head, *iter, *tail, *tmp;

	head = 0;
	rc = make(&head, dirs, before);

	if (rc < 0) {
		path_search_unref(head);
		*paths = 0;
	}
	else if (!head) {
		*paths = path_search_addref(parent);
	}
	else {
		/* compute values to set */
		if (before) {
			/* reverse the list */
			tail = head;
			iter = head->parent;
			while (iter) {
				tmp = iter->parent;
				iter->parent = head;
				head = iter;
				iter = tmp;
			}
		}
		else {
			/* change the lengths */
			iter = head;
			do {
				tail = iter;
				iter->length = (short)-iter->length;
				iter = iter->parent;
			} while(iter);
		}
		/* set the values */
		tail->parent = path_search_addref(parent);
		*paths = head;
	}
	return rc;
}

int path_search_make_dirs(struct path_search **paths, const char *dirs)
{
	return path_search_add_dirs(paths, dirs, 1, 0);
}

int path_search_make_env(struct path_search **paths, const char *var)
{
	return path_search_add_env(paths, var, 1, 0);
}

int path_search_add_dirs(struct path_search **paths, const char *dirs, int before, struct path_search *other)
{
	char *ex = rp_expand_vars_env_only(dirs, 0);
	int rc = add_dir_expanded(paths, ex ?: dirs, before, other);
	free(ex);
	return rc;
}

int path_search_add_env(struct path_search **paths, const char *var, int before, struct path_search *other)
{
	const char *value = getenv(var);
	if (value)
		return path_search_add_dirs(paths, value, before, other);

	*paths = path_search_addref(other);
	return 0;
}

int path_search_extend_dirs(struct path_search **paths, const char *dirs, int before)
{
	int rc;
	struct path_search *p;

	rc = path_search_add_dirs(&p, dirs, before, *paths);
	if (rc >= 0) {
		path_search_unref(*paths);
		*paths = p;
	}
	return rc;
}

int path_search_extend_env(struct path_search **paths, const char *var, int before)
{
	int rc;
	struct path_search *p;

	rc = path_search_add_env(&p, var, before, *paths);
	if (rc >= 0) {
		path_search_unref(*paths);
		*paths = p;
	}
	return rc;
}

void path_search_list(struct path_search *paths, void (*callback)(void*,const char*), void *closure)
{
	if (paths->length > 0) {
		callback(closure, paths->path);
		if (paths->parent)
			path_search_list(paths->parent, callback, closure);
	}
	else if (paths->length < 0) {
		if (paths->parent)
			path_search_list(paths->parent, callback, closure);
		callback(closure, paths->path);
	}
	else {
		/* TODO: PREFIXING */
	}
}

#if WITH_DIRENT

/**
 * back link to detect loops
 */
struct direntlist {
	/** the directory entry */
	struct dirent *ent;
	/** the previous entry in the list */
	struct direntlist *prv;
};

/**
 * search data
 */
struct search {
	/** Original search object */
	struct path_search *paths;

	/** flags of the search */
	int flags;

	/** Callback to be invoked for each entry found */
	int (*callback)(void*,struct path_search_item*);

	/** Closure of the callback */
	void *closure;

	/** Callback to be invoked before entring a directory */
	int (*filter)(void*,struct path_search_item*);

	/** link to the previous entry */
	struct direntlist *prv;

	/** Item passed to the callback */
	struct path_search_item item;

	/** path of the currently scanned file */
	char path[PATH_MAX];
};

/**
 * recursive search
 *
 * @param search the search structure
 */
static int search_in_dir(struct search *search)
{
	struct direntlist dl, *idl;
	struct stat st;
	short pos, len;
	int stop, type;
	char *name;
	DIR *dir;

	/* init and open directory */
	stop = 0;
	dir = opendir(search->path);
	if (dir) {
		/* prepare to name files in path */
		dl.prv = search->prv;
		pos = search->item.pathlen;
		if (pos && search->path[pos - 1] != '/')
			search->path[pos++] = '/';
		name = &search->path[pos];

		/* loop on each entry */
		while (!stop && (dl.ent = readdir(dir))) {

			/* detection of loops */
			for (idl = dl.prv ; idl && idl->ent->d_ino != dl.ent->d_ino ; idl = idl->prv);
			if (idl)
				continue;

			/* copy name if no overflow */
			len = (short)strlen(dl.ent->d_name);
			if (len + pos >= (int)sizeof(search->path)) {
				/* overflow detected */
				continue;
			}
			memcpy(name, dl.ent->d_name, 1 + (size_t)len);

			/* get type after dereferencing if link */
			type = dl.ent->d_type;
			if (type == DT_LNK && stat(search->path, &st) == 0) {
				switch (st.st_mode & S_IFMT) {
				case S_IFREG:
					type = DT_REG;
					break;
				case S_IFDIR:
					type = DT_DIR;
					break;
				default:
					break;
				}
			}

			/* prepare entry */
			search->item.name = name;
			search->item.namelen = len;
			search->item.pathlen = (short)(pos + len);

			/* inspect type */
			if (type == DT_REG) {
				if (search->flags & PATH_SEARCH_FILE) {
					/* regular file, use the callback */
					search->item.isDir = 0;
					stop = search->callback(search->closure, &search->item);
				}
			}
			else if (type == DT_DIR && (name[0] != '.' || (len > 1 && (name[1] != '.' || len > 2)))) {
				if (search->flags & PATH_SEARCH_DIRECTORY) {
					/* regular file, use the callback */
					search->item.isDir = 1;
					stop = search->callback(search->closure, &search->item);
				}
				if (!stop && (search->flags & PATH_SEARCH_RECURSIVE)
					&& (!search->filter || search->filter(search->closure, &search->item))) {
					/* directory in recusive mode */
					search->item.pathlen = (short)(pos + len);
					search->prv = &dl;
					stop = search_in_dir(search);
				}
			}
		}
		closedir(dir);
	}
	else if ((search->flags & (PATH_SEARCH_FLEXIBLE|PATH_SEARCH_FILE)) == (PATH_SEARCH_FLEXIBLE|PATH_SEARCH_FILE)
	      && errno == ENOTDIR
	      && stat(search->path, &st) == 0
	      && (st.st_mode & S_IFMT) == S_IFREG) {
		/* flexible and regular file, use the callback */
		name = strrchr(search->path, '/');
		if (!name) {
			search->item.name = search->path;
			search->item.namelen = search->item.pathlen;
		}
		else {
			search->item.name = ++name;
			search->item.namelen = (short)(search->item.pathlen - (name - search->path));
		}
		search->item.isDir = 0;
		stop = search->callback(search->closure, &search->item);
	}

	return stop;
}

static int searchrec(struct path_search *paths, struct search *search)
{
	unsigned short length;
	int stop;

	if (paths->length > 0) {
		/* searching before parents */
		length = (unsigned short)paths->length;
		search->item.pathlen = (short)length;
		memcpy(search->path, paths->path, 1 + (size_t)length);
		search->prv = 0;
		stop = search_in_dir(search);
		if (!stop && paths->parent) {
			stop = searchrec(paths->parent, search);
		}
	}
	else if (paths->length < 0) {
		/* searching after parents */
		stop = paths->parent ? searchrec(paths->parent, search) : 0;
		if (!stop) {
			length = (unsigned short)-paths->length;
			search->item.pathlen = (short)length;
			memcpy(search->path, paths->path, 1 + (size_t)length);
			search->prv = 0;
			stop = search_in_dir(search);
		}
	}
	else {
		/* LENGTH = 0, prefixing, TODO */
		stop = 0;
	}
	return stop;
}

void path_search_filter(struct path_search *paths, int flags, int (*callback)(void*,struct path_search_item*), void *closure, int (*filter)(void*,struct path_search_item*))
{
	struct search search = {
		.paths = paths,
		.flags = flags & (PATH_SEARCH_FILE|PATH_SEARCH_DIRECTORY) ? flags : flags|PATH_SEARCH_FILE,
		.callback = callback,
		.closure = closure,
		.filter = filter,
		.prv = 0,
		.item.pathlen = 0,
		.item.path = search.path
	};

	searchrec(paths, &search);
}

void path_search(struct path_search *paths, int flags, int (*callback)(void*,struct path_search_item*), void *closure)
{
	return path_search_filter(paths, flags, callback, closure, 0);
}

/**
 * Simple structure for searching a file
 */
struct searchfile {
	/** Original search object */
	struct path_search *paths;

	/** Search parameter */
	const void *parameter;

	/** Name of the file searched */
	int (*match)(const void*, struct path_search_item *item);

	/** Callback to create the result from the path */
	int (*load)(void **,const char*);

	/** the result */
	void *result;

	/** the status */
	int status;
};

/** simple callback for searching a file */
static int searchfilecb(void *closure, struct path_search_item *item)
{
	struct searchfile *sf = closure;

	if (sf->match(sf->parameter, item)) {
		sf->status = sf->load(&sf->result, item->path);
		return 1;
	}
	return 0;
}

/** search a file by its name */
static int searchfile(
	struct path_search *paths,
	void **result,
	int rec,
	const void *parameter,
	int (*match)(const void*,struct path_search_item*),
	int (*load)(void **,const char*)
) {
	struct searchfile sf = {
		.paths = paths,
		.parameter = parameter,
		.match = match,
		.load = load,
		.result = 0,
		.status = X_ENOENT
	};
	int flags = rec ? PATH_SEARCH_RECURSIVE|PATH_SEARCH_FILE : PATH_SEARCH_FILE;
	path_search(paths, flags, searchfilecb, &sf);
	*result = sf.result;
	return sf.status;
}


/** simple callback for matching the name */
static int matchnamecb(const void *param, struct path_search_item *item)
{
	return 0 == strcmp((const char*)param, item->name);
}

/** search a file by its name */
static int searchfilename(struct path_search *paths, void **result, int rec, const char *name, const char *extension, int (*load)(void **,const char*))
{
	const char *param;
	if (!extension) {
		param = name;
	}
	else {
		param = alloca(strlen(name) + strlen(extension) + 1);
		strcpy(stpcpy((char*)param, name), extension);
	}
	return searchfile(paths, result, rec, param, matchnamecb, load);
}

/* load procedure for getting the path */
static int get_path(void **result, const char *path)
{

	return (*result = strdup(path)) ? 0 : X_ENOMEM;
}

int path_search_get_path(struct path_search *paths, char **path, int rec, const char *name, const char *extension)
{
	return searchfilename(paths, (void**)path, rec, name, extension, get_path);
}

/* load procedure for getting the content */
static int get_content(void **result, const char *path)
{
	off_t off, pos;
	ssize_t srd;
	char *content;
	int rc, fd;

	content = 0;
	fd = open(path, O_RDONLY);
	if (fd < 0)
		rc = -errno;
	else {
		off = lseek(fd, 0, SEEK_END);
		if (off < 0)
			rc = -errno;
		else {
			content = malloc((size_t)(off + 1));
			if (!content)
				rc = X_ENOMEM;
			else {
				content[off] = 0;
				lseek(fd, 0, SEEK_SET);
				for (pos = 0 ; pos < off ; pos = lseek(fd, 0, SEEK_CUR)) {
					srd = read(fd, &content[pos], (size_t)(off - pos));
					if (srd < 0 && errno != EINTR) {
						free(content);
						content = 0;
						break;
					}
					if (srd == 0) {
						/* unexpected end of file! */
						content[pos] = 0;
						break;
					}
				}
				rc = 0;
			}
		}
		close(fd);
	}
	*result = content;
	return rc;
}

int path_search_get_content(struct path_search *paths, char **content, int rec, const char *name, const char *extension)
{
	return searchfilename(paths, (void**)content, rec, name, extension, get_content);
}

#endif
