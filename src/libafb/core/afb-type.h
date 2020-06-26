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

struct afb_type_x4;

/**
 * Register a type of name and returns a numeric id for it.
 * The name is used as a unique identifier to a type.
 *
 * @param type regiter a type
 *
 * @return the id of the registered type if greater than zero
 * or zero if the type can't be registered.
 */
extern int afb_type_register_type_x4(const struct afb_type_x4 *type);


/**
 * Get the name of the registered type of id
 *
 * @param id the id of the type
 *
 * @return the name if id is valid or NULL if not valid
 */
extern const char *afb_type_name_of_id(int id);

/**
 * Check if id is a valid registered type
 *
 * @param id the id to check
 *
 * @return 1 if valid or 0 if not valid
 */
extern int afb_type_is_valid_id(int id);

/**
 * Get the id of a registered name.
 *
 * @param name the unique name of the type to get
 *
 * @return the id of the registered type if greater than zero
 * or zero if the type isn't registered.
 */
extern int afb_type_id_of_name(const char *name);

extern
int
afb_type_convert_data_x4(
	struct afb_data *from_data,
	const struct afb_type_x4 *to_type,
	struct afb_data **to_data
);
