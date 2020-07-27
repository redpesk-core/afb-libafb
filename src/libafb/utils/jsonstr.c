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

#include "libafb-config.h"

#include <string.h>

#include "jsonstr.h"

/**********************************************************************/

/* returns the character for the hexadecimal digit */
static inline char hex(int digit)
{
	return (char)(digit + (digit > 9 ? 'a' - 10 : '0'));
}

size_t jsonstr_string_escape_length(const char *string, size_t maxlen)
{
	size_t i, r;
	char c;

	for(i = r = 0 ; i < maxlen  && (c = string[i]); i++) {
		if (32 > (unsigned char)c) {
			/* escaping control character */
			r += 6;
		}
		else if (c == '"' || c == '\\') {
			/* simple character escaping */
			r += 2;
		}
		else {
			r += 1;
		}
	}
	/* end */
	return r;
}

/*
 * escape the string for JSON in destination
 * returns length of the final string as if enougth room existed
 */
size_t jsonstr_string_escape(char *dest, size_t destlenmax, const char *string, size_t stringlenmax)
{
	size_t i, r;
	char c;

	/* copy until end */
	for(i = r = 0 ; i < stringlenmax  && r < destlenmax && (c = string[i]); i++, r++) {
		if (32 > (unsigned char)c) {
			/* escaping control character */
			dest[r] = '\\';
			if (++r < destlenmax)
				dest[r] = 'u';
			if (++r < destlenmax)
				dest[r] = '0';
			if (++r < destlenmax)
				dest[r] = '0';
			if (++r < destlenmax)
				dest[r] = hex((c >> 4) & 15);
			if (++r < destlenmax)
				dest[r] = hex(c & 15);
		}
		else if (c == '"' || c == '\\') {
			/* simple character escaping */
			dest[r] = '\\';
			if (++r < destlenmax)
				dest[r] = c;
		}
		else {
			dest[r] = c;
		}
	}
	/* fullfil return length */
	if (i < stringlenmax && c)
		r += jsonstr_string_escape_length(&string[i], stringlenmax - i);

	/* end */
	if (r < destlenmax)
		dest[r] = 0;
	return r;
}

/*
 * escape the string for JSON in destination
 * returns offset of the terminating zero
 */
size_t jsonstr_string_escape_unsafe(char *dest, const char *string, size_t stringlenmax)
{
	size_t i, r;
	char c;

	/* copy until end */
	for(i = r = 0 ; i < stringlenmax  && (c = string[i]); i++, r++) {
		if (32 > (unsigned char)c) {
			/* escaping control character */
			dest[r] = '\\';
			dest[++r] = 'u';
			dest[++r] = '0';
			dest[++r] = '0';
			dest[++r] = hex((c >> 4) & 15);
			dest[++r] = hex(c & 15);
		}
		else if (c == '"' || c == '\\') {
			/* simple character escaping */
			dest[r] = '\\';
			dest[++r] = c;
		}
		else {
			dest[r] = c;
		}
	}
	dest[r] = 0;
	return r;
}

