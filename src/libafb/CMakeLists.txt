###########################################################################
# Copyright (C) 2015-2021 IoT.bzh Company
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

file(GLOB LIBAFBSRCS
	core/*.c
	utils/*.c
	sys/*.c
	http/*.c
	apis/*.c
	misc/*.c
	wsapi/*.c
	wsj1/*.c
	extend/*.c
)
set(LIBAFB_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

###########################################################################

add_library(libafb OBJECT ${LIBAFBSRCS})
target_include_directories(libafb PUBLIC ${LIBAFB_SOURCE_DIR})

###########################################################################

add_library(libafbsta STATIC $<TARGET_OBJECTS:libafb>)
set_target_properties(libafbsta PROPERTIES
	OUTPUT_NAME afb
	VERSION ${LIBAFB_VERSION}
)
INSTALL(TARGETS libafbsta ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

###########################################################################

add_library(libafbso SHARED  $<TARGET_OBJECTS:libafb>)
set_target_properties(libafbso PROPERTIES
	OUTPUT_NAME afb
	VERSION ${LIBAFB_VERSION}
	SOVERSION ${LIBAFB_SOVERSION}
)
TARGET_LINK_LIBRARIES(libafbso
	${ldflags}
	-Wl,--as-needed
	-Wl,--gc-sections
)
INSTALL(TARGETS libafbso LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

# install header files
INSTALL(DIRECTORY
		core
		utils
		sys
		http
		apis
		misc
		wsapi
		wsj1
		extend
	DESTINATION
		${CMAKE_INSTALL_INCLUDEDIR}/libafb
	FILES_MATCHING PATTERN
		*/*.h
)

INSTALL(FILES
		${CMAKE_CURRENT_BINARY_DIR}/../libafb-config.h
	DESTINATION
		${CMAKE_INSTALL_INCLUDEDIR}/libafb
)

# install pkgconfig file
list(JOIN deps " " PCREQUIRES)
list(JOIN privs " " PCLIBSPRIV)

CONFIGURE_FILE(libafb.pc.in libafb.pc @ONLY)
INSTALL(FILES
		${CMAKE_CURRENT_BINARY_DIR}/libafb.pc
	DESTINATION
		${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

