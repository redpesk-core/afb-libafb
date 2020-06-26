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

#include <pthread.h>

#define x_rwlock_t                     pthread_rwlock_t
#define x_rwlock_init(prwlock)         pthread_rwlock_init(prwlock,NULL)
#define x_rwlock_destroy(prwlock)      pthread_rwlock_destroy(prwlock)
#define x_rwlock_rdlock(prwlock)       pthread_rwlock_rdlock(prwlock)
#define x_rwlock_wrlock(prwlock)       pthread_rwlock_wrlock(prwlock)
#define x_rwlock_trywrlock(prwlock)    pthread_rwlock_trywrlock(prwlock)
#define x_rwlock_unlock(prwlock)       pthread_rwlock_unlock(prwlock)
#define X_RWLOCK_INITIALIZER           PTHREAD_RWLOCK_INITIALIZER
