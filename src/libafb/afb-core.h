/*
 * Copyright (C) 2015-2022 IoT.bzh Company
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

#include "core/afb-api-common.h"
#include "core/afb-apiname.h"
#include "core/afb-apiset.h"
#include "core/afb-api-v3.h"
#include "core/afb-api-v4.h"
#include "core/afb-auth.h"
#include "core/afb-calls.h"
#include "core/afb-common.h"
#include "core/afb-cred.h"
#include "core/afb-data.h"
#include "core/afb-data-array.h"
#include "core/afb-error-text.h"
#include "core/afb-ev-mgr.h"
#include "core/afb-evt.h"
#include "core/afb-global.h"
#include "core/afb-hook-flags.h"
#include "core/afb-hook.h"
#include "core/afb-jobs.h"
#include "core/afb-json-legacy.h"
#include "core/afb-perm.h"
#include "core/afb-permission-text.h"
#include "core/afb-req-common.h"
#include "core/afb-req-v3.h"
#include "core/afb-req-v4.h"
#include "core/afb-sched.h"
#include "core/afb-session.h"
#include "core/afb-sig-monitor.h"
#include "core/afb-string-mode.h"
#include "core/afb-token.h"
#include "core/afb-type.h"
#include "core/afb-type-internal.h"
#include "core/afb-type-predefined.h"
#include "core/afb-v4-itf.h"
#include "core/containerof.h"
