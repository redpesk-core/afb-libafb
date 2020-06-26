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

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <afb/afb-type-x4.h>
#include <afb/afb-data-x4.h>

#include "afb-data.h"
#include "afb-type.h"
#include "containerof.h"

#include "sys/x-errno.h"
#include "sys/x-mutex.h"

/*****************************************************************************/
/***    Management of data  ***/
/*****************************************************************************/

/**
 * description of a data
 */
struct afb_data
{
	/* as x4 */
	struct afb_data_x4 x4;

	/** type of the data */
	const struct afb_type_x4 *type;

	/** reference count */
	uint16_t refcount;

	/** cache count */
	uint8_t cachecount;

	/** size */
	uint32_t size;

	/** content */
	const void *pointer;

	/** the dispose */
	void (*dispose)(void*);

	/** closure of the dispose */
	void *closure;
};

/* interface x4 (but initialisation below) */
static const struct afb_data_x4_itf x4_itf;

/*****************************************************************************/
/***    Cache of converted data  ***/
/*****************************************************************************/

#if !defined(CONVERT_CACHE_SIZE)
#define CONVERT_CACHE_SIZE 16
#endif
#if CONVERT_CACHE_SIZE > 128
#undef CONVERT_CACHE_SIZE
#define CONVERT_CACHE_SIZE 128 /* limit to 128 because of link */
#endif

/** mutual exclusion on the cache */
static x_mutex_t convert_cache_mutex = X_MUTEX_INITIALIZER;

/** head of the active convert cache list */
static int8_t convert_cache_head;

/** head of the unused convert cache list */
static int8_t convert_cache_free;

/** link list of cache */
static int8_t convert_cache_link[CONVERT_CACHE_SIZE];

/** static global cache */
static struct afb_data *convert_cache[CONVERT_CACHE_SIZE][2];

/* increment reference count to the data */
static inline
void
data_addref(
	struct afb_data *data
) {
	__atomic_add_fetch(&data->refcount, 1, __ATOMIC_RELAXED);
}

/** really release the data */
static
void
data_free(
	struct afb_data *data
) {
	if (data->dispose)
		data->dispose(data->closure);
	free(data);
}

/**
 * remove any instance of given 'data' from
 * the content of the conversion cache
 */
static
void
convert_cache_clean(
	int8_t head
) {
	int8_t idx;
	struct afb_data *data;

	while(head >= 0) {
		idx = head;
		data = convert_cache[head][0] ?: convert_cache[head][1];
		if (!data) {
			head = convert_cache_link[idx];
			convert_cache_link[idx] = convert_cache_free;
			convert_cache_free = idx;
		}
		else {
			while (idx >= 0) {
				if (data == convert_cache[idx][0]) {
					convert_cache[idx][0] = 0;
					data->cachecount--;
				}
				else if (data == convert_cache[idx][1]) {
					convert_cache[idx][1] = 0;
					data->cachecount--;
				}
				idx = convert_cache_link[idx];
			}
			if (data->cachecount == 0 && data->refcount == 0) {
				x_mutex_unlock(&convert_cache_mutex);
				data_free(data);
				x_mutex_lock(&convert_cache_mutex);
			}
		}
	}
}

/**
 * remove any instance of given 'data' from
 * the content of the conversion cache
 */
static
void
convert_cache_remove(
	struct afb_data *data,
	int lazy
) {
	int8_t idx, *prv, cleanidx;
	struct afb_data *x, *y;

	cleanidx = -1;
	prv = &convert_cache_head;
	x_mutex_lock(&convert_cache_mutex);
	idx = *prv;
	if (idx != convert_cache_free) {
		/* list initialized */
		while (idx >= 0) {
			x = convert_cache[idx][0];
			y = convert_cache[idx][1];
			if ((data != x && data != y)
			 || (lazy && (x->refcount | y->refcount))) {
				prv = &convert_cache_link[idx];
			}
			else {
				*prv = convert_cache_link[idx];
				convert_cache_link[idx] = cleanidx;
				cleanidx = idx;
			}
			idx = *prv;
		}
	}
	if (cleanidx >= 0)
		convert_cache_clean(cleanidx);
	x_mutex_unlock(&convert_cache_mutex);
}

