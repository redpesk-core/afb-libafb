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


#include <stdlib.h>

#include <json-c/json.h>
#include <afb/afb-auth.h>
#include <afb/afb-session.h>

#include "core/afb-auth.h"

/******************************************************************************/
/* json output format                                                         */
/******************************************************************************/

static struct json_object *addperm(struct json_object *o, struct json_object *x)
{
	struct json_object *a;

	if (!o)
		return x;

	if (!json_object_object_get_ex(o, "allOf", &a)) {
		a = json_object_new_array();
		json_object_array_add(a, o);
		o = json_object_new_object();
		json_object_object_add(o, "allOf", a);
	}
	json_object_array_add(a, x);
	return o;
}

static struct json_object *addperm_key_val(struct json_object *o, const char *key, struct json_object *val)
{
	struct json_object *x = json_object_new_object();
	json_object_object_add(x, key, val);
	return addperm(o, x);
}

static struct json_object *addperm_key_valstr(struct json_object *o, const char *key, const char *val)
{
	return addperm_key_val(o, key, json_object_new_string(val));
}

static struct json_object *addperm_key_valint(struct json_object *o, const char *key, int val)
{
	return addperm_key_val(o, key, json_object_new_int(val));
}

static struct json_object *addauth_or_array(struct json_object *o, const struct afb_auth *auth);

static struct json_object *addauth(struct json_object *o, const struct afb_auth *auth)
{
	switch(auth->type) {
	case afb_auth_No: return addperm(o, json_object_new_boolean(0));
	case afb_auth_Token: return addperm_key_valstr(o, "session", "check");
	case afb_auth_LOA: return addperm_key_valint(o, "LOA", (int)(auth->loa));
	case afb_auth_Permission: return addperm_key_valstr(o, "permission", auth->text);
	case afb_auth_Or: return addperm_key_val(o, "anyOf", addauth_or_array(json_object_new_array(), auth));
	case afb_auth_And: return addauth(addauth(o, auth->first), auth->next);
	case afb_auth_Not: return addperm_key_val(o, "not", addauth(NULL, auth->first));
	case afb_auth_Yes: return addperm(o, json_object_new_boolean(1));
	}
	return o;
}

static struct json_object *addauth_or_array(struct json_object *o, const struct afb_auth *auth)
{
	if (auth->type != afb_auth_Or)
		json_object_array_add(o, addauth(NULL, auth));
	else {
		addauth_or_array(o, auth->first);
		addauth_or_array(o, auth->next);
	}

	return o;
}

struct json_object *afb_auth_json_x2(const struct afb_auth *auth, uint32_t session)
{
	struct json_object *result = NULL;

	if (session & AFB_SESSION_CLOSE)
		result = addperm_key_valstr(result, "session", "close");

	if (session & AFB_SESSION_CHECK)
		result = addperm_key_valstr(result, "session", "check");

	if (session & AFB_SESSION_LOA_MASK)
		result = addperm_key_valint(result, "LOA", session & AFB_SESSION_LOA_MASK);

	if (auth)
		result = addauth(result, auth);

	return result;
}
