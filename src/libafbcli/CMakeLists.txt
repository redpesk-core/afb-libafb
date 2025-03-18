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

###########################################
# build and install libafbcli
###########################################

set(DIR ${CMAKE_CURRENT_SOURCE_DIR}/../libafb)

set(LIBAFBCLI_SOURCES
	afb-ws-client.c
	${DIR}/misc/afb-socket.c
	${DIR}/misc/afb-ws.c
	${DIR}/core/afb-ev-mgr.c
	${DIR}/core/afb-jobs.c
	${DIR}/sys/ev-mgr.c
	${DIR}/sys/systemd.c
	${DIR}/utils/websock.c
	${DIR}/wsapi/afb-proto-ws.c
	${DIR}/wsj1/afb-wsj1.c
	${DIR}/tls/tls.c
)

add_library(libafbcli OBJECT ${LIBAFBCLI_SOURCES})
target_compile_definitions(libafbcli PRIVATE -DWITH_JOB_NOT_MONITORED=1)
target_include_directories(libafbcli PRIVATE ${DIR})

add_library(libafbclista STATIC $<TARGET_OBJECTS:libafbcli>)
set_target_properties(libafbclista PROPERTIES
	OUTPUT_NAME afbcli
	VERSION ${LIBAFBCLI_VERSION}
)

add_library(libafbcliso SHARED $<TARGET_OBJECTS:libafbcli>)
set_target_properties(libafbcliso PROPERTIES
	OUTPUT_NAME afbcli
	VERSION ${LIBAFBCLI_VERSION}
	SOVERSION ${LIBAFBCLI_SOVERSION})
target_link_libraries(libafbcliso
	${libsystemd_LDFLAGS}
	${json-c_LDFLAGS}
	${gnutls_LDFLAGS}
	${librp-utils_LDFLAGS}
	-lpthread
	-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export-libafbcli.map
	-Wl,--as-needed
	-Wl,--gc-sections
)

set(PCLIBSPRIV ${libsystemd_LDFLAGS} ${json-c_LDFLAGS} ${gnutls_LDFLAGS} ${librp-utils_LDFLAGS} -lpthread)
list(JOIN PCLIBSPRIV " " PCLIBSPRIV)

install(TARGETS libafbclista ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS libafbcliso LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

add_custom_target(libafbcli-headers
	${CMAKE_CURRENT_SOURCE_DIR}/trfhdr
		${DIR}/wsj1/afb-wsj1.h
		${DIR}/wsapi/afb-proto-ws.h
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_dependencies(libafbcli libafbcli-headers)
add_dependencies(libafbclista libafbcli-headers)

install(FILES
		afb-ws-client.h
		${CMAKE_CURRENT_BINARY_DIR}/afb-wsj1.h
		${CMAKE_CURRENT_BINARY_DIR}/afb-proto-ws.h
		${CMAKE_CURRENT_BINARY_DIR}/afb-proto-ws.h
		${CMAKE_CURRENT_BINARY_DIR}/../libafb/libafb-config.h
	DESTINATION
		${CMAKE_INSTALL_INCLUDEDIR}/libafbcli
)

configure_file(libafbcli.pc.in libafbcli.pc @ONLY)
install(FILES
		${CMAKE_CURRENT_BINARY_DIR}/libafbcli.pc
	DESTINATION
		${CMAKE_INSTALL_LIBDIR}/pkgconfig
)
