/*
 Copyright (C) 2015-2024 IoT.bzh Company

 Author: José Bollo <jose.bollo@iot.bzh>

 $RP_BEGIN_LICENSE$
 Commercial License Usage
  Licensees holding valid commercial IoT.bzh licenses may use this file in
  accordance with the commercial license agreement provided with the
  Software or, alternatively, in accordance with the terms contained in
  a written agreement between you and The IoT.bzh Company. For licensing terms
  and conditions see https://www.iot.bzh/terms-conditions. For further
  information use the contact form at https://www.iot.bzh/contact.

 GNU General Public License Usage
  Alternatively, this file may be used under the terms of the GNU General
  Public license version 3. This license is as published by the Free Software
  Foundation and appearing in the file LICENSE.GPLv3 included in the packaging
  of this file. Please review the following information to ensure the GNU
  General Public License requirements will be met
  https://www.gnu.org/licenses/gpl-3.0.html.
 $RP_END_LICENSE$
*/

#include <stdlib.h>
#include <stdio.h>
#include <sys/uio.h>

#include <check.h>

#include "rpc/afb-rpc-coder.h"
#include "rpc/afb-stub-rpc.h"
#include "rpc/afb-rpc-coder.h"
#include "rpc/afb-rpc-decoder.h"
#include "rpc/afb-rpc-v0.h"
#include "rpc/afb-rpc-v3.h"

/*************************** Helpers Functions ***************************/

#define AFB_RPC_V3_ID_OP_CALL_REQUEST        0xffff
#define AFB_RPC_V3_ID_OP_CALL_REPLY          0xfffe
#define AFB_RPC_V3_ID_OP_EVENT_PUSH          0xfffd
#define AFB_RPC_V3_ID_OP_EVENT_SUBSCRIBE     0xfffc
#define AFB_RPC_V3_ID_OP_EVENT_UNSUBSCRIBE   0xfffb
#define AFB_RPC_V3_ID_OP_EVENT_UNEXPECTED    0xfffa
#define AFB_RPC_V3_ID_OP_EVENT_BROADCAST     0xfff9
#define AFB_RPC_V3_ID_OP_RESOURCE_CREATE     0xfff8
#define AFB_RPC_V3_ID_OP_RESOURCE_DESTROY    0xfff7

const char *data[] = {
	"res1", // 1000
	"uuiduuiduuiduuid", // 1001 length = 16
	"bev", // 1002
	"data1", // 1003
	"api", // 1004
	"verb", // 1005
	"session", // 1006
	"token", // 1007
	"creds", // 1008
	"data2", // 1009
	"data3", // 1010
	"datax", // 1011
	"datay", // 1012
	"dataz", // 1013
	"\"=1=\"", // 1014
};

