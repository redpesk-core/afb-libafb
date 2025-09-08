/*
 * Copyright (C) 2015-2025 IoT.bzh Company
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

#include "../libafb-config.h"

struct afb_type;

/*****************************************************************************/

#if !defined(AFB_PREFIX_PREDEF_TYPE)
#  define AFB_PREFIX_PREDEF_TYPE "#"
#endif

static inline int afb_type_is_predefined(const char *name)
{
	return name[0] == AFB_PREFIX_PREDEF_TYPE[0];
}

/*****************************************************************************/

extern struct afb_type afb_type_predefined_opaque;
extern struct afb_type afb_type_predefined_bytearray;
extern struct afb_type afb_type_predefined_stringz;
extern struct afb_type afb_type_predefined_json;
extern struct afb_type afb_type_predefined_json_c;

extern struct afb_type afb_type_predefined_bool;
extern struct afb_type afb_type_predefined_i32;
extern struct afb_type afb_type_predefined_u32;
extern struct afb_type afb_type_predefined_i64;
extern struct afb_type afb_type_predefined_u64;
extern struct afb_type afb_type_predefined_double;
extern struct afb_type afb_type_predefined_UUID;

/*****************************************************************************/

enum afb_typeids
{
	Afb_Typeid_Predefined_None = 0,
	Afb_Typeid_Predefined_Opaque,
	Afb_Typeid_Predefined_Bytearray,
	Afb_Typeid_Predefined_Stringz,
	Afb_Typeid_Predefined_Json,
	Afb_Typeid_Predefined_Json_C,
	Afb_Typeid_Predefined_Bool,
	Afb_Typeid_Predefined_I8,
	Afb_Typeid_Predefined_U8,
	Afb_Typeid_Predefined_I16,
	Afb_Typeid_Predefined_U16,
	Afb_Typeid_Predefined_I32,
	Afb_Typeid_Predefined_U32,
	Afb_Typeid_Predefined_I64,
	Afb_Typeid_Predefined_U64,
	Afb_Typeid_Predefined_Float,
	Afb_Typeid_Predefined_Double,
	Afb_Typeid_Predefined_UUID,
	Afb_Typeid_Last_Predefined = Afb_Typeid_Predefined_UUID,
	Afb_Typeid_First_Userid
};

/*****************************************************************************/
