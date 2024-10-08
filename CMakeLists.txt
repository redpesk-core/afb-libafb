###########################################################################
# Copyright (C) 2015-2024 IoT.bzh Company
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

cmake_minimum_required(VERSION 3.13)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(afb-libafb
	VERSION 5.3.3
	DESCRIPTION "Core library for implementing Application framework Binder"
	HOMEPAGE_URL "https://github.com/redpesk-core/afb-libafb"
	LANGUAGES C)

set(LIBAFB_VERSION "6.0")
set(LIBAFB_SOVERSION "6")

set(LIBAFBCLI_VERSION "6.0")
set(LIBAFBCLI_SOVERSION "6")

include(FindPkgConfig)
include(CheckIncludeFiles)
include(CheckLibraryExists)
include(GNUInstallDirs)
include(CTest)

###########################################################################
# possible settings
option(WITH_SYSTEMD               "Require use of libsystemd"              ON)
option(WITHOUT_CYNAGORA           "Forbids use of cynagora"                OFF)
option(WITHOUT_TESTS              "Avoid compiling tests"                  OFF)
option(ARCH32                     "Set arch32"                             OFF)
option(WITH_RPUTILS_STATIC        "Link statically with librp-utils"       OFF)
#
#############################################################################
## design options
option(WITH_AFB_HOOK              "Include hooking"                        ON)
option(WITH_SIG_MONITOR_SIGNALS   "Activate handling of signals"           ON)
option(WITH_SIG_MONITOR_FOR_CALL  "Activate monitoring of calls"           ON)
option(WITH_SIG_MONITOR_TIMERS    "Activate monitoring of call expiration" ON)
option(WITH_AFB_TRACE             "Include monitoring trace"               ON)
option(WITH_SUPERVISION           "Activates installation of supervision"  ON)
option(WITH_DYNAMIC_BINDING       "Allow to load dynamic bindings (shared libraries)" ON)
option(WITH_LIBMICROHTTPD         "Activates HTTP server through LIBMICROHTTPD" ON)
option(WITH_L4VSOCK               "Activates L4RE support of VIRTIO-SOCKS" OFF)
option(WITH_CRED                  "Activate tracking of peer credentials"  ON)
option(WITH_REPLY_JOB             "Send replies in separate job"           ON)
option(WITH_ENVIRONMENT           "Use environment variables"              ON)
option(WITH_AFB_DEBUG             "Activates debug features"               ON)
option(WITH_CALL_PERSONALITY      "Call to personality (debugging)"        ON)
option(WITH_AFB_CALL_SYNC         "Allows the (sub)call synchronous"       ON)
option(WITH_API_CREATOR           "Record creator's API of API"            ON)
option(WITH_REQ_PROCESS_ASYNC     "Enforce asynchronous processing of requests" ON)
option(WITH_CASE_FOLDING          "Fold case for API, VERB, EVENT names"   OFF)
option(WITH_EXTENSION             "Activate ability to load extensions"    ON)
option(WITH_GNUTLS                "Activate TLS ability with gnutls"       OFF)
option(WITH_BINDINGS_V3           "Support legacy bindings version 3"      ON)
option(WITH_WSJ1                  "Activate WSJ1 protocol"                 ON)
option(WITH_WSAPI                 "Activate WSAPI protocol"                ON)
option(WITH_RPC_V1                "Activate RPC protocol version 1"        ON)
option(WITH_RPC_V3                "Activate RPC protocol version 3"        ON)
option(WITH_TRACK_JOB_CALL        "Track stack of jobs to detect locks"    OFF)
option(WITHOUT_JSON_C             "Remove use of json-c library"           OFF)
## tune options
option(WITH_SIG_MONITOR_DUMPSTACK "Activate dump stack on error"           ON)
option(WITH_FDEV_POLL             "Use poll implementation of fdev"        OFF)
option(WITH_FDEV_EPOLL            "Use epoll implementation of fdev"       OFF)
option(WITH_FDEV_SELECT           "Use select implementation of fdev"      OFF)
option(WITH_FDEV_SYSTEMD          "Use systemd implementation of fdev"     ON)
option(WITH_LOCALE_FOLDER         "Use folder image in locale-root"        ON) # TODO: set to OFF
option(WITH_LOCALE_SEARCH_NODE    "Use folder in searchs in locale-root"   ON) # TODO: set to OFF
option(WITH_WSCLIENT_URI_COPY     "Copy URI of ws-client specification"    OFF)
## system adaptation
option(WITH_FNMATCH               "Use fnmatch where possible"             ON)
option(WITH_LIBUUID               "Activates use of lib uuid"              ON)
option(WITH_EPOLL                 "Allow use of epoll"                     ON)
option(WITH_EVENTFD               "Allow use of eventfd"                   ON)
option(WITH_CLOCK_GETTIME         "Use clock_gettime where possible"       ON)
option(WITH_RANDOM_R              "Use random_r where possible"            ON)
option(WITH_OPENAT                "Use openat where possible"              ON)
option(WITH_DIRENT                "Use opendir and directory browsing"     ON)
option(WITH_SYS_UIO               "Use sys/uio.h for vectored IO"          ON)
option(WITH_REALPATH              "Use realpath function"                  ON)
option(WITH_UNIX_SOCKET           "Allow use of Unix Domain Sockets"       ON)
option(WITH_THREAD_LOCAL          "Allow to use _Thread_Local"             ON)
option(WITH_LIBMAGIC              "Activates use of libmagic"              ON)
#
###########################################################################
if(WITHOUT_JSON_C)
	set(WITH_AFB_HOOK OFF)
	set(WITH_BINDINGS_V3 OFF)
	set(WITH_WSJ1 OFF)
	set(WITH_WSAPI OFF)
	set(WITH_RPC_V1 OFF)