int tdef[] = {
	AFB_RPC_V3_ID_OP_EVENT_PUSH, // eid, 1-data, id, val/s
		1, 1, 1, 1014,

	AFB_RPC_V3_ID_OP_CALL_REPLY, // cid, sts, 0-data
		2, 0, 0,

	AFB_RPC_V3_ID_OP_RESOURCE_CREATE, // kind, id, data/s
		1, 2, 1000,

	AFB_RPC_V3_ID_OP_RESOURCE_DESTROY, // kind, id
		3, 4,

	AFB_RPC_V3_ID_OP_EVENT_UNEXPECTED, // eid
		5,

	AFB_RPC_V3_ID_OP_EVENT_SUBSCRIBE, // cid, eid
		6, 7,

	AFB_RPC_V3_ID_OP_EVENT_UNSUBSCRIBE, // cid, eid
		8, 9,

	AFB_RPC_V3_ID_OP_EVENT_BROADCAST, // uid/s, hop, name/s, 0-data
		1001, 10, 1002, 0,

	AFB_RPC_V3_ID_OP_EVENT_PUSH, // eid, 1-data, id, val/s
		11, 1, 12, 1003,

	AFB_RPC_V3_ID_OP_CALL_REQUEST, // cid, api(i+d/s), verb(i+d/s), session(i+d/s),
	                               // token(i+d/s), creds(i+d/s), to, 2-data, id, val/s, id, val/s
		13, 14, 1004, 15, 1005, 16, 1006, 17, 1007, 18, 1008, 7777, 2, 19, 1009, 20, 1010,

	AFB_RPC_V3_ID_OP_CALL_REPLY, // cid, sts, 3-data, id, val/s, id, val/s, id, val/s
		21, 22, 3, 0, 1011, 23, 0, 24, 1012,

	AFB_RPC_V3_ID_OP_RESOURCE_CREATE, // kind, id, data/s
		25, 26, 1013,

	AFB_RPC_V3_ID_OP_RESOURCE_DESTROY, // kind, id
		27, 28,

	AFB_RPC_V3_ID_OP_EVENT_UNEXPECTED, // eid
		29,

	/***************/

	AFB_RPC_V3_ID_OP_RESOURCE_CREATE, // kind, id, data/s
		30, 31, 1000,

	AFB_RPC_V3_ID_OP_CALL_REPLY, // cid, sts, 0-data
		32, 33, 0,

	AFB_RPC_V3_ID_OP_EVENT_SUBSCRIBE, // cid, eid
		34, 35,

	AFB_RPC_V3_ID_OP_CALL_REPLY, // cid, sts, 0-data
		36, 37, 0,

	AFB_RPC_V3_ID_OP_EVENT_PUSH, // eid, 1-data, id, val/s
		38, 1, 39, 1003,

	AFB_RPC_V3_ID_OP_CALL_REPLY, // cid, sts, 0-data
		40, 41, 0,
};

afb_rpc_v3_value_t values[2][10];
afb_rpc_v3_value_array_t vals[2] = {
	{ 0, values[0] },
	{ 0, values[1] }
};
afb_rpc_v3_msg_t msgs[2] = {
	{ .values = { &vals[0], NULL, NULL } },
	{ .values = { &vals[1], NULL, NULL } },
};

void dump(const void *buffer, unsigned size, const char *prf)
{
	const unsigned char *p = buffer;
	unsigned idx = 0;
	while (idx < size) {
		if ((idx & 15) == 0)
			printf("%s%03x", prf?:"", idx);
		printf(" %02x", (unsigned)p[idx]);
		if ((++idx & 15) == 0)
			printf("\n");
	}
	if ((idx & 15) != 0)
		printf("\n");
}

unsigned getdata(int idx, void *pptr)
{
	if (idx < 1000) {
		*(void**)pptr = NULL;
		return 0;
	}
	else {
		const char *str = data[idx - 1000];
		*(void**)pptr = (void*)str;
		return (unsigned)strlen(str) + 1;
	}
}

int getval(int pos, afb_rpc_v3_value_t *val)
{
	val->id = (afb_rpc_v3_id_t)tdef[pos++];
	val->length = (uint16_t)getdata(tdef[pos++], &val->data);
	return pos;
}

