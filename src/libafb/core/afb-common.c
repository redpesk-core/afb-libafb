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

#include "afb-config.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "core/afb-common.h"
#include "utils/locale-root.h"

static const char *default_locale = NULL;
static struct locale_root *rootdir = NULL;

void afb_common_default_locale_set(const char *locale)
{
	default_locale = locale;
}

const char *afb_common_default_locale_get()
{
	return default_locale;
}

int afb_common_rootdir_set(const char *dirname)
{
	int rc;
	struct locale_root *root;
	struct locale_search *search;

	rc = -1;
	root = locale_root_create_path(dirname);
	if (root == NULL) {
		/* TODO message */
	} else {
		rc = 0;
		if (default_locale != NULL) {
			search = locale_root_search(root, default_locale, 0);
			if (search == NULL) {
				/* TODO message */
			} else {
				locale_root_set_default_search(root, search);
				locale_search_unref(search);
			}
		}
		locale_root_unref(rootdir);
		rootdir = root;
	}
	return rc;
}

#if WITH_OPENAT
int afb_common_rootdir_get_fd()
{
	return locale_root_get_dirfd(rootdir);
}
#endif

const char *afb_common_rootdir_get_path()
{
	return locale_root_get_path(rootdir);
}

int afb_common_rootdir_open_locale(const char *filename, int flags, const char *locale)
{
	return locale_root_open(rootdir, filename, flags, locale);
}


