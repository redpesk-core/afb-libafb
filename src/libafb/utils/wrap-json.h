/*
 Copyright (C) 2015-2020 IoT.bzh Company

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

#ifdef __cplusplus
    extern "C" {
#endif

#include <stdarg.h>
#include <json-c/json.h>

/**
 * Definition of error codes returned by wrap/unwrap/check functions
 */
enum wrap_json_error_codes {
	wrap_json_error_none,
	wrap_json_error_null_object,
	wrap_json_error_truncated,
	wrap_json_error_internal_error,
	wrap_json_error_out_of_memory,
	wrap_json_error_invalid_character,
	wrap_json_error_too_long,
	wrap_json_error_too_deep,
	wrap_json_error_null_spec,
	wrap_json_error_null_key,
	wrap_json_error_null_string,
	wrap_json_error_out_of_range,
	wrap_json_error_incomplete,
	wrap_json_error_missfit_type,
	wrap_json_error_key_not_found,
	wrap_json_error_bad_base64,
	_wrap_json_error_count_
};

/**
 * Extract the position from the given error 'rc'.
 * The error is one returned by the function wrap_json_vpack,
 * wrap_json_pack, wrap_json_vunpack, wrap_json_unpack, wrap_json_unpack,
 * wrap_json_check, wrap_json_vmatch, wrap_json_match and is the
 * position in the description.
 *
 * @param rc     a returned error
 *
 * @return the position of the error
 */
extern int wrap_json_get_error_position(int rc);

/**
 * Extract the error code from the given error 'rc'.
 *
 * @param rc     a returned error
 *
 * @return the code of the error
 *
 * @see wrap_json_error_codes
 */
extern int wrap_json_get_error_code(int rc);

/**
 * Extract the error string from the given error 'rc'.
 * The sting is the human representation of the error code.
 *
 * @param rc     a returned error
 *
 * @return the string of the error
 */
extern const char *wrap_json_get_error_string(int rc);

/**
 * Creates an object from the given description and the variable parameters.
 *
 * @param result   address where to store the result
 * @param desc     description of the pack to do
 * @param args     the arguments of the description
 *
 * @return 0 in case of success and result is filled or a negative error code
 * and result is set to NULL.
 *
 * @see wrap_json_get_error_position
 * @see wrap_json_get_error_code
 * @see wrap_json_get_error_string
 * @see wrap_json_pack
 */
extern int wrap_json_vpack(struct json_object **result, const char *desc, va_list args);

/**
 * Creates an object from the given description and the variable parameters.
 *
 * @param result   address where to store the result
 * @param desc     description of the pack to do
 * @param ...      the arguments of the description
 *
 * @return 0 in case of success and result is filled or a negative error code
 * and result is set to NULL.
 *
 * @see wrap_json_get_error_position
 * @see wrap_json_get_error_code
 * @see wrap_json_get_error_string
 * @see wrap_json_vpack
 */
extern int wrap_json_pack(struct json_object **result, const char *desc, ...);

/**
 * Scan an object according to its description and unpack the data it
 * contains following the description.
 *
 * @param object   object to scan
 * @param desc     description of the pack to do
 * @param args     the arguments of the description
 *
 * @return 0 in case of success or a negative error code
 *
 * @see wrap_json_get_error_position
 * @see wrap_json_get_error_code
 * @see wrap_json_get_error_string
 * @see wrap_json_unpack
 */
extern int wrap_json_vunpack(struct json_object *object, const char *desc, va_list args);

/**
 * Scan an object according to its description and unpack the data it
 * contains following the description.
 *
 * @param object   object to scan
 * @param desc     description of the pack to do
 * @param ...      the arguments of the description
 *
 * @return 0 in case of success or a negative error code
 *
 * @see wrap_json_get_error_position
 * @see wrap_json_get_error_code
 * @see wrap_json_get_error_string
 * @see wrap_json_vunpack
 */
extern int wrap_json_unpack(struct json_object *object, const char *desc, ...);

/**
 * Scan an object according to its description and return a status code.
 *
 * @param object   object to scan
 * @param desc     description of the pack to do
 * @param args     the arguments of the description
 *
 * @return 0 in case of the object matches the description or a negative error code
 *
 * @see wrap_json_get_error_position
 * @see wrap_json_get_error_code
 * @see wrap_json_get_error_string
 * @see wrap_json_vunpack
 * @see wrap_json_check
 */
