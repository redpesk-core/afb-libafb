/*
 * Copyright (C) 2015-2021 IoT.bzh Company
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

#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include <json-c/json.h>
#if !defined(JSON_C_TO_STRING_NOSLASHESCAPE)
#define JSON_C_TO_STRING_NOSLASHESCAPE 0
#endif

#include "core/afb-type.h"
#include "core/afb-type-predefined.h"
#include "core/afb-data.h"
#include "core/afb-evt.h"
#include "core/afb-req-common.h"
#include "core/afb-json-legacy.h"
#include "utils/jsonstr.h"
#include "sys/x-errno.h"
#include "sys/verbose.h"

#define SLEN(s) ((s) ? 1 + strlen(s) : 0)

static const char _success_[] = "success";

/**********************************************************************/

int
afb_json_legacy_make_data_json_c(
	struct afb_data **result,
	struct json_object *object
) {
	return afb_data_create_raw(result, &afb_type_predefined_json_c, object, 0, (void*)json_object_put, object);
}

int
afb_json_legacy_make_data_stringz_len_mode(
	struct afb_data **data,
	const char *string,
	size_t len,
	enum afb_string_mode mode
) {
	int rc;
	const void *val = 0;
	void *clo = 0;
	size_t lenp1;

	if (len >= UINT32_MAX) {
		if (mode == Afb_String_Free)
			free((void*)string);
		*data = NULL;
		rc = X_EINVAL;
	}
	else {
		rc = 0;
		if (!string) {
			lenp1 = 0;
			val = clo = 0;
		}
		else {
			lenp1 = 1 + len;
			switch(mode) {
			case Afb_String_Const:
				val = string;
				clo = 0;
				break;
			case Afb_String_Free:
				val = string;
				clo = (void*)string;
				break;
			case Afb_String_Copy:
				val = clo = malloc(lenp1);
				if (clo)
					memcpy(clo, string, lenp1);
				else {
					*data = NULL;
					rc = X_ENOMEM;
				}
				break;
			}
		}
		if (rc == 0) {
			rc = afb_data_create_raw(data,
						&afb_type_predefined_stringz, val, lenp1, clo ? free : 0, clo);
		}
	}
	return rc;
}

int
afb_json_legacy_make_data_stringz_mode(
	struct afb_data **data,
	const char *string,
	enum afb_string_mode mode
) {
	int rc;
	size_t len;

	if (!string)
		rc = afb_json_legacy_make_data_stringz_len_mode(data, 0, 0, Afb_String_Const);
	else {
		len = strnlen(string, UINT32_MAX);
		rc = afb_json_legacy_make_data_stringz_len_mode(data, string, len, mode);
	}
	return rc;
}

/**********************************************************************/
/**
 * Unpack the data compatible with legacy versions V1, V2, V3.
 */
static int do_single_any_json(
	unsigned nparams,
	struct afb_data * const params[],
	void (*callback)(void*, const void*, const void*),
	void *closure1,
	const void *closure2,
	struct afb_type *type,
	const void *defval
) {
	int rc;
	struct afb_data *dobj;
	const void *object;

	/* extract the object */
	if (nparams < 1 || !params[0]) {
		dobj = NULL;
		object = defval;
		rc = 0;
	}
	else {
		rc = afb_data_convert(params[0], type, &dobj);
		if (rc >= 0)
			object = afb_data_ro_pointer(dobj);
		else {
			dobj = NULL;
			object = defval;
		}
	}

	/* callback */
	callback(closure1, object, closure2);

	/* cleanup */
	afb_data_unref(dobj);

	return rc;
}

int
afb_json_legacy_do2_single_json_string(
	unsigned nparams,
	struct afb_data * const params[],
	void (*callback)(void *closure1, const char *object, const void *closure2),
	void *closure1,
	const void *closure2
) {
	return do_single_any_json(nparams, params, (void*)callback, closure1, closure2, &afb_type_predefined_json, "null");
}

int
afb_json_legacy_do2_single_json_c(
	unsigned nparams,
	struct afb_data * const params[],
	void (*callback)(void *closure1, struct json_object *object, const void *closure2),
	void *closure1,
	const void *closure2
) {
	return do_single_any_json(nparams, params, (void*)callback, closure1, closure2,  &afb_type_predefined_json_c, NULL);
}

