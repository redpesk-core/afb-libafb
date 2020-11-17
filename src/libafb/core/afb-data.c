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

#include "afb-data.h"
#include "afb-type.h"
#include "containerof.h"

#include "utils/u16id.h"
#include "utils/lockany.h"
#include "sys/x-errno.h"

/*****************************************************************************/
/***    Management of data  ***/
/*****************************************************************************/

/**
 * description of a data
 */
struct afb_data
{
	/* the type */
	struct afb_type *type;

	/* the pointer */
	const void *pointer;

	/* the size */
	size_t size;

	/** the dispose */
	void (*dispose)(void*);

	/** closure of the dispose */
	void *closure;

	/** next conversion */
	struct afb_data *cvt;

	/** reference count and flags */
	uint16_t ref_and_flags;

	/** opaque id of the data */
	uint16_t opaqueid;
};

/*****************************************************************************/
/***    Opacifier  ***/
/*****************************************************************************/

static uint16_t opaqueidgen;
static struct u16id2ptr *opacifier;

/*****************************************************************************/
/***    Flag values  ***/
/*****************************************************************************/

#define FLAG_IS_VOLATILE        1
#define FLAG_IS_CONSTANT        2
#define FLAG_IS_VALID           4
#define FLAG_IS_LOCKED          8
#define FLAG_IS_ALIAS          16
#define FLAG_IS_ETERNAL        32
#define REF_COUNT_INCREMENT    64

#define INITIAL_REF_AND_FLAGS  (FLAG_IS_CONSTANT|FLAG_IS_VALID|REF_COUNT_INCREMENT)

#define _HASREF_(rf)           ((rf) >= FLAG_IS_ETERNAL)
#define HASREF(data)           _HASREF_((data)->ref_and_flags)

#define ADDREF(data)           _HASREF_(__atomic_add_fetch(&data->ref_and_flags, REF_COUNT_INCREMENT, __ATOMIC_RELAXED))
#define UNREF(data)            _HASREF_(__atomic_sub_fetch(&data->ref_and_flags, REF_COUNT_INCREMENT, __ATOMIC_RELAXED))

#define TEST_FLAGS(data,flag)  (__atomic_load_n(&((data)->ref_and_flags), __ATOMIC_RELAXED) & (flag))
#define SET_FLAGS(data,flag)   (__atomic_or_fetch(&((data)->ref_and_flags), flag, __ATOMIC_RELAXED))
#define UNSET_FLAGS(data,flag) (__atomic_and_fetch(&((data)->ref_and_flags), ~flag, __ATOMIC_RELAXED))

#define IS_VALID(data)         TEST_FLAGS(data,FLAG_IS_VALID)
#define SET_VALID(data)        SET_FLAGS(data,FLAG_IS_VALID)
#define UNSET_VALID(data)      UNSET_FLAGS(data,FLAG_IS_VALID)

#define IS_VOLATILE(data)      TEST_FLAGS(data,FLAG_IS_VOLATILE)
#define SET_VOLATILE(data)     SET_FLAGS(data,FLAG_IS_VOLATILE)
#define UNSET_VOLATILE(data)   UNSET_FLAGS(data,FLAG_IS_VOLATILE)

#define IS_CONSTANT(data)      TEST_FLAGS(data,FLAG_IS_CONSTANT)
#define SET_CONSTANT(data)     SET_FLAGS(data,FLAG_IS_CONSTANT)
#define UNSET_CONSTANT(data)   UNSET_FLAGS(data,FLAG_IS_CONSTANT)

#define IS_LOCKED(data)        TEST_FLAGS(data,FLAG_IS_LOCKED)
#define SET_LOCKED(data)       SET_FLAGS(data,FLAG_IS_LOCKED)
#define UNSET_LOCKED(data)     UNSET_FLAGS(data,FLAG_IS_LOCKED)

#define IS_ALIAS(data)         TEST_FLAGS(data,FLAG_IS_ALIAS)
#define SET_ALIAS(data)        SET_FLAGS(data,FLAG_IS_ALIAS)
#define UNSET_ALIAS(data)      UNSET_FLAGS(data,FLAG_IS_ALIAS)

/*****************************************************************************/
/***    Shared memory emulation  ***/
/*****************************************************************************/

