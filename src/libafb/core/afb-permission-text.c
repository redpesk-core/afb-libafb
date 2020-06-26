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

#include "afb-permission-text.h"

#if !defined(AFB_PERM_TEXT_ON_BEHALF)
#  define AFB_PERM_TEXT_ON_BEHALF "urn:AGL:permission:*:partner:on-behalf-credentials"
#endif

#if !defined(AFB_PERM_TEXT_TOKEN)
#  define AFB_PERM_TEXT_TOKEN "urn:AGL:token:valid"
#endif

const char afb_permission_on_behalf_credential[] = AFB_PERM_TEXT_ON_BEHALF;
const char afb_permission_token_valid[]          = AFB_PERM_TEXT_TOKEN;
