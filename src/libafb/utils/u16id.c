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

#include <stdint.h>
#include <malloc.h>

#include "utils/u16id.h"
#include "sys/x-errno.h"

/* compute P, the count of bits of pointers */
#if UINTPTR_MAX == (18446744073709551615UL)
#  define P 64
#elif UINTPTR_MAX == (4294967295U)
#  define P 32
#elif UINTPTR_MAX == (65535U)
#  define P 16
#else
#  error "Unsupported pointer size"
#endif

/* granule of allocation */
#define N 4

/*
 * The u16id maps are made of a single block of memory structured
 * as an array of uint16_t followed by an array of void*. To ensure
 * that void* pointers are correctly aligned, the array of uint16_t
 * at head is a multiple of N items, with N being a multiple of 2
 * if void* is 32 bits or 4 if void* is 64 bits.
 *
 * The first item of the array of uint16_t is used to record the
 * upper index of valid uint16_t ids.
 *
 * +-----+-----+-----+-----+ - - - - - - - - +-----+-----+-----+-----+ - - - - - - - -
 * |upper| id1 | id2 | id3 |                 |         ptr1          |
 * +-----+-----+-----+-----+ - - - - - - - - +-----+-----+-----+-----+ - - - - - - - -
 */

static inline uint16_t get_capacity(uint16_t upper)
{
	/* capacity is the smallest kN-1 such that kN-1 >= upper) */
#if N == 2 || N == 4 || N == 8 || N == 16
	return upper | (N - 1);
#else
#	error "not supported"
#endif
}

typedef struct {
	uint16_t upper;
	uint16_t capacity;
	uint16_t *ids;
	void **ptrs;
} flat_t;

static void flatofup(flat_t *flat, void *base, uint16_t up)
{
	uint16_t cap, *ids;

	flat->upper = up;
	flat->capacity = cap = get_capacity(up);
	flat->ids = ids = base;
	flat->ptrs = ((void**)(&ids[cap + 1])) - 1;
}

static void flatof(flat_t *flat, void *base)
{
	if (base)
		flatofup(flat, base, *(uint16_t*)base);
	else {
		flat->upper = flat->capacity = 0;
		flat->ids = NULL;
		flat->ptrs = NULL;
	}
}

static inline size_t size(uint16_t capacity)
{
	return sizeof(uint16_t) * (capacity + 1)
		+ sizeof(void*) * capacity;
}

static inline uint16_t search(flat_t *flat, uint16_t id)
{
	uint16_t *ids = flat->ids;
	uint16_t r = flat->upper;
	while(r && ids[r] != id)
		r--;
	return r;
}

static void *add(flat_t *flat, uint16_t id, void *ptr)
{
	void *grown, *result;
	flat_t oflat;
	uint16_t nupper, oupper;

	oupper = flat->upper;
	nupper = (uint16_t)(oupper + 1);
	result = flat->ids;
	if (nupper > flat->capacity) {
		grown = realloc(result, size(get_capacity(nupper)));
		if (grown == NULL)
			return NULL;
		result = grown;
		flatofup(flat, grown, nupper);
		if (oupper) {
			flatofup(&oflat, grown, oupper);
			while (oupper) {
				flat->ptrs[oupper] = oflat.ptrs[oupper];
				oupper--;
			}
		}
	}
	/* flat->upper = nupper; NOT DONE BECAUSE NOT NEEDED */
	flat->ids[0] = nupper;
	flat->ids[nupper] = id;
	flat->ptrs[nupper] = ptr;
	return result;
}

static void *drop(flat_t *flat, uint16_t index)
{
	void **ptrs, *result;
	uint16_t upper, idx, capa;

	upper = flat->upper;
	if (index != upper) {
		flat->ids[index] = flat->ids[upper];
		flat->ptrs[index] = flat->ptrs[upper];
	}
	flat->ids[0] = --upper;
	capa = get_capacity(upper);
	result = flat->ids;
	if (capa != flat->capacity) {
		ptrs = flat->ptrs;
		flatofup(flat, result, upper);
		idx = 1;
		while(idx <= upper) {
			flat->ptrs[idx] = ptrs[idx];
			idx++;
		}
#if U16ID_ALWAYS_SHRINK
		result = realloc(flat->ids, size(capa));
		if (result == NULL)
			result = flat->ids;
#endif
	}
	return result;
}

static void dropall(void **pbase)
{
	void *base;

	base = *pbase;
	if (base)
		*(uint16_t*)base = 0;
}

static void destroy(void **pbase)
{
	void *base;

	base = *pbase;
	*pbase = NULL;
	free(base);
}

static int create(void **pbase)
{
	void *base;

	*pbase = base = malloc(size(get_capacity(0)));
	if (base == NULL)
		return X_ENOMEM;
	*(uint16_t*)base = 0;
	return 0;
}

/**********************************************************************/
/**        u16id2ptr                                                 **/
/**********************************************************************/

int u16id2ptr_create(struct u16id2ptr **pi2p)
{
	return create((void**)pi2p);
}

void u16id2ptr_destroy(struct u16id2ptr **pi2p)
{
	destroy((void**)pi2p);
}

void u16id2ptr_dropall(struct u16id2ptr **pi2p)
{
	dropall((void**)pi2p);
}

