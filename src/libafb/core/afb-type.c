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

#include "libafb-config.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

#include "afb-type.h"
#include "afb-data.h"

#include "sys/x-errno.h"
#include "sys/x-rwlock.h"
#include "utils/jsonstr.h"

/*****************************************************************************/

/**
 * Values of opkind are used to distinguish the kind
 * of operation in the structure opdesc.
 */
enum opkind
{
	/** describes an operation of conversion to an other type */
	Convert,

	/** describe an operation of update to some type */
	Update,

	/** set the family hierachy */
	Family
};

/**
 * Description of operation associated to types.
 */
struct opdesc
{
	/** link to the next operation for the same type */
	struct opdesc *next;

	/** kind of the operation descibed: family, convert or update */
	enum opkind kind;

	/** target type if convert or update or fimly type */
	struct afb_type *type;

	/** closure to converter or updater */
	void *closure;

	union {
		/** converter function if kind is convert */
		afb_type_converter_t converter;

		/** updater function if kind is update */
		afb_type_updater_t updater;

		/** any */
		void *callback;
	};
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

	/** flags */
	uint16_t flags;
};

/*****************************************************************************/

#define FLAG_IS_SHAREABLE        1
#define FLAG_IS_STREAMABLE       2
#define FLAG_IS_OPAQUE           4

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

/*****************************************************************************/

/* the types */
static struct afb_type *known_types = &afb_type_predefined_json_c;

/*****************************************************************************/

#if defined(AFB_TYPE_NO_LOCK)

/** takes the read lock */
static inline void lock_read() {}

/** takes the write lock */
static inline void lock_write() {}

/** unlock the gotten lock */
static inline void unlock() {}

#else

static x_rwlock_t  rwlock = X_RWLOCK_INITIALIZER;

/** takes the read lock */
static inline void lock_read() { x_rwlock_rdlock(&rwlock); }

/** takes the write lock */
static inline void lock_write() { x_rwlock_wrlock(&rwlock); }

/** unlock the gotten lock */
static inline void unlock() { x_rwlock_unlock(&rwlock); }

#endif

/*****************************************************************************/

static struct afb_type *search_type_locked(const char *name)
{
	struct afb_type *type = known_types;
	while (type && strcmp(name, type->name))
		type = type->next;
	return type;
}


int afb_type_register(struct afb_type **result, const char *name, int streamable, int shareable, int opaque)
{
	int rc;
	struct afb_type *type;

	lock_write();
	type = search_type_locked(name);
	if (type) {
		type = 0;
		rc = X_EEXIST;
	}
	else {
		type = malloc(sizeof *type);
		if (!type)
			rc = X_ENOMEM;
		else {
			type->name = name;
			if (opaque) {
				SET_OPAQUE(type);
			}
			else if (streamable) {
				SET_STREAMABLE(type);
				SET_SHAREABLE(type);
			}
			else if (shareable) {
				SET_SHAREABLE(type);
			}
			type->operations = 0;
			type->flags = 0;
			type->next = known_types;
			known_types = type;
			rc = 0;
		}
	}
	unlock();
	*result = type;
	return rc;
}

struct afb_type *afb_type_get(const char *name)
{
	struct afb_type *type;
	lock_read();
	type = search_type_locked(name);
	unlock();
	return type;
}

const char *afb_type_name(struct afb_type *type)
{
	return type->name;
}

int afb_type_is_streamable(struct afb_type *type)
{
	return IS_STREAMABLE(type);
}

int afb_type_is_shareable(struct afb_type *type)
{
	return IS_SHAREABLE(type);
}

int afb_type_is_opaque(struct afb_type *type)
{
	return IS_OPAQUE(type);
}

