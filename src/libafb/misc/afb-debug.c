/*
 Copyright (C) 2015-2022 IoT.bzh Company

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

#include "libafb-config.h"

#if WITH_AFB_DEBUG

#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#if WITH_CALL_PERSONALITY
#include <sys/personality.h>
#endif

#include <rp-utils/rp-verbose.h>

#include "utils/namecmp.h"

static char key_env_break[] = "AFB_DEBUG_BREAK";
static char key_env_wait[] = "AFB_DEBUG_WAIT";
static char separs[] = ", \t\n";

/*
 * Checks whether the 'key' is in the 'list'
 * Return 1 if it is in or 0 otherwise
 */
static int has_key(const char *key, const char *list)
{
	if (list && key) {
		list += strspn(list, separs);
		while (*list) {
			size_t s = strcspn(list, separs);
			if (!namencmp(list, key, s) && !key[s])
				return 1;
			list += s;
			list += strspn(list, separs);
		}
	}
	return 0;
}

static void indicate(const char *key)
{
#if !defined(NO_AFB_DEBUG_FILE_INDICATION)
	char filename[200];
	int fd;

	snprintf(filename, sizeof filename, "/tmp/afb-debug-%ld", (long)getpid());
	if (key) {
		fd = creat(filename, 0644);
		write(fd, key, strlen(key));
		close(fd);
	} else {
		unlink(filename);
	}
#endif
}

static void handler(int signum)
{
}

void afb_debug_wait(const char *key)
{
	struct sigaction sa, psa;
	sigset_t ss, oss;

	key = key ?: "NULL";
	RP_NOTICE("DEBUG WAIT before %s", key);
	sigfillset(&ss);
	sigdelset(&ss, SIGINT);
	sigprocmask(SIG_SETMASK, &ss, &oss);
	sigemptyset(&ss);
	sigaddset(&ss, SIGINT);
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = handler;
	sigaction(SIGINT, &sa, &psa);
	indicate(key);
	sigwaitinfo(&ss, NULL);
	sigaction(SIGINT, &psa, NULL);
	indicate(NULL);
	sigprocmask(SIG_SETMASK, &oss, NULL);
	RP_NOTICE("DEBUG WAIT after %s", key);
#if WITH_CALL_PERSONALITY
	personality((unsigned long)-1L);
#endif
}

void afb_debug_break(const char *key)
{
	struct sigaction sa, psa;

	key = key ?: "NULL";
	RP_NOTICE("DEBUG BREAK before %s", key);
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = handler;
	sigaction(SIGINT, &sa, &psa);
	raise(SIGINT);
	sigaction(SIGINT, &psa, NULL);
	RP_NOTICE("DEBUG BREAK after %s", key);
}

void afb_debug(const char *key)
{
#if WITH_ENVIRONMENT
	if (has_key(key, secure_getenv(key_env_wait)))
		afb_debug_wait(key);
	if (has_key(key, secure_getenv(key_env_break)))
		afb_debug_break(key);
#endif
}

#endif

