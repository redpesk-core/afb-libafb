/*
 * Copyright (C) 2015-2024 IoT.bzh Company
 * Author: José Bollo <jose.bollo@iot.bzh>
 *
 * $RP_BEGIN_LICENSE$
 * Commercial License Usage
 *  Licensees holding valid commercial IoT.bzh licenses may use this file in
 *  accordance with the commercial license agreement provided with the
 *  Software or, alternatively, in accordance with the terms contained in
 *  a written agreement between you and The IoT.bzh Company. For licensing terms
 *  and conditions see https://www.iot.bzh/terms-conditions. For further
 *  information use the contact form at https://www.iot.bzh/contact.
 *
 * GNU General Public License Usage
 *  Alternatively, this file may be used under the terms of the GNU General
 *  Public license version 3. This license is as published by the Free Software
 *  Foundation and appearing in the file LICENSE.GPLv3 included in the packaging
 *  of this file. Please review the following information to ensure the GNU
 *  General Public License requirements will be met
 *  https://www.gnu.org/licenses/gpl-3.0.html.
 * $RP_END_LICENSE$
 */

#pragma once

#if !defined(_GNU_SOURCE)
#  define _GNU_SOURCE
#endif

#define LIBAFB_VERSION_MAJOR   5
#define LIBAFB_VERSION_MINOR   2
#define LIBAFB_VERSION_PATCH   0

#define LIBAFB_MAKE_VERSION(major,minor,patch)   ((major) * 0x1000000 + (minor) * 0x10000 + (patch))
#define LIBAFB_VERSION                           LIBAFB_MAKE_VERSION(LIBAFB_VERSION_MAJOR, LIBAFB_VERSION_MINOR, LIBAFB_VERSION_PATCH)
#define LIBAFB_BEFORE_VERSION(major,minor,patch) (LIBAFB_MAKE_VERSION(major,minor,patch) > LIBAFB_VERSION)
#define LIBAFB_SINCE_VERSION(major,minor,patch)  (!LIBAFB_BEFORE_VERSION(major,minor,patch))
#define LIBAFB_AFTER_VERSION(major,minor,patch)  LIBAFB_SINCE_VERSION(major,minor,patch)

#define WITH_SIG_MONITOR_DUMPSTACK 0
#define WITH_SIG_MONITOR_SIGNALS 0
#define WITH_SIG_MONITOR_FOR_CALL 0
#define WITH_SIG_MONITOR_TIMERS 0
#define WITH_AFB_HOOK 0
#define WITH_AFB_TRACE 0
#define WITH_SUPERVISION 0
#define WITH_DYNAMIC_BINDING 0
#define WITH_REPLY_JOB 1
#define WITH_SYSTEMD 0
#define WITH_LIBMICROHTTPD 0
#define WITH_ENVIRONMENT 0
#define WITH_AFB_DEBUG 0
#define WITH_CALL_PERSONALITY 0
#define WITH_AFB_CALL_SYNC 0
#define WITH_FNMATCH 0
#define WITH_L4VSOCK 0
#define WITH_LIBUUID 0
#define WITH_EPOLL 0
#define WITH_EVENTFD 1
#define WITH_TIMERFD 0
#define WITH_CLOCK_GETTIME 0
#define WITH_RANDOM_R 0
#define WITH_OPENAT 0
#define WITH_DIRENT 0
#define WITH_SYS_UIO 1
#define WITH_WSCLIENT_URI_COPY 0
#define WITH_REALPATH 0
#define WITH_CRED 0
#define WITH_TCP_SOCKET 1
#define WITH_UNIX_SOCKET 0
#define WITH_THREAD_LOCAL 1
#define WITH_API_CREATOR 0
#define WITH_REQ_PROCESS_ASYNC 1
#define WITH_CASE_FOLDING 0
#define WITH_EXTENSION 0
#define WITH_GNUTLS 0
#define WITH_BINDINGS_V3 0
#define WITH_WSJ1 0
#define WITH_WSAPI 0
#define WITH_RPC_V1 0
#define WITH_RPC_V3 1
#define WITH_TRACK_JOB_CALL 0
#define WITHOUT_JSON_C 1
#define WITH_LOCALE_ROOT 0
#define WITH_LOCALE_FOLDER 0
#define WITH_LOCALE_SEARCH_NODE 0
#define MINIMAL_LOCALE_ROOT 0

#if WITH_LIBMICROHTTPD
/* Always add INFER_EXTENSION (more details in afb-hreq.c) */
#  define INFER_EXTENSION 1
#  define HAVE_LIBMAGIC 0
#endif
#if !WITH_AFB_HOOK && WITH_AFB_TRACE
#  undef WITH_AFB_TRACE
#  define WITH_AFB_TRACE 0
#endif

#define WITH_TLS (WITH_GNUTLS || WITH_MBEDTLS)
