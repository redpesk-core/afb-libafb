/*
 * Copyright (C) 2015-2026 IoT.bzh Company
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

struct afb_auth;

/**
 * Compute if auth is valid
 *
 * @param auth the auth structure to be validated
 *
 * @return 0 if invalid or 1 if valid
 */
extern
int afb_auth_is_valid(const struct afb_auth *auth);

/**
 * Compute the minimum value of loa
 *
 * @param auth the auth to process (must be valid)
 *
 * @return the minimal LOA value
 */
extern
unsigned afb_auth_minloa(const struct afb_auth *auth);

/**
 * Compute if session check is required
 *
 * @param auth the auth to process (must be valid)
 *
 * @return 0 if if token is not to be check
 *         or returns AFB_SESSION_CHECK if it has to be checked
 */
extern
unsigned afb_auth_check_token(const struct afb_auth *auth);

/**
 * Compute the string representation of the couple
 * auth and session.
 *
 * The callback function 'put' is called (generally more than
 * one time) to output the text representation.
 * It receives its closure and the text to add to the string
 * representation.
 *
 * @param auth    the auth to process (must be valid)
 * @param session the session flags
 * @param put     a function for writing text
 * @param closure a closure argument for the function put
 */
extern
void afb_auth_put_string(
	const struct afb_auth *auth,
	unsigned session,
	void (*put)(void *closure, const char *text),
	void *closure);
