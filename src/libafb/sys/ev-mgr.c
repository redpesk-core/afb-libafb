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

#include "../libafb-config.h"

#if __ZEPHYR__
#  undef WITH_TIMERFD
#  define WITH_TIMERFD 0
#endif
#if !defined(WITH_TIMERFD)
#  define WITH_TIMERFD 1
#endif


#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#if WITH_TIMERFD
#  include <sys/timerfd.h>
#endif

#include <rp-utils/rp-verbose.h>

#include "sys/x-errno.h"
#include "sys/x-poll.h"
#include "sys/x-epoll.h"

#include "sys/ev-mgr.h"


/******************************************************************************/
#undef WAKEUP_TGKILL
#undef WAKEUP_THREAD_KILL
#undef WAKEUP_EVENTFD
#undef WAKEUP_PIPE
#if WITH_EVENTFD
#  define WAKEUP_EVENTFD 1
#else
#  define WAKEUP_PIPE 1
#endif
/******************************************************************************/
#if WAKEUP_TGKILL
#  error "kept for memory but is not working"
#  undef WAKEUP_TGKILL
#  undef WAKEUP_THREAD_KILL
#  undef WAKEUP_EVENTFD
#  undef WAKEUP_PIPE
#  define WAKEUP_TGKILL 1
#  include <signal.h>
#elif WAKEUP_THREAD_KILL
#  error "kept for memory but is not working"
#  undef WAKEUP_TGKILL
#  undef WAKEUP_THREAD_KILL
#  undef WAKEUP_EVENTFD
#  undef WAKEUP_PIPE
#  define WAKEUP_THREAD_KILL 1
#  include "sys/x-thread.h"
#elif WAKEUP_EVENTFD
#  undef WAKEUP_TGKILL
#  undef WAKEUP_THREAD_KILL
#  undef WAKEUP_EVENTFD
#  undef WAKEUP_PIPE
#  define WAKEUP_EVENTFD 1
#  include <sys/eventfd.h>
#  if __ZEPHYR__
#    define EFD_CLOEXEC 0
#  endif
#else
#  undef WAKEUP_TGKILL
#  undef WAKEUP_THREAD_KILL
#  undef WAKEUP_EVENTFD
#  undef WAKEUP_PIPE
#  define WAKEUP_PIPE 1
#  include <fcntl.h>
#endif
/******************************************************************************/
#if !WITH_EPOLL
#  if WAKEUP_EVENTFD || WAKEUP_PIPE
#    define IDX_SIGNAL 0
#    if WITH_TIMERFD
#      define IDX_TIME 1
#      define IDX_FDS0 2
#    else
#      define IDX_FDS0 1
#    endif
#  else
#    if WITH_TIMERFD
#      define IDX_TIME 0
#      define IDX_FDS0 1
#    else
#      define IDX_FDS0 0
#    endif
#  endif
#endif

/******************************************************************************/

/** for times in nano seconds (685 years since 1970 -> 2655) */
typedef int64_t time_unit_t;

/** maximum value of time_unit_t instances */
#define TIME_UNIT_MAX INT64_MAX

#if __ZEPHYR__
# define TIME_IN_MILLISEC 1
# define TIME_IN_NANOSEC  0
#else
# define TIME_IN_MILLISEC 1
# define TIME_IN_NANOSEC  0
#endif

#if TIME_IN_MILLISEC
#define ONESEC       1000        /**< value of one second in nanosecond */
#define ONEMILLISEC     1        /**< value of one millisecond in nanosecond */
#define MS2UT(x)     ((time_unit_t)(x))              /**< convert ms to ut */
#define UT2MS(x)     (x)                             /**< convert ut to ms */
#define NS2UT(x)     ((time_unit_t)((x) / 1000000))  /**< convert ns to ut */
#define UT2NS(x)     ((x) * 1000000)                 /**< convert ut to ns */
#endif

#if TIME_IN_NANOSEC
#define ONESEC      1000000000   /**< value of one second in nanosecond */
#define ONEMILLISEC    1000000   /**< value of one millisecond in nanosecond */
#define MS2UT(x)     ((time_unit_t)((x) * 1000000)) /**< convert ms to ut */
#define UT2MS(x)     ((x) / 1000000)                /**< convert ut to ms */
#define NS2UT(x)     ((time_unit_t)(x))             /**< convert ns to ut */
#define UT2NS(x)     (x)                            /**< convert ut to ns */
#endif

/** what clock to use for timers */
#define CLOCK CLOCK_REALTIME

/** minimal period value (milliseconds)  */
#define PERIOD_MIN_MS 1

/** default period (milliseconds) */
#define DEFAULT_PERIOD_MS 1000

/** minimal period accuracy value (milliseconds) */
#define ACCURACY_MIN_MS 1

/** default a period accuracy (milliseconds) */
#define DEFAULT_ACCURACY_MS  ACCURACY_MIN_MS

/** get the default or min or value of x */
#define GETTM(x,def,min) ((x)==0 ? (def) : (x)<(min) ? (min) : (x))

/******************************************************************************/

