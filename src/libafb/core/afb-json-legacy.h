/*
 * Copyright (C) 2015-2025 IoT.bzh Company
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

#include "../libafb-config.h"

#if WITH_BINDINGS_V3

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#include <rp-utils/rp-uuid.h>

#include "afb-string-mode.h"

struct json_object;
struct afb_data;
struct afb_evt;
struct afb_req_common;

/**********************************************************************/

/**
 * Create a DATA for the given JSON-C object
 *
 * The take owning of the object. This means that the object
 * will be release (json_object_put) when the data get released
 * or if the creation of the data fails.
 *
 * @param result pointer to the created result
 * @param object the JSON-C object to wrap in data
 *
 * @return 0 on success or else a negative error code
 */
extern
int
afb_json_legacy_make_data_json_c(
	struct afb_data **result,
	struct json_object *object
);

/**********************************************************************/

/**
 * Call 'callback' with the JSON string representation
 * of the parameters.
 *
 * @param nparam  count of data in arra params
 * @param params  array of the input data
 * @param closure closure for the callback
 *
 * @return 0 on success or a negative error code
 */
extern
int
afb_json_legacy_do_single_json_string(
	unsigned nparams,
	struct afb_data * const params[],
	void (*callback)(void *closure, const char *object),
	void *closure
);

/**
 * Call 'callback' with the json-c object representation
 * of the parameters.
 *
 * @param nparam  count of data in arra params
 * @param params  array of the input data
 * @param closure closure for the callback
 *
 * @return 0 on success or a negative error code
 */
extern
int
afb_json_legacy_do_single_json_c(
	unsigned nparams,
	struct afb_data * const params[],
	void (*callback)(void *closure, struct json_object *object),
	void *closure
);

/**
 * Call 'callback' with the JSON string representation
 * of the parameters.
 *
 * Same as afb_json_legacy_do_single_json_string but with 2 closures.
 *
 * @param nparam   count of data in arra params
 * @param params   array of the input data
 * @param closure1 first closure for the callback
 * @param closure2 second closure for the callback
 *
 * @return 0 on success or a negative error code
 */
extern
int
afb_json_legacy_do2_single_json_string(
	unsigned nparams,
	struct afb_data * const params[],
	void (*callback)(void *closure1, const char *object, const void *closure2),
	void *closure1,
	const void *closure2
);

/**
 * Call 'callback' with the json-c object representation
 * of the parameters.
 *
 * Same as afb_json_legacy_do_single_json_c but with 2 closures.
 *
 * @param nparam   count of data in arra params
 * @param params   array of the input data
 * @param closure1 first closure for the callback
 * @param closure2 second closure for the callback
 *
 * @return 0 on success or a negative error code
 */
extern
int
afb_json_legacy_do2_single_json_c(
	unsigned nparams,
	struct afb_data * const params[],
	void (*callback)(void *closure1, struct json_object *object, const void *closure2),
	void *closure1,
	const void *closure2
);

/**
 * Get the json-c object representation of the parameters.
 *
 * @param nparam   count of data in arra params
 * @param params   array of the input data
 * @param obj      pointer for storing the result
 *
 * @return 0 on success or a negative error code
 */
extern
int
afb_json_legacy_get_single_json_c(
	unsigned nparams,
	struct afb_data * const params[],
	struct json_object **obj
);

/**********************************************************************/

/**
 * Interpret the reply given by (status, nreplies, replies) in terms
 * of bindings 1, 2 and 3 (object, error, info) and call the callback
 * with it. The object is expected as a json-c object.
 *
 * @param closure  closure to the callback
 * @param status   status of the reply
 * @param nreplies count of data in array replies
 * @param replies  array of replied data
 * @param callback called callback
 *
 * @return 0 on success or a negative error code
 */
extern
int
afb_json_legacy_do_reply_json_c(
	void *closure,
	int status,
	unsigned nreplies,
	struct afb_data * const replies[],
	void (*callback)(void*, struct json_object*, const char*, const char*)
);

/**
 * Interpret the reply given by (status, nreplies, replies) in terms
 * of bindings 1, 2 and 3 (object, error, info) and call the callback
 * with it.
 *
 * @param closure  closure to the callback
 * @param status   status of the reply
 * @param nreplies count of data in array replies
 * @param replies  array of replied data
 * @param callback called callback
 *
 * @return 0 on success or a negative error code
 */
