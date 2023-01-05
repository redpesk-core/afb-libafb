/*
 * Copyright (C) 2015-2023 IoT.bzh Company
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

#include "../libafb-config.h"

#include <pthread.h>
#include <signal.h>
#include "../sys/x-errno.h"

#define x_thread_t	pthread_t

typedef void *(*x_thread_cb)(void* arg);

static inline int x_thread_create(
			x_thread_t *tid,
                        x_thread_cb entry,
			void *arg,
			int detached)
{
	int rc;
	pthread_attr_t attr;

	pthread_attr_init(&attr);
	if (detached)
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	rc = pthread_create(tid, &attr, (void*(*)(void*))entry, arg) ? -errno : 0;
	pthread_attr_destroy(&attr);
	return rc;
}

static inline int x_thread_detach(x_thread_t tid)
{
	return pthread_detach(tid) ? -errno : 0;
}

static inline x_thread_t x_thread_self(void)
{
	return pthread_self();
}

static inline int x_thread_equal(x_thread_t t1, x_thread_t t2)
{
	return pthread_equal(t1, t2);
}

static inline int x_thread_kill(x_thread_t tid, int sig)
{
	return pthread_kill(tid, sig);
}

static inline void x_thread_exit(void *retval)
{
	pthread_exit(retval);
}

static inline int x_thread_join(x_thread_t tid, void **retval)
{
	return pthread_join(tid, retval);
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
