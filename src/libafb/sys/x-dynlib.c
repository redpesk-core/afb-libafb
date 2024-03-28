/*
 * Copyright (C) 2015-2024 IoT.bzh Company
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

#define _GNU_SOURCE /* for secure_getenv */

#include "../libafb-config.h"

#if WITH_EXTENSION || WITH_DYNAMIC_BINDING

#include <stdlib.h>
#include <dlfcn.h>

#include "x-dynlib.h"
#include "x-errno.h"

int x_dynlib_open(const char *filename, x_dynlib_t *dynlib, int global, int lazy)
{
	/* compute the dlopen flags */
	int flags = lazy ? RTLD_LAZY : RTLD_NOW;

#if defined(RTLD_DEEPBIND)
	/* For ASan mode, export AFB_NO_RTLD_DEEPBIND=1, to disable RTLD_DEEPBIND */
	char *notdeep = secure_getenv("AFB_NO_RTLD_DEEPBIND");
	if (!notdeep || notdeep[0] != '1' || notdeep[1])
		flags |= RTLD_DEEPBIND;
#endif

	flags |= global ? RTLD_GLOBAL : RTLD_LOCAL;

	/* open the library now */
	dynlib->handle = dlopen(filename, flags);
	return dynlib->handle ? 0 : X_ENODATA;
}

void x_dynlib_close(x_dynlib_t *dynlib)
{
	dlclose(dynlib->handle);
}

int x_dynlib_symbol(x_dynlib_t *dynlib, const char* name, void** ptr)
{
	void *p = dlsym(dynlib->handle, name);
	*ptr = p;
	return p ? 0 : X_ENOENT;
}

const char* x_dynlib_error(const x_dynlib_t *dynlib)
{
	return dlerror();
}

#endif
