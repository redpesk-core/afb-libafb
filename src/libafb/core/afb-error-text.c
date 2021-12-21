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

#include <string.h>

#include "afb-error-text.h"

#include <afb/afb-errno.h>

static const char text_bad_api_state[]      = "bad-api-state";
static const char text_bad_state[]          = "bad-state";
static const char text_disconnected[]       = "disconnected";
static const char text_forbidden[]          = "forbidden";
static const char text_insufficient_scope[] = "insufficient-scope";
static const char text_internal_error[]     = "internal-error";
static const char text_invalid_request[]    = "invalid-request";
static const char text_invalid_token[]      = "invalid-token";
static const char text_no_item[]            = "no-item";
static const char text_not_available[]      = "not-available";
static const char text_no_reply[]           = "no-reply";
static const char text_out_of_memory[]      = "out-of-memory";
static const char text_unauthorized[]       = "unauthorized";
static const char text_unknown_api[]        = "unknown-api";
static const char text_unknown_verb[]       = "unknown-verb";
static const char text_user_error[]         = "user-error";

#define ALSO_SOME_LEGACY 1
#if ALSO_SOME_LEGACY
static const char text_not_item[]           = "not-item";
static const char text_not_replied[]        = "not-replied";
#endif

const char *afb_error_text(int code)
{
	if (!AFB_IS_ERRNO(code))
		return 0;

	if (AFB_IS_USER_ERRNO(code))
		return text_user_error;

	switch(code) {
	case AFB_ERRNO_OUT_OF_MEMORY:
		return text_out_of_memory;
	case AFB_ERRNO_UNKNOWN_API:
		return text_unknown_api;
	case AFB_ERRNO_UNKNOWN_VERB:
		return text_unknown_verb;
	case AFB_ERRNO_NOT_AVAILABLE:
		return text_not_available;
	case AFB_ERRNO_UNAUTHORIZED:
		return text_unauthorized;
	case AFB_ERRNO_INVALID_TOKEN:
		return text_invalid_token;
	case AFB_ERRNO_FORBIDDEN:
		return text_forbidden;
	case AFB_ERRNO_INSUFFICIENT_SCOPE:
		return text_insufficient_scope;
	case AFB_ERRNO_BAD_API_STATE:
		return text_bad_api_state;
	case AFB_ERRNO_NO_REPLY:
		return text_no_reply;
	case AFB_ERRNO_INVALID_REQUEST:
		return text_invalid_request;
	case AFB_ERRNO_NO_ITEM:
		return text_no_item;
	case AFB_ERRNO_BAD_STATE:
		return text_bad_state;
	case AFB_ERRNO_DISCONNECTED:
		return text_disconnected;
	default:
		return text_internal_error;
	}
}

int afb_error_code(const char *error)
{
	if (!error)
		return 0;

	if (!strcmp(error, text_internal_error))
		return AFB_ERRNO_INTERNAL_ERROR;
	if (!strcmp(error, text_out_of_memory))
		return AFB_ERRNO_OUT_OF_MEMORY;
	if (!strcmp(error, text_unknown_api))
		return AFB_ERRNO_UNKNOWN_API;
	if (!strcmp(error, text_unknown_verb))
		return AFB_ERRNO_UNKNOWN_VERB;
	if (!strcmp(error, text_not_available))
		return AFB_ERRNO_NOT_AVAILABLE;
	if (!strcmp(error, text_unauthorized))
		return AFB_ERRNO_UNAUTHORIZED;
	if (!strcmp(error, text_invalid_token))
		return AFB_ERRNO_INVALID_TOKEN;
	if (!strcmp(error, text_forbidden))
		return AFB_ERRNO_FORBIDDEN;
	if (!strcmp(error, text_insufficient_scope))
		return AFB_ERRNO_INSUFFICIENT_SCOPE;
	if (!strcmp(error, text_bad_api_state))
		return AFB_ERRNO_BAD_API_STATE;
	if (!strcmp(error, text_no_reply))
		return AFB_ERRNO_NO_REPLY;
	if (!strcmp(error, text_invalid_request))
		return AFB_ERRNO_INVALID_REQUEST;
	if (!strcmp(error, text_no_item))
		return AFB_ERRNO_NO_ITEM;
	if (!strcmp(error, text_bad_state))
		return AFB_ERRNO_BAD_STATE;
	if (!strcmp(error, text_disconnected))
		return AFB_ERRNO_DISCONNECTED;
#if ALSO_SOME_LEGACY
	if (!strcmp(error, text_not_replied))
		return AFB_ERRNO_NO_REPLY;
	if (!strcmp(error, text_not_item))
		return AFB_ERRNO_NO_ITEM;
#endif
	return AFB_ERRNO_GENERIC_FAILURE;
}
