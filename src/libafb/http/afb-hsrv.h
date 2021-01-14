/*
 Copyright (C) 2015-2021 IoT.bzh Company

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

#pragma once

#if WITH_LIBMICROHTTPD

struct afb_hsrv;
struct afb_hreq;
struct locale_root;

extern struct afb_hsrv *afb_hsrv_create();
extern void afb_hsrv_put(struct afb_hsrv *hsrv);

extern void afb_hsrv_stop(struct afb_hsrv *hsrv);
extern int afb_hsrv_start(struct afb_hsrv *hsrv, unsigned int connection_timeout);
extern int afb_hsrv_start_tls(struct afb_hsrv *hsrv, unsigned int connection_timeout, const char *cert, const char *key);
extern int afb_hsrv_set_cache_timeout(struct afb_hsrv *hsrv, int duration);
#if WITH_OPENAT
extern int afb_hsrv_add_alias(struct afb_hsrv *hsrv, const char *prefix, int dirfd, const char *alias, int priority, int relax);
#endif
extern int afb_hsrv_add_alias_path(struct afb_hsrv *hsrv, const char *prefix, const char *basepath, const char *alias, int priority, int relax);
extern int afb_hsrv_add_alias_root(struct afb_hsrv *hsrv, const char *prefix, struct locale_root *root, int priority, int relax);
extern int afb_hsrv_add_handler(struct afb_hsrv *hsrv, const char *prefix, int (*handler) (struct afb_hreq *, void *), void *data, int priority);
extern int afb_hsrv_add_interface(struct afb_hsrv *hsrv, const char *uri);
extern int afb_hsrv_add_interface_tcp(struct afb_hsrv *hsrv, const char *itf, uint16_t port);

extern void afb_hsrv_run(struct afb_hsrv *hsrv);

#endif
