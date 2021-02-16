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
#    define  EXPAND_VARS_LIMIT           16384
#endif
#if !defined(EXPAND_VARS_DEPTH_MAX)
#    define  EXPAND_VARS_DEPTH_MAX       10
#endif

extern char **environ;

/**
 * Internal expansion of the variables of the given value
 * using values returned by function.
 *
 * @param value the value whose variables are to be expanded
 * @param function the name resolution function
 * @param closure the closure to give to the function
 *
 * @return NULL if no variable expansion was performed or if
 * memory allocation failed or if maximum recusion was reached
 */
static char *expand(const char *value, expand_vars_cb function, void *closure)
{
	char *result, *write, *previous, c;
	const char *begin, *end, *val;
	int drop, again, depth;
	size_t remove, add, i, len;

	depth = 0;
	write = result = previous = NULL;
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
					len = (size_t)(end - begin);
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
					len = (size_t)(end - begin);
					remove += 1 + len; /* length to remove */
				}
				/* search the value of the variable in vars and env */
				if (drop) {
					drop = 0;
				}
				else {
					val = function(closure, begin, len);
					if (val != NULL) {
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
			result = write = NULL;
			if (++depth >= EXPAND_VARS_DEPTH_MAX) {
				/* limit recursivity effect */
				begin = NULL;
			}
		}
		else if (write) {
			/* expansion done */
			*write = 0;
			begin = NULL;
		}
		else if (!remove) {
			/* no expansion to do after first scan */
			begin = NULL;
		}
		else {
			/* prepare expansion after scan */
			i = (size_t)(begin - value) + add - remove;
			if (i >= EXPAND_VARS_LIMIT) {
				/* limit expansion size */
				begin = NULL;
			}
			else {
				result = write = malloc(i);
				begin = write ? value : NULL;
			}
		}
	}
	free(previous);
	return result;
}

/**
 * Internal routine used to get variables from arrays
 *
 * @param closure array of array of variable definitions
 * @param name begin of the name of the variable
 * @param len  len of the variable
 *
 * @return a pointer to the value or NULL if the variable is not found
 */
static const char *getvar(void *closure, const char *name, size_t len)
{
	char ***varsarray = closure;
	char **ivar;
	const char *res;

	for (ivar = *varsarray ; ivar ; ivar = *++varsarray) {
		res = expand_vars_search(ivar, name, len);
		if (res)
			return res;
	}
	return NULL;
}

/* search in vars */
const char *expand_vars_search(char **vars, const char *name, size_t len)
{
	char *var;

	for (var = *vars ; var ; var = *++vars) {
		if (!memcmp(var, name, len) && var[len] == '=')
			return &var[len + 1];
	}
	return NULL;
}

/* search in env */
const char *expand_vars_search_env(const char *name, size_t len)
{
	return expand_vars_search(environ, name, len);
}

/* expand using function */
char *expand_vars_function(const char *value, int copy, expand_vars_cb function, void *closure)
{
	char *expanded = expand(value, function, closure);
	return expanded ?: copy ? strdup(value) : 0;
}

/* expand using array of definitions */
char *expand_vars_array(const char *value, int copy, char ***varsarray)
{
	char *expanded = expand(value, getvar, varsarray);
	return expanded ?: copy ? strdup(value) : 0;
}

/* expand using variables definition */
char *expand_vars_only(const char *value, int copy, char **vars)
{
	char **array[] = { vars, 0 };
	return expand_vars_array(value, copy, array);
}

/* expand using environment */
char *expand_vars_env_only(const char *value, int copy)
{
	char **array[] = { environ, 0 };
	return expand_vars_array(value, copy, array);
}

/* expand using variables and environment */
char *expand_vars(const char *value, int copy, char **before, char **after)
{
	char **array[] = { before, environ, after, 0 };
	return expand_vars_array(value, copy, &array[!before]);
}

/* expand using variables and environment */
char *expand_vars_first(const char *value, int copy, char **vars)
{
	char **array[] = { vars, environ, 0 };
	return expand_vars_array(value, copy, &array[!vars]);
}

/* expand using variables and environment */
char *expand_vars_last(const char *value, int copy, char **vars)
{
	char **array[] = { environ, vars, 0 };
	return expand_vars_array(value, copy, array);
}