static
int
operate(
	struct afb_type *from_type,
	struct afb_data *from_data,
	struct afb_type *to_type,
	struct afb_data **to_data,
	enum opkind kind
) {
	int rc;
	struct opdesc *odsc;
	struct afb_type *type, *family;

	/* iterate the family */
	type = from_type;
	while (type) {
		family = 0;
		/* inspect operations */
		for (odsc = type->operations; odsc; odsc = odsc->next) {
			if (odsc->kind == Family) {
				/* record the family */
				family = odsc->type;
			}
			else if (odsc->kind == kind && odsc->type == to_type) {
				/* operation for the destination type */
				if (odsc->kind == Convert) {
					/* conversion case */
					rc = odsc->converter(odsc->closure, from_data, to_type, to_data);
					if (rc >= 0) {
						return rc;
					}
				} else {
					/* update case */
					rc = odsc->updater(odsc->closure, from_data, to_type, *to_data);
					if (rc >= 0) {
						return rc;
					}
				}
			}
		}
		/* not found, try an ancestor if one exists */
		type = family;
		if (type == to_type && kind == Convert) {
			/* implicit conversion to an ancestor of the family */
			rc = afb_data_create_raw(to_data, type,
				afb_data_const_pointer(from_data), afb_data_size(from_data),
				(void(*)(void*))afb_data_unref, afb_data_addref(from_data));
			return rc;
		}
	}
	/* no operation found or succesful */
	if (kind == Convert)
		*to_data = 0;
	return X_ENOENT;
}

/* rc<0: error, 0: no conversion (same type), 1: converted */
int
afb_type_update_data(
	struct afb_type *from_type,
	struct afb_data *from_data,
	struct afb_type *to_type,
	struct afb_data *to_data
) {
	return operate(from_type, from_data, to_type, &to_data, Update);
}

int
afb_type_convert_data(
	struct afb_type *from_type,
	struct afb_data *from_data,
	struct afb_type *to_type,
	struct afb_data **to_data
) {
	return operate(from_type, from_data, to_type, to_data, Convert);
}

static
int
add_op(
	struct afb_type *type,
	enum opkind kind,
	struct afb_type *totype,
	void *callback,
	void *closure
) {
	struct opdesc *desc;

	desc = type->operations;
	while (desc && (desc->kind != kind || desc->type != totype)) {
		desc = desc->next;
	}
	if (!desc) {
		desc = malloc(sizeof *desc);
		if (!desc)
			return X_ENOMEM;

		desc->kind = kind;
		desc->type = totype;
		desc->next = type->operations;
		type->operations = desc;
	}
	desc->callback = callback;
	desc->closure = closure;
	return 0;
}

int afb_type_set_family(
	struct afb_type *type,
	struct afb_type *family
) {
	return add_op(type, Family, family, 0, 0);
}

int afb_type_add_converter(
	struct afb_type *type,
	struct afb_type *totype,
	afb_type_converter_t converter,
	void *closure
) {
	return add_op(type, Convert, totype, converter, closure);
}

int afb_type_add_updater(
	struct afb_type *type,
	struct afb_type *totype,
	afb_type_updater_t updater,
	void *closure
) {
	return add_op(type, Update, totype, updater, closure);
}

/*****************************************************************************/
/*****************************************************************************/
/**                                                                         **/
/**                   OPAQUE HELPERS                                        **/
/**                                                                         **/
/*****************************************************************************/
/*****************************************************************************/
#include <stdio.h>

#define OPAQUE_KEY      "afbh@"
#define OPAQUE_FMT_RD   OPAQUE_KEY"%x"
#define OPAQUE_FMT_WR   OPAQUE_KEY"%04x"
#define OPAQUE_BUFSIZE  (sizeof(OPAQUE_KEY) + 4)

