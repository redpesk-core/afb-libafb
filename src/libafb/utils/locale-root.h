/*
 * Copyright (C) 2015-2023 IoT.bzh Company
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

#pragma once

#include "../libafb-config.h"

struct locale_root;
struct locale_search;

#if WITH_OPENAT
extern struct locale_root *locale_root_create(int dirfd);
extern struct locale_root *locale_root_create_at(int dirfd, const char *path);
#endif

extern struct locale_root *locale_root_create_path(const char *path);
extern struct locale_root *locale_root_addref(struct locale_root *root);
extern void locale_root_unref(struct locale_root *root);

extern struct locale_search *locale_root_search(struct locale_root *root, const char *definition, int immediate);
extern struct locale_search *locale_search_addref(struct locale_search *search);
extern void locale_search_unref(struct locale_search *search);

extern void locale_root_set_default_search(struct locale_root *root, struct locale_search *search);

#if WITH_OPENAT
extern int locale_root_get_dirfd(struct locale_root *root);
#endif

extern const char *locale_root_get_path(struct locale_root *root);

extern int locale_root_open(struct locale_root *root, const char *filename, int flags, const char *locale);
extern char *locale_root_resolve(struct locale_root *root, const char *filename, const char *locale);

extern int locale_search_open(struct locale_search *search, const char *filename, int flags);
extern char *locale_search_resolve(struct locale_search *search, const char *filename);