/**
 * structure for managing file descriptor events
 */
struct ev_fd
{
	/** link to the next of the list */
	struct ev_fd *next;

	/** link to the manager */
	struct ev_mgr *mgr;

	/** callback handler */
	ev_fd_cb_t handler;

	/** closure of the handler */
	void *closure;

	/** monitored file descriptor */
	int fd;

	/** expected events */
	uint32_t events;

	/** reference count */
	uint16_t refcount;

	/** is active ? */
	uint16_t is_active: 1;

#if WITH_EPOLL
	/** is set in epoll ? */
	uint16_t is_set: 1;
#endif

	/** has changed since set ? */
	uint16_t has_changed: 1;

	/** is deleted ? */
	uint16_t is_deleted: 1;

	/** auto_close the file when last reference lost */
	uint16_t auto_close: 1;

	/** auto unref the file */
	uint16_t auto_unref: 1;
};

/**
 * structure for recording timers
 */
struct ev_timer
{
	/** next timer of the list of timers */
	struct ev_timer *next;

	/** the event manager */
	struct ev_mgr *mgr;

	/** handler callback */
	ev_timer_cb_t handler;

	/** closure of the handler */
	void *closure;

	/** time of the next expected occurence in nanoseconds */
	time_unit_t next_ut;

	/** expected accuracy of the timer in nanoseconds */
	time_unit_t accuracy_ut;

	/** period between 2 events in nanoseconds */
	time_unit_t period_ut;

	/** decount of occurences or zero if infinite */
	unsigned decount;

	/** reference count */
	uint16_t refcount;

	/** is active ? */
	uint16_t is_active: 1;

	/** is deleted ? */
	uint16_t is_deleted: 1;

	/** auto unref the timer */
	uint16_t auto_unref: 1;
};

/**
 * structure for recording prepare
 */
struct ev_prepare
{
	/** next prepare of the list */
	struct ev_prepare *next;

	/** the event manager */
	struct ev_mgr *mgr;

	/** handler callback */
	ev_prepare_cb_t handler;

	/** closure of the handler */
	void *closure;

	/** reference count */
	uint16_t refcount;
};

/** constants for tracking state of the manager */
enum state
{
	/** does nothing */
	Idle = 0,

	/** is preparing */
	Preparing = 1,

	/** is ready to wait */
	Ready = 2,

	/** is waiting */
	Waiting = 3,

	/** an event is pending */
	Pending = 4,

	/** is dispatching an event */
	Dispatching = 5
};

/** Description of handled event loops */
struct ev_mgr
{
	/** abstract pointer identifying the holder thread (null if none) */
	void *holder;

	/** list of managed file descriptors */
	struct ev_fd *efds;

	/** list of managed timers */
	struct ev_timer *timers;

	/** list of preparers */
	struct ev_prepare *preparers;

#if WITH_EPOLL
	/** latest event to be dispatched */
	struct epoll_event event;

	/** internally used epoll file descriptor */
	int epollfd;
#else
	/** */
	uint16_t szpollfds;
	uint16_t nrpollfds;
	struct pollfd *pollfds;
#endif

#if WAKEUP_TGKILL
	/** last known awaiting thread id */
	pid_t tid;
#elif WAKEUP_THREAD_KILL
	/** last known awaiting thread id */
	x_thread_t tid;
#elif WAKEUP_EVENTFD
	/** the eventfd */
	int eventfd;
#else
	/** the pipes fds */
	int pipefds[2];
#endif

#if WITH_TIMERFD
	/** internally used timerfd */
	int timerfd;
#endif

	/** last value set to the timer */
	time_unit_t last_timer;

	/** reference count */
	uint16_t refcount;

	/** current state */
	uint16_t state: 3;

	/** boolean flag indicating if efds list has changed */
	uint16_t efds_changed: 1;

	/** flag indicating that a cleanup of efds is needed */
	uint16_t efds_cleanup: 1;

	/** flag indicating that a cleanup of timers is needed */
	uint16_t timers_cleanup: 1;

	/** flag indicating that a cleanup of preparers is needed */
	uint16_t preparers_cleanup: 1;
};

#if !WITH_EPOLL
static int add_poll(struct ev_mgr *mgr, int fd, short events);
#endif

/******************************************************************************/
/******************************************************************************/
/** SECTION conversion                                                       **/
/******************************************************************************/
/******************************************************************************/
#define _TEST_(M,X)        (EV_FD_##X == M##X)
#define _SAME_MASK_(M,X)   (_TEST_(M,X) ? EV_FD_##X : 0)
#define _TO_MASK_(M,X,V)   (_TEST_(M,X) ? 0 : ((V) & EV_FD_##X) ? M##X : 0)
#define _FROM_MASK_(M,X,V) (_TEST_(M,X) ? 0 : ((V) & M##X) ? EV_FD_##X : 0)
#define SAME_MASK(M)       (_SAME_MASK_(M,IN)|_SAME_MASK_(M,OUT)|_SAME_MASK_(M,ERR)|_SAME_MASK_(M,HUP))
#define TO_MASK(M,V)       (_TO_MASK_(M,IN,V)|_TO_MASK_(M,OUT,V)|_TO_MASK_(M,ERR,V)|_TO_MASK_(M,HUP,V))
#define FROM_MASK(M,V)     (_FROM_MASK_(M,IN,V)|_FROM_MASK_(M,OUT,V)|_FROM_MASK_(M,ERR,V)|_FROM_MASK_(M,HUP,V))


