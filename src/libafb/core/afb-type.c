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


#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

#include "afb-type.h"
#include "afb-type-internal.h"
#include "afb-type-predefined.h"
#include "afb-data.h"

#include "sys/x-errno.h"
#include "sys/x-rwlock.h"
#include "utils/jsonstr.h"


/*****************************************************************************/

/* the types */
static struct afb_type *known_types = &_afb_type_head_of_predefineds_;

/* the ids */
static uint16_t last_typeid = Afb_Typeid_First_Userid;

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
	else if (last_typeid > TYPEID_MAX) {
		type = 0;
		rc = X_ECANCELED;
	}
	else {
		type = malloc(sizeof *type);
		if (!type)
			rc = X_ENOMEM;
		else {
			type->name = name;
			type->operations = 0;
			type->family = 0;
			if (opaque)
				type->flags = FLAG_IS_OPAQUE;
			else if (streamable)
				type->flags = FLAG_IS_STREAMABLE|FLAG_IS_SHAREABLE;
			else if (shareable)
				type->flags = FLAG_IS_SHAREABLE;
			else
				type->flags = 0;
			type->op_count = 0;
			type->typeid = last_typeid++;
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

int afb_type_lookup(struct afb_type **type, const char *name)
{
	return (*type = afb_type_get(name)) ? 0 : X_ENOENT;
}

const char *afb_type_name(struct afb_type *type)
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
	struct opdesc *op, *opend;
	struct opdesc *op2, *opend2;
	struct afb_type *type, *t, *midtyp;
	struct afb_data *xdata;
	enum opkind opk;

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
		opk = convert ? Convert_To : Update_To;
		op = type->operations;
		opend = &op[type->op_count];
		for ( ; op != opend ; op++) {
			if (op->kind == opk && op->type == to_type) {
				rc = convert
					? op->converter(op->closure, from_data, to_type, to_data)
					: op->updater(op->closure, from_data, to_type, *to_data);
				if (rc >= 0)
					return rc;
			}
		}
		/* search backward */
		opk = convert ? Convert_From : Update_From;
		op = to_type->operations;
		opend = &op[to_type->op_count];
		for ( ; op != opend ; op++) {
			if (op->kind == opk && op->type == type) {
				rc = convert
					? op->converter(op->closure, from_data, to_type, to_data)
					: op->updater(op->closure, from_data, to_type, *to_data);
				if (rc >= 0)
					return rc;
			}
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
			midtyp = op->type;
			/*
			 * the original type converts to middle type,
			 * search how to_type can convert or update from middle type
			 */
			opk = convert ? Convert_From : Update_From;
			op2 = to_type->operations;
			opend2 = &op2[to_type->op_count];
			for ( ; op2 != opend2 ; op2++) {
				if (op2->kind == opk && op2->type == midtyp) {
					rc = op->converter(op->closure, from_data, midtyp, &xdata);
					if (rc >= 0) {
						rc = convert
							? op2->converter(op2->closure, xdata, to_type, to_data)
							: op2->updater(op2->closure, xdata, to_type, *to_data);
						afb_data_unref(xdata);
						if (rc >= 0)
							return rc;
					}
				}
			}
			/*
			 * the original type converts to middle type,
			 * search how middle type can convert or update to to_type
			 */
			opk = convert ? Convert_To : Update_To;
			op2 = midtyp->operations;
			opend2 = &op2[midtyp->op_count];
			for ( ; op2 != opend2 ; op2++) {
				if (op2->kind == opk && op2->type == to_type) {
					rc = op->converter(op->closure, from_data, midtyp, &xdata);
					if (rc >= 0) {
						rc = convert
							? op2->converter(op2->closure, xdata, to_type, to_data)
							: op2->updater(op2->closure, xdata, to_type, *to_data);
						afb_data_unref(xdata);
						if (rc >= 0)
							return rc;
					}
				}
			}
		}
		/* search backward: the middle type is given by a convert/update-from */
		opk = convert ? Convert_From : Update_From;
		op = to_type->operations;
		opend = &op[to_type->op_count];
		for ( ; op != opend ; op++) {
			if (op->kind != opk)
				continue;
			midtyp = op->type;
			/*
			 * if the target type converts/updates from middle type,
			 * search how it can convert from original type from_type.
			 * note that after forward search, some possibilities were already
			 * checked (i.e. Convert_To).
			 */
			opk = convert ? Convert_From : Update_From;
			op2 = midtyp->operations;
			opend2 = &op2[midtyp->op_count];
			for ( ; op2 != opend2 ; op2++) {
				if (op2->kind == Convert_From && op2->type == type) {
					rc = op2->converter(op2->closure, from_data, midtyp, &xdata);
					if (rc >= 0) {
						rc = convert
							? op->converter(op->closure, xdata, to_type, to_data)
							: op->updater(op->closure, xdata, to_type, *to_data);
						afb_data_unref(xdata);
						if (rc >= 0)
							return rc;
					}
				}
			}
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
	opk = convert ? Convert_To : Update_To;
	for (midtyp = known_types ; midtyp ; midtyp = midtyp->next) {
		/* avoid already checked types */
		if (midtyp == to_type)
			continue;
		for (t = from_type; t && t != midtyp; t = t->family);
		if (t)
			continue;

		/* check if it can be a middle type */
		op = midtyp->operations;
		opend = &op[midtyp->op_count];
		for ( ; op != opend ; op++) {
			if (op->kind != Convert_From || op->type != from_type)
				continue;
			/*
			 * if the target type converts/updates from middle type,
			 * search how it can convert from original type from_type.
			 * note that after forward search, some possibilities were already
			 * checked (i.e. Convert_To).
			 */
			op2 = midtyp->operations;
			opend2 = &op2[midtyp->op_count];
			for ( ; op2 != opend2 ; op2++) {
				if (op2->kind == opk && op2->type == to_type) {
					rc = op->converter(op->closure, from_data, midtyp, &xdata);
					if (rc >= 0) {
						rc = convert
							? op2->converter(op2->closure, xdata, to_type, to_data)
							: op2->updater(op2->closure, xdata, to_type, *to_data);
						afb_data_unref(xdata);
						if (rc >= 0)
							return rc;
					}
				}
			}
		}
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

	if (type->op_count == TYPE_OP_COUNT_MAX)
		return X_ECANCELED;
	desc = realloc(type->operations, (1 + type->op_count) * sizeof *desc);
	if (!desc)
		return X_ENOMEM;
	type->operations = desc;
	desc += type->op_count++;
	desc->kind = kind;
	desc->type = totype;
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

/* Get the typeid */
uint16_t afb_typeid(
	const struct afb_type *type
) {
	return type->typeid;
}
