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

#pragma once

#include "../libafb-config.h"

#include <stdarg.h>

enum
{
	afb_Log_Level_Emergency = 0,
	afb_Log_Level_Alert = 1,
	afb_Log_Level_Critical = 2,
	afb_Log_Level_Error = 3,
	afb_Log_Level_Warning = 4,
	afb_Log_Level_Notice = 5,
	afb_Log_Level_Info = 6,
	afb_Log_Level_Debug = 7,
	afb_Log_Level_Extra_Debug = 8
};

extern void afb_verbose(int loglevel, const char *file, int line, const char *function, const char *fmt, ...) __attribute__((format(printf, 5, 6)));
extern void afb_vverbose(int loglevel, const char *file, int line, const char *function, const char *fmt, va_list args);
extern int  afb_verbose_wants(int lvl);

extern void afb_verbose_set(int mask);
extern int  afb_verbose_get(void);
extern void afb_verbose_dec(void);
extern void afb_verbose_inc(void);
extern void afb_verbose_clear(void);
extern void afb_verbose_add(int level);
extern void afb_verbose_sub(int level);
extern int afb_verbose_colorize(int value);
extern int afb_verbose_is_colorized(void);
extern int afb_verbose_level_of_name(const char *name);
extern const char *afb_verbose_name_of_level(int level);

#if defined(LIBAFB_VERBOSE_NO_DATA)
#  define _LIBAFB_VERBOSE_(lvl,...)    \
                 do{ if(afb_verbose_wants(lvl)) afb_verbose((lvl), NULL, 0, NULL, \
				((lvl)<=afb_Log_Level_Error ? (fmt) : NULL)__VA_ARGS__); } while(0)
#elif defined(LIBAFB_VERBOSE_NO_DETAILS)
#  define _LIBAFB_VERBOSE_(lvl,...)    \
                 do{ if(afb_verbose_wants(lvl)) afb_verbose((lvl), NULL, 0, NULL, __VA_ARGS__); } while(0)
#else
#  define _LIBAFB_VERBOSE_(lvl,...)    \
                 do{ if(afb_verbose_wants(lvl)) afb_verbose((lvl), __FILE__, __LINE__, __func__, __VA_ARGS__); } while(0)
#endif

#define LIBAFB_EMERGENCY(...)	_LIBAFB_VERBOSE_(afb_Log_Level_Emergency, __VA_ARGS__)
#define LIBAFB_ALERT(...)	_LIBAFB_VERBOSE_(afb_Log_Level_Alert, __VA_ARGS__)
#define LIBAFB_CRITICAL(...)	_LIBAFB_VERBOSE_(afb_Log_Level_Critical, __VA_ARGS__)
#define LIBAFB_ERROR(...)	_LIBAFB_VERBOSE_(afb_Log_Level_Error, __VA_ARGS__)
#define LIBAFB_WARNING(...)	_LIBAFB_VERBOSE_(afb_Log_Level_Warning, __VA_ARGS__)
#define LIBAFB_NOTICE(...)	_LIBAFB_VERBOSE_(afb_Log_Level_Notice, __VA_ARGS__)
#define LIBAFB_INFO(...)	_LIBAFB_VERBOSE_(afb_Log_Level_Info, __VA_ARGS__)
#define LIBAFB_DEBUG(...)	_LIBAFB_VERBOSE_(afb_Log_Level_Debug, __VA_ARGS__)
#define LIBAFB_EXTRA_DEBUG(...)	_LIBAFB_VERBOSE_(afb_Log_Level_Extra_Debug, __VA_ARGS__)

