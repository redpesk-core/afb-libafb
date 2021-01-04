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

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

#include "afb-type.h"
#include "afb-type-internal.h"
#include "afb-data.h"

#include "sys/x-errno.h"
#include "sys/x-rwlock.h"
#include "utils/jsonstr.h"


/*****************************************************************************/

/* the types */
static struct afb_type *known_types = &_afb_type_head_of_predefineds_;

/*****************************************************************************/

#if defined(AFB_TYPE_NO_LOCK)

/** takes the read lock */
static inline void lock_read() {}

/** takes the write lock */
static inline void lock_write() {}

/** unlock the gotten lock */
static inline void unlock() {}

#else

static x_rwlock_t  rwlock = X_RWLOCK_INITIALIZER;

/** takes the read lock */
static inline void lock_read() { x_rwlock_rdlock(&rwlock); }

/** takes the write lock */
static inline void lock_write() { x_rwlock_wrlock(&rwlock); }

/** unlock the gotten lock */
static inline void unlock() { x_rwlock_unlock(&rwlock); }

#endif

/*****************************************************************************/

static struct afb_type *search_type_locked(const char *name)
{
	struct afb_type *type = known_types;
	while (type && strcmp(name, type->name))
		type = type->next;
	return type;
}


int afb_type_register(struct afb_type **result, const char *name, int streamable, int shareable, int opaque)
{
	int rc;
	struct afb_type *type;

	lock_write();
	type = search_type_locked(name);
	if (type) {
		type = 0;
		rc = X_EEXIST;
	}
	else {
		type = malloc(sizeof *type);
		if (!type)
			rc = X_ENOMEM;
		else {
			type->name = name;
			if (opaque) {
				SET_OPAQUE(type);
			}
			else if (streamable) {
				SET_STREAMABLE(type);
				SET_SHAREABLE(type);
			}
			else if (shareable) {
				SET_SHAREABLE(type);
			}
			type->operations = 0;
			type->family = 0;
			type->flags = 0;
			type->op_count = 0;
			type->next = known_types;
			known_types = type;
			rc = 0;
		}
	}
	unlock();
	*result = type;
	return rc;
}

struct afb_type *afb_type_get(const char *name)
{
	struct afb_type *type;
	lock_read();
	type = search_type_locked(name);
	unlock();
	return type;
}

const char *afb_type_name(const struct afb_type *type)
{
	return type->name;
}

int afb_type_is_streamable(const struct afb_type *type)
{
	return IS_STREAMABLE(type);
}

int afb_type_is_shareable(const struct afb_type *type)
{
	return IS_SHAREABLE(type);
}

int afb_type_is_opaque(const struct afb_type *type)
{
	return IS_OPAQUE(type);
}

/**
 * Search in the operation list the operation matching the given kind and type.
 *
 * @param odsc   list of operation to be searched
 * @param kind   the kind of operation searched
 * @param type   the type searched
 *
 * @return the found operation or NULL
 */
static
struct opdesc *
searchop(
	struct afb_type *base_type,
	enum opkind kind,
	const struct afb_type *type
) {
	struct opdesc *odsc = base_type->operations;
	struct opdesc *end = &odsc[base_type->op_count];
	while (odsc != end) {
		if (odsc->kind == kind && odsc->type == type)
			return odsc;
		odsc++;
	}
	return 0;
}

