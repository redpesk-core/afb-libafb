/*
 * Copyright (C) 2015-2020 IoT.bzh Company
 * Author: José Bollo <jose.bollo@iot.bzh>
 *
 * SPDX-License-Identifier: LGPL-3.0-only
 */

#pragma once

struct afb_event_x2;
struct afb_event_x2_itf;

/** @addtogroup AFB_EVENT
 *  @{ */

/**
 * Interface for handling event_x2.
 *
 * It records the functions to be called for the event_x2.
 *
 * Don't use this structure directly.
 */
struct afb_event_x2_itf
{
	/* CAUTION: respect the order, add at the end */

	/** broadcast the event */
	int (*broadcast)(struct afb_event_x2 *event, struct json_object *obj);

	/** push the event to its subscribers */
	int (*push)(struct afb_event_x2 *event, struct json_object *obj);

	/** unreference the event */
	void (*unref)(struct afb_event_x2 *event); /* aka drop */

	/** get the event name */
	const char *(*name)(struct afb_event_x2 *event);

	/** rereference the event */
	struct afb_event_x2 *(*addref)(struct afb_event_x2 *event);
};

/**
 * Describes the event_x2
 */
struct afb_event_x2
{
	const struct afb_event_x2_itf *itf;	/**< the interface functions to use */
};

/** @} */
