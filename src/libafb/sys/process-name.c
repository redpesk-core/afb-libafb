/*
 * Copyright (C) 2015-2020 IoT.bzh Company
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

#include <string.h>
#include <unistd.h>
#include <sys/prctl.h>
#include "sys/x-errno.h"

#include "sys/process-name.h"

int process_name_set_name(const char *name)
{
	return prctl(PR_SET_NAME, name, 0, 0, 0) < 0 ? -errno : 0;
}

int process_name_replace_cmdline(char **argv, const char *name)
{
	char *beg, *end, **av, c;

	/* update the command line */
	av = argv;
	if (!av)
		return X_EINVAL; /* no command line update required */

	/* longest prefix */
	end = beg = *av;
	while (*av)
		if (*av++ == end)
			while(*end++)
				;
	if (end == beg)
		return X_EINVAL; /* nothing to change */

	/* patch the command line */
	av = &argv[1];
	end--;
	while (beg != end && (c = *name++)) {
		if (c != ' ' || !*av)
			*beg++ = c;
		else {
			*beg++ = 0;
			*av++ = beg;
		}
	}
	/* terminate last arg */
	if (beg != end)
		*beg++ = 0;
	/* inform system */
	prctl(PR_SET_MM, PR_SET_MM_ARG_END, (unsigned long)(intptr_t)beg, 0, 0);
	/* update remaining args (for keeping initial length correct) */
	while (*av)
		*av++ = beg;
	/* fulfill last arg with spaces */
	while (beg != end)
		*beg++ = ' ';
	*beg = 0;

	return 0;
}

