/*
 Copyright (C) 2015-2020 IoT.bzh Company

 Author: Johann Gautier <johann.gautier@iot.bzh>

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

#include "libafb-config.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <check.h>

#include <json-c/json.h>


#include <afb/afb-auth.h>
#include <afb/afb-session.h>

#include "core/afb-auth.h"
#include "sys/verbose.h"
#include "utils/wrap-json.h"

START_TEST (test)
{
    const char * supposed_result[][8] = {
        {
		    "false",
		    "{ \"session\": \"check\" }",
		    "{ \"LOA\": 2 }",
		    "{ \"permission\": \"urn:test\" }",
		    "{ \"anyOf\": [ true, false ] }",
		    "{ \"allOf\": [ true, false ] }",
		    "{ \"not\": true }",
		    "true"
		},
		{
		    "{ \"allOf\": [ { \"LOA\": 1 }, false ] }",
		    "{ \"allOf\": [ { \"LOA\": 1 }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"LOA\": 1 }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"LOA\": 1 }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"LOA\": 1 }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"LOA\": 1 }, true, false ] }",
		    "{ \"allOf\": [ { \"LOA\": 1 }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"LOA\": 1 }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"LOA\": 2 }, false ] }",
		    "{ \"allOf\": [ { \"LOA\": 2 }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"LOA\": 2 }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"LOA\": 2 }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"LOA\": 2 }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"LOA\": 2 }, true, false ] }",
		    "{ \"allOf\": [ { \"LOA\": 2 }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"LOA\": 2 }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"LOA\": 3 }, false ] }",
		    "{ \"allOf\": [ { \"LOA\": 3 }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"LOA\": 3 }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"LOA\": 3 }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"LOA\": 3 }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"LOA\": 3 }, true, false ] }",
		    "{ \"allOf\": [ { \"LOA\": 3 }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"LOA\": 3 }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"session\": \"check\" }, false ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, true, false ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 1 }, false ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 1 }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 1 }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 1 }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 1 }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 1 }, true, false ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 1 }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 1 }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 2 }, false ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 2 }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 2 }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 2 }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 2 }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 2 }, true, false ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 2 }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 2 }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 3 }, false ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 3 }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 3 }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 3 }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 3 }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 3 }, true, false ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 3 }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 3 }, true ] }"
		},
		{
		    "false",
		    "{ \"session\": \"check\" }",
		    "{ \"LOA\": 2 }",
		    "{ \"permission\": \"urn:test\" }",
		    "{ \"anyOf\": [ true, false ] }",
		    "{ \"allOf\": [ true, false ] }",
		    "{ \"not\": true }",
		    "true"
		},
		{
		    "{ \"allOf\": [ { \"LOA\": 1 }, false ] }",
		    "{ \"allOf\": [ { \"LOA\": 1 }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"LOA\": 1 }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"LOA\": 1 }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"LOA\": 1 }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"LOA\": 1 }, true, false ] }",
		    "{ \"allOf\": [ { \"LOA\": 1 }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"LOA\": 1 }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"LOA\": 2 }, false ] }",
		    "{ \"allOf\": [ { \"LOA\": 2 }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"LOA\": 2 }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"LOA\": 2 }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"LOA\": 2 }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"LOA\": 2 }, true, false ] }",
		    "{ \"allOf\": [ { \"LOA\": 2 }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"LOA\": 2 }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"LOA\": 3 }, false ] }",
		    "{ \"allOf\": [ { \"LOA\": 3 }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"LOA\": 3 }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"LOA\": 3 }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"LOA\": 3 }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"LOA\": 3 }, true, false ] }",
		    "{ \"allOf\": [ { \"LOA\": 3 }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"LOA\": 3 }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"session\": \"check\" }, false ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, true, false ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 1 }, false ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 1 }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 1 }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 1 }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 1 }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 1 }, true, false ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 1 }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 1 }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 2 }, false ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 2 }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 2 }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 2 }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 2 }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 2 }, true, false ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 2 }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 2 }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 3 }, false ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 3 }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 3 }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 3 }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 3 }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 3 }, true, false ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 3 }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"session\": \"check\" }, { \"LOA\": 3 }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"session\": \"close\" }, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, true, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 1 }, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 1 }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 1 }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 1 }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 1 }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 1 }, true, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 1 }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 1 }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 2 }, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 2 }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 2 }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 2 }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 2 }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 2 }, true, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 2 }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 2 }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 3 }, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 3 }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 3 }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 3 }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 3 }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 3 }, true, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 3 }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 3 }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, true, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 1 }, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 1 }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 1 }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 1 }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 1 }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 1 }, true, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 1 }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 1 }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 2 }, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 2 }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 2 }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 2 }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 2 }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 2 }, true, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 2 }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 2 }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 3 }, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 3 }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 3 }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 3 }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 3 }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 3 }, true, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 3 }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 3 }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"session\": \"close\" }, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, true, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 1 }, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 1 }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 1 }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 1 }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 1 }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 1 }, true, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 1 }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 1 }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 2 }, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 2 }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 2 }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 2 }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 2 }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 2 }, true, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 2 }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 2 }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 3 }, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 3 }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 3 }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 3 }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 3 }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 3 }, true, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 3 }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"LOA\": 3 }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, true, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 1 }, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 1 }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 1 }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 1 }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 1 }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 1 }, true, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 1 }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 1 }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 2 }, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 2 }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 2 }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 2 }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 2 }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 2 }, true, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 2 }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 2 }, true ] }"
		},
		{
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 3 }, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 3 }, { \"session\": \"check\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 3 }, { \"LOA\": 2 } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 3 }, { \"permission\": \"urn:test\" } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 3 }, { \"anyOf\": [ true, false ] } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 3 }, true, false ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 3 }, { \"not\": true } ] }",
		    "{ \"allOf\": [ { \"session\": \"close\" }, { \"session\": \"check\" }, { \"LOA\": 3 }, true ] }"
		}
    };

    int i;
    struct afb_auth auth, first, next;
    struct json_object * result , * expected_result;
    uint32_t session;

    auth.next = &next;
    first.type = afb_auth_Yes;
    next.type = afb_auth_No;

    // Compare generated result to expected result
    for (session=0; session<32; session++){
        fprintf(stderr, "{\n");
        for(i=0; i<=7; i++){
            auth.type = i;
            switch(i){
	        case afb_auth_LOA:
                auth.loa = 2;
                break;
            case afb_auth_Permission:
                auth.text = "urn:test";
                break;
            default:
                auth.first = &first;
                break;
            }
            result = afb_auth_json_x2(&auth, session);
            expected_result = json_tokener_parse(supposed_result[session][i]);
            fprintf(stderr, "    '%s'", json_object_to_json_string(result));
            if(i<7) fprintf(stderr, ",");
            fprintf(stderr, "\n");
            ck_assert_int_eq(wrap_json_equal(expected_result, result), 1);
        }
        fprintf(stderr, "},\n");
    }
}
END_TEST


static Suite *suite;
static TCase *tcase;

void mksuite(const char *name) { suite = suite_create(name); }
void addtcase(const char *name) { tcase = tcase_create(name); suite_add_tcase(suite, tcase); }
void addtest(TFun fun) { tcase_add_test(tcase, fun); }
int srun()
{
	int nerr;
	SRunner *srunner = srunner_create(suite);
	srunner_run_all(srunner, CK_NORMAL);
	nerr = srunner_ntests_failed(srunner);
	srunner_free(srunner);
	return nerr;
}

int main(int ac, char **av)
{
	mksuite("afb_auth");
		addtcase("afb_auth");
			addtest(test);
	return !!srun();
}
