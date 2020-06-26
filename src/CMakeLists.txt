###########################################################################
# Copyright (C) 2015-2020 IoT.bzh Company
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

INCLUDE_DIRECTORIES(
	${CMAKE_SOURCE_DIR}/include
	${json-c_INCLUDE_DIRS}
	${libsystemd_INCLUDE_DIRS}
	${libmicrohttpd_INCLUDE_DIRS}
	${libuuid_INCLUDE_DIRS}
	${cynagora_INCLUDE_DIRS}
	${cynara_INCLUDE_DIRS}
)

SET(link_libraries
	${CMAKE_THREAD_LIBS_INIT}
	${json-c_LDFLAGS}
	${libsystemd_LDFLAGS}
	${libmicrohttpd_LDFLAGS}
	${libuuid_LDFLAGS}
	${cynagora_LDFLAGS}
	${cynara_LDFLAGS}
	${LIBMAGIC_LDFLAGS}
	-ldl
	-lrt
)

############################################################################

configure_file(libafb-config.h.in libafb-config.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

############################################################################

ADD_SUBDIRECTORY(libafb)

############################################################################

if(WITH_SYSTEMD AND libsystemd_FOUND)
	ADD_SUBDIRECTORY(libafbwsc)
endif()
if(NOT WITHOUT_TESTS)
	ADD_SUBDIRECTORY(tests)
endif()
