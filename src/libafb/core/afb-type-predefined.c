/*
 * Copyright (C) 2015-2024 IoT.bzh Company
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

/*
 * Predefined types declared in constant initialized memory
 * It can then take placr in some read only memory.
 * For convenience, the symbols are exported as writeable
 * but the program takes care of the flag FLAG_IS_PREDEFINED
 * to avoid modifying such read only data.
 */

#include "../libafb-config.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>

#include <rp-utils/rp-jsonstr.h>

#include "afb-type.h"
#include "afb-type-internal.h"
#include "afb-type-predefined.h"

#include "afb-data.h"

#include "sys/x-errno.h"

#if !WITHOUT_JSON_C
# include <json-c/json.h>
# if !defined(JSON_C_TO_STRING_NOSLASHESCAPE)
#  define JSON_C_TO_STRING_NOSLASHESCAPE 0
# endif
#endif

#if 0
#define UNUSED_POLICY
#else
#define UNUSED_POLICY __attribute__((unused))
#endif

/*****************************************************************************/
/*****************************************************************************/
/**                                                                         **/
/**         HELPER MACROS                                                   **/
/**                                                                         **/
/*****************************************************************************/
/*****************************************************************************/

/** external variable name for 'stype' */
#define PREDEF(stype) afb_type_predefined_##stype

