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

#include "afb-config.h"

#if WITH_LIBMICROHTTPD

#include <microhttpd.h>

#include "http/afb-method.h"

enum afb_method get_method(const char *method)
{
	switch (method[0] & ~' ') {
	case 'C':
		return afb_method_connect;
	case 'D':
		return afb_method_delete;
	case 'G':
		return afb_method_get;
	case 'H':
		return afb_method_head;
	case 'O':
		return afb_method_options;
	case 'P':
		switch (method[1] & ~' ') {
		case 'A':
			return afb_method_patch;
		case 'O':
			return afb_method_post;
		case 'U':
			return afb_method_put;
		}
		break;
	case 'T':
		return afb_method_trace;
	}
	return afb_method_none;
}

#if !defined(MHD_HTTP_METHOD_PATCH)
#define MHD_HTTP_METHOD_PATCH "PATCH"
#endif
const char *get_method_name(enum afb_method method)
{
	switch (method) {
	case afb_method_get:
		return MHD_HTTP_METHOD_GET;
	case afb_method_post:
		return MHD_HTTP_METHOD_POST;
	case afb_method_head:
		return MHD_HTTP_METHOD_HEAD;
	case afb_method_connect:
		return MHD_HTTP_METHOD_CONNECT;
	case afb_method_delete:
		return MHD_HTTP_METHOD_DELETE;
	case afb_method_options:
		return MHD_HTTP_METHOD_OPTIONS;
	case afb_method_patch:
		return MHD_HTTP_METHOD_PATCH;
	case afb_method_put:
		return MHD_HTTP_METHOD_PUT;
	case afb_method_trace:
		return MHD_HTTP_METHOD_TRACE;
	default:
		return NULL;
	}
}

#endif

