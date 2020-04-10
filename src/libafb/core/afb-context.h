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

struct afb_session;
struct afb_token;
#if WITH_CRED
struct afb_cred;
#endif

struct afb_context
{
	struct afb_session *session;	/**< session */
	struct afb_token *token;	/**< token */
#if WITH_CRED
	struct afb_cred *credentials;	/**< credential */
#endif

	const void *api_key;
	struct afb_context *super;
	union {
		unsigned flags;
		struct {
			unsigned created: 1;
			unsigned validated: 1;
			unsigned invalidated: 1;
			unsigned closing: 1;
			unsigned closed: 1;
		};
	};
};

extern void afb_context_init(struct afb_context *context, struct afb_session *session, struct afb_token *token);
extern int afb_context_connect(struct afb_context *context, const char *uuid, struct afb_token *token);
extern void afb_context_init_validated(struct afb_context *context, struct afb_session *session, struct afb_token *token);
extern int afb_context_connect_validated(struct afb_context *context, const char *uuid, struct afb_token *token);

extern void afb_context_subinit(struct afb_context *context, struct afb_context *super);
extern void afb_context_disconnect(struct afb_context *context);
extern const char *afb_context_uuid(struct afb_context *context);

extern void *afb_context_get(struct afb_context *context);
extern int afb_context_set(struct afb_context *context, void *value, void (*free_value)(void*));
extern void *afb_context_make(struct afb_context *context, int replace, void *(*make_value)(void *closure), void (*free_value)(void *item), void *closure);

extern void afb_context_change_token(struct afb_context *context, struct afb_token *token);
#if WITH_CRED
extern void afb_context_change_cred(struct afb_context *context, struct afb_cred *cred);
#endif

#if SYNCHRONOUS_CHECKS
extern int afb_context_on_behalf_import(struct afb_context *context, const char *exported);
#endif
extern void afb_context_on_behalf_import_async(
	struct afb_context *context,
	const char *exported,
	void (*callback)(void *_closure, int _status),
	void *closure
);

extern const char *afb_context_on_behalf_export(struct afb_context *context);
extern void afb_context_on_behalf_other_context(struct afb_context *context, struct afb_context *other);

#if SYNCHRONOUS_CHECKS
extern int afb_context_has_permission(struct afb_context *context, const char *permission);
#endif
extern void afb_context_has_permission_async(
	struct afb_context *context,
	const char *permission,
	void (*callback)(void *_closure, int _status),
	void *closure
);

#if SYNCHRONOUS_CHECKS
extern int afb_context_check(struct afb_context *context);
#endif
extern void afb_context_check_async(
	struct afb_context *context,
	void (*callback)(void *_closure, int _status),
	void *closure
);

extern void afb_context_close(struct afb_context *context);
extern int afb_context_check_loa(struct afb_context *context, unsigned loa);
extern int afb_context_force_loa(struct afb_context *context, unsigned loa);
#if SYNCHRONOUS_CHECKS
extern int afb_context_change_loa(struct afb_context *context, unsigned loa);
#endif
extern void afb_context_change_loa_async(struct afb_context *context, unsigned loa, void (*callback)(void *_closure, int _status), void *closure);
extern unsigned afb_context_get_loa(struct afb_context *context);

