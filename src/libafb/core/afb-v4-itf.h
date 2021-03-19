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

#include <libafb/libafb-config.h>

#define afb_api_x4   afb_api_v4
#define afb_type_x4  afb_type
#define afb_data_x4  afb_data
#define afb_req_x4   afb_req_v4
#define afb_event_x4 afb_evt
#define afb_evfd_x4  ev_fd
#define afb_timer_x4 ev_timer

#define AFB_BINDING_VERSION 0
#include <afb/afb-binding-v4.h>

extern int afb_v4_itf_type_register(struct afb_type **type, const char *name, afb_type_flags_x4_t flags);

#if WITH_DYNAMIC_BINDING

#include "../sys/x-dynlib.h"

struct afb_v4_dynlib_info
{
	/** root api */
	afb_api_x4_t *root;

	/** descriptor of the binding for static api */
	const struct afb_binding_v4 *desc;

	/** main control routine */
	afb_api_callback_x4_t mainctl;

	/** revision of the interface (0 if no interface found) */
	short itfrev;

	/** the revision */
	short revision;
};

extern void afb_v4_connect_dynlib(x_dynlib_t *dynlib, struct afb_v4_dynlib_info *info, afb_api_x4_t rootapi);
extern int afb_v4_itf_setup_shared_object(afb_api_x4_t root, void *handle);

#endif
