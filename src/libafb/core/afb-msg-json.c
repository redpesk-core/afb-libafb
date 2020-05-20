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

#include <json-c/json.h>

#include "core/afb-msg-json.h"
#include "core/afb-req-reply.h"

static const char _success_[] = "success";

struct json_object *afb_msg_json_reply(const struct afb_req_reply *reply)
{
	json_object *msg, *request;
	json_object *type_reply = NULL;

	msg = json_object_new_object();
	if (reply->object != NULL)
		json_object_object_add(msg, "response", json_object_get(reply->object));

	type_reply = json_object_new_string("afb-reply");
	json_object_object_add(msg, "jtype", type_reply);

	request = json_object_new_object();
	json_object_object_add(msg, "request", request);
	json_object_object_add(request, "status", json_object_new_string(reply->error ?: _success_));

	if (reply->info != NULL)
		json_object_object_add(request, "info", json_object_new_string(reply->info));

	return msg;
}

struct json_object *afb_msg_json_event(const char *event, struct json_object *object)
{
	json_object *msg;
	json_object *type_event = NULL;

	msg = json_object_new_object();

	json_object_object_add(msg, "event", json_object_new_string(event));

	if (object != NULL)
		json_object_object_add(msg, "data", object);

	type_event = json_object_new_string("afb-event");
	json_object_object_add(msg, "jtype", type_event);

	return msg;
}


