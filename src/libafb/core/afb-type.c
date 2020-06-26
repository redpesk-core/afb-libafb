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

#include <afb/afb-type-x4.h>
#include <afb/afb-data-x4.h>

#include "afb-type.h"
#include "afb-data.h"

#include "sys/x-errno.h"

#define TYPES_PER_BLOCK 7

struct types;

/**
 * Registration of types
 */
struct types
{
	/** Next block of registered types */
	struct types *next;

	/** The registered types */
	const struct afb_type_x4 *types[TYPES_PER_BLOCK];
};

/* the types */
static struct types types;

/* predefined types (see below) */
static const struct afb_type_x4 pt_STRINGZ;
static const struct afb_type_x4 pt_JSON;
static const struct afb_type_x4 pt_JSON_C;
static
const struct afb_type_x4 *predefineds[] =
{
	[_AFB_TYPENUM_X4_STRINGZ_] = &pt_STRINGZ,
	[_AFB_TYPENUM_X4_JSON_]    = &pt_JSON,
	[_AFB_TYPENUM_X4_JSON_C_]  = &pt_JSON_C
};

/* search/register a name */
static int search(const char *name, const struct afb_type_x4 *type)
{
	struct types *iter;
	const struct afb_type_x4 *t;
	int r;
	int base;
	int i;

	r = 0;
	i = 0;
	base = 1;
	iter = &types;
	for (;;) {
		t = iter->types[i];

		/* at end? */
		if (t == NULL) {
			/* yes at end */
			if (!type)
				return 0;
			iter->types[i] = type;
			return r ? r : i + base;
		}

		/* found the name ? */
		if (0 == strcmp(t->name, name)) {
			if (r == 0)
				r = i + base;
			if (!type || type == t)
				return r;
		}

		/* end of the bloc ? */
		if (++i >= TYPES_PER_BLOCK) {
			/* check if too many types */
			base += TYPES_PER_BLOCK;
			/* has a block after ? */
			if (iter->next == NULL) {
				/* no end of blocks */
				if (!type)
					return 0;
				iter->next = calloc(1, sizeof *iter);
				if (iter->next == NULL) {
					return X_ENOMEM;
				}
			}
			iter = iter->next;
			i = 0;
		}
	}
}

/* get the name */
static const struct afb_type_x4 *get(int id)
{
	struct types *iter;

	/* search the bloc */
	iter = &types;
	while (id > TYPES_PER_BLOCK && iter) {
		id -= TYPES_PER_BLOCK;
		iter = iter->next;
	}

	/* return the value */
	return (id > 0 && iter) ? iter->types[id - 1] : 0;
}

static inline
const struct afb_type_x4*
type_desc(
	const struct afb_type_x4 *type
) {
	unsigned index;

	if (!_AFB_TYPE_X4_IS_PREDEFINED_(type))
		return type;
	index = _AFB_TYPE_X4_PREDEFINED_INDEX_(type);
	if (index < sizeof predefineds / sizeof *predefineds)
		return predefineds[index];
	return 0;
}


/* check if a type is registered */
int afb_type_id_of_name(const char *name)
{
	return search(name, 0);
}

/* register a name */
int afb_type_register_type_x4(const struct afb_type_x4 *type)
{
	return search(type->name, type);
}

/* get the name */
const char *afb_type_name_of_id(int id)
{
	const struct afb_type_x4 *t;

	t = get(id);
	return t ? t->name : NULL;
}

/* check validity */
int afb_type_is_valid_id(int id)
{
	return !!get(id);
}

/* get a type */
const struct afb_type_x4 *afb_type_type_x4_of_id(int id)
{
	return get(id);
}

/* get a type */
const struct afb_type_x4 *afb_type_type_x4_of_name(const char *name)
{
	return get(search(name, 0));
}

struct conversion
{
	afb_type_converter_x4_t converter;
	void *closure;
};

static
int
convert_from_x4_s(
	const struct afb_type_x4 *type,
	const struct afb_type_x4 *from,
	struct conversion *conv
) {
	unsigned n;

	n = type->nconverts;
	while (n) {
		n--;
		if (type->converts[n].convert_from
		 && type->converts[n].type == from) {
			conv->converter = type->converts[n].convert_from;
			conv->closure = type->closure;
			return 1;
		}
	}
	return 0;
}

__attribute__((unused))
static
int
convert_from_x4(
	const struct afb_type_x4 *type,
	const struct afb_type_x4 *from,
	struct conversion *conv
) {
	const struct afb_type_x4 *t;

	t = type_desc(type);
	return t ? convert_from_x4_s(t, from, conv) : 0;
}

static
int
convert_to_x4_s(
	const struct afb_type_x4 *type,
	const struct afb_type_x4 *to,
	struct conversion *conv
) {
	unsigned n;

	n = type->nconverts;
	while (n) {
		n--;
		if (type->converts[n].convert_to
		 && type->converts[n].type == to) {
			conv->converter = type->converts[n].convert_to;
			conv->closure = type->closure;
			return 1;
		}
	}
	return 0;
}

