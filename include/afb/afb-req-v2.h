/*
 * Copyright (C) 2015-2020 IoT.bzh Company
 * Author: José Bollo <jose.bollo@iot.bzh>
 *
 * SPDX-License-Identifier: LGPL-3.0-only
 */

#pragma once

#include "afb-req-x1.h"

/** @addtogroup AFB_REQ
 *  @{ */

/**
 * @deprecated use bindings version 3
 *
 * Stores 'req' on heap for asynchrnous use.
 * Returns a handler to the stored 'req' or NULL on memory depletion.
 * The count of reference to 'req' is incremented on success
 * (see afb_req_addref).
 */
static inline struct afb_stored_req *afb_req_x1_store_v2(struct afb_req_x1 req)
{
	return req.itf->legacy_store_req(req.closure);
}


/** @} */
