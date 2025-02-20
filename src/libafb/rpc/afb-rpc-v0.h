/*
 * Copyright (C) 2015-2025 IoT.bzh Company
 * Author: José Bollo <jose.bollo@iot.bzh>
 * Author: johann Gautier <johann.gautier@iot.bzh>
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

#include <stdint.h>

struct afb_rpc_coder;
struct afb_rpc_decoder;

#define AFBRPC_PROTO_VERSION_UNSET	0
#define AFBRPC_PROTO_VERSION_1		1
#define AFBRPC_PROTO_VERSION_2		2
#define AFBRPC_PROTO_VERSION_3		3

#define AFBRPC_PROTO_VERSION_MIN	AFBRPC_PROTO_VERSION_1
#define AFBRPC_PROTO_VERSION_MAX	AFBRPC_PROTO_VERSION_3

enum afb_rpc_v0_msg_type {
	afb_rpc_v0_msg_type_NONE,
	afb_rpc_v0_msg_type_version_offer,
	afb_rpc_v0_msg_type_version_set
};

typedef enum afb_rpc_v0_msg_type afb_rpc_v0_msg_type_t;

struct afb_rpc_v0_msg {
	afb_rpc_v0_msg_type_t type;
	union {
		struct {
			uint8_t count;
			const uint8_t *versions;
		} version_offer;
		struct {
			uint8_t version;
		} version_set;
	};
};

typedef struct afb_rpc_v0_msg afb_rpc_v0_msg_t;

extern int afb_rpc_v0_code_version_offer(struct afb_rpc_coder *coder, uint8_t count, const uint8_t *versions);
extern int afb_rpc_v0_code_version_offer_v1(struct afb_rpc_coder *coder);
extern int afb_rpc_v0_code_version_offer_v3(struct afb_rpc_coder *coder);
extern int afb_rpc_v0_code_version_offer_v1_or_v3(struct afb_rpc_coder *rpc);
extern int afb_rpc_v0_code_version_set(struct afb_rpc_coder *coder, uint8_t version);
extern int afb_rpc_v0_code_version_set_v1(struct afb_rpc_coder *coder);
extern int afb_rpc_v0_code_version_set_v3(struct afb_rpc_coder *coder);

extern int afb_rpc_v0_code(struct afb_rpc_coder *coder, afb_rpc_v0_msg_t *msg);

extern int afb_rpc_v0_decode(struct afb_rpc_decoder *decoder, afb_rpc_v0_msg_t *msg);
