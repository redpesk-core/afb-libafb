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

/*
 * CAUTION!
 * the default setting uses an abstract socket path
 * be aware that this setting doesn't allow to enforce
 * DAC for accessing the socket and then would allow
 * anyone to create a such socket and usurpate the
 * supervisor.
 */
#if !defined(AFS_SUPERVISION_SOCKET)
#  define AFS_SUPERVISION_SOCKET "@urn:AGL:afs:supervision:socket" /* abstract */
#endif

/*
 * generated using
 * uuid -v 5 ns:URL urn:AGL:afs:supervision:interface:1
 */
#define AFS_SUPERVISION_INTERFACE_1 "86040e8d-eee5-5900-a129-3edb8da3ed46"


/**
 * packet initially sent by monitor at start
 */
struct afs_supervision_initiator
{
	char interface[37];	/**< zero terminated interface uuid */
	char extra[27];		/**< zero terminated extra computed here to be 64-37 */
};

#define AFS_SUPERVISION_APINAME      "."
#define AFS_SUPERVISOR_APINAME       "supervisor"
