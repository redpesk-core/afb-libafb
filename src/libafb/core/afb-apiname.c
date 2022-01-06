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


#include "core/afb-apiname.h"

/**
 * Checks wether 'name' is a valid API name.
 * @return 1 if valid, 0 otherwise
 */
int afb_apiname_is_valid(const char *apiname)
{
	unsigned char c;

	c = (unsigned char)*apiname;
	if (c == 0)
		/* empty names aren't valid */
		return 0;

	do {
		if (c < (unsigned char)'\x80') {
			switch(c) {
			default:
				if (c > ' ')
					break;
				/*@fallthrough@*/
			case '"':
			case '#':
			case '%':
			case '&':
			case '\'':
			case '/':
			case '?':
			case '`':
			case '\\':
			case '\x7f':
				return 0;
			}
		}
		c = (unsigned char)*++apiname;
	} while(c != 0);
	return 1;
}