endif()

if(WITH_GNUTLS)
	add_definitions(-DWITH_TLS)
endif()
###########################################################################

link_libraries(-Wl,--as-needed -Wl,--gc-sections -Wl,--no-undefined)

add_compile_options(-Wall -Wextra -Wconversion -Wincompatible-pointer-types)
add_compile_options(-Wno-unused-parameter) # frankly not using a parameter does it care?
add_compile_options(-Wsign-compare -Wsign-conversion)
add_compile_options(
	$<$<C_COMPILER_ID:GNU>:-Werror=maybe-uninitialized>
	$<$<CXX_COMPILER_ID:GNU>:-Werror=maybe-uninitialized>
	$<$<C_COMPILER_ID:Clang>:-Werror=sometimes-uninitialized>
	$<$<CXX_COMPILER_ID:Clang>:-Werror=sometimes-uninitialized>
	$<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>
)
add_compile_options(-ffunction-sections -fdata-sections)
add_compile_options(-fPIC)
add_compile_options(-g -O2)
set (CMAKE_CXX_STANDARD 14)

if(ARCH32)
	add_compile_options(-m32)
	link_libraries(-m32)
endif()

set(CMAKE_C_FLAGS_RELEASE      "-g -O3")
#set(CMAKE_C_FLAGS_DEBUG        "-g -O1 -ggdb -fsanitize=address -fno-omit-frame-pointer -lasan")
#set(CMAKE_C_FLAGS_DEBUG        "-g -O1 -ggdb -fsanitize=thread -fno-omit-frame-pointer")
set(CMAKE_C_FLAGS_DEBUG        "-g -O1 -ggdb")
set(CMAKE_C_FLAGS_PROFILING    "-g -O1 -pg")
set(CMAKE_C_FLAGS_COVERAGE     "-g -O1 --coverage")

# for tools use
set(CMAKE_EXPORT_COMPILE_COMMANDS on)

###########################################################################

include(FindThreads)
find_package(Threads)

if (NOT WITH_ZEPHYR)
	PKG_CHECK_MODULES(afb-binding REQUIRED afb-binding>=4.1.0)
	
	if (NOT WITHOUT_JSON_C)
		PKG_CHECK_MODULES(json-c REQUIRED json-c)
	endif()

	if(WITH_RPUTILS_STATIC)
		PKG_CHECK_MODULES(librp-utils REQUIRED librp-utils-static>=0.0.3)
	else()
		PKG_CHECK_MODULES(librp-utils REQUIRED librp-utils>=0.0.3)
	endif()
endif()

if(WITH_LIBMICROHTTPD)
	PKG_CHECK_MODULES(libmicrohttpd REQUIRED libmicrohttpd>=0.9.60)
	if(WITH_LIBMAGIC)
		CHECK_INCLUDE_FILES(magic.h HAVE_MAGIC_H)
		CHECK_LIBRARY_EXISTS(magic magic_load "" HAVE_LIBMAGIC_SO)
		if(HAVE_MAGIC_H AND HAVE_LIBMAGIC_SO)
			set(LIBMAGIC_LDFLAGS -lmagic)
		else()
			message(WARNING "\"magic.h\" or \"libmagic.so\" missing.
			Please install the \"file-devel\" or \"libmagic-dev\" package !")
			set(WITH_LIBMAGIC OFF)
		endif()
	endif()
else()
	set(WITH_LIBMAGIC OFF)
endif(WITH_LIBMICROHTTPD)
if(WITH_LIBMAGIC)
	set(HAVE_LIBMAGIC 1)
else()
	set(HAVE_LIBMAGIC 0)
endif()

if(NOT WITHOUT_CYNAGORA)
	PKG_CHECK_MODULES(cynagora REQUIRED cynagora)
	add_definitions(-DBACKEND_PERMISSION_IS_CYNAGORA=1)
endif()

if(WITH_SYSTEMD)
	PKG_CHECK_MODULES(libsystemd REQUIRED libsystemd>=222)
else()
	#PKG_CHECK_MODULES(libsystemd libsystemd>=222)
endif(WITH_SYSTEMD)

if(WITH_LIBUUID)
	PKG_CHECK_MODULES(libuuid uuid)
	if(NOT libuuid_FOUND)
		message(WARNING "Dependency to lib 'uuid' is missing but WITH_LIBUUID is set!
		Setting WITH_LIBUUID to OFF and continuing without LIBUUID.")
		set(WITH_LIBUUID OFF)
	endif()
endif()

if(WITH_GNUTLS)
	PKG_CHECK_MODULES(gnutls REQUIRED gnutls>=3.6.5)
endif()

add_definitions(-DAFB_VERSION="${PROJECT_VERSION}")

add_subdirectory(src)

