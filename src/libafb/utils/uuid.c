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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "utils/uuid.h"

#if WITH_LIBUUID

#include <uuid/uuid.h>

#else

static void b2h(const unsigned char *uu, char *out, int count, char term)
{
	int x, y;

	do {
		x = *uu++;
		y = (x >> 4) & 15;
		*out++ = (char)(y < 10 ? ('0' + y) : (('a' - 10) + y));
		y = x & 15;
		*out++ = (char)(y < 10 ? ('0' + y) : (('a' - 10) + y));
	} while(--count);
	*out = term;
}

static void uuid_unparse_lower(const uuid_binary_t uu, uuid_stringz_t out)
{
	/* 01234567-9012-4567-9012-456789012345 */
	b2h(&uu[0], &out[0], 4, '-');
	b2h(&uu[4], &out[9], 2, '-');
	b2h(&uu[6], &out[14], 2, '-');
	b2h(&uu[8], &out[19], 2, '-');
	b2h(&uu[10], &out[24], 6, 0);
}
#endif

/**
 * generate a new fresh 'uuid'
 */
void uuid_new_binary(uuid_binary_t uuid)
{
#if defined(USE_UUID_GENERATE)
	uuid_generate(uuid);
#else
	static uint16_t pid;
	static uint16_t counter;

#if WITH_RANDOM_R
	static char state[32];
	static struct random_data rdata;
#define INIRND(x) do{ rdata.state = NULL; initstate_r((unsigned)(x), state, sizeof state, &rdata); }while(0)
#define GETRND(x) random_r(&rdata, (x))
#else
#define INIRND(x) srand((unsigned)(x))
#define GETRND(x) (*(x) = rand())
#endif
	int32_t x;

#if WITH_CLOCK_GETTIME
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
	ts.tv_nsec ^= (long)ts.tv_sec;
	x = (int32_t)((int32_t)ts.tv_sec ^ (int32_t)ts.tv_nsec);
#else
	x = (int32_t)time(NULL);
#endif
	if (pid == 0) {
		pid = (uint16_t)getpid();
		counter = (uint16_t)(x);
		INIRND((((unsigned)pid) << 16) + (unsigned)counter);
	}
	if (++counter == 0)
		counter = 1;

	uuid[0] = (char)(x >> 24);
	uuid[1] = (char)(x >> 16);
	uuid[2] = (char)(x >> 8);
	uuid[3] = (char)(x);

	uuid[4] = (char)(pid >> 8);
	uuid[5] = (char)(pid);

	GETRND(&x);
	uuid[6] = (char)(((x >> 16) & 0x0f) | 0x40); /* pseudo-random version */
	uuid[7] = (char)(x >> 8);

	GETRND(&x);
	uuid[8] = (char)(((x >> 16) & 0x3f) | 0x80); /* variant RFC4122 */
	uuid[9] = (char)(x >> 8);

	GETRND(&x);
	uuid[10] = (char)(x >> 16);
	uuid[11] = (char)(x >> 8);

	GETRND(&x);
	uuid[12] = (char)(x >> 16);
	uuid[13] = (char)(x >> 8);

	uuid[14] = (char)(counter >> 8);
	uuid[15] = (char)(counter);
#undef INIRND
#undef GETRND
#endif
}

void uuid_new_stringz(uuid_stringz_t uuid)
{
	uuid_binary_t newuuid;
	uuid_new_binary(newuuid);
	uuid_unparse_lower(newuuid, uuid);
}
