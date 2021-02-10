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


#include <string.h>

#include "jsonstr.h"

/**********************************************************************/

/* returns the character for the hexadecimal digit */
static inline char hex(int digit)
{
	return (char)(digit + (digit > 9 ? 'a' - 10 : '0'));
}

size_t jsonstr_string_escape_length(const char *string, size_t maxlen)
{
	size_t i, r;
	char c;

	for(i = r = 0 ; i < maxlen  && (c = string[i]); i++) {
		if (32 > (unsigned char)c) {
			/* escaping control character */
			r += 6;
		}
		else if (c == '"' || c == '\\') {
			/* simple character escaping */
			r += 2;
		}
		else {
			r += 1;
		}
	}
	/* end */
	return r;
}

/*
 * escape the string for JSON in destination
 * returns length of the final string as if enougth room existed
 */
size_t jsonstr_string_escape(char *dest, size_t destlenmax, const char *string, size_t stringlenmax)
{
	size_t i, r;
	char c;

	/* copy until end */
	for(i = r = 0 ; i < stringlenmax  && r < destlenmax && (c = string[i]); i++, r++) {
		if (32 > (unsigned char)c) {
			/* escaping control character */
			dest[r] = '\\';
			if (++r < destlenmax)
				dest[r] = 'u';
			if (++r < destlenmax)
				dest[r] = '0';
			if (++r < destlenmax)
				dest[r] = '0';
			if (++r < destlenmax)
				dest[r] = hex((c >> 4) & 15);
			if (++r < destlenmax)
				dest[r] = hex(c & 15);
		}
		else if (c == '"' || c == '\\') {
			/* simple character escaping */
			dest[r] = '\\';
			if (++r < destlenmax)
				dest[r] = c;
		}
		else {
			dest[r] = c;
		}
	}
	/* fullfil return length */
	if (i < stringlenmax && c)
		r += jsonstr_string_escape_length(&string[i], stringlenmax - i);

	/* end */
	if (r < destlenmax)
		dest[r] = 0;
	return r;
}

/*
 * escape the string for JSON in destination
 * returns offset of the terminating zero
 */
size_t jsonstr_string_escape_unsafe(char *dest, const char *string, size_t stringlenmax)
{
	size_t i, r;
	char c;

	/* copy until end */
	for(i = r = 0 ; i < stringlenmax  && (c = string[i]); i++, r++) {
		if (32 > (unsigned char)c) {
			/* escaping control character */
			dest[r] = '\\';
			dest[++r] = 'u';
			dest[++r] = '0';
			dest[++r] = '0';
			dest[++r] = hex((c >> 4) & 15);
			dest[++r] = hex(c & 15);
		}
		else if (c == '"' || c == '\\') {
			/* simple character escaping */
			dest[r] = '\\';
			dest[++r] = c;
		}
		else {
			dest[r] = c;
		}
	}
	dest[r] = 0;
	return r;
}

/****************************************************************************/
/*****  part for testing validity of json string                        *****/
/****************************************************************************/

/** structure for reading data */
struct readtxt {
	char c;             /**< current character */
	const char *pos;    /**< read position of the next character */
	const char *end;    /**< end of the buffer to read */
};

/** init the read data */
static void init_readtxt(const char *string, size_t lenmax, struct readtxt *rt)
{
	rt->pos = string;
	rt->end = &string[lenmax];
	rt->c = lenmax ? *rt->pos++ : 0;
}

/** set the character to the next one if any */
static void read_readtxt(struct readtxt *rt)
{
	if (rt->pos == rt->end)
		rt->c = 0;
	else {
		rt->c = *rt->pos;
		if (rt->c == 0)
			rt->end = rt->pos;
		else
			rt->pos++;
	}
}

/** skip any white space */
static void skip_whitespaces(struct readtxt *rt)
{
	switch(rt->c) {
	case ' ': case '\n': case '\r': case '\t':
		read_readtxt(rt);
		skip_whitespaces(rt);
	}
}

/** test if next is an hexadecimal digit */
static int read_next_hex_readtxt(struct readtxt *rt)
{
	read_readtxt(rt);
	return (rt->c >= '0' && rt->c <= '9')
		|| (rt->c >= 'a' && rt->c <= 'f')
		|| (rt->c >= 'A' && rt->c <= 'F');
}

