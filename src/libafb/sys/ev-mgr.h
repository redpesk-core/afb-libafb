/*
 * Copyright (C) 2015-2022 IoT.bzh Company
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

#include "x-epoll.h"

/******************************************************************************/

struct ev_mgr;

/**
 * Creates a new event manager
 *
 * @param mgr   address where is stored the result
 *
 * @return 0 on success or an negative error code
 */
extern int ev_mgr_create(struct ev_mgr **mgr);

/**
 * Increase the reference count of the event manager
 *
 * @param mgr  the event manager
 *
 * @return the event manager
 */
extern struct ev_mgr *ev_mgr_addref(struct ev_mgr *mgr);

/**
 * Deccrease the reference count of the event manager and release it
 * if it falls to zero
 *
 * @param mgr  the event manager
 */
extern void ev_mgr_unref(struct ev_mgr *mgr);

/**
 * Return a pollable/selectable file descriptor
 * for the event manager
 *
 * @param mgr  the event manager
 *
 * @return the file descriptor
 */
extern int ev_mgr_get_fd(struct ev_mgr *mgr);

/**
 * prepare the ev_mgr to run
 *
 * @param mgr  the event manager
 *
 * @return 0 on success or an negative error code
 */
extern int ev_mgr_prepare(struct ev_mgr *mgr);

/**
 * prepare the ev_mgr to run with a wakeup
 *
 * @param mgr  the event manager
 * @param wakeup_ms the wakeup time in millisecond
 *
 * @return 0 on success or an negative error code
 */
extern int ev_mgr_prepare_with_wakeup(struct ev_mgr *mgr, int wakeup_ms);

/**
 * wait an event
 *
 * @param mgr  the event manager
 *
 * @return 0 if no event were raise, 1 if one event
 * occured or an negative error code
 */
extern int ev_mgr_wait(struct ev_mgr *mgr, int timeout_ms);

/**
 * dispatch lastest events
 *
 * @param mgr  the event manager
 *
 * @return 0 on success or an negative error code
 */
extern void ev_mgr_dispatch(struct ev_mgr *mgr);

/**
 */
extern int ev_mgr_run(struct ev_mgr *mgr, int timeout_ms);
extern int ev_mgr_can_run(struct ev_mgr *mgr);
extern void ev_mgr_recover_run(struct ev_mgr *mgr);

/**
 * wake up the event loop if needed
 *
 * @param mgr  the event manager
 */
extern void ev_mgr_wakeup(struct ev_mgr *mgr);

/**
 * Try to change the holder
 *
 * It is successful if the given holder is the current holder
 *
 * @param mgr     the event manager
 * @param holder  the requesting possible holder
 * @param next    next holder to be set if holder matches current holder
 *
 * @return the afterward holder that is either
 */
extern void *ev_mgr_try_change_holder(struct ev_mgr *mgr, void *holder, void *next);


/**
 * Gets the current holder
 *
 * @return the current holder
 */
extern void *ev_mgr_holder(struct ev_mgr *mgr);


/******************************************************************************/

struct ev_prepare;

typedef void (*ev_prepare_cb_t)(struct ev_prepare *prep, void *closure);

extern int ev_mgr_add_prepare(
		struct ev_mgr *mgr,
		struct ev_prepare **prep,
		ev_prepare_cb_t handler,
		void *closure);

extern struct ev_prepare *ev_prepare_addref(struct ev_prepare *prep);
extern void ev_prepare_unref(struct ev_prepare *prep);

/******************************************************************************/

struct ev_fd;

typedef void (*ev_fd_cb_t)(struct ev_fd *efd, int fd, uint32_t revents, void *closure);

/**
 * Callbacks of file event handlers
 *
 * These callbacks are called when an event occurs on the handled
 * file descriptor.
 *
 * @param efd the file event handler object
 * @param fd the file descriptor index
 * @param revents the received events
 * @param closure the closure given at creation
 */
extern int ev_mgr_add_fd(
		struct ev_mgr *mgr,
		struct ev_fd **efd,
		int fd,
		uint32_t events,
		ev_fd_cb_t handler,
		void *closure,
		int autounref,
		int autoclose);

extern struct ev_fd *ev_fd_addref(struct ev_fd *efd);
extern void ev_fd_unref(struct ev_fd *efd);

extern int ev_fd_fd(struct ev_fd *efd);

extern uint32_t ev_fd_events(struct ev_fd *efd);
extern void ev_fd_set_events(struct ev_fd *efd, uint32_t events);

extern void ev_fd_set_handler(struct ev_fd *efd, ev_fd_cb_t handler, void *closure);

/******************************************************************************/

struct ev_timer;

/**
 * Callbacks of timers
 *
 * These callbacks are called when a programmed time even occurs.
 *
 * @param timer the timer object
 * @param closure the closure given at creation
 * @param decount reverse index of the event: zero for infinite timer
 *                or a decreasing value finishing with 1
 */
typedef void (*ev_timer_cb_t)(struct ev_timer *timer, void *closure, int decount);

extern int ev_mgr_add_timer(
		struct ev_mgr *mgr,
		struct ev_timer **timer,
		int absolute,
		time_t start_sec,
		unsigned start_ms,
		unsigned count,
		unsigned period_ms,
		unsigned accuracy_ms,
		ev_timer_cb_t handler,
		void *closure,
		int autounref);

extern struct ev_timer *ev_timer_addref(struct ev_timer *timer);
extern void ev_timer_unref(struct ev_timer *timer);