__attribute__((unused))
static
int
convert_to_x4(
	const struct afb_type_x4 *type,
	const struct afb_type_x4 *to,
	struct conversion *conv
) {
	const struct afb_type_x4 *t;

	t = type_desc(type);
	return t ? convert_to_x4_s(t, to, conv) : 0;
}

static
int
convert_family_canonical_x4(
	void *closure,
	const struct afb_data_x4 *from,
	const struct afb_type_x4 *type,
	const struct afb_data_x4 **to
) {
	struct afb_data *f = afb_data_of_data_x4(from);
	return afb_data_x4_create_set_x4(to, type,
			afb_data_pointer(f), afb_data_size(f),
			(void(*)(void*))from->itf->unref, afb_data_addref(f));
}

static
int
convert_x4(
	const struct afb_type_x4 *from,
	const struct afb_type_x4 *to,
	struct conversion *conv
) {
	const struct afb_type_x4 *f, *t, *c;

	if (to == from)
		return 0;

	t = type_desc(to);
	if (t && convert_from_x4_s(t, from, conv))
		return 1;

	c = from;
	f = type_desc(c);
	while(f) {
		if (convert_to_x4_s(f, to, conv))
			return 1;
		c = f->family;
		if (c == to) {
			conv->converter = convert_family_canonical_x4;
			conv->closure = 0;
			return 1;
		}
		if (t && convert_from_x4_s(t, c, conv))
			return 1;
		f = type_desc(c);
	}
	return X_ENOENT;
}

/* rc<0: error, 0: no conversion, 1: converted */
int
afb_type_convert_data_x4(
	struct afb_data *from_data,
	const struct afb_type_x4 *to_type,
	struct afb_data **to_data
) {
	int rc;
	struct conversion conv;
	struct afb_data *r = NULL;
	const struct afb_data_x4 *rx4;

	rc = convert_x4(afb_data_type_x4(from_data), to_type, &conv);
	if (rc > 0) {
		/* convert */
		rc = conv.converter(conv.closure,
			afb_data_as_data_x4(from_data),
			to_type,
			&rx4);
		if (rc >= 0) {
			r = afb_data_of_data_x4(rx4);
			rc = 1;
		}
	}
	*to_data = r;
	return rc;
}



/*****************************************************************************/
/*****************************************************************************/
/**                                                                         **/
/**                   PREDEFINED TYPES                                      **/
/**                                                                         **/
/*****************************************************************************/
/*****************************************************************************/

/*****************************************************************************/
/* PREDEFINED STRINGZ */

static const struct afb_type_x4 pt_STRINGZ =
{
	.name = "STRINGZ",
	.sharing = AFB_TYPE_X4_STREAMABLE,
	.family = NULL,
	.closure = NULL,
	.nconverts = 0,
	.converts = {}
};

/*****************************************************************************/
/* PREDEFINED JSON */

static const struct afb_type_x4 pt_JSON =
{
	.name = "JSON",
	.sharing = AFB_TYPE_X4_STREAMABLE,
	.family = &pt_STRINGZ,
	.closure = NULL,
	.nconverts = 0,
	.converts = {}
};

/*****************************************************************************/
/* PREDEFINED JSON-C */

#include <json-c/json.h>
#if !defined(JSON_C_TO_STRING_NOSLASHESCAPE)
#define JSON_C_TO_STRING_NOSLASHESCAPE 0
#endif

static int convert_json_string_to_json_c(void*,const struct afb_data_x4*,const struct afb_type_x4*,const struct afb_data_x4**);
static int convert_json_c_to_json_string(void*,const struct afb_data_x4*,const struct afb_type_x4*,const struct afb_data_x4**);

static const struct afb_type_x4 pt_JSON_C =
{
	.name = "JSON-C",
	.sharing = AFB_TYPE_X4_LOCAL,
	.family = NULL,
	.closure = NULL,
	.nconverts = 1,
	.converts = {
		{
			.type = AFB_TYPE_X4_JSON,
			.convert_from = convert_json_string_to_json_c,
			.convert_to = convert_json_c_to_json_string
		}
	}
};

static int convert_json_string_to_json_c(
	void *unused,
	const struct afb_data_x4 *in,
	const struct afb_type_x4 *type,
	const struct afb_data_x4 **out
) {
	int rc;
	json_object *json;
	enum json_tokener_error jerr;
	const char *str;

	str = afb_data_x4_pointer(in);
	json = json_tokener_parse_verbose(str, &jerr);
	if (jerr != json_tokener_success)
		json = json_object_new_string(str);
	rc = afb_data_x4_create_set_x4(out, type, json, 0, (void*)json_object_put, json);
	if (rc < 0)
		json_object_put(json);
	return rc;
}

static int convert_json_c_to_json_string(
	void *unused,
	const struct afb_data_x4 *in,
	const struct afb_type_x4 *type,
	const struct afb_data_x4 **out
) {
	struct json_object *object;
	const char *jsonstr;
	size_t sz;
	int rc;

	object = (struct json_object *)afb_data_x4_pointer(in);
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
		rc = afb_data_x4_create_set_x4(out, type, (void*)jsonstr, sz + 1, (void*)json_object_put, object);
		if (rc < 0)
			json_object_put(object);
	}
	return rc;
}