inline uint32_t ev_fd_from_poll(short events)
{
	uint32_t r = (uint32_t)(events & SAME_MASK(POLL));
	r |= (uint32_t)FROM_MASK(POLL,events);
	return r;
}

inline short ev_fd_to_poll(uint32_t events)
{
	short r = (short)(events & SAME_MASK(POLL));
	r |= (short)TO_MASK(POLL,events);
	return r;
}

#if WITH_EPOLL
inline uint32_t ev_fd_from_epoll(uint32_t events)
{
	uint32_t r = (uint32_t)(events & SAME_MASK(EPOLL));
	r |= (uint32_t)FROM_MASK(EPOLL,events);
	return r;
}

inline uint32_t ev_fd_to_epoll(uint32_t events)
{
	uint32_t r = (uint32_t)(events & SAME_MASK(EPOLL));
	r |= (uint32_t)TO_MASK(EPOLL,events);
	return r;
}
#else
inline uint32_t ev_fd_from_epoll(uint32_t events)
{
	return ev_fd_from_poll((short)events);
}

inline uint32_t ev_fd_to_epoll(uint32_t events)
{
	return (uint32_t)ev_fd_to_poll(events);
}
#endif

/******************************************************************************/
/******************************************************************************/
/** SECTION ev_fd                                                            **/
/******************************************************************************/
/******************************************************************************/

int ev_mgr_add_fd(
		struct ev_mgr *mgr,
		struct ev_fd **pefd,
		int fd,
		uint32_t events,
		ev_fd_cb_t handler,
		void *closure,
		int autounref,
		int autoclose
) {
	int rc;
	struct ev_fd *efd;

	efd = malloc(sizeof *efd);
	if (!efd)
		rc = X_ENOMEM;
	else {
		efd->handler = handler;
		efd->closure = closure;
		efd->fd = fd;
		efd->events = events;
		efd->refcount = 1;
		efd->is_active = 1;
#if WITH_EPOLL
		efd->is_set = 0;
#endif
	        efd->has_changed = 0;
	        efd->auto_close = !!autoclose;
		efd->auto_unref = !!autounref;
		efd->is_deleted = 0;
		efd->mgr = mgr;
		efd->next = mgr->efds;
		mgr->efds = efd;
		mgr->efds_changed = 1;
		rc = 0;
	}
	*pefd = efd;
	return rc;
}

struct ev_fd *ev_fd_addref(struct ev_fd *efd)
{
	if (efd)
		__atomic_add_fetch(&efd->refcount, 1, __ATOMIC_RELAXED);
	return efd;
}

void ev_fd_unref(struct ev_fd *efd)
{
	if (efd && !__atomic_sub_fetch(&efd->refcount, 1, __ATOMIC_RELAXED)) {
#if WITH_EPOLL
		if (efd->is_active && efd->is_set) {
			int rc = epoll_ctl(efd->mgr->epollfd, EPOLL_CTL_DEL, efd->fd, 0);
			if (rc == 0)
				efd->is_set = 0;
		}
#endif
		efd->is_active = 0;
		efd->is_deleted = 1;
		if (efd->mgr)
			efd->mgr->efds_cleanup = 1;
		else {
			if (efd->auto_close && efd->fd >= 0)
				close(efd->fd);
			free(efd);
		}
	}
}

int ev_fd_fd(struct ev_fd *efd)
{
	return efd->fd;
}

uint32_t ev_fd_events(struct ev_fd *efd)
{
	return efd->events;
}

void ev_fd_set_events(struct ev_fd *efd, uint32_t events)
{
#if WITH_EPOLL
	if (efd->events != events) {
		int rc = 0;
		if (efd->is_active) {
			struct epoll_event ev;
			ev.data.ptr = efd;
			ev.events = efd->events = ev_fd_to_epoll(events);
			if (efd->is_set)
				rc = epoll_ctl(efd->mgr->epollfd, EPOLL_CTL_MOD, efd->fd, &ev);
			else {
				rc = epoll_ctl(efd->mgr->epollfd, EPOLL_CTL_ADD, efd->fd, &ev);
				if (rc == 0)
					efd->is_set = 1;
			}
		}
		else if (efd->is_set) {
			rc = epoll_ctl(efd->mgr->epollfd, EPOLL_CTL_DEL, efd->fd, 0);
			if (rc == 0)
				efd->is_set = 0;
		}
		if (rc < 0) {
			efd->has_changed = 1;
			efd->mgr->efds_changed = 1;
		}
	}
#else
	efd->events = events;
#endif
}

void ev_fd_set_handler(struct ev_fd *efd, ev_fd_cb_t handler, void *closure)
{
	efd->handler = handler;
	efd->closure = closure;
}