int u16id2ptr_has(struct u16id2ptr *i2p, uint16_t id)
{
	flat_t flat;

	flatof(&flat, i2p);
	return search(&flat, id) != 0;
}

int u16id2ptr_add(struct u16id2ptr **pi2p, uint16_t id, void *ptr)
{
	struct u16id2ptr *i2p;
	uint16_t index;
	flat_t flat;

	i2p = *pi2p;
	flatof(&flat, i2p);
	index = search(&flat, id);
	if (index)
		return X_EEXIST;
	i2p = add(&flat, id, ptr);
	if (!i2p)
		return X_ENOMEM;
	*pi2p = i2p;
	return 0;
}

int u16id2ptr_set(struct u16id2ptr **pi2p, uint16_t id, void *ptr)
{
	struct u16id2ptr *i2p;
	uint16_t index;
	flat_t flat;

	i2p = *pi2p;
	flatof(&flat, i2p);
	index = search(&flat, id);
	if (index)
		flat.ptrs[index] = ptr;
	else {
		i2p = add(&flat, id, ptr);
		if (!i2p)
			return X_ENOMEM;
		*pi2p = i2p;
	}
	return 0;
}

int u16id2ptr_put(struct u16id2ptr *i2p, uint16_t id, void *ptr)
{
	uint16_t index;
	flat_t flat;

	flatof(&flat, i2p);
	index = search(&flat, id);
	if (!index)
		return X_ENOENT;
	flat.ptrs[index] = ptr;
	return 0;
}

int u16id2ptr_get(struct u16id2ptr *i2p, uint16_t id, void **pptr)
{
	uint16_t index;
	flat_t flat;

	flatof(&flat, i2p);
	index = search(&flat, id);
	if (!index)
		return X_ENOENT;
	*pptr = flat.ptrs[index];
	return 0;
}

int u16id2ptr_drop(struct u16id2ptr **pi2p, uint16_t id, void **pptr)
{
	struct u16id2ptr *i2p;
	uint16_t index;
	flat_t flat;

	i2p = *pi2p;
	flatof(&flat, i2p);
	index = search(&flat, id);
	if (!index)
		return X_ENOENT;
	if (pptr)
		*pptr = flat.ptrs[index];

	i2p = drop(&flat, index);
	if (!i2p)
		return X_ENOMEM;
	*pi2p = i2p;
	return 0;
}

int u16id2ptr_count(struct u16id2ptr *i2p)
{
	return i2p ? ((int)*(uint16_t*)i2p) : 0;
}

int u16id2ptr_at(struct u16id2ptr *i2p, int index, uint16_t *pid, void **pptr)
{
	flat_t flat;

	flatof(&flat, i2p);
	if (index < 0 || index >= (int)flat.upper)
		return X_EINVAL;
	*pid = flat.ids[index + 1];
	*pptr = flat.ptrs[index + 1];
	return 0;
}

void u16id2ptr_forall(struct u16id2ptr *i2p, void (*callback)(void*closure, uint16_t id, void *ptr), void *closure)
{
	flat_t flat;

	flatof(&flat, i2p);
	while (flat.upper) {
		callback(closure, flat.ids[flat.upper], flat.ptrs[flat.upper]);
		flat.upper--;
	}
}

/**********************************************************************/
/**        u16id2bool                                                **/
/**********************************************************************/

int u16id2bool_create(struct u16id2bool **pi2b)
{
	return create((void**)pi2b);
}

void u16id2bool_destroy(struct u16id2bool **pi2b)
{
	destroy((void**)pi2b);
}

void u16id2bool_clearall(struct u16id2bool **pi2b)
{
	dropall((void**)pi2b);
}

int u16id2bool_get(struct u16id2bool *i2b, uint16_t id)
{
	uintptr_t mask, field;
	uint16_t index, idm;
	flat_t flat;

	flatof(&flat, i2b);
	idm = (uint16_t)(id & ~(P - 1));
	index = search(&flat, idm);
	if (!index)
		return 0;

	field = (uintptr_t)flat.ptrs[index];
	mask = (uintptr_t)((uintptr_t)1 << (id & (P - 1)));
	return (field & mask) != 0;
}

int u16id2bool_set(struct u16id2bool **pi2b, uint16_t id, int value)
{
	struct u16id2bool *i2b;
	uintptr_t mask, field, ofield;
	uint16_t index, idm;
	flat_t flat;

	i2b = *pi2b;
	flatof(&flat, i2b);
	idm = (uint16_t)(id & ~(P - 1));
	index = search(&flat, idm);
	ofield = index ? (uintptr_t)flat.ptrs[index] : 0;
	mask = (uintptr_t)((uintptr_t)1 << (id & (P - 1)));
	if (value)
		field = ofield | mask;
	else
		field = ofield & ~mask;
	if (field != ofield) {
		if (field) {
			if (index)
				flat.ptrs[index] = (void*)field;
			else {
				i2b = add(&flat, idm, (void*)field);
				if (!i2b)
					return X_ENOMEM;
				*pi2b = i2b;
			}
		} else {
			if (index) {
				i2b = drop(&flat, index);
				if (!i2b)
					return X_ENOMEM;
				*pi2b = i2b;
			}
		}
	}
	return (ofield & mask) != 0;
}
