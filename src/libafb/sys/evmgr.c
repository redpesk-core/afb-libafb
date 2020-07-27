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
#include <stdint.h>
#include <unistd.h>


#if WITH_FDEV_SYSTEMD
# include <systemd/sd-event.h>
# include "sys/fdev-systemd.h"
# include "sys/systemd.h"
#elif WITH_FDEV_EPOLL
# include "sys/fdev-epoll.h"
#elif WITH_FDEV_POLL
# include "sys/fdev-poll.h"
#elif WITH_FDEV_SELECT
# include "sys/fdev-select.h"
#else
# error some FDEV is expected
#endif

#include "sys/evmgr.h"
#include "sys/fdev.h"
#include "sys/verbose.h"
#include "sys/x-errno.h"

/** Description of handled event loops */
struct evmgr
{
#if WITH_FDEV_SYSTEMD
	struct sd_event *imgr; /**< the systemd event loop */
#elif WITH_FDEV_EPOLL
	struct fdev_epoll *imgr;
#elif WITH_FDEV_POLL
	struct fdev_poll *imgr;
#elif WITH_FDEV_SELECT
	struct fdev_select *imgr;
#endif
	void *holder;          /**< holder of the evmgr */
	struct fdev *sigfdev;
	int osigfd;
	unsigned state;        /**< encoded state */
};

#define EVLOOP_STATE_WAIT           1U
#define EVLOOP_STATE_RUN            2U

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
/******************************************************************************/
#if WITH_FDEV_SYSTEMD
/**
 * Run the event loop is set.
 */
int evmgr_systemd_wait_and_dispatch(struct evmgr *evmgr, int timeout_ms)
{
	int rc;
	struct sd_event *se;

	se = evmgr->imgr;
	rc = sd_event_prepare(se);
	if (rc < 0) {
		CRITICAL("sd_event_prepare returned an error (state: %d): %m", sd_event_get_state(se));
		abort();
	} else {
		if (rc == 0) {
			rc = sd_event_wait(se, (uint64_t)(int64_t)(timeout_ms < 0 ? -1 : (1000 * timeout_ms)));
			if (rc < 0)
				ERROR("sd_event_wait returned an error (state: %d): %m", sd_event_get_state(se));
		}
		evmgr->state = EVLOOP_STATE_RUN;
		if (rc > 0) {
			rc = sd_event_dispatch(se);
			if (rc < 0)
				ERROR("sd_event_dispatch returned an error (state: %d): %m", sd_event_get_state(se));
		}
	}
	return rc;
}
#endif

/**
 * Run the event loop is set.
 */
int evmgr_run(struct evmgr *evmgr, int timeout_ms)
{
	int rc __attribute__((unused));

	evmgr->state = EVLOOP_STATE_WAIT|EVLOOP_STATE_RUN;
#if WITH_FDEV_SYSTEMD
	rc = evmgr_systemd_wait_and_dispatch(evmgr, timeout_ms);
#elif WITH_FDEV_EPOLL
	rc = fdev_epoll_wait_and_dispatch(evmgr->imgr, timeout_ms);
#elif WITH_FDEV_POLL
	rc = fdev_poll_wait_and_dispatch(evmgr->imgr, timeout_ms);
#elif WITH_FDEV_SELECT
	rc = fdev_select_wait_and_dispatch(evmgr->imgr, timeout_ms);
#endif
	evmgr->state = 0;
	return rc;
}

int evmgr_add(struct fdev **fdev, struct evmgr *evmgr, int fd)
{
#if WITH_FDEV_SYSTEMD
	*fdev = fdev_systemd_create(evmgr->imgr, fd);
#elif WITH_FDEV_EPOLL
	*fdev = fdev_epoll_add(evmgr->imgr, fd);
#elif WITH_FDEV_POLL
	*fdev = fdev_poll_add(evmgr->imgr, fd);
#elif WITH_FDEV_SELECT
	*fdev = fdev_select_add(evmgr->imgr, fd);
#endif
	return *fdev ? 0 : -errno;
}

static int imgr_create(struct evmgr *evmgr)
{
#if WITH_FDEV_SYSTEMD
	evmgr->imgr = systemd_get_event_loop();
#elif WITH_FDEV_EPOLL
	evmgr->imgr = fdev_epoll_create();
#elif WITH_FDEV_POLL
	evmgr->imgr = fdev_epoll_create();
#elif WITH_FDEV_SELECT
	evmgr->imgr = fdev_select_create();
#endif
	return evmgr->imgr ? 0 : X_ENOTSUP;
}

static void imgr_destroy(struct evmgr *evmgr)
{
#if WITH_FDEV_SYSTEMD
#elif WITH_FDEV_EPOLL
	fdev_epoll_destroy(evmgr->imgr);
#elif WITH_FDEV_POLL
	fdev_poll_destroy(evmgr->imgr);
#elif WITH_FDEV_SELECT
	fdev_select_destroy(evmgr->imgr);
#endif
}

