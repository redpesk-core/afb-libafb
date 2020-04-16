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

#include "libafb-config.h"

#include <stdlib.h>
#include <unistd.h>

#include "misc/afb-fdev.h"
#include "misc/afb-socket.h"

#include "sys/fdev.h"
#include "sys/verbose.h"


/**
 * open socket for client or server
 *
 * @param uri the specification of the socket
 * @param server 0 for client, server otherwise
 * @param scheme the default scheme to use if none is set in uri (can be NULL)
 *
 * @return the fdev of the socket or NULL in case of error
 */
struct fdev *afb_socket_fdev_open_scheme(const char *uri, int server, const char *scheme)
{
	struct fdev *fdev;
	int fd;

	fd = afb_socket_open_scheme(uri, server, scheme);
	if (fd < 0)
		fdev = NULL;
	else {
		fdev = afb_fdev_create(fd);
		if (!fdev) {
			close(fd);
			ERROR("can't make %s socket for %s", server ? "server" : "client", uri);
		}
	}
	return fdev;
}

