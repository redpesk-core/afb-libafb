/*
 * Copyright (C) 2015-2021 IoT.bzh Company
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

struct u16id2ptr;
struct u16id2bool;

/**********************************************************************/
/**        u16id2ptr                                                 **/
/**********************************************************************/

extern int u16id2ptr_create(struct u16id2ptr **pi2p);
extern void u16id2ptr_destroy(struct u16id2ptr **pi2p);
extern void u16id2ptr_dropall(struct u16id2ptr **pi2p);
extern int u16id2ptr_has(struct u16id2ptr *i2p, uint16_t id);
extern int u16id2ptr_add(struct u16id2ptr **pi2p, uint16_t id, void *ptr);
extern int u16id2ptr_set(struct u16id2ptr **pi2p, uint16_t id, void *ptr);
extern int u16id2ptr_put(struct u16id2ptr *i2p, uint16_t id, void *ptr);
extern int u16id2ptr_get(struct u16id2ptr *i2p, uint16_t id, void **ptr);
extern int u16id2ptr_drop(struct u16id2ptr **pi2p, uint16_t id, void **ptr);
extern int u16id2ptr_count(struct u16id2ptr *i2p);
extern int u16id2ptr_at(struct u16id2ptr *i2p, int index, uint16_t *pid, void **pptr);
extern void u16id2ptr_forall(
			struct u16id2ptr *i2p,
			void (*callback)(void*closure, uint16_t id, void *ptr),
			void *closure);

/**********************************************************************/
/**        u16id2bool                                                **/
/**********************************************************************/

extern int u16id2bool_create(struct u16id2bool **pi2b);
extern void u16id2bool_destroy(struct u16id2bool **pi2b);
extern void u16id2bool_clearall(struct u16id2bool **pi2b);
extern int u16id2bool_get(struct u16id2bool *i2b, uint16_t id);
extern int u16id2bool_set(struct u16id2bool **pi2b, uint16_t id, int value);