static void fd_dispatch(struct ev_fd *efd, uint32_t events)
{
	efd->handler(efd, efd->fd, events, efd->closure);
	if (events & EV_FD_HUP) {
		if (efd->fd >= 0) {
#if WITH_EPOLL
			if (efd->is_set && (efd->auto_close || efd->auto_unref)) {
				efd->is_set = 0;
				efd->is_active = 0;
				if (efd->mgr)
					epoll_ctl(efd->mgr->epollfd, EPOLL_CTL_DEL, efd->fd, 0);
			}
#endif
			if (efd->auto_close) {
				close(efd->fd);
				efd->fd = -1;
			}
		}
		if (efd->auto_unref)
			ev_fd_unref(efd);
	}
}

static int efds_prepare(struct ev_mgr *mgr)
{
	int rc;
	struct ev_fd *efd;
#if WITH_EPOLL
	int s;
	struct epoll_event ev;
#endif

	mgr->efds_changed = 0;
	rc = 0;
	efd = mgr->efds;
	while (efd) {
#if WITH_EPOLL
		if (efd->is_active) {
			if (!efd->is_set) {
				efd->is_set = 1;
				efd->has_changed = 0;
				ev.data.ptr = efd;
				ev.events = ev_fd_to_epoll(efd->events);
				s = epoll_ctl(mgr->epollfd, EPOLL_CTL_ADD, efd->fd, &ev);
				if (s < 0)
					rc = s;
			}
			else if (efd->has_changed) {
				efd->has_changed = 0;
				ev.data.ptr = efd;
				ev.events = ev_fd_to_epoll(efd->events);
				s = epoll_ctl(mgr->epollfd, EPOLL_CTL_MOD, efd->fd, &ev);
				if (s < 0)
					rc = s;
			}
		}
		else if (efd->is_set) {
			efd->is_set = 0;
			efd->has_changed = 0;
			s = epoll_ctl(mgr->epollfd, EPOLL_CTL_DEL, efd->fd, 0);
			if (s < 0)
				rc = s;
		}
#else
		if (efd->is_active)
			rc = add_poll(mgr, efd->fd, ev_fd_to_poll(efd->events));
		efd->has_changed = 0;
#endif
		efd = efd->next;
	}
	return rc;
}

static void efds_cleanup(struct ev_mgr *mgr)
{
	struct ev_fd **pefd, *efd;

	if (mgr->efds_cleanup) {
		mgr->efds_cleanup = 0;
		pefd = &mgr->efds;
		efd = *pefd;
		while (efd) {
			if (efd->is_deleted) {
#if WITH_EPOLL
				if (efd->is_set)
					epoll_ctl(mgr->epollfd, EPOLL_CTL_DEL, efd->fd, 0);
#endif
				if (efd->auto_close && efd->fd >= 0)
					close(efd->fd);
				*pefd = efd->next;
				free(efd);
				efd = *pefd;
			}
			else {
				pefd = &efd->next;
				efd = efd->next;
			}
		}
	}
}

/******************************************************************************/
/******************************************************************************/
/** SECTION ev_timer                                                         **/
/******************************************************************************/
/******************************************************************************/

/**
 * Remove deleted timers
 */
static void timers_cleanup(struct ev_mgr *mgr)
{
	struct ev_timer *timer, **prvtim;

	if (mgr->timers_cleanup) {
		mgr->timers_cleanup = 0;
		prvtim = &mgr->timers;
		while ((timer = *prvtim)) {
			if (!timer->is_deleted)
				prvtim = &timer->next;
			else {
				*prvtim = timer->next;
				free(timer);
			}
		}
	}
}

/**
 * returns the current value of the time in ms
 */
static time_unit_t now_ut()
{
#if __ZEPHYR__
	return MS2UT(k_uptime_get());
#else
	struct timespec ts;
	clock_gettime(CLOCK, &ts);
	return (time_unit_t)ts.tv_sec * ONESEC + (time_unit_t)NS2UT(ts.tv_nsec);
#endif
}

/**
 * Arm the timer
 */
static int timer_arm(struct ev_mgr *mgr, time_unit_t when)
{
	if (when < mgr->last_timer || mgr->last_timer == 0) {
#if WITH_TIMERFD
		int rc;
		struct itimerspec its;
		lldiv_t dr;

		if (mgr->last_timer && when >= mgr->last_timer)
			return 0;

		/* set the timer */
		dr = lldiv((long long)when, ONESEC);
		its.it_value.tv_sec = (time_t)dr.quot;
		its.it_value.tv_nsec = (time_t)UT2NS(dr.rem);
		its.it_interval.tv_sec = 0;
		its.it_interval.tv_nsec = 0;
		rc = timerfd_settime(mgr->timerfd, TFD_TIMER_ABSTIME, &its, 0);
		if (rc < 0)
			return -errno;
		mgr->last_timer = when;
#else
		mgr->last_timer = when;
		if (mgr->state == Waiting)
			ev_mgr_wakeup(mgr);
#endif
	}
	return 0;
}

