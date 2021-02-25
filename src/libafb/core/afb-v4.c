/*
 * Copyright (C) 2015-2021 IoT.bzh Company
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
#include "afb-type-predefined.h"
#include "afb-api-v4.h"
#include "afb-ev-mgr.h"

#include "sys/ev-mgr.h"
#include "sys/x-errno.h"

/***********************************************************/

static
int
x4_api_type_register(
	struct afb_type **type,
	const char *name,
	afb_type_flags_x4_t flags
) {
	if (afb_type_is_predefined(name)) {
		*type = 0;
		return X_ERANGE;
	}
	else {
		return afb_type_register(type, name,
			flags & Afb_Type_Flags_x4_Streamable,
			flags & Afb_Type_Flags_x4_Streamable,
			flags & Afb_Type_Flags_x4_Opaque);
	}
}

static
int
x4_api_type_lookup(
	struct afb_type **type,
	const char *name
) {
	return (*type = afb_type_get(name)) ? 0 : X_ENOENT;
}

static
int
x4_api_setup_shared_object(
	afb_api_x4_t root,
	void *handle
) {
	struct afb_v4_dynlib_info info;
	x_dynlib_t dynlib;

	dynlib.handle = handle;
	afb_v4_connect_dynlib(&dynlib, &info, root);
	return 0;
}

/***********************************************************
 * interface
 */

static const struct afb_binding_x4r1_itf afb_v4_itf = {

/*-- DATA ------------------------------------------*/

	.create_data_raw = afb_data_create_raw,
	.create_data_alloc = afb_data_create_alloc0,
	.create_data_copy = afb_data_create_copy,
	.data_addref = afb_data_addref,
	.data_unref = afb_data_unref,
	.data_get_mutable = afb_data_get_mutable,
	.data_get_constant = afb_data_get_constant,
	.data_update = afb_data_update,
	.data_convert = afb_data_convert,
	.data_type = afb_data_type,
	.data_notify_changed = afb_data_notify_changed,
	.data_is_volatile = afb_data_is_volatile,
	.data_set_volatile = afb_data_set_volatile,
	.data_set_not_volatile = afb_data_set_not_volatile,
	.data_is_constant = afb_data_is_constant,
	.data_set_constant = afb_data_set_constant,
	.data_set_not_constant = afb_data_set_not_constant,
	.data_lock_read = afb_data_lock_read,
	.data_try_lock_read = afb_data_try_lock_read,
	.data_lock_write = afb_data_lock_write,
	.data_try_lock_write = afb_data_try_lock_write,
	.data_unlock = afb_data_unlock,

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
	.job_post = afb_api_v4_post_job_hookable,
	.alias_api = afb_api_v4_add_alias_hookable,
	.setup_shared_object = x4_api_setup_shared_object,

/*-- PREDEFINED TYPES -----------------------------------*/

	.type_opaque = &afb_type_predefined_opaque,
	.type_stringz = &afb_type_predefined_stringz,
	.type_json = &afb_type_predefined_json,
	.type_json_c = &afb_type_predefined_json_c,
#if !NO_BASIC_PREDEFINED_TYPES
	.type_bool = &afb_type_predefined_bool,
	.type_i32 = &afb_type_predefined_i32,
	.type_u32 = &afb_type_predefined_u32,
	.type_i64 = &afb_type_predefined_i64,
	.type_u64 = &afb_type_predefined_u64,
	.type_double = &afb_type_predefined_double,
#endif

/*-- FD's EVENT HANDLING -----------------------------------*/

	.evfd_create = afb_ev_mgr_add_fd,
	.evfd_addref = ev_fd_addref,
	.evfd_unref = ev_fd_unref,
	.evfd_get_fd = ev_fd_fd,
	.evfd_get_events = ev_fd_events,
	.evfd_set_events = ev_fd_set_events,

/*-- TIMER HANDLING -----------------------------------*/

	.timer_create = afb_ev_mgr_add_timer,
	.timer_addref = ev_timer_addref,
	.timer_unref = ev_timer_unref,

/*-- EXTRA FUNCTIONS -----------------------------------*/

	.req_session_get_LOA = afb_req_v4_session_get_LOA_hookable,

/*-- END -----------------------------------*/
};

#if WITH_DYNAMIC_BINDING

#include "sys/x-dynlib.h"

/**********************************************************/

/**
 * Name of the structure of callbacks
 */
static const char afb_api_so_v4r1_itf[] = "afbBindingV4r1_itf";

/**
 * Name of the pointer to the structure of callbacks
 */
static const char afb_api_so_v4r1_itfptr[] = "afbBindingV4r1_itfptr";

/**
 * Name of the structure describing statically the binding: "afbBindingV4"
 */
static const char afb_api_so_v4_desc[] = "afbBindingV4";

/**
 * Name of the pointer for the root api: "afbBindingV4root"
 */
static const char afb_api_so_v4_root[] = "afbBindingV4root";

/**
 * Name of the entry function for dynamic bindings: "afbBindingV4entry"
 */
static const char afb_api_so_v4_entry[] = "afbBindingV4entry";


void afb_v4_connect_dynlib(x_dynlib_t *dynlib, struct afb_v4_dynlib_info *info, afb_api_x4_t rootapi)
{
	struct afb_binding_x4r1_itf *itf1;
	const struct afb_binding_x4r1_itf **itfptr1;

	/* retrieves important exported symbols */
	x_dynlib_symbol(dynlib, afb_api_so_v4_root, (void**)&info->root);
	x_dynlib_symbol(dynlib, afb_api_so_v4_desc, (void**)&info->desc);
	x_dynlib_symbol(dynlib, afb_api_so_v4_entry, (void**)&info->mainctl);

	/* retrieves interfaces */
	info->itfrev = 0;
	x_dynlib_symbol(dynlib, afb_api_so_v4r1_itf, (void**)&itf1);
	x_dynlib_symbol(dynlib, afb_api_so_v4r1_itfptr, (void**)&itfptr1);
	if (itf1) {
		info->itfrev = 1;
		*itf1 = afb_v4_itf;
		if (itfptr1)
			*itfptr1 = itf1;
	}
	else if (itfptr1) {
		info->itfrev = 1;
		*itfptr1 = &afb_v4_itf;
	}

	/* set the root api */
	if (rootapi && info->root)
		*info->root = rootapi;
}

#endif
