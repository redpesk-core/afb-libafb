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

#include "../libafb-config.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>

#include <json-c/json.h>
#include <rp-utils/rp-verbose.h>

#if !defined(JSON_C_TO_STRING_NOSLASHESCAPE)
#define JSON_C_TO_STRING_NOSLASHESCAPE 0
#endif
#if JSON_C_VERSION_NUM < 0x000D00
static inline
const char *
json_object_to_json_string_length(
	struct json_object *object, int flags, size_t *length
) {
	const char *jsonstr = json_object_to_json_string_ext(object, flags);
	if (length)
		*length = jsonstr ? strlen(jsonstr) : 0;
	return jsonstr;
}
#endif

#include <afb/afb-event-x2.h>
#include <afb/afb-binding-x4.h>
#include <afb/afb-errno.h>

#include "core/afb-session.h"
#include "core/afb-cred.h"
#include "core/afb-apiset.h"
#include "core/afb-evt.h"
#include "core/afb-req-common.h"
#include "core/afb-json-legacy.h"
#include "core/afb-data.h"
#include "core/afb-data-array.h"
#include "core/afb-type.h"
#include "core/afb-type-predefined.h"
#include "core/afb-token.h"
#include "core/afb-error-text.h"
#include "core/afb-sched.h"
#include "utils/u16id.h"
#include "core/containerof.h"
#include "sys/x-errno.h"

#include "rpc/afb-stub-rpc.h"
#include "rpc/afb-rpc-coder.h"
#include "rpc/afb-rpc-decoder.h"

#if !defined(WITH_RPC_V1)
# define WITH_RPC_V1  1
#endif
#if !defined(WITH_RPC_V3)
# define WITH_RPC_V3  1
#endif

#include "rpc/afb-rpc-v0.h"
#if WITH_RPC_V1
# include "rpc/afb-rpc-v1.h"
#endif
#if WITH_RPC_V3
# include "rpc/afb-rpc-v3.h"
#endif

#define USE_ALIAS 1

/**************************************************************************
* PART - MODULE DECLARATIONS
**************************************************************************/

#define ACTIVE_ID_MAX 4095

/**
 * generic structure for received blocks
 */
struct inblock
{
	/** reference count */
	unsigned refcount;

	/** the stub */
	struct afb_stub_rpc *stub;

	/** the memory block, used as chain for free pool */
	void *data;

	/** its size in bytes */
	size_t size;
};

/**
 * structure for a request: requests on server side, the call came in
 */
struct incall
{
	/** the request (in first for efficiency) */
	struct afb_req_common comreq;

	/** the client of the request */
	struct afb_stub_rpc *stub;

	/** messages of the request */
	struct inblock *inblock; /* only for the verb name */

	/** id of the call */
	uint16_t callid;
};

/**
 * structure for a describe request
 */
struct indesc
{
	union {
		/** the client of the request */
		struct afb_stub_rpc *stub;

		/** the next in the pool */
		struct indesc *next;
	} link;

	/** id of the call */
	uint16_t callid;
};

/**
 * Type of out going call
 */
enum outcall_type
{
	/** unset */
	outcall_type_unset,
	/** standard call */
	outcall_type_call,
	/** call for describe */
	outcall_type_describe
};

/**
 * structure for a request: requests on client side, the call went out
 */
struct outcall
{
	/** link to the next */
	struct outcall *next;

	/** id of the request */
	uint16_t id;

	/** type of the call (a outcall_type value) */
	uint8_t type;

	union {
		/** related request */
		struct afb_req_common *comreq;

		/** related describe request */
		struct {
			void (*callback)(void *, struct json_object *);
			void *closure;
		} describe;
	} item;
};

/**
 * structure for waiting version set
 */
struct version_waiter
{
	/** the stub */
	struct afb_stub_rpc   *stub;
	/** link to next waiter */
	struct version_waiter *next;
	/** the lock */
	struct afb_sched_lock *lock;
};

/******************* stub description for client or servers ******************/

/**
 * Structure recording client or server stub linking together
 * AFB internals and RPC protocols
 */
struct afb_stub_rpc
{
	/** count of references */
	unsigned refcount;

	/** version of the protocol */
	uint8_t version;

	/** flag disallowing packing, packing allows to group messages before sending */
	uint8_t unpack;

	/** count of ids */
	uint16_t idcount;

	/** last given id */
	uint16_t idlast;

	/** last given data id */
	uint16_t dataidlast;

	/** apiset for declaration */
	struct afb_apiset *declare_set;

	/** apiset for calling */
	struct afb_apiset *call_set;

	/***************/
	/* server side */
	/***************/

	/** listener for events */
	struct afb_evt_listener *listener;

	/** default remote session */
	struct afb_session *session;

	/** default remote token */
	struct afb_token *token;

#if WITH_CRED
	/** credentials of the client */
	struct afb_cred *cred;
#endif

	/** event from server */
	struct u16id2bool *event_flags;

	/** transmitted sessions */
	struct u16id2ptr *session_proxies;

	/** transmitted tokens */
	struct u16id2ptr *token_proxies;

	/** outgoing calls (and describes) */
	struct outcall *outcalls;

	/***************/
	/* client side */
	/***************/

	/** waiter for version */

	/** event from server */
	struct u16id2ptr *event_proxies;

	/** sent sessions */
	struct u16id2bool *session_flags;

	/** sent tokens */
	struct u16id2bool *token_flags;

	/** free indescs */
	struct indesc *indesc_pool;

	/** waiters for version */
	struct version_waiter *version_waiters;

	afb_rpc_decoder_t decoder;
	afb_rpc_coder_t coder;

	/** group of receive */
	struct {
		/** currently decoded block */
		struct inblock *current_inblock;

		/** bank of free inblocks */
		struct inblock *pool;

		/** dispose in blocks */
		void (*dispose)(void*, void*, size_t);

		/** closure of the dispose function */
		void *closure;
	}
		receive;

	/** group for emiter */
	struct {
		/** notify callback */
		void (*notify)(void*, struct afb_rpc_coder*);

		/** closure of the notify callback */
		void *closure;
	}
		emit;

	/** the default api name or NULL */
	const char *apiname;
};

/**************************************************************************
* PART - UTILITY FUNCTIONS
**************************************************************************/

/******************* jobs *****************/

/**
 * enqueue a job
 */
static int queue_job(void *group, void (*callback)(int signum, void* arg), void *arg)
{
	return afb_sched_post_job(group, 0, 0, callback, arg, Afb_Sched_Mode_Normal);
}

/******************* wait version *****************/
/* TODO: this is not a thread safe implementation */
static void wait_version_cb(int signum, void *closure, struct afb_sched_lock *lock)
{
	struct version_waiter *awaiter = closure;
	struct afb_stub_rpc *stub = awaiter->stub;
	if (signum != 0) {
		struct version_waiter **prv = &stub->version_waiters;
		while (*prv != NULL)
			if ((*prv) != awaiter)
				prv = &(*prv)->next;
			else {
				*prv = awaiter->next;
				break;
			}
		afb_sched_leave(lock);
	}
	else if (stub->version != AFBRPC_PROTO_VERSION_UNSET)
		afb_sched_leave(lock);
	else {
		awaiter->lock = lock;
		awaiter->next = stub->version_waiters;
		stub->version_waiters = awaiter;
	}
}

static int wait_version(struct afb_stub_rpc *stub)
{
	int rc = 0;

	if (stub->version == AFBRPC_PROTO_VERSION_UNSET) {
		struct version_waiter awaiter;
		awaiter.stub = stub;
		awaiter.next = NULL;
		awaiter.lock = NULL;
		rc = afb_sched_sync(0, wait_version_cb, &awaiter);
		if (rc >= 0 && stub->version == AFBRPC_PROTO_VERSION_UNSET)
			rc = X_EBUSY;
	}
	return rc;
}

static void wait_version_unlock(struct version_waiter *waiter)
{
	if (waiter != NULL) {
		wait_version_unlock(waiter->next);
		if (waiter->lock != NULL)
			afb_sched_leave(waiter->lock);
	}
}

static void wait_version_done(struct afb_stub_rpc *stub)
{
	struct version_waiter *head = stub->version_waiters;
	stub->version_waiters = NULL;
	wait_version_unlock(head);
}

/******************* memory *****************/

/**
 * callback for releasing (put) a json objet
 */
static void json_put_cb(void *closure)
{
	json_object_put((struct json_object *)closure);
}

/******************* inblocks *****************/

/** get a fresh new inblock for data of size */
static int inblock_get(struct afb_stub_rpc *stub, void *data, size_t size, struct inblock **inblock)
{
	/* get a fresh */
	struct inblock *result = stub->receive.pool;
	if (result)
		stub->receive.pool = result->data;
	else
		result = malloc(sizeof *result);
	if (result) {
		result->refcount = 1;
		result->stub = afb_stub_rpc_addref(stub);
		result->data = data;
		result->size = size;
	}
	return (*inblock = result) != NULL ? 0 : X_ENOMEM;
}

static struct inblock *inblock_addref(struct inblock *inblock)
{
	inblock->refcount++;
	return inblock;
}

static void inblock_unref(struct inblock *inblock)
{
	if (--inblock->refcount == 0) {
		struct afb_stub_rpc *stub = inblock->stub;
		if (stub->receive.dispose)
			stub->receive.dispose(stub->receive.closure, inblock->data, inblock->size);
		inblock->data = stub->receive.pool;
		stub->receive.pool = inblock;
		afb_stub_rpc_unref(stub);
	}
}

static void inblock_unref_cb(void *closure)
#if USE_ALIAS
	__attribute__((alias("inblock_unref")));
#else
{
	struct  inblock *inblock = closure;
	inblock_unref(inblock);
}
#endif

/******************* indesc *****************/

static struct indesc *indesc_get(struct afb_stub_rpc *stub, uint16_t callid)
{
	struct indesc *result = stub->indesc_pool;
	if (result)
		stub->indesc_pool = result->link.next;
	else
		result = malloc(sizeof *result);
	if (result) {
		result->link.stub = afb_stub_rpc_addref(stub);
		result->callid = callid;
	}
	return result;
}

static void indesc_release(struct indesc *indesc)
{
	struct afb_stub_rpc *stub = indesc->link.stub;
	indesc->link.next = stub->indesc_pool;
	stub->indesc_pool = indesc;
	afb_stub_rpc_unref(stub);
}

/******************* outcalls *****************/

/* create a fresh outcall */
static struct outcall *outcall_alloc(struct afb_stub_rpc *stub)
{
	return malloc(sizeof(struct outcall));
}

/* destroy an outcall */
static void outcall_free(struct afb_stub_rpc *stub, struct outcall *call)
{
	free(call);
}