/**
 * Compute the next time for blowing an event
 * Then arm the timer.
 */
static int timer_set(struct ev_mgr *mgr, time_unit_t upper)
{
	struct ev_timer *timer;
	time_unit_t lower, lo, up;

	timers_cleanup(mgr);

	/* get the next slice */
	lower = 0;
	timer = mgr->timers;
	while (timer) {
		if (timer->is_active) {
			lo = timer->next_ut;
			if (lo <= upper) {
				up = lo + timer->accuracy_ut;
				if (up <= lower) {
					lower = lo;
					upper = up;
				}
				else {
					if (lower < lo)
						lower = lo;
					if (up < upper)
						upper = up;
				}
			}
		}
		timer = timer->next;
	}

	/* activate the timer */
	return timer_arm(mgr, lower ? ((lower + upper) >> 1) : 0);
}

/**
 * dispatch the timer events
 */
static void timer_dispatch(
	struct ev_mgr *mgr
) {
	struct ev_timer *timer, **prvtim;
	time_unit_t now;

	/* extract expired timers */
	now = now_ut();
	prvtim = &mgr->timers;
	while ((timer = *prvtim)) {
		/* process the timer */
		if (timer->is_active && timer->next_ut <= now) {
			timer->handler(timer, timer->closure, timer->decount);
			/* hack, hack, hack: below, just ignore blind events */
			do { timer->next_ut += timer->period_ut; } while(timer->next_ut <= now);
			if (timer->decount) {
				/* deactivate or delete if counted down */
				timer->decount--;
				if (!timer->decount) {
					timer->is_active = 0;
					if (timer->auto_unref)
						timer->is_deleted = 1;
					else
						timer->next_ut = TIME_UNIT_MAX;
				}
			}
		}
		/* either delete or add to tlist */
		if (!timer->is_deleted)
			prvtim = &timer->next;
		else {
			*prvtim = timer->next;
			free(timer);
		}
	}
}

#if WITH_TIMERFD
/**
 * function to handle a timer event: tries to read the timerfd
 * and if needed dispatch its value.
 */
static int timer_event(
	struct ev_mgr *mgr
) {
	uint64_t count;
	int rc = (int)read(mgr->timerfd, &count, sizeof count);
	if (rc < 0)
		return -errno;
	mgr->last_timer = 0;
	if (count > 0)
		timer_dispatch(mgr);
	return 0;
}
#endif

/* create a new timer object */
int ev_mgr_add_timer(
		struct ev_mgr *mgr,
		struct ev_timer **ptimer,
		int absolute,
		time_t start_sec,
		unsigned start_ms,
		unsigned count,
		unsigned period_ms,
		unsigned accuracy_ms,
		ev_timer_cb_t handler,
		void *closure,
		int autounref
) {
	int rc;
	struct ev_timer *timer;

	timer = malloc(sizeof *timer);
	if (!timer)
		rc = X_ENOMEM;
	else {
		timer->mgr = mgr;
		timer->handler = handler;
		timer->closure = closure;
		timer->decount = count;
		timer->period_ut = MS2UT(GETTM(period_ms, DEFAULT_PERIOD_MS, PERIOD_MIN_MS));
		timer->accuracy_ut = MS2UT(GETTM(accuracy_ms, DEFAULT_ACCURACY_MS, ACCURACY_MIN_MS));
#if !__ZEPHYR__
		if (absolute)
			start_sec -= time(NULL);
#endif
		timer->next_ut = now_ut() + MS2UT(start_sec * 1000 + start_ms);
		timer->is_deleted = 0;
		timer->auto_unref = !!autounref;
		timer->is_active = 1;
		timer->refcount = 1;
		timer->next = mgr->timers;
		mgr->timers = timer;
		rc = timer_set(mgr, TIME_UNIT_MAX);
		if (rc < 0) {
			timer->is_deleted = 1;
			timer->is_active = 0;
		}
	}
	*ptimer = timer;
	return rc;
}

/* add one reference to the timer */
struct ev_timer *ev_timer_addref(struct ev_timer *timer)
{
	if (timer)
		__atomic_add_fetch(&timer->refcount, 1, __ATOMIC_RELAXED);
	return timer;
}

/* remove one reference to the timer */
void ev_timer_unref(struct ev_timer *timer)
{
	if (timer && !__atomic_sub_fetch(&timer->refcount, 1, __ATOMIC_RELAXED)) {
		timer->is_active = 0;
		timer->is_deleted = 1;
		timer->mgr->timers_cleanup = 1;
	}
}

/* modify the period of the timer */
void ev_timer_modify_period(struct ev_timer *timer, unsigned period_ms)
{
	timer->period_ut = MS2UT(GETTM(period_ms, DEFAULT_PERIOD_MS, PERIOD_MIN_MS));
	timer->next_ut = now_ut() + timer->period_ut;
	timer_set(timer->mgr, TIME_UNIT_MAX);
}

/******************************************************************************/

