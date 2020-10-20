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


#pragma once

#if WITH_DYNAMIC_BINDING

struct afb_apiset;

extern int afb_api_so_add_binding(const char *path, struct afb_apiset *declare_set, struct afb_apiset * call_set);

#if WITH_DIRENT
struct path_search;
extern int afb_api_so_add_path_search(struct path_search *pathsearch, struct afb_apiset *declare_set, struct afb_apiset *call_set, int failstops);
extern int afb_api_so_add_pathset(const char *pathset, struct afb_apiset *declare_set, struct afb_apiset * call_set, int failstops);
extern int afb_api_so_add_pathset_fails(const char *pathset, struct afb_apiset *declare_set, struct afb_apiset * call_set);
extern int afb_api_so_add_pathset_nofails(const char *pathset, struct afb_apiset *declare_set, struct afb_apiset * call_set);
#endif

#endif