/**
 * Search in the cache of converted data if the given 'data'
 * has a cached conversion to the given 'type'.
 * Returns a pointer to the conversion found or 0 if not found.
 **/
static
struct afb_data *
convert_cache_search(
	struct afb_data *data,
	const struct afb_type_x4 *type
) {
	int8_t idx, *prv;
	struct afb_data *x, *y, *r;

	r = 0;
	prv = &convert_cache_head;
	x_mutex_lock(&convert_cache_mutex);
	idx = *prv;
	if (idx != convert_cache_free) {
		/* list initialized */
		while (idx >= 0 && !r) {
			x = convert_cache[idx][0];
			y = convert_cache[idx][1];
			if (x == data && y->type == type) {
				r = y;
			}
			else if (y == data && x->type == type) {
				r = x;
			}
			else {
				prv = &convert_cache_link[idx];
				idx = *prv;
			}
		}
		if (r) {
			data_addref(r);
			*prv = convert_cache_link[idx];
			convert_cache_link[idx] = convert_cache_head;
			convert_cache_head = idx;
		}
	}
	x_mutex_unlock(&convert_cache_mutex);
	return r;
}

static
void
convert_cache_put(
	struct afb_data *data,
	struct afb_data *other
) {
	int8_t idx, *prv;

	x_mutex_lock(&convert_cache_mutex);
	idx = convert_cache_free;
	if (idx == convert_cache_head) {
		/* initialize the list */
		for (idx = 0 ; idx < CONVERT_CACHE_SIZE - 1 ; idx++)
			convert_cache_link[idx] = (int8_t)(idx + 1);
		convert_cache_link[idx] = -1;
		convert_cache_head = -1;
		idx = convert_cache_free = 0;
	}
	if (idx < 0) {
		/* full cache, drop last entry */
		prv = &convert_cache_head;
		idx = *prv;
		while (convert_cache_link[idx] >= 0) {
			prv = &convert_cache_link[idx];
			idx = *prv;
		}
		*prv = -1;
		convert_cache_clean(idx);
		idx = convert_cache_free;
	}
	/* add at head now */
	convert_cache_free = convert_cache_link[idx];
	convert_cache[idx][0] = data;
	convert_cache[idx][1] = other;
	convert_cache_link[idx] = convert_cache_head;
	convert_cache_head = idx;
	data->cachecount++;
	other->cachecount++;
	x_mutex_unlock(&convert_cache_mutex);
}

/* set the data */
static
void
set(
	struct afb_data *data,
	const struct afb_type_x4 *type,
	const void *pointer,
	uint32_t size,
	void (*dispose)(void*),
	void *closure
) {
	/* dispose the data if any */
	if (data->dispose)
		data->dispose(data->closure);

	/* set the values now */
	data->type = type;
	data->pointer = pointer;
	data->size = size;
	data->dispose = dispose;
	data->closure = closure;

	/* invalidate cached conversions */
	afb_data_convert_cache_clear(data);
}

static void share_free(void *closure)
{
	free(closure);
}

static void *share_realloc(const void *previous, size_t size)
{
	return realloc((void*)previous, size ?: 1);
}

static int handles_share(struct afb_data *data)
{
	return data->dispose == share_free;
}

const struct afb_data_x4 *afb_data_as_data_x4(struct afb_data *data)
{
	return &data->x4;
}

struct afb_data *afb_data_of_data_x4(const struct afb_data_x4 *datax4)
{
	return containerof(struct afb_data, x4, datax4);
}

int
afb_data_create_set_x4(
	struct afb_data **result,
	const struct afb_type_x4 *type,
	const void *pointer,
	size_t size,
	void (*dispose)(void*),
	void *closure
) {
	int rc;
	struct afb_data *data;

	if (size > UINT32_MAX) {
		*result = NULL;
		rc = X_EINVAL;
	}
	else {
		*result = data = malloc(sizeof *data);
		if (data == NULL)
			rc = X_ENOMEM;
		else  {
			data->x4.itf = &x4_itf;
			data->type = type;
			data->refcount = 1;
			data->cachecount = 0;
			data->pointer = pointer;
			data->size = (uint32_t)size;
			data->dispose = dispose;
			data->closure = closure;
			rc = 0;
		}
	}
	if (rc && dispose)
		dispose(closure);

	return rc;
}

