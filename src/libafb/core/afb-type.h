/*
 * Copyright (C) 2015-2023 IoT.bzh Company
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
 * The type afb_type_converter_x4_t denote a conversion callback.
 *
 * A conversion callback receives 4 parameters:
 *
 * @param closure   A closure defined when converter is declared
 * @param from      The data to convert
 * @param type      The type to convert to
 * @param to        Where to store the result of the conversion
 *
 * @return It should return an integer status of 0 in case of success
 *         or a negative value indicating the error.
 */
typedef int (*afb_type_converter_t)(
			void *closure,
			struct afb_data *from,
			struct afb_type *type,
			struct afb_data **to);

/**
 * The type afb_type_updater_x4_t denote a conversion callback that is able
 * to update the target instead of creating it.
 *
 * A conversion callback receives 4 parameters:
 *
 * @param  closure   A closure defined when converter is declared
 * @param  from      The data of reference
 * @param  type      The type of the data to update
 * @param  to        the existing data to update from the given reference
 *
 * @return It should return an integer status of 0 in case of success
 *         or a negative value indicating the error.
 */
typedef int (*afb_type_updater_t)(
			void *closure,
			struct afb_data *from,
			struct afb_type *type,
			struct afb_data *to);

/**
 * Register a type
 *
 * @param type pointer to the returned created type
 * @param name name of the type to be created
 * @param streamable boolean, true if type can be streamed
 * @param shareable boolean, true if type can be shared through memory
 * @param opaque boolean, true if type can be opacified
 *
 * @return 0 in case of success or a negative error code
 */
extern int afb_type_register(struct afb_type **result, const char *name, int streamable, int shareable, int opaque);

/**
 * Get the type of given name
 *
 * @param name queried type name
 *
 * @return the found type of NULL if not found
 */
extern struct afb_type *afb_type_get(const char *name);

/**
 * Lookup for an existing type
 *
 * @param type pointer to the type returned if found
 * @param name name of the searched type
 *
 * @return 0 in case of success or a negative error code
 */
extern int afb_type_lookup(struct afb_type **type, const char *name);

/**
 * Get the name of a type
 *
 * @param type the type whose name is queried
 *
 * @return the name of the type
 */
extern const char *afb_type_name(struct afb_type *type);

/**
 * Set the family of the type. An instance of a type naturally converts
 * to an instance of its family.
 *
 * @param type the type whose family is to update
 * @param family the family to set to the type
 *
 * @return 0 on success or a negative -errno like error code
 */
extern int afb_type_set_family(
	struct afb_type *type,
	struct afb_type *family
);

/**
 * Add a convertion routine to a given type
 *
 * @param fromtype the reference from type
 * @param totype the type to convert to
 * @param converter the converter routine
 * @param closure the closure for the converter
 *
 * @return 0 in case of success or a negative error code
 */
extern int afb_type_add_converter(
	struct afb_type *fromtype,
	struct afb_type *totype,
	afb_type_converter_t converter,
	void *closure
);

extern int afb_type_add_updater(
	struct afb_type *fromtype,
	struct afb_type *totype,
	afb_type_updater_t updater,
	void *closure
);

/**
 * Is the given type opaque
 *
 * @param type type to test
 *
 * @return 1 if opaque, 0 if not
 */
extern int afb_type_is_opaque(const struct afb_type *type);

/**
 * Is the given type streamable
 *
 * @param type type to test
 *
 * @return 1 if streamable, 0 if not
 */
extern int afb_type_is_streamable(const struct afb_type *type);

/**
 * Is the given type shareable
 *
 * @param type type to test
 *
 * @return 1 if shareable, 0 if not
 */
extern int afb_type_is_shareable(const struct afb_type *type);

extern
int
afb_type_convert_data(
	struct afb_type *from_type,
	struct afb_data *from_data,
	struct afb_type *to_type,
	struct afb_data **to_data
);

extern
int
afb_type_update_data(
	struct afb_type *from_type,
	struct afb_data *from_data,
	struct afb_type *to_type,
	struct afb_data *to_data
);

/**
 * Get the typeid
 *
 * @param type type whose id is queried
 *
 * @return Return the typeid to 'type'
 */
extern uint16_t afb_typeid(const struct afb_type *type);
