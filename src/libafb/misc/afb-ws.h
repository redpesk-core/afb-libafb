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

#include <stddef.h>
#include <stdint.h>

struct afb_ws;
struct iovec;

struct afb_ws_itf
{
	void (*on_close) (void *, uint16_t code, char *, size_t size); /* optional, if not set hangup is called */
	void (*on_text) (void *, char *, size_t size);
	void (*on_binary) (void *, char *, size_t size);
	void (*on_error) (void *, uint16_t code, const void *, size_t size); /* optional, if not set hangup is called */
	void (*on_hangup) (void *); /* optional, it is safe too call afb_ws_destroy within the callback */
};

extern struct afb_ws *afb_ws_create(int fd, const struct afb_ws_itf *itf, void *closure);
extern void afb_ws_destroy(struct afb_ws *ws);
extern void afb_ws_hangup(struct afb_ws *ws);
extern int afb_ws_is_connected(struct afb_ws *ws);
extern int afb_ws_close(struct afb_ws *ws, uint16_t code, const char *reason);
extern int afb_ws_error(struct afb_ws *ws, uint16_t code, const char *reason);
extern int afb_ws_text(struct afb_ws *ws, const char *text, size_t length);
extern int afb_ws_texts(struct afb_ws *ws, ...);
extern int afb_ws_binary(struct afb_ws *ws, const void *data, size_t length);
extern int afb_ws_text_v(struct afb_ws *ws, const struct iovec *iovec, int count);
extern int afb_ws_binary_v(struct afb_ws *ws, const struct iovec *iovec, int count);

