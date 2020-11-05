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

#pragma once

struct afb_data;
struct afb_type;

/**
 *  Allocates a new data without type
 *
 * @param result where to store the allocated data
 *
 * @return 0 in case of success or a negative number on error
 */
extern
int
afb_data_create_raw(
	struct afb_data **result,
	struct afb_type *type,
	const void *pointer,
	size_t size,
	void (*dispose)(void*),
	void *closure
);

extern
int
afb_data_create_alloc(
	struct afb_data **result,
	struct afb_type *type,
	void **pointer,
	size_t size,
	int zeroes
);

extern
int
afb_data_create_copy(
	struct afb_data **result,
	struct afb_type *type,
	const void *pointer,
	size_t size
);

extern
int
afb_data_create_alias(
	struct afb_data **result,
	struct afb_type *type,
	struct afb_data *other
);

/**
 * Get the typenum of the data
 *
 * @param data the data to query
 *
 * @return the typenum of the data
 */
extern
struct afb_type *
afb_data_type(
	struct afb_data *data
);

/**
 * Increment reference count to the data and return it
 *
 * @param data the data to reference
 * @return the data
 */
extern
struct afb_data *
afb_data_addref(
	struct afb_data *data
);

/**
 * Decrement reference count to the data and release it if
 * not more referenced

 * @param data the data to dereference
 */
extern
void
afb_data_unref(
	struct afb_data *data
);

/**
 * Get the pointer of the data
 *
 * @param data the data
 *
 * @return the pointer of the data
 */
extern
const void*
afb_data_const_pointer(
	struct afb_data *data
);

/**
 * Get the size of the data
 *
 * @param data the data
 *
 * @return the size of the data
 */
extern
size_t
afb_data_size(
	struct afb_data *data
);

/**
 * Convert to an other data (possibly return a cached conversion)
 *
 * @param data the data
 * @param other the other data to convert from
 *
 * @return 0 in case of success, a negative code on error
 */
extern
int
afb_data_convert_to(
	struct afb_data *data,
	struct afb_type *type,
	struct afb_data **result
);

/* update a data */
extern
int
afb_data_update(
	struct afb_data *data,
	struct afb_data *value
);

/**
 * Clear cache of conversions but not the data itself
 *
 * @param data the data to clear
 */
extern
void
afb_data_notify_changed(
	struct afb_data *data
);

/* test if constant */
extern
int
afb_data_is_constant(
	struct afb_data *data
);

/* set as constant */
extern
void
afb_data_set_constant(
	struct afb_data *data
);

/* set as not constant */
extern
void
afb_data_set_not_constant(
	struct afb_data *data
);

/* test if volatile */
extern
int
afb_data_is_volatile(
	struct afb_data *data
);

/* set as volatile */
extern
void
afb_data_set_volatile(
	struct afb_data *data
);

/* set as not volatile */
extern
void
afb_data_set_not_volatile(
	struct afb_data *data
);

/* opacifies the data and returns its opaque id */
extern
int
afb_data_opacify(
	struct afb_data *data
);

/* get the data of the given opaque id */
extern
int
afb_data_get_opacified(
	int opaqueid,
	struct afb_data **data,
	struct afb_type **type
);

extern
int
afb_data_get_mutable(
	struct afb_data *data,
	void **pointer,
	size_t *size);

extern
int
afb_data_get_constant(
	struct afb_data *data,
	const void **pointer,
	size_t *size);

extern
void
afb_data_lock_read(
	struct afb_data *data);

extern
int
afb_data_try_lock_read(
	struct afb_data *data);

extern
void
afb_data_lock_write(
	struct afb_data *data);

extern
int
afb_data_try_lock_write(
	struct afb_data *data);

extern
void
afb_data_unlock(
	struct afb_data *data);