/** test a string */
static int test_string(struct readtxt *rt)
{
	if (rt->c != '"')
		return 0;
	for(;;) {
		read_readtxt(rt);
		switch (rt->c) {
		case 0:
			return 0;
		case '"':
			read_readtxt(rt);
			return 1;
		case '\\':
			read_readtxt(rt);
			switch (rt->c) {
			case '"': case '\\': case '/':
			case 'b': case 'n': case 'f':
			case 'r': case 't':
				break;
			case 'u':
				if (read_next_hex_readtxt(rt)
				 && read_next_hex_readtxt(rt)
				 && read_next_hex_readtxt(rt)
				 && read_next_hex_readtxt(rt))
					break;
				/*@fallthrough@*/
			default:
				return 0;
			}
		default:
			break;
		}
	}
}

/** test a number */
static int test_number(struct readtxt *rt)
{
	if (rt->c < '0' || rt->c > '9')
		return 0;
	while (rt->c >= '0' && rt->c <= '9')
		read_readtxt(rt);
	if (rt->c == '.') {
		read_readtxt(rt);
		while(rt->c >= '0' && rt->c <= '9')
			read_readtxt(rt);
	}
	if (rt->c == 'e' || rt->c == 'E') {
		read_readtxt(rt);
		if (rt->c == '-' || rt->c == '+')
			read_readtxt(rt);
		if (rt->c < '0' || rt->c > '9')
			return 0;
		while (rt->c >= '0' && rt->c <= '9')
			read_readtxt(rt);
	}
	return 1;
}

/** test a literal value (null/false/true) */
static int test_literal(struct readtxt *rt, const char *literal)
{
	while(*literal) {
		if (rt->c != *literal)
			return 0;
		literal++;
		read_readtxt(rt);
	}
	return 1;
}

static int test_value(struct readtxt *rt);

/** test if a value is an object */
static int test_object(struct readtxt *rt)
{
	/* start with a '[' */
	if (rt->c != '{')
		return 0;
	read_readtxt(rt);
	skip_whitespaces(rt);

	/* until it is ended */
	while(rt->c != '}') {

		/* read the key */
		if (!test_string(rt))
			return 0;
		skip_whitespaces(rt);

		/* key/value colon separator */
		if (rt->c != ':')
			return 0;
		read_readtxt(rt);

		/* read the value */
		if (!test_value(rt))
			return 0;
		switch (rt->c) {
		case ',':
			read_readtxt(rt);
			skip_whitespaces(rt);
			/*@fallthrough@*/
		case '}':
			break;
		default:
			return 0;
		}
	}
	read_readtxt(rt);
	return 1;
}

/** test if a value is an array */
static int test_array(struct readtxt *rt)
{
	/* start with a '[' */
	if (rt->c != '[')
		return 0;
	read_readtxt(rt);
	skip_whitespaces(rt);

	/* until it is ended */
	while(rt->c != ']') {
		/* read an array value */
		if (!test_value(rt))
			return 0;
		switch (rt->c) {
		case ',':
			read_readtxt(rt);
			/*@fallthrough@*/
		case ']':
			break;
		default:
			return 0;
		}
	}
	read_readtxt(rt);
	return 1;
}

/** test if read text has any json value */
static int test_value(struct readtxt *rt)
{
	int r = 0;

	/* ignore whitespaces efore the value */
	skip_whitespaces(rt);

	/* test the value */
	switch(rt->c) {
	case 'n':
		r = test_literal(rt, "null");
		break;
	case 't':
		r = test_literal(rt, "true");
		break;
	case 'f':
		r = test_literal(rt, "false");
		break;
	case '-':
	case '+':
		read_readtxt(rt);
		/*@fallthrough@*/
	case '0': case '1': case '2':
	case '3': case '4': case '5':
	case '6': case '7': case '8':
	case '9':
		r = test_number(rt);
		break;
	case '"':
		r = test_string(rt);
		break;
	case '[':
		r = test_array(rt);
		break;
	case '{':
		r = test_object(rt);
		break;
	}
	/* return if error found */
	if (r == 0)
		return 0;

	/* ignore whitespaces after the value */
	skip_whitespaces(rt);
	return 1;
}

/*
 * test if a string is a valid json utf8 stream
 */
int jsonstr_test(const char *string, size_t stringlenmax, size_t *size)
{
	int r;
	struct readtxt rt;

	init_readtxt(string, stringlenmax, &rt);
	r = test_value(&rt) && rt.c == 0;
	if (size)
		*size = rt.pos - string - !r;
	return r;
}

