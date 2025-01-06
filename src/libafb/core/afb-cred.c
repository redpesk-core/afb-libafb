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

#if WITH_CRED

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>

#include <rp-utils/rp-verbose.h>

#include "core/afb-cred.h"
#include "sys/x-socket.h"
#include "sys/x-errno.h"

#define MAX_LABEL_LENGTH  1024

#if !defined(DEFAULT_PEERSEC_LABEL)
#  define DEFAULT_PEERSEC_LABEL "NoLabel"
#endif
#if !defined(DEFAULT_PEERCRED_UID)
#  define DEFAULT_PEERCRED_UID 99 /* nobody */
#endif
#if !defined(DEFAULT_PEERCRED_GID)
#  define DEFAULT_PEERCRED_GID 99 /* nobody */
#endif
#if !defined(DEFAULT_PEERCRED_PID)
#  define DEFAULT_PEERCRED_PID 0  /* no process */
#endif

static char export_format[] = "%x:%x:%x-%s";
static char import_format[] = "%x:%x:%x-%n";

static struct afb_cred *current;

static struct afb_cred *mkcred(uid_t uid, gid_t gid, pid_t pid, const char *label, size_t size)
{
	struct afb_cred *cred;
	char *dest, user[64];
	size_t i;
	uid_t u;

	i = 0;
	u = uid;
	do {
		user[i++] = (char)('0' + u % 10);
		u = u / 10;
	} while(u && i < sizeof user);

	cred = malloc(2 + i + size + sizeof *cred);
	if (cred) {
		cred->refcount = 1;
		cred->uid = uid;
		cred->gid = gid;
		cred->pid = pid;
		cred->exported = NULL;
		dest = (char*)(&cred[1]);
		cred->user = dest;
		while(i)
			*dest++ = user[--i];
		*dest++ = 0;
		cred->label = dest;
		cred->id = dest;
		memcpy(dest, label, size);
		dest[size] = 0;
		dest = strrchr(dest, ':');
		if (dest)
			cred->id = &dest[1];
	}
	return cred;
}

static struct afb_cred *mkcurrent()
{
	char label[MAX_LABEL_LENGTH];
	int fd;
	ssize_t rc;

	fd = open("/proc/self/attr/current", O_RDONLY);
	if (fd < 0)
		rc = 0;
	else {
		rc = read(fd, label, sizeof label);
		if (rc < 0)
			rc = 0;
		close(fd);
	}

	return mkcred(getuid(), getgid(), getpid(), label, (size_t)rc);
}

int afb_cred_create(struct afb_cred **cred, uid_t uid, gid_t gid, pid_t pid, const char *label)
{
	label = label ? : DEFAULT_PEERSEC_LABEL;
	return (*cred = mkcred(uid, gid, pid, label, strlen(label))) ? 0 : X_ENOMEM;
}

int afb_cred_create_for_socket(struct afb_cred **cred, int fd)
{
	int rc;
	socklen_t length;
	struct ucred ucred;
	char label[MAX_LABEL_LENGTH];

	/* get the credentials */
	length = (socklen_t)(sizeof ucred);
	rc = getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &ucred, &length);
	if (rc < 0 || length != (socklen_t)(sizeof ucred) || !~ucred.uid) {
		ucred.uid = DEFAULT_PEERCRED_UID;
		ucred.gid = DEFAULT_PEERCRED_GID;
		ucred.pid = DEFAULT_PEERCRED_PID;
	}

	/* get the security label */
	length = (socklen_t)(sizeof label);
	rc = getsockopt(fd, SOL_SOCKET, SO_PEERSEC, label, &length);
	if (rc < 0 || length > (socklen_t)(sizeof label)) {
		length = (socklen_t)strlen(DEFAULT_PEERSEC_LABEL);
		strcpy (label, DEFAULT_PEERSEC_LABEL);
	}

	/* makes the result */
	return (*cred = mkcred(ucred.uid, ucred.gid, ucred.pid, label, (size_t)length)) ? 0 : X_ENOMEM;
}

struct afb_cred *afb_cred_addref(struct afb_cred *cred)
{
	if (cred)
		__atomic_add_fetch(&cred->refcount, 1, __ATOMIC_RELAXED);
	return cred;
}

void afb_cred_unref(struct afb_cred *cred)
{
	if (cred && !__atomic_sub_fetch(&cred->refcount, 1, __ATOMIC_RELAXED)) {
		if (cred == current)
			cred->refcount = 1;
		else {
			free((void*)cred->exported);
			free(cred);
		}
	}
}

struct afb_cred *afb_cred_current()
{
	if (!current)
		current = mkcurrent();
	return afb_cred_addref(current);
}

const char *afb_cred_export(struct afb_cred *cred)
{
	int rc;

	if (!cred->exported) {
		rc = asprintf((char**)&cred->exported,
			export_format,
				(int)cred->uid,
				(int)cred->gid,
				(int)cred->pid,
				cred->label);
		if (rc < 0)
			cred->exported = NULL;
	}
	return cred->exported;
}

int afb_cred_import(struct afb_cred **cred, const char *string)
{
	int rc, uid, gid, pid, pos;

	pos = -1;
	rc = sscanf(string, import_format, &uid, &gid, &pid, &pos);
	if (rc != 3 || pos < 0) {
		*cred = NULL;
		return X_EINVAL;

	}
	return afb_cred_create(cred, (uid_t)uid, (gid_t)gid, (pid_t)pid, &string[pos]);
}

#endif
