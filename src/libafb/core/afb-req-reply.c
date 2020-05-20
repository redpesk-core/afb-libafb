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

#include <string.h>

#include <json-c/json.h>
#if !defined(JSON_C_TO_STRING_NOSLASHESCAPE)
#define JSON_C_TO_STRING_NOSLASHESCAPE 0
#endif

#include "core/afb-req-reply.h"
#include "sys/x-errno.h"

void
afb_req_reply_move_splitted(
	const struct afb_req_reply *reply,
	struct json_object **object,
	char **error,
	char **info
) {
	if (object)
		*object = reply->object;
	else
		json_object_put(reply->object);
	if (error)
		*error = reply->error;
	else
		free(reply->error);
	if (info)
		*info = reply->info;
	else
		free(reply->info);
}

int
afb_req_reply_copy_splitted(
	const struct afb_req_reply *reply,
	struct json_object **object,
	char **error,
	char **info
) {
	int status = 0;

	if (object)
		*object = json_object_get(reply->object);
	if (error) {
		if (reply->error == NULL)
			*error = NULL;
		else {
			*error = strdup(reply->error);
			if (*error == NULL)
				status = X_ENOMEM;
		}
	}
	if (info) {
		if (reply->info == NULL)
			*info = NULL;
		else {
			*info = strdup(reply->info);
			if (*info == NULL)
				status = X_ENOMEM;
		}
	}
	return status;
}

int
afb_req_reply_copy(
	const struct afb_req_reply *from_reply,
	struct afb_req_reply *to_reply
) {
	if (to_reply)
		return afb_req_reply_copy_splitted(
				from_reply,
				&to_reply->object,
				&to_reply->error,
				&to_reply->info);
	return 0;
}