int getmsg(int pos, afb_rpc_v3_msg_t *msg)
{
	uint16_t i;
	msg->oper = (afb_rpc_v3_id_t)tdef[pos++];
	switch(msg->oper) {
	case AFB_RPC_V3_ID_OP_EVENT_SUBSCRIBE:
		msg->head.event_subscribe.callid = (afb_rpc_v3_call_id_t)tdef[pos++];
		msg->head.event_subscribe.eventid = (afb_rpc_v3_id_t)tdef[pos++];
		break;
	case AFB_RPC_V3_ID_OP_EVENT_UNSUBSCRIBE:
		msg->head.event_unsubscribe.callid = (afb_rpc_v3_call_id_t)tdef[pos++];
		msg->head.event_unsubscribe.eventid = (afb_rpc_v3_id_t)tdef[pos++];
		break;
	case AFB_RPC_V3_ID_OP_EVENT_UNEXPECTED:
		msg->head.event_unexpected.eventid = (afb_rpc_v3_id_t)tdef[pos++];
		break;
	case AFB_RPC_V3_ID_OP_RESOURCE_CREATE:
		msg->head.resource_create.kind = (afb_rpc_v3_id_t)tdef[pos++];
		msg->head.resource_create.id = (afb_rpc_v3_id_t)tdef[pos++];
		msg->head.resource_create.length = (uint32_t)getdata(tdef[pos++], &msg->head.resource_create.data);
		break;
	case AFB_RPC_V3_ID_OP_RESOURCE_DESTROY:
		msg->head.resource_destroy.kind = (afb_rpc_v3_id_t)tdef[pos++];
		msg->head.resource_destroy.id = (afb_rpc_v3_id_t)tdef[pos++];
		break;
	case AFB_RPC_V3_ID_OP_CALL_REQUEST:
		msg->head.call_request.callid = (afb_rpc_v3_call_id_t)tdef[pos++];
		pos = getval(pos, &msg->head.call_request.api);
		pos = getval(pos, &msg->head.call_request.verb);
		pos = getval(pos, &msg->head.call_request.session);
		pos = getval(pos, &msg->head.call_request.token);
		pos = getval(pos, &msg->head.call_request.creds);
		msg->head.call_request.timeout = (uint32_t)tdef[pos++];
		break;
	case AFB_RPC_V3_ID_OP_CALL_REPLY:
		msg->head.call_reply.callid = (afb_rpc_v3_call_id_t)tdef[pos++];
		msg->head.call_reply.status = (int32_t)tdef[pos++];
		break;
	case AFB_RPC_V3_ID_OP_EVENT_PUSH:
		msg->head.event_push.eventid = (afb_rpc_v3_id_t)tdef[pos++];
		break;
	case AFB_RPC_V3_ID_OP_EVENT_BROADCAST:
		getdata(tdef[pos++], &msg->head.event_broadcast.uuid);
		msg->head.event_broadcast.hop = (uint8_t)tdef[pos++];
		msg->head.event_broadcast.length = (uint16_t)getdata(tdef[pos++], &msg->head.event_broadcast.event);
		break;
	}
	switch(msg->oper) {
	case AFB_RPC_V3_ID_OP_CALL_REQUEST:
	case AFB_RPC_V3_ID_OP_CALL_REPLY:
	case AFB_RPC_V3_ID_OP_EVENT_PUSH:
	case AFB_RPC_V3_ID_OP_EVENT_BROADCAST:
		msg->values.array->count = (uint16_t)tdef[pos++];
		for(i = 0 ; i < msg->values.array->count ; i++)
			pos = getval(pos, &msg->values.array->values[i]);
		break;
	default:
		msg->values.array->count = 0;
		break;
	}
	return pos;
}

int cmpval(afb_rpc_v3_value_t *val1, afb_rpc_v3_value_t *val2)
{
	if (val1->data == NULL)
		return val2->data == NULL
		    && val1->id == val2->id;

	return val2->data != NULL
	    && val1->length == val2->length
	    && !memcmp(val1->data, val2->data, val2->length);
}