static
int
opaque_from_string(
	const char *string,
	char term,
	struct afb_type *type,
	struct afb_data **out
) {
	int rc, p, opaqueid;
	struct afb_data *found;
	struct afb_type *otype;

	rc = sscanf(string, OPAQUE_FMT_RD "%n", &opaqueid, &p);
	if (rc != 1 || string[p] != term) {
		rc = X_EINVAL;
	}
	else {
		rc = afb_data_get_opacified(opaqueid, &found, &otype);
		if (rc >= 0) {
			if (type == afb_data_type(found))
				*out = found;
			else {
				afb_data_unref(found);
				rc = X_ENOENT;
			}
		}
	}
	return rc;
}

static
int
opaque_to_string(
	struct afb_data *in,
	char *buffer,
	size_t size
) {
	int opaqueid, rc;

	opaqueid = afb_data_opacify(in);
	if (opaqueid < 0) {
		rc = opaqueid;
	}
	else {
		rc = snprintf(buffer, size, OPAQUE_FMT_WR, opaqueid);
		if (rc < 0 || rc >= size) {
			rc = X_EFAULT;
		}
	}
	return rc;
}

/*****************************************************************************/
/*****************************************************************************/
/**                                                                         **/
/**                   PREDEFINED TYPES                                      **/
/**                                                                         **/
/*****************************************************************************/
/*****************************************************************************/

#include <json-c/json.h>
#if !defined(JSON_C_TO_STRING_NOSLASHESCAPE)
#define JSON_C_TO_STRING_NOSLASHESCAPE 0
#endif

/*****************************************************************************/
/* PREDEFINED OPAQUE */

static int convert_opaque_to_stringz(void*,struct afb_data*,struct afb_type*,struct afb_data**);
static int convert_opaque_to_json_string(void*,struct afb_data*,struct afb_type*,struct afb_data**);
static int convert_opaque_to_json_c(void*,struct afb_data*,struct afb_type*,struct afb_data**);

static struct opdesc opcvt_opaque_to_stringz =
	{
		.next = 0,
		.kind = Convert,
		.type = &afb_type_predefined_stringz,
		.closure = 0,
		.converter = convert_opaque_to_stringz
	};

static struct opdesc opcvt_opaque_to_json_string =
	{
		.next = &opcvt_opaque_to_stringz,
		.kind = Convert,
		.type = &afb_type_predefined_json,
		.closure = 0,
		.converter = convert_opaque_to_json_string
	};

static struct opdesc opcvt_opaque_to_json_c =
	{
		.next = &opcvt_opaque_to_json_string,
		.kind = Convert,
		.type = &afb_type_predefined_json_c,
		.closure = 0,
		.converter = convert_opaque_to_json_c
	};

struct afb_type afb_type_predefined_opaque =
	{
		.name = "afb:OPAQUE",
		.next = 0,
		.operations = &opcvt_opaque_to_json_c,
		.flags = FLAG_IS_OPAQUE
	};

static int convert_opaque_to_stringz(
	void *unused,
	struct afb_data *in,
	struct afb_type *type,
	struct afb_data **out
) {
	int rc;
	char buffer[OPAQUE_BUFSIZE], *s;
	unsigned sz;

	rc = opaque_to_string(in, buffer, sizeof buffer);
	if (rc >= 0) {
		sz = (size_t)(rc + 1);
		s = malloc(sz);
		if (s == NULL) {
			rc = X_ENOMEM;
		}
		else {
			rc = afb_data_create_raw(out, type, s, sz, free, s);
			if (rc >= 0)
				memcpy(s, buffer, sz);
		}
	}
	return rc;
}

static int convert_opaque_to_json_string(
	void *unused,
	struct afb_data *in,
	struct afb_type *type,
	struct afb_data **out
) {
	int rc;
	char buffer[OPAQUE_BUFSIZE], *s;
	unsigned sz;

	rc = opaque_to_string(in, buffer, sizeof buffer);
	if (rc >= 0) {
		sz = (size_t)(rc + 3);
		s = malloc(sz);
		if (s == NULL) {
			rc = X_ENOMEM;
		}
		else {
			rc = afb_data_create_raw(out, type, s, sz, free, s);
			if (rc >= 0) {
				s[--sz] = 0;
				s[--sz] = s[0] = '"';
				memcpy(s + 1, buffer, --sz);
			}
		}
	}
	return rc;
}

