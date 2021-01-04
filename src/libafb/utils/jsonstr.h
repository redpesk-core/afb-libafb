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

#include <stddef.h>

/**
 * Compute the length of string as escaped for JSON
 * without the enclosing double quotes.
 *
 * @param string the string to escape
 * @param maxlen the maximum length of the string
 *
 * @return the length of the escaped JSON string (without the enclosing double quotes)
 *
 * @example
 *   jsonstr_string_escape_length("hello", 10000) = 5
 *   jsonstr_string_escape_length("hello", 3) = 3
 *   jsonstr_string_escape_length("hello\n", 10000) = 13
 */
extern size_t jsonstr_string_escape_length(const char *string, size_t maxlen);

/**
 * Compute the possibly escaped JSON string value of
 * the given string. Doesn't put the enclosing double quotes.
 *
 * @param dest   buffer where to store the escaped JSON string
 * @param destmaxlen the maximum length of the dest
 * @param string the string to escape
 * @param stringmaxlen the maximum length of the string
 *
 * @return the length of the escaped JSON string without the enclosing double quotes
 * and without the added terminating zero. The returned length can be greater than
 * given destmaxlen but overflowing characters are not written to dest in that case.
 * If the returned length is greater or equal to the given length then the escaped
 * string is not terminated by a zero.
 */
extern size_t jsonstr_string_escape(char *dest, size_t destlenmax, const char *string, size_t stringlenmax);

/**
 * Compute the possibly escaped JSON string value of
 * the given string. Doesn't put the enclosing double quotes.
 * This routine is unsafe because it does not check the overflow
 * of the destination. However the check can be done before by using
 * the function @see jsonstr_string_escape_length.
 *
 * @param dest   buffer where to store the escaped JSON string
 * @param string the string to escape
 * @param stringmaxlen the maximum length of the string
 *
 * @return the length of the escaped JSON string without the enclosing double quotes
 * and without the added terminating zero.
 *
 * @see jsonstr_string_escape_length
 * @see jsonstr_string_escape
 */
extern size_t jsonstr_string_escape_unsafe(char *dest, const char *string, size_t stringlenmax);

/**
 * test if a string is a valid json utf8 stream.
 *
 * @param string        the string to be tested
 * @param stringlenmax  maximum length of the string to test
 * @param size          if not NULL, the consumed size until status
 *
 * @return 0 if the string is not a valid JSON or otherwise 1
 */
extern int jsonstr_test(const char *string, size_t stringlenmax, size_t *size);

