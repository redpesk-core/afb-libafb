/*
 * Copyright (C) 2015-2025 IoT.bzh Company
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

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "afb-rpc-coder.h"
#include "afb-rpc-decoder.h"

#if __ZEPHYR__
#  define MSG_CMSG_CLOEXEC 0
#endif

/** receive as much as data as possible */
int afb_rpc_sock_recv_decoder(int sockfd, afb_rpc_decoder_t *decoder)
{
	struct iovec iovec;
	ssize_t ssz;
	struct msghdr msghdr;
	union { char control[128]; struct cmsghdr align; } u;

	/* prepare the header */
	iovec.iov_base = (void*)decoder->pointer; /* remove const */
	iovec.iov_len = decoder->size;
	msghdr.msg_iov = &iovec;
	msghdr.msg_name = 0;
	msghdr.msg_namelen = 0;
	msghdr.msg_iovlen = 1;
	msghdr.msg_control = u.control;
	msghdr.msg_controllen = sizeof u.control;
	msghdr.msg_flags = 0;

	/* receive data */
	ssz = recvmsg(sockfd, &msghdr, MSG_CMSG_CLOEXEC|MSG_DONTWAIT);
	if (ssz < 0)
		return -1;

	return (int)ssz;
}

/** put the received buffers in rpc */
int afb_rpc_sock_send_coder(int sockfd, afb_rpc_coder_t *coder)
{
	ssize_t ssz;
	int nio;
	struct msghdr msghdr;
	struct iovec iovecs[AFB_RPC_OUTPUT_BUFFER_COUNT_MAX];

	/* get buffers to send */
	nio = afb_rpc_coder_output_get_iovec(coder, iovecs, AFB_RPC_OUTPUT_BUFFER_COUNT_MAX);
	if (nio <= 0)
		return nio;

	/* prepare message header */
	msghdr.msg_iov = iovecs;
	msghdr.msg_name = 0;
	msghdr.msg_namelen = 0;
	msghdr.msg_iovlen = (size_t)nio;
	msghdr.msg_control = 0;
	msghdr.msg_controllen = 0;
	msghdr.msg_flags = 0;

	/* send data */
	ssz = sendmsg(sockfd, &msghdr, MSG_DONTWAIT);
	if (ssz < 0)
		return -1;

	afb_rpc_coder_output_dispose(coder);
	return 0;
}