static int convert_opaque_to_json_c(
	void *unused,
	struct afb_data *in,
	struct afb_type *type,
	struct afb_data **out
) {
	int rc;
	char buffer[OPAQUE_BUFSIZE];
	json_object *json;

	rc = opaque_to_string(in, buffer, sizeof buffer);
	if (rc >= 0) {
		json = json_object_new_string(buffer);
		if (json == NULL)
			rc = X_ENOMEM;
		else
			rc = afb_data_create_raw(out, type, json, 0, (void*)json_object_put, json);
	}
	return rc;
}


/*****************************************************************************/
/* PREDEFINED STRINGZ */

static int convert_stringz_to_opaque(void*,struct afb_data*,struct afb_type*,struct afb_data**);
static int convert_stringz_to_json_string(void*,struct afb_data*,struct afb_type*,struct afb_data**);

static struct opdesc opcvt_stringz_to_opaque =
	{
		.next = 0,
		.kind = Convert,
		.type = &afb_type_predefined_opaque,
		.closure = 0,
		.converter = convert_stringz_to_opaque
	};

static struct opdesc opcvt_stringz_to_json_string =
	{
		.next = &opcvt_stringz_to_opaque,
		.kind = Convert,
		.type = &afb_type_predefined_json,
		.closure = 0,
		.converter = convert_stringz_to_json_string
	};

struct afb_type afb_type_predefined_stringz =
	{
		.name = "afb:STRINGZ",
		.next = &afb_type_predefined_opaque,
		.operations = &opcvt_stringz_to_json_string,
		.flags = FLAG_IS_STREAMABLE
	};

static int convert_stringz_to_opaque(
	void *unused,
	struct afb_data *in,
	struct afb_type *type,
	struct afb_data **out
) {
	int rc;
	const char *s;

	s = afb_data_const_pointer(in);
	rc = opaque_from_string(s, 0, type, out);
	return rc;
}

static int convert_stringz_to_json_string(
	void *unused,
	struct afb_data *in,
	struct afb_type *type,
	struct afb_data **out
) {
	const char *istr;
	char *ostr;
	size_t isz, osz;

	/* null is still null */
	istr = afb_data_const_pointer(in);
	if (!istr)
		return afb_data_create_raw(out, type, "null", 5, 0, 0);

	/* empty case is optimized */
	isz = afb_data_size(in);
	if (isz <= 1)
		return afb_data_create_raw(out, type, "\"\"", 3, 0, 0);

	/* compute escaped size and allocate it */
	osz = jsonstr_string_escape_length(istr, isz - 1);
	ostr = malloc(3 + osz);
	if (!ostr)
		return X_ENOMEM;

	/* make the json string */
	ostr[0] = '"';
	jsonstr_string_escape_unsafe(&ostr[1], istr, isz - 1);
	ostr[osz + 1] = '"';
	ostr[osz + 2] = 0;

	/* create the data */
	return afb_data_create_raw(out, type, ostr, 3 + osz, free, ostr);
}


/*****************************************************************************/
/* PREDEFINED JSON */

static int convert_json_string_to_opaque(void*,struct afb_data*,struct afb_type*,struct afb_data**);
static int convert_json_string_to_json_c(void*,struct afb_data*,struct afb_type*,struct afb_data**);


static struct opdesc opfamily_json_string =
	{
		.next = 0,
		.kind = Family,
		.type = &afb_type_predefined_stringz
	};

static struct opdesc opcvt_json_string_to_opaque =
	{
		.next = &opfamily_json_string,
		.kind = Convert,
		.type = &afb_type_predefined_opaque,
		.converter = convert_json_string_to_opaque
	};