static void share_free(void *closure)
{
	free(closure);
}

static void *share_realloc(const void *previous, size_t size)
{
	return realloc((void*)previous, size ?: 1);
}

__attribute__((unused))
static int share_is_owner(struct afb_data *data)
{
	return data->dispose == share_free || data->pointer == NULL;
}

/*****************************************************************************/
/***    Internal routines  ***/
/*****************************************************************************/

/**
 * Increment reference count of data (not null)
 */
static inline
void
data_addref(
	struct afb_data *data
) {
	if (!ADDREF(data))
		SET_FLAGS(data, FLAG_IS_ETERNAL);
}

/**
 * really destroys the data and release (dispose) its resources
 */
static inline
void
data_destroy(
	struct afb_data *data
) {
	/* cancel any opacified shadow */
	if (data->opaqueid)
		u16id2ptr_drop(&opacifier, data->opaqueid, 0);
	/* release resource */
	if (data->dispose)
		data->dispose(data->closure);
	/* release the data itself */
	free(data);
}

/**
 * release the data if it is not latent.
 * A data is latent if it is living
 * or one of its conversion is living.
 */
static
void
data_release(
	struct afb_data *data
) {
	int gr, r;
	struct afb_data *i, *n, *p;

	gr = HASREF(data);
	i = data->cvt;
	if (i != data) {
		/* more than one element */
		p = data;
		for (;;) {
			/* count global use */
			r = HASREF(i);
			gr += r;
			if (r == 0) {
				/* search if unused duplication */
				n = i->cvt;
				while (n != i && n->type != i->type)
					n = n->cvt;
				if (n != i) {
					/* release unused duplicate */
					p->cvt = n = i->cvt;
					data_destroy(i);
					/* iteration */
					if (i == data)
						data = n;
					i = n;
				}
			}
			if (i == data)
				break;
			p = i;
			i = i->cvt;
		}
	}

	/* is it used ? */
	if (gr == 0) {
		/* no more ref count on any data */
		i = data;
		do {
			n = i->cvt;
			data_destroy(i);
			i = n;
		} while (i != data);
	}
}

/**
 * invalidate any conversion of this data
 */
static
void
data_cvt_changed(
	struct afb_data *data
) {
	struct afb_data *i, *p;

	p = data;
	i = p->cvt;
	while (i != data) {
		if (HASREF(i)) {
			UNSET_VALID(i);
			if (i->dispose) {
				i->dispose(i->closure);
				i->dispose = 0;
			}
			p = i;
		}
		else {
			p->cvt = i->cvt;
			data_destroy(i);
		}
		i = p->cvt;
	}
}

/**
 * merge an origin and its conversion
 */
static
void
data_cvt_merge(
	struct afb_data *origin,
	struct afb_data *data
) {
	struct afb_data *i, *j;

	for (i = origin; i != data && i->cvt != origin; i = i->cvt);
	if (i != data) {
		for (j = data; j->cvt != data; j = j->cvt);
		i->cvt = data;
		j->cvt = origin;
	}
}

/**
 * search the conversion of data to the type
 * and returns it or null.
 *
 * @param data the data whose type is searched (not NULL)
 * @param type the type to search (not NULL)
 *
 * @return the found cached conversion data or NULL
 */
static
struct afb_data *
data_cvt_search(
	struct afb_data *data,
	struct afb_type *type
) {
	struct afb_data *i = data;

	do {
		if (i->type == type)
			return i;
		i = i->cvt;
	} while (i != data);
	return 0;
}

/**
 * unalias the data
 */
static inline
struct afb_data *
data_unaliased(
	struct afb_data *data
) {
	while(IS_ALIAS(data))
		data = (struct afb_data*)data->closure;
	return data;
}

/**
 * tries to ensure that data is valid
 * returns 1 if valid or zero if it can't validate the data
 */
static
int
data_validate(
	struct afb_data *data
) {
	struct afb_data *r, *i;

	i = data->cvt;
	while (!IS_VALID(data) && i != data) {
		if (!IS_VALID(i) || IS_ALIAS(i) || afb_type_convert_data(i->type, i, data->type, &r) < 0) {
			i = i->cvt;
		}
		else {
			data->pointer = r->pointer;
			data->size = r->size;
			data->dispose = (void(*)(void*))afb_data_unref;
			data->closure = r;
			SET_VALID(data);
		}
	}
	return IS_VALID(data);
}

