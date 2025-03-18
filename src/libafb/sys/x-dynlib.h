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

#pragma once


#include "../libafb-config.h"

#if WITH_EXTENSION || WITH_DYNAMIC_BINDING

typedef struct { void *handle; } x_dynlib_t;

extern int x_dynlib_open(const char *filename, x_dynlib_t *dynlib, int global, int lazy);

extern void x_dynlib_close(x_dynlib_t *dynlib);

extern int x_dynlib_symbol(x_dynlib_t *dynlib, const char* name, void** ptr);

extern const char* x_dynlib_error(const x_dynlib_t *dynlib);

#endif
#if WITH_ZEPHYR_LLEXT

#include <zephyr/llext/llext.h>

typedef struct llext x_dynlib_t;

extern int x_dynlib_symbol(x_dynlib_t *ext, const char* name, void** ptr);

#endif
