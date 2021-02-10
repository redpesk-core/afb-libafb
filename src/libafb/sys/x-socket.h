/*
 * Copyright (C) 2015-2021 IoT.bzh Company
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

#include <libafb/libafb-config.h>

#if __ZEPHYR__

#include <net/socket.h>

#define socket(x,y,z)  zsock_socket(x,y,z)
#define setsockopt     zsock_setsockopt

#define accept(x,y,z)  zsock_accept(x,y,z)
#define bind(x,y,z)    zsock_bind(x,y,z)
#define connect(x,y,z) zsock_connect(x,y,z)
#define listen(x,y)    zsock_listen(x,y)

#define addrinfo       zsock_addrinfo
#define getaddrinfo    zsock_getaddrinfo
#define freeaddrinfo   zsock_freeaddrinfo
#define AI_PASSIVE     0

#undef WITH_UNIX_SOCKET

#else

#include <sys/socket.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/un.h>

#endif