/* get the outcall of the given id */
static struct outcall *outcall_at(struct afb_stub_rpc *stub, uint16_t id)
{
	struct outcall *it = stub->outcalls;
	while(it && it->id != id)
		it = it->next;
	return it;
}

/* release the given outcall */
static void outcall_release(struct afb_stub_rpc *stub, struct outcall *call)
{
	/* search */
	struct outcall **prv = &stub->outcalls;
	while(*prv) {
		if (*prv != call)
			prv = &(*prv)->next;
		else {
			/* unuse */
			*prv = call->next;
			stub->idcount--;
			/* release */
			outcall_free(stub, call);
			return;
		}
	}
	/* TODO: raise an error! */
}

/* get a new outcall, allocates its id */
static int outcall_get(struct afb_stub_rpc *stub, struct outcall **ocall)
{
	struct outcall *call = NULL;
	uint16_t id;
	int rc;

	if (stub->idcount >= ACTIVE_ID_MAX)
		rc = X_ECANCELED;
	else {
		call = outcall_alloc(stub);
		if (call == NULL)
			rc = X_ENOMEM;
		else {
			stub->idcount++;
			id = stub->idlast;
			do {
				id++;
			} while(!id || outcall_at(stub, id));
			call->id = stub->idlast = id;
			call->type = outcall_type_unset;
			call->next = stub->outcalls;
			stub->outcalls = call;
			rc = 0;
		}
	}
	*ocall = call;
	return rc;
}

/******************* incalls *****************/

/* create a fresh incall */
static struct incall *incall_alloc(struct afb_stub_rpc *stub)
{
	return malloc(sizeof(struct incall));
}

/* destroy an incall */
static void incall_free(struct afb_stub_rpc *stub, struct incall *call)
{
	free(call);
}

/* release the given incall */
static void incall_release(struct afb_stub_rpc *stub, struct incall *call)
{
	afb_stub_rpc_unref(stub);
	incall_free(stub, call);
}

/* get a new incall, allocates its id */
static int incall_get(struct afb_stub_rpc *stub, struct incall **ocall)
{
	int rc;
	struct incall *call = incall_alloc(stub);
	if (call == NULL)
		rc = X_ENOMEM;
	else {
		call->stub = afb_stub_rpc_addref(stub);
		rc = 0;
	}
	*ocall = call;
	return rc;
}

/******************* notify *****************/

static void emit(struct afb_stub_rpc *stub)
{
	if (stub->emit.notify)
		stub->emit.notify(stub->emit.closure, &stub->coder);
}

#if WITH_RPC_V1
/**************************************************************************
* PART - SENDING FOR V1
**************************************************************************/

static int send_session_create_v1(struct afb_stub_rpc *stub, uint16_t id, const char *value)
{
	return afb_rpc_v1_code_session_create(&stub->coder, id, value);
}

static int send_token_create_v1(struct afb_stub_rpc *stub, uint16_t id, const char *value)
{
	return afb_rpc_v1_code_token_create(&stub->coder, id, value);
}

static int send_event_create_v1(struct afb_stub_rpc *stub, uint16_t id, const char *value)
{
	return afb_rpc_v1_code_event_create(&stub->coder, id, value);
}

static int send_event_destroy_v1(struct afb_stub_rpc *stub, uint16_t id)
{
	return afb_rpc_v1_code_event_remove(&stub->coder, id);
}

static int send_event_unexpected_v1(struct afb_stub_rpc *stub, uint16_t id)
{
	return afb_rpc_v1_code_event_unexpected(&stub->coder, id);
}

struct send_v1_cb_data_event_push {
	struct afb_stub_rpc *stub;
	uint16_t eventid;
	int rc;
};

static void send_event_push_v1_cb(void *closure1, struct json_object *object, const void *closure2)
{
	struct send_v1_cb_data_event_push *rd = closure1;
	struct afb_stub_rpc *stub = rd->stub;
	rd->rc = afb_rpc_v1_code_event_push(&stub->coder, rd->eventid, json_object_to_json_string(object));
	if (rd->rc >= 0)
		rd->rc = afb_rpc_coder_on_dispose_output(&stub->coder, json_put_cb, json_object_get(object));
}

static int send_event_push_v1(struct afb_stub_rpc *stub, uint16_t eventid, unsigned nparams, struct afb_data * const params[])
{
	struct send_v1_cb_data_event_push rd;
	int rc;

	rd.stub = stub;
	rd.eventid = eventid;
	rd.rc = X_ECANCELED;
	rc = afb_json_legacy_do2_single_json_c(nparams, params, send_event_push_v1_cb, &rd, 0);
	return rc < 0 ? rc : rd.rc;
}

struct send_v1_cb_data_event_broadcast {
	struct afb_stub_rpc *stub;
	const char *eventname;
	const unsigned char *uuid;
	uint8_t hop;
	int rc;
};

static void send_event_broadcast_v1_cb(void *closure1, struct json_object *object, const void *closure2)
{
	struct send_v1_cb_data_event_broadcast *rd = closure1;
	struct afb_stub_rpc *stub = rd->stub;
	rd->rc = afb_rpc_v1_code_event_broadcast(&stub->coder, rd->eventname, json_object_to_json_string(object), rd->uuid, rd->hop);
	if (rd->rc >= 0)
		rd->rc = afb_rpc_coder_on_dispose_output(&stub->coder, json_put_cb, json_object_get(object));
}

static int send_event_broadcast_v1(struct afb_stub_rpc *stub, const char *eventname, unsigned nparams, struct afb_data * const params[], const unsigned char uuid[16], uint8_t hop)
{
	struct send_v1_cb_data_event_broadcast rd;
	int rc;

	rd.stub = stub;
	rd.eventname = eventname;
	rd.uuid = uuid;
	rd.hop = hop;
	rd.rc = X_ECANCELED;
	rc = afb_json_legacy_do2_single_json_c(
			nparams, params,
			send_event_broadcast_v1_cb, &rd, 0);
	return rc < 0 ? rc : rd.rc;
}

static int send_event_subscribe_v1(struct afb_stub_rpc *stub, uint16_t callid, uint16_t eventid)
{
	return afb_rpc_v1_code_subscribe(&stub->coder, callid, eventid);
}

static int send_event_unsubscribe_v1(struct afb_stub_rpc *stub, uint16_t callid, uint16_t eventid)
{
	return afb_rpc_v1_code_unsubscribe(&stub->coder, callid, eventid);
}

struct send_v1_cb_data_call_reply {
	struct afb_stub_rpc *stub;
	uint16_t callid;
	int rc;
};

static void send_call_reply_v1_cb(void *closure, struct json_object *object, const char *error, const char *info)
{
	struct send_v1_cb_data_call_reply *rd = closure;
	struct afb_stub_rpc *stub = rd->stub;
	const char *jstr;
	size_t length;

	jstr = json_object_to_json_string_length(object, 0, &length);
	rd->rc = afb_rpc_v1_code_reply(&stub->coder,  rd->callid, jstr, (uint32_t)(length + 1), error, info);
	if (rd->rc >= 0)
		afb_rpc_coder_on_dispose_output(&stub->coder, json_put_cb, json_object_get(object));
}

static int send_call_reply_v1(struct afb_stub_rpc *stub, int status, unsigned nreplies, struct afb_data * const replies[], uint16_t callid)
{
	struct send_v1_cb_data_call_reply rd;
	int rc;

	rd.stub = stub;
	rd.callid = callid;
	rd.rc = X_ECANCELED;
	rc = afb_json_legacy_do_reply_json_c(&rd, status, nreplies, replies, send_call_reply_v1_cb);
	return rc < 0 ? rc : rd.rc;
}

struct send_v1_cb_data_call_request {
	struct afb_stub_rpc *stub;
	uint16_t callid;
	uint16_t sessionid;
	uint16_t tokenid;
	const char *verbname;
	const char *usrcreds;
	int rc;
};

static void send_call_request_v1_cb(void *closure1, struct json_object *object, const void *closure2)
{
	struct send_v1_cb_data_call_request *rd = closure1;
	struct afb_stub_rpc *stub = rd->stub;
	const char *jstr;
	size_t length;

	jstr = json_object_to_json_string_length(object, 0, &length);
	rd->rc = afb_rpc_v1_code_call(&stub->coder, rd->callid, rd->verbname, jstr, (uint32_t)(length + 1), rd->sessionid, rd->tokenid, rd->usrcreds);
	if (rd->rc >= 0)
		afb_rpc_coder_on_dispose_output(&stub->coder, json_put_cb, json_object_get(object));
}

static int send_call_request_v1(
	struct afb_stub_rpc *stub,
	uint16_t callid,
	uint16_t sessionid,
	uint16_t tokenid,
	const char *verbname,
	const char *usrcreds,
	unsigned nparams,
	struct afb_data * const params[])
{
	struct send_v1_cb_data_call_request rd;
	int rc;

	rd.stub = stub;
	rd.callid = callid;
	rd.sessionid = sessionid;
	rd.tokenid = tokenid;
	rd.verbname = verbname;
	rd.usrcreds = usrcreds;
	rd.rc = X_ECANCELED;
	rc = afb_json_legacy_do2_single_json_c(nparams, params, send_call_request_v1_cb, &rd, 0);
	return rc < 0 ? rc : rd.rc;
}

static int send_describe_request_v1(struct afb_stub_rpc *stub, uint16_t callid)
{
	return afb_rpc_v1_code_describe(&stub->coder, callid);
}

static int send_describe_reply_v1(struct afb_stub_rpc *stub, uint16_t callid, const char *description)
{
	return afb_rpc_v1_code_description(&stub->coder, callid, description);
}

#endif
#if WITH_RPC_V3
/**************************************************************************
* PART - SENDING FOR V3
**************************************************************************/

static int datas_to_values_v3(unsigned ndata, struct afb_data * const datas[], afb_rpc_v3_value_t values[])
{
	unsigned i;
	void *cptr;
	size_t size;
	uint16_t typenum;
	struct afb_data *data;

	for (i = 0 ; i < ndata ; i++) {
		data = datas[i];
		typenum = afb_typeid(afb_data_type(data));
		afb_data_get_constant(data, &cptr, &size);
		switch (typenum) {
		case Afb_Typeid_Predefined_Opaque:
			typenum = AFB_RPC_V3_ID_TYPE_OPAQUE;
			break;
		case Afb_Typeid_Predefined_Bytearray:
			typenum = AFB_RPC_V3_ID_TYPE_BYTEARRAY;
			break;
		case Afb_Typeid_Predefined_Stringz:
			typenum = AFB_RPC_V3_ID_TYPE_STRINGZ;
			break;
		case Afb_Typeid_Predefined_Json_C:
			cptr = (void*)json_object_to_json_string_length((json_object*)cptr, 0, &size);
			size++;
			/*@fallthrough@*/
		case Afb_Typeid_Predefined_Json:
			typenum = AFB_RPC_V3_ID_TYPE_JSON;
			break;
		case Afb_Typeid_Predefined_Bool:
			typenum = AFB_RPC_V3_ID_TYPE_BOOL;
			break;
		case Afb_Typeid_Predefined_I8:
			typenum = AFB_RPC_V3_ID_TYPE_I8; /* TODO not a predefined type */
			break;
		case Afb_Typeid_Predefined_U8:
			typenum = AFB_RPC_V3_ID_TYPE_U8; /* TODO not a predefined type */
			break;
		case Afb_Typeid_Predefined_I16:
		case Afb_Typeid_Predefined_U16:
		case Afb_Typeid_Predefined_I32:
		case Afb_Typeid_Predefined_U32:
		case Afb_Typeid_Predefined_I64:
		case Afb_Typeid_Predefined_U64:
		case Afb_Typeid_Predefined_Float:
		case Afb_Typeid_Predefined_Double:
		default:
			typenum = 0; /* TODO */
		}
		if (size > UINT16_MAX - 8)
			return X_EOVERFLOW;
		values[i].id = typenum;
		values[i].data = cptr ? cptr : &values[i].data;
		values[i].length = (uint16_t)size;
	}
	return 0;
}

