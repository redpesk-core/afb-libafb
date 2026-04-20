/*
 * Copyright (C) 2015-2025 IoT.bzh Company
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

#include "core/afb-info.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "sys/x-errno.h"

#include "core/afb-v4-itf.h"
#include "core/afb-data.h"
#include "core/afb-type-predefined.h"
#include "core/afb-auth.h"

/*
* states:
*
*               |
*          init |
*               V
*              INIT
*               |
*       sed-api |
*               |
*               +---------------+
*               |               |
*  add-verb     V               V
* +--------> GOTSPEC          GOTAPI
* |             |               |
* |             |               |   add-verb
* +-------------+               +------------+
*               |               |            |
*               |               |            V     add-verb
*               |               |         GOTVERB <--------+
*               |               |            |             |
*               |               |            +-------------+
*               |               |            |
*               +---------------+------------+
*                               |
*                           end |
*                               V
*                              INIT
*
* Other cases leads to error until init.
*/
#define INIT    0
#define GOTSPEC 1
#define GOTAPI  2
#define GOTVERB 3

const char afb_info_verbname[] = "info";
static const char schema[] = "afb-api-info/v2";

/************************************************************************
 ** manage info internals
 ***********************************************************************/

/* record the first error and return it until reset */
static int ret(struct afb_info *info, int rc)
{
	if (info->state < 0)
		return info->state;
	if (rc < 0)
		info->state = rc;
	return rc;
}

/* copy the string to the buffer */
static void putstr(struct afb_info *info, const char *str, bool escape)
{
	for(;;) {
		char c = *str++;
		if (c == 0) {
			if (info->pos < info->size)
				info->buffer[info->pos] = c;
			return;
		}
		if (escape && (c == '"' || c == '\\')) {
			if (info->pos < info->size)
				info->buffer[info->pos] = '\\';
			info->pos++;
		}
		if (info->pos < info->size)
			info->buffer[info->pos] = c;
		info->pos++;
	}
}

/************************************************************************
 ** manage authorisation
 ***********************************************************************/

static void putauthstr(
	void *closure,
	const char *text
) {
	struct afb_info *info = closure;
	putstr(info, text, true);
}

/* put the auth string */
static void putauth(
	struct afb_info *info,
	const struct afb_auth *auth,
	unsigned session
) {
	afb_auth_put_string(auth, session, putauthstr, info);
}

/************************************************************************
 ** public interface
 ***********************************************************************/

void afb_info_init(
	struct afb_info *info
) {
	memset(info, 0, sizeof *info);
}

int afb_info_set_api(
	struct afb_info *info,
	const char *apiname,
	const char *apiinfo,
	const char *apispec
) {
	int rc = X_EINVAL;

	if (info->state != INIT)
		goto end;

	if (apispec != NULL) {
		info->state = GOTSPEC;
		info->spec = apispec;
		return ret(info, 0);
	}

	if (apiname == NULL)
		goto end;

	/* begin with schema */
	putstr(info, "{\"$schema\":\"", false);
	putstr(info, schema, true);
	putstr(info, "\"", false);

	/* name of the API (optional) */
	if (apiname != NULL) {
		putstr(info, ",\"name\":\"", false);
		putstr(info, apiname, true);
		putstr(info, "\"", false);
	}

	/* info of the API (optional) */
	if (apiinfo != NULL) {
		putstr(info, ",\"info\":\"", false);
		putstr(info, apiinfo, true);
		putstr(info, "\"", false);
	}

	/* verbs, mandatory */
	putstr(info, ",\"verbs\":[", false);

	info->state = GOTAPI;
	rc = 0;

end:
	return ret(info, rc);
}

int afb_info_add_verb(
	struct afb_info *info,
	const char *verbname,
	const char *verbinfo,
	unsigned session,
	const struct afb_auth *auth,
	unsigned glob
) {
	int rc = X_EINVAL;

	if (verbname == NULL)
		goto end;

	switch(info->state) {
	case GOTSPEC:
		/* do nothing if spec exists */
		return ret(info, 0);

	case GOTAPI:
		/* first verb */
		info->state = GOTVERB;
		break;

	case GOTVERB:
		/* other verbs */
		putstr(info, ",", false);
		break;

	case INIT:
	default:
		goto end;
	}

	/* enter dictionary */
	putstr(info, "{\"name\":\"", false);
	putstr(info, verbname, true);
	putstr(info, "\"", false);

	if (verbinfo != NULL) {
		putstr(info, ",\"info\":\"", false);
		putstr(info, verbinfo, true);
		putstr(info, "\"", false);
	}
	if (glob)
		putstr(info, ",\"glob\":true", false);
	if (session & AFB_SESSION_CLOSE)
		putstr(info, ",\"session-close\":true", false);

	session &= AFB_SESSION_LOA_MASK | AFB_SESSION_CHECK;
	if (auth != NULL || session != 0) {
		putstr(info, ",\"auth\":\"", false);
		putauth(info, auth, session);
		putstr(info, "\"", false);
	}
	putstr(info, "}", false);
	rc = 0;

end:
	return ret(info, rc);
}

int afb_info_end(
	struct afb_info *info,
	struct afb_data **data
) {
	int rc;
	switch(info->state) {
	case INIT:
		/* bad state */
		rc = X_EINVAL;
		break;

	case GOTSPEC:
		/* make the data from the spec */
		rc = afb_data_create_raw(data, &afb_type_predefined_json,
			info->spec, 1 + strlen(info->spec), NULL, NULL);
		if (rc > 0)
			rc = 0;
		break;

	case GOTAPI:
	case GOTVERB:
		putstr(info, "]}", false);
		if (info->pos >= info->size) {
			if (info->buffer != NULL)
				rc = X_EOVERFLOW;
			else {
				info->buffer = malloc(info->pos + 1);
				if (info->buffer == NULL)
					rc = X_ENOMEM;
				else {
					info->size = info->pos + 1;
					rc = 1; /* again */
				}
			}
		}
		else {
			if (info->buffer == NULL)
				rc = X_EINVAL;
			else {
				/* make the data from the spec */
				rc = afb_data_create_raw(data, &afb_type_predefined_json,
						info->buffer, 1 + info->pos,
						free, info->buffer);
				if (rc > 0)
					rc = 0;
				info->buffer = NULL;
				info->size = 0;
			}
		}
		break;

	default:
		/* had an error */
		rc = info->state;
		break;
	}
	info->state = INIT;
	info->pos = 0;
	if (rc <= 0) {
		free(info->buffer);
		info->buffer = NULL;
		info->size = 0;
	}
	return ret(info, rc);
}


