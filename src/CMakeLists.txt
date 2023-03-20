###########################################################################
# Copyright (C) 2015-2023 IoT.bzh Company
#
# Author: José Bollo <jose.bollo@iot.bzh>
#
# $RP_BEGIN_LICENSE$
# Commercial License Usage
#  Licensees holding valid commercial IoT.bzh licenses may use this file in
#  accordance with the commercial license agreement provided with the
#  Software or, alternatively, in accordance with the terms contained in
#  a written agreement between you and The IoT.bzh Company. For licensing terms
#  and conditions see https://www.iot.bzh/terms-conditions. For further
#  information use the contact form at https://www.iot.bzh/contact.
#
# GNU General Public License Usage
#  Alternatively, this file may be used under the terms of the GNU General
#  Public license version 3. This license is as published by the Free Software
#  Foundation and appearing in the file LICENSE.GPLv3 included in the packaging
#  of this file. Please review the following information to ensure the GNU
#  General Public License requirements will be met
#  https://www.gnu.org/licenses/gpl-3.0.html.
# $RP_END_LICENSE$
###########################################################################

if (CMAKE_C_COMPILER_VERSION VERSION_LESS 4.9)
    message(FATAL_ERROR "Require at least gcc-4.9")
endif(CMAKE_C_COMPILER_VERSION VERSION_LESS 4.9)

############################################################################

set(incdirs
	${CMAKE_SOURCE_DIR}/include
	${json-c_INCLUDE_DIRS}
	${afb-binding_INCLUDE_DIRS}
	${librp-utils_INCLUDE_DIRS}
)
set(ldflags
	${CMAKE_THREAD_LIBS_INIT}
	${json-c_LDFLAGS}
	${afb-binding_LDFLAGS}
	${librp-utils_LDFLAGS}
)

set(deps
	json-c
	librp-utils
)

set(privs
)

if (WITH_SYSTEMD)
	set(incdirs ${incdirs} ${libsystemd_INCLUDE_DIRS})
	set(ldflags ${ldflags} ${libsystemd_LDFLAGS})
	set(deps ${deps} libsystemd)
endif()
if (NOT WITHOUT_CYNAGORA)
	set(incdirs ${incdirs} ${cynagora_INCLUDE_DIRS})
	set(ldflags ${ldflags} ${cynagora_LDFLAGS})
	set(deps ${deps} cynagora)
endif()
if(WITH_LIBUUID)
	set(incdirs ${incdirs} ${libuuid_INCLUDE_DIRS})
	set(ldflags ${ldflags} ${libuuid_LDFLAGS})
	set(deps ${deps} uuid)
endif()
if(WITH_GNUTLS)
	set(incdirs ${incdirs} ${gnutls_INCLUDE_DIRS})
	set(ldflags ${ldflags} ${gnutls_LDFLAGS})
	set(deps ${deps} gnutls)
endif()
if(WITH_LIBMICROHTTPD)
	set(incdirs ${incdirs} ${libmicrohttpd_INCLUDE_DIRS})
	set(ldflags ${ldflags} ${libmicrohttpd_LDFLAGS})
	set(deps ${deps} libmicrohttpd)
	if(WITH_LIBMAGIC)
		set(ldflags ${ldflags} -lmagic)
		set(privs ${privs} -lmagic)
	endif()
endif()
if(WITH_DYNAMIC_BINDING)
	set(ldflags ${ldflags} -ldl)
	set(privs ${privs} -ldl)
endif()
if(WITH_SIG_MONITOR_TIMERS)
	set(ldflags ${ldflags} -lrt)
	set(privs ${privs} -lrt)
endif()

include_directories(AFTER SYSTEM ${incdirs})

############################################################################

configure_file(libafb/libafb-config.h.in libafb-config.h @ONLY)
include_directories(BEFORE ${CMAKE_CURRENT_BINARY_DIR}/libafb)

############################################################################

ADD_SUBDIRECTORY(libafb)

############################################################################

if(WITH_SYSTEMD AND libsystemd_FOUND)
	ADD_SUBDIRECTORY(libafbcli)
endif()
if(NOT WITHOUT_TESTS)
	ADD_SUBDIRECTORY(tests)
endif()