static void do2_to_do1(void *closure1, const void *object, const void *closure2)
{
	void (*f)(void*, const void*) = closure2;
	f(closure1, object);
}

int
afb_json_legacy_do_single_json_string(
	unsigned nparams,
	struct afb_data * const params[],
	void (*callback)(void *closure, const char *object),
	void *closure
) {
	return afb_json_legacy_do2_single_json_string(nparams, params, (void*)do2_to_do1, closure, callback);
}

int
afb_json_legacy_do_single_json_c(
	unsigned nparams,
	struct afb_data * const params[],
	void (*callback)(void *closure, struct json_object *object),
	void *closure
) {
	return afb_json_legacy_do2_single_json_c(nparams, params, (void*)do2_to_do1, closure, callback);
}

static void get_json_object(void *closure, struct json_object *object, const void *unused)
{
	struct json_object **ptr = closure;
	*ptr = json_object_get(object);
}

int
afb_json_legacy_get_single_json_c(
	unsigned nparams,
	struct afb_data * const params[],
	struct json_object **obj
) {
	return afb_json_legacy_do2_single_json_c(
			nparams, params, get_json_object, obj, 0);
}

/**********************************************************************/

/**
 * Unpack the data of a reply to extract the reply compatible with
 * legacy versions V1, V2, V3 and invoke the callback with it.
 */
static
int
do_reply_any_json(
	void *closure,
	int status,
	unsigned nreplies,
	struct afb_data * const replies[],
	void (*callback)(void*, const void*, const char*, const char*),
	struct afb_type *type
) {
	struct afb_data *dobj, *derr, *dinf;
	const char *error, *info;
	const void *object;

	/* extract the replied object */
	if (nreplies < 1 || !replies[0] || afb_data_convert(replies[0], type, &dobj) < 0) {
		dobj = NULL;
		object = type == &afb_type_predefined_json ? "null" : NULL;
	}
	else {
		object = afb_data_ro_pointer(dobj);
	}

	/* extract the replied error */
	if (nreplies < 2 || !replies[1] || afb_data_convert(replies[1], &afb_type_predefined_stringz, &derr) < 0) {
		derr = NULL;
		error = NULL;
	}
	else {
		error = (const char*)afb_data_ro_pointer(derr);
	}

	/* extract the replied info */
	if (nreplies < 3 || !replies[2] || afb_data_convert(replies[2], &afb_type_predefined_stringz, &dinf) < 0) {
		dinf = NULL;
		info = NULL;
	}
	else {
		info = (const char*)afb_data_ro_pointer(dinf);
	}

	/* cohercision to coherent status */
	if (status < 0 && error == NULL)
		error = "error";
	else if (status >= 0 && error != NULL)
		error = NULL;

	/* callback */
	callback(closure, object, error, info);

	/* cleanup */
	afb_data_unref(dobj);
	afb_data_unref(derr);
	afb_data_unref(dinf);

	return 0; /* TODO: process allocation errors */
}


/**
 * Unpack the data of a reply to extract the reply compatible with
 * legacy versions V1, V2, V3 and invoke the callback with it.
 */
int afb_json_legacy_do_reply_json_c(
	void *closure,
	int status,
	unsigned nreplies,
	struct afb_data * const replies[],
	void (*callback)(void*, struct json_object*, const char*, const char*)
) {
	return do_reply_any_json(closure, status, nreplies, replies, (void*)callback, &afb_type_predefined_json_c);
}

/**
 * Unpack the data of a reply to extract the reply compatible with
 * legacy versions V1, V2, V3 and invoke the callback with it.
 */
int afb_json_legacy_do_reply_json_string(
	void *closure,
	int status,
	unsigned nreplies,
	struct afb_data * const replies[],
	void (*callback)(void*, const char*, const char*, const char*)
) {
	return do_reply_any_json(closure, status, nreplies, replies, (void*)callback, &afb_type_predefined_json);
}

/**********************************************************************/

struct reply_getter {
	struct json_object **object;
	char **error;
	char **info;
	int rc;
};

static void get_reply_sync_cb(
	void *closure,
	struct json_object *object,
	const char *error,
	const char *info
) {
	struct reply_getter *rg = closure;

	if (rg->object)
		*rg->object = json_object_get(object);
	if (rg->error)
		*rg->error = error ? strdup(error) : 0;
	if (rg->info)
		*rg->info = info ? strdup(info) : 0;
	rg->rc = 0; /* TODO: process allocation errors */
}

