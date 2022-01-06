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

#pragma once

#include <libafb/libafb-config.h>

struct path_search;

extern struct path_search *path_search_addref(struct path_search *paths);
extern void path_search_unref(struct path_search *paths);

extern int path_search_make_dirs(struct path_search **paths, const char *dirs);
extern int path_search_make_env(struct path_search **paths, const char *var);

extern int path_search_add_dirs(struct path_search **paths, const char *dirs, int before, struct path_search *other);
extern int path_search_add_env(struct path_search **paths, const char *var, int before, struct path_search *other);

extern int path_search_extend_dirs(struct path_search **paths, const char *dirs, int before);
extern int path_search_extend_env(struct path_search **paths, const char *var, int before);

extern void path_search_list(struct path_search *paths, void (*callback)(void*,const char*), void *closure);

#if WITH_DIRENT

/**
 * searching flags
 */
#define PATH_SEARCH_RECURSIVE   1
#define PATH_SEARCH_FILE        2
#define PATH_SEARCH_DIRECTORY   4
#define PATH_SEARCH_FLEXIBLE    8

/**
 * structure received by the callback
 */
struct path_search_item
{
  /** path to the item */
  const char *path;

  /** filename (last part of path) */
  const char *name;

  /** length of the path */
  short pathlen;

  /** length of the name */
  short namelen;

  /** is item a directory */
  short isDir;
};

extern void path_search_filter(struct path_search *paths, int flags, int (*callback)(void*,struct path_search_item*), void *closure, int (*filter)(void*,struct path_search_item*));
extern void path_search(struct path_search *paths, int flags, int (*callback)(void*,struct path_search_item*), void *closure);
extern int path_search_get_path(struct path_search *paths, char **path, int rec, const char *name, const char *extension);
extern int path_search_get_content(struct path_search *paths, char **content, int rec, const char *name, const char *extension);

#endif