extern int wrap_json_vcheck(struct json_object *object, const char *desc, va_list args);

/**
 * Scan an object according to its description and return a status code.
 *
 * @param object   object to scan
 * @param desc     description of the pack to do
 * @param ...      the arguments of the description
 *
 * @return 0 in case of the object matches the description or a negative error code
 *
 * @see wrap_json_get_error_position
 * @see wrap_json_get_error_code
 * @see wrap_json_get_error_string
 * @see wrap_json_vunpack
 * @see wrap_json_vcheck
 */
extern int wrap_json_check(struct json_object *object, const char *desc, ...);

/**
 * Test if an object matches its description.
 *
 * @param object   object to scan
 * @param desc     description of the pack to do
 * @param args     the arguments of the description
 *
 * @return 1 if it matches or 0 if not
 *
 * @see wrap_json_get_error_position
 * @see wrap_json_get_error_code
 * @see wrap_json_get_error_string
 * @see wrap_json_vunpack
 * @see wrap_json_match
 */
extern int wrap_json_vmatch(struct json_object *object, const char *desc, va_list args);

/**
 * Test if an object matches its description.
 *
 * @param object   object to scan
 * @param desc     description of the pack to do
 * @param ...      the arguments of the description
 *
 * @return 1 if it matches or 0 if not
 *
 * @see wrap_json_get_error_position
 * @see wrap_json_get_error_code
 * @see wrap_json_get_error_string
 * @see wrap_json_unpack
 * @see wrap_json_match
 */
extern int wrap_json_match(struct json_object *object, const char *desc, ...);

/**
 * Calls the callback for each item of an array, in order. If the object is not
 * an array, the callback is called for the object itself.
 *
 * The given callback receives 2 arguments:
 *  1. the closure
 *  2. the item
 *
 * @param object   the item to iterate
 * @param callback the callback to call
 * @param closure  the closure for the callback
 */
extern void wrap_json_optarray_for_all(struct json_object *object, void (*callback)(void*,struct json_object*), void *closure);

/**
 * Calls the callback for each item of an array, in order. If the object is not
 * an array, nothing is done, the callback is not called.
 *
 * The given callback receives 2 arguments:
 *  1. the closure
 *  2. the item
 *
 * @param object   the item to iterate
 * @param callback the callback to call
 * @param closure  the closure for the callback
 */
extern void wrap_json_array_for_all(struct json_object *object, void (*callback)(void*,struct json_object*), void *closure);

/**
 * Calls the callback for each item of an object. If the object is not
 * an object (a dictionnary), nothing is done, the callback is not called.
 *
 * The given callback receives 3 arguments:
 *  1. the closure
 *  2. the item
 *  3. the name of the item
 *
 * @param object   the item to iterate
 * @param callback the callback to call
 * @param closure  the closure for the callback
 */
extern void wrap_json_object_for_all(struct json_object *object, void (*callback)(void*,struct json_object*,const char*), void *closure);

/**
 * Calls the callback for each item of an object. If the object is not
 * an object (a dictionnary), the call back is called without name
 * for the object itself.
 *
 * The given callback receives 3 arguments:
 *  1. the closure
 *  2. the item
 *  3. the name of the item it its parent object or NULL
 *
 * @param object   the item to iterate
 * @param callback the callback to call
 * @param closure  the closure for the callback
 */
extern void wrap_json_optobject_for_all(struct json_object *object, void (*callback)(void*,struct json_object*,const char*), void *closure);

/**
 * Calls the callback for each item of object. Each item depends on
 * the nature of the object:
 *  - object: each element of the object with its name
 *  - array: each item of the array, in order, without name
 *  - other: the item without name
 *
 * The given callback receives 3 arguments:
 *  1. the closure
 *  2. the item
 *  3. the name of the item it its parent object or NULL
 *
 * @param object   the item to iterate
 * @param callback the callback to call
 * @param closure  the closure for the callback
 */
extern void wrap_json_for_all(struct json_object *object, void (*callback)(void*,struct json_object*,const char*), void *closure);

