/*
  Copyright (C) 2015-2022 IoT.bzh Company

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

afb_event_t event;

static void hello(afb_req_t request, unsigned nparams, afb_data_t const *params){
  fprintf(stderr, "Hello !\n");
}

static void call(afb_req_t request, unsigned nparams, afb_data_t const *params){
  fprintf(stderr, "Hello : call !\n");
  afb_data_array_addref(nparams, params);
  afb_req_reply(request, 0, nparams, params);
}

static void subscribe(afb_req_t request, unsigned nparams, afb_data_t const *params){
  int rc;
  afb_api_new_event(afbBindingRoot, "event", &event);
  rc = afb_req_subscribe(request, event);
  if (rc >= 0){
    fprintf(stderr, "Hello : subscribe success!\n");
    afb_data_array_addref(nparams, params);
    afb_req_reply(request, 0, nparams, params);
  }
  else
    fprintf(stderr, "Hello : subscribe fail !\n");
}

static void unsubscribe(afb_req_t request, unsigned nparams, afb_data_t const *params){
  int rc;
  rc = afb_req_unsubscribe(request, event);
  if(rc >= 0){
    fprintf(stderr, "Hello : unsubscribe success !\n");
    afb_data_array_addref(nparams, params);
    afb_req_reply(request, 0, nparams, params);
  }
  else fprintf(stderr, "Hello : unsubscribe fail !\n");
}

static void evpush(afb_req_t request, unsigned nparams, afb_data_t const *params){
  int rc;
  afb_data_array_addref(nparams, params);
  rc = afb_event_push(event, nparams, params);
  if( rc>= 0){
    fprintf(stderr, "Hello : evpush sucess !\n");
    afb_data_array_addref(nparams, params);
    afb_req_reply(request, 0, nparams, params);
  }
  else fprintf(stderr, "Hello : evpush fail !\n");
}

static const struct afb_verb_v4 verbs[]= {
  { .verb="hello",        .callback=hello },
  { .verb="call",         .callback=call },
  { .verb="subscribe",    .callback=subscribe },
  { .verb="unsubscribe",  .callback=unsubscribe },
  { .verb="evpush",       .callback=evpush },
  { .verb=NULL }
};

static int mainctl(afb_api_x4_t api, afb_ctlid_t ctlid, afb_ctlarg_t ctlarg, void *userdata){
  switch(ctlid){
    case afb_ctlid_Root_Entry :
      fprintf(stderr, "Binding hello receved control signal %d : afb_ctlid_Root_Entry\n", afb_ctlid_Root_Entry);
    break;

    case afb_ctlid_Pre_Init :
      fprintf(stderr, "Binding hello receved control signal %d : afb_ctlid_Pre_Init\n", afb_ctlid_Pre_Init);
    break;

    case afb_ctlid_Init :
      fprintf(stderr, "Binding hello receved control signal %d : afb_ctlid_Init\n", afb_ctlid_Init);
    break;

    case afb_ctlid_Class_Ready :
      fprintf(stderr, "Binding hello receved control signal %d : afb_ctlid_Class_Ready\n", afb_ctlid_Class_Ready);
    break;

    case afb_ctlid_Orphan_Event :
      fprintf(stderr, "Binding hello receved control signal %d : afb_ctlid_Orphan_Event\n", afb_ctlid_Orphan_Event);
    break;

    case afb_ctlid_Exiting :
      fprintf(stderr, "Binding hello receved control signal %d : afb_ctlid_Exiting\n", afb_ctlid_Exiting);
    break;
  }

  return 0;
}
const struct afb_binding_v4 afbBindingExport = {
  .api = APINAME,
  .specification = NULL,
  .verbs = verbs,
  .mainctl = mainctl
};