int ev_mgr_add_prepare(
	struct ev_mgr *mgr,
	struct ev_prepare **pprep,
	ev_prepare_cb_t handler,
	void *closure
) {
	int rc;
	struct ev_prepare *prep;

	prep = malloc(sizeof *prep);
	if (!prep)
		rc = X_ENOMEM;
	else {
		prep->handler = handler;
		prep->closure = closure;
		prep->mgr = mgr;
		prep->refcount = 1;
		prep->next = mgr->preparers;
		mgr->preparers = prep;
		rc = 0;
	}
	*pprep = prep;
	return rc;
}

struct ev_prepare *ev_prepare_addref(struct ev_prepare *prep)
{
	if (prep)
		__atomic_add_fetch(&prep->refcount, 1, __ATOMIC_RELAXED);
	return prep;
}

void ev_prepare_unref(struct ev_prepare *prep)
{
	if (prep && !__atomic_sub_fetch(&prep->refcount, 1, __ATOMIC_RELAXED)) {
		if (prep->mgr)
			prep->mgr->preparers_cleanup = 1;
		else
			free(prep);
	}
}

static void preparers_cleanup(struct ev_mgr *mgr)
{
	struct ev_prepare *prep, **pprep;

	if (mgr->preparers_cleanup) {
		mgr->preparers_cleanup = 0;
		pprep = &mgr->preparers;
		prep = mgr->preparers;
		while(prep) {
			if (prep->refcount) {
				pprep = &prep->next;
				prep = prep->next;
			}
			else {
				*pprep = prep->next;
				free(prep);
				prep = *pprep;
			}
		}
	}
}

static void preparers_prepare(struct ev_mgr *mgr)
{
	struct ev_prepare *prep;

	for (prep = mgr->preparers ; prep ; prep = prep->next) {
		if (prep->refcount)
			prep->handler(prep, prep->closure);
	}
}

/******************************************************************************/
/******************************************************************************/
/** SECTION ev_mgr internals                                                 **/
/******************************************************************************/
/******************************************************************************/

/**
 * Clean the event loop manager
 */
static void do_cleanup(struct ev_mgr *mgr)
{
	efds_cleanup(mgr);
	timers_cleanup(mgr);
	preparers_cleanup(mgr);
}

#if !WITH_EPOLL
static int add_poll(struct ev_mgr *mgr, int fd, short events)
{
	if (mgr->nrpollfds == mgr->szpollfds) {
		uint16_t nxtsz = mgr->szpollfds + 4;
		size_t szall = nxtsz * sizeof *mgr->pollfds;
		struct pollfd *pollfds = realloc(mgr->pollfds, szall);
		if (pollfds == NULL)
			return X_ENOMEM;
		mgr->pollfds = pollfds;
		mgr->szpollfds = nxtsz;
	}
	mgr->pollfds[mgr->nrpollfds].fd = fd;
	mgr->pollfds[mgr->nrpollfds].events = (short)events;
	mgr->nrpollfds++;
	return 0;
}
#endif


/**
 * Prepare the event loop manager
 */
static int do_prepare(struct ev_mgr *mgr, time_unit_t wakeup_ms)
{
	int rc;

	if (mgr->state != Idle && mgr->state != Ready)
		rc = X_ENOTSUP;
	else {
		mgr->state = Preparing;
		do_cleanup(mgr);
		preparers_prepare(mgr);
		timer_set(mgr, wakeup_ms);
		if (!mgr->efds_changed)
			rc = 0;
		else {
#if !WITH_EPOLL
			mgr->nrpollfds = IDX_FDS0;
#endif
			rc = efds_prepare(mgr);
		}
		mgr->state = Ready;
	}
	return rc;
}

/**
 * Wait for an event
 */
