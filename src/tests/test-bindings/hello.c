/*
 Copyright (C) 2015-2020 IoT.bzh Company

 Author: José Bollo <jose.bollo@iot.bzh>

 $RP_BEGIN_LICENSE$
 Commercial License Usage
  Licensees holding valid commercial IoT.bzh licenses may use this file in
  accordance with the commercial license agreement provided with the
  Software or, alternatively, in accordance with the terms contained in
  a written agreement between you and The IoT.bzh Company. For licensing terms
  and conditions see https://www.iot.bzh/terms-conditions. For further
  information use the contact form at https://www.iot.bzh/contact.

 GNU General Public License Usage
  Alternatively, this file may be used under the terms of the GNU General
  Public license version 3. This license is as published by the Free Software
  Foundation and appearing in the file LICENSE.GPLv3 included in the packaging
  of this file. Please review the following information to ensure the GNU
  General Public License requirements will be met
  https://www.gnu.org/licenses/gpl-3.0.html.
 $RP_END_LICENSE$
*/
#include <stdio.h>

#define AFB_BINDING_VERSION 4
#include <afb/afb-binding-v4.h>

#if !defined(APINAME)
#define APINAME "hello"
#endif

static int mainctl(afb_api_x4_t api, afb_ctlid_t ctlid, afb_ctlarg_t ctlarg, void *userdata){
    return 0;
}
static void hello(afb_req_t request, unsigned nparams, afb_data_t const *params){
    fprintf(stderr, "Hello !\n");
    afb_data_array_addref(nparams, params);
    afb_req_reply(request, 0, nparams, params);
}

static const struct afb_verb_v4 verbs[]= {
  { .verb="hello",        .callback=hello }
};

const struct afb_binding_v4 afbBindingExport = {
	.api = APINAME,
	.specification = NULL,
	.verbs = verbs,
	.mainctl = mainctl
};
