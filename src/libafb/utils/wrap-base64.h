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

#include <stdlib.h>
#include <stdint.h>

#define wrap_base64_ok        0
#define wrap_base64_nomem     -1
#define wrap_base64_invalid   -2

/**
 * encode a buffer of data as a fresh allocated base64 string
 *
 * @param data        pointer to the head of the data to encode
 * @param datalen     length in byt of the data to encode
 * @param encoded     pointer to a pointer receiving the encoded value
 * @param encodedlen  pointer to a size receiving the encoded size
 * @param width       width of the lines or zero for one long line
 * @param pad         if not zero pads with = according standard
 * @param url         if not zero emit url variant of base64
 *
 * @return wrap_base64_ok in case of success
 *    or wrap_base64_nomem if allocation memory failed
 */
extern
int wrap_base64_encode(
		const uint8_t *data,
		size_t datalen,
		char **encoded,
		size_t *encodedlen,
		int width,
		int pad,
		int url);

/**
 * decode a base64 string as a fresh buffer of data
 *
 * @param data        pointer to base64 string to be decode
 * @param datalen     length of the string to decode
 * @param encoded     pointer to a pointer receiving the decoded value
 * @param encodedlen  pointer to a size receiving the decoded size
 * @param url         indicates processing of variants
 *                     - url = 0: any variant (even mixed)
 *                     - url > 0: only url variant
 *                     - url < 0: only standard variant
 *
 * @return wrap_base64_ok in case of success,
 *  wrap_base64_nomem if allocation memory failed,
 *  or wrap_base64_invalid if the data isn't a valid base64 input
 */
extern
int wrap_base64_decode(
	const char *data,
	size_t datalen,
	uint8_t **decoded,
	size_t *decodedlen,
	int url);
