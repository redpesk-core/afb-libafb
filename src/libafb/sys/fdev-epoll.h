/*
 * Copyright (C) 2015-2020 IoT.bzh Company
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

#if WITH_FDEV_EPOLL && WITH_EPOLL

struct fdev;
struct fdev_epoll;

extern struct fdev_epoll *fdev_epoll_create();
extern void fdev_epoll_destroy(struct fdev_epoll *fdev_epoll);
extern int fdev_epoll_fd(struct fdev_epoll *fdev_epoll);
extern struct fdev *fdev_epoll_add(struct fdev_epoll *fdev_epoll, int fd);
extern int fdev_epoll_wait_and_dispatch(struct fdev_epoll *fdev_epoll, int timeout_ms);
extern int fdev_epoll_get_epoll_fd(struct fdev_epoll *fdev_epoll);

#endif