static struct opdesc opcvt_json_string_to_json_c =
	{
		.next = &opcvt_json_string_to_opaque,
		.kind = Convert,
		.type = &afb_type_predefined_json_c,
		.converter = convert_json_string_to_json_c
	};

struct afb_type afb_type_predefined_json =
	{
		.name = "afb:JSON",
		.next = &afb_type_predefined_stringz,
		.operations = &opcvt_json_string_to_json_c,
		.flags = FLAG_IS_STREAMABLE
	};

/**
 * conversion from data of type JSON to data of type JSON-C
 */
static int convert_json_string_to_json_c(
	void *unused,
	struct afb_data *in,
	struct afb_type *type,
	struct afb_data **out
) {
	int rc;
	json_object *json;
	enum json_tokener_error jerr;
	const char *str;

	str = afb_data_const_pointer(in);
	json = json_tokener_parse_verbose(str, &jerr);
	if (jerr != json_tokener_success)
		json = json_object_new_string(str);
	rc = afb_data_create_raw(out, type, json, 0, (void*)json_object_put, json);
	return rc;
}

static int convert_json_string_to_opaque(
	void *unused,
	struct afb_data *in,
	struct afb_type *type,
	struct afb_data **out
) {
	int rc;
	const char *s;

	s = afb_data_const_pointer(in);
	if (*s++ != '"')
		rc = X_EINVAL;
	else
		rc = opaque_from_string(s, '"', type, out);
	return rc;
}


/*****************************************************************************/
/* PREDEFINED JSON-C */

static int convert_json_c_to_json_string(void*,struct afb_data*,struct afb_type*,struct afb_data**);
static int convert_json_c_to_opaque(void*,struct afb_data*,struct afb_type*,struct afb_data**);

extern struct afb_type afb_type_predefined_json_c;

static struct opdesc opcvt_json_c_to_opaque =
	{
		.next = 0,
		.kind = Convert,
		.type = &afb_type_predefined_opaque,
		.converter = convert_json_c_to_opaque
	};

static struct opdesc opcvt_json_c_to_json_string =
	{
		.next = &opcvt_json_c_to_opaque,
		.kind = Convert,
		.type = &afb_type_predefined_json,
		.converter = convert_json_c_to_json_string
	};

struct afb_type afb_type_predefined_json_c =
	{
		.name = "afb:JSON-C",
		.next = &afb_type_predefined_json,
		.operations = &opcvt_json_c_to_json_string,
		.flags = 0
	};

static int convert_json_c_to_json_string(
	void *unused,
	struct afb_data *in,
	struct afb_type *type,
	struct afb_data **out
) {
	struct json_object *object;
	const char *jsonstr;
	size_t sz;
	int rc;

	object = (struct json_object *)afb_data_const_pointer(in);
#if JSON_C_VERSION_NUM >= 0x000D00
	jsonstr = json_object_to_json_string_length(object, JSON_C_TO_STRING_NOSLASHESCAPE, &sz);
#else
	jsonstr = json_object_to_json_string_ext(object, JSON_C_TO_STRING_NOSLASHESCAPE);
	sz = strlen(jsonstr);
#endif
	if (jsonstr == NULL)
		rc = X_ENOMEM;
	else {
		json_object_get(object);
		rc = afb_data_create_raw(out, type, (void*)jsonstr, sz + 1, (void*)json_object_put, object);
		if (rc < 0)
			json_object_put(object);
	}
	return rc;
}

static int convert_json_c_to_opaque(
	void *unused,
	struct afb_data *in,
	struct afb_type *type,
	struct afb_data **out
) {
	int rc;
	struct json_object *object;
	const char *s;

	object = (struct json_object *)afb_data_const_pointer(in);
	if (!json_object_is_type(object, json_type_string))
		rc = X_EINVAL;
	else {
		s = json_object_get_string(object);
		rc = opaque_from_string(s, 0, type, out);
	}
	return rc;
}

