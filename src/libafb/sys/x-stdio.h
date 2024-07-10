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

#if HAVENT_vasprintf
#include <stdio.h>
#include <stdarg.h>
static inline int vasprintf(char **strp, const char *fmt, va_list ap)
{
	int l;
	char *buffer;
	va_list ap2;
	va_copy(ap2, ap);
	l = vsnprintf((char*)&l, 0, fmt, ap);
	if (l >= 0) {
		*strp = buffer = malloc(1 + (size_t)l);
		if (buffer)
			l = vsnprintf(buffer, 1 + (size_t)l, fmt, ap2);
		else
			l = -1;
	}
	return l;
}
#endif

#if HAVENT_asprintf
#include <stdarg.h>
static inline int asprintf(char **strp, const char *fmt, ...)
{
	int l;
	va_list ap;
	va_start(ap, fmt);
	l = vasprintf(strp, fmt, ap);
	va_end(ap);
	return l;
}
#endif
