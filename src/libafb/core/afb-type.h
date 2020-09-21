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

typedef int (*afb_type_converter_t)(
			void *closure,
			struct afb_data *from,
			struct afb_type *type,
			struct afb_data **to);

typedef int (*afb_type_updater_t)(
			void *closure,
			struct afb_data *from,
			struct afb_type *type,
			struct afb_data *to);

extern int afb_type_register(struct afb_type **result, const char *name, int streamable, int shareable, int opaque);
extern struct afb_type *afb_type_get(const char *name);
extern const char *afb_type_name(const struct afb_type *type);

extern int afb_type_set_family(
	struct afb_type *type,
	struct afb_type *family
);

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
extern int afb_type_is_streamable(const struct afb_type *type);
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
