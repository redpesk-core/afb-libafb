/*
 * Copyright (C) 2015-2022 IoT.bzh Company
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

#include "../sys/x-epoll.h"

struct fdev;

extern struct fdev *fdev_addref(struct fdev *fdev);
extern void fdev_unref(struct fdev *fdev);

extern int fdev_fd(const struct fdev *fdev);
extern uint32_t fdev_events(const struct fdev *fdev);
extern int fdev_autoclose(const struct fdev *fdev);

extern void fdev_set_callback(struct fdev *fdev, void (*callback)(void*,uint32_t,struct fdev*), void *closure);
extern void fdev_set_events(struct fdev *fdev, uint32_t events);
extern void fdev_set_autoclose(struct fdev *fdev, int autoclose);
