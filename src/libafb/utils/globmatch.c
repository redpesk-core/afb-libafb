/*
 * Copyright (C) 2015-2021 IoT.bzh Company
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

#include <ctype.h>

#include "utils/globmatch.h"

#define TOLOWER(ch)        (tolower((int)(unsigned char)(ch)))
#define EQ(flags,cha,chb)  ((flags & FNM_CASEFOLD) ? \
				(TOLOWER(cha) == TOLOWER(chb)) : (cha == chb))

/**
 * Matches whether the string 'str' matches the pattern 'pat'
 * and returns its matching score.
 *
 * @param pat the glob pattern
 * @param str the string to match
 * @return 0 if no match or number representing the matching score
 */
static unsigned match(const char *pat, const char *str, int flags)
{
	unsigned r, rs, rr;
	char c, x;

	/* scan the prefix without glob */
	r = 1;
	while ((c = *pat++) != GLOB) {
		x = *str++;
		if (!EQ(flags,c,x))
			return 0; /* no match */
		if (!c)
			return r; /* match up to end */
		r++;
	}

	/* glob found */
	c = *pat++;
	if (!c) {
		/* not followed by pattern */
		if (flags & FNM_PATHNAME) {
			while(*str)
				if (*str++ == '/')
					return 0;
		}
		return r;
	}

	/* evaluate the best score for following pattern */
	rs = 0;
	while (*str) {
		x = *str++;
		if (EQ(flags,c,x)) {
			/* first char matches, check remaining string */
			rr = match(pat, str, flags);
			if (rr > rs)
				rs = rr;
		} else if ((flags & FNM_PATHNAME) && x == '/')
			return 0;
	}

	/* best score or not match if rs == 0 */
	return rs ? rs + r : 0;
}

/**
 * Matches whether the string 'str' matches the pattern 'pat'
 * and returns its matching score.
 *
 * @param pat the glob pattern
 * @param str the string to match
 * @return 0 if no match or number representing the matching score
 */
unsigned globmatch(const char *pat, const char *str)
{
	return match(pat, str, 0);
}

/**
 * Matches whether the string 'str' matches the pattern 'pat'
 * and returns its matching score.
 *
 * @param pat the glob pattern
 * @param str the string to match
 * @return 0 if no match or number representing the matching score
 */
unsigned globmatchi(const char *pat, const char *str)
{
	return match(pat, str, FNM_CASEFOLD);
}

#if !WITH_FNMATCH
int fnmatch(const char *pattern, const char *string, int flags)
{
	return !match(pattern, string, flags);
}
#endif
