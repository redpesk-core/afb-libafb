#include <errno.h>
#include <stdint.h>

/**************************************************************************/
/**************************************************************************/
/***           BINDINGS V3                                              ***/
/**************************************************************************/
/**************************************************************************/
#if defined(BUG11) /* make a SEGV */

#define AFB_BINDING_VERSION 4
#include <afb/afb-binding.h>
int afbBindingEntry(afb_api_t api, afb_ctlid_t ctlid, afb_ctlarg_t ctlarg)
{
	return ((int(*)())(intptr_t)0)();
}
#endif
/**************************************************************************/
#if defined(BUG12) /* no afbBindingExport nor afbBindingEntry */

#define AFB_BINDING_VERSION 0
#include <afb/afb-binding.h>
struct afb_api_x4 *afbBindingV4root;

#endif
/**************************************************************************/
#if defined(BUG13) /* no afbBindingExportroot nor afbBindingEntry */

#define AFB_BINDING_VERSION 0
#include <afb/afb-binding.h>
const struct afb_binding_v4 afbBindingV4;
int afbBindingV4entry(struct afb_api_x4 *rootapi) { return 0; }

#endif
/**************************************************************************/
#if defined(BUG14) /* no api name */

#define AFB_BINDING_VERSION 4
#include <afb/afb-binding.h>

const afb_binding_t afbBindingExport;

#endif
/**************************************************************************/
#if defined(BUG15) /* bad api name */

#define AFB_BINDING_VERSION 4
#include <afb/afb-binding.h>

const afb_binding_t afbBindingExport = {
	.api = "bug 15"
};

#endif
/**************************************************************************/
#if defined(BUG16) /* both entry and preinit */

#define AFB_BINDING_VERSION 4
#include <afb/afb-binding.h>

int afbBindingEntry(afb_api_t rootapi, afb_ctlid_t ctlid, afb_ctlarg_t ctlarg) { return 0; }
const afb_binding_t afbBindingExport = {
	.api = "bug16",
	.mainctl = afbBindingEntry
};

#endif
/**************************************************************************/
#if defined(BUG17) /* entry fails */

#define AFB_BINDING_VERSION 4
#include <afb/afb-binding.h>

int afbBindingEntry(afb_api_t rootapi, afb_ctlid_t ctlid, afb_ctlarg_t ctlarg)
	{ errno = EAGAIN; return -1; }
#endif
/**************************************************************************/
#if defined(BUG18) /* preinit fails */

#define AFB_BINDING_VERSION 4
#include <afb/afb-binding.h>

static int err()
{
	errno = EAGAIN;
	return -1;
}

const afb_binding_t afbBindingExport = {
	.api = "bug18",
	.mainctl = (void*)err
};

#endif
/**************************************************************************/
#if defined(BUG19) /* preinit SEGV */

#define AFB_BINDING_VERSION 4
#include <afb/afb-binding.h>

static int bug()
{
	errno = 0;
	return ((int(*)())(intptr_t)0)();
}

const afb_binding_t afbBindingExport = {
	.api = "bug19",
	.mainctl = (void*)bug
};

#endif
/**************************************************************************/
#if defined(BUG20) /* init fails */

#define AFB_BINDING_VERSION 4
#include <afb/afb-binding.h>

static int err()
{
	errno = EAGAIN;
	return -1;
}

const afb_binding_t afbBindingExport = {
	.api = "bug20",
	.mainctl = (void*)err
};

#endif
/**************************************************************************/
#if defined(BUG21) /* init SEGV */

#define AFB_BINDING_VERSION 4
#include <afb/afb-binding.h>

static int bug()
{
	errno = 0;
	return ((int(*)())(intptr_t)0)();
}

const afb_binding_t afbBindingExport = {
	.api = "bug21",
	.mainctl = (void*)bug,
	.provide_class = "a b c",
	.require_class = "x y z",
	.require_api = "bug4 bug5",
};

#endif
/**************************************************************************/
