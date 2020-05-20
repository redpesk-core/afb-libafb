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

#include "afb-string-mode.h"

struct json_object;

/**
 * structure for handling replies
 */
struct afb_req_reply
{
	/** the replied object if any */
	struct json_object *object;

	/** the replied error if any */
	char *error;

	/** the replied info if any */
	char *info;

	/** object mode */
	int8_t object_put;

	/** string mode for the error */
	int8_t error_mode;

	/** string mode for the error */
	int8_t info_mode;
};

extern
void
afb_req_reply_move_splitted(
	const struct afb_req_reply *reply,
	struct json_object **object,
	char **error,
	char **info
);

extern
int
afb_req_reply_copy_splitted(
	const struct afb_req_reply *reply,
	struct json_object **object,
	char **error,
	char **info
);

extern
int
afb_req_reply_copy(
	const struct afb_req_reply *from_reply,
	struct afb_req_reply *to_reply
);