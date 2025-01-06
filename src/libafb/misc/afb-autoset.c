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

#include "../libafb-config.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <rp-utils/rp-verbose.h>

#include "apis/afb-api-ws.h"
#include "apis/afb-api-so.h"
#include "core/afb-apiset.h"
#include "misc/afb-autoset.h"
#include "sys/x-errno.h"

static void cleanup(void *closure)
{
	struct afb_apiset *call_set = closure;
	afb_apiset_unref(call_set);
}

static int onlack(void *closure, struct afb_apiset *set, const char *name, int (*create)(const char *path, struct afb_apiset *declare_set, struct afb_apiset *call_set))
{
	struct afb_apiset *call_set = closure;
	char *path;
	const char *base;
	size_t lbase, lname;

	base = afb_apiset_name(set);
	lbase = strlen(base);
	lname = strlen(name);

	path = alloca(2 + lbase + lname);
	memcpy(path, base, lbase);
	path[lbase] = '/';
	memcpy(&path[lbase + 1], name, lname + 1);

	return create(path, set, call_set);
}

static int add(const char *path, struct afb_apiset *declare_set, struct afb_apiset *call_set, int (*callback)(void *, struct afb_apiset *, const char*))
{
	struct afb_apiset *ownset;

	/* create a sub-apiset */
	ownset = afb_apiset_create_subset_first(declare_set, path, 3600);
	if (!ownset) {
		RP_ERROR("Can't create apiset autoset-ws %s", path);
		return X_ENOMEM;
	}

	/* set the onlack behaviour on this set */
	afb_apiset_onlack_set(ownset, callback, afb_apiset_addref(call_set), cleanup);
	return 0;
}

/*******************************************************************/

#if WITH_WSAPI
static int create_ws(const char *path, struct afb_apiset *declare_set, struct afb_apiset *call_set)
{
	return afb_api_ws_add_client(path, declare_set, call_set, 0) >= 0;
}

static int onlack_ws(void *closure, struct afb_apiset *set, const char *name)
{
	return onlack(closure, set, name, create_ws);
}

int afb_autoset_add_ws(const char *path, struct afb_apiset *declare_set, struct afb_apiset *call_set)
{
	return add(path, declare_set, call_set, onlack_ws);
}
#endif

/*******************************************************************/

#if WITH_DYNAMIC_BINDING
static int create_so(const char *path, struct afb_apiset *declare_set, struct afb_apiset *call_set)
{
	return afb_api_so_add_binding(path, declare_set, call_set) >= 0;
}

static int onlack_so(void *closure, struct afb_apiset *set, const char *name)
{
	return onlack(closure, set, name, create_so);
}

int afb_autoset_add_so(const char *path, struct afb_apiset *declare_set, struct afb_apiset *call_set)
{
	return add(path, declare_set, call_set, onlack_so);
}
#endif

/*******************************************************************/

static int create_any(const char *path, struct afb_apiset *declare_set, struct afb_apiset *call_set)
{
	int rc;
	struct stat st;

	rc = stat(path, &st);
	if (!rc) {
		switch(st.st_mode & S_IFMT) {
#if WITH_DYNAMIC_BINDING
		case S_IFREG:
			rc = afb_api_so_add_binding(path, declare_set, call_set);
			break;
#endif
#if WITH_WSAPI
		case S_IFSOCK: {
			char sockname[PATH_MAX + 7];
			snprintf(sockname, sizeof sockname, "unix:%s", path);
			rc = afb_api_ws_add_client(sockname, declare_set, call_set, 0);
			break;
		}
#endif
		default:
			RP_NOTICE("Unexpected autoset entry: %s", path);
			rc = -errno;
			break;
		}
	}
	return rc >= 0;
}

static int onlack_any(void *closure, struct afb_apiset *set, const char *name)
{
	return onlack(closure, set, name, create_any);
}

int afb_autoset_add_any(const char *path, struct afb_apiset *declare_set, struct afb_apiset *call_set)
{
	return add(path, declare_set, call_set, onlack_any);
}