static int do_wait(struct ev_mgr *mgr, int timeout_ms)
{
	int rc = 0;

	if (mgr->state != Ready)
		rc = X_ENOTSUP;
	else {
#if WAKEUP_TGKILL
		mgr->tid = gettid();
#elif WAKEUP_THREAD_KILL
		mgr->tid = x_thread_self();
#endif
		mgr->state = Waiting;
#if !WITH_TIMERFD
		if (mgr->last_timer) {
			time_unit_t nxt = mgr->last_timer;
			time_unit_t now = now_ut();
			if (now > nxt)
				timeout_ms = 0;
			else {
				int delay = (int)UT2MS(nxt - now);
				if (timeout_ms < 0 || timeout_ms > delay)
					timeout_ms = delay;
			}
		}
#endif
		if (timeout_ms < 0)
			timeout_ms = -1;
#if WITH_EPOLL
		rc = epoll_wait(mgr->epollfd, &mgr->event, 1, timeout_ms);
#else
		rc = poll(mgr->pollfds, mgr->nrpollfds, timeout_ms);
#endif
		if (rc < 1) {
#if WITH_EPOLL
			mgr->event.events = 0;
#endif
#if WITH_TIMERFD
			mgr->state = Idle;
			rc = rc ? -errno : rc;
#else
			if (rc == 0) {
				mgr->state = Pending;
				mgr->last_timer = 0;
				rc = 1;
			}
			else {
				mgr->state = Idle;
				rc = -errno;
			}
#endif
		}
		else {
			mgr->state = Pending;
#if WAKEUP_EVENTFD || WAKEUP_PIPE
#if WITH_EPOLL
			if (mgr->event.data.ptr == mgr) {
#else
			if (mgr->pollfds[IDX_SIGNAL].revents) {
#endif
#if WAKEUP_EVENTFD
				uint64_t x;
				read(mgr->eventfd, &x, sizeof x);
#else
				char x;
				read(mgr->pipefds[0], &x, sizeof x);
#endif
#if WITH_EPOLL
				mgr->event.events = 0;
				mgr->state = Idle;
				rc = X_EINTR;
#else
				rc--;
				if (rc == 0) {
					mgr->state = Idle;
					rc = X_EINTR;
				}
#endif
			}
#endif
		}
	}
	return rc;
}

/**
 * dispatch latest found event if any
 */
static void do_dispatch(struct ev_mgr *mgr)
{
	struct ev_fd *efd;
#if !WITH_EPOLL
	struct pollfd *it, *end;
#endif

	if (mgr->state == Pending) {
		mgr->state = Dispatching;
#if !WITH_TIMERFD
		timer_dispatch(mgr);
#endif
#if WITH_EPOLL
		efd = mgr->event.data.ptr;
		if (efd)
			fd_dispatch(efd, ev_fd_from_epoll(mgr->event.events));
#if WITH_TIMERFD
		else
			timer_event(mgr);
#endif
#else
#if WITH_TIMERFD
		if (mgr->pollfds[IDX_TIME].revents)
			timer_event(mgr);
#endif
		it = &mgr->pollfds[IDX_FDS0];
		end = &mgr->pollfds[mgr->nrpollfds];
		for ( ; it != end ; it++) {
			if (it->revents) {
				for (efd = mgr->efds ; efd ; efd = efd->next)
					if (efd->fd == it->fd) {
						fd_dispatch(efd, ev_fd_from_poll(it->revents));
						break;
					}
			}
		}
#endif
		mgr->state = Idle;
	}
}

/******************************************************************************/
/******************************************************************************/
/** SECTION ev_mgr PUBLIC                                                    **/
/******************************************************************************/
/******************************************************************************/

/**
 * wakeup the event loop if needed by sending
 * an event.
 */
int ev_mgr_wakeup(struct ev_mgr *mgr)
{
#if WAKEUP_TGKILL
	tgkill(getpid(), mgr->tid, SIGURG);
#elif WAKEUP_THREAD_KILL
	x_thread_kill(mgr->tid, SIGURG);
#elif WAKEUP_EVENTFD
	uint64_t x = 1;
	write(mgr->eventfd, &x, sizeof x);
#elif WAKEUP_PIPE
	char x = 1;
	write(mgr->pipefds[1], &x, sizeof x);
#endif
	return 1;
}

/**
 * Returns the current holder
 */
void *ev_mgr_holder(struct ev_mgr *mgr)
{
	return mgr->holder;
}

/* change the holder */
void *ev_mgr_try_change_holder(struct ev_mgr *mgr, void *holder, void *next)
{
	if (mgr->holder == holder)
		mgr->holder = next;
	return mgr->holder;
}

/**
 * prepare the ev_mgr to run
 */
int ev_mgr_prepare(struct ev_mgr *mgr)
{
	return do_prepare(mgr, TIME_UNIT_MAX);
}

/**
 * prepare the ev_mgr to run with a wakeup
 */
int ev_mgr_prepare_with_wakeup(struct ev_mgr *mgr, int wakeup_ms)
{
	return do_prepare(mgr, wakeup_ms >= 0 ? now_ut() + MS2UT(wakeup_ms) : TIME_UNIT_MAX);
}

/**
 * wait an event
 */
int ev_mgr_wait(struct ev_mgr *mgr, int timeout_ms)
{
	return do_wait(mgr, timeout_ms);
}

/**
 * dispatch latest event
 */
void ev_mgr_dispatch(struct ev_mgr *mgr)
{
	do_dispatch(mgr);
}

/**
 * Run the event loop is set.
 */
int ev_mgr_run(struct ev_mgr *mgr, int timeout_ms)
{
	int rc;

	rc = do_prepare(mgr, TIME_UNIT_MAX);
	if (rc >= 0) {
		rc = do_wait(mgr, timeout_ms);
		if (rc > 0)
			do_dispatch(mgr);
	}

	return rc;
}

int ev_mgr_can_run(struct ev_mgr *mgr)
{
	return mgr->state == Idle;
}

void ev_mgr_recover_run(struct ev_mgr *mgr)
{
	mgr->state = Idle;
}

/* pollable file descriptor */
int ev_mgr_get_fd(struct ev_mgr *mgr)
{
#if WITH_EPOLL
	return mgr->epollfd;
#else
	return X_ENOTSUP;
#endif
}

/* create an event manager */
int ev_mgr_create(struct ev_mgr **result)
{
#if WITH_EPOLL && (WAKEUP_EVENTFD || WAKEUP_PIPE || WITH_TIMERFD)
	struct epoll_event ee;
#endif
	struct ev_mgr *mgr;
	int rc;

	/* creates the ev_mgr on need */
	mgr = calloc(1, sizeof *mgr);
	if (!mgr) {
		RP_ERROR("out of memory");
		rc = X_ENOMEM;
		goto error;
	}

	/* init */
	mgr->refcount = 1;
	mgr->state = Idle;
	mgr->last_timer = 0;
#if WITH_EPOLL
	mgr->epollfd = -1;
#endif
#if WAKEUP_EVENTFD
	mgr->eventfd = -1;
#elif WAKEUP_PIPE
	mgr->pipefds[0] = -1;
	mgr->pipefds[1] = -1;
#endif
#if WITH_TIMERFD
	mgr->timerfd = -1;
#endif

	/* create the event loop */
#if WITH_EPOLL
	mgr->epollfd = epoll_create1(EPOLL_CLOEXEC);
	if (mgr->epollfd < 0) {
		rc = -errno;
		RP_ERROR("can't make new epollfd");
		goto error2;
	}
#endif

#if WAKEUP_EVENTFD
	/* create signaling */
	mgr->eventfd = eventfd(0, EFD_CLOEXEC|EFD_SEMAPHORE);
	if (mgr->eventfd < 0) {
		rc = -errno;
		RP_ERROR("can't make eventfd for events");
		goto error2;
	}

#if WITH_EPOLL
	ee.events = EPOLLIN;
	ee.data.ptr = mgr;
	rc = epoll_ctl(mgr->epollfd, EPOLL_CTL_ADD, mgr->eventfd, &ee);
#else
	rc = add_poll(mgr, mgr->eventfd, POLLIN);
#endif
	if (rc < 0) {
		rc = -errno;
		RP_ERROR("can't poll the eventfd");
		goto error2;
	}
#elif WAKEUP_PIPE
	rc = pipe2(mgr->pipefds, O_CLOEXEC);
	if (rc < 0) {
		rc = -errno;
		RP_ERROR("can't make pipes for events");
		goto error2;
	}

#if WITH_EPOLL
	ee.events = EPOLLIN;
	ee.data.ptr = mgr;
	rc = epoll_ctl(mgr->epollfd, EPOLL_CTL_ADD, mgr->pipefds[0], &ee);
#else
	rc = add_poll(mgr, mgr->pipefds[0], POLLIN);
#endif
	if (rc < 0) {
		rc = -errno;
		RP_ERROR("can't poll the pipes");
		goto error2;
	}
#endif

#if WITH_TIMERFD
	/* create the timerfd */
	mgr->timerfd = timerfd_create(CLOCK, TFD_NONBLOCK|TFD_CLOEXEC);
	if (mgr->timerfd < 0) {
		rc = -errno;
		RP_ERROR("can't make timerfd");
		goto error2;
	}

	/* add the timer to the polls */
#if WITH_EPOLL
	ee.events = EPOLLIN;
	ee.data.ptr = 0;
	rc = epoll_ctl(mgr->epollfd, EPOLL_CTL_ADD, mgr->timerfd, &ee);
#else
	rc = add_poll(mgr, mgr->timerfd, POLLIN);
#endif
	if (rc < 0) {
		rc = -errno;
		RP_ERROR("can't poll the timer");
		goto error2;
	}
#endif

	/* finalize the creation */
	*result = mgr;
	return 0;

#if WITH_EPOLL || WAKEUP_EVENTFD || WAKEUP_PIPE || WITH_TIMERFD
error2:
	ev_mgr_unref(mgr);
#endif

error:
	*result = 0;
	return rc;
}

struct ev_mgr *ev_mgr_addref(struct ev_mgr *mgr)
{
	if (mgr)
		__atomic_add_fetch(&mgr->refcount, 1, __ATOMIC_RELAXED);
	return mgr;
}

void ev_mgr_unref(struct ev_mgr *mgr)
{
	struct ev_fd *efd;
	struct ev_timer *timer;
	struct ev_prepare *prep;
	if (mgr) {
		do_cleanup(mgr);
		if (!__atomic_sub_fetch(&mgr->refcount, 1, __ATOMIC_RELAXED)) {
			for (prep = mgr->preparers ; prep ; prep = prep->next)
				prep->mgr = 0;
			for (timer = mgr->timers ; timer ; timer = timer->next)
				timer->mgr = 0;
			for (efd = mgr->efds ; efd ; efd = efd->next)
				efd->mgr = 0;
#if WITH_EPOLL
			if (mgr->epollfd >= 0)
				close(mgr->epollfd);
#else
			free(mgr->pollfds);
#endif
#if WAKEUP_EVENTFD
			if (mgr->eventfd >= 0)
				close(mgr->eventfd);
#elif WAKEUP_PIPE
			if (mgr->pipefds[0] >= 0)
				close(mgr->pipefds[0]);
			if (mgr->pipefds[1] >= 0)
				close(mgr->pipefds[1]);
#endif
#if WITH_TIMERFD
			if (mgr->timerfd >= 0)
				close(mgr->timerfd);
#endif
			free(mgr);
		}
	}
}
