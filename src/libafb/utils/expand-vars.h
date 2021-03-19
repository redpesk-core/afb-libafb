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

#pragma once

#include <stddef.h>

/**
 * Definition of the type of callback functions for @see expand_vars_function
 *
 * The function receives a closure and a name represented by a pointer to
 * characters and a length. Based on that the function returns the value
 * associated to the name or NULL.
 *
 * @param closure the closure data
 * @param name    start pointer for the name
 * @param len     the length in chars of the name
 *
 * @return the value for the name or NULL if not found
 */
typedef const char *(*expand_vars_cb)(void *closure, const char *name, size_t len);

/**
 * Search for the variable in an array of variable definitions
 *
 * @param vars null terminated array of variable definitions
 *             of the form "NAME=VALUE..."
 * @param name begin of the name of the searched variable
 * @param len  length of the searched variable
 *
 * @return a pointer to the value or NULL if the variable is not found
 */
extern const char *expand_vars_search(char **vars, const char *name, size_t len);

/**
 * Search for the variable in the environment. Like getenv but the name has
 * not to be null terminated.
 *
 * @param name begin of the name of the searched variable
 * @param len  length of the searched variable
 *
 * @return a pointer to the value or NULL if the variable is not found
 */
extern const char *expand_vars_search_env(const char *name, size_t len);

/**
 * Return the result of expanding variables of 'value'.
 * When 'value' does not contains variables, returns either NULL (when 'copy' == 0)
 * or a copy of value (when 'copy' != 0).
 *
 * The resolution of the variables if done by the function.
 *
 * @param value  the string to expand
 * @param copy   behavior in lack of variable (0 return NULL, not zero return copy)
 * @param function function to resolve the variables
 * @param closure closure of the function
 *
 * @return The result of expanding variables of value or NULL if lake of variables and copy == 0
 */
extern char *expand_vars_function(const char *value, int copy, expand_vars_cb function, void *closure);

/**
 * Return the result of expanding variables of 'value'.
 * When 'value' does not contains variables, returns either NULL (when 'copy' == 0)
 * or a copy of value (when 'copy' != 0).
 *
 * The resolution of the variables if done by searching in the null terminated array
 * of array of null terminated of variable definitions in the form "NAME=VALUE..."
 *
 * @param value  the string to expand
 * @param copy   behavior in lack of variable (0 return NULL, not zero return copy)
 * @param varsarray
 *
 * @return The result of expanding variables of value or NULL if lake of variables and copy == 0
 */
extern char *expand_vars_array(const char *value, int copy, char ***varsarray);

/**
 * Return the result of expanding variables of 'value'.
 * When 'value' does not contains variables, returns either NULL (when 'copy' == 0)
 * or a copy of value (when 'copy' != 0).
 *
 * The resolution of the variables if done by searching in the null terminated array
 * of variable definitions in the form "NAME=VALUE..."
 *
 * @param value  the string to expand
 * @param copy   behavior in lack of variable (0 return NULL, not zero return copy)
 * @param vars   null terminated array of definitions "NAME=VALUE..."
 *
 * @return The result of expanding variables of value or NULL if lake of variables and copy == 0
 */
extern char *expand_vars_only(const char *value, int copy, char **vars);

/**
 * Return the result of expanding variables of 'value'.
 * When 'value' does not contains variables, returns either NULL (when 'copy' == 0)
 * or a copy of value (when 'copy' != 0).
 *
 * The resolution of the variables if done using the environment
 *
 * @param value  the string to expand
 * @param copy   behavior in lack of variable (0 return NULL, not zero return copy)
 *
 * @return The result of expanding variables of value or NULL if lake of variables and copy == 0
 */
extern char *expand_vars_env_only(const char *value, int copy);

/**
 * Return the result of expanding variables of 'value'.
 * When 'value' does not contains variables, returns either NULL (when 'copy' == 0)
 * or a copy of value (when 'copy' != 0).
 *
 * The resolution of the variables if done by searching
 *   1. in the null terminated array 'before'
 *   2. in the environment
 *   3. in the null terminated array 'after'
 *
 * @param value  the string to expand
 * @param copy   behavior in lack of variable (0 return NULL, not zero return copy)
 * @param before null terminated array of definitions "NAME=VALUE..."
 * @param after  null terminated array of definitions "NAME=VALUE..."
 *
 * @return The result of expanding variables of value or NULL if lake of variables and copy == 0
 */
extern char *expand_vars(const char *value, int copy, char **before, char **after);

/**
 * Return the result of expanding variables of 'value'.
 * When 'value' does not contains variables, returns either NULL (when 'copy' == 0)
 * or a copy of value (when 'copy' != 0).
 *
 * The resolution of the variables if done by searching
 *   1. in the null terminated array 'vars'
 *   2. in the environment
 *
 * @param value  the string to expand
 * @param copy   behavior in lack of variable (0 return NULL, not zero return copy)
 * @param vars   null terminated array of definitions "NAME=VALUE..."
 *
 * @return The result of expanding variables of value or NULL if lake of variables and copy == 0
 */
extern char *expand_vars_first(const char *value, int copy, char **vars);

/**
 * Return the result of expanding variables of 'value'.
 * When 'value' does not contains variables, returns either NULL (when 'copy' == 0)
 * or a copy of value (when 'copy' != 0).
 *
 * The resolution of the variables if done by searching
 *   1. in the environment
 *   2. in the null terminated array 'vars''
 *
 * @param value  the string to expand
 * @param copy   behavior in lack of variable (0 return NULL, not zero return copy)
 * @param vars   null terminated array of definitions "NAME=VALUE..."
 *
 * @return The result of expanding variables of value or NULL if lake of variables and copy == 0
 */
extern char *expand_vars_last(const char *value, int copy, char **vars);