static void dispose_dataids_v3(void *closure, void *arg)
{
	afb_data_array_unref((unsigned)(intptr_t)closure, (struct afb_data**)arg);
}

static int send_resource_create_v3(struct afb_stub_rpc *stub, uint16_t id, const char *value, uint16_t kind)
{
	afb_rpc_v3_msg_resource_create_t creres;

	creres.kind = kind;
	creres.id = id;
	creres.data = (void*)value;
	creres.length = value ? (uint16_t)(1 + strlen(value)) : 0; /* TODO check size */
	return afb_rpc_v3_code_resource_create(&stub->coder, &creres);
}

static int send_resource_destroy_v3(struct afb_stub_rpc *stub, uint16_t id, uint16_t kind)
{
	afb_rpc_v3_msg_resource_destroy_t desres;

	desres.kind = kind;
	desres.id = id;
	return afb_rpc_v3_code_resource_destroy(&stub->coder, &desres);
}

static int send_session_create_v3(struct afb_stub_rpc *stub, uint16_t id, const char *value)
{
	return send_resource_create_v3(stub, id, value, AFB_RPC_V3_ID_KIND_SESSION);
}

static int send_token_create_v3(struct afb_stub_rpc *stub, uint16_t id, const char *value)
{
	return send_resource_create_v3(stub, id, value, AFB_RPC_V3_ID_KIND_TOKEN);
}

static int send_event_create_v3(struct afb_stub_rpc *stub, uint16_t id, const char *value)
{
	return send_resource_create_v3(stub, id, value, AFB_RPC_V3_ID_KIND_EVENT);
}

static int send_event_destroy_v3(struct afb_stub_rpc *stub, uint16_t id)
{
	return send_resource_destroy_v3(stub, id, AFB_RPC_V3_ID_KIND_EVENT);
}

static int send_event_unexpected_v3(struct afb_stub_rpc *stub, uint16_t id)
{
	afb_rpc_v3_msg_event_unexpected_t msg = { .eventid = id };
	return afb_rpc_v3_code_event_unexpected(&stub->coder, &msg);
}

static int send_event_push_v3(struct afb_stub_rpc *stub, uint16_t eventid, unsigned nparams, struct afb_data * const params[])
{
	afb_rpc_v3_msg_event_push_t push;
	afb_rpc_v3_value_t values[nparams];
	afb_rpc_v3_value_array_t valarr = { .count = (uint16_t)nparams, .values = values };;

	int rc = datas_to_values_v3(nparams, params, values);
	if (rc >= 0) {
		push.eventid = eventid;
		rc = afb_rpc_v3_code_event_push(&stub->coder, &push, &valarr);
		if (rc >= 0) {
			afb_data_array_addref(nparams, params);
			afb_rpc_coder_on_dispose2_output(&stub->coder, dispose_dataids_v3, (void*)(intptr_t)nparams, (void*)params);
		}
	}
	return rc;
}

static int send_event_broadcast_v3(struct afb_stub_rpc *stub, const char *eventname, unsigned nparams, struct afb_data * const params[], const unsigned char uuid[16], uint8_t hop)
{
/* TODO */return 0;
}

static int send_event_subscribe_v3(struct afb_stub_rpc *stub, uint16_t callid, uint16_t eventid)
{
	afb_rpc_v3_msg_event_subscribe_t sub;
	sub.callid = callid;
	sub.eventid = eventid;
	return afb_rpc_v3_code_event_subscribe(&stub->coder, &sub);
}

static int send_event_unsubscribe_v3(struct afb_stub_rpc *stub, uint16_t callid, uint16_t eventid)
{
	afb_rpc_v3_msg_event_unsubscribe_t sub;
	sub.callid = callid;
	sub.eventid = eventid;
	return afb_rpc_v3_code_event_unsubscribe(&stub->coder, &sub);
}

static int send_call_reply_v3(struct afb_stub_rpc *stub, int status, unsigned nparams, struct afb_data * const params[], uint16_t callid)
{
	afb_rpc_v3_msg_call_reply_t reply;
	afb_rpc_v3_value_t values[nparams];
	afb_rpc_v3_value_array_t valarr = { .count = (uint16_t)nparams, .values = values };;

	int rc = datas_to_values_v3(nparams, params, values);
	if (rc >= 0) {
		reply.callid = callid;
		reply.status = (int32_t)status;
		rc = afb_rpc_v3_code_call_reply(&stub->coder, &reply, &valarr);
		if (rc >= 0) {
			afb_data_array_addref(nparams, params);
			afb_rpc_coder_on_dispose2_output(&stub->coder, dispose_dataids_v3, (void*)(intptr_t)nparams, (void*)params);
		}
	}
	return rc;
}

static int send_call_request_v3(
	struct afb_stub_rpc *stub,
	uint16_t callid,
	uint16_t sessionid,
	uint16_t tokenid,
	const char *verbname,
	const char *usrcreds,
	unsigned nparams,
	struct afb_data * const params[])
{
	afb_rpc_v3_msg_call_request_t request;
	afb_rpc_v3_value_t values[nparams];
	afb_rpc_v3_value_array_t valarr = { .count = (uint16_t)nparams, .values = values };;

	int rc = datas_to_values_v3(nparams, params, values);
	if (rc >= 0) {
		memset(&request, 0, sizeof request);
		request.callid = callid;
		request.verb.data = verbname;
		request.verb.length = (uint16_t)(1 + strlen(verbname));
		request.session.id = sessionid;
		request.token.id = tokenid;
		request.creds.length = usrcreds ? (uint16_t)(1 + strlen(usrcreds)) : 0;
		rc = afb_rpc_v3_code_call_request(&stub->coder, &request, &valarr);
		if (rc >= 0) {
			afb_data_array_addref(nparams, params);
			afb_rpc_coder_on_dispose2_output(&stub->coder, dispose_dataids_v3, (void*)(intptr_t)nparams, (void*)params);
		}
	}
	return rc;
}

static int send_describe_request_v3(struct afb_stub_rpc *stub, uint16_t callid)
{
	afb_rpc_v3_msg_call_request_t request;

	memset(&request, 0, sizeof request);
	request.callid = callid;
	request.verb.id = AFB_RPC_V3_ID_VERB_DESCRIBE;
	return afb_rpc_v3_code_call_request(&stub->coder, &request, NULL);
}

static int send_describe_reply_v3(struct afb_stub_rpc *stub, uint16_t callid, const char *description)
{
	return afb_rpc_v1_code_description(&stub->coder, callid, description);
	afb_rpc_v3_value_t value;
	afb_rpc_v3_value_array_t valarr = { .count = 1, .values = &value };;
	afb_rpc_v3_msg_call_reply_t reply;

	value.id = 0;
	value.data = description;
	value.length = description ? (uint16_t)(1 + strlen(description)) : 0;
	reply.callid = callid;
	reply.status = 0;
	return afb_rpc_v3_code_call_reply(&stub->coder, &reply, &valarr);
}
#endif

/**************************************************************************
* PART - SENDING FOR ANY VERSION
**************************************************************************/

static int send_session_create(struct afb_stub_rpc *stub, uint16_t id, const char *value)
{
	switch (stub->version) {
#if WITH_RPC_V1
	case AFBRPC_PROTO_VERSION_1:
		return send_session_create_v1(stub, id, value);
#endif
#if WITH_RPC_V3
	case AFBRPC_PROTO_VERSION_3:
		return send_session_create_v3(stub, id, value);
#endif
	case AFBRPC_PROTO_VERSION_UNSET:
		return wait_version(stub) ?: send_session_create(stub, id, value);
	default:
		return X_ENOTSUP;
	}
}

static int send_token_create(struct afb_stub_rpc *stub, uint16_t id, const char *value)
{
	switch (stub->version) {
#if WITH_RPC_V1
	case AFBRPC_PROTO_VERSION_1:
		return send_token_create_v1(stub, id, value);
#endif
#if WITH_RPC_V3
	case AFBRPC_PROTO_VERSION_3:
		return send_token_create_v3(stub, id, value);
#endif
	case AFBRPC_PROTO_VERSION_UNSET:
		return wait_version(stub) ?: send_token_create(stub, id, value);
	default:
		return X_ENOTSUP;
	}
}

static int send_event_create(struct afb_stub_rpc *stub, uint16_t id, const char *value)
{
	switch (stub->version) {
#if WITH_RPC_V1
	case AFBRPC_PROTO_VERSION_1:
		return send_event_create_v1(stub, id, value);
#endif
#if WITH_RPC_V3
	case AFBRPC_PROTO_VERSION_3:
		return send_event_create_v3(stub, id, value);
#endif
	case AFBRPC_PROTO_VERSION_UNSET:
		return wait_version(stub) ?: send_event_create(stub, id, value);
	default:
		return X_ENOTSUP;
	}
}

static int send_event_destroy(struct afb_stub_rpc *stub, uint16_t id)
{
	switch (stub->version) {
#if WITH_RPC_V1
	case AFBRPC_PROTO_VERSION_1:
		return send_event_destroy_v1(stub, id);
#endif
#if WITH_RPC_V3
	case AFBRPC_PROTO_VERSION_3:
		return send_event_destroy_v3(stub, id);
#endif
	case AFBRPC_PROTO_VERSION_UNSET:
		return wait_version(stub) ?: send_event_destroy(stub, id);
	default:
		return X_ENOTSUP;
	}
}

