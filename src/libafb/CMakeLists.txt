###########################################################################
# Copyright (C) 2015-2025 IoT.bzh Company
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

configure_file(libafb-config.h.in libafb-config.h @ONLY)

###########################################################################

macro(installfiles dest)
	install(FILES ${ARGN} DESTINATION ${dest})
endmacro(installfiles)

set(DESTHDR ${CMAKE_INSTALL_INCLUDEDIR}/libafb)

macro(installheader)
	installfiles(${DESTHDR} ${ARGV})
endmacro(installheader)

installheader(${CMAKE_CURRENT_BINARY_DIR}/libafb-config.h)
installheader(libafb.h libafb afb-extension.h afb-v4.h)

foreach(entry core utils sys http apis misc wsapi wsj1 extend tls rpc)
	file(GLOB hdr ${entry}/*.h ${entry}/*.map)
	installfiles(${DESTHDR}/${entry} ${hdr})
	installheader(afb-${entry}.h)
	file(GLOB src ${entry}/*.c)
	list(APPEND LIBAFBSRCS ${src})
endforeach(entry)

###########################################################################

set(LIBAFB_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

###########################################################################

add_library(libafb OBJECT ${LIBAFBSRCS})
target_include_directories(libafb BEFORE PUBLIC ${LIBAFB_SOURCE_DIR})

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

# install pkgconfig file

#list(JOIN deps " " PCREQUIRES)
set(PCREQUIRES "")
foreach(x IN LISTS deps)
	string(APPEND PCREQUIRES " " ${x})
endforeach(x)

#list(JOIN privs " " PCLIBSPRIV)
set(PCLIBSPRIV "")
foreach(x IN LISTS privs)
	string(APPEND PCLIBSPRIV " " ${x})
endforeach(x)

CONFIGURE_FILE(libafb.pc.in libafb.pc @ONLY)
INSTALL(FILES
		${CMAKE_CURRENT_BINARY_DIR}/libafb.pc
	DESTINATION
		${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

