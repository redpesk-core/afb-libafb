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

#include "afb-config.h"

#include <stdint.h>

/*
 * Returns a tiny hash value for the 'text'.
 *
 * Tiny hash function inspired from pearson
 */
uint8_t pearson4(const char *text)
{
	static uint8_t T[16] = {
		 4,  1,  6,  0,  9, 14, 11,  5,
		 2,  3, 12, 15, 10,  7,  8, 13
	};
	uint8_t r, c;

	for (r = 0; (c = (uint8_t)*text) ; text++) {
		r = T[r ^ (15 & c)];
		r = T[r ^ (c >> 4)];
	}
	return r; // % HEADCOUNT;
}

