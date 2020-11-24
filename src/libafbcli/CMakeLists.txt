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

###########################################
# build and install libafbcli
###########################################

set(DIR ../libafb)

set(LIBAFBCLI_SOURCES
	afb-ws-client.c
	${DIR}/misc/afb-socket.c
	${DIR}/misc/afb-ws.c
	${DIR}/core/afb-ev-mgr.c
	${DIR}/sys/ev-mgr.c
	${DIR}/sys/systemd.c
	${DIR}/sys/verbose.c
	${DIR}/utils/websock.c
	${DIR}/wsapi/afb-proto-ws.c
	${DIR}/wsapi/afb-wsapi.c
	${DIR}/wsj1/afb-wsj1.c
)

ADD_LIBRARY(libafbcli SHARED ${LIBAFBCLI_SOURCES})
set_property(TARGET libafbcli PROPERTY OUTPUT_NAME afbcli)

SET_TARGET_PROPERTIES(libafbcli PROPERTIES
	VERSION ${LIBAFBCLI_VERSION}
	SOVERSION ${LIBAFBCLI_SOVERSION})

TARGET_LINK_LIBRARIES(libafbcli
	${libsystemd_LDFLAGS}
	${json-c_LDFLAGS}
	-lpthread
	-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/export-libafbcli.map
	-Wl,--as-needed
	-Wl,--gc-sections
)

target_include_directories(libafbcli PUBLIC ${DIR})

INSTALL(TARGETS libafbcli LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
INSTALL(FILES
		afb-ws-client.h
		${DIR}/wsj1/afb-wsj1.h
		${DIR}/wsapi/afb-proto-ws.h
		${DIR}/wsapi/afb-wsapi.h
	DESTINATION
		${CMAKE_INSTALL_INCLUDEDIR}/libafbcli
)

CONFIGURE_FILE(libafbcli.pc.in libafbcli.pc @ONLY)
INSTALL(FILES
		${CMAKE_CURRENT_BINARY_DIR}/libafbcli.pc
	DESTINATION
		${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

