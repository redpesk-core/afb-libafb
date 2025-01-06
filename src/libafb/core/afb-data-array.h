/*
 * Copyright (C) 2015-2025 IoT.bzh Company
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

#include <string.h>
#include "afb-data.h"

/**
 * Increase the count of references of the array 'array'
 * that has 'count' data.
 *
 * @param count the count of data in the array
 * @param array the array of data
 */
static inline
void
afb_data_array_addref(
	unsigned count,
	struct afb_data * const array[]
) {
	for (unsigned index = 0 ; index < count ; index++)
		afb_data_addref(array[index]);
}

/**
 * Decrease the count of references of the array 'array'
 * that has 'count' data.
 *
 * Call this function when the data is no more used.
 * It destroys the data when the reference count falls to zero.
 *
 * @param count the count of data in the array
 * @param array the array of data
 */
static inline
void
afb_data_array_unref(
	unsigned count,
	struct afb_data * const array[]
) {
	for (unsigned index = 0 ; index < count ; index++)
		afb_data_unref(array[index]);
}

/**
 * Get a new instances of items of 'array_data' converted to the
 * corresponding types of 'array_type'
 *
 * If a data are returned (no error case), they MUST be released
 * using afb_data_unref.
 *
 * @param count        count of items in the arrays
 * @param array_data   array of original data
 * @param array_type   array of expected types
 * @param array_result array for storing result of conversions
 *
 * @return 0 in case of success or a negative value indication the error.
 */
static inline
int
afb_data_array_convert(
	unsigned count,
	struct afb_data * const array_data[],
	struct afb_type * const array_type[],
	struct afb_data * array_result[]
) {
	int rc = 0;
	unsigned index = 0;

	while (rc >= 0 && index < count) {
		rc = afb_data_convert(array_data[index], array_type[index], &array_result[index]);
		if (rc >= 0)
			index++;
		else {
			while (index)
				afb_data_unref(array_result[--index]);
			while (index < count)
				array_result[index++] = 0;
		}
	}
	return rc;
}

/**
 * Copy the data from an array to an other.
 * It does not increase the reference count.
 * If increasing the reference count is needed
 * you can use the function afb_data_array_copy_addref
 *
 * @param count the count of data in the array
 * @param array_from array of data to copy
 * @param array_to destination array of the copy
 */
static inline
void
afb_data_array_copy(
	unsigned count,
	struct afb_data * const array_from[],
	struct afb_data * array_to[]
) {
	if (count)
		memcpy(array_to, array_from, count * sizeof *array_to);
}

/**
 * Copy the data from an array to an other and
 * increment the reference count for each copied data.
 *
 * @param count the count of data in the array
 * @param array_from array of data to copy
 * @param array_to destination array of the copy
 */
static inline
void
afb_data_array_copy_addref(
	unsigned count,
	struct afb_data * const array_from[],
	struct afb_data * array_to[]
) {
	for (unsigned index = 0 ; index < count ; index++)
		array_to[index] = afb_data_addref(array_from[index]);
}

#define AFB_DATA_ARRAY_PRINT_EMPTY 0
#define AFB_DATA_ARRAY_PRINT_PREFIX 1
#define AFB_DATA_ARRAY_PRINT_SEPARATOR 2
#define AFB_DATA_ARRAY_PRINT_SUFFIX_FULL 3
#define AFB_DATA_ARRAY_PRINT_SUFFIX_TRUNCATED 4

/**
 * Prints as well as possible values of data array in buffer
 *
 * when no data exist, i.e. count == 0, prints the 'empty' string
 * (i.e. patterns[AFB_DATA_ARRAY_PRINT_EMPTY]) if it is not NULL
 *
 * when count > 0 then
 *   - print the 'prefix' string (i.e. patterns[AFB_DATA_ARRAY_PRINT_EMPTY])
 *     if not NULL
 *   - print the itams of the array separated each another with
 *     the 'separator' string (i.e. patterns[AFB_DATA_ARRAY_PRINT_SEPARATOR])
 *     if not NULL
 *   - print the 'suffix' string (i.e. patterns[AFB_DATA_ARRAY_PRINT_SUFFIX_FULL])
 *     if not NULL
 *   - check that all characters were in the buffer. If it is not the case
 *     and if the 'truncated' string (i.e. patterns[AFB_DATA_ARRAY_PRINT_SUFFIX_TRUNCATED])
 *     is not NULL then put it at the end of the buffer
 *
 * @param buffer buffer where to print the value
 * @param size  size of the buffer
 * @param patterns array of formatting patterns
 * @param count the count of data in the array
 * @param array the array of data
 *
 * @returns the count of characters written not including terminating null
 */
extern
size_t
afb_data_array_print(
	char *buffer,
	size_t size,
	const char * const patterns[5],
	unsigned count,
	struct afb_data * const array[]
);
