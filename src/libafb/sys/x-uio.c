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

#include "../libafb-config.h"

#if !WITH_SYS_UIO

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "sys/x-uio.h"

/* TODO: optimize by using optional tiny buffer */

ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{
	ssize_t result, rc;
	size_t size;
	void *base;

	result = 0;
	while (iovcnt) {
		base = iov->iov_base;
		size = iov->iov_len;
		while (size) {
			do {
				rc = write(fd, base, size);
			} while (rc < 0 && errno == EINTR);
			if (rc <= 0)
				return result ? result : rc;
			result += (size_t)rc;
			size -= (size_t)rc;
			base = ((char*)base) + (size_t)rc;
		}
		iovcnt--;
		iov++;
	}
	return result;
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
	ssize_t result, rc;
	size_t size;
	void *base;

	result = 0;
	while (iovcnt) {
		base = iov->iov_base;
		size = iov->iov_len;
		while (size) {
			do {
				rc = read(fd, base, size);
			} while (rc < 0 && errno == EINTR);
			if (rc <= 0)
				return result ? result : rc;
			result += (size_t)rc;
			size -= (size_t)rc;
			base = ((char*)base) + (size_t)rc;
		}
		iovcnt--;
		iov++;
	}
	return result;
}

#endif
