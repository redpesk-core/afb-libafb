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
 * structure recording dependency relationship of a data to an other
 */
struct datadep
{
	/** the linked data */
	struct afb_data *other;

	/** next item if any */
	struct datadep *next;
};

/**
 * Description of a data
 *
 * The pointer cvt is used to link together data that result from convertion.
 * The pointer records a circular list data. The elements of that list are
 * linked together by the equivalency relation "convert".
 *
 * The conversion is thinked as reflexive, symetric and transitive.
 * So formerly, cvt pointer record an equivalence class for "convert".
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

	/** dependencies to other data */
	struct datadep *dependof;

	/** flags */
	uint16_t flags;

	/** reference count */
	uint16_t refcount;

	/** dependency count */
	uint16_t depcount;

	/** opaque id of the data */
	uint16_t opaqueid;
};

/*****************************************************************************/
/***    Opacifier  ***/
/*****************************************************************************/

/**
 * opaqueidgen records the last opaqueid generated
 */
static uint16_t opaqueidgen;

/**
 * When existing, the structure associates a pointer to an opaqueid
 */
static struct u16id2ptr *opacifier;

/*****************************************************************************/
/***    Flag values  ***/
/*****************************************************************************/

#define FLAG_IS_VOLATILE        1
#define FLAG_IS_CONSTANT        2
#define FLAG_IS_VALID           4
#define FLAG_IS_LOCKED          8
#define FLAG_IS_ALIAS          16

#define REF_COUNT_ETERNAL      1
#define REF_COUNT_INCREMENT    2

#define INITIAL_FLAGS_STD     (FLAG_IS_CONSTANT|FLAG_IS_VALID)
#define INITIAL_FLAGS_ALIAS   (FLAG_IS_CONSTANT|FLAG_IS_VALID|FLAG_IS_ALIAS)

#define HASREF(data)           ((data)->refcount != 0)
#define ADDREF(data)           __atomic_add_fetch(&(data)->refcount, REF_COUNT_INCREMENT, __ATOMIC_RELAXED)
#define UNREF(data)            __atomic_sub_fetch(&(data)->refcount, REF_COUNT_INCREMENT, __ATOMIC_RELAXED)
#define SET_ETERNAL(data)      ((data)->refcount = REF_COUNT_ETERNAL)

#define HASDEP(data)           ((data)->depcount != 0)
#define ADDDEP(data)           __atomic_add_fetch(&(data)->depcount, 1, __ATOMIC_RELAXED)
#define UNDEP(data)            __atomic_sub_fetch(&(data)->depcount, 1, __ATOMIC_RELAXED)

#define TEST_FLAGS(data,flag)  (__atomic_load_n(&((data)->flags), __ATOMIC_RELAXED) & (flag))
#define SET_FLAGS(data,flag)   (__atomic_or_fetch(&((data)->flags), flag, __ATOMIC_RELAXED))
#define UNSET_FLAGS(data,flag) (__atomic_and_fetch(&((data)->flags), ~flag, __ATOMIC_RELAXED))

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
/***    dependof  ***/
/*****************************************************************************/

/** allocator of dependencies */
static inline
struct datadep *
dependof_alloc()
{
	return malloc(sizeof(struct datadep));
}

/** freeer of dependencies */
static inline
void
dependof_free(struct datadep *datadep)
{
	return free(datadep);
}

/**
 * increment dependency count of the data
 */
static inline
void
data_inc_depcount(
	struct afb_data *data
) {
	ADDDEP(data);
}

static void data_release(struct afb_data *data);

/**
 * decrement dependency count of the data
 */
static inline
void
data_dec_depcount(
	struct afb_data *data
) {
	if (!UNDEP(data))
		data_release(data);
}

/**
 * Add one explicit dependency from @p data to @p other
 *
 * @param data the data that has a dependency
 * @param other dependency
 *
 * @return 0 in case of success or X_ENOMEM on error
 */
static inline
int
data_add_dependof(
	struct afb_data *data,
	struct afb_data *other
) {
	struct datadep *dep;

	dep = dependof_alloc();
	if (dep == NULL)
		return X_ENOMEM;
	dep->other = other;
	dep->next = data->dependof;
	data->dependof = dep;
	data_inc_depcount(other);

	return 0;
}

/**
 * Remove one dependency from @p data to @p other
 *
 * @param data the data that has a dependency
 * @param other dependency
 *
 * @return 0 in case of success or X_ENOENT if no dependency was
 * recorded from data to other
 */
static inline
int
data_del_dependof(
	struct afb_data *data,
	struct afb_data *other
) {
	struct datadep *iter, **pprv;

	pprv = &data->dependof;
	while ((iter = *pprv) != NULL) {
		if (iter->other == other) {
			data_dec_depcount(other);
			*pprv = iter->next;
			free(iter);
			return 0;
		}
		pprv = &iter->next;
	}
	return X_ENOENT;
}

