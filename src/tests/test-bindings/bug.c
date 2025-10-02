/*
 Copyright (C) 2015-2025 IoT.bzh Company

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

#include <errno.h>
#include <stdint.h>

/**************************************************************************/
/**************************************************************************/
/***           BINDINGS V4                                              ***/
/**************************************************************************/
/**************************************************************************/
#if defined(BUG11) /* make a SEGV */

#define AFB_BINDING_VERSION 4
#define AFB_BINDING_X4R1_ITF_REVISION 1
#include <afb/afb-binding.h>
int afbBindingEntry(afb_api_t api, afb_ctlid_t ctlid, afb_ctlarg_t ctlarg, void *userdata)
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
#define AFB_BINDING_X4R1_ITF_REVISION 1
#include <afb/afb-binding.h>

const afb_binding_t afbBindingExport;

#endif
/**************************************************************************/
#if defined(BUG15) /* bad api name */

#define AFB_BINDING_VERSION 4
#define AFB_BINDING_X4R1_ITF_REVISION 1
#include <afb/afb-binding.h>

const afb_binding_t afbBindingExport = {
	.api = "bug 15"
};

#endif
/**************************************************************************/
#if defined(BUG16) /* both entry and preinit but the same, not more a bug! */

#define AFB_BINDING_VERSION 4
#define AFB_BINDING_X4R1_ITF_REVISION 1
#include <afb/afb-binding.h>

int afbBindingEntry(afb_api_t rootapi, afb_ctlid_t ctlid, afb_ctlarg_t ctlarg, void *userdata)
{
	return 0;
}

const afb_binding_t afbBindingExport = {
	.api = "bug16",
	.mainctl = afbBindingEntry
};

#endif
/**************************************************************************/
#if defined(BUG17) /* entry fails */

#define AFB_BINDING_VERSION 4
#define AFB_BINDING_X4R1_ITF_REVISION 1
#include <afb/afb-binding.h>

int afbBindingEntry(afb_api_t rootapi, afb_ctlid_t ctlid, afb_ctlarg_t ctlarg, void *userdata)
{
	return -EAGAIN;
}
#endif
/**************************************************************************/
#if defined(BUG18) /* preinit fails */

#define AFB_BINDING_VERSION 4
#define AFB_BINDING_X4R1_ITF_REVISION 1
#include <afb/afb-binding.h>

static int err()
{
	return -EAGAIN;
}

const afb_binding_t afbBindingExport = {
	.api = "bug18",
	.mainctl = (void*)err
};

#endif
/**************************************************************************/
#if defined(BUG19) /* preinit SEGV */

#define AFB_BINDING_VERSION 4
#define AFB_BINDING_X4R1_ITF_REVISION 1
#include <afb/afb-binding.h>

static int bug()
{
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
#define AFB_BINDING_X4R1_ITF_REVISION 1
#include <afb/afb-binding.h>

static int err()
{
	return -EAGAIN;
}

const afb_binding_t afbBindingExport = {
	.api = "bug20",
	.mainctl = (void*)err
};

#endif
/**************************************************************************/
#if defined(BUG21) /* init SEGV */

#define AFB_BINDING_VERSION 4
#define AFB_BINDING_X4R1_ITF_REVISION 1
#include <afb/afb-binding.h>

static int bug()
{
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
#if defined(BUG22) /* both entry and preinit but not the same */

#define AFB_BINDING_VERSION 4
#define AFB_BINDING_X4R1_ITF_REVISION 1
#include <afb/afb-binding.h>

int afbBindingEntry(afb_api_t rootapi, afb_ctlid_t ctlid, afb_ctlarg_t ctlarg, void *userdata)
{
	return 0;
}

int afbBindingEntry2(afb_api_t rootapi, afb_ctlid_t ctlid, afb_ctlarg_t ctlarg, void *userdata)
{
	return 0;
}

const afb_binding_t afbBindingExport = {
	.api = "bug22",
	.mainctl = afbBindingEntry2
};

#endif