/**
 * Clones the 'object': returns a copy of it. But doesn't clones
 * the content. Synonym of wrap_json_clone_depth(object, 1).
 *
 * Be aware that this implementation doesn't clones content that is deeper
 * than 1 but it does link these contents to the original object and
 * increments their use count. So, everything deeper that 1 is still available.
 *
 * @param object the object to clone
 *
 * @return a copy of the object.
 *
 * @see wrap_json_clone_depth
 * @see wrap_json_clone_deep
 */
extern struct json_object *wrap_json_clone(struct json_object *object);

/**
 * Clones the 'object': returns a copy of it. Also clones all
 * the content recursively. Synonym of wrap_json_clone_depth(object, INT_MAX).
 *
 * @param object the object to clone
 *
 * @return a copy of the object.
 *
 * @see wrap_json_clone_depth
 * @see wrap_json_clone
 */
extern struct json_object *wrap_json_clone_deep(struct json_object *object);

/**
 * Clones any json 'item' for the depth 'depth'. The item is duplicated
 * and if 'depth' is not zero, its contents is recursively cloned with
 * the depth 'depth' - 1.
 *
 * Be aware that this implementation doesn't copies the primitive json
 * items (numbers, nulls, booleans, strings) but instead increments their
 * use count. This can cause issues with newer versions of libjson-c that
 * now unfortunately allows to change their values.
 *
 * @param item the item to clone. Can be of any kind.
 * @param depth the depth to use when cloning composites: object or arrays.
 *
 * @return the cloned array.
 *
 * @see wrap_json_clone
 * @see wrap_json_clone_deep
 */
extern struct json_object *wrap_json_clone_depth(struct json_object *object, int depth);

/**
 * Adds the items of the object 'added' to the object 'dest'.
 *
 * @param dest the object to complete this object is modified
 * @param added the object containing fields to add
 *
 * @return the destination object 'dest'
 *
 * @example wrap_json_object_add({"a":"a"},{"X":"X"}) -> {"a":"a","X":"X"}
 */
extern struct json_object *wrap_json_object_add(struct json_object *dest, struct json_object *added);

/**
 * Insert content of the array 'added' at index 'idx' of 'dest' array.
 *
 * @param dest the array to complete, this array is modified
 * @param added the array containing content to add
 * @param idx the index where the 'added' array content will be inserted into
 * 'dest' array. To insert 'added' array at the end of 'dest' array,
 * you can set 'idx' to:
 * - a negative value.
 * - a valued bigger than 'dest' array length.
 *
 * @return the destination array 'dest'
 *
 * @example wrap_json_array_insert(["a","b",5],["X","Y",0.92], 1) -> ["a","X","Y",0.92,"b",5]
 */
extern struct json_object *wrap_json_array_insert_array(struct json_object *dest, struct json_object *added, int idx);

/**
 * Sort the 'array' and returns it. Sorting is done accordingly to the
 * order given by the function 'wrap_json_cmp'. If the paramater isn't
 * an array, nothing is done and the parameter is returned unchanged.
 *
 * @param array the array to sort
 *
 * @returns the array sorted
 */
extern struct json_object *wrap_json_sort(struct json_object *array);

/**
 * Returns a json array of the sorted keys of 'object' or null if 'object' has no keys.
 *
 * @param object the object whose keys are to be returned
 *
 * @return either NULL is 'object' isn't an object or a sorted array of the key's strings.
 */
extern struct json_object *wrap_json_keys(struct json_object *object);

/**
 * Compares 'x' with 'y'
 *
 * @param x first object to compare
 * @param y second object to compare
 *
 * @return an integer less than, equal to, or greater than zero
 * if 'x' is found, respectively, to be less than, to match,
 * or be greater than 'y'.
 */
extern int wrap_json_cmp(struct json_object *x, struct json_object *y);

/**
 * Searchs whether 'x' equals 'y'
 *
 * @param x first object to compare
 * @param y second object to compare
 *
 * @return an integer equal to zero when 'x' != 'y' or 1 when 'x' == 'y'.
 */
extern int wrap_json_equal(struct json_object *x, struct json_object *y);

/**
 * Searchs whether 'x' contains 'y'
 *
 * @param x first object to compare
 * @param y second object to compare
 *
 * @return an integer equal to 1 when 'y' is a subset of 'x' or zero otherwise
 */
extern int wrap_json_contains(struct json_object *x, struct json_object *y);

#ifdef __cplusplus
    }
#endif
