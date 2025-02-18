/*
 Copyright (C) 2015-2025 IoT.bzh Company

 Author: José Bollo <jose.bollo@iot.bzh>

 $RP_BEGIN_LICENSE$
 Commercial License Usage
  Licensees holding valid commercial IoT.bzh licenses may use this file in
  accordance with the commercial license agreement provided with the
  Software or, alternatively, in accordance with the terms contained in
  a written agreement between you and The IoT.bzh Company. For licensing terms
  and conditions see https://www.iot.bzh/terms-conditions. For further
  information use the contact form at https://www.iot.bzh/contact.

 GNU General Public License Usage
  Alternatively, this file may be used under the terms of the GNU General
  Public license version 3. This license is as published by the Free Software
  Foundation and appearing in the file LICENSE.GPLv3 included in the packaging
  of this file. Please review the following information to ensure the GNU
  General Public License requirements will be met
  https://www.gnu.org/licenses/gpl-3.0.html.
 $RP_END_LICENSE$
*/

#pragma once

#include "../libafb-config.h"

#include <stddef.h>

struct afb_vcomm_itf
{
	int (*close)(void *closure);
	int (*get_tx_buffer)(void *closure, void **data, size_t size);
	int (*drop_tx_buffer)(void *closure, void *data);
	int (*send_nocopy)(void *closure, void *data, size_t size);
	int (*send)(void *closure, const void *data, size_t size);
	int (*hold_rx_buffer)(void *closure, const void *data);
	int (*release_rx_buffer)(void *closure, const void *data);
	int (*on_message)(void *closure, void (*callback)(void *priv, const void *data, size_t size), void *priv);
};

struct afb_vcomm
{
	void *closure;
	struct afb_vcomm_itf itf;
};

static inline
int afb_vcomm_get_tx_buffer(const struct afb_vcomm *vcomm, void **data, size_t size)
{
	return vcomm->itf.get_tx_buffer(vcomm->closure, data, size);
}

static inline
int afb_vcomm_drop_tx_buffer(const struct afb_vcomm *vcomm, void *data)
{
	return vcomm->itf.drop_tx_buffer(vcomm->closure, data);
}

static inline
int afb_vcomm_send_nocopy(const struct afb_vcomm *vcomm, void *data, size_t size)
{
	return vcomm->itf.send_nocopy(vcomm->closure, data, size);
}

static inline
int afb_vcomm_send(const struct afb_vcomm *vcomm, const void *data, size_t size)
{
	return vcomm->itf.send(vcomm->closure, data, size);
}

static inline
int afb_vcomm_hold_rx_buffer(const struct afb_vcomm *vcomm, const void *data)
{
	return vcomm->itf.hold_rx_buffer(vcomm->closure, data);
}

static inline
int afb_vcomm_release_rx_buffer(const struct afb_vcomm *vcomm, const void *data)
{
	return vcomm->itf.release_rx_buffer(vcomm->closure, data);
}

static inline
int afb_vcomm_on_message(const struct afb_vcomm *vcomm, void (*callback)(void *closure, const void *data, size_t size), void *closure)
{
	return vcomm->itf.on_message(vcomm->closure, callback, closure);
}

static inline
int afb_vcomm_close(const struct afb_vcomm *vcomm)
{
	return vcomm->itf.close(vcomm->closure);
}