/*****************************************************************************/
/***    Public routines  ***/
/*****************************************************************************/

int
afb_data_create_raw(
	struct afb_data **result,
	struct afb_type *type,
	const void *pointer,
	size_t size,
	void (*dispose)(void*),
	void *closure
) {
	int rc;
	struct afb_data *data;

	*result = data = malloc(sizeof *data);
	if (data == NULL)
		rc = X_ENOMEM;
	else  {
		data->type = type;
		data->pointer = pointer;
		data->size = size;
		data->dispose = dispose;
		data->closure = closure;
		data->cvt = data;
		data->ref_and_flags = INITIAL_REF_AND_FLAGS;
		data->opaqueid = 0;
		rc = 0;
	}
	if (rc && dispose)
		dispose(closure);

	return rc;
}

int
afb_data_create_alloc(
	struct afb_data **result,
	struct afb_type *type,
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
		rc = afb_data_create_raw(result, type, p, size, share_free, p);
		if (rc < 0)
			p = NULL;
		else if (zeroes)
			memset(p, 0, size);
	}
	*pointer = p;
	return rc;
}

int
afb_data_create_copy(
	struct afb_data **result,
	struct afb_type *type,
	const void *pointer,
	size_t size
) {
	int rc;
	void *p;

	rc = afb_data_create_alloc(result, type, &p, size, 0);
	if (rc >= 0 && size)
		memcpy(p, pointer, size);
	return rc;
}

int
afb_data_create_alias(
	struct afb_data **result,
	struct afb_type *type,
	struct afb_data *other
) {
	int rc;
	struct afb_data *data;

	*result = data = malloc(sizeof *data);
	if (data == NULL)
		rc = X_ENOMEM;
	else  {
		data->type = type;
		data->pointer = 0;
		data->size = 0;
		data->cvt = data;
		data->ref_and_flags = INITIAL_REF_AND_FLAGS | FLAG_IS_ALIAS;
		data->opaqueid = 0;
		data->dispose = (void(*)(void*))afb_data_unref;
		data->closure = other;
		data_addref(other);
		rc = 0;
	}
	return rc;
}

/* Get the typenum of the data */
struct afb_type*
afb_data_type(
	struct afb_data *data
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
	if (data && !UNREF(data)) {
		data_release(data);
	}
}

/* Get the pointer. */
const void*
afb_data_const_pointer(
	struct afb_data *data
) {
	data = data_unaliased(data);
	return data_validate(data) ? data->pointer : NULL;
}

/* Get the size. */
size_t
afb_data_size(
	struct afb_data *data
) {
	data = data_unaliased(data);
	return data_validate(data) ? data->size : 0;
}

/* add conversion from other data */
int
afb_data_convert_to(
	struct afb_data *data,
	struct afb_type *type,
	struct afb_data **result
) {
	int rc;
	struct afb_data *r;

	if (!data_validate(data)) {
		/* original data is not valid */
		r = 0;
		rc = -1;
	}
	else if (!type) {
		/* trivial case: the data is of the expected type */
		data_addref(data);
		r = data;
		rc = 0;
	}
	else {
		/* search for a cached conversion */
		r = data_cvt_search(data, type);
		if (r) {
			/* found! cool */
			data_addref(r);
			rc = 0;
		}
		else {
			/* conversion */
			rc = afb_type_convert_data(data->type, data, type, &r);
			if (rc >= 0 && !IS_VOLATILE(data)) {
				data_cvt_merge(data, r);
				rc = 0;
			}
		}
	}
	*result = r;
	return rc;
}

/* update a data */
int
afb_data_update(
	struct afb_data *data,
	struct afb_data *value
) {
	if (!IS_VALID(data) || afb_data_is_constant(data) || !data_validate(value)) {
		/* can not update based on parameter state */
		return X_EINVAL;
	}

	return afb_type_update_data(value->type, value, data->type, data);
}


