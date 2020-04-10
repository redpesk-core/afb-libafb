/*
 * Copyright (C) 2015-2020 IoT.bzh Company
 * Author: José Bollo <jose.bollo@iot.bzh>
 *
 * SPDX-License-Identifier: LGPL-3.0-only
 */

#pragma once

#include "afb-event-x2-itf.h"

/** @addtogroup AFB_EVENT
 *  @{ */

/**
 * @deprecated use bindings version 3
 *
 * Describes the request of afb-daemon for bindings
 */
struct afb_event_x1
{
	const struct afb_event_x2_itf *itf;	/**< the interface to use */
	struct afb_event_x2 *closure;		/**< the closure argument for functions of 'itf' */
};

/** @} */
