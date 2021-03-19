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

#include "afb-req-common.h"

struct afb_api_v4;
struct afb_verb_v4;

extern
void
afb_req_v4_process(
	struct afb_req_common *comreq,
	struct afb_api_v4 *api,
	const struct afb_verb_v4 *verbv4
);


extern
struct afb_req_v4 *
afb_req_v4_addref_hookable(
	struct afb_req_v4 *reqv4
);

extern
void
afb_req_v4_unref_hookable(
	struct afb_req_v4 *reqv4
);

extern
void
afb_req_v4_vverbose_hookable(
	struct afb_req_v4 *reqv4,
	int level,
	const char *file,
	int line,
	const char *func,
	const char *fmt,
	va_list args
);

extern
void *
afb_req_v4_cookie_hookable(
	struct afb_req_v4 *reqv4,
	int replace,
	void *(*create_value)(void*),
	void (*free_value)(void*),
	void *create_closure
);

extern
int
afb_req_v4_session_set_LOA_hookable(
	struct afb_req_v4 *reqv4,
	unsigned level
);

extern
unsigned
afb_req_v4_session_get_LOA_hookable(
	struct afb_req_v4 *reqv4
);

extern
void
afb_req_v4_session_close_hookable(
	struct afb_req_v4 *reqv4
);

extern
struct json_object *
afb_req_v4_get_client_info_hookable(
	struct afb_req_v4 *reqv4
);

extern
int
afb_req_v4_logmask(
	struct afb_req_v4 *reqv4
);

extern
struct afb_api_v4 *
afb_req_v4_api(
	struct afb_req_v4 *reqv4
);

extern
void *
afb_req_v4_vcbdata(
	struct afb_req_v4 *reqv4
);

extern
const char *
afb_req_v4_called_api(
	struct afb_req_v4 *reqv4
);

extern
const char *
afb_req_v4_called_verb(
	struct afb_req_v4 *reqv4
);

extern
int
afb_req_v4_subscribe_hookable(
	struct afb_req_v4 *reqv4,
	struct afb_evt *event
);

extern
int
afb_req_v4_unsubscribe_hookable(
	struct afb_req_v4 *reqv4,
	struct afb_evt *event
);

extern
void
afb_req_v4_check_permission_hookable(
	struct afb_req_v4 *reqv4,
	const char *permission,
	void (*callback)(void*,int,struct afb_req_v4*),
	void *closure
);

extern
unsigned
afb_req_v4_parameters(
	struct afb_req_v4 *reqv4,
	struct afb_data * const **params
);

extern
void
afb_req_v4_reply_hookable(
	struct afb_req_v4 *reqv4,
	int status,
	unsigned nparams,
	struct afb_data * const params[]
);

extern
void
afb_req_v4_subcall_hookable(
	struct afb_req_v4 *reqv4,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	struct afb_data * const params[],
	int flags,
	void (*callback)(void *closure, int status, unsigned, struct afb_data * const[], struct afb_req_v4 *reqv4),
	void *closure
);

extern
int
afb_req_v4_subcall_sync_hookable(
	struct afb_req_v4 *reqv4,
	const char *apiname,
	const char *verbname,
	unsigned nparams,
	struct afb_data * const params[],
	int flags,
	int *status,
	unsigned *nreplies,
	struct afb_data *replies[]
);