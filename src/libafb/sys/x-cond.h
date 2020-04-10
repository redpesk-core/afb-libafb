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

#include "sys/x-mutex.h"

#include <pthread.h>

#define x_cond_t                  pthread_cond_t
#define x_cond_init(pcond)        pthread_cond_init(pcond,NULL)
#define x_cond_destroy(pcond)     pthread_cond_destroy(pcond)
#define x_cond_wait(pcond,pmutex) pthread_cond_wait(pcond,pmutex)
#define x_cond_signal(pcond)      pthread_cond_signal(pcond)
#define x_cond_broadcast(pcond)   pthread_cond_broadcast(pcond)
#define X_COND_INITIALIZER        PTHREAD_COND_INITIALIZER

