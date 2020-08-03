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

/**
 * initialise signal monitoring
 *
 * @param enable non 0 to activate active signal monitoring
 *
 * @return 0 in case of success
 */
extern int afb_sig_monitor_init(int enable);

/**
 * Remove time out of the current thread
 */
extern void afb_sig_monitor_clean_timeouts();

/**
 * Creates a timer for the current thread
 *
 * @return  0 in case of success
 */
extern int afb_sig_monitor_init_timeouts();

/**
 * Run a job with signal monitoring if it has been set up perviously,
 * else just run the job with signal O for default action.
 *
 * @param timeout   timeout for the job in secondes
 *
 * @param function  the job to run as void callback function
 *
 * @param arg       the arguments to pass to the job
 */
extern void afb_sig_monitor_run(int timeout, void (*function)(int sig, void*), void *arg);


/**
 * Dumps the current stack
 */
extern void afb_sig_monitor_dumpstack();

