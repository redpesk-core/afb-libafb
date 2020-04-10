/*
 * Copyright (C) 2015-2020 IoT.bzh Company
 * Author: José Bollo <jose.bollo@iot.bzh>
 *
 * SPDX-License-Identifier: LGPL-3.0-only
 */

#pragma once

#include "afb-req-x2-itf.h"

/** @addtogroup AFB_REQ
 *  @{ */

/**
 * @deprecated use bindings version 3
 *
 * Describes the request to bindings version 1 and 2
 */
struct afb_req_x1
{
	const struct afb_req_x2_itf *itf;	/**< the interface to use */
	struct afb_req_x2 *closure;		/**< the closure argument for functions of 'itf' */
};


/** @} */