int afb_json_legacy_get_reply_sync(
	int status,
	unsigned nreplies,
	struct afb_data * const replies[],
	struct json_object **object,
	char **error,
	char **info
) {
	int rc;
	struct reply_getter rg;

	rg.object = object;
	rg.error = error;
	rg.info = info;

	rc = afb_json_legacy_do_reply_json_c(&rg, status, nreplies, replies, get_reply_sync_cb);
	return rc >= 0 ? rg.rc : rc;
}

/**********************************************************************/
int
afb_json_legacy_make_reply_json_string(
	struct afb_data *params[],
	const char *object, void (*dobj)(void*), void *cobj,
	const char *error, void (*derr)(void*), void *cerr,
	const char *info, void (*dinf)(void*), void *cinf
) {
	int rc;

	rc = afb_data_create_raw(&params[0], &afb_type_predefined_json, object, SLEN(object), dobj, cobj);
	if (rc >= 0) {
		rc = afb_data_create_raw(&params[1], &afb_type_predefined_stringz, error, SLEN(error), derr, cerr);
		if (rc >= 0) {
			rc = afb_data_create_raw(&params[2], &afb_type_predefined_stringz, info, SLEN(info), dinf, cinf);
			if (rc < 0)
				afb_data_unref(params[1]);
		}
		if (rc < 0)
			afb_data_unref(params[0]);
	}
	return rc;
}

int
afb_json_legacy_make_reply_json_c(
	struct afb_data *params[],
	struct json_object *object,
	const char *error, void (*derr)(void*), void *cerr,
	const char *info, void (*dinf)(void*), void *cinf
) {
	int rc;

	rc = afb_data_create_raw(&params[0], &afb_type_predefined_json_c, object, 0, (void*)json_object_put, object);
	if (rc >= 0) {
		rc = afb_data_create_raw(&params[1], &afb_type_predefined_stringz, error, SLEN(error), derr, cerr);
		if (rc >= 0) {
			rc = afb_data_create_raw(&params[2], &afb_type_predefined_stringz, info, SLEN(info), dinf, cinf);
			if (rc < 0)
				afb_data_unref(params[1]);
		}
		if (rc < 0)
			afb_data_unref(params[0]);
	}
	return rc;
}

/**
 * Create in params the data of the legacy reply corresponding
 * to the given object, strings and modes.
 *
 * @param params   the parameters to create, an array of at least 3 data
 * @param object   the JSON-C object to return or NULL
 * @param error    the error text if error or NULL
 * @param info     the informative test or NULL
 * @param mode_error  The mode of the error string (static/copy/free)
 * @param mode_info   The mode of the info string (static/copy/free)
 *
 * @return 0 in case of success or an error code negative
 */
int
afb_json_legacy_make_reply_json_c_mode(
	struct afb_data *params[],
	struct json_object *object,
	const char *error,
	const char *info,
	enum afb_string_mode mode_error,
	enum afb_string_mode mode_info
) {
	int rc;

	rc = afb_data_create_raw(&params[0], &afb_type_predefined_json_c, object, 0, (void*)json_object_put, object);
	if (rc >= 0) {
		rc = afb_json_legacy_make_data_stringz_mode(&params[1], error, mode_error);
		if (rc >= 0) {
			rc = afb_json_legacy_make_data_stringz_mode(&params[2], info, mode_info);
			if (rc < 0)
				afb_data_unref(params[1]);
		}
		if (rc < 0)
			afb_data_unref(params[0]);
	}
	return rc;
}

/**********************************************************************/
/**
 * Emits the reply in the same way that for bindings V1, V2 and V3.
 *
 * @param comreq     the common request to be replied
 * @param obj        the JSON-C object to return or NULL
 * @param error      the error text if error or NULL
 * @param info       the informative test or NULL
 */
void
afb_json_legacy_req_reply_hookable(
	struct afb_req_common *comreq,
	struct json_object *obj,
	const char *error,
	const char *info
) {
	struct afb_data *reply[3];

	afb_json_legacy_make_reply_json_c_mode(reply, obj, error, info, Afb_String_Copy, Afb_String_Copy);
	afb_req_common_reply_hookable(comreq, LEGACY_STATUS(error), 3, reply);
}

