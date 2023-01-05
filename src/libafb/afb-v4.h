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

#include "libafb-config.h"
#include "core/afb-v4-itf.h"
#include "core/afb-type.h"
#include "afb-core.h"

/*******************************************************************/
/* COMMON RENAMINGS                                                */
/*******************************************************************/

/*-- PREDEFINED ------------------------------------------*/

#define AFB_PREDEFINED_TYPE_OPAQUE	(&afb_type_predefined_opaque)
#define AFB_PREDEFINED_TYPE_BYTEARRAY	(&afb_type_predefined_bytearray)
#define AFB_PREDEFINED_TYPE_STRINGZ	(&afb_type_predefined_stringz)
#define AFB_PREDEFINED_TYPE_JSON	(&afb_type_predefined_json)
#define AFB_PREDEFINED_TYPE_JSON_C	(&afb_type_predefined_json_c)
#define AFB_PREDEFINED_TYPE_BOOL	(&afb_type_predefined_bool)
#define AFB_PREDEFINED_TYPE_I32		(&afb_type_predefined_i32)
#define AFB_PREDEFINED_TYPE_U32		(&afb_type_predefined_u32)
#define AFB_PREDEFINED_TYPE_I64		(&afb_type_predefined_i64)
#define AFB_PREDEFINED_TYPE_U64		(&afb_type_predefined_u64)
#define AFB_PREDEFINED_TYPE_DOUBLE	(&afb_type_predefined_double)

/*-- DATA ------------------------------------------*/

#define afb_create_data_raw             afb_data_create_raw
#define afb_create_data_alloc           afb_data_create_alloc
#define afb_create_data_copy            afb_data_create_copy

/*-- EVENT ------------------------------------------*/

#define afb_event_is_valid              afb_evt_is_valid
#define afb_event_addref                afb_evt_addref_hookable
#define afb_event_unref                 afb_evt_unref_hookable
#define afb_event_name                  afb_evt_name_hookable
#define afb_event_push                  afb_evt_push_hookable
#define afb_event_broadcast             afb_evt_broadcast_hookable

/*-- TYPE ------------------------------------------*/

#define afb_type_register               afb_v4_itf_type_register
#define afb_type_add_converter_to       afb_type_add_converter
#define afb_type_add_update_to          afb_type_add_updater
#define afb_type_add_converter_from(ft,tt,cvt,clo)  afb_type_add_converter(tt,ft,cvt,clo)
#define afb_type_add_update_from(ft,tt,cvt,clo)     afb_type_add_updater(tt,ft,cvt,clo)

/*-- FD's EVENT HANDLING -----------------------------------*/

#define afb_evfd_create                 afb_ev_mgr_add_fd
#define afb_evfd_addref                 ev_fd_addref
#define afb_evfd_unref                  ev_fd_unref
#define afb_evfd_get_fd                 ev_fd_fd
#define afb_evfd_get_events             ev_fd_events
#define afb_evfd_set_events             ev_fd_set_events

/*-- TIMERS's EVENT HANDLING -----------------------------------*/

#define afb_timer_create                afb_ev_mgr_add_timer
#define afb_timer_addref                ev_timer_addref
#define afb_timer_unref                 ev_timer_unref


/*******************************************************************/
/* SPECIFIC TO THE VERSION 4                                       */
/*******************************************************************/

/*-- TYPES ------------------------------------------*/

typedef struct afb_verb_v4		afb_verb_t;
typedef struct afb_binding_v4		afb_binding_t;

typedef afb_api_x4_t			afb_api_t;
typedef afb_req_x4_t			afb_req_t;
typedef afb_event_x4_t			afb_event_t;
typedef afb_data_x4_t			afb_data_t;
typedef afb_type_x4_t			afb_type_t;
typedef afb_evfd_x4_t			afb_evfd_t;
typedef afb_timer_x4_t			afb_timer_t;

typedef afb_type_flags_x4_t		afb_type_flags_t;
typedef afb_type_converter_x4_t		afb_type_converter_t;
typedef afb_type_updater_x4_t		afb_type_updater_t;

typedef afb_api_callback_x4_t		afb_api_callback_t;
typedef afb_req_callback_x4_t		afb_req_callback_t;
typedef afb_call_callback_x4_t		afb_call_callback_t;
typedef afb_subcall_callback_x4_t	afb_subcall_callback_t;
typedef afb_check_callback_x4_t		afb_check_callback_t;
typedef afb_event_handler_x4_t		afb_event_handler_t;
typedef afb_type_converter_x4_t		afb_type_converter_t;
typedef afb_type_updater_x4_t		afb_type_updater_t;
typedef afb_evfd_handler_x4_t		afb_evfd_handler_t;
typedef afb_timer_handler_x4_t		afb_timer_handler_t;

