/*
 * Copyright (C) 2015-2020 IoT.bzh Company
 * Author: José Bollo <jose.bollo@iot.bzh>
 *
 * SPDX-License-Identifier: LGPL-3.0-only
 */

#pragma once

struct afb_api_x3;

/** @defgroup AFB_SERVICE
 *  @{ */

/**
 * @deprecated use bindings version 3
 *
 * Interface for internal of services
 * It records the functions to be called for the request.
 * Don't use this structure directly.
 * Use the helper functions documented below.
 */
struct afb_service_itf_x1
{
	/* CAUTION: respect the order, add at the end */

	void (*call)(struct afb_api_x3 *closure, const char *api, const char *verb, struct json_object *args,
	             void (*callback)(void*, int, struct json_object*), void *callback_closure);

	int (*call_sync)(struct afb_api_x3 *closure, const char *api, const char *verb, struct json_object *args,
	                 struct json_object **result);
};

/**
 * @deprecated use bindings version 3
 *
 * Object that encapsulate accesses to service items
 */
struct afb_service_x1
{
	const struct afb_service_itf_x1 *itf;
	struct afb_api_x3 *closure;
};

/** @} */
