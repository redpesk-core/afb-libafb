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

#include "afb-v4.h"

#include "afb-data.h"
#include "afb-req-v4.h"
#include "afb-evt.h"
#include "afb-type.h"
#include "afb-api-v4.h"

/* avoid use of afb_data_action_x4_t in header afb_data */
static int x4_data_control(struct afb_data *data, afb_data_action_x4_t action)
{
	int r = 0;

	switch (action) {
	case Afb_Data_Action_x4_Notify_Changed:
		afb_data_notify_changed(data);
		break;
	case Afb_Data_Action_x4_Is_Constant:
		r = afb_data_is_constant(data);
		break;
	case Afb_Data_Action_x4_Set_Constant:
		afb_data_set_constant(data);
		break;
	case Afb_Data_Action_x4_Set_Not_Constant:
		afb_data_set_not_constant(data);
		break;
	case Afb_Data_Action_x4_Is_Volatile:
		r = afb_data_is_volatile(data);
		break;
	case Afb_Data_Action_x4_Set_Volatile:
		afb_data_set_volatile(data);
		break;
	case Afb_Data_Action_x4_Set_Not_Volatile:
		afb_data_set_not_volatile(data);
		break;
	case Afb_Data_Action_x4_Lock_Read:
		afb_data_lock_read(data);
		break;
	case Afb_Data_Action_x4_Try_Lock_Read:
		r = afb_data_try_lock_read(data);
		break;
	case Afb_Data_Action_x4_Lock_Write:
		afb_data_lock_write(data);
		break;
	case Afb_Data_Action_x4_Try_Lock_Write:
		r = afb_data_try_lock_write(data);
		break;
	case Afb_Data_Action_x4_Unlock:
		afb_data_unlock(data);
		break;
	default:
		break;
	}
	return r;
}

static
int
x4_api_type_register(
	struct afb_type **type,
	const char *name,
	afb_type_flags_x4_t flags
) {
	return afb_type_register(type, name,
			flags & Afb_Type_Flags_x4_Streamable,
			flags & Afb_Type_Flags_x4_Streamable,
			flags & Afb_Type_Flags_x4_Opaque);
}

static
int
x4_api_type_lookup(
	struct afb_type **type,
	const char *name
) {
	return (*type = afb_type_get(name)) ? 0 : -1;
}



const struct afb_binding_x4_itf afb_v4_itf = {

/*-- DATA ------------------------------------------*/

	.create_data_raw = afb_data_create_raw,
	.create_data_alloc = afb_data_create_alloc,
	.create_data_copy = afb_data_create_copy,
	.data_addref = afb_data_addref,
	.data_unref = afb_data_unref,
	.data_get_mutable = afb_data_get_mutable,
	.data_get_constant = afb_data_get_constant,
	.data_update = afb_data_update,
	.data_convert = afb_data_convert_to,
	.data_control = x4_data_control,
	.data_type = afb_data_type,

/*-- REQ ------------------------------------------*/

	.req_logmask = afb_req_v4_logmask,
	.req_addref = afb_req_v4_addref_hookable,
	.req_unref = afb_req_v4_unref_hookable,
	.req_api = afb_req_v4_api,
	.req_vcbdata = afb_req_v4_vcbdata,
	.req_called_api = afb_req_v4_called_api,
	.req_called_verb = afb_req_v4_called_verb,
	.req_vverbose = afb_req_v4_vverbose_hookable,
	.req_session_close = afb_req_v4_session_close_hookable,
	.req_session_set_LOA = afb_req_v4_session_set_LOA_hookable,
	.req_cookie = afb_req_v4_cookie_hookable,
	.req_subscribe = afb_req_v4_subscribe_hookable,
	.req_unsubscribe = afb_req_v4_unsubscribe_hookable,
	.req_get_client_info = afb_req_v4_get_client_info_hookable,
	.req_check_permission = afb_req_v4_check_permission_hookable,
	.req_parameters = afb_req_v4_parameters,
	.req_reply = afb_req_v4_reply_hookable,
	.req_subcall = afb_req_v4_subcall_hookable,
	.req_subcall_sync = afb_req_v4_subcall_sync_hookable,

/*-- EVENT ------------------------------------------*/

	.event_addref = afb_evt_addref_hookable,
	.event_unref = afb_evt_unref_hookable,
	.event_name = afb_evt_name_hookable,
	.event_push = afb_evt_push_hookable,
	.event_broadcast = afb_evt_broadcast_hookable,

/*-- TYPE ------------------------------------------*/

	.type_lookup = x4_api_type_lookup,
	.type_register = x4_api_type_register,
	.type_name = afb_type_name,
	.type_set_family = afb_type_set_family,
	.type_add_converter = afb_type_add_converter,
	.type_add_updater = afb_type_add_updater,

/*-- API ------------------------------------------*/

	.api_logmask = afb_api_v4_logmask,
	.api_vverbose = afb_api_v4_vverbose_hookable,
	.api_name = afb_api_v4_name,
	.api_get_userdata = afb_api_v4_get_userdata,
	.api_set_userdata = afb_api_v4_set_userdata,
	.api_settings = afb_api_v4_settings_hookable,
	.api_event_broadcast = afb_api_v4_event_broadcast_hookable,
	.api_new_event = afb_api_v4_new_event_hookable,
	.api_event_handler_add = afb_api_v4_event_handler_add_hookable,
	.api_event_handler_del = afb_api_v4_event_handler_del_hookable,
	.api_call = afb_api_v4_call_hookable,
	.api_call_sync = afb_api_v4_call_sync_hookable,
	.api_add_verb = afb_api_v4_add_verb_hookable,
	.api_del_verb = afb_api_v4_del_verb_hookable,
	.api_seal = afb_api_v4_seal_hookable,
	.api_set_verbs = afb_api_v4_set_verbs_hookable,
	.api_require_api = afb_api_v4_require_api_hookable,
	.api_class_provide = afb_api_v4_class_provide_hookable,
	.api_class_require = afb_api_v4_class_require_hookable,
	.api_delete = afb_api_v4_delete_api_hookable,

/*-- MISC ------------------------------------------*/

	.create_api = afb_api_v4_new_api_hookable,
	.job_queue = afb_api_v4_queue_job_hookable,
	.alias_api = afb_api_v4_add_alias_hookable,

/*-- PREDEFINED TYPES -----------------------------------*/

	.type_opaque = &afb_type_predefined_opaque,
	.type_stringz = &afb_type_predefined_stringz,
	.type_json = &afb_type_predefined_json,
	.type_json_c = &afb_type_predefined_json_c

/*-- AFTERWARD ------------------------------------------*/

};
