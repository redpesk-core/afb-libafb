/*
 * Copyright (C) 2015-2020 IoT.bzh Company
 * Author: José Bollo <jose.bollo@iot.bzh>
 *
 * SPDX-License-Identifier: LGPL-3.0-only
 */

#pragma once

/** @defgroup AFB_AUTH
 *  @{ */


/**
 * Enumeration  for authority (Session/Token/Assurance) definitions.
 *
 * @see afb_auth
 */
enum afb_auth_type
{
	/** never authorized, no data */
	afb_auth_No = 0,

	/** authorized if token valid, no data */
	afb_auth_Token,

	/** authorized if LOA greater than or equal to data 'loa' */
	afb_auth_LOA,

	/** authorized if permission 'text' is granted */
	afb_auth_Permission,

	/** authorized if 'first' or 'next' is authorized */
	afb_auth_Or,

	/** authorized if 'first' and 'next' are authorized */
	afb_auth_And,

	/** authorized if 'first' is not authorized */
	afb_auth_Not,

	/** always authorized, no data */
	afb_auth_Yes
};

/**
 * Definition of an authorization entry
 */
struct afb_auth
{
	/** type of entry @see afb_auth_type */
	enum afb_auth_type type;

	union {
		/** text when @ref type == @ref afb_auth_Permission */
		const char *text;

		/** level of assurancy when @ref type ==  @ref afb_auth_LOA */
		unsigned loa;

		/** first child when @ref type in { @ref afb_auth_Or, @ref afb_auth_And, @ref afb_auth_Not } */
		const struct afb_auth *first;
	};

	/** second child when @ref type in { @ref afb_auth_Or, @ref afb_auth_And } */
	const struct afb_auth *next;
};

/** @} */
