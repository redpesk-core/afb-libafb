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

#include <libafb/libafb-config.h>

struct afb_api_common;
struct afb_req_common;
struct afb_data;

/******************************************************************************/
extern
void
afb_calls_call(
	struct afb_api_common *comapi,
	const char *api,
	const char *verb,
	unsigned nparams,
	struct afb_data * const params[],
	void (*callback)(void*, void*, void*, int, unsigned, struct afb_data * const[]),
	void *closure1,
	void *closure2,
	void *closure3
);

extern
int
afb_calls_call_sync(
	struct afb_api_common *comapi,
	const char *api,
	const char *verb,
	unsigned nparams,
	struct afb_data * const params[],
	int *status,
	unsigned *nreplies,
	struct afb_data *replies[]
);

extern
void
afb_calls_subcall(
	struct afb_api_common *comapi,
	const char *api,
	const char *verb,
	unsigned nparams,
	struct afb_data * const params[],
	void (*callback)(void*, void*, void*, int, unsigned, struct afb_data * const[]),
	void *closure1,
	void *closure2,
	void *closure3,
	struct afb_req_common *comreq,
	int flags
);

extern
int
afb_calls_subcall_sync(
	struct afb_api_common *comapi,
	const char *api,
	const char *verb,
	unsigned nparams,
	struct afb_data * const params[],
	int *status,
	unsigned *nreplies,
	struct afb_data *replies[],
	struct afb_req_common *comreq,
	int flags
);

/******************************************************************************/
#if WITH_AFB_HOOK
extern
void
afb_calls_call_hooking(
	struct afb_api_common *comapi,
	const char *api,
	const char *verb,
	unsigned nparams,
	struct afb_data * const params[],
	void (*callback)(void*, void*, void*, int, unsigned, struct afb_data * const[]),
	void *closure1,
	void *closure2,
	void *closure3
);

extern
int
afb_calls_call_sync_hooking(
	struct afb_api_common *comapi,
	const char *api,
	const char *verb,
	unsigned nparams,
	struct afb_data * const params[],
	int *status,
	unsigned *nreplies,
	struct afb_data *replies[]
);

extern
void
afb_calls_subcall_hooking(
	struct afb_api_common *comapi,
	const char *api,
	const char *verb,
	unsigned nparams,
	struct afb_data * const params[],
	void (*callback)(void*, void*, void*, int, unsigned, struct afb_data * const[]),
	void *closure1,
	void *closure2,
	void *closure3,
	struct afb_req_common *comreq,
	int flags
);

extern
int
afb_calls_subcall_sync_hooking(
	struct afb_api_common *comapi,
	const char *api,
	const char *verb,
	unsigned nparams,
	struct afb_data * const params[],
	int *status,
	unsigned *nreplies,
	struct afb_data *replies[],
	struct afb_req_common *comreq,
	int flags
);
#endif

/******************************************************************************/