/**
 * Remove all dependency of the data
 *
 * @param data the data whose dependencies are to be removed
 */
static inline
void
data_del_all_dependof(
	struct afb_data *data
) {
	struct datadep *dependof, *nextdependof;

	dependof = data->dependof;
	if (dependof != NULL) {
		data->dependof = NULL;
		do {
			nextdependof = dependof->next;
			data_dec_depcount(dependof->other);
			free(dependof);
			dependof = nextdependof;
		} while (dependof != NULL);
	}
}

/*****************************************************************************/
/***    Internal routines  ***/
/*****************************************************************************/

/** data allocator */
static inline
struct afb_data *
data_alloc()
{
	return malloc(sizeof(struct afb_data));
}

/** data freeer */
static inline
void
data_free(struct afb_data *data)
{
	free(data);
}

/**
 * Increment reference count of data (not null)
 */
static inline
void
data_addref(
	struct afb_data *data
) {
	if (!ADDREF(data))
		SET_ETERNAL(data);
}

/**
 * really destroys the data and release (dispose) its resources
 */
static inline
void
data_destroy(
	struct afb_data *data
) {
	/* clean dependencies to other data */
	data_del_all_dependof(data);

	/* cancel any opacified shadow */
	if (data->opaqueid)
		u16id2ptr_drop(&opacifier, data->opaqueid, 0);

	/* release resource */
	if (data->dispose)
		data->dispose(data->closure);

	/* release the data itself */
	data_free(data);
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
 * Purge unused duplicate conversions
 */
__attribute__((unused))
static
void
data_purge_duplicates(
	struct afb_data *data
) {
	struct afb_data *iter, *prev;

	/* check if referenced */
	prev = data;
	iter = data->cvt;
	while (iter != data) {
		if (!HASREF(iter) && data_cvt_search(iter->cvt, iter->type) != iter) {
			/* unreferenced and duplicated type! destroy it */
			prev->cvt = iter->cvt;
			data_destroy(iter);
			iter = prev->cvt;
		}
		else {
			/* next */
			prev = iter;
			iter = iter->cvt;
		}
	}
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
	int hasref;
	struct afb_data *iter, *next, *head, *tail, *rest;

#if PREFER_MEMORY /* this optimisation reduce the use of memory but is slower */
	data_purge_duplicates(data);
#endif

	/* check if cleanup is needed */
	hasref = HASREF(data);
	iter = data->cvt;
	while (!hasref && iter != data) {
		hasref = HASREF(iter);
		iter = iter->cvt;
	}

	/* cleanup if no more refence to the cvt */
	if (!hasref) {
		/* split in two parts */
		head = NULL;
		tail = NULL;
		rest = NULL;
		iter = data;
		do {
			next = iter->cvt;
			if (HASDEP(iter)) {
				/* keep that part */
				iter->cvt = tail;
				if (tail == NULL)
					head = iter;
				tail = iter;
			}
			else {
				/* candidate to direct removal */
				iter->cvt = rest;
				rest = iter;
			}
			iter = next;
		} while (iter != data);

		/* ensure ring of kept part */
		if (head != NULL)
			head->cvt = tail;

		/* destroys the removed part */
		iter = rest;
		while (iter != NULL) {
			next = iter->cvt;
			iter->cvt = iter;
			data_destroy(iter);
			iter = next;
		}
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
 * remove any conversion of that data
 */
static
void
data_cvt_isolate(
	struct afb_data *data
) {
	struct afb_data *i;

	i = data->cvt;
	if (i != data) {
		do { i = i->cvt; } while (i->cvt != data);
		i->cvt = data->cvt;
		data->cvt = data;
		data_release(i);
	}
}

/**
 * test if item is in the conversion ring of data
 */
__attribute__((unused))
static
int
data_cvt_has(
	struct afb_data *data,
	struct afb_data *item
) {
	struct afb_data *i = data;
	do {
		if (i == item)
			return 1;
		i = i->cvt;
	} while (i != data);
	return 0;
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

	for (i = origin->cvt ; i->cvt != origin ; i = i->cvt);
	for (j = data->cvt ; j->cvt != data ; j = j->cvt);
	i->cvt = data;
	j->cvt = origin;
}

/**
 * unalias the data
 */
static inline
struct afb_data *
data_unaliased(
	struct afb_data *data
) {
	while (IS_ALIAS(data))
		data = (struct afb_data*)data->closure;
	return data;
}

/**
 */
static
void
data_make_alias(
	struct afb_data *alias,
	struct afb_data *to_data
) {
	// ASSERT NOT IS_VALID(alias)
	SET_ALIAS(alias);
	data_inc_depcount(to_data);
	alias->closure = to_data;
	alias->dispose = (void(*)(void*))data_dec_depcount;
	data_cvt_merge(alias, to_data);
}

/**
 */
static
struct afb_data *
data_value_constant(
	struct afb_data *data
) {
	struct afb_data *u, *r, *i;

	/* unalias first */
	u = data_unaliased(data);

	/* fast check */
	if (IS_VALID(u))
		return u;

	/* not valid, search if alias of some valid data */
	for (i = u->cvt ; i != u ; i = i->cvt) {
		if (i->type == u->type) {
			r = data_unaliased(i);
			if (IS_VALID(r)) {
				data_make_alias(data, r);
				return r;
			}
		}
	}

	/* no valid data, try to make a conversion */
	for (i = u->cvt ; i != u ; i = i->cvt) {
		if (!IS_ALIAS(i) && IS_VALID(i) && afb_type_convert_data(i->type, i, u->type, &r) >= 0) {
			data_make_alias(data, r);
			return r;
		}
	}
	return 0;
}

/**
 */
static
struct afb_data *
data_value_mutable(
	struct afb_data *data
) {
	struct afb_data *u, *r, *i;

	/* unalias first */
	u = data;
	while (IS_ALIAS(u)) {
		u = (struct afb_data*)u->closure;
		if (u->type != data->type)
			return 0;
	}

	/* fast check */
	if (IS_CONSTANT(u))
		return 0;
	if (IS_VALID(u))
		return u;

	/* not valid, search if alias of some valid data */
	for (i = u->cvt ; i != u ; i = i->cvt) {
		if (i->type == u->type) {
			r = data_unaliased(i);
			if (IS_VALID(r) && !IS_CONSTANT(r)) {
				data_make_alias(data, r);
				return r;
			}
		}
	}

	/* no valid data, try to make a conversion */
	for (i = u->cvt ; i != u ; i = i->cvt) {
		if (!IS_ALIAS(i) && IS_VALID(i) && afb_type_convert_data(i->type, i, u->type, &r) >= 0) {
			UNSET_CONSTANT(r);
			data_make_alias(data, r);
			return r;
		}
	}
	return 0;
}

/**
 * common data creator
 *
 * @param result  pointer to the created data
 * @param type    type of the data to create
 * @param pointer pointer wrapped by the data
 * @param size    size of the pointed data or 0 if it does not care
 * @param dispose a function to release the wrapped data (can be NULL)
 * @param closure closure for the dispose function
 * @param flags   flags to set
 *
 * @return 0 in case of success or a negative number on error
 */
int
data_create(
	struct afb_data **result,
	struct afb_type *type,
	const void *pointer,
	size_t size,
	void (*dispose)(void*),
	void *closure,
	uint16_t flags
) {
	int rc;
	struct afb_data *data;

	*result = data = data_alloc();
	if (data == NULL)
		rc = X_ENOMEM;
	else  {
		data->type = type;
		data->pointer = pointer;
		data->size = size;
		data->dispose = dispose;
		data->closure = closure;
		data->cvt = data; /* single cvt ring: itself */
		data->flags = flags;
		data->refcount = REF_COUNT_INCREMENT;
		data->depcount = 0;
		data->dependof = NULL;
		data->opaqueid = 0;
		rc = 0;
	}
	return rc;
}


/*****************************************************************************/
/***    Public routines  ***/
/*****************************************************************************/

/* creates a data */
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

	rc = data_create(result, type, pointer, size, dispose, closure, INITIAL_FLAGS_STD);
	if (rc && dispose)
		dispose(closure);

	return rc;
}

/* allocates a data */
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

	/* alloc shared memory */
	p = share_realloc(NULL, size);
	if (p == NULL) {
		*result = NULL;
		p = NULL;
		rc = X_ENOMEM;
	}
	else {
		/* create the data */
		rc = afb_data_create_raw(result, type, p, size, share_free, p);
		if (rc < 0)
			p = NULL;
		else if (zeroes)
			memset(p, 0, size);
	}
	*pointer = p;
	return rc;
}

/* allocate data */
int
afb_data_create_alloc0(
	struct afb_data **result,
	struct afb_type *type,
	void **pointer,
	size_t size
) {
	return afb_data_create_alloc(result, type, pointer, size, 1);
}

/* allocate data copy */
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

/* create an alias */
int
afb_data_create_alias(
	struct afb_data **result,
	struct afb_type *type,
	struct afb_data *other
) {
	int rc;

	rc = data_create(result, type, NULL, 0, (void*)data_dec_depcount, other, INITIAL_FLAGS_ALIAS);
	if (rc == 0)
		data_inc_depcount(other);

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
void*
afb_data_ro_pointer(
	struct afb_data *data
) {
	data = data_value_constant(data);
	return data ? (void*)data->pointer : NULL;
}

/* Get the size. */
size_t
afb_data_size(
	struct afb_data *data
) {
	data = data_value_constant(data);
	return data ? data->size : 0;
}

/* add conversion from other data */
int
afb_data_convert(
	struct afb_data *data,
	struct afb_type *type,
	struct afb_data **result
) {
	int rc;
	struct afb_data *r, *v;

	v = data_value_constant(data);
	if (!v) {
		/* original data is not valid */
		r = 0;
		rc = X_EINVAL;
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
			rc = afb_type_convert_data(data->type, v, type, &r);
			if (rc >= 0 && !afb_data_is_volatile(data)) {
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
	struct afb_data *from, *to;

	to = data_value_mutable(data);
	from = data_value_constant(value);
	if (!to || !from) {
		/* can not update based on parameter state */
		return X_EINVAL;
	}

	return afb_type_update_data(value->type, from, data->type, to);
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
	data = data_unaliased(data);
	data_cvt_changed(data);
}

/* test if constant */
int
afb_data_is_constant(
	struct afb_data *data
) {
	data = data_unaliased(data);
	return IS_CONSTANT(data);
}

/* set as constant */
void
afb_data_set_constant(
	struct afb_data *data
) {
	data = data_unaliased(data);
	SET_CONSTANT(data);
}

/* set as not constant */
void
afb_data_set_not_constant(
	struct afb_data *data
) {
	data = data_unaliased(data);
	UNSET_CONSTANT(data);
}

/* test if volatile */
int
afb_data_is_volatile(
	struct afb_data *data
) {
	data = data_unaliased(data);
	return IS_VOLATILE(data);
}

/* set as volatile */
void
afb_data_set_volatile(
	struct afb_data *data
) {
	data = data_unaliased(data);
	SET_VOLATILE(data);
	data_cvt_isolate(data);
}

/* set as not volatile */
void
afb_data_set_not_volatile(
	struct afb_data *data
) {
	data = data_unaliased(data);
	UNSET_VOLATILE(data);
}

/* get locker */
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

/* lock for reading */
void afb_data_lock_read(struct afb_data *data)
{
	data = data_unaliased(data);
	lockany_lock_read(lockhead(data));
}

/* try lock for reading */
int afb_data_try_lock_read(struct afb_data *data)
{
	data = data_unaliased(data);
	return lockany_try_lock_read(lockhead(data));
}

/* lock for writing */
void afb_data_lock_write(struct afb_data *data)
{
	data = data_unaliased(data);
	lockany_lock_write(lockhead(data));
}

/* try lock for writing */
int afb_data_try_lock_write(struct afb_data *data)
{
	data = data_unaliased(data);
	return lockany_try_lock_write(lockhead(data));
}

/* unlock */
void afb_data_unlock(struct afb_data *data)
{
	struct afb_data *head = lockhead(data_unaliased(data));
	if (!lockany_unlock(head))
		UNSET_LOCKED(head);
}

/* get values for read/write */
int afb_data_get_mutable(struct afb_data *data, void **pointer, size_t *size)
{
	int rc;

	data = data_value_mutable(data);
	if (data) {
		if (pointer)
			*pointer = (void*)data->pointer;
		if (size)
			*size = data->size;
		rc = 0;
	}
	else {
		if (pointer)
			*pointer = 0;
		if (size)
			*size = 0;
		rc = X_EINVAL;
	}
	return rc;
}

/* get values for read */
int afb_data_get_constant(struct afb_data *data, void **pointer, size_t *size)
{
	int rc;

	data = data_value_constant(data);
	if (data) {
		if (pointer)
			*pointer = (void*)data->pointer;
		if (size)
			*size = data->size;
		rc = 0;
	}
	else {
		if (pointer)
			*pointer = 0;
		if (size)
			*size = 0;
		rc = X_EINVAL;
	}
	return rc;
}

/* add dependency */
int afb_data_dependency_add(struct afb_data *from_data, struct afb_data *to_data)
{
	return from_data == to_data ? X_EINVAL : data_add_dependof(from_data, to_data);
}

/* remove dependency */
int afb_data_dependency_sub(struct afb_data *from_data, struct afb_data *to_data)
{
	return from_data == to_data ? X_EINVAL : data_del_dependof(from_data, to_data);
}

/* remove all dependencies */
void afb_data_dependency_drop_all(struct afb_data *data)
{
	data_del_all_dependof(data);
}
