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

#include "../utils/uuid.h"

struct afb_event_x2;
struct afb_evt;
struct afb_session;
struct json_object;
struct afb_evt_listener;

struct afb_evt_itf
{
	void (*push)(void *closure, const char *event, uint16_t evtid, struct json_object *object);
	void (*broadcast)(void *closure, const char *event, struct json_object *object, const uuid_binary_t uuid, uint8_t hop);
	void (*add)(void *closure, const char *event, uint16_t evtid);
	void (*remove)(void *closure, const char *event, uint16_t evtid);
};

extern struct afb_evt_listener *afb_evt_listener_create(const struct afb_evt_itf *itf, void *closure);
extern struct afb_evt_listener *afb_evt_listener_addref(struct afb_evt_listener *listener);
extern void afb_evt_listener_unref(struct afb_evt_listener *listener);

extern int afb_evt_listener_watch_evt(struct afb_evt_listener *listener, struct afb_evt *evt);
extern int afb_evt_listener_unwatch_evt(struct afb_evt_listener *listener, struct afb_evt *evt);
extern int afb_evt_listener_unwatch_id(struct afb_evt_listener *listener, uint16_t eventid);
extern void afb_evt_listener_unwatch_all(struct afb_evt_listener *listener, int remove);

extern struct afb_evt *afb_evt_create(const char *fullname);
extern struct afb_evt *afb_evt_create2(const char *prefix, const char *name);
extern struct afb_evt *afb_evt_addref(struct afb_evt *evt);
extern void afb_evt_unref(struct afb_evt *evt);

extern uint16_t afb_evt_id(struct afb_evt *evt);
extern const char *afb_evt_fullname(struct afb_evt *evt);
extern const char *afb_evt_name(struct afb_evt *evt);

extern int afb_evt_push(struct afb_evt *evt, struct json_object *obj);
extern int afb_evt_broadcast(struct afb_evt *evt, struct json_object *object);

extern int afb_evt_broadcast_name(const char *event, struct json_object *object);
extern int afb_evt_rebroadcast_name(const char *event, struct json_object *object, const  uuid_binary_t uuid, uint8_t hop);

#if WITH_AFB_HOOK
extern struct afb_evt *afb_evt_hooked_addref(struct afb_evt *evt);
extern void afb_evt_hooked_unref(struct afb_evt *evt);
extern const char *afb_evt_hooked_name(struct afb_evt *evt);
extern int afb_evt_hooked_push(struct afb_evt *evt, struct json_object *obj);
extern int afb_evt_hooked_broadcast(struct afb_evt *evt, struct json_object *object);
extern void afb_evt_update_hooks();
#endif

extern struct afb_evt *afb_evt_of_x2(struct afb_event_x2 *eventx2);
extern struct afb_event_x2 *afb_evt_as_x2(struct afb_evt *evt);


extern struct afb_event_x2 *afb_evt_event_x2_create(const char *fullname);
extern struct afb_event_x2 *afb_evt_event_x2_create2(const char *prefix, const char *name);
extern const char *afb_evt_event_x2_fullname(struct afb_event_x2 *event);
extern uint16_t afb_evt_event_x2_id(struct afb_event_x2 *eventx2);
extern struct afb_event_x2 *afb_evt_event_x2_addref(struct afb_event_x2 *eventx2);
extern void afb_evt_event_x2_unref(struct afb_event_x2 *eventx2);

extern int afb_evt_event_x2_push(struct afb_event_x2 *eventx2, struct json_object *object);
extern int afb_evt_event_x2_unhooked_push(struct afb_event_x2 *eventx2, struct json_object *object);

extern int afb_evt_listener_watch_x2(struct afb_evt_listener *listener, struct afb_event_x2 *eventx2);
extern int afb_evt_listener_unwatch_x2(struct afb_evt_listener *listener, struct afb_event_x2 *eventx2);