int
afb_data_create_alloc_x4(
	struct afb_data **result,
	const struct afb_type_x4 *type,
	void **pointer,
	size_t size,
	int zeroes
) {
	void *p;
	int rc;

	p = share_realloc(NULL, size);
	if (p == NULL) {
		*result = NULL;
		p = NULL;
		rc = X_ENOMEM;
	}
	else {
		rc = afb_data_create_set_x4(result, type, p, size, share_free, p);
		if (rc < 0)
			p = NULL;
		else if (zeroes)
			memset(p, 0, size);
	}
	*pointer = p;
	return rc;
}

int
afb_data_create_copy_x4(
	struct afb_data **result,
	const struct afb_type_x4 *type,
	const void *pointer,
	size_t size
) {
	int rc;
	void *p;

	rc = afb_data_create_alloc_x4(result, type, &p, size, 0);
	if (rc >= 0 && size)
		memcpy(p, pointer, size);
	return rc;
}

/* Get the typenum of the data */
const struct afb_type_x4*
afb_data_type_x4(
	const struct afb_data *data
) {
	return data->type;
}

/* increment reference count to the data */
struct afb_data *
afb_data_addref(
	struct afb_data *data
) {
	if (data)
		data_addref(data);
	return data;
}

/* decrement reference count to the data */
void
afb_data_unref(
	struct afb_data *data
) {
	if (data && !__atomic_sub_fetch(&data->refcount, 1, __ATOMIC_RELAXED)) {
		if (data->cachecount == 0)
			data_free(data);
		else
			convert_cache_remove(data, 1);
	}
}

/* Clear the content of data */
void
afb_data_clear(
	struct afb_data *data
) {
	set(data, 0, 0, 0, 0, 0);
}

/* Get the pointer. */
const void*
afb_data_pointer(
	const struct afb_data *data
) {
	return data->pointer;
}

/* Get the size. */
size_t
afb_data_size(
	const struct afb_data *data
) {
	return (size_t)data->size;
}

/* set the data */
int
afb_data_set_x4(
	struct afb_data *data,
	const struct afb_type_x4 *type,
	const void *pointer,
	size_t size,
	void (*dispose)(void*),
	void *closure
) {
	if (size > UINT32_MAX)
		return X_EINVAL;

	set(data, type, pointer, (uint32_t)size, dispose, closure);
	return 0;
}

/* Allocate a shareable buffer. */
int
afb_data_alloc_x4(
	struct afb_data *data,
	const struct afb_type_x4 *type,
	void **pointer,
	size_t size,
	int zeroes
) {
	void *result;
	int rc;

	if (size > UINT32_MAX) {
		result = NULL;
		rc = X_EINVAL;
	}
	else {
		result = share_realloc(NULL, size);
		if (result == NULL) {
			rc = X_ENOMEM;
		}
		else {
			if (zeroes && size)
				memset(result, 0, size);
			data->size = (uint32_t)size;
			data->pointer = data->closure = result;
			set(data, type, result, (uint32_t)size, share_free, result);
		}
		return result ? 0 : X_ENOMEM;
	}
	*pointer = result;
	return rc;
}

/* Allocate a shareable buffer. */
int
afb_data_resize(
	struct afb_data *data,
	void **pointer,
	size_t size,
	int zeroes
) {
	void *result;
	int rc;

	if (size > UINT32_MAX) {
		result = NULL;
		rc = X_EINVAL;
	}
	else if (!handles_share(data)) {
		result = NULL;
		rc = X_EINVAL;
	}
	else {
		result = share_realloc(data->pointer, size);
		if (result == NULL) {
			rc = X_ENOMEM;
		}
		else {
			if (zeroes && size > data->size)
				memset(data->size + (char*)result, 0, size - data->size);
			data->size = (uint32_t)size;
			data->pointer = data->closure = result;
			rc = 0;
		}
	}
	*pointer = result;
	return rc;
}

