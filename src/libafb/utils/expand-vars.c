/*
 * Copyright (C) 2020-2021 IoT.bzh Company
 * Author: José Bollo <jose.bollo@iot.bzh>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, something express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include "expand-vars.h"

#if !defined(EXPAND_VARS_LIMIT)
#    define  EXPAND_VARS_LIMIT           4096
#endif

extern char **environ;

static const char *getvar(const char *name, size_t len, char ***varsarray)
{
	char **ivar, *var;
	size_t i;

	for (ivar = *varsarray ; ivar ; ivar = *++varsarray) {
		for (var = *ivar ; var ; var = *++ivar) {
			for (i = 0 ; var[i] == name[i] ; i++);
			if (var[i] == '=' && i == len)
				return &var[i + 1];
		}
	}
	return 0;
}

static char *expand(const char *value, char ***varsarray)
{
	char *result, *write, *previous, c;
	const char *begin, *end, *val;
	int drop, again;
	size_t remove, add, i, len;

	write = result = previous = 0;
	begin = value;
	while (begin) {
		drop = again = 0;
		remove = add = 0;
		/* scan/expand the input */
		while ((c = *begin++)) {
			if (c != '$') {
				/* not a variable to expand */
				if (write)
				*write++ = c;
			}
			else {
				/* search name of the variable to expand */
				switch(*begin) {
					case '(': c = ')'; break;
					case '{': c = '}'; break;
					default: c = 0; break;
				}
				if (c) {
					for (end = ++begin ; *end && *end != c ; end++);
					len = end - begin;
					if (*end) {
						end++;
						remove += 3 + len; /* length to remove */
					}
					else {
						remove += 2 + len; /* length to remove */
						drop = 1;
					}
				}
				else {
					for (end = begin ; isalnum(*end) || *end == '_' ; end++);
					len = end - begin;
					remove += 1 + len; /* length to remove */
				}
				/* search the value of the variable in vars and env */
				if (drop) {
					drop = 0;
				}
				else {
					val = getvar(begin, len, varsarray);
					if (val) {
						/* expand value of found variable */
						for(i = 0 ; (c = val[i]) ; i++) {
							if (write) {
								*write++ = c;
								again += c == '$'; /* should iterate again? */
							}
						}
						add += i;
					}
				}
				begin = end;
			}
		}
		/* scan/expand done */
		if (again) {
			/* expansion done but must iterate */
			free(previous);
			begin = value = previous = result;
			*write = 0;
			result = write = 0;
		}
		else if (write) {
			/* expansion done */
			*write = 0;
			begin = 0;
		}
		else if (!remove) {
			/* no expansion to do after first scan */
			begin = 0;
		}
		else {
			/* prepare expansion after scan */
			i = (begin - value) + add - remove;
			if (i >= EXPAND_VARS_LIMIT) {
				/* limit recursivity effect */
				begin = 0;
			}
			else {
				result = write = malloc(i);
				begin = write ? value : 0;
			}
		}
	}
	free(previous);
	return result;
}

char *expand_vars_array(const char *value, int copy, char ***varsarray)
{
	char *expanded = expand(value, varsarray);
	return expanded ?: copy ? strdup(value) : 0;
}

char *expand_vars_only(const char *value, int copy, char **vars)
{
	char **array[] = { vars, 0 };
	return expand_vars_array(value, copy, array);
}

char *expand_vars_env_only(const char *value, int copy)
{
	char **array[] = { environ, 0 };
	return expand_vars_array(value, copy, array);
}

char *expand_vars(const char *value, int copy, char **before, char **after)
{
	char **array[] = { before, environ, after, 0 };
	return expand_vars_array(value, copy, &array[!before]);
}

char *expand_vars_first(const char *value, int copy, char **vars)
{
	char **array[] = { vars, environ, 0 };
	return expand_vars_array(value, copy, &array[!vars]);
}

char *expand_vars_last(const char *value, int copy, char **vars)
{
	char **array[] = { environ, vars, 0 };
	return expand_vars_array(value, copy, array);
}