/** export 'stype' definition with the given name 'asvar' */
#define EXPORT_AS(stype,asvar) \
	extern struct afb_type __attribute__((alias("predefined_" #stype))) asvar

/** export the predefined type 'stype' */
#define EXPORT_PREDEF(stype)  EXPORT_AS(stype,PREDEF(stype))

/** declare a convertion routine from 'ftype' tot 'ttype' */
#define CONVERT(ftype,ttype) \
	UNUSED_POLICY \
	static int convert_##ftype##_to_##ttype(\
		void *closure,\
		struct afb_data *in,\
		struct afb_type *type,\
		struct afb_data **out \
	)

/** declare an updata routine from 'ftype' tot 'ttype' */
#define UPDATE(ftype,ttype) \
	UNUSED_POLICY \
	static int update_##ftype##_to_##ttype(\
		void *closure,\
		struct afb_data *in,\
		struct afb_type *type,\
		struct afb_data *to \
	)

/** declare array of operation for 'stype' */
#define PREDEFINED_OPERATION(stype) \
		static const struct opdesc opcvt_##stype[] =

/** add in 'ftype' operations the convert to 'ttype' */
#define CONVERT_TO(ftype,ttype) \
	{ .kind = Convert_To, .type = &PREDEF(ttype), \
	  .converter = convert_##ftype##_to_##ttype, .closure = 0 }

/** add in 'ttype' operations the convert from 'ttype' */
#define CONVERT_FROM(ftype,ttype) \
	{ .kind = Convert_From, .type = &PREDEF(ftype), \
	  .converter = convert_##ftype##_to_##ttype, .closure = 0 }

/** add in 'ftype' operations the update to 'ttype' */
#define UPDATE_TO(ftype,ttype) \
	{ .kind = Update_To, .type = &PREDEF(ttype), \
	  .updater = update_##ftype##_to_##ttype, .closure = 0 }

/** add in 'ttype' operations the update from 'ttype' */
#define UPDATE_FROM(ftype,ttype) \
	{ .kind = Update_From, .type = &PREDEF(ftype), \
	  .updater = update_##ftype##_to_##ttype, .closure = 0 }

/** add in 'ftype' operations the convert and update to 'ttype' */
#define TRANSFORM_TO(ftype,ttype) \
	CONVERT_TO(ftype,ttype), \
	UPDATE_TO(ftype,ttype)

/** add in 'ttype' operations the convert and update from 'ftype' */
#define TRANSFORM_FROM(ftype,ttype) \
	CONVERT_FROM(ftype,ttype), \
	UPDATE_FROM(ftype,ttype)

/**
 * Declare and export the predefined 'stype'.
 * Set its flags to 'FLAGS' bitwise-or FLAG_IS_PREDEFINED.
 * Set its family to 'FAMILY'.
 * Set the link to next to NEXT.
 * Declare the operations (see PREDEFINED_OPERATION)
 */
#define PREDEFINED_TYPE(stype,ID,FLAGS,FAMILY,NEXT) \
	EXPORT_PREDEF(stype);  \
	static const struct afb_type predefined_##stype = \
	{ \
		.name = AFB_PREFIX_PREDEF_TYPE #stype, \
		.next = NEXT, \
		.operations = (struct opdesc*)opcvt_##stype,\
		.family = FAMILY, \
		.flags = FLAG_IS_PREDEFINED | FLAGS, \
		.typenum = ID, \
		.op_count = (uint16_t)(sizeof opcvt_##stype / sizeof opcvt_##stype[0]) \
	}

/*****************************************************************************/
/*****************************************************************************/
/**                                                                         **/
/**                   OPAQUE HELPERS                                        **/
/**                                                                         **/
/*****************************************************************************/
/*****************************************************************************/

#define OPAQUE_KEY      "#@"
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

	if (!string)
		rc = X_EINVAL;
	else {
		rc = sscanf(string, OPAQUE_FMT_RD "%n", &opaqueid, &p);
		if (rc != 1 || string[p] != term) {
			rc = X_EINVAL;
		}
		else {
			rc = afb_data_get_opacified(opaqueid, &found, &otype);
			if (rc >= 0) {
				if (type == otype)
					*out = found;
				else {
					afb_data_unref(found);
					rc = X_ENOENT;
				}
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
		if (rc < 0 || (size_t)rc >= size) {
			rc = X_EFAULT;
		}
	}
	return rc;
}

/*****************************************************************************/
/*****************************************************************************/
/**                                                                         **/
/**                   ANY STRING HELPERS                                    **/
/**                                                                         **/
/*****************************************************************************/
/*****************************************************************************/

UNUSED_POLICY
static int make_static(struct afb_data **result, struct afb_type *type, const char *value, size_t length) {
	return afb_data_create_raw(result, type, value, length, 0, 0);
}

UNUSED_POLICY
static int make_str_static(struct afb_data **result, struct afb_type *type, const char *value) {
	return make_static(result, type, value, 1 + strlen(value));
}

UNUSED_POLICY
static int make_str_copy(struct afb_data **result, struct afb_type *type, const char *value) {
	return afb_data_create_copy(result, type, value, 1 + strlen(value));
}

/*****************************************************************************/
/*****************************************************************************/
/**                                                                         **/
/**                   STRINZ HELPERS                                        **/
/**                                                                         **/
/*****************************************************************************/
/*****************************************************************************/

UNUSED_POLICY
static int make_stringz_static_length(struct afb_data **result, const char *value, size_t length) {
	return make_static(result, &PREDEF(stringz), value, 1 + length);
}

UNUSED_POLICY
static int make_stringz_static(struct afb_data **result, const char *value) {
	return make_str_static(result, &PREDEF(stringz), value);
}

UNUSED_POLICY
static int make_stringz_copy_length(struct afb_data **result, const char *value, size_t length) {
	return afb_data_create_copy(result, &PREDEF(stringz), value, 1 + length);
}

UNUSED_POLICY
static int make_stringz_copy(struct afb_data **result, const char *value) {
	return make_str_copy(result, &PREDEF(stringz), value);
}

/*****************************************************************************/
/*****************************************************************************/
/**                                                                         **/
/**                   JSON HELPERS                                          **/
/**                                                                         **/
/*****************************************************************************/
/*****************************************************************************/

UNUSED_POLICY
static int make_json_static_length(struct afb_data **result, const char *value, size_t length) {
	return make_static(result, &PREDEF(json), value, 1 + length);
}

UNUSED_POLICY
static int make_json_static(struct afb_data **result, const char *value) {
	return make_str_static(result, &PREDEF(json), value);
}

UNUSED_POLICY
static int make_json_copy_length(struct afb_data **result, const char *value, size_t length) {
	return afb_data_create_copy(result, &PREDEF(json), value, length + 1);
}

UNUSED_POLICY
static int make_json_copy(struct afb_data **result, const char *value) {
	return make_str_copy(result, &PREDEF(json), value);
}

UNUSED_POLICY
static const char *get_json(struct afb_data *data) {
	return (const char*)afb_data_ro_pointer(data);
}

/*****************************************************************************/
/*****************************************************************************/
/**                                                                         **/
/**                   JSON-C HELPERS                                        **/
/**                                                                         **/
/*****************************************************************************/
/*****************************************************************************/

#if !WITHOUT_JSON_C
UNUSED_POLICY
static int make_json_c(struct afb_data **result, struct json_object *value) {
	return afb_data_create_raw(result, &PREDEF(json_c), value, 0,
					(void*)json_object_put, value);
}

UNUSED_POLICY
static int make_json_c_not_null(struct afb_data **result, struct json_object *value) {
	return value ? make_json_c(result, value) : X_ENOMEM;
}

UNUSED_POLICY
static struct json_object *get_json_c(struct afb_data *data) {
	return (struct json_object*)afb_data_ro_pointer(data);
}
#endif

/*****************************************************************************/
/*****************************************************************************/
/**                                                                         **/
/**         PREDEFINED TYPES                                                **/
/**                                                                         **/
/*****************************************************************************/
/*****************************************************************************/

/*****************************************************************************/
/* PREDEFINED OPAQUE */

CONVERT(opaque,stringz)
{
	int rc;
	char buffer[OPAQUE_BUFSIZE];

	rc = opaque_to_string(in, buffer, sizeof buffer);
	return rc < 0 ? rc : make_stringz_copy_length(out, buffer, (size_t)rc);
}

CONVERT(opaque,json)
{
	int rc;
	char buffer[OPAQUE_BUFSIZE + 2];

	rc = opaque_to_string(in, &buffer[1], (sizeof buffer) - 1);
	if (rc >= 0) {
		buffer[0] = buffer[++rc] = '"';
		buffer[++rc] = 0;
		rc = make_json_copy_length(out, buffer, (size_t)rc);
	}
	return rc;
}

#if !WITHOUT_JSON_C
CONVERT(opaque,json_c)
{
	int rc;
	char buffer[OPAQUE_BUFSIZE];

	rc = opaque_to_string(in, buffer, sizeof buffer);
	if (rc >= 0)
		rc = make_json_c_not_null(out, json_object_new_string(buffer));
	return rc;
}
#endif

PREDEFINED_OPERATION(opaque)
	{
		CONVERT_TO(opaque, stringz),
		CONVERT_TO(opaque, json),
#if !WITHOUT_JSON_C
		CONVERT_TO(opaque, json_c)
#endif
	};

PREDEFINED_TYPE(opaque, Afb_Typeid_Predefined_Opaque, FLAG_IS_OPAQUE, 0, 0);

/*****************************************************************************/
/* PREDEFINED BYTEARRAY */

PREDEFINED_OPERATION(bytearray)
	{
	};

PREDEFINED_TYPE(bytearray, Afb_Typeid_Predefined_Bytearray, FLAG_IS_SHAREABLE | FLAG_IS_STREAMABLE, 0, &PREDEF(opaque));

/*****************************************************************************/
/* PREDEFINED STRINGZ */

CONVERT(stringz,opaque)
{
	int rc;
	const char *s;

	s = afb_data_ro_pointer(in);
	rc = opaque_from_string(s, 0, type, out);
	return rc;
}

CONVERT(stringz,json)
{
	const char *istr;
	char *ostr;
	size_t isz, osz;
	int rc;

	/* get string parameters */
	rc = afb_data_get_constant(in, (void**)&istr, &isz);
	if (rc < 0)
		return rc;

	/* null is still null */
	if (!istr || !isz)
		return afb_data_create_raw(out, type, "null", 5, 0, 0);

	/* empty case is optimized */
	if (isz <= 1)
		return afb_data_create_raw(out, type, "\"\"", 3, 0, 0);

	/* compute escaped size and allocate it */
	osz = rp_jsonstr_string_escape_length(istr, isz - 1);
	ostr = malloc(3 + osz);
	if (!ostr)
		return X_ENOMEM;

	/* make the json string */
	ostr[0] = '"';
	rp_jsonstr_string_escape_unsafe(&ostr[1], istr, isz - 1);
	ostr[osz + 1] = '"';
	ostr[osz + 2] = 0;

	/* create the data */
	return afb_data_create_raw(out, type, ostr, 3 + osz, free, ostr);
}

CONVERT(stringz,bytearray)
{
	void *ptr;
	size_t sz;
	int rc;

	/* get string parameters */
	rc = afb_data_get_constant(in, &ptr, &sz);
	if (rc >= 0) {
		rc = afb_data_create_raw(out, type, ptr, sz - !!sz, 0, 0);
		if (rc >= 0) {
			rc = afb_data_dependency_add(*out, in);
			if (rc < 0)
				afb_data_unref(*out);
		}
	}
	return rc;
}

#if !WITHOUT_JSON_C


static int str_to_json_c(
	struct afb_data *in,
	struct afb_data **out,
	int fallback
) {
	int rc, sts, len;
	struct json_tokener *tok;
	json_object *json;
	const char *str;
	size_t sz;

	/* get string parameters */
	rc = afb_data_get_constant(in, (void**)&str, &sz);
	if (rc >= 0) {
		if (str == NULL || sz == 0)
			rc = make_json_c(out, NULL);
		else if (sz > INT_MAX)
			rc = X_E2BIG;
		else {
			len = (int)sz - 1;
			tok = json_tokener_new();
			sts = tok != NULL;
			if (tok == NULL)
				rc = X_ENOMEM;
			else {
				json = json_tokener_parse_ex(tok, str, len);
				sts =  json_tokener_get_error(tok) == json_tokener_success
#if JSON_C_VERSION_NUM >= 0x000E00
				   && (int)json_tokener_get_parse_end(tok) == len;
#else
				   && tok->char_offset == len;
#endif
				json_tokener_free(tok);
				if (sts)
					rc = make_json_c(out, json);
				else if (fallback)
					rc = make_json_c_not_null(out, json_object_new_string_len(str, len));
				else
					rc = X_EINVAL;
			}
		}
	}
	return rc;
}

CONVERT(stringz,json_c)
{
	return str_to_json_c(in, out, 1);
}
#endif

PREDEFINED_OPERATION(stringz)
	{
		CONVERT_TO(stringz, opaque),
		CONVERT_TO(stringz, bytearray),
#if !WITHOUT_JSON_C
		CONVERT_TO(stringz, json_c),
#endif
		CONVERT_TO(stringz, json)
	};

PREDEFINED_TYPE(stringz, Afb_Typeid_Predefined_Stringz, FLAG_IS_STREAMABLE, 0, &PREDEF(bytearray));

/*****************************************************************************/
/* PREDEFINED JSON */

#if !WITHOUT_JSON_C
CONVERT(json,json_c)
{
	return str_to_json_c(in, out, 0);
}
#endif

CONVERT(json,opaque)
{
	int rc;
	const char *s;

	s = afb_data_ro_pointer(in);
	if (*s++ != '"')
		rc = X_EINVAL;
	else
		rc = opaque_from_string(s, '"', type, out);
	return rc;
}

PREDEFINED_OPERATION(json)
	{
		CONVERT_TO(json, opaque),
#if !WITHOUT_JSON_C
		CONVERT_TO(json, json_c)
#endif
	};

PREDEFINED_TYPE(json, Afb_Typeid_Predefined_Json, FLAG_IS_STREAMABLE, &PREDEF(stringz), &PREDEF(stringz));

/*****************************************************************************/
/* PREDEFINED JSON-C */

#if !WITHOUT_JSON_C
CONVERT(json_c,json)
{
	struct json_object *object;
	const char *jsonstr;
	size_t sz;
	int rc;

	object = (struct json_object *)afb_data_ro_pointer(in);
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
	}
	return rc;
}

CONVERT(json_c,opaque)
{
	int rc;
	struct json_object *object;
	const char *s;

	object = (struct json_object *)afb_data_ro_pointer(in);
	if (!json_object_is_type(object, json_type_string))
		rc = X_EINVAL;
	else {
		s = json_object_get_string(object);
		rc = opaque_from_string(s, 0, type, out);
	}
	return rc;
}
#endif

PREDEFINED_OPERATION(json_c)
	{
#if !WITHOUT_JSON_C
		CONVERT_TO(json_c, opaque),
		CONVERT_TO(json_c, json)
#endif
	};

PREDEFINED_TYPE(json_c, Afb_Typeid_Predefined_Json_C, 0, 0, &PREDEF(json));

/*****************************************************************************/
/*****************************************************************************/
/**                                                                         **/
/**         PREDEFINED BASIC TYPES                                          **/
/**                                                                         **/
/*****************************************************************************/
/*****************************************************************************/

/** declare a function creating a data of 'stype' the the copied value of 'ctype' */
#define MAKE_BASIC(stype,ctype) \
	UNUSED_POLICY \
	static int make_##stype(struct afb_data **result, ctype value) { \
		return afb_data_create_copy(result, &PREDEF(stype), &value, sizeof value); \
	}

/** declare a function creating get the 'ctype' value of a data of 'stype' */
#define GET_BASIC(stype,ctype) \
	UNUSED_POLICY \
	static ctype get_##stype(struct afb_data *data) {\
		return *(const ctype*)afb_data_ro_pointer(data); \
	}

/** declare a function updating the the 'ctype' value of a data of 'stype' */
#define SET_BASIC(stype,ctype) \
	UNUSED_POLICY \
	static int set_##stype(struct afb_data *data, ctype value) {\
		ctype *ptr; \
		int rc = afb_data_get_mutable(data, (void**)&ptr, 0); \
		if (rc >= 0) \
			*ptr = value; \
		return rc; \
	}

/** declare the 3 functions for creating, getting, updating data of 'stype' with values of 'ctype' */
#define DECLARE_BASIC(stype,ctype) \
	MAKE_BASIC(stype,ctype) \
	GET_BASIC(stype,ctype) \
	SET_BASIC(stype,ctype)

/** declare a function creating a data of 'stype_to' of value of 'ctype_to'
 * from the value of type 'ctype_from' held by the data of 'stype_from'.
 * Use C conversion. */
#define CONVERT_BASIC(stype_from,ctype_from,stype_to,ctype_to) \
	static int make_##stype_to(struct afb_data **result, ctype_to value); \
	static ctype_from get_##stype_from(struct afb_data *data); \
	UNUSED_POLICY \
	CONVERT(stype_from,stype_to) { \
		return make_##stype_to(out, (ctype_to)get_##stype_from(in)); \
	}

/** declare a function updating a data of 'stype_to' of value of 'ctype_to'
 * from the value of type 'ctype_from' held by the data of 'stype_from'
 * Use C conversion. */
#define UPDATE_BASIC(stype_from,ctype_from,stype_to,ctype_to) \
	static int set_##stype_to(struct afb_data *to, ctype_to value); \
	static ctype_from get_##stype_from(struct afb_data *data); \
	UNUSED_POLICY \
	UPDATE(stype_from,stype_to) { \
		return set_##stype_to(to, (ctype_to)get_##stype_from(in)); \
	}

/** declare the 2 convertion and updating functions using C conversion */
#define TRANSFORM_BASIC(stype_from,ctype_from,stype_to,ctype_to) \
	CONVERT_BASIC(stype_from,ctype_from,stype_to,ctype_to) \
	UPDATE_BASIC(stype_from,ctype_from,stype_to,ctype_to)

/** declare a function converting value of 'ctype_from' to value of 'ctype_to'
 * needed when the convertion isn't hold by C or if error are to be detected */
#define EXTRACT(stype_from,ctype_from,stype_to,ctype_to) \
	UNUSED_POLICY \
	static int extract_##stype_to##_of_##stype_from(\
		ctype_from from,\
		ctype_to *to \
	)

/** declare a function creating a data of 'stype_to' of value of 'ctype_to'
 * from the value of type 'ctype_from' held by the data of 'stype_from'.
 * Use EXTRACT conversion. */
#define CONVERT_EXTRACT(stype_from,ctype_from,stype_to,ctype_to) \
	static int make_##stype_to(struct afb_data **result, ctype_to value); \
	static int extract_##stype_to##_of_##stype_from(ctype_from from,ctype_to *to); \
	static ctype_from get_##stype_from(struct afb_data *data); \
	UNUSED_POLICY \
	CONVERT(stype_from,stype_to) { \
		ctype_to value; \
		ctype_from from = get_##stype_from(in); \
		int rc = extract_##stype_to##_of_##stype_from(from, &value); \
		return rc < 0 ? rc : make_##stype_to(out, value); \
	}

/** declare a function updating a data of 'stype_to' of value of 'ctype_to'
 * from the value of type 'ctype_from' held by the data of 'stype_from'
 * Use EXTRACT conversion. */
#define UPDATE_EXTRACT(stype_from,ctype_from,stype_to,ctype_to) \
	static int set_##stype_to(struct afb_data *to, ctype_to value); \
	static int extract_##stype_to##_of_##stype_from(ctype_from from,ctype_to *to); \
	static ctype_from get_##stype_from(struct afb_data *data); \
	UNUSED_POLICY \
	UPDATE(stype_from,stype_to) { \
		ctype_to value; \
		ctype_from from = get_##stype_from(in); \
		int rc = extract_##stype_to##_of_##stype_from(from, &value); \
		return rc < 0 ? rc : set_##stype_to(to, value); \
	}

/** declare the 2 convertion and updating functions using EXTRACT conversion */
#define TRANSFORM_EXTRACT(stype_from,ctype_from,stype_to,ctype_to) \
	CONVERT_EXTRACT(stype_from,ctype_from,stype_to,ctype_to) \
	UPDATE_EXTRACT(stype_from,ctype_from,stype_to,ctype_to)

/*****************************************************************************/
/* PREDEFINED bool */

UNUSED_POLICY
static int make_bool(struct afb_data **result, uint8_t value) {
	value = !!value;
	return afb_data_create_copy(result, &PREDEF(bool), &value, 1);
}
GET_BASIC(bool, uint8_t)
SET_BASIC(bool, uint8_t)

TRANSFORM_BASIC(bool, uint8_t, i32, int32_t)
TRANSFORM_BASIC(bool, uint8_t, u32, uint32_t)
TRANSFORM_BASIC(bool, uint8_t, i64, int64_t)
TRANSFORM_BASIC(bool, uint8_t, u64, uint64_t)
TRANSFORM_BASIC(bool, uint8_t, double, double)

CONVERT(bool,json)
{
	uint8_t value = get_bool(in);
	return make_stringz_static(out, value ? "true" : "false");
}

#if !WITHOUT_JSON_C
CONVERT(bool,json_c)
{
	return make_json_c_not_null(out, json_object_new_boolean(get_bool(in)));
}

EXTRACT(json_c,struct json_object*,bool,uint8_t)
{
	if (json_object_get_type(from) != json_type_boolean)
		return X_EINVAL;
	*to = (uint8_t)json_object_get_boolean(from);
	return 0;
}
TRANSFORM_EXTRACT(json_c,struct json_object*,bool,uint8_t)
#endif

EXTRACT(json,const char*,bool,uint8_t)
{
	if (!strcmp(from, "true"))
		*to = 1;
	else if (!strcmp(from, "false"))
		*to = 0;
	else
		return X_EINVAL;
	return 0;
}
TRANSFORM_EXTRACT(json,const char*,bool,uint8_t)

PREDEFINED_OPERATION(bool)
	{
		CONVERT_TO(bool, json),
		TRANSFORM_FROM(json, bool),
#if !WITHOUT_JSON_C
		CONVERT_TO(bool, json_c),
		TRANSFORM_FROM(json_c, bool),
#endif
		TRANSFORM_TO(bool, i32),
		TRANSFORM_TO(bool, u32),
		TRANSFORM_TO(bool, i64),
		TRANSFORM_TO(bool, u64),
		TRANSFORM_TO(bool, double)
	};

PREDEFINED_TYPE(bool, Afb_Typeid_Predefined_Bool, FLAG_IS_SHAREABLE, 0, &PREDEF(json_c));

/*****************************************************************************/
/* PREDEFINED i32 */

DECLARE_BASIC(i32, int32_t)

TRANSFORM_BASIC(i32, int32_t, i64, int64_t)
TRANSFORM_BASIC(i32, int32_t, double, double)

CONVERT(i32,json)
{
	char buffer[30];
	int len = snprintf(buffer, sizeof buffer, "%d", get_i32(in));
	return make_stringz_copy_length(out, buffer, (size_t)len);
}

#if !WITHOUT_JSON_C
CONVERT(i32,json_c)
{
	return make_json_c_not_null(out, json_object_new_int(get_i32(in)));
}

EXTRACT(json_c,struct json_object*,i32,int32_t)
{
	int v;
	if (json_object_get_type(from) != json_type_int)
		return X_EINVAL;
	v = json_object_get_int(from);
	if (v < INT32_MIN || v > INT32_MAX)
		return X_ERANGE;
	*to = (int32_t)v;
	return 0;
}
TRANSFORM_EXTRACT(json_c,struct json_object*,i32,int32_t)
#endif

EXTRACT(json,const char*,i32,int32_t)
{
	char *end;
	long int v = strtol(from, &end, 10);
	if (*end || v < INT32_MIN || v > INT32_MAX)
		return X_ERANGE;
	*to = (int32_t)v;
	return 0;
}
TRANSFORM_EXTRACT(json,const char*,i32,int32_t)

PREDEFINED_OPERATION(i32)
	{
		CONVERT_TO(i32, json),
		TRANSFORM_FROM(json, i32),
#if !WITHOUT_JSON_C
		CONVERT_TO(i32, json_c),
		TRANSFORM_FROM(json_c, i32),
#endif
		TRANSFORM_TO(i32, i64),
		TRANSFORM_TO(i32, double)
	};

PREDEFINED_TYPE(i32, Afb_Typeid_Predefined_I32, FLAG_IS_SHAREABLE, 0, &PREDEF(bool));

/*****************************************************************************/
/* PREDEFINED u32 */

DECLARE_BASIC(u32, uint32_t)

TRANSFORM_BASIC(u32, uint32_t, i64, int64_t)
TRANSFORM_BASIC(u32, uint32_t, u64, uint64_t)
TRANSFORM_BASIC(u32, uint32_t, double, double)

CONVERT(u32,json)
{
	char buffer[30];
	int len = snprintf(buffer, sizeof buffer, "%u", get_u32(in));
	return make_stringz_copy_length(out, buffer, (size_t)len);
}

#if !WITHOUT_JSON_C
CONVERT(u32,json_c)
{
	return make_json_c_not_null(out, json_object_new_int64((int64_t)get_u32(in)));
}

EXTRACT(json_c,struct json_object*,u32,uint32_t)
{
	int64_t v;
	if (json_object_get_type(from) != json_type_int)
		return X_EINVAL;
	v = json_object_get_int64(from);
	if (v < 0 || v > UINT32_MAX)
		return X_ERANGE;
	*to = (uint32_t)v;
	return 0;
}
TRANSFORM_EXTRACT(json_c,struct json_object*,u32,uint32_t)
#endif

EXTRACT(json,const char*,u32,uint32_t)
{
	char *end;
	unsigned long int v = strtoul(from, &end, 10);
	if (*end || v > UINT32_MAX)
		return X_ERANGE;
	*to = (uint32_t)v;
	return 0;
}
TRANSFORM_EXTRACT(json,const char*,u32,uint32_t)

PREDEFINED_OPERATION(u32)
	{
		CONVERT_TO(u32, json),
		TRANSFORM_FROM(json, u32),
#if !WITHOUT_JSON_C
		CONVERT_TO(u32, json_c),
		TRANSFORM_FROM(json_c, u32),
#endif
		TRANSFORM_TO(u32, i64),
		TRANSFORM_TO(u32, u64),
		TRANSFORM_TO(u32, double)
	};

PREDEFINED_TYPE(u32, Afb_Typeid_Predefined_U32, FLAG_IS_SHAREABLE, 0, &PREDEF(i32));

/*****************************************************************************/
/* PREDEFINED i64 */

DECLARE_BASIC(i64, int64_t)

TRANSFORM_BASIC(i64, int64_t, double, double)

CONVERT(i64,json)
{
	char buffer[60];
	int len = snprintf(buffer, sizeof buffer, "%lld", (long long)get_i64(in));
	return make_stringz_copy_length(out, buffer, (size_t)len);
}

#if !WITHOUT_JSON_C
CONVERT(i64,json_c)
{
	return make_json_c_not_null(out, json_object_new_int64(get_i64(in)));
}

EXTRACT(json_c,struct json_object*,i64,int64_t)
{
	int64_t v;
	if (json_object_get_type(from) != json_type_int)
		return X_EINVAL;
	v = json_object_get_int64(from);
	if (v < INT64_MIN || v > INT64_MAX)
		return X_ERANGE;
	*to = (int64_t)v;
	return 0;
}
TRANSFORM_EXTRACT(json_c,struct json_object*,i64,int64_t)
#endif

EXTRACT(json,const char*,i64,int64_t)
{
	char *end;
	long long int v = strtoll(from, &end, 10);
	if (*end || v < INT64_MIN || v > INT64_MAX)
		return X_ERANGE;
	*to = (int64_t)v;
	return 0;
}
TRANSFORM_EXTRACT(json,const char*,i64,int64_t)

PREDEFINED_OPERATION(i64)
	{
		CONVERT_TO(i64, json),
		TRANSFORM_FROM(json, i64),
#if !WITHOUT_JSON_C
		CONVERT_TO(i64, json_c),
		TRANSFORM_FROM(json_c, i64),
#endif
		TRANSFORM_TO(i64, double)
	};

PREDEFINED_TYPE(i64, Afb_Typeid_Predefined_I64, FLAG_IS_SHAREABLE, 0, &PREDEF(u32));

/*****************************************************************************/
/* PREDEFINED u64 */

DECLARE_BASIC(u64, uint64_t)

TRANSFORM_BASIC(u64, uint64_t, double, double)

CONVERT(u64,json)
{
	char buffer[60];
	int len = snprintf(buffer, sizeof buffer, "%llu", (long long)get_u64(in));
	return make_stringz_copy_length(out, buffer, (size_t)len);
}

EXTRACT(json,const char*,u64,uint64_t)
{
	char *end;
	unsigned long long int v = strtoull(from, &end, 10);
	if (*end || v > UINT64_MAX)
		return X_ERANGE;
	*to = (uint64_t)v;
	return 0;
}
TRANSFORM_EXTRACT(json,const char*,u64,uint64_t)

PREDEFINED_OPERATION(u64)
	{
		CONVERT_TO(u64, json),
		TRANSFORM_FROM(json, u64),
		TRANSFORM_TO(u64, double)
	};

PREDEFINED_TYPE(u64, Afb_Typeid_Predefined_U64, FLAG_IS_SHAREABLE, 0, &PREDEF(i64));

/*****************************************************************************/
/* PREDEFINED double */

DECLARE_BASIC(double, double)

CONVERT(double,json)
{
	char buffer[60];
	int len = snprintf(buffer, sizeof buffer, "%18g", get_double(in));
	return make_stringz_copy_length(out, buffer, (size_t)len);
}

#if !WITHOUT_JSON_C
CONVERT(double,json_c)
{
	return make_json_c_not_null(out, json_object_new_double(get_double(in)));
}

EXTRACT(json_c,struct json_object*,double,double)
{
	if (json_object_get_type(from) == json_type_int)
		*to = (double)json_object_get_int64(from);
	else if (json_object_get_type(from) == json_type_double)
		*to = json_object_get_double(from);
	else
		return X_EINVAL;
	return 0;
}
TRANSFORM_EXTRACT(json_c,struct json_object*,double,double)
#endif

EXTRACT(json,const char*,double,double)
{
	char *end;
	double v = strtod(from, &end);
	if (*end)
		return X_EINVAL;
	*to = v;
	return 0;
}
TRANSFORM_EXTRACT(json,const char*,double,double)

PREDEFINED_OPERATION(double)
	{
		CONVERT_TO(double, json),
		TRANSFORM_FROM(json, double),
#if !WITHOUT_JSON_C
		CONVERT_TO(double, json_c),
		TRANSFORM_FROM(json_c, double)
#endif
	};

PREDEFINED_TYPE(double, Afb_Typeid_Predefined_Double, FLAG_IS_SHAREABLE, 0, &PREDEF(u64));

/*****************************************************************************/

EXPORT_AS(double,_afb_type_head_of_predefineds_);