/* opacifies the data and returns its opaque id */
int
afb_data_opacify(
	struct afb_data *data
) {
	int rc;
	uint16_t id;

	/* check if already opacified */
	id = data->opaqueid;
	if (id != 0) {
		return (int)id;
	}

	/* create the opacifier once (TODO: make it static) */
	if (opacifier == NULL) {
		rc = u16id2ptr_create(&opacifier);
		if (rc < 0)
			return rc;
	}
	else {
		/* refuse to creatio too many ids */
		if (u16id2ptr_count(opacifier) >= INT16_MAX)
			return X_ECANCELED;
	}

	/* find an id */
	for (;;) {
		id = ++opaqueidgen;
		if (id == 0) {
			continue;
		}
		rc = u16id2ptr_add(&opacifier, id, data);
		if (rc == 0) {
			data->opaqueid = id;
			return (int)id;
		}
		if (rc != X_EEXIST)
			return rc;
	}
}

/* get the data of the given opaque id */
int
afb_data_get_opacified(
	int opaqueid,
	struct afb_data **data,
	struct afb_type **type
) {
	int rc;
	struct afb_data *d;

	if (opaqueid <= 0 || opaqueid > UINT16_MAX) {
		rc = X_EINVAL;
	}
	else {
		rc = u16id2ptr_get(opacifier, (uint16_t)opaqueid, (void**)&d);
		if (rc == 0) {
			*data = afb_data_addref(d);
			*type = d->type;
		}
	}
	return rc;
}

/* invalidate cached conversions */
void
afb_data_notify_changed(
	struct afb_data *data
) {
	data_cvt_changed(data);
}

/* test if constant */
int
afb_data_is_constant(
	struct afb_data *data
) {
	return IS_CONSTANT(data);
}

/* set as constant */
void
afb_data_set_constant(
	struct afb_data *data
) {
	SET_CONSTANT(data);
}

/* set as not constant */
void
afb_data_set_not_constant(
	struct afb_data *data
) {
	UNSET_CONSTANT(data);
}

/* test if volatile */
int
afb_data_is_volatile(
	struct afb_data *data
) {
	return IS_VOLATILE(data);
}

/* set as volatile */
void
afb_data_set_volatile(
	struct afb_data *data
) {
	SET_VOLATILE(data);
}

/* set as not volatile */
void
afb_data_set_not_volatile(
	struct afb_data *data
) {
	UNSET_VOLATILE(data);
}

static struct afb_data *lockhead(struct afb_data *data)
{
	struct afb_data *i = data;
	while(!IS_LOCKED(i)) {
		i = i->cvt;
		if (i == data) {
			SET_LOCKED(i);
			break;
		}
	}
	return i;
}

void afb_data_lock_read(struct afb_data *data)
{
	lockany_lock_read(lockhead(data));
}

int afb_data_try_lock_read(struct afb_data *data)
{
	return lockany_try_lock_read(lockhead(data));
}

void afb_data_lock_write(struct afb_data *data)
{
	lockany_lock_write(lockhead(data));
}

int afb_data_try_lock_write(struct afb_data *data)
{
	return lockany_try_lock_write(lockhead(data));
}

void afb_data_unlock(struct afb_data *data)
{
	struct afb_data *head = lockhead(data);
	if (!lockany_unlock(head))
		UNSET_LOCKED(head);
}

int afb_data_get_mutable(struct afb_data *data, void **pointer, size_t *size)
{
	int rc;

	if (afb_data_is_constant(data) || IS_ALIAS(data)){
		rc = -1;
	}
	else if (data_validate(data)) {
		afb_data_notify_changed(data);
		rc = 0;
	}
	else {
		rc = -1;
	}
	if (pointer)
		*pointer = rc < 0 ? NULL : (void*)data->pointer;
	if (size)
		*size = rc < 0 ? 0 : data->size;
	return rc;
}

int afb_data_get_constant(struct afb_data *data, const void **pointer, size_t *size)
{
	int rc;

	data = data_unaliased(data);
	if (data_validate(data)) {
		rc = 0;
	}
	else {
		rc = -1;
	}
	if (pointer)
		*pointer = rc < 0 ? NULL : data->pointer;
	if (size)
		*size = rc < 0 ? 0 : data->size;
	return rc;
}
