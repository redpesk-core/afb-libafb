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


#pragma once

#if WITH_LIBMICROHTTPD

enum afb_method {
	afb_method_none = 0,
	afb_method_get = 1,
	afb_method_post = 2,
	afb_method_head = 4,
	afb_method_connect = 8,
	afb_method_delete = 16,
	afb_method_options = 32,
	afb_method_patch = 64,
	afb_method_put = 128,
	afb_method_trace = 256,
	afb_method_all = 511
};

extern enum afb_method get_method(const char *method);
extern const char *get_method_name(enum afb_method method);

#endif
