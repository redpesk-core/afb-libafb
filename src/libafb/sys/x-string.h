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

#if HAVENT_strcasecmp
#include <ctype.h>
static inline int strcasecmp(const char *s1, const char *s2)
{
	char c1 = *s1;
	char c2 = *s2;
	int r = toupper(c1) - toupper(c2);
	while(c1 && c2 && r == 0) {
		c1 = *++s1;
		c2 = *++s2;
		r = toupper(c1) - toupper(c2);
	}
	return r;
}
#endif

#if HAVENT_stpcpy
static inline char *stpcpy(char *dest, const char *src)
{
	while((*dest = *src))
		dest++, src++;
	return dest;
}
#endif

#if HAVENT_strchrnul
static inline char *strchrnul(const char *s, int c)
{
	while (*s && *s != (char)c)
		s++;
	return (char*)s;
}
#endif

#if HAVENT_strdup
static inline char *strdup(const char *s)
{
	size_t l = strlen(s) + 1;
	char *r = malloc(l);
	return r ? memcpy(r, s, l) : r;
}
#endif

#if HAVENT_strndupa
#include <alloca.h>
#define strndup(s,n) strncpy(alloca((n)+1), (s), (n))
#endif

#if HAVENT_strdupa
#define strdupa(s) strcpy(alloca(strlen(s)+1), (s))
#endif
