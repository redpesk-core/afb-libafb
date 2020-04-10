/*
 * Copyright (C) 2015-2020 IoT.bzh Company
 * Author: José Bollo <jose.bollo@iot.bzh>
 *
 * SPDX-License-Identifier: LGPL-3.0-only
 */

#pragma once

/** @addtogroup AFB_REQ
 *  @{ */

/**
 * Describes an argument (or parameter) of a request.
 *
 * @see afb_req_get
 */
struct afb_arg
{
	const char *name;	/**< name of the argument or NULL if invalid */
	const char *value;	/**< string representation of the value of the argument */
				/**< original filename of the argument if path != NULL */
	const char *path;	/**< if not NULL, path of the received file for the argument */
				/**< when the request is finalized this file is removed */
};


/** @} */
