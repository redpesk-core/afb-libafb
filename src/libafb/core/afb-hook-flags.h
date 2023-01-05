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

#include "../libafb-config.h"

#if WITH_AFB_HOOK  /***********************************************************/

extern int afb_hook_flags_req_from_text(const char *text, unsigned *flags);
extern int afb_hook_flags_api_from_text(const char *text, unsigned *flags);
extern int afb_hook_flags_evt_from_text(const char *text, unsigned *flags);
extern int afb_hook_flags_session_from_text(const char *text, unsigned *flags);
extern int afb_hook_flags_global_from_text(const char *text, unsigned *flags);

extern char *afb_hook_flags_req_to_text(unsigned value);
extern char *afb_hook_flags_api_to_text(unsigned value);
extern char *afb_hook_flags_evt_to_text(unsigned value);
extern char *afb_hook_flags_session_to_text(unsigned value);

#endif /* WITH_AFB_HOOK *******************************************************/