static
int
operate(
	struct afb_type *from_type,
	struct afb_data *from_data,
	struct afb_type *to_type,
	struct afb_data **to_data,
	int convert
) {
	int rc;
	struct opdesc *odsc, *op, *opend;
	struct afb_type *type, *t;
	struct afb_data *xdata;

	/*
	 * Search direct conversion
	 * ------------------------
	 *
	 * A direct convertion is one given by an operation
	 * of convertion/update from the current type, or one of
	 * its family parent, to the targetted type.
	 */
	type = from_type;
	while (type) {
		/* search forward */
		odsc = searchop(type, convert ? Convert_To : Update_To, to_type);
		if (odsc) {
			rc = convert
				? odsc->converter(odsc->closure, from_data, to_type, to_data)
				: odsc->updater(odsc->closure, from_data, to_type, *to_data);
			if (rc >= 0)
				return rc;
		}
		/* search backward */
		odsc = searchop(to_type, convert ? Convert_From : Update_From, type);
		if (odsc) {
			rc = convert
				? odsc->converter(odsc->closure, from_data, to_type, to_data)
				: odsc->updater(odsc->closure, from_data, to_type, *to_data);
			if (rc >= 0)
				return rc;
		}
		/* not found, try an ancestor if one exists */
		type = type->family;
		if (type == to_type && convert) {
			/* implicit conversion to an ancestor of the family */
			rc = afb_data_create_alias(to_data, type, from_data);
			return rc;
		}
	}
	/*
	 * Fast search of indirect conversion
	 * ----------------------------------
	 *
	 * Indirect conversions are the one that imply two operations:
	 *  - one convertion from the original type to a middle type
	 *  - one conversion/update from the middle type to the target type
	 *
	 * Fast search assumes that the 2 operations are described as operations
	 * part of the original type, or one of its family parent, and/or the
	 * target type.
	 */
	type = from_type;
	while (type) {
		/* search forward: the middle type is given by a convert-to */
		op = type->operations;
		opend = &op[type->op_count];
		for ( ; op != opend ; op++) {
			if (op->kind != Convert_To)
				continue;
			/*
			 * the original type converts to middle type op->type,
			 * search how it can convert to target type to_type
			 */
			odsc = convert
				? (searchop(to_type, Convert_From, op->type)
					?: searchop(op->type, Convert_To, to_type))
				: (searchop(to_type, Update_From, op->type)
					?: searchop(op->type, Update_To, to_type));
			if (!odsc)
				continue;
			rc = op->converter(op->closure, from_data, op->type, &xdata);
			if (rc < 0)
				continue;
			rc = convert
				? odsc->converter(odsc->closure, xdata, to_type, to_data)
				: odsc->updater(odsc->closure, xdata, to_type, *to_data);
			afb_data_unref(xdata);
			if (rc >= 0)
				return rc;
		}
		/* search backward: the middle type is given by a convert/update-from */
		op = to_type->operations;
		opend = &op[to_type->op_count];
		for ( ; op != opend ; op++) {
			/*
			 * if the target type converts/updates from middle type op->type,
			 * search how it can convert from original type from_type.
			 * note that after forward search, some possibilities were already
			 * checked (i.e. Convert_To).
			 */
			if (convert) {
				if (op->kind != Convert_From)
					continue;
				odsc = searchop(op->type, Convert_From, type);
			} else {
				if (op->kind != Update_From)
					continue;
				odsc = searchop(op->type, Convert_From, type);
			}
			if (!odsc)
				continue;
			rc = odsc->converter(odsc->closure, from_data, op->type, &xdata);
			if (rc < 0)
				continue;
			rc = convert
				? op->converter(op->closure, xdata, to_type, to_data)
				: op->updater(op->closure, xdata, to_type, *to_data);
			afb_data_unref(xdata);
			if (rc >= 0)
				return rc;
		}
		type = type->family;
	}
	/*
	 * Long search of indirect conversion
	 * ----------------------------------
	 *
	 * The indirect conversion searched here are the one that carry
	 * both operations otherwise it already had been found.
	 */
	for (type = known_types ; type ; type = type->next) {
		/* avoid already checked types */
		for (t = from_type; t && t != type; t = t->family);
		if (t || type == to_type)
			continue;

		/* check if it can be a middle type */
		op = searchop(type, Convert_From, from_type);
		if (!op)
			continue;
		odsc = searchop(type, convert ? Convert_To : Update_To, to_type);
		if (!odsc)
			continue;
		rc = op->converter(op->closure, from_data, type, &xdata);
		if (rc < 0)
			continue;
		rc = convert
			? odsc->converter(odsc->closure, xdata, to_type, to_data)
			: odsc->updater(odsc->closure, xdata, to_type, *to_data);
		afb_data_unref(xdata);
		if (rc >= 0)
			return rc;
	}

	/* nothing found */
	if (convert)
		*to_data = 0;
	return X_ENOENT;
}

/* rc<0: error, 0: no conversion (same type), 1: converted */
int
afb_type_update_data(
	struct afb_type *from_type,
	struct afb_data *from_data,
	struct afb_type *to_type,
	struct afb_data *to_data
) {
	return operate(from_type, from_data, to_type, &to_data, 0);
}

int
afb_type_convert_data(
	struct afb_type *from_type,
	struct afb_data *from_data,
	struct afb_type *to_type,
	struct afb_data **to_data
) {
	return operate(from_type, from_data, to_type, to_data, 1);
}

static
int
add_op(
	struct afb_type *type,
	enum opkind kind,
	struct afb_type *totype,
	void *callback,
	void *closure
) {
	struct opdesc *desc;

	desc = searchop(type, kind, totype);
	if (!desc) {
		desc = realloc(type->operations, (1 + type->op_count) * sizeof *desc);
		if (!desc)
			return X_ENOMEM;
		type->operations = desc;
		desc += type->op_count++;
		desc->kind = kind;
		desc->type = totype;
	}
	desc->callback = callback;
	desc->closure = closure;
	return 0;
}

int afb_type_set_family(
	struct afb_type *type,
	struct afb_type *family
) {
	type->family = family;
	return 0;
}

int afb_type_add_converter(
	struct afb_type *type,
	struct afb_type *totype,
	afb_type_converter_t converter,
	void *closure
) {
	/* this method ensures that predefined types can be read only */
	if (!IS_PREDEFINED(type))
		return add_op(type, Convert_To, totype, converter, closure);
	if (!IS_PREDEFINED(totype))
		return add_op(totype, Convert_From, type, converter, closure);
	return X_EINVAL;
}

int afb_type_add_updater(
	struct afb_type *type,
	struct afb_type *totype,
	afb_type_updater_t updater,
	void *closure
) {
	/* this method ensures that predefined types can be read only */
	if (!IS_PREDEFINED(type))
		return add_op(type, Update_To, totype, updater, closure);
	if (!IS_PREDEFINED(totype))
		return add_op(totype, Update_From, type, updater, closure);
	return X_EINVAL;
}
