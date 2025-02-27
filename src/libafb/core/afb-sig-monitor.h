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

#pragma once

#include "../libafb-config.h"

#if WITH_SIG_MONITOR_SIGNALS
/**
 * initialise signal monitoring
 *
 * @param enable non 0 to activate active signal monitoring
 *
 * @return 0 in case of success
 */
extern int afb_sig_monitor_init(int enable);
#endif

#if WITH_SIG_MONITOR_TIMERS
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
#endif


#if WITH_SIG_MONITOR_SIGNALS && WITH_SIG_MONITOR_FOR_CALL
/**
 * When monitoring is enabled (@see function afb_sig_monitor_init)
 * run the given function within monitoring of signals and timeout.
 * So the function is called with a signal number of zero when it is
 * run directly. But if it fails by raising a signal or by taking
 * more time than set by timeout, its execution is cancelled and the
 * given function is called a second time with the value of the
 * signal that produced the cancellation (SIGALRM for timeout expiration).
 *
 * When monitoring is not enable, the function is just directly
 * called with signal == 0.
 *
 * @param timeout   timeout for the job in secondes (<= 0 for no timeout)
 * @param function  the job to run as void callback function
 * @param arg       the arguments to pass to the job
 */
extern void afb_sig_monitor_run(int timeout, void (*function)(int sig, void*), void *arg);

/**
 * Call the function the function directly with signal == 0 and the given arg.
 *
 * If the monitoring is enabled and is active for the current thread (activated
 * through call to afb_sig_monitor_run), and if a signal is trapped, the function
 * is called with the value of the signal, letting the function cleanup the process
 * state before returning to the main entry point (the one set
 * with afb_sig_monitor_run)
 *
 * @param function  the job to run as void callback function
 * @param arg       the arguments to pass to the job
 */
extern void afb_sig_monitor_do(void (*function)(int sig, void*), void *arg);

/**
 * If the monitoring is active this function calls afb_sig_monitor_do
 * otherwise it calls afb_sig_monitor_run woth no timeout.
 *
 * @param timeout   timeout for the job in secondes (<= 0 for no timeout)
 * @param function  the job to run as void callback function
 * @param arg       the arguments to pass to the job
 */
extern void afb_sig_monitor_do_run(int timeout, void (*function)(int sig, void*), void *arg);
#endif


#if WITH_SIG_MONITOR_DUMPSTACK
/**
 * Dumps the current stack
 */
extern void afb_sig_monitor_dumpstack();

/**
 * enable or disable stack dumps
 */
extern void afb_sig_monitor_dumpstack_enable(int enable);
#endif


#if !WITH_SIG_MONITOR_SIGNALS
static inline int afb_sig_monitor_init(int enable) { return 0; }
#endif
#if !WITH_SIG_MONITOR_TIMERS
static inline void afb_sig_monitor_clean_timeouts() {}
static inline int afb_sig_monitor_init_timeouts() { return 0; }
#endif
#if !(WITH_SIG_MONITOR_SIGNALS && WITH_SIG_MONITOR_FOR_CALL)
static inline void afb_sig_monitor_run(int timeout, void (*function)(int sig, void*), void *arg) { function(0, arg); }
static inline void afb_sig_monitor_do(void (*function)(int sig, void*), void *arg) { function(0, arg); }
static inline void afb_sig_monitor_do_run(int timeout, void (*function)(int sig, void*), void *arg) { function(0, arg); }
#endif
#if !WITH_SIG_MONITOR_DUMPSTACK
static inline void afb_sig_monitor_dumpstack() {}
static inline void afb_sig_monitor_dumpstack_enable(int enable) {}
#endif
