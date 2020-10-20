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

#if WITH_DYNAMIC_BINDING

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#include "sys/x-dynlib.h"
#include "apis/afb-api-so.h"
#include "apis/afb-api-so-v3.h"
#include "apis/afb-api-so-v4.h"
#include "sys/verbose.h"
#include "core/afb-sig-monitor.h"
#include "utils/path-search.h"

struct safe_dlopen
{
	const char *path;
	x_dynlib_t *dynlib;
	int global;
	int lazy;
	int status;
};

static void safe_dlopen_cb(int sig, void *closure)
{
	struct safe_dlopen *sd = closure;
	if (!sig)
		sd->status = x_dynlib_open(sd->path, sd->dynlib, sd->global, sd->lazy);
	else {
		ERROR("dlopen of %s raised signal %s", sd->path, strsignal(sig));
		sd->status = X_EINTR;
	}
}

static int safe_dlopen(const char *filename, x_dynlib_t *dynlib, int global, int lazy)
{
	struct safe_dlopen sd;
	sd.path = filename;
	sd.global = global;
	sd.lazy = lazy;
	sd.dynlib = dynlib;
	afb_sig_monitor_run(0, safe_dlopen_cb, &sd);
	return sd.status;
}

static int load_binding(const char *path, int force, struct afb_apiset *declare_set, struct afb_apiset * call_set)
{
	int rc;
	x_dynlib_t dynlib;

	// This is a loadable library let's check if it's a binding
	rc = safe_dlopen(path, &dynlib, 0, 0);
	if (rc) {
		_VERBOSE_(
			force ? Log_Level_Error : Log_Level_Notice,
			"binding [%s] not loadable: %s",
				path,
				rc == X_EINTR ? "signal raised" : x_dynlib_error(&dynlib)
		);
		goto error;
	}

	/*
	 * This is a loadable library let's check if it's a binding ...
	 */

	/* try the version 4 */
	rc = afb_api_so_v4_add(path, &dynlib, declare_set, call_set);
	if (rc < 0)
		/* error when loading a valid v4 binding */
		goto error2;

	if (rc)
		return 0; /* yes version 4 */

	/* try the version 3 */
	rc = afb_api_so_v3_add(path, &dynlib, declare_set, call_set);
	if (rc < 0)
		/* error when loading a valid v3 binding */
		goto error2;

	if (rc)
		return 0; /* yes version 3 */

	/* not a valid binding */
	_VERBOSE_(force ? Log_Level_Error : Log_Level_Info, "binding [%s] %s",
					path, "isn't a supported AFB binding");

error2:
	x_dynlib_close(&dynlib);
error:
	return force ? rc : 0;
}


int afb_api_so_add_binding(const char *path, struct afb_apiset *declare_set, struct afb_apiset * call_set)
{
	return load_binding(path, 1, declare_set, call_set);
}

#if WITH_DIRENT

/**
 * internal structure for keeping parameters of load across callbacks
 */
struct search
{
	/** the declare set */
	struct afb_apiset *declare_set;
	/** the call set */
	struct afb_apiset *call_set;
	/** are failures fatal? */
	int failstops;
	/** final status */
	int status;
};

/**
 * callback of files
 */
static int processfiles(void *closure, struct path_search_item *item)
{
	int rc;
	struct search *s = closure;

	/* only try files having ".so" extension */
	if (item->namelen < 3 || memcmp(&item->name[item->namelen - 3], ".so", 4))
		return 0;

	/* try to get it as a binding */
	rc = load_binding(item->path, 0, s->declare_set, s->call_set);
	if (rc >= 0 || !s->failstops)
		return 0; /* got it or fails ignored */

	/* report the error and tell to stop exploration of files */
	s->status = rc;
	return 1;
}

/**
 * function to filter out the directories that must not be entered
 */
static int filterdirs(void *closure, struct path_search_item *item)
{
/*
Exclude from the search of bindings any
directory starting with a dot (.) by default.

It is possible to reactivate the previous behaviour
by defining the following preprocessor variables

 - AFB_API_SO_ACCEPT_DOT_PREFIXED_DIRS

   When this variable is defined, the directories
   starting with a dot are searched except
   if their name is "." or ".." or ".debug"

 - AFB_API_SO_ACCEPT_DOT_DEBUG_DIRS

   When this variable is defined and the variable
   AFB_API_SO_ACCEPT_DOT_PREFIXED_DIRS is also defined
   scans any directory not being "." or "..".

The previous behaviour was like difining the 2 variables,
meaning that only . and .. were excluded from the search.

This change is intended to definitely solve the issue
SPEC-662. Yocto installed the debugging symbols in the
subdirectory .debug. For example the binding.so also
had a .debug/binding.so file attached. Opening that
debug file made dlopen crashing.
See https://sourceware.org/bugzilla/show_bug.cgi?id=22101
 */
	int result;
#if !defined(AFB_API_SO_ACCEPT_DOT_PREFIXED_DIRS) /* not defined by default */
	/* ignore any directory beginning with a dot */
	result = item->name[0] != '.';
#elif  !defined(AFB_API_SO_ACCEPT_DOT_DEBUG_DIRS) /* not defined by default */
	/* ignore directories '.debug' */
	result = strcmp(item->name, ".debug") != 0;
#else
	result = 1;
#endif
	if (result)
		INFO("Scanning dir=[%s] for bindings", item->path);
	return result;
}

int afb_api_so_add_path_search(struct path_search *pathsearch, struct afb_apiset *declare_set, struct afb_apiset *call_set, int failstops)
{
	struct search s = { .declare_set = declare_set, .call_set = call_set, .failstops = failstops, .status = 0 };
	path_search_filter(pathsearch, PATH_SEARCH_FILE|PATH_SEARCH_RECURSIVE|PATH_SEARCH_FLEXIBLE, processfiles, &s, filterdirs);
	return s.status;
}

int afb_api_so_add_pathset(const char *pathset, struct afb_apiset *declare_set, struct afb_apiset * call_set, int failstops)
{
	int rc;
	struct path_search *ps;

	rc = path_search_make_dirs(&ps, pathset);
	if (rc >= 0) {
		rc = afb_api_so_add_path_search(ps, declare_set, call_set, failstops);
		path_search_unref(ps);
	}
	return rc;
}

int afb_api_so_add_pathset_fails(const char *pathset, struct afb_apiset *declare_set, struct afb_apiset * call_set)
{
	return afb_api_so_add_pathset(pathset, declare_set, call_set, 1);
}

int afb_api_so_add_pathset_nofails(const char *pathset, struct afb_apiset *declare_set, struct afb_apiset * call_set)
{
	return afb_api_so_add_pathset(pathset, declare_set, call_set, 0);
}

#endif
#endif
