/*
 * Copyright (C) 2015-2024 IoT.bzh Company
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

extern void afb_common_default_locale_set(const char *locale);
extern const char *afb_common_default_locale_get();

extern int afb_common_rootdir_set(const char *rootdir);
#if WITH_OPENAT
extern int afb_common_rootdir_get_fd();
#endif
extern const char *afb_common_rootdir_get_path();
extern int afb_common_rootdir_open_locale(const char *filename, int flags, const char *locale);