static int send_event_unexpected(struct afb_stub_rpc *stub, uint16_t eventid)
{
	switch (stub->version) {
#if WITH_RPC_V1
	case AFBRPC_PROTO_VERSION_1:
		return send_event_unexpected_v1(stub, eventid);
#endif
#if WITH_RPC_V3
	case AFBRPC_PROTO_VERSION_3:
		return send_event_unexpected_v3(stub, eventid);
#endif
	case AFBRPC_PROTO_VERSION_UNSET:
		return wait_version(stub) ?: send_event_unexpected(stub, eventid);
	default:
		return X_ENOTSUP;
	}
}

static int send_event_push(struct afb_stub_rpc *stub, uint16_t eventid, unsigned nparams, struct afb_data * const params[])
{
	switch (stub->version) {
#if WITH_RPC_V1
	case AFBRPC_PROTO_VERSION_1:
		return send_event_push_v1(stub, eventid, nparams, params);
#endif
#if WITH_RPC_V3
	case AFBRPC_PROTO_VERSION_3:
		return send_event_push_v3(stub, eventid, nparams, params);
#endif
	case AFBRPC_PROTO_VERSION_UNSET:
		return wait_version(stub) ?: send_event_push(stub, eventid, nparams, params);
	default:
		return X_ENOTSUP;
	}
}

static int send_event_broadcast(struct afb_stub_rpc *stub, const char *eventname, unsigned nparams, struct afb_data * const params[], const unsigned char uuid[16], uint8_t hop)
{
	switch (stub->version) {
#if WITH_RPC_V1
	case AFBRPC_PROTO_VERSION_1:
		return send_event_broadcast_v1(stub, eventname, nparams, params, uuid, hop);
#endif
#if WITH_RPC_V3
	case AFBRPC_PROTO_VERSION_3:
		return send_event_broadcast_v3(stub, eventname, nparams, params, uuid, hop);
#endif
	case AFBRPC_PROTO_VERSION_UNSET:
		return wait_version(stub) ?: send_event_broadcast(stub, eventname, nparams, params, uuid, hop);
	default:
		return X_ENOTSUP;
	}
}

static int send_event_subscribe(struct afb_stub_rpc *stub, uint16_t callid, uint16_t eventid)
{
	switch (stub->version) {
#if WITH_RPC_V1
	case AFBRPC_PROTO_VERSION_1:
		return send_event_subscribe_v1(stub, callid, eventid);
#endif
#if WITH_RPC_V3
	case AFBRPC_PROTO_VERSION_3:
		return send_event_subscribe_v3(stub, callid, eventid);
#endif
	case AFBRPC_PROTO_VERSION_UNSET:
		return wait_version(stub) ?: send_event_subscribe(stub, callid, eventid);
	default:
		return X_ENOTSUP;
	}
}

static int send_event_unsubscribe(struct afb_stub_rpc *stub, uint16_t callid, uint16_t eventid)
{
	switch (stub->version) {
#if WITH_RPC_V1
	case AFBRPC_PROTO_VERSION_1:
		return send_event_unsubscribe_v1(stub, callid, eventid);
#endif
#if WITH_RPC_V3
	case AFBRPC_PROTO_VERSION_3:
		return send_event_unsubscribe_v3(stub, callid, eventid);
#endif
	case AFBRPC_PROTO_VERSION_UNSET:
		return wait_version(stub) ?: send_event_unsubscribe(stub, callid, eventid);
	default:
		return X_ENOTSUP;
	}
}

static int send_call_reply(struct afb_stub_rpc *stub, int status, unsigned nreplies, struct afb_data * const replies[], uint16_t callid)
{
	switch (stub->version) {
#if WITH_RPC_V1
	case AFBRPC_PROTO_VERSION_1:
		return send_call_reply_v1(stub, status, nreplies, replies, callid);
#endif
#if WITH_RPC_V3
	case AFBRPC_PROTO_VERSION_3:
		return send_call_reply_v3(stub, status, nreplies, replies, callid);
#endif
	case AFBRPC_PROTO_VERSION_UNSET:
		return wait_version(stub) ?: send_call_reply(stub, status, nreplies, replies, callid);
	default:
		return X_ENOTSUP;
	}
}

static int send_call_request(
	struct afb_stub_rpc *stub,
	uint16_t callid,
	uint16_t sessionid,
	uint16_t tokenid,
	const char *verbname,
	const char *usrcreds,
	unsigned nparams,
	struct afb_data * const params[])
{
	switch (stub->version) {
#if WITH_RPC_V1
	case AFBRPC_PROTO_VERSION_1:
		return send_call_request_v1(stub, callid, sessionid, tokenid, verbname, usrcreds, nparams, params);
#endif
#if WITH_RPC_V3
	case AFBRPC_PROTO_VERSION_3:
		return send_call_request_v3(stub, callid, sessionid, tokenid, verbname, usrcreds, nparams, params);
#endif
	case AFBRPC_PROTO_VERSION_UNSET:
		return wait_version(stub) ?: send_call_request(stub, callid, sessionid, tokenid, verbname, usrcreds, nparams, params);
	default:
		return X_ENOTSUP;
	}
}

static int send_describe_request(struct afb_stub_rpc *stub, uint16_t callid)
{
	switch (stub->version) {
#if WITH_RPC_V1
	case AFBRPC_PROTO_VERSION_1:
		return send_describe_request_v1(stub, callid);
#endif
#if WITH_RPC_V3
	case AFBRPC_PROTO_VERSION_3:
		return send_describe_request_v3(stub, callid);
#endif
	case AFBRPC_PROTO_VERSION_UNSET:
		return wait_version(stub) ?: send_describe_request(stub, callid);
	default:
		return X_ENOTSUP;
	}
}

static int send_describe_reply(struct afb_stub_rpc *stub, uint16_t callid, const char *description)
{
	switch (stub->version) {
#if WITH_RPC_V1
	case AFBRPC_PROTO_VERSION_1:
		return send_describe_reply_v1(stub, callid, description);;
#endif
#if WITH_RPC_V3
	case AFBRPC_PROTO_VERSION_3:
		return send_describe_reply_v3(stub, callid, description);;
#endif
	case AFBRPC_PROTO_VERSION_UNSET:
		return wait_version(stub) ?: send_describe_reply(stub, callid, description);
	default:
		return X_ENOTSUP;
	}
}

/**************************************************************************
* PART - EVENT LISTENER
**************************************************************************/

static void on_listener_event_add_cb(void *closure, const char *event, uint16_t eventid)
{
	struct afb_stub_rpc *stub = closure;
	int rc;

	rc = u16id2bool_set(&stub->event_flags, eventid, 1);
	if (rc == 0) {
		rc = send_event_create(stub, eventid, event);
		if (rc < 0)
			u16id2bool_set(&stub->event_flags, eventid, 0);
		emit(stub);
	}
}

static void on_listener_event_remove_cb(void *closure, const char *event, uint16_t eventid)
{
	struct afb_stub_rpc *stub = closure;

	if (u16id2bool_set(&stub->event_flags, eventid, 0)) {
		send_event_destroy(stub, eventid);
		emit(stub);
	}
}

static void on_listener_event_push_cb(void *closure, const struct afb_evt_pushed *event)
{
	struct afb_stub_rpc *stub = closure;

	if (u16id2bool_get(stub->event_flags, event->data.eventid)) {
		send_event_push(stub, event->data.eventid, event->data.nparams, event->data.params);
		emit(stub);
	}
}

static void on_listener_event_broadcast_cb(void *closure, const struct afb_evt_broadcasted *event)
{
	struct afb_stub_rpc *stub = closure;
	send_event_broadcast(stub, event->data.name, event->data.nparams, event->data.params, event->uuid, event->hop);
	emit(stub);
}

static const struct afb_evt_itf server_event_itf = {
	.broadcast = on_listener_event_broadcast_cb,
	.push = on_listener_event_push_cb,
	.add = on_listener_event_add_cb,
	.remove = on_listener_event_remove_cb
};

static int ensure_listener(struct afb_stub_rpc *stub)
{
	if (stub->listener == NULL) {
		stub->listener = afb_evt_listener_create(&server_event_itf, stub, stub);
		if (stub->listener == NULL)
			return X_ENOMEM;
	}
	return 0;
}

/**************************************************************************
* PART - HANDLING OF INCOMING REQUESTS
**************************************************************************/

/* decrement the reference count of the request and free/release it on falling to null */
static void incall_destroy_cb(struct afb_req_common *comreq)
{
	struct incall *incall = containerof(struct incall, comreq, comreq);
	struct afb_stub_rpc *stub = incall->stub;
	inblock_unref(incall->inblock);
	afb_req_common_cleanup(comreq);
	afb_stub_rpc_unref(stub);
	incall_release(stub, incall);
}

static void incall_reply_cb(struct afb_req_common *comreq, int status, unsigned nreplies, struct afb_data * const replies[])
{
	struct incall *req = containerof(struct incall, comreq, comreq);
	struct afb_stub_rpc *stub = req->stub;
	int rc = send_call_reply(stub, status, nreplies, replies, req->callid);
	if (rc < 0)
		RP_ERROR("error while sending reply");
	emit(stub);
}

static int incall_subscribe_cb(struct afb_req_common *comreq, struct afb_evt *evt)
{
	struct incall *req = containerof(struct incall, comreq, comreq);
	struct afb_stub_rpc *stub = req->stub;
	int rc = ensure_listener(stub);
	if (rc >= 0)
		rc = afb_evt_listener_watch_evt(stub->listener, evt);
	if (rc >= 0)
		rc = send_event_subscribe(stub, req->callid, afb_evt_id(evt));
	if (rc < 0)
		RP_ERROR("error while subscribing event");
	emit(stub);
	return rc;
}

static int incall_unsubscribe_cb(struct afb_req_common *comreq, struct afb_evt *evt)
{
	struct incall *req = containerof(struct incall, comreq, comreq);
	struct afb_stub_rpc *stub = req->stub;
	int rc = afb_evt_listener_unwatch_evt(stub->listener, evt);
	int rc2 = send_event_unsubscribe(stub, req->callid, afb_evt_id(evt));
	if (rc >= 0 && rc2 < 0)
		rc = rc2;
	if (rc < 0)
		RP_ERROR("error while unsubscribing event");
	emit(stub);
	return rc;
}

static const struct afb_req_common_query_itf incall_common_itf = {
	.reply = incall_reply_cb,
	.unref = incall_destroy_cb,
	.subscribe = incall_subscribe_cb,
	.unsubscribe = incall_unsubscribe_cb,
	.interface = NULL
};

/**************************************************************************
* PART - HANDLING OF API PROXY
**************************************************************************/

static int make_session_id(struct afb_stub_rpc *stub, struct afb_session *session, uint16_t *id)
{
	int rc, rc2;
	uint16_t sid;

	rc = 0;

	/* get the session */
	if (!session)
		sid = 0;
	else {
		sid = afb_session_id(session);
		rc2 = u16id2bool_set(&stub->session_flags, sid, 1);
		if (rc2 < 0)
			rc = rc2;
		else if (rc2 == 0) {
			rc = send_session_create(stub, sid, afb_session_uuid(session));
			if (rc >= 0 && stub->unpack)
				emit(stub);
		}
	}

	*id = sid;
	return rc;
}