/* copy data */
int
afb_data_copy_x4(
	struct afb_data *data,
	const struct afb_type_x4 *type,
	const void *pointer,
	size_t size
) {
	void *to;
	int rc;

	rc = afb_data_alloc_x4(data, type, &to, size, 0);
	if (rc >= 0)
		memcpy(to, pointer, size);
	return rc;
}

/* add conversion from other data */
int
afb_data_convert_to_x4(
	struct afb_data *data,
	const struct afb_type_x4 *type,
	struct afb_data **result
) {
	int rc;
	struct afb_data *r;

	/* trivial case: the data is of the expected type */
	if (!type || type == data->type) {
		data_addref(data);
		r = data;
		rc = 0;
	}
	else {
		/* search for a cached conversion */
		r = convert_cache_search(data, type);
		if (r) {
			/* found! cool */
			rc = 0;
		}
		else {
			/* conversion */
			rc = afb_type_convert_data_x4(data, type, &r);
			if (rc >= 0) {
				convert_cache_put(data, r);
				rc = 0;
			}
		}
	}
	*result = r;
	return rc;
}

/* invalidate cached conversions */
void
afb_data_convert_cache_clear(
	struct afb_data *data
) {
	convert_cache_remove(data, 0);
}

/*****************************************************************************/
/* HELPERS FOR X4 CREATION                                                   */
/*****************************************************************************/

int
afb_data_x4_create_set_x4(
	const struct afb_data_x4 **result,
	const struct afb_type_x4 *type,
	const void *pointer,
	size_t size,
	void (*dispose)(void*),
	void *closure
) {
	struct afb_data *data;
	int rc;

	rc = afb_data_create_set_x4(&data, type, pointer, size, dispose, closure);
	*result = rc < 0 ? NULL : afb_data_as_data_x4(data);
	return rc;
}

int
afb_data_x4_create_alloc_x4(
	const struct afb_data_x4 **result,
	const struct afb_type_x4 *type,
	void **pointer,
	size_t size,
	int zeroes
) {
	struct afb_data *data;
	int rc;

	rc = afb_data_create_alloc_x4(&data, type, pointer, size, zeroes);
	*result = rc < 0 ? NULL : afb_data_as_data_x4(data);
	return rc;
}

int
afb_data_x4_create_copy_x4(
	const struct afb_data_x4 **result,
	const struct afb_type_x4 *type,
	const void *pointer,
	size_t size
) {
	struct afb_data *data;
	int rc;

	rc = afb_data_create_copy_x4(&data, type, pointer, size);
	*result = rc < 0 ? NULL : afb_data_as_data_x4(data);
	return rc;
}

/*****************************************************************************/
/* INTERFACE X4                                                              */
/*****************************************************************************/

static const struct afb_data_x4 *x4_addref(const struct afb_data_x4 *datax4)
{
	struct afb_data *data = containerof(struct afb_data, x4, datax4);
	afb_data_addref(data);
	return datax4;
}

static void x4_unref(const struct afb_data_x4 *datax4)
{
	struct afb_data *data = containerof(struct afb_data, x4, datax4);
	afb_data_unref(data);
}

static const struct afb_type_x4 *x4_type(const struct afb_data_x4 *datax4)
{
	struct afb_data *data = containerof(struct afb_data, x4, datax4);
	return afb_data_type_x4(data);
}

static const void* x4_pointer(const struct afb_data_x4 *datax4)
{
	struct afb_data *data = containerof(struct afb_data, x4, datax4);
	return afb_data_pointer(data);
}

static size_t x4_size(const struct afb_data_x4 *datax4)
{
	struct afb_data *data = containerof(struct afb_data, x4, datax4);
	return afb_data_size(data);
}

static int x4_convert(const struct afb_data_x4 *datax4, const struct afb_type_x4 *type, const struct afb_data_x4 **resultx4)
{
	struct afb_data *data = containerof(struct afb_data, x4, datax4);
	struct afb_data *result;
	int rc;

	rc = afb_data_convert_to_x4(data, type, &result);
	*resultx4 = (rc < 0) ? NULL : &result->x4;
	return rc;
}

static const struct afb_data_x4_itf x4_itf = {
	.addref  = x4_addref,
	.unref   = x4_unref,
	.type    = x4_type,
	.pointer = x4_pointer,
	.size    = x4_size,
	.convert = x4_convert
};