void
afb_json_legacy_req_vreply_hookable(
	struct afb_req_common *comreq,
	struct json_object *obj,
	const char *error,
	const char *fmt,
	va_list args
) {
	struct afb_data *reply[3];
	char *info;

	if (fmt == NULL || vasprintf(&info, fmt, args) < 0)
		info = NULL;

	afb_json_legacy_make_reply_json_c_mode(reply, obj, error, info, Afb_String_Copy, Afb_String_Free);
	afb_req_common_reply_hookable(comreq, LEGACY_STATUS(error), 3, reply);
}

/**************************************************************************/

int
afb_json_legacy_event_rebroadcast_name(
	const char *event,
	struct json_object *obj,
	const uuid_binary_t uuid,
	uint8_t hop
) {
	int rc;
	struct afb_data *data;

	rc = afb_json_legacy_make_data_json_c(&data, obj);
	if (rc >= 0)
		rc = afb_evt_rebroadcast_name_hookable(event, 1, &data, uuid, hop);
	else {
		ERROR("impossible to create the data to rebroadcast");
	}
	return rc;
}


int
afb_json_legacy_event_push(
	struct afb_evt *evt,
	struct json_object *obj
) {
	int rc;
	struct afb_data *data;

	rc = afb_json_legacy_make_data_json_c(&data, obj);
	if (rc >= 0)
		rc = afb_evt_push(evt, 1, &data);
	else {
		ERROR("impossible to create the data to push");
	}
	return rc;
}

int
afb_json_legacy_event_push_hookable(
	struct afb_evt *evt,
	struct json_object *obj
) {
	int rc;
	struct afb_data *data;

	rc = afb_json_legacy_make_data_json_c(&data, obj);
	if (rc >= 0)
		rc = afb_evt_push_hookable(evt, 1, &data);
	else {
		ERROR("impossible to create the data to push");
	}
	return rc;
}

int
afb_json_legacy_event_broadcast_hookable(
	struct afb_evt *evt,
	struct json_object *obj
) {
	int rc;
	struct afb_data *data;

	rc = afb_json_legacy_make_data_json_c(&data, obj);
	if (rc >= 0)
		rc = afb_evt_broadcast_hookable(evt, 1, &data);
	else {
		ERROR("impossible to create the data to broadcast");
	}
	return rc;
}

#if WITH_AFB_HOOK
int
afb_json_legacy_event_hooked_push(
	struct afb_evt *evt,
	struct json_object *obj
) {
	int rc;
	struct afb_data *data;

	rc = afb_json_legacy_make_data_json_c(&data, obj);
	if (rc >= 0)
		rc = afb_evt_push_hookable(evt, 1, &data);
	else {
		ERROR("impossible to create the data to push");
	}
	return rc;
}

int
afb_json_legacy_event_hooked_broadcast(
	struct afb_evt *evt,
	struct json_object *obj
) {
	int rc;
	struct afb_data *data;

	rc = afb_json_legacy_make_data_json_c(&data, obj);
	if (rc >= 0)
		rc = afb_evt_broadcast_hookable(evt, 1, &data);
	else {
		ERROR("impossible to create the data to broadcast");
	}
	return rc;
}
#endif

/**********************************************************************/

/*
 * escape the string for JSON in destination
 * returns pointer to the terminating null
 */
static char *escjson_stpcpy(char *dest, const char *string)
{
	return &dest[jsonstr_string_escape_unsafe(dest, string, SIZE_MAX)];
}

/* compute the length of string as escaped for JSON */
static size_t escjson_strlen(const char *string)
{
	return jsonstr_string_escape_length(string, SIZE_MAX);
}

/**
 * make a string by concatenating a sequence of strings
 * each being optionnaly escaped as JSON string
 *
 * @param result   address where to store the resulting string pointer
 * @param length   place to store the computed length
 * @param count    count of strings to concatenate
 * @param strings  array of the strings to concatenate
 * @param sizes    size after copy of each concatenated string
 * @param escapes  bitfields of escape flags
 *
 * @return 0 in case of success or a negative error code
 */
