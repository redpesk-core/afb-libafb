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

#include "libafb-config.h"

#include "sys/x-stdlib.h"

#if HAVENT_qsort

static inline void _swap_(void *x, void *y, size_t size)
{
#define _SWAP_(type) \
	while(size >= sizeof(type)) {\
		size -= sizeof(type);\
		type tmp = *(type*)x;\
		*(type*)x = *(type*)y;\
		x += sizeof(type);\
		*(type*)y = tmp;\
		y += sizeof(type);\
	}
	_SWAP_(long long)
	_SWAP_(int)
	_SWAP_(short)
	_SWAP_(char)
#undef _SWAP_
}

void qsort(void *base, size_t nmemb, size_t size,
                  int (*compar)(const void *, const void *))
{
	size_t nlow, iup;
#define ENT(i) (((char*)base) + ((i) * size))
#define CMP(i,j) (compar(ENT(i), ENT(j)))
#define SWAP(i,j) _swap_(ENT(i), ENT(j), size)

	if (nmemb > 1) {
		nlow = 1;
		iup = nmemb;
		while(nlow < iup) {
			while(nlow < iup && CMP(0, nlow) <= 0)
				nlow++;
			while(nlow < iup && CMP(0, iup - 1) > 0)
				iup--;
			if (nlow < iup) {
				iup--;
				SWAP(nlow, iup);
				nlow++;
			}
		}
		if (nlow > 1) {
			SWAP(0, nlow - 1);
			qsort(E(0), nlow - 1, size, compar);
		}
		if (iup < nmemb)
			qsort(E(iup), nmemb - iup, size, compar);
	}
#undef SWAP
#undef CMP
#undef ENT
}

#endif