static int make_token_id(struct afb_stub_rpc *stub, struct afb_token *token, uint16_t *id)
{
	int rc, rc2;
	uint16_t tid;

	rc = 0;

	/* get the token */
	if (!token)
		tid = 0;
	else {
		tid = afb_token_id(token);
		rc2 = u16id2bool_set(&stub->token_flags, tid, 1);
		if (rc2 < 0)
			rc = rc2;
		else if (rc2 == 0) {
			rc = send_token_create(stub, tid, afb_token_string(token));
			if (rc >= 0 && stub->unpack)
				emit(stub);
		}
	}

	*id = tid;
	return rc;
}

static int client_make_ids(struct afb_stub_rpc *stub, struct afb_req_common *comreq, uint16_t *sessionid, uint16_t *tokenid)
{
	int rcs = make_session_id(stub, comreq->session, sessionid);
	int rct = make_token_id(stub, comreq->token, tokenid);
	return rcs < 0 ? rcs : rct;
}

static void api_process_cb(void * closure, struct afb_req_common *comreq)
{
	struct afb_stub_rpc *stub = closure;

	struct outcall *call;
	const char *ucreds;
	uint16_t sessionid;
	uint16_t tokenid;
	int rc;

	rc = outcall_get(stub, &call);
	if (rc >= 0) {
		call->type = outcall_type_call;
		call->item.comreq = afb_req_common_addref(comreq);
		rc = client_make_ids(stub, comreq, &sessionid, &tokenid);
		if (rc >= 0) {
			ucreds = afb_req_common_on_behalf_cred_export(comreq);
			rc = send_call_request(stub, call->id, sessionid, tokenid,
					comreq->verbname, ucreds,
					comreq->params.ndata, comreq->params.data);
			emit(stub);
		}
		if (rc < 0)
			outcall_release(stub, call);
	}
	if (rc < 0)
		afb_req_common_reply_unavailable_error_hookable(comreq);
}

/* get the description */
static void api_describe_cb(void * closure, void (*describecb)(void *, struct json_object *), void *clocb)
{
	struct afb_stub_rpc *stub = closure;

	struct outcall *call;
	int rc;

	rc = outcall_get(stub, &call);
	if (rc >= 0) {
		call->type = outcall_type_describe;
		call->item.describe.callback = describecb;
		call->item.describe.closure = clocb;
		rc = send_describe_request(stub, call->id);
		if (rc < 0)
			outcall_release(stub, call);
		emit(stub);
	}
	if (rc < 0)
		describecb(clocb, NULL);
}

static struct afb_api_itf stub_api_itf = {
	.process = api_process_cb,
	.describe = api_describe_cb
};

/**************************************************************************
* PART - PROCESS INCOMING MESSAGES FROM ANY VERSION
**************************************************************************/

static void describe_reply_data(struct outcall *outcall, unsigned ndata, struct afb_data *data[]);

static int add_session(struct afb_stub_rpc *stub, uint16_t sessionid, const char *sessionstr, struct afb_session **psess)
{
	struct afb_session *session;
	int rc, created;

	rc = afb_session_get(&session, sessionstr, AFB_SESSION_TIMEOUT_DEFAULT, &created);
	if (rc < 0)
		RP_ERROR("can't create session %s", sessionstr);
	else {
		afb_session_set_autoclose(session, 1);
		rc = u16id2ptr_add(&stub->session_proxies, sessionid, session);
		if (rc < 0) {
			RP_ERROR("can't record session %s", sessionstr);
			afb_session_unref(session);
			session = NULL;
		}
	}
	if (psess)
		*psess = session;
	return rc;
}

static int receive_call_request(
	struct afb_stub_rpc *stub,
	uint16_t callid,
	const char *api,
	const char *verb,
	unsigned ndata,
	struct afb_data *data[],
	uint16_t sessionid,
	uint16_t tokenid,
	const char *user_creds
) {
	struct incall *incall;
	struct afb_session *session;
	struct afb_token *token;
	int rc;
	int err;

	afb_stub_rpc_addref(stub);

	/* get api */
	if (api == NULL) {
		api = stub->apiname;
		if (api == NULL)
			goto invalid;
	}

	/* get session */
	if (sessionid == 0) {
		session = stub->session;
		if (session == NULL) {
			rc = afb_session_get(&session, NULL, AFB_SESSION_TIMEOUT_DEFAULT, &err);
			if (rc < 0) {
				RP_ERROR("can't create new session");
				goto out_of_memory;
			}
			stub->session = session;
		}
	}
	else {
		rc = u16id2ptr_get(stub->session_proxies, sessionid, (void**)&session);
		if (rc < 0)
			goto invalid;
	}

	/* get token */
	if (tokenid == 0)
		token = stub->token;
	else if (u16id2ptr_get(stub->token_proxies, tokenid, (void**)&token) < 0)
		goto invalid;

	/* create the request */
	rc = incall_get(stub, &incall);
	if (rc < 0)
		goto out_of_memory;

	/* initialise */
	incall->inblock = inblock_addref(stub->receive.current_inblock);
	incall->callid = callid;
	afb_req_common_init(&incall->comreq, &incall_common_itf, api, verb, ndata, data, stub);
	afb_req_common_set_session(&incall->comreq, session);
	afb_req_common_set_token(&incall->comreq, token);
#if WITH_CRED
	afb_req_common_set_cred(&incall->comreq, stub->cred);
#endif
	afb_req_common_process_on_behalf(&incall->comreq, incall->stub->call_set, user_creds);
	return 0;

out_of_memory:
	err = AFB_ERRNO_OUT_OF_MEMORY;
	goto error;
invalid:
	err = AFB_ERRNO_INVALID_REQUEST;
error:
	afb_data_array_unref(ndata, data);
	send_call_reply(stub, err, 0, 0, callid);
	afb_stub_rpc_unref(stub);
	return X_ECANCELED;
}

static int receive_call_reply(
	struct afb_stub_rpc *stub,
	uint16_t callid,
	int32_t status,
	unsigned ndata,
	struct afb_data *data[]
) {
	int rc;
	struct outcall *outcall = outcall_at(stub, callid);

	if (!outcall) {
		/* unexpected reply */
		RP_ERROR("no call of id %d for the reply", (int)callid);
		rc = X_EPROTO;
	}
	else {
		switch(outcall->type) {
		case outcall_type_call:
			afb_req_common_reply_hookable(outcall->item.comreq, status, ndata, data);
			afb_req_common_unref(outcall->item.comreq);
			break;
		case outcall_type_describe:
			describe_reply_data(outcall, ndata, data);
			break;
		}
		outcall_release(stub, outcall);
		rc = 0;
	}
	return rc;
}

static int receive_session_create(struct afb_stub_rpc *stub, uint16_t sessionid, const char *sessionstr)
{
	return add_session(stub, sessionid, sessionstr, NULL);
}

static int receive_session_destroy(struct afb_stub_rpc *stub, uint16_t sessionid)
{
	struct afb_session *session;
	int rc;

	rc = u16id2ptr_drop(&stub->session_proxies, sessionid, (void**)&session);
	if (rc == 0 && session)
		afb_session_unref(session);
	return rc;
}

static int receive_token_create(struct afb_stub_rpc *stub, uint16_t tokenid, const char *tokenstr)
{
	struct afb_token *token;
	int rc;

	rc = afb_token_get(&token, tokenstr);
	if (rc < 0)
		RP_ERROR("can't create token %s, out of memory", tokenstr);
	else {
		rc = u16id2ptr_add(&stub->token_proxies, tokenid, token);
		if (rc < 0) {
			RP_ERROR("can't record token %s", tokenstr);
			afb_token_unref(token);
		}
	}
	return rc;
}

static int receive_token_destroy(struct afb_stub_rpc *stub, uint16_t tokenid)
{
	struct afb_token *token;
	int rc;

	rc = u16id2ptr_drop(&stub->token_proxies, tokenid, (void**)&token);
	if (rc == 0 && token)
		afb_token_unref(token);
	return rc;
}

static int receive_event_create(struct afb_stub_rpc *stub, uint16_t eventid, const char *event_name)
{
	struct afb_evt *event;
	int rc;

	/* check conflicts */
	rc = afb_evt_create(&event, event_name);
	if (rc < 0)
		RP_ERROR("can't create event %s, out of memory", event_name);
	else {
		rc = u16id2ptr_add(&stub->event_proxies, eventid, event);
		if (rc < 0) {
			RP_ERROR("can't record event %s", event_name);
			afb_evt_unref(event);
		}
	}
	return rc;
}

static int receive_event_destroy(struct afb_stub_rpc *stub, uint16_t eventid)
{
	struct afb_evt *event;
	int rc;

	rc = u16id2ptr_drop(&stub->event_proxies, eventid, (void**)&event);
	if (rc == 0 && event)
		afb_evt_unref(event);
	return rc;
}

static int receive_event_unexpected(struct afb_stub_rpc *stub, uint16_t eventid)
{
	return stub->listener ? afb_evt_listener_unwatch_id(stub->listener, eventid) : 0;
}

static int receive_event_subscription(struct afb_stub_rpc *stub, uint16_t callid, uint16_t eventid, int sub)
{
	static const char _unsubscribe_[] = "unsubscribe";
	struct outcall *outcall;
	struct afb_evt *event;
	int rc;

	outcall = outcall_at(stub, callid);
	if (outcall == NULL || outcall->type != outcall_type_call) {
		RP_ERROR("can't %s, no call of id %d", _unsubscribe_+sub, (int)callid);
		rc = X_EPROTO;
	}
	else {
		rc = u16id2ptr_get(stub->event_proxies, eventid, (void**)&event);
		if (rc < 0 || !event)
			RP_ERROR("can't %s, no event of id %d", _unsubscribe_+sub, (int)callid);
		else if (afb_req_common_subscribe_hookable(outcall->item.comreq, event) < 0)
			RP_ERROR("can't  %s: %m", _unsubscribe_+sub);
	}
	return rc;
}

static int receive_event_subscribe(struct afb_stub_rpc *stub, uint16_t callid, uint16_t eventid)
{
	return receive_event_subscription(stub, callid, eventid, 2/*used for logging*/);
}

static int receive_event_unsubscribe(struct afb_stub_rpc *stub, uint16_t callid, uint16_t eventid)
{
	return receive_event_subscription(stub, callid, eventid, 0);
}

/*****************************************************/

