/*
 * Copyright (C) 2015-2024 IoT.bzh Company
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


typedef struct afb_rpc_coder afb_rpc_coder_t;
typedef struct afb_rpc_decoder afb_rpc_decoder_t;

/**
 * Receive as much as data as possible
 *
 * @param sockfd the i/o socket
 * @param decoder the decoder object to init from receive
 *
 * @return the count of buffer waiting on success or a negative error code
 */
extern int afb_rpc_sock_recv_decoder(int sockfd, afb_rpc_decoder_t *decoder);

/**
 * Send the coded buffers to the socket
 *
 * @param sockfd the i/o socket
 * @param coder the coder object to send
 *
 * @return 0 success or a negative error code
 */
extern int afb_rpc_sock_send_coder(int sockfd, afb_rpc_coder_t *coder);