static
int
make_msg_string(
	char **result,
	size_t *length,
	int count,
	const char *strings[],
	size_t sizes[],
	unsigned escapes
) {
	size_t s;
	int i;
	char *iter;

	/* compute the length */
	s = 0;
	for (i = 0 ; i < count ; i++)
		s += sizes[i];
	if (length)
		*length = s;

	/* allocate */
	*result = iter = malloc(s + 1);
	if (!iter)
		return X_ENOMEM;

	/* copy with/without escaping */
	for (i = 0 ; i < count ; i++)
		iter = (((escapes >> i) & 1) ? escjson_stpcpy : stpcpy)(iter, strings[i]);

	return 0;
}

struct mkmsg {
	int rc;
	char *message;
	size_t length;
};

static
void
mkmsg_reply_cb(
	void *closure,
	const char *object,
	const char *error,
	const char *info
) {
	static const char msg_head[] = "{\"jtype\":\"afb-reply\",\"request\":{\"status\":\"";
	static const char msg_info[] = "\",\"info\":\"";
	static const char msg_no_response[] = "\"}}";
	static const char msg_response[] = "\"},\"response\":";
	static const char msg_end_response[] = "}";

	struct mkmsg *mm = closure;
	const char *strings[10]; /* 7 is enough */
	size_t sizes[10];
	unsigned escapes;
	int n;

	escapes = 0;
	strings[0] = msg_head;
	sizes[0] = sizeof(msg_head) - 1;
	if (error) {
		strings[1] = error;
		sizes[1] = escjson_strlen(error);
		escapes = 1 << 1;
	}
	else {
		strings[1] = _success_;
		sizes[1] = sizeof(_success_) - 1;
	}
	n = 2;
	if (info) {
		strings[2] = msg_info;
		sizes[2] = sizeof(msg_info) - 1;
		strings[3] = info;
		sizes[3] = escjson_strlen(info);
		escapes |= 1 << 3;
		n = 4;
	}
	if (object && strcmp(object, "null")) {
		strings[n] = msg_response;
		sizes[n] = sizeof(msg_response) - 1;
		n++;
		strings[n] = object;
		sizes[n] = strlen(object);
		n++;
		strings[n] = msg_end_response;
		sizes[n] = sizeof(msg_end_response) - 1;
		n++;
	}
	else {
		strings[n] = msg_no_response;
		sizes[n] = sizeof(msg_no_response) - 1;
		n++;
	}

	mm->rc = make_msg_string(&mm->message, &mm->length, n, strings, sizes, escapes);
}

int
afb_json_legacy_make_msg_string_reply(
	char **message,
	size_t *length,
	int status,
	unsigned nreplies,
	struct afb_data * const replies[]
) {
	struct mkmsg mm;
	int rc;

	rc = afb_json_legacy_do_reply_json_string(&mm, status, nreplies, replies, mkmsg_reply_cb);

	*message = mm.message;
	*length = mm.length;
	return rc >= 0 ? mm.rc : rc;
}

static
void
mkmsg_event_cb(
	void *closure1,
	const char *object,
	const void *closure2
) {
	static const char msg_head[] = "{\"jtype\":\"afb-event\",\"event\":\"";
	static const char msg_no_data[] = "\"}";
	static const char msg_data[] = "\",\"data\":";
	static const char msg_end_data[] = "}";

	struct mkmsg *mm = closure1;
	const char *event = closure2;
	const char *strings[10]; /* 5 is enough */
	size_t sizes[10];
	unsigned escapes;
	int n;

	strings[0] = msg_head;
	sizes[0] = sizeof(msg_head) - 1;
	strings[1] = event;
	sizes[1] = escjson_strlen(event);
	if (object) {
		strings[2] = msg_data;
		sizes[2] = sizeof(msg_data) - 1;
		strings[3] = object;
		sizes[3] = strlen(object);
		strings[4] = msg_end_data;
		sizes[4] = sizeof(msg_end_data) - 1;
		n = 5;
	}
	else {
		strings[2] = msg_no_data;
		sizes[2] = sizeof(msg_no_data) - 1;
		n = 3;
	}
	escapes = 1 << 1;

	mm->rc = make_msg_string(&mm->message, &mm->length, n, strings, sizes, escapes);
}

int
afb_json_legacy_make_msg_string_event(
	char **message,
	size_t *length,
	const char *event,
	unsigned nparams,
	struct afb_data * const params[]
) {
	struct mkmsg mm;

	afb_json_legacy_do2_single_json_string(nparams, params, mkmsg_event_cb, &mm, event);

	*message = mm.message;
	*length = mm.length;
	return mm.rc;
}