static int receive_event_push(struct afb_stub_rpc *stub, uint16_t eventid, unsigned ndata, struct afb_data *data[])
{
	struct afb_evt *evt;
	int rc;

	rc = u16id2ptr_get(stub->event_proxies, eventid, (void**)&evt);
	if (rc >= 0 && evt)
		rc = afb_evt_push_hookable(evt, ndata, data);
	else
		RP_ERROR("unreadable push event");
	if (rc <= 0)
		send_event_unexpected(stub, eventid);
	return rc;
}

static int receive_event_broadcast(
		struct afb_stub_rpc *stub,
		const char *event_name,
		unsigned ndata,
		struct afb_data *data[],
		const rp_uuid_binary_t uuid,
		uint8_t hop
) {
	return afb_evt_rebroadcast_name_hookable(event_name, ndata, data, uuid, hop);
}

static void describe_reply(struct outcall *outcall, const char *description)
{
	struct json_object *desc = description ? json_tokener_parse(description) : NULL;
	outcall->item.describe.callback(outcall->item.describe.closure, desc);
}

static void describe_reply_data(struct outcall *outcall, unsigned ndata, struct afb_data *data[])
{
	const char *desc = ndata ? afb_data_ro_pointer(data[0]) : NULL;
	describe_reply(outcall, desc);
}

static int receive_describe_reply(struct afb_stub_rpc *stub, const char *description, uint16_t callid)
{
	int rc;
	struct outcall *outcall = outcall_at(stub, callid);
	if (outcall == NULL) {
		RP_ERROR("no describe of id %d", (int)callid);
		rc = X_EPROTO;
	}
	else {
		if (outcall->type != outcall_type_describe) {
			RP_ERROR("describe mismatch for id %d", (int)callid);
		rc = X_EPROTO;
		}
		else {
			describe_reply(outcall, description);
			rc = 0;
		}
		outcall_release(stub, outcall);
	}
	return rc;
}

static int reply_description(struct afb_stub_rpc *stub, struct json_object *object, uint16_t callid)
{
	afb_rpc_coder_on_dispose_output(&stub->coder, json_put_cb, object);
	return send_describe_reply(stub, callid, json_object_to_json_string(object));
}

static int indesc_reply_description(struct indesc *indesc, struct json_object *object)
{
	int rc = reply_description(indesc->link.stub, object, indesc->callid);
	indesc_release(indesc);
	return rc;
}

static void got_description_cb(void *closure, struct json_object *object)
{
	struct indesc *indesc = closure;
	indesc_reply_description(indesc, object);
}

static void describe_job_cb(int status, void *closure)
{
	struct indesc *indesc = closure;
	if (status || indesc->link.stub->apiname == NULL)
		indesc_reply_description(indesc, NULL);
	else
		afb_apiset_describe(indesc->link.stub->call_set, indesc->link.stub->apiname, got_description_cb, indesc);
}

static int receive_describe_request(struct afb_stub_rpc *stub, uint16_t callid)
{
	int rc;
	struct indesc *indesc = indesc_get(stub, callid);

	if (indesc == NULL) {
		RP_ERROR("can't reply describe request %d", (int)callid);
		reply_description(stub, NULL, callid);
		rc = X_ENOMEM;
	}
	else {
		rc = queue_job(stub, describe_job_cb, indesc);
		if (rc < 0) {
			RP_ERROR("can't schedule describe request %d", (int)callid);
			indesc_reply_description(indesc, NULL);
		}
	}
	return rc;
}

#if WITH_RPC_V1
/**************************************************************************
* PART - PROCESS INCOMING MESSAGES V1
**************************************************************************/

static int decode_call_v1(struct afb_stub_rpc *stub, afb_rpc_v1_msg_call_t *msg)
{
	struct afb_data *arg;
	int rc = afb_data_create_raw(&arg, &afb_type_predefined_json, msg->data, msg->data_len, inblock_unref_cb, inblock_addref(stub->receive.current_inblock));
	if (rc >= 0)
		rc = receive_call_request(stub, msg->callid, NULL, msg->verb, 1, &arg, msg->sessionid, msg->tokenid, msg->user_creds);
	return rc;
}

static int decode_reply_v1(struct afb_stub_rpc *stub, afb_rpc_v1_msg_reply_t *msg)
{
	struct afb_data *datas[4];
	unsigned ndata = 4;
	int rc, rc2;
	int status = afb_error_code(msg->error);

	rc = afb_json_legacy_make_reply_json_string(datas, /* TODO improve that decoding to handle V4 wrapped on rpcv1 */
			msg->data, inblock_unref_cb, inblock_addref(stub->receive.current_inblock),
			msg->error, inblock_unref_cb, inblock_addref(stub->receive.current_inblock),
			msg->info, inblock_unref_cb, inblock_addref(stub->receive.current_inblock));
	if (rc < 0) {
		ndata = 0;
		status = status ? status : AFB_ERRNO_OUT_OF_MEMORY;
	}
	rc2 = receive_call_reply(stub, msg->callid, status, ndata, datas);
	return rc ? rc : rc2;
}

static int decode_event_create_v1(struct afb_stub_rpc *stub, afb_rpc_v1_msg_event_create_t *msg)
{
	return receive_event_create(stub, msg->eventid, msg->eventname);
}

static int decode_event_remove_v1(struct afb_stub_rpc *stub, afb_rpc_v1_msg_event_remove_t *msg)
{
	return receive_event_destroy(stub, msg->eventid);
}

static int decode_event_subscribe_v1(struct afb_stub_rpc *stub, afb_rpc_v1_msg_event_subscribe_t *msg)
{
	return receive_event_subscribe(stub, msg->callid, msg->eventid);
}

static int decode_event_unsubscribe_v1(struct afb_stub_rpc *stub, afb_rpc_v1_msg_event_unsubscribe_t *msg)
{
	return receive_event_unsubscribe(stub, msg->callid, msg->eventid);
}

static int decode_event_push_v1(struct afb_stub_rpc *stub, afb_rpc_v1_msg_event_push_t *msg)
{
	int rc;
	unsigned count;
	struct afb_data *arg;
	if (msg->data) {
		count = 1;
		rc = afb_data_create_raw(&arg, &afb_type_predefined_json, msg->data, 1 + strlen(msg->data),
					inblock_unref_cb, inblock_addref(stub->receive.current_inblock));
	}
	else {
		count = 0;
		rc = 0;
	}
	if (rc >= 0)
		rc = receive_event_push(stub, msg->eventid, count, &arg);
	return rc;
}

static int decode_event_broadcast_v1(struct afb_stub_rpc *stub, afb_rpc_v1_msg_event_broadcast_t *msg)
{
	int rc;
	unsigned count;
	struct afb_data *arg;
	if (msg->data) {
		count = 1;
		rc = afb_data_create_raw(&arg, &afb_type_predefined_json, msg->data, 1 + strlen(msg->data),
					inblock_unref_cb, inblock_addref(stub->receive.current_inblock));
	}
	else {
		count = 0;
		rc = 0;
	}
	if (rc >= 0)
		rc = receive_event_broadcast(stub, msg->name, count, &arg, *msg->uuid, msg->hop);
	return rc;
}

static int decode_event_unexpected_v1(struct afb_stub_rpc *stub, afb_rpc_v1_msg_event_unexpected_t *msg)
{
	return receive_event_unexpected(stub, msg->eventid);
}

static int decode_session_create_v1(struct afb_stub_rpc *stub, afb_rpc_v1_msg_session_create_t *msg)
{
	return receive_session_create(stub, msg->sessionid, msg->sessionname);
}

static int decode_session_remove_v1(struct afb_stub_rpc *stub, afb_rpc_v1_msg_session_remove_t *msg)
{
	return receive_session_destroy(stub, msg->sessionid);
}

static int decode_token_create_v1(struct afb_stub_rpc *stub, afb_rpc_v1_msg_token_create_t *msg)
{
	return receive_token_create(stub, msg->tokenid, msg->tokenname);
}

static int decode_token_remove_v1(struct afb_stub_rpc *stub, afb_rpc_v1_msg_token_remove_t *msg)
{
	return receive_token_destroy(stub, msg->tokenid);
}

static int decode_describe_v1(struct afb_stub_rpc *stub, afb_rpc_v1_msg_describe_t *msg)
{
	return receive_describe_request(stub, msg->descid);
}

static int decode_description_v1(struct afb_stub_rpc *stub, afb_rpc_v1_msg_description_t *msg)
{
	return receive_describe_reply(stub, msg->data, msg->descid);
}

static int decode_v1(struct afb_stub_rpc *stub)
{
	afb_rpc_v1_msg_t msg;
	int rc;

	rc = afb_rpc_v1_decode(&stub->decoder, &msg);
	if (rc >= 0) {
		switch(msg.type) {
		case afb_rpc_v1_msg_type_call:
			rc = decode_call_v1(stub, &msg.call);
			break;
		case afb_rpc_v1_msg_type_reply:
			rc = decode_reply_v1(stub, &msg.reply);
			break;
		case afb_rpc_v1_msg_type_event_create:
			rc = decode_event_create_v1(stub, &msg.event_create);
			break;
		case afb_rpc_v1_msg_type_event_remove:
			rc = decode_event_remove_v1(stub, &msg.event_remove);
			break;
		case afb_rpc_v1_msg_type_event_subscribe:
			rc = decode_event_subscribe_v1(stub, &msg.event_subscribe);
			break;
		case afb_rpc_v1_msg_type_event_unsubscribe:
			rc = decode_event_unsubscribe_v1(stub, &msg.event_unsubscribe);
			break;
		case afb_rpc_v1_msg_type_event_push:
			rc = decode_event_push_v1(stub, &msg.event_push);
			break;
		case afb_rpc_v1_msg_type_event_broadcast:
			rc = decode_event_broadcast_v1(stub, &msg.event_broadcast);
			break;
		case afb_rpc_v1_msg_type_event_unexpected:
			rc = decode_event_unexpected_v1(stub, &msg.event_unexpected);
			break;
		case afb_rpc_v1_msg_type_session_create:
			rc = decode_session_create_v1(stub, &msg.session_create);
			break;
		case afb_rpc_v1_msg_type_session_remove:
			rc = decode_session_remove_v1(stub, &msg.session_remove);
			break;
		case afb_rpc_v1_msg_type_token_create:
			rc = decode_token_create_v1(stub, &msg.token_create);
			break;
		case afb_rpc_v1_msg_type_token_remove:
			rc = decode_token_remove_v1(stub, &msg.token_remove);
			break;
		case afb_rpc_v1_msg_type_describe:
			rc = decode_describe_v1(stub, &msg.describe);
			break;
		case afb_rpc_v1_msg_type_description:
			rc = decode_description_v1(stub, &msg.description);
			break;
		default:
			rc = X_EPROTO;
			break;
		}
	}
	return rc;
}

#endif
#if WITH_RPC_V3
/**************************************************************************
* PART - PROCESS INCOMING MESSAGES V3
**************************************************************************/