/******************************************************************************/
/******************************************************************************/
/******  event signal manager                                            ******/
/******************************************************************************/
/******************************************************************************/
/**
 * Internal callback for evmgr management.
 * The effect of this function is hidden: it exits
 * the waiting poll if any. Then it wakes up a thread
 * awaiting the evmgr using signal.
 */
static void sig_on_event(void *closure, uint32_t event, struct fdev *fdev)
{
	__attribute__((unused)) struct evmgr *evmgr = closure;
	int efd = fdev_fd(fdev);
	uint64_t x;
	read(efd, &x, sizeof x);
}

#if WITH_EVENTFD
#include <sys/eventfd.h>
/**
 */
static int sig_create(struct evmgr *evmgr)
{
	int efd, rc;
	struct fdev *fdev;

	/* creates the eventfd for waking up polls */
	efd = eventfd(0, EFD_CLOEXEC|EFD_SEMAPHORE);
	if (efd < 0) {
		rc = -errno;
		ERROR("can't make eventfd for events");
		goto error;
	}

	/* put the eventfd in the event loop */
	rc = evmgr_add(&fdev, evmgr, efd);
	if (rc < 0) {
		ERROR("can't add eventfd");
		goto error2;
	}
	evmgr->sigfdev = fdev;
	evmgr->osigfd = efd;
	return 0;

error2:
	close(efd);
error:
	return rc;
}
#else
#include <unistd.h>
/**
 */
static int sig_create(struct evmgr *evmgr)
{
	int rc, fds[2];
	struct fdev *fdev;

	/* creates the eventfd for waking up polls */
	rc = pipe(fds);
	if (rc < 0) {
		ERROR("can't make pipe for events");
		goto error;
	}

	/* put the eventfd in the event loop */
	rc = evmgr_add(&fdev, evmgr, fds[0]);
	if (rc < 0) {
		ERROR("can't add pipe for events");
		goto error2;
	}
	evmgr->sigfdev = fdev;
	evmgr->osigfd = fds[1];
	return 0;

error2:
	close(fds[0]);
	close(fds[1]);
error:
	return -1;
}
#endif
/******************************************************************************/
/******************************************************************************/
/******  COMMON                                                          ******/
/******************************************************************************/
/******************************************************************************/
/**
 * wakeup the event loop if needed by sending
 * an event.
 */
void evmgr_wakeup(struct evmgr *evmgr)
{
	uint64_t x;

	if (evmgr->state & EVLOOP_STATE_WAIT) {
		x = 1;
		write(evmgr->osigfd, &x, sizeof x);
	}
}

/**
 */
void *evmgr_holder(struct evmgr *evmgr)
{
	return evmgr->holder;
}

/**
 */
int evmgr_release_if(struct evmgr *evmgr, void *holder)
{
	if (evmgr->holder != holder)
		return 0;
	evmgr->holder = 0;
	return 1;
}

/**
 */
int evmgr_try_hold(struct evmgr *evmgr, void *holder)
{
	if (!evmgr->holder)
		evmgr->holder = holder;
	return evmgr->holder == holder;
}

/**
 * prepare the evmgr to run
 */
void evmgr_prepare_run(struct evmgr *evmgr)
{
	evmgr->state = EVLOOP_STATE_WAIT|EVLOOP_STATE_RUN;
}

void evmgr_job_run(int signum, struct evmgr *evmgr)
{
	if (signum)
		evmgr->state = 0;
	else
		evmgr_run(evmgr, -1);
}

int evmgr_can_run(struct evmgr *evmgr)
{
	return !evmgr->state;
}

#if WITH_FDEV_EPOLL
int evmgr_get_epoll_fd(struct evmgr *evmgr)
{
	if (evmgr->imgr)
		return fdev_epoll_get_epoll_fd(evmgr->imgr);
	return X_EINVAL;
}
#endif

/**
 * Gets a sd_event item for the current thread.
 * @return a sd_event or NULL in case of error
 */
int evmgr_create(struct evmgr **result)
{
	struct evmgr *evmgr;
	int rc;

	/* creates the evmgr on need */
	evmgr = malloc(sizeof *evmgr);
	if (!evmgr) {
		ERROR("out of memory");
		rc = X_ENOMEM;
		goto error;
	}

	/* create the systemd event loop */
	rc = imgr_create(evmgr);
	if (rc < 0) {
		ERROR("can't make new event loop");
		goto error1;
	}

	rc = sig_create(evmgr);
	if (rc < 0) {
		ERROR("can't add the signaling");
		goto error2;
	}

	/* finalize the creation */
	fdev_set_events(evmgr->sigfdev, EPOLLIN);
	fdev_set_callback(evmgr->sigfdev, sig_on_event, evmgr);
	evmgr->holder = 0;
	evmgr->state = 0;
	*result = evmgr;
	return 0;

error2:
	imgr_destroy(evmgr);
error1:
	free(evmgr);
error:
	*result = 0;
	return rc;
}

