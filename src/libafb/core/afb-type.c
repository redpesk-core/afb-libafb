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
			type->flags = 0;
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

const char *afb_type_name(struct afb_type *type)
{
	return type->name;
}

int afb_type_is_streamable(struct afb_type *type)
{
	return IS_STREAMABLE(type);
}

int afb_type_is_shareable(struct afb_type *type)
{
	return IS_SHAREABLE(type);
}

int afb_type_is_opaque(struct afb_type *type)
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
	enum opkind kind
) {
	int rc;
	struct opdesc *odsc;
	struct afb_type *type, *family;

	/* iterate the family */
	type = from_type;
	while (type) {
		family = 0;
		/* inspect operations */
		for (odsc = type->operations; odsc; odsc = odsc->next) {
			if (odsc->kind == Family) {
				/* record the family */
				family = odsc->type;
			}
			else if (odsc->kind == kind && odsc->type == to_type) {
				/* operation for the destination type */
				if (odsc->kind == Convert) {
					/* conversion case */
					rc = odsc->converter(odsc->closure, from_data, to_type, to_data);
					if (rc >= 0) {
						return rc;
					}
				} else {
					/* update case */
					rc = odsc->updater(odsc->closure, from_data, to_type, *to_data);
					if (rc >= 0) {
						return rc;
					}
				}
			}
		}
		/* not found, try an ancestor if one exists */
		type = family;
		if (type == to_type && kind == Convert) {
			/* implicit conversion to an ancestor of the family */
			rc = afb_data_create_alias(to_data, type, from_data);
			return rc;
		}
	}
	/* no operation found or succesful */
	if (kind == Convert)
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
	return operate(from_type, from_data, to_type, &to_data, Update);
}

int
afb_type_convert_data(
	struct afb_type *from_type,
	struct afb_data *from_data,
	struct afb_type *to_type,
	struct afb_data **to_data
) {
	return operate(from_type, from_data, to_type, to_data, Convert);
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

	desc = type->operations;
	while (desc && (desc->kind != kind || desc->type != totype)) {
		desc = desc->next;
	}
	if (!desc) {
		desc = malloc(sizeof *desc);
		if (!desc)
			return X_ENOMEM;

		desc->kind = kind;
		desc->type = totype;
		desc->next = type->operations;
		type->operations = desc;
	}
	desc->callback = callback;
	desc->closure = closure;
	return 0;
}

int afb_type_set_family(
	struct afb_type *type,
	struct afb_type *family
) {
	return add_op(type, Family, family, 0, 0);
}

int afb_type_add_converter(
	struct afb_type *type,
	struct afb_type *totype,
	afb_type_converter_t converter,
	void *closure
) {
	return add_op(type, Convert, totype, converter, closure);
}

int afb_type_add_updater(
	struct afb_type *type,
	struct afb_type *totype,
	afb_type_updater_t updater,
	void *closure
) {
	return add_op(type, Update, totype, updater, closure);
}

