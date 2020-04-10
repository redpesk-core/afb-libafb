/*
 * Copyright (C) 2015-2020 IoT.bzh Company
 * Author: José Bollo <jose.bollo@iot.bzh>
 *
 * SPDX-License-Identifier: LGPL-3.0-only
 */

#pragma once

/**
 * Enum for Session/Token/Assurance middleware of bindings version 2 and 3.
 */
enum afb_session_flags_x2
{
       AFB_SESSION_LOA_MASK_X2 = 3,	/**< mask for LOA */

       AFB_SESSION_LOA_0_X2 = 0,	/**< value for LOA of 0 */
       AFB_SESSION_LOA_1_X2 = 1,	/**< value for LOA of 1 */
       AFB_SESSION_LOA_2_X2 = 2,	/**< value for LOA of 2 */
       AFB_SESSION_LOA_3_X2 = 3,	/**< value for LOA of 3 */

       AFB_SESSION_CHECK_X2 = 4,	/**< Requires token authentification */
       AFB_SESSION_REFRESH_X2 = 8,	/**< After token authentification, refreshes the token at end */
       AFB_SESSION_CLOSE_X2 = 16,	/**< After token authentification, closes the session at end */

       AFB_SESSION_NONE_X2 = 0		/**< nothing required */
};

