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

/**
 * Update the value of the given data with the given value
 *
 * @param data the data to be changed, must be mutable
 * @param value the value to set to data, possibly with convertion
 *
 * @return 0 on success or a negative -errno like value
 */
extern
int
afb_data_update(
	struct afb_data *data,
	struct afb_data *value
);

/**
 * Notifies that the data changed and that any of its conversions are not
 * more valid.
 *
 * @param data the data that changed
 */
extern
void
afb_data_notify_changed(
	struct afb_data *data
);

/**
 * Tests if the data is constant.
 *
 * @param data the data to test
 *
 * @return 1 if the data is constant or 0 otherwise
 */
extern
int
afb_data_is_constant(
	struct afb_data *data
);

/**
 * Makes the data constant
 *
 * @param data the data to set
 */
extern
void
afb_data_set_constant(
	struct afb_data *data
);

/**
 * Makes the data not constant
 *
 * @param data the data to set
 */
extern
void
afb_data_set_not_constant(
	struct afb_data *data
);

/**
 * Tests if the data is volatile. Conversions of volatile data are never cached.
 *
 * @param data the data to test
 *
 * @return 1 if the data is volatile or 0 otherwise
 */
extern
int
afb_data_is_volatile(
	struct afb_data *data
);

/**
 * Makes the data volatile
 *
 * @param data the data to set
 */
extern
void
afb_data_set_volatile(
	struct afb_data *data
);

/**
 * Makes the data not volatile
 *
 * @param data the data to set
 */
extern
void
afb_data_set_not_volatile(
	struct afb_data *data
);

/**
 * Opacifies the data and returns its opaque id
 *
 * @param data the data to be opacified
 *
 * @return a positive opaque id or a negative
 * value -errno like error value in case of error
 */
extern
int
afb_data_opacify(
	struct afb_data *data
);

/**
 * Get the data associated to the given opaque id
 *
 * @param opaqueid the id to search
 * @param data pointer to the returned data, must not be null
 * @param type pointer to the type of the returned data, must not be null
 *
 * @return 0 in case of success or a negative -errno like value (-EINVAL or -ENOENT)
 */
extern
int
afb_data_get_opacified(
	int opaqueid,
	struct afb_data **data,
	struct afb_type **type
);

/**
 * Gets a mutable pointer to the data and also its size
 *
 * @param data the data
 * @param pointer if not NULL address where to store the pointer
 * @param size if not NULL address where to store the size
 *
 * @return 0 in case of success or -1 in case of error
 */
extern
int
afb_data_get_mutable(
	struct afb_data *data,
	void **pointer,
	size_t *size);

/**
 * Gets a mutable pointer to the data.
 * Getting a mutable pointer has the effect of
 * notifying that the data changed.
 *
 * @param data the data
 *
 * @return the pointer (can be NULL)
 */
extern
int
afb_data_get_constant(
	struct afb_data *data,
	const void **pointer,
	size_t *size);

/**
 * Locks the data for read, blocks the current thread
 * until the data is available for reading.
 *
 * The data MUST be unlocked afterward using 'afb_data_unlock'
 *
 * @param data the data to lock for read
 */
extern
void
afb_data_lock_read(
	struct afb_data *data);

/**
 * Try to locks the data for read. Always return immediately
 * with a status indicating whether the data has been locked
 * for read or whether it wasn't possible to lock it for read.
 *
 * If the lock was successful, the data MUST be unlocked
 * afterward using 'afb_data_unlock'.
 *
 * @param data the data to lock for read
 *
 * @return 0 in case of success or a negative -errno status if not locked
 */
extern
int
afb_data_try_lock_read(
	struct afb_data *data);

/**
 * Locks the data for write, blocks the current thread
 * until the data is available for writing.
 *
 * The data MUST be unlocked afterward using 'afb_data_unlock'
 *
 * @param data the data to lock for write
 */
extern
void
afb_data_lock_write(
	struct afb_data *data);

/**
 * Try to locks the data for write. Always return immediately
 * with a status indicating whether the data has been locked
 * for write or whether it wasn't possible to lock it for write.
 *
 * If the lock was successful, the data MUST be unlocked
 * afterward using 'afb_data_unlock'.
 *
 * @param data the data to lock for write
 *
 * @return 0 in case of success or a negative -errno status if not locked
 */
extern
int
afb_data_try_lock_write(
	struct afb_data *data);

/**
 * Unlock a locked data. It is an error to unlock a data that
 * the current thread doesn't hold locked.
 *
 * @param data the data to unlock
 */
extern
void
afb_data_unlock(
	struct afb_data *data);
