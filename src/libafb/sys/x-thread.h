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
#include "../sys/x-errno.h"

#define x_thread_t	pthread_t

typedef void (*x_thread_cb)(void* arg);

static inline int x_thread_create(
			x_thread_t *tid,
                        x_thread_cb entry,
			void *arg)
{
	return pthread_create(tid, NULL, (void*)entry, arg) ? -errno : 0;
}

#if WITH_THREAD_LOCAL
#define X_TLS(type,name) \
	static _Thread_local type *__tls_##name;\
	static inline type *x_tls_get_##name()\
		{ return __tls_##name; } \
	static inline void x_tls_set_##name(type *value)\
		{ __tls_##name = value; }
#else
#define X_TLS(type,name) \
	static inline pthread_key_t __tls_key_##name()\
		{\
			static pthread_key_t key = 0;\
			if (!key)\
				pthread_key_create(&key, NULL);\
			return key;\
		}\
	static inline type *x_tls_get_##name()\
		{ return (type*)pthread_getspecific(__tls_key_##name()); }\
	static inline void x_tls_set_##name(type *value)\
		{ pthread_setspecific(__tls_key_##name(),value); }
#endif