static int typed_value_to_data_v3(struct afb_stub_rpc *stub, uint16_t typenum, uint32_t length, const void *value, struct afb_data **data)
{
	int rc;
	struct afb_type *type1 = 0, *type2 = 0;
	uint8_t size;

	switch (typenum) {
	case AFB_RPC_V3_ID_TYPE_OPAQUE:
		type1 = &afb_type_predefined_opaque;
		break;
	case AFB_RPC_V3_ID_TYPE_BYTEARRAY:
		type1 = &afb_type_predefined_bytearray;
		break;
	case AFB_RPC_V3_ID_TYPE_STRINGZ:
		type1 = &afb_type_predefined_stringz;
		break;
	case AFB_RPC_V3_ID_TYPE_JSON:
		type1 = &afb_type_predefined_json;
		break;
	case AFB_RPC_V3_ID_TYPE_BOOL:
		size = 1;
		type2 = &afb_type_predefined_bool;
		break;
	case AFB_RPC_V3_ID_TYPE_I32:
		size = 4;
		type2 = &afb_type_predefined_i32;
		break;
	case AFB_RPC_V3_ID_TYPE_U32:
		size = 4;
		type2 = &afb_type_predefined_u32;
		break;
	case AFB_RPC_V3_ID_TYPE_I64:
		size = 8;
		type2 = &afb_type_predefined_i64;
		break;
	case AFB_RPC_V3_ID_TYPE_U64:
		size = 8;
		type2 = &afb_type_predefined_u64;
		break;
	case AFB_RPC_V3_ID_TYPE_DOUBLE:
		size = 8;
		type2 = &afb_type_predefined_double;
		break;
	default:
		rc = X_ENOTSUP; /* TODO */
		break;
	}
	if (type1) {
		if (length)
			rc = afb_data_create_raw(data, type1, value, length,
					inblock_unref_cb, inblock_addref(stub->receive.current_inblock));
		else
			rc = afb_data_create_raw(data, type1, NULL, 0, 0, 0);
	}
	else if (type2) {
		if (size != length)
			rc = X_EPROTO;
		else {
#if BYTE_ORDER == LITTLE_ENDIAN
			rc = afb_data_create_copy(data, type2, value, length);
#else
			char buffer[8];
			uint32_t idx;
			for (idx = 0 ; idx < size ; idx++)
				buffer[idx] = ((const char*)value)[size - (idx + 1)];
			rc = afb_data_create_copy(data, type2, buffer, length);
#endif
		}
	}

	return rc;
}

static int value_to_data_v3(struct afb_stub_rpc *stub, afb_rpc_v3_value_t *value, struct afb_data **data)
{
	int rc;
	if (value->id && value->data) {
		rc = typed_value_to_data_v3(stub, value->id, value->length, value->data, data);
	}
	else if (value->id) {
		/* data value */
		rc = X_ENOTSUP; /* TODO */
	}
	else {
		rc = typed_value_to_data_v3(stub, AFB_RPC_V3_ID_TYPE_OPAQUE, value->length, value->data, data);
	}
	return rc;
}

static int value_array_to_data_array_v3(struct afb_stub_rpc *stub, unsigned count, afb_rpc_v3_value_t values[], struct afb_data *datas[])
{
	int rc;
	unsigned idx;

	for (idx = 0, rc = 0 ; idx < count && rc >= 0 ; idx++) {
		rc = value_to_data_v3(stub, &values[idx], &datas[idx]);
		if (rc < 0 && idx)
			afb_data_array_unref(idx, datas);
	}
	return rc;
}

static int decode_call_request_v3(struct afb_stub_rpc *stub, afb_rpc_v3_msg_call_request_t *msg, afb_rpc_v3_value_array_t *values)
{
	int rc;
	const char *verb, *api;
	unsigned count = values->count;
	struct afb_data *datas[count];

	api = msg->api.data;
	verb = msg->verb.data;
	if (verb == NULL) {
		/* ATM special cases are treated here */
		switch (msg->verb.id) {
		case AFB_RPC_V3_ID_VERB_DESCRIBE:
			rc = receive_describe_request(stub, msg->callid);
			break;
		default:
			rc = X_ENOTSUP; /*TODO*/
			break;
		}
	}
	else {
		/* normal case */
		rc = value_array_to_data_array_v3(stub, count, values->values, datas);
		if (rc >= 0)
			rc = receive_call_request(stub, msg->callid, api, verb, count, datas, msg->session.id, msg->token.id, msg->creds.data);
	}
	return rc;
}

static int decode_call_reply_v3(struct afb_stub_rpc *stub, afb_rpc_v3_msg_call_reply_t *msg, afb_rpc_v3_value_array_t *values)
{
	int rc;
	unsigned count = values->count;
	struct afb_data *datas[count];

	rc = value_array_to_data_array_v3(stub, count, values->values, datas);
	if (rc >= 0)
		rc = receive_call_reply(stub, msg->callid, msg->status, count, datas);
	return rc;
}

static int decode_event_push_v3(struct afb_stub_rpc *stub, afb_rpc_v3_msg_event_push_t *msg, afb_rpc_v3_value_array_t *values)
{
	int rc;
	unsigned count = values->count;
	struct afb_data *datas[count];

	rc = value_array_to_data_array_v3(stub, count, values->values, datas);
	if (rc >= 0)
		rc = receive_event_push(stub, msg->eventid, count, datas);
	return rc;
}

static int decode_event_subscribe_v3(struct afb_stub_rpc *stub, afb_rpc_v3_msg_event_subscribe_t *msg)
{
	return receive_event_subscribe(stub, msg->callid, msg->eventid);
}

static int decode_event_unsubscribe_v3(struct afb_stub_rpc *stub, afb_rpc_v3_msg_event_unsubscribe_t *msg)
{
	return receive_event_unsubscribe(stub, msg->callid, msg->eventid);
}

static int decode_event_unexpected_v3(struct afb_stub_rpc *stub, afb_rpc_v3_msg_event_unexpected_t *msg)
{
	return receive_event_unexpected(stub, msg->eventid);
}

static int decode_event_broadcast_v3(struct afb_stub_rpc *stub, afb_rpc_v3_msg_event_broadcast_t *msg, afb_rpc_v3_value_array_t *values)
{
	int rc;
	unsigned count = values->count;
	struct afb_data *datas[count];

	rc = value_array_to_data_array_v3(stub, count, values->values, datas);
	if (rc >= 0)
		rc = receive_event_broadcast(stub, msg->event, count, datas, *msg->uuid, msg->hop);
	return rc;
}

static int decode_resource_create_v3(struct afb_stub_rpc *stub, afb_rpc_v3_msg_resource_create_t *msg)
{
	int rc = 0;
	switch (msg->kind) {
	case AFB_RPC_V3_ID_KIND_SESSION:
		rc = receive_session_create(stub, msg->id, msg->data);
		break;
	case AFB_RPC_V3_ID_KIND_TOKEN:
		rc = receive_token_create(stub, msg->id, msg->data);
		break;
	case AFB_RPC_V3_ID_KIND_EVENT:
		rc = receive_event_create(stub, msg->id, msg->data);
		break;
	case AFB_RPC_V3_ID_KIND_API:
	case AFB_RPC_V3_ID_KIND_VERB:
	case AFB_RPC_V3_ID_KIND_TYPE:
	case AFB_RPC_V3_ID_KIND_DATA:
	case AFB_RPC_V3_ID_KIND_KIND:
	case AFB_RPC_V3_ID_KIND_CREDS:
	case AFB_RPC_V3_ID_KIND_OPERATOR:
		rc = X_ENOTSUP;
		break;
	}
	return rc;
}

static int decode_resource_destroy_v3(struct afb_stub_rpc *stub, afb_rpc_v3_msg_resource_destroy_t *msg)
{
	int rc = 0;
	switch (msg->kind) {
	case AFB_RPC_V3_ID_KIND_SESSION:
		rc = receive_session_destroy(stub, msg->id);
		break;
	case AFB_RPC_V3_ID_KIND_TOKEN:
		rc = receive_token_destroy(stub, msg->id);
		break;
	case AFB_RPC_V3_ID_KIND_EVENT:
		rc = receive_event_destroy(stub, msg->id);
		break;
	case AFB_RPC_V3_ID_KIND_API:
	case AFB_RPC_V3_ID_KIND_VERB:
	case AFB_RPC_V3_ID_KIND_TYPE:
	case AFB_RPC_V3_ID_KIND_DATA:
	case AFB_RPC_V3_ID_KIND_KIND:
	case AFB_RPC_V3_ID_KIND_CREDS:
	case AFB_RPC_V3_ID_KIND_OPERATOR:
		rc = X_ENOTSUP;
		break;
	}
	return rc;
}

static int decode_v3(struct afb_stub_rpc *stub)
{
	afb_rpc_v3_pckt_t packet;
	afb_rpc_v3_msg_t msg;
	afb_rpc_v3_value_t values[64];
	afb_rpc_v3_value_array_t valarr;

	int rc = afb_rpc_v3_decode_packet(&stub->decoder, &packet);
	if (rc >= 0) {
		valarr.values = values;
		valarr.count = sizeof values / sizeof *values;
		msg.values.array = &valarr;
		msg.values.allocator = NULL;
		rc = afb_rpc_v3_decode_operation(&packet, &msg);
		if (rc >= 0) {
			switch(msg.oper) {
			case AFB_RPC_V3_ID_OP_CALL_REQUEST:
				rc = decode_call_request_v3(stub, &msg.head.call_request, &valarr);
				break;
			case AFB_RPC_V3_ID_OP_CALL_REPLY:
				rc = decode_call_reply_v3(stub, &msg.head.call_reply, &valarr);
				break;
			case AFB_RPC_V3_ID_OP_EVENT_PUSH:
				rc = decode_event_push_v3(stub, &msg.head.event_push, &valarr);
				break;
			case AFB_RPC_V3_ID_OP_EVENT_SUBSCRIBE:
				rc = decode_event_subscribe_v3(stub, &msg.head.event_subscribe);
				break;
			case AFB_RPC_V3_ID_OP_EVENT_UNSUBSCRIBE:
				rc = decode_event_unsubscribe_v3(stub, &msg.head.event_unsubscribe);
				break;
			case AFB_RPC_V3_ID_OP_EVENT_UNEXPECTED:
				rc = decode_event_unexpected_v3(stub, &msg.head.event_unexpected);
				break;
			case AFB_RPC_V3_ID_OP_EVENT_BROADCAST:
				rc = decode_event_broadcast_v3(stub, &msg.head.event_broadcast, &valarr);
				break;
			case AFB_RPC_V3_ID_OP_RESOURCE_CREATE:
				rc = decode_resource_create_v3(stub, &msg.head.resource_create);
				break;
			case AFB_RPC_V3_ID_OP_RESOURCE_DESTROY:
				rc = decode_resource_destroy_v3(stub, &msg.head.resource_destroy);
				break;
			default:
				rc = X_EPROTO;
				break;
			}
		}
	}
	return rc;
}

