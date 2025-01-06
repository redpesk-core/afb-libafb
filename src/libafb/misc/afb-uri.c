/*
 * Copyright (C) 2015-2025 IoT.bzh Company
 * Author: Louis-Baptiste Sobolewski <lb.sobolewski@iot.bzh>
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

#include "../libafb-config.h"

#include <stdlib.h>
#include <string.h>

#include <rp-utils/rp-escape.h>

#include "core/afb-apiname.h"

const char *afb_uri_api_name(const char *uri)
{
    const char **args;
    const char *api, *as_api, *uri_args;
    char *apicpy;
    size_t len;

    /* look for "as-api" in URI query section */
    uri_args = strchr(uri, '?');
    if (uri_args != NULL) {
        args = rp_unescape_args(uri_args + 1);
        as_api = rp_unescaped_args_get(args, "as-api");
        if (as_api != NULL) {
            apicpy = malloc(strlen(as_api) + 1);
            strcpy(apicpy, as_api);
            free(args);
            goto check_return;
        }
    }

    /* look for a '/' or a ':' */
    len = uri_args ? (size_t)(uri_args - uri) : strlen(uri); // stop before the '?' when there's one
    api = memrchr(uri, '/', len);
    if (api == NULL) {
        api = strchr(uri, ':');
        if (api == NULL || strlen(api) < 2)
            return NULL;
        if (api[1] == '@')
            api++;
    }

    /* at this point api is the char before an api name */
    api++;
    if (uri_args != NULL)
        len = (size_t)(uri_args - api);
    else
        len = strlen(api);
    apicpy = malloc(len + 1);
    strncpy(apicpy, api, len);
    apicpy[len] = '\0';

check_return:
    if (afb_apiname_is_valid(apicpy))
        return apicpy;
    else {
        free(apicpy);
        return NULL;
    }
}
