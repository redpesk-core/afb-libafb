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

#include "../libafb-config.h"

#include "misc/afb-verbose.h"

#include <rp-utils/rp-verbose.h>

void afb_verbose_set(int mask)
{
	rp_set_logmask(mask);
}

int afb_verbose_get()
{
	return rp_logmask;
}

void afb_verbose(int loglevel, const char *file, int line, const char *function, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	rp_vverbose(loglevel, file, line, function, fmt, args);
	va_end(args);
}

void afb_vverbose(int loglevel, const char *file, int line, const char *function, const char *fmt, va_list args)
{
	rp_vverbose(loglevel, file, line, function, fmt, args);
}

int  afb_verbose_wants(int lvl)
{
	return rp_verbose_wants(lvl);
}

void afb_verbose_dec()
{
	rp_verbose_dec();
}

void afb_verbose_inc()
{
	rp_verbose_inc();
}

void afb_verbose_clear()
{
	rp_verbose_clear();
}

void afb_verbose_add(int level)
{
	rp_verbose_add(level);
}

void afb_verbose_sub(int level)
{
	rp_verbose_sub(level);
}

int afb_verbose_colorize(int value)
{
	return rp_verbose_colorize(value);
}

int afb_verbose_is_colorized()
{
	return rp_verbose_is_colorized();
}

int afb_verbose_level_of_name(const char *name)
{
	return rp_verbose_level_of_name(name);
}

const char *afb_verbose_name_of_level(int level)
{
	return rp_verbose_name_of_level(level);
}

