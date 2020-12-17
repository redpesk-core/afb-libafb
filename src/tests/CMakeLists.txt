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

pkg_check_modules(check check)

macro(addtest name)
	add_executable(test-${name} test-${name}.c)
	target_include_directories(test-${name} PRIVATE ${INCLUDE_DIRS} ${check_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/../libafb)
	target_link_libraries(test-${name} libafbsta ${ldflags}  ${check_LDFLAGS})
	add_test(NAME ${name} COMMAND test-${name} ${ARGN})
endmacro(addtest)

if(check_FOUND)
	addtest(ev-mgr)
	addtest(wrap-json)
	addtest(globset ${CMAKE_CURRENT_SOURCE_DIR}/globset.in ${CMAKE_CURRENT_SOURCE_DIR}/globset.out)
	addtest(u16id)
	addtest(session)
	addtest(apiset)
	addtest(api-common)
	addtest(api-v3)
	addtest(req-common)
	addtest(req-v3)
	addtest(afb-type)
	addtest(afb-data)
	addtest(token)
	addtest(afb-jobs)
	addtest(sig-monitor)
	addtest(sched)
	addtest(afb-auth)
	addtest(path-search)
	addtest(expand-vars)

	add_subdirectory(test-bindings)
	addtest(api-so-v4)

	if(NOT ${WITHOUT_CYNAGORA})
		addtest(afb-perm)
		add_dependencies(test-afb-perm cynagoraInit)
		add_custom_target(cynagoraInit
			SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cynagoraTest.initial
			COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/cynagoraTest.initial" "${CMAKE_CURRENT_BINARY_DIR}/cynagoraTest.initial"
			DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cynagoraTest.initial
		)
	endif(NOT ${WITHOUT_CYNAGORA})

	addtest(afb-calls)
else(check_FOUND)
	MESSAGE(WARNING "check not found! no test!")
endif(check_FOUND)

