/*
 * Copyright (C) 2015-2020 IoT.bzh Company
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

/*****************************************************************************/

/**
 * Values of opkind are used to distinguish the kind
 * of operation in the structure opdesc.
 */
enum opkind
{
	/** describes an operation of conversion to an other type */
	Convert_To,

	/** describes an operation of conversion from an other type */
	Convert_From,

	/** describe an operation of update to some type */
	Update_To,

	/** describe an operation of update from some type */
	Update_From
};

/**
 * Description of operation associated to types.
 */
struct opdesc
{
	/** kind of the operation descibed: family, convert or update */
	enum opkind kind;

	/** target type if convert or update or fimly type */
	struct afb_type *type;

	/** link to the next operation for the same type */
	struct opdesc *next;

	union {
		/** converter function if kind is convert */
		afb_type_converter_t converter;

		/** updater function if kind is update */
		afb_type_updater_t updater;

		/** any */
		void *callback;
	};

	/** closure to converter or updater */
	void *closure;
};

/**
 * Main structure describing a type
 */
struct afb_type
{
	/** name */
	const char *name;

	/** link to next type */
	struct afb_type *next;

	/** operations */
	struct opdesc *operations;

	/** link to direct ancestor of family */
	struct afb_type *family;

	/** flags */
	uint16_t flags;
};

/*****************************************************************************/

#define FLAG_IS_SHAREABLE        1
#define FLAG_IS_STREAMABLE       2
#define FLAG_IS_OPAQUE           4
#define FLAG_IS_PREDEFINED       8

#define INITIAL_FLAGS            0

#define TEST_FLAGS(type,flag)    (__atomic_load_n(&((type)->flags), __ATOMIC_RELAXED) & (flag))
#define SET_FLAGS(type,flag)     (__atomic_or_fetch(&((type)->flags), flag, __ATOMIC_RELAXED))
#define UNSET_FLAGS(type,flag)   (__atomic_and_fetch(&((type)->flags), ~flag, __ATOMIC_RELAXED))

#define IS_SHAREABLE(type)       TEST_FLAGS(type,FLAG_IS_SHAREABLE)
#define SET_SHAREABLE(type)      SET_FLAGS(type,FLAG_IS_SHAREABLE)
#define UNSET_SHAREABLE(type)    UNSET_FLAGS(type,FLAG_IS_SHAREABLE)

#define IS_STREAMABLE(type)      TEST_FLAGS(type,FLAG_IS_STREAMABLE)
#define SET_STREAMABLE(type)     SET_FLAGS(type,FLAG_IS_STREAMABLE)
#define UNSET_STREAMABLE(type)   UNSET_FLAGS(type,FLAG_IS_STREAMABLE)

#define IS_OPAQUE(type)          TEST_FLAGS(type,FLAG_IS_OPAQUE)
#define SET_OPAQUE(type)         SET_FLAGS(type,FLAG_IS_OPAQUE)
#define UNSET_OPAQUE(type)       UNSET_FLAGS(type,FLAG_IS_OPAQUE)

#define IS_PREDEFINED(type)      TEST_FLAGS(type,FLAG_IS_PREDEFINED)
#define SET_PREDEFINED(type)     SET_FLAGS(type,FLAG_IS_PREDEFINED)
#define UNSET_PREDEFINED(type)   UNSET_FLAGS(type,FLAG_IS_PREDEFINED)

/*****************************************************************************/

extern struct afb_type _afb_type_head_of_predefineds_;