/*-- FLAGS ------------------------------------------*/

#define Afb_Type_Flags_Shareable	Afb_Type_Flags_X4_Shareable
#define Afb_Type_Flags_Streamable	Afb_Type_Flags_X4_Streamable
#define Afb_Type_Flags_Opaque		Afb_Type_Flags_X4_Opaque

/*-- REQ ------------------------------------------*/

#define afb_req_is_valid                afb_req_v4_is_valid
#define afb_req_logmask                 afb_req_v4_logmask
#define afb_req_addref                  afb_req_v4_addref_hookable
#define afb_req_unref                   afb_req_v4_unref_hookable
#define afb_req_get_api                 afb_req_v4_api
#define afb_req_get_vcbdata             afb_req_v4_vcbdata
#define afb_req_get_called_api          afb_req_v4_called_api
#define afb_req_get_called_verb         afb_req_v4_called_verb
#define afb_req_vverbose                afb_req_v4_vverbose_hookable
#define afb_req_verbose                 afb_req_v4_verbose_hookable
#define afb_req_session_close           afb_req_v4_session_close_hookable
#define afb_req_session_set_LOA         afb_req_v4_session_set_LOA_hookable
#define afb_req_session_get_LOA         afb_req_v4_session_get_LOA_hookable
#define afb_req_context                 afb_req_v4_cookie_hookable
#define afb_req_subscribe               afb_req_v4_subscribe_hookable
#define afb_req_unsubscribe             afb_req_v4_unsubscribe_hookable
#define afb_req_get_client_info         afb_req_v4_get_client_info_hookable
#define afb_req_check_permission        afb_req_v4_check_permission_hookable
#define afb_req_parameters              afb_req_v4_parameters
#define afb_req_param_convert           afb_req_v4_param_convert
#define afb_req_reply                   afb_req_v4_reply_hookable
#define afb_req_subcall                 afb_req_v4_subcall_hookable
#define afb_req_subcall_sync            afb_req_v4_subcall_sync_hookable
#define afb_req_wants_log_level(r,l)    (afb_req_logmask(r) & (1 << (l)))
#define afb_req_context_get             afb_req_v4_cookie_get_hookable
#define afb_req_context_set             afb_req_v4_cookie_set_hookable
#define afb_req_context_drop            afb_req_v4_cookie_drop_hookable
#define afb_req_get_userdata            afb_req_v4_get_userdata_hookable
#define afb_req_set_userdata            afb_req_v4_set_userdata_hookable

/*-- API ------------------------------------------*/

#define afb_api_logmask                 afb_api_v4_logmask
#define afb_api_name                    afb_api_v4_name
#define afb_api_get_userdata            afb_api_v4_get_userdata
#define afb_api_set_userdata            afb_api_v4_set_userdata
#define afb_api_settings                afb_api_v4_settings_hookable
#define afb_api_vverbose                afb_api_v4_vverbose_hookable
#define afb_api_verbose                 afb_api_v4_verbose_hookable
#define afb_api_broadcast_event         afb_api_v4_event_broadcast_hookable
#define afb_api_new_event               afb_api_v4_new_event_hookable
#define afb_api_event_handler_add       afb_api_v4_event_handler_add_hookable
#define afb_api_event_handler_del       afb_api_v4_event_handler_del_hookable
#define afb_api_call                    afb_api_v4_call_hookable
#define afb_api_call_sync               afb_api_v4_call_sync_hookable
#define afb_api_add_verb                afb_api_v4_add_verb_hookable
#define afb_api_del_verb                afb_api_v4_del_verb_hookable
#define afb_api_seal                    afb_api_v4_seal_hookable
#define afb_api_set_verbs               afb_api_v4_set_verbs_hookable
#define afb_api_require_api             afb_api_v4_require_api_hookable
#define afb_api_provide_class           afb_api_v4_class_provide_hookable
#define afb_api_require_class           afb_api_v4_class_require_hookable
#define afb_api_delete                  afb_api_v4_delete_api_hookable

/*-- MISC ------------------------------------------*/

#define afb_create_api                  afb_api_v4_new_api_hookable
#define afb_job_post                    afb_api_v4_post_job_hookable
#define afb_alias_api                   afb_api_v4_add_alias_hookable
#define afb_setup_shared_object         afb_v4_itf_setup_shared_object
