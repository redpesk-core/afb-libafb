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

#include "../libafb-config.h"

#include <stdint.h>
#include <string.h>

#include "afb-type.h"
#include "afb-type-predefined.h"
#include "afb-data.h"
#include "afb-data-array.h"

/*
 * adds at offset the string to buffer stopping if size reached
 * returns the reached offset
 */
static inline size_t add(
	char *buffer,
	size_t size,
	size_t offset,
	const char *text
) {
	while (*text && offset < size)
		buffer[offset++] = *text++;
	return offset;
}


/* internal implementation of afb_data_array_print */
static size_t
do_print(
	char *buffer,
	size_t size,
	const char *const patterns[5],
	unsigned count,
	struct afb_data * const array[]
) {
	int rc;
	const char *str, *sep;
	struct afb_data *cvt, *item;
	unsigned index = 0;
	size_t length, saved, offset = 0;

	if (count == 0 && patterns[AFB_DATA_ARRAY_PRINT_EMPTY] != NULL)
		offset = add(buffer, size, offset, patterns[AFB_DATA_ARRAY_PRINT_EMPTY]);
	else {
		/* introduce prefix as separator */
		sep = patterns[AFB_DATA_ARRAY_PRINT_PREFIX] ?: "";

		/* process all items in the order */
		while (index < count && offset < size) {

			/* add item separator */
			offset = add(buffer, size, offset, sep);

			/* try to get the item as a string */
			item = array[index++];
			rc = afb_data_convert(item, &afb_type_predefined_stringz, &cvt);
			if (rc >= 0) {
				/* add string representation of the item */
				str = afb_data_ro_pointer(cvt);
				offset = add(buffer, size, offset, str ?: "(null)");
				afb_data_unref(cvt);
			}
			else {
				/* add type indication of the item */
				offset = add(buffer, size, offset, "<TYPE#");
				str = afb_type_name(afb_data_type(item));
				offset = add(buffer, size, offset, str ?: "?");
				offset = add(buffer, size, offset, ">");
			}

			/* set next separator */
			sep = patterns[AFB_DATA_ARRAY_PRINT_SEPARATOR] ?: "";
		}

		/* try normal termination  */
		saved = offset;
		if (patterns[AFB_DATA_ARRAY_PRINT_SUFFIX_FULL])
			offset = add(buffer, size, offset, patterns[AFB_DATA_ARRAY_PRINT_SUFFIX_FULL]);

		if (offset >= size) {
			str = patterns[AFB_DATA_ARRAY_PRINT_SUFFIX_TRUNCATED];
			if (str) {
				length = strlen(str);
				if (length >= size)
					offset = 0;
				else {
					offset = size - length - 1;
					offset = add(buffer, size, offset < saved ? offset : saved, str);
				}
			}
		}
	}

	/* terminates */
	if (offset >= size)
		offset = size - 1;
	buffer[offset] = 0;
	return offset;
}


/* see afb-data-array.h */
size_t
afb_data_array_print(
	char *buffer,
	size_t size,
	const char *const patterns[5],
	unsigned count,
	struct afb_data * const array[]
) {
	const char * const *pats, *defpats[5];

	/* avoid degenerated case */
	if (size == 0)
		return 0;

	/* initiate default array */
	if (patterns)
		pats = patterns;
	else {
		defpats[AFB_DATA_ARRAY_PRINT_EMPTY] = "";
		defpats[AFB_DATA_ARRAY_PRINT_PREFIX] = "";
		defpats[AFB_DATA_ARRAY_PRINT_SEPARATOR] = ", ";
		defpats[AFB_DATA_ARRAY_PRINT_SUFFIX_FULL] = "";
		defpats[AFB_DATA_ARRAY_PRINT_SUFFIX_TRUNCATED] = "...";
		pats = defpats;
	}
	return do_print(buffer, size, pats, count, array);
}

