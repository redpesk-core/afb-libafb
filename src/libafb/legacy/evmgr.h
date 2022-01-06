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

#include <libafb/libafb-config.h>


struct evmgr;
struct fdev;

extern void evmgr_prepare_run(struct evmgr *evmgr);
extern int evmgr_run(struct evmgr *evmgr, int timeout_ms);
extern void evmgr_job_run(int signum, struct evmgr *evmgr);
extern int evmgr_can_run(struct evmgr *evmgr);
extern void evmgr_wakeup(struct evmgr *evmgr);
extern void *evmgr_try_change_holder(struct evmgr *evmgr, void *holder, void *next);
extern void *evmgr_holder(struct evmgr *evmgr);
extern int evmgr_create(struct evmgr **result);
extern int evmgr_add(struct fdev **fdev, struct evmgr *evmgr, int fd);

#if WITH_FDEV_EPOLL
extern int evmgr_get_epoll_fd(struct evmgr *evmgr);
#endif
