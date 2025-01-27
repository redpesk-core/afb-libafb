/*
 Copyright (C) 2015-2025 IoT.bzh Company

 Author: Johann Gautier <johann.gautier@iot.bzh>

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

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <check.h>
#include <signal.h>

#include "sys/ev-mgr.h"

/*********************************************************************/

START_TEST (basic)
{
	int rc;
	int fd;
	struct ev_mgr *mgr;

	rc = ev_mgr_create(&mgr);
	ck_assert_int_eq(rc, 0);

	fd = ev_mgr_get_fd(mgr);
	ck_assert_int_ge(fd, 0);

	ck_assert_ptr_eq(mgr, ev_mgr_addref(mgr));
	ev_mgr_unref(mgr);
	ev_mgr_unref(mgr);
}
END_TEST

int readdata;

void readcb(struct ev_fd *efd, int fd, uint32_t revents, void *closure)
{
	ssize_t rc;
	rc = read(fd, &readdata, sizeof readdata);
	ck_assert_int_eq(rc, (ssize_t)(sizeof readdata));
	ck_assert_ptr_eq(closure, readcb);
}

START_TEST (fd)
{
	ssize_t szrc;
	int x;
	int rc;
	int fds[2];
	struct ev_mgr *mgr;
	struct ev_fd *efd;

	rc = ev_mgr_create(&mgr);
	ck_assert_int_eq(rc, 0);

	rc = pipe2(fds, O_CLOEXEC|O_DIRECT|O_NONBLOCK);
	ck_assert_int_eq(rc, 0);

	rc = ev_mgr_add_fd(mgr, &efd, fds[0], EV_FD_IN, readcb, readcb, 1, 1);
	ck_assert_int_eq(rc, 0);

	rc = ev_mgr_run(mgr, 100);
	ck_assert_int_eq(rc, 0);

	readdata = 0;
	x = 15151515;
	szrc = write(fds[1], &x, sizeof x);
	ck_assert_int_eq(szrc, sizeof x);

	rc = ev_mgr_run(mgr, 100);
	ck_assert_int_eq(rc, 1);
	ck_assert_int_eq(x, readdata);

	readdata = 0;
	ev_fd_set_events(efd, 0);
	szrc = write(fds[1], &x, sizeof x);
	ck_assert_int_eq(szrc, sizeof x);

	rc = ev_mgr_run(mgr, 100);
	ck_assert_int_eq(rc, 0);
	ck_assert_int_eq(0, readdata);

	ev_fd_set_events(efd, EV_FD_IN);
	rc = ev_mgr_run(mgr, 100);
	ck_assert_int_eq(rc, 1);
	ck_assert_int_eq(x, readdata);

	readdata = 0;
	ev_fd_unref(efd);
	szrc = write(fds[1], &x, sizeof x);
	ck_assert_int_eq(szrc, sizeof x);

	rc = ev_mgr_run(mgr, 100);
	ck_assert_int_eq(rc, 0);
	ck_assert_int_eq(0, readdata);

	ev_mgr_unref(mgr);
}
END_TEST

unsigned timerdata;

void timercb(struct ev_timer *timer, void *closure, unsigned decount)
{
	timerdata += decount;
}

START_TEST (timer)
{
	int rc;
	struct ev_mgr *mgr;
	struct ev_timer *timer;

	rc = ev_mgr_create(&mgr);
	ck_assert_int_eq(rc, 0);

	timerdata = 0;
	rc = ev_mgr_add_timer(mgr, &timer, 0, 0, 10, 3, 10, 1, timercb, 0, 1);
	ck_assert_int_eq(rc, 0);

	do {
		rc = ev_mgr_run(mgr, 100);
	}
	while(rc == 1);
	ck_assert_int_eq(rc, 0);
	ck_assert_uint_eq(timerdata, 1 + 2 + 3);

	ev_mgr_unref(mgr);
}
END_TEST

/*********************************************************************/

static Suite *suite;
static TCase *tcase;

void mksuite(const char *name) { suite = suite_create(name); }
void addtcase(const char *name) { tcase = tcase_create(name); suite_add_tcase(suite, tcase); }
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
	mksuite("ev-mgr");
		addtcase("ev-mgr");
			addtest(basic);
			addtest(fd);
			addtest(timer);
	return !!srun();
}
