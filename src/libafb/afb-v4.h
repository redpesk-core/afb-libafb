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

#pragma once

#include "core/afb-v4-itf.h"

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

#define Afb_Type_Flags_Shareable	Afb_Type_Flags_X4_Shareable
#define Afb_Type_Flags_Streamable	Afb_Type_Flags_X4_Streamable
#define Afb_Type_Flags_Opaque		Afb_Type_Flags_X4_Opaque

#define AFB_PREDEFINED_TYPE_OPAQUE	(&afb_type_predefined_opaque)
#define AFB_PREDEFINED_TYPE_STRINGZ	(&afb_type_predefined_stringz)
#define AFB_PREDEFINED_TYPE_JSON	(&afb_type_predefined_json)
#define AFB_PREDEFINED_TYPE_JSON_C	(&afb_type_predefined_json_c)
#define AFB_PREDEFINED_TYPE_BOOL	(&afb_type_predefined_bool)
#define AFB_PREDEFINED_TYPE_I32		(&afb_type_predefined_i32)
#define AFB_PREDEFINED_TYPE_U32		(&afb_type_predefined_u32)
#define AFB_PREDEFINED_TYPE_I64		(&afb_type_predefined_i64)
#define AFB_PREDEFINED_TYPE_U64		(&afb_type_predefined_u64)
#define AFB_PREDEFINED_TYPE_DOUBLE	(&afb_type_predefined_double)

