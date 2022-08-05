/*
 * Copyright (C) 2015-2022 IoT.bzh Company
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

#define GLOB    '*'

extern unsigned globmatch(const char *pat, const char *str);
extern unsigned globmatchi(const char *pat, const char *str);

#if WITH_FNMATCH

#include <fnmatch.h>

#else

#define	FNM_PATHNAME	(1 << 0)	/* No wildcard can ever match `/'.  */
#define	FNM_NOESCAPE	(1 << 1)	/* Backslashes don't quote special chars.  */
#define	FNM_PERIOD	(1 << 2)	/* Leading `.' is matched only explicitly.  */
#define FNM_FILE_NAME	FNM_PATHNAME	/* Preferred GNU name.  */
#define FNM_LEADING_DIR (1 << 3)	/* Ignore `/...' after a match.  */
#define FNM_CASEFOLD	(1 << 4)	/* Compare without regard to case.  */
#define FNM_EXTMATCH	(1 << 5)	/* Use ksh-like extended matching. */
#define	FNM_NOMATCH	1

extern int fnmatch(const char *pattern, const char *string, int flags);

#endif
