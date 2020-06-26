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
struct afb_data_x4;
struct afb_type_x4;

/**
 */
extern
const struct afb_data_x4*
afb_data_as_data_x4(
	struct afb_data *data
);

/**
 */
extern
struct afb_data*
afb_data_of_data_x4(
	const struct afb_data_x4 *datax4
);

/**
 *  Allocates a new data without type
 *
 * @param result where to store the allocated data
 *
 * @return 0 in case of success or a negative number on error
 */
extern
int
afb_data_create_set_x4(
	struct afb_data **result,
	const struct afb_type_x4 *type,
	const void *pointer,
	size_t size,
	void (*dispose)(void*),
	void *closure
);

extern
int
afb_data_create_alloc_x4(
	struct afb_data **result,
	const struct afb_type_x4 *type,
	void **pointer,
	size_t size,
	int zeroes
);

extern
int
afb_data_create_copy_x4(
	struct afb_data **result,
	const struct afb_type_x4 *type,
	const void *pointer,
	size_t size
);



/**
 * Get the typenum of the data
 *
 * @param data the data to query
 *
 * @return the typenum of the data
 */
extern
const struct afb_type_x4 *
afb_data_type_x4(
	const struct afb_data *data
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
 * Clear the content of data, releasing memory and calling cleaners
 *
 * @param data the data to clear
 */
extern
void
afb_data_clear(
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
afb_data_pointer(
	const struct afb_data *data
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
	const struct afb_data *data
);

/**
 * set the data
 *
 * @param data    the data to set
 * @param type    the type x4
 * @param pointer pointer to the data
 * @param size    size of the data
 * @param dispose a function to call to release the data (can be NULL)
 * @param closure parameter to give to the function 'dispose'
 *
 * @return 0 in case of success or a negative number indicating the error
 */
extern
int
afb_data_set_x4(
	struct afb_data *data,
	const struct afb_type_x4 *type,
	const void *pointer,
	size_t size,
	void (*dispose)(void*),
	void *closure
);

/**
 * Allocate a shareable buffer. Also allows to resize
 * a previously allocated buffer.
 *
 * @param data    the data
 * @param type    the type x4
 * @param pointer where to store base address of the allocated memory
 * @param size    the size to (re)alloc
 * @param zeroes  put zeroes in allocated memory
 *
 * @return 0 if success or a negative error code
 */
extern
int
afb_data_alloc_x4(
	struct afb_data *data,
	const struct afb_type_x4 *type,
	void **pointer,
	size_t size,
	int zeroes
);

/**
 * Allocate a shareable buffer. Also allows to resize
 * a previously allocated buffer.
 *
 * @param data the data
 * @param pointer where to store base address of the allocated memory
 * @param size the size to (re)alloc
 * @param zeroes put zeroes in allocated memory
 *
 * @return 0 if success or a negative error code
 */
extern
int
afb_data_resize(
	struct afb_data *data,
	void **pointer,
	size_t size,
	int zeroes
);

/**
 * Allocate shareable memory and copy data to it
 *
 * @param data    the data to set
	const struct afb_type_x4 *type,
 * @param pointer pointer to the data
 * @param size    size of the data
 *
 * @return 0 in case of success or a negative number indicating the error
 */
extern
int
afb_data_copy_x4(
	struct afb_data *data,
	const struct afb_type_x4 *type,
	const void *pointer,
	size_t size
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
afb_data_convert_to_x4(
	struct afb_data *data,
	const struct afb_type_x4 *type,
	struct afb_data **result
);

/**
 * Clear cache of conversions but not the data itself
 *
 * @param data the dat to clear
 */
extern
void
afb_data_convert_cache_clear(
	struct afb_data *data
);

extern
int
afb_data_x4_create_set_x4(
	const struct afb_data_x4 **result,
	const struct afb_type_x4 *type,
	const void *pointer,
	size_t size,
	void (*dispose)(void*),
	void *closure
);

extern
int
afb_data_x4_create_alloc_x4(
	const struct afb_data_x4 **result,
	const struct afb_type_x4 *type,
	void **pointer,
	size_t size,
	int zeroes
);

extern
int
afb_data_x4_create_copy_x4(
	const struct afb_data_x4 **result,
	const struct afb_type_x4 *type,
	const void *pointer,
	size_t size
);

