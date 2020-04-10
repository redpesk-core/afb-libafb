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

#if WITH_CRED

#include <sys/types.h>

struct afb_context;

struct afb_cred
{
	int refcount;
	uid_t uid;
	gid_t gid;
	pid_t pid;
	const char *user;
	const char *label;
	const char *id;
	const char *exported;
};

extern struct afb_cred *afb_cred_current();


extern int afb_cred_create(struct afb_cred **cred, uid_t uid, gid_t gid, pid_t pid, const char *label);
extern int afb_cred_create_for_socket(struct afb_cred **cred, int fd);
extern struct afb_cred *afb_cred_addref(struct afb_cred *cred);
extern void afb_cred_unref(struct afb_cred *cred);

extern const char *afb_cred_export(struct afb_cred *cred);
extern int afb_cred_import(struct afb_cred **cred, const char *string);

#endif