#endif

/**************************************************************************
* PART - PROCESS INCOMING VERSION NEGOTIATION
**************************************************************************/

static int decode_v0(struct afb_stub_rpc *stub)
{
	afb_rpc_v0_msg_t m0;
	int rc;

	/* decode the received input */
	rc = afb_rpc_v0_decode(&stub->decoder, &m0);
	if (rc < 0) {
		if (rc == X_EPROTO) {
			stub->version = AFBRPC_PROTO_VERSION_1;
			rc = 0;
		}
	}
	else {
		switch(m0.type) {
		case afb_rpc_v0_msg_type_version_offer:
			for (rc = 0 ; rc < (int)m0.version_offer.count ; rc++) {
				uint8_t offer = m0.version_offer.versions[rc];
				switch(offer) {
#if WITH_RPC_V1
				case AFBRPC_PROTO_VERSION_1:
#endif
#if WITH_RPC_V3
				case AFBRPC_PROTO_VERSION_3:
#endif
					if (offer > stub->version)
						stub->version = offer;
					break;
				default:
					break;
				}
			}
			rc = afb_rpc_v0_code_version_set(&stub->coder, stub->version);
			emit(stub);
			wait_version_done(stub);
			break;
		case afb_rpc_v0_msg_type_version_set:
			stub->version = m0.version_set.version;
			wait_version_done(stub);
			break;
		default:
			break;
		}

	}
	return rc;
}

/**************************************************************************
* PART - DISPATCH INCOMING MESSAGES
**************************************************************************/
static int decode_block(struct afb_stub_rpc *stub, struct inblock *inblock)
{
	int rc = 0;

	if (inblock->size > UINT32_MAX)
		return X_E2BIG;

	stub->receive.current_inblock = inblock;
	afb_rpc_decoder_init(&stub->decoder, inblock->data, (uint32_t)inblock->size);
	while (rc >= 0 && afb_rpc_decoder_remaining_size(&stub->decoder)) {
		switch(stub->version) {
		case AFBRPC_PROTO_VERSION_UNSET:
			rc = decode_v0(stub);
			break;
#if WITH_RPC_V1
		case AFBRPC_PROTO_VERSION_1:
			rc = decode_v1(stub);
			break;
#endif
#if WITH_RPC_V3
		case AFBRPC_PROTO_VERSION_3:
			rc = decode_v3(stub);
			break;
#endif
		default:
			rc = X_EINVAL;
			break;
		}
	}
	stub->receive.current_inblock = 0;
	return rc;
}

/**************************************************************************
* PART - RECEIVING BUFFERS
**************************************************************************/

int afb_stub_rpc_receive(struct afb_stub_rpc *stub, void *data, size_t size)
{
	struct inblock *inblock;
	int rc = inblock_get(stub, data, size, &inblock);
	if (rc >= 0) {
		rc = decode_block(stub, inblock);
		inblock_unref(inblock);
	}
	return rc;
}

void afb_stub_rpc_receive_set_dispose(struct afb_stub_rpc *stub, void (*dispose)(void*, void*, size_t), void *closure)
{
	stub->receive.dispose = dispose;
	stub->receive.closure = closure;
}

/**************************************************************************
* PART - SENDING BUFFERS
**************************************************************************/

int afb_stub_rpc_emit_is_ready(struct afb_stub_rpc *stub)
{
	/* TODO check */
	return afb_rpc_coder_output_sizes(&stub->coder, 0);
}

afb_rpc_coder_t *afb_stub_rpc_emit_coder(struct afb_stub_rpc *stub)
{
	return &stub->coder;
}

void afb_stub_rpc_emit_set_notify(struct afb_stub_rpc *stub, void (*notify)(void*, struct afb_rpc_coder*), void *closure)
{
	stub->emit.notify = notify;
	stub->emit.closure = closure;
}

/*****************************************************/

/**
 * Creation of an api stub either client or server for an api
 *
 * @param apiname name of the api stubbed
 * @param call_set apiset for calling
 *
 * @return a handle on the created stub object
 */
int afb_stub_rpc_create(struct afb_stub_rpc **pstub, const char *apiname, struct afb_apiset *call_set)
{
	struct afb_stub_rpc *stub;

	/* allocation */
	*pstub = stub = calloc(1, sizeof *stub + (apiname == NULL ? 0 : 1 + strlen(apiname)));
	if (stub == NULL)
		return X_ENOMEM;

	/* terminate initialization */
	stub->refcount = 1;
	if (apiname != NULL) {
		char *name = (char*)(&stub[1]);
		strcpy(name, apiname);
		stub->apiname = name;
	}
	stub->call_set = afb_apiset_addref(call_set);
	return 0;
}

/* return the api name */
const char *afb_stub_rpc_apiname(struct afb_stub_rpc *stub)
{
	return stub->apiname;
}

/* declares the client api in apiset */
int afb_stub_rpc_client_add(struct afb_stub_rpc *stub, struct afb_apiset *declare_set)
{
	struct afb_api_item api;

	if (stub->apiname == NULL)
		return X_EINVAL;
	if (stub->declare_set)
		return X_EEXIST;

	api.closure = stub;
	api.itf = &stub_api_itf;
	api.group = stub; /* serialize */

	stub->declare_set = afb_apiset_addref(declare_set);
	return afb_apiset_add(declare_set, stub->apiname, api);
}

/* add one reference */
struct afb_stub_rpc *afb_stub_rpc_addref(struct afb_stub_rpc *stub)
{
	__atomic_add_fetch(&stub->refcount, 1, __ATOMIC_RELAXED);
	return stub;
}

/* offer the version */
int afb_stub_rpc_offer_version(struct afb_stub_rpc *stub)
{
	int rc = 0;
	if (stub->version == AFBRPC_PROTO_VERSION_UNSET) {
		uint8_t versions[] = {
#if WITH_RPC_V1
			AFBRPC_PROTO_VERSION_1,
#endif
#if WITH_RPC_V3
			AFBRPC_PROTO_VERSION_3,
#endif
		};
		rc = afb_rpc_v0_code_version_offer(&stub->coder, (uint8_t)(sizeof versions / sizeof *versions), versions);
		emit(stub);
	}
	return rc;
}

void afb_stub_rpc_set_unpack(struct afb_stub_rpc *stub, int unpack)
{
	stub->unpack = unpack != 0;
}

void afb_stub_rpc_set_session(struct afb_stub_rpc *stub, struct afb_session *session)
{
	afb_session_unref(__atomic_exchange_n(&stub->session, afb_session_addref(session), __ATOMIC_RELAXED));
}

void afb_stub_rpc_set_token(struct afb_stub_rpc *stub, struct afb_token *token)
{
	afb_token_unref(__atomic_exchange_n(&stub->token, afb_token_addref(token), __ATOMIC_RELAXED));
}



#if WITH_CRED
void afb_stub_rpc_set_cred(struct afb_stub_rpc *stub, struct afb_cred *cred)
{
	afb_cred_unref(__atomic_exchange_n(&stub->cred, cred, __ATOMIC_RELAXED));
}
#endif

/*****************************************************/
/*****************************************************/

static void release_all_sessions_cb(void*closure, uint16_t id, void *ptr)
{
	struct afb_session *session = ptr;
	afb_session_unref(session);
}

static void release_all_tokens_cb(void*closure, uint16_t id, void *ptr)
{
	struct afb_token *token = ptr;
	afb_token_unref(token);
}

static void release_all_events_cb(void*closure, uint16_t id, void *ptr)
{
	struct afb_evt *eventid = ptr;
	afb_evt_unref(eventid);
}

static void release_all_outcalls(struct afb_stub_rpc *stub)
{
	struct outcall *ocall, *iter = __atomic_exchange_n(&stub->outcalls, NULL, __ATOMIC_RELAXED);
	while (iter != NULL) {
		ocall = iter;
		iter = iter->next;
		ocall->next = NULL;

		switch(ocall->type) {
		case outcall_type_call:
			afb_req_common_reply_hookable(ocall->item.comreq, AFB_ERRNO_DISCONNECTED, 0, NULL);
			afb_req_common_unref(ocall->item.comreq);
			break;
		case outcall_type_describe:
			describe_reply(ocall, NULL);
			break;
		}
		outcall_free(stub, ocall);
	}
}

/* disconnect */
static void disconnect(struct afb_stub_rpc *stub)
{
	struct u16id2ptr *i2p;
	struct u16id2bool *i2b;

	wait_version_done(stub);
	release_all_outcalls(stub);

	i2p = __atomic_exchange_n(&stub->event_proxies, NULL, __ATOMIC_RELAXED);
	if (i2p) {
		u16id2ptr_forall(i2p, release_all_events_cb, NULL);
		u16id2ptr_destroy(&i2p);
	}
	i2b = __atomic_exchange_n(&stub->session_flags, NULL, __ATOMIC_RELAXED);
	u16id2bool_destroy(&i2b);
	i2b = __atomic_exchange_n(&stub->token_flags, NULL, __ATOMIC_RELAXED);
	u16id2bool_destroy(&i2b);
	afb_evt_listener_unref(__atomic_exchange_n(&stub->listener, NULL, __ATOMIC_RELAXED));
#if WITH_CRED
	afb_cred_unref(__atomic_exchange_n(&stub->cred, NULL, __ATOMIC_RELAXED));
#endif
	i2b = __atomic_exchange_n(&stub->event_flags, NULL, __ATOMIC_RELAXED);
	u16id2bool_destroy(&i2b);
	i2p = __atomic_exchange_n(&stub->session_proxies, NULL, __ATOMIC_RELAXED);
	if (i2p) {
		u16id2ptr_forall(i2p, release_all_sessions_cb, NULL);
		u16id2ptr_destroy(&i2p);
	}
	i2p = __atomic_exchange_n(&stub->token_proxies, NULL, __ATOMIC_RELAXED);
	if (i2p) {
		u16id2ptr_forall(i2p, release_all_tokens_cb, NULL);
		u16id2ptr_destroy(&i2p);
	}
}

/* sub one reference and free resources if falling to zero */
void afb_stub_rpc_unref(struct afb_stub_rpc *stub)
{
	struct inblock *iblk;

	if (stub && !__atomic_sub_fetch(&stub->refcount, 1, __ATOMIC_RELAXED)) {

		/* cleanup */
		disconnect(stub);
		if (stub->declare_set) {
			afb_apiset_del(stub->declare_set, stub->apiname);
			afb_apiset_unref(stub->declare_set);
		}
		afb_apiset_unref(stub->call_set);
		while ((iblk = stub->receive.pool) != NULL) {
			stub->receive.pool = iblk->data;
			free(iblk);
		}
		free(stub);
	}
}