int cmpmsg(afb_rpc_v3_msg_t *msg1, afb_rpc_v3_msg_t *msg2)
{
	uint16_t i;
	int r = msg1->oper == msg2->oper;
	if (r)
	switch(msg1->oper) {
	case AFB_RPC_V3_ID_OP_EVENT_SUBSCRIBE:
		r = msg1->head.event_subscribe.callid == msg2->head.event_subscribe.callid
		 && msg1->head.event_subscribe.eventid == msg2->head.event_subscribe.eventid;
		break;
	case AFB_RPC_V3_ID_OP_EVENT_UNSUBSCRIBE:
		r = msg1->head.event_unsubscribe.callid == msg2->head.event_unsubscribe.callid
		 && msg1->head.event_unsubscribe.eventid == msg2->head.event_unsubscribe.eventid;
		break;
	case AFB_RPC_V3_ID_OP_EVENT_UNEXPECTED:
		r = msg1->head.event_unexpected.eventid == msg2->head.event_unexpected.eventid;
		break;
	case AFB_RPC_V3_ID_OP_RESOURCE_CREATE:
		r = msg1->head.resource_create.kind == msg2->head.resource_create.kind
		 && msg1->head.resource_create.id == msg2->head.resource_create.id
		 && msg1->head.resource_create.length == msg2->head.resource_create.length
		 && !memcmp(msg1->head.resource_create.data, msg2->head.resource_create.data, msg2->head.resource_create.length);
		break;
	case AFB_RPC_V3_ID_OP_RESOURCE_DESTROY:
		r = msg1->head.resource_destroy.kind == msg2->head.resource_destroy.kind
		 && msg1->head.resource_destroy.id == msg2->head.resource_destroy.id;
		break;
	case AFB_RPC_V3_ID_OP_CALL_REQUEST:
		r = msg1->head.call_request.callid == msg2->head.call_request.callid
		 && msg1->head.call_request.timeout == msg2->head.call_request.timeout
		 && cmpval(&msg1->head.call_request.api, &msg2->head.call_request.api)
		 && cmpval(&msg1->head.call_request.verb, &msg2->head.call_request.verb)
		 && cmpval(&msg1->head.call_request.session, &msg2->head.call_request.session)
		 && cmpval(&msg1->head.call_request.token, &msg2->head.call_request.token)
		 && cmpval(&msg1->head.call_request.creds, &msg2->head.call_request.creds);
		break;
	case AFB_RPC_V3_ID_OP_CALL_REPLY:
		r = msg1->head.call_reply.callid == msg2->head.call_reply.callid
		 && msg1->head.call_reply.status == msg2->head.call_reply.status;
		break;
	case AFB_RPC_V3_ID_OP_EVENT_PUSH:
		r = msg1->head.event_push.eventid == msg2->head.event_push.eventid;
		break;
	case AFB_RPC_V3_ID_OP_EVENT_BROADCAST:
		r = msg1->head.event_broadcast.hop == msg2->head.event_broadcast.hop
		 && msg1->head.event_broadcast.length == msg2->head.event_broadcast.length
		 && !memcmp(msg1->head.event_broadcast.event, msg2->head.event_broadcast.event, msg2->head.event_broadcast.length)
		 && !memcmp(msg1->head.event_broadcast.uuid, msg2->head.event_broadcast.uuid, sizeof *msg2->head.event_broadcast.uuid);
		break;
	}
	if (r)
	switch(msg1->oper) {
	case AFB_RPC_V3_ID_OP_CALL_REQUEST:
	case AFB_RPC_V3_ID_OP_CALL_REPLY:
	case AFB_RPC_V3_ID_OP_EVENT_PUSH:
	case AFB_RPC_V3_ID_OP_EVENT_BROADCAST:
		r = msg1->values.array->count == msg2->values.array->count;
		for(i = 0 ; r && i < msg1->values.array->count ; i++)
			r = cmpval(&msg1->values.array->values[i], &msg2->values.array->values[i]);
		break;
	default:
		break;
	}
	return r;
}

/******************************** Test output ********************************/