extern
int
afb_json_legacy_do_reply_json_string(
	void *closure,
	int status,
	unsigned nreplies,
	struct afb_data * const replies[],
	void (*callback)(void*, const char*, const char*, const char*)
);

/**
 * Interpret the reply given by (status, nreplies, replies) in terms
 * of bindings 1, 2 and 3 (object, error, info) and return items for
 * synchronous call.
 *
 * @param status   status of the reply
 * @param nreplies count of data in array replies
 * @param replies  array of replied data
 * @param object   pointer to store the extracted json-c object
 * @param error    pointer to store the extracted error string
 * @param info     pointer to store the extracted info string
 *
 * @return 0 on success or a negative error code
 */
extern
int
afb_json_legacy_get_reply_sync(
	int status,
	unsigned nreplies,
	struct afb_data * const replies[],
	struct json_object **object,
	char **error,
	char **info
);

/**********************************************************************/

/**
 * Create a V4 array of data in params using the given v1,2,3
 * object, error and info.
 */
extern
int
afb_json_legacy_make_reply_json_string(
	struct afb_data *params[4],
	const char *object, void (*dobj)(void*), void *cobj,
	const char *error, void (*derr)(void*), void *cerr,
	const char *info, void (*dinf)(void*), void *cinf
);

extern
int
afb_json_legacy_make_reply_json_c(
	struct afb_data *params[4],
	struct json_object *object,
	const char *error, void (*derr)(void*), void *cerr,
	const char *info, void (*dinf)(void*), void *cinf
);

/**
 * Create in params the data of the legacy reply corresponding
 * to the given object, strings and modes.
 *
 * @param params   the parameters to create, an array of at least 4 data
 * @param object   the JSON-C object to return or NULL
 * @param error    the error text if error or NULL
 * @param info     the informative test or NULL
 * @param mode_error  The mode of the error string (static/copy/free)
 * @param mode_info   The mode of the info string (static/copy/free)
 *
 * @return 0 in case of success or an error code negative
 */
extern
int
afb_json_legacy_make_reply_json_c_mode(
	struct afb_data *params[4],
	struct json_object *object,
	const char *error,
	const char *info,
	enum afb_string_mode mode_error,
	enum afb_string_mode mode_info
);

/**********************************************************************/

extern
void
afb_json_legacy_req_reply_hookable(
	struct afb_req_common *comreq,
	struct json_object *obj,
	const char *error,
	const char *info
);

extern
void
afb_json_legacy_req_vreply_hookable(
	struct afb_req_common *comreq,
	struct json_object *obj,
	const char *error,
	const char *fmt,
	va_list args
);

/**********************************************************************/

extern
int
afb_json_legacy_event_rebroadcast_name(
	const char *event,
	struct json_object *obj,
	const rp_uuid_binary_t uuid,
	uint8_t hop
);

extern
int
afb_json_legacy_event_push(
	struct afb_evt *evt,
	struct json_object *obj
);

extern
int
afb_json_legacy_event_push_hookable(
	struct afb_evt *evt,
	struct json_object *obj
);

extern
int
afb_json_legacy_event_broadcast_hookable(
	struct afb_evt *evt,
	struct json_object *obj
);

/**********************************************************************/

/**
 * Get the JSON message compatible with protocol afb-wsj1 from the reply.
 * The returned message string must be freed by the caller.
 *
 * @param message  pointer for storing created reply string
 * @param length   pointer for storing length of created reply string
 * @param status   replied status
 * @param nreplies count of data of the array replies
 * @param replies  array of data of the reply
 *
 * @return 0 on success or a negative error code
 */
extern
int
afb_json_legacy_make_msg_string_reply(
	char **message,
	size_t *length,
	int status,
	unsigned nreplies,
	struct afb_data * const replies[]
);

/**
 * Get the JSON message compatible with protocol afb-wsj1 from the event.
 * The returned message string must be freed by the caller.
 *
 * @param message  pointer for storing created reply string
 * @param length   pointer for storing length of created reply string
 * @param status   replied status
 * @param nparams  count of data of the array params
 * @param params   array of data of the event
 *
 * @return 0 on success or a negative error code
 */
extern
int
afb_json_legacy_make_msg_string_event(
	char **message,
	size_t *length,
	const char *event,
	unsigned nparams,
	struct afb_data * const params[]
);

#endif
