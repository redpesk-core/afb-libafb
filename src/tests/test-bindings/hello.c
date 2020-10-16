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
    fprintf(stderr, "Hello !");
    afb_req_reply(request, 0, 0, NULL);
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