START_TEST(test)
{
	uint32_t sz;
	char buffer[1000];
	afb_rpc_coder_t coder;
	afb_rpc_decoder_t decoder;
	afb_rpc_v3_pckt_t pckt;
	int it, off0, off1, off2, off3, rc;

	afb_rpc_coder_init(&coder);

	/* encode triplet of messages */
	it = 0;
	off0 = 0;
	do {
		printf("it%d\n", ++it);

		printf("  coding %d\n", off0);
		off1 = getmsg(off0, &msgs[0]);
		rc = afb_rpc_v3_code(&coder, &msgs[0]);
		ck_assert_int_eq(rc, 0);

		printf("  coding %d\n", off1);
		off2 = getmsg(off1, &msgs[0]);
		rc = afb_rpc_v3_code(&coder, &msgs[0]);
		ck_assert_int_eq(rc, 0);

		printf("  coding %d\n", off2);
		off3 = getmsg(off2, &msgs[0]);
		rc = afb_rpc_v3_code(&coder, &msgs[0]);
		ck_assert_int_eq(rc, 0);

		sz = afb_rpc_coder_output_get_buffer(&coder, buffer, (uint32_t)sizeof buffer);
		dump(buffer, sz, "    ");

		afb_rpc_decoder_init(&decoder, buffer, sz);

		printf("  decoding %d\n", off0);
		getmsg(off0, &msgs[1]);
		rc = afb_rpc_v3_decode_packet(&decoder, &pckt);
		ck_assert_int_eq(rc, 0);
		msgs[0].values.array->count = (uint16_t)(sizeof values[0] / sizeof values[0][0]);
		rc = afb_rpc_v3_decode_operation(&pckt, &msgs[0]);
		ck_assert_int_eq(rc, 0);
		rc = cmpmsg(&msgs[0], &msgs[1]);
		ck_assert_int_eq(rc, 1);

		printf("  decoding %d\n", off1);
		getmsg(off1, &msgs[1]);
		rc = afb_rpc_v3_decode_packet(&decoder, &pckt);
		ck_assert_int_eq(rc, 0);
		msgs[0].values.array->count = (uint16_t)(sizeof values[0] / sizeof values[0][0]);
		rc = afb_rpc_v3_decode_operation(&pckt, &msgs[0]);
		ck_assert_int_eq(rc, 0);
		rc = cmpmsg(&msgs[0], &msgs[1]);
		ck_assert_int_eq(rc, 1);

		printf("  decoding %d\n", off2);
		getmsg(off2, &msgs[1]);
		rc = afb_rpc_v3_decode_packet(&decoder, &pckt);
		ck_assert_int_eq(rc, 0);
		msgs[0].values.array->count = (uint16_t)(sizeof values[0] / sizeof values[0][0]);
		rc = afb_rpc_v3_decode_operation(&pckt, &msgs[0]);
		ck_assert_int_eq(rc, 0);
		rc = cmpmsg(&msgs[0], &msgs[1]);
		ck_assert_int_eq(rc, 1);

		rc = afb_rpc_v3_decode_packet(&decoder, &pckt);
		ck_assert_int_lt(rc, 0);

		afb_rpc_coder_output_dispose(&coder);

		off0 = off1;
	}
	while (off3 < (int)(sizeof tdef / sizeof *tdef));

}
END_TEST

/******************************** Tests ********************************/

struct exmpl {
	const char *buffer;
	unsigned size;
}
	exmpls[] ={
	{
		"\375\377\v\0\36\0\0\0\2\0\1\0\0\0\0\0"
		"\0\0\374\377\f\0\374\377\"=1=\"\0\0\0"
		"\376\377\f\0\20\0\0\0\6\0\0\0\0\0\0\0",
		48
	}
};

START_TEST(check)
{
	int rc, iex;
	afb_rpc_decoder_t decoder;
	afb_rpc_v3_pckt_t pckt;

	for (iex = 0 ; iex < (int)(sizeof exmpls / sizeof exmpls[0]) ; iex++) {
		printf("exmpl %d\n", iex);
		dump(exmpls[iex].buffer, exmpls[iex].size, "  ");
		afb_rpc_decoder_init(&decoder, exmpls[iex].buffer, exmpls[iex].size);
		while(decoder.offset < decoder.size) {
			printf("  %d/%d\n", decoder.offset, decoder.size);
			rc = afb_rpc_v3_decode_packet(&decoder, &pckt);
			ck_assert_int_eq(rc, 0);
			msgs[0].values.array->count = (uint16_t)(sizeof values[0] / sizeof values[0][0]);
			rc = afb_rpc_v3_decode_operation(&pckt, &msgs[0]);
			ck_assert_int_eq(rc, 0);
		}
	}
}

/******************************** Tests ********************************/
static Suite *suite;
static TCase *tcase;

void mksuite(const char *name)
{
	suite = suite_create(name);
}

void addtcase(const char *name)
{
	tcase = tcase_create(name);
	suite_add_tcase(suite, tcase);
}

#define addtest(test) tcase_add_test(tcase, test)
int srun()
{
	int nerr;
	SRunner *srunner = srunner_create(suite);
	srunner_run_all(srunner, CK_NORMAL);
	nerr = srunner_ntests_failed(srunner);
	srunner_free(srunner);
	return nerr;
}

int main(int ac, char **av)
{
	mksuite("afb-rpc-v3");
		addtcase("output");
			addtest(test);
			addtest(check);
	return !!srun();
}
