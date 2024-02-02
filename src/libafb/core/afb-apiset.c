/*
 * Copyright (C) 2015-2024 IoT.bzh Company
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

#include "../libafb-config.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <rp-utils/rp-verbose.h>

#include "core/afb-session.h"
#include "core/afb-apiname.h"
#include "core/afb-apiset.h"
#include "core/afb-sched.h"

#include "sys/x-errno.h"
#include "utils/namecmp.h"

#define INCR		8	/* CAUTION: must be a power of 2 */
#define NOT_STARTED 	1

struct afb_apiset;
struct api_desc;
struct api_class;
struct api_alias;
struct api_depend;

/**
 * array of items
 */
struct api_array {
	int count;			/* count of items */
	union {
		void **anys;
		struct api_desc **apis;
		struct api_class **classes;
		struct api_alias **aliases;
		struct api_depend **depends;
	};
};

/**
 * Internal description of an api
 */
struct api_desc
{
	struct api_desc *next;
	const char *name;		/**< name of the api */
	int status;			/**< initialisation status:
						- NOT_STARTED not started,
						- 0 started without error,
						- minus than 0, error number of start */
	struct afb_api_item api;	/**< handler of the api */
	struct {
		struct api_array classes;
		struct api_array apis;
	} require;
};

/**
 * internal description of aliases
 */
struct api_alias
{
	struct api_alias *next;
	struct api_desc *api;
	char name[];
};

/**
 *
 */
struct api_class
{
	struct api_class *next;
	struct api_array providers;
	char name[];
};

/**
 *
 */
struct api_depend
{
	struct afb_apiset *callset;
	char name[];
};

/**
 * Data structure for apiset
 */
struct afb_apiset
{
	struct api_array apis;		/**< the apis */
	struct api_alias *aliases;	/**< the aliases */
	struct afb_apiset *subset;	/**< subset if any */
	struct {
		int (*callback)(void*, struct afb_apiset*, const char*); /* not found handler */
		void *closure;
		void (*cleanup)(void*);
	} onlack;			/** not found handler */
	int timeout;			/**< the timeout in second for the apiset */
	int refcount;			/**< reference count for freeing resources */
	char name[];			/**< name of the apiset */
};

/**
 * global apis
 */
static struct api_desc *all_apis;

/**
 * global classes
 */
static struct api_class *all_classes;

/**
 * Ensure enough room in 'array' for 'count' items
 */
static int api_array_ensure_count(struct api_array *array, int count)
{
	int c;
	void **anys;

	c = (count + INCR - 1) & ~(INCR - 1);
	anys = realloc(array->anys, (size_t)c * sizeof *anys);
	if (!anys)
		return X_ENOMEM;

	array->count = count;
	array->anys = anys;
	return 0;
}

/**
 * Insert in 'array' the item 'any' at the 'index'
 */
static int api_array_insert(struct api_array *array, void *any, int index)
{
	int rc;
	int n = array->count;

	rc = api_array_ensure_count(array, n + 1);
	if (rc < 0)
		return rc;

	while (index < n) {
		array->anys[n] = array->anys[n - 1];
		n--;
	}

	array->anys[index] = any;
	return 0;
}

/**
 * Add the item 'any' to the 'array'
 */
static int api_array_add(struct api_array *array, void *any)
{
	int rc;
	int i, n = array->count;

	for (i = 0 ; i < n ; i++) {
		if (array->anys[i] == any)
			return 0;
	}

	rc = api_array_ensure_count(array, n + 1);
	if (rc < 0)
		return rc;

	array->anys[n] = any;
	return 0;
}

/**
 * Delete the 'api' from the 'array'
 * Returns 1 if delete or 0 if not found
 */
static int api_array_del(struct api_array *array, void *any)
{
	int i = array->count;
	while (i) {
		if (array->anys[--i] == any) {
			array->anys[i] = array->anys[--array->count];
			return 1;
		}
	}
	return 0;
}

/**
 * Search the class of 'name' and return it.
 * In case where the class of 'namle' isn't found, it returns
 * NULL when 'create' is null or a fresh created instance if 'create' isn't
 * zero (but NULL on allocation failure).
 */
static struct api_class *class_search(const char *name, int create)
{
	struct api_class *c;

	for (c= all_classes ; c ; c = c->next) {
		if (!namecmp(name, c->name))
			return c;
	}

	if (!create)
		return NULL;

	c = calloc(1, strlen(name) + 1 + sizeof *c);
	if (c) {
		strcpy(c->name, name);
		c->next = all_classes;
		all_classes = c;
	}
	return c;
}

/**
 * Search the api of 'name'.
 * @param set the api set
 * @param name the api name to search
 * @return the descriptor if found or NULL otherwise
 */
static struct api_desc *search(struct afb_apiset *set, const char *name)
{
	int i, c, up, lo;
	struct api_desc *a;
	struct api_alias *aliases;

	/* dichotomic search of the api */
	/* initial slice */
	lo = 0;
	up = set->apis.count;
	while (lo < up) {
		/* check the mid of the slice */
		i = (lo + up) >> 1;
		a = set->apis.apis[i];
		c = namecmp(a->name, name);
		if (c == 0) {
			/* found */
			return a;
		}
		/* update the slice */
		if (c < 0)
			lo = i + 1;
		else
			up = i;
	}

	/* linear search of aliases */
	aliases = set->aliases;
	for(;;) {
		if (!aliases)
			break;
		c = namecmp(aliases->name, name);
		if (!c)
			return aliases->api;
		if (c > 0)
			break;
		aliases = aliases->next;
	}
	return NULL;
}

/**
 * Search the api of 'name' in the apiset and in its subsets.
 * @param set the api set
 * @param name the api name to search
 * @return the descriptor if found or NULL otherwise
 */
static struct api_desc *searchrec(struct afb_apiset *set, const char *name)
{
	struct api_desc *result;

	do {
		result = search(set, name);
	} while (result == NULL && (set = set->subset) != NULL);

	return result;
}

/**
 * Increases the count of references to the apiset and return its address
 * @param set the set whose reference count is to be increased
 * @return the given apiset
 */
struct afb_apiset *afb_apiset_addref(struct afb_apiset *set)
{
	if (set)
		__atomic_add_fetch(&set->refcount, 1, __ATOMIC_RELAXED);
	return set;
}

/**
 * Decreases the count of references to the apiset and frees its
 * resources when no more references exists.
 * @param set the set to unrefrence
 */
void afb_apiset_unref(struct afb_apiset *set)
{
	struct api_alias *a;
	struct api_desc *d;

	if (set && !__atomic_sub_fetch(&set->refcount, 1, __ATOMIC_RELAXED)) {
		afb_apiset_unref(set->subset);
		if (set->onlack.cleanup)
			set->onlack.cleanup(set->onlack.closure);
		while((a = set->aliases)) {
			set->aliases = a->next;
			free(a);
		}
		while (set->apis.count) {
			d = set->apis.apis[--set->apis.count];
			if (d->api.itf->unref)
				d->api.itf->unref(d->api.closure);
			free(d);
		}
		free(set->apis.apis);
		free(set);
	}
}

/**
 * Create an apiset
 * @param name the name of the apiset
 * @param timeout the default timeout in seconds for the apiset
 * @return the created apiset or NULL in case of error
 */
struct afb_apiset *afb_apiset_create(const char *name, int timeout)
{
	struct afb_apiset *set;

	set = calloc(1, (name ? strlen(name) : 0) + 1 + sizeof *set);
	if (set) {
		set->timeout = timeout;
		set->refcount = 1;
		if (name)
			strcpy(set->name, name);
	}
	return set;
}

/**
 * Create an apiset being the last subset of 'set'
 * @param set     the set to extend with the created subset (can be NULL)
 * @param name    the name of the created apiset (can be NULL)
 * @param timeout the default timeout in seconds for the created apiset
 * @return the created apiset or NULL in case of error
 */
struct afb_apiset *afb_apiset_create_subset_last(struct afb_apiset *set, const char *name, int timeout)
{
	if (set)
		while (set->subset)
			set = set->subset;
	return afb_apiset_create_subset_first(set, name, timeout);
}

/**
 * Create an apiset being the first subset of 'set'
 * @param set     the set to extend with the created subset (can be NULL)
 * @param name    the name of the created apiset (can be NULL)
 * @param timeout the default timeout in seconds for the created apiset
 * @return the created apiset or NULL in case of error
 */
struct afb_apiset *afb_apiset_create_subset_first(struct afb_apiset *set, const char *name, int timeout)
{
	struct afb_apiset *result = afb_apiset_create(name, timeout);
	if (result && set) {
		result->subset = set->subset;
		set->subset = result;
	}
	return result;
}

/**
 * the name of the apiset
 * @param set the api set
 * @return the name of the set
 */
const char *afb_apiset_name(struct afb_apiset *set)
{
	return set->name;
}

/**
 * Get the API timeout of the set
 * @param set the api set
 * @return the timeout in seconds
 */
int afb_apiset_timeout_get(struct afb_apiset *set)
{
	return set->timeout;
}

/**
 * Set the API timeout of the set
 * @param set the api set
 * @param to the timeout in seconds
 */
void afb_apiset_timeout_set(struct afb_apiset *set, int to)
{
	set->timeout = to;
}

/**
 * Get the subset of the set
 * @param set the api set
 * @return the subset of set
 */
struct afb_apiset *afb_apiset_subset_get(struct afb_apiset *set)
{
	return set->subset;
}

/**
 * Returns the 'set' or one of its subset matching 'name'
 * @param set the head apiset
 * @param name the name to find
 * @return the found apiset or NULL
 */
struct afb_apiset *afb_apiset_subset_find(struct afb_apiset *set, const char *name)
{
	while(set && strcmp(name, set->name))
		set = set->subset;
	return set;
}

/**
 * Set the subset of the set
 * @param set the api set
 * @param subset the subset to set
 *
 * @return 0 in case of success or -1 if it had created a loop
 */
int afb_apiset_subset_set(struct afb_apiset *set, struct afb_apiset *subset)
{
	struct afb_apiset *tmp;

	/* avoid infinite loop */
	for (tmp = subset ; tmp ; tmp = tmp->subset)
		if (tmp == set)
			return X_ENOENT;

	tmp = set->subset;
	set->subset = afb_apiset_addref(subset);
	afb_apiset_unref(tmp);

	return 0;
}

void afb_apiset_onlack_set(struct afb_apiset *set, int (*callback)(void*, struct afb_apiset*, const char*), void *closure, void (*cleanup)(void*))
{
	if (set->onlack.cleanup)
		set->onlack.cleanup(set->onlack.closure);
	set->onlack.callback = callback;
	set->onlack.closure = closure;
	set->onlack.cleanup = cleanup;
}

/**
 * Adds the api of 'name' described by 'api'.
 * @param set the api set
 * @param name the name of the api to add (have to survive, not copied!)
 * @param api the api
 * @returns 0 in case of success or a negative number in case of error:
 *   - X_EEXIST if name already registered
 *   - X_ENOMEM when out of memory
 */
int afb_apiset_add(struct afb_apiset *set, const char *name, struct afb_api_item api)
{
	struct api_desc *desc;
	int i, c;

	/* check whether it exists already */
	if (search(set, name)) {
		RP_ERROR("api of name %s already exists", name);
		return X_EEXIST;
	}

	/* search insertion place */
	for (i = 0 ; i < set->apis.count ; i++) {
		c = namecmp(set->apis.apis[i]->name, name);
		if (c > 0)
			break;
	}

	/* allocates memory */
	desc = calloc(1, sizeof *desc);
	if (!desc)
		goto oom;

	desc->status = NOT_STARTED;
	desc->api = api;
	desc->name = name;

	if (api_array_insert(&set->apis, desc, i) < 0) {
		free(desc);
		goto oom;
	}

	desc->next = all_apis;
	all_apis = desc;

	if (afb_apiname_is_public(name))
		RP_INFO("API %s added", name);

	return 0;

oom:
	RP_ERROR("out of memory");
	return X_ENOMEM;
}

/**
 * Adds a the 'alias' name to the api of 'name'.
 * @params set the api set
 * @param name the name of the api to alias
 * @param alias the aliased name to add to the api of name
 * @returns 0 in case of success or a negative number in case of error:
 *   - X_ENOENT if the api doesn't exist
 *   - X_EEXIST if name already registered
 *   - X_ENOMEM when out of memory
 */
int afb_apiset_add_alias(struct afb_apiset *set, const char *name, const char *alias)
{
	struct api_desc *api;
	struct api_alias *ali, **pali;

	/* check alias doesn't already exist */
	if (search(set, alias)) {
		RP_ERROR("api of name %s already exists", alias);
		return X_EEXIST;
	}

	/* check aliased api exists */
	api = search(set, name);
	if (api == NULL) {
		RP_ERROR("api of name %s doesn't exists", name);
		return X_ENOENT;
	}

	/* allocates and init the struct */
	ali = malloc(sizeof *ali + strlen(alias) + 1);
	if (ali == NULL) {
		RP_ERROR("out of memory");
		return X_ENOMEM;
	}
	ali->api = api;
	strcpy(ali->name, alias);

	/* insert the alias in the sorted order */
	pali = &set->aliases;
	while(*pali && strcmp((*pali)->name, alias) < 0)
		pali = &(*pali)->next;
	ali->next = *pali;
	*pali = ali;
	return 0;
}

int afb_apiset_is_alias(struct afb_apiset *set, const char *name)
{
	struct api_desc *api = searchrec(set, name);
	return api && namecmp(api->name, name);
}

const char *afb_apiset_unalias(struct afb_apiset *set, const char *name)
{
	struct api_desc *api = searchrec(set, name);
	return api ? api->name : NULL;
}

/**
 * Delete from the 'set' the api of 'name'.
 * @param set the set to be changed
 * @param name the name of the API to remove
 * @return 0 in case of success or -1 in case where the API doesn't exist.
 */
int afb_apiset_del(struct afb_apiset *set, const char *name)
{
	struct api_class *cla;
	struct api_alias *ali, **pali;
	struct api_desc *desc, **pdesc, *odesc;
	int i, c;

	/* search the alias */
	pali = &set->aliases;
	while ((ali = *pali)) {
		c = namecmp(ali->name, name);
		if (!c) {
			*pali = ali->next;
			free(ali);
			return 0;
		}
		if (c > 0)
			break;
		pali = &ali->next;
	}

	/* search the api */
	for (i = 0 ; i < set->apis.count ; i++) {
		desc = set->apis.apis[i];
		c = namecmp(desc->name, name);
		if (c == 0) {
			/* remove from classes */
			for (cla = all_classes ; cla ; cla = cla->next)
				api_array_del(&cla->providers, desc);

			/* unlink from the whole set and their requires */
			pdesc = &all_apis;
			while ((odesc = *pdesc) != desc) {
				pdesc = &odesc->next;
			}
			*pdesc = odesc = desc->next;
			while (odesc) {
				odesc = odesc->next;
			}

			/* remove references from classes */
			free(desc->require.classes.classes);

			/* drop the aliases */
			pali = &set->aliases;
			while ((ali = *pali)) {
				if (ali->api != desc)
					pali = &ali->next;
				else {
					*pali = ali->next;
					free(ali);
				}
			}

			/* unref the api */
			if (desc->api.itf->unref)
				desc->api.itf->unref(desc->api.closure);

			set->apis.count--;
			while(i < set->apis.count) {
				set->apis.apis[i] = set->apis.apis[i + 1];
				i++;
			}
			free(desc);
			return 0;
		}
		if (c > 0)
			break;
	}
	return X_ENOENT;
}

/**
 * Get from the 'set' the API of 'name' in 'api' with fallback to subset or default api
 * @param set the set of API
 * @param name the name of the API to get
 * @param rec if not zero look also recursively in subsets
 * @return the api pointer in case of success or NULL in case of error
 */
static struct api_desc *lookup(struct afb_apiset *set, const char *name, int rec)
{
	struct api_desc *result;

	result = search(set, name);
	while (!result) {
		/* lacking the api, try onlack behaviour */
		if (set->onlack.callback && set->onlack.callback(set->onlack.closure, set, name) > 0) {
			result = search(set, name);
			if (result)
				break;
		}
		if (!rec || !(set = set->subset))
			break;
		result = search(set, name);
	}
	return result;
}

static int start_api(struct api_desc *api);

/**
 * Get from the 'set' the API of 'name' in 'api'
 * @param set the set of API
 * @param name the name of the API to get
 * @param rec if not zero look also recursively in subsets
 * @param started if not zero ensure the api is sarted
 * @param api where to store the retrived api
 * @return 0 in case of success or a negative error code
 */
int afb_apiset_get_api(struct afb_apiset *set, const char *name, int rec, int started, const struct afb_api_item **api)
{
	int result;
	struct api_desc *i;

	i = lookup(set, name, rec);
	if (!i)
		result = X_ENOENT;
	else if (started)
		result = i->status == NOT_STARTED ? start_api(i) : i->status;
	else
		result = 0;
	if (api)
		*api = result ? NULL : &i->api;
	return result;
}

/**
 * Start the apis of the 'array'
 */
static int start_array_apis(struct api_array *array)
{
	int i, rc = 0, rc2;

	i = 0;
	while (i < array->count) {
		rc2 = array->apis[i]->status;
		if (rc2 == NOT_STARTED) {
			rc2 = start_api(array->apis[i]);
			if (rc2 < 0)
				rc = rc2;
			i = 0;
		} else {
			if (rc2)
				rc = rc2;
			i++;
		}
	}
	return rc;
}

/**
 * Start the class 'cla' (start the apis that provide it).
 */
static int start_class(struct api_class *cla)
{
	return start_array_apis(&cla->providers);
}

/**
 * Start the classes of the 'array'
 */
static int start_array_classes(struct api_array *array)
{
	int i, rc = 0, rc2;

	i = array->count;
	while (i) {
		rc2 = start_class(array->classes[--i]);
		if (rc2 < 0) {
			rc = rc2;
		}
	}
	return rc;
}

/**
 * Start the depends of the 'array'
 */
static int start_array_depends(struct api_array *array)
{
	struct api_desc *api;
	int i, rc = 0, rc2;

	i = 0;
	while (i < array->count) {
		api = lookup(array->depends[i]->callset, array->depends[i]->name, 1);
		if (!api) {
			RP_ERROR("required api %s not found", array->depends[i]->name);
			if (rc == 0)
				rc = X_ENOENT;
			i++;
		} else {
			rc2 = api->status;
			if (rc2 == NOT_STARTED) {
				rc2 = start_api(api);
				if (rc2 < 0 && rc == 0)
					rc = rc2;
				i = 0;
			} else {
				if (rc2 < 0  && rc == 0)
					rc = rc2;
				i++;
			}
		}
	}

	return rc;
}

/**
 * Starts the service 'api'.
 * @param api the api
 * @return zero on success, -1 on error
 */
static int start_api(struct api_desc *api)
{
	int rc;

	if (api->status != NOT_STARTED)
		return api->status;

	RP_NOTICE("API %s starting...", api->name);
	api->status = X_EBUSY;
	rc = start_array_classes(&api->require.classes);
	if (rc < 0)
		RP_ERROR("Cannot start classes needed by api %s", api->name);
	else {
		rc = start_array_depends(&api->require.apis);
		if (rc < 0)
			RP_ERROR("Cannot start apis needed by api %s", api->name);
		else if (api->api.itf->service_start) {
			rc = api->api.itf->service_start(api->api.closure);
			if (rc < 0)
				RP_ERROR("The api %s failed to start", api->name);
		}
	}
	api->status = rc;
	if (rc == 0)
		RP_INFO("API %s started", api->name);
	return rc;
}

/**
 * Starts a service by its 'api' name.
 * @param set the api set
 * @param name name of the service to start
 * @return zero on success, -1 on error
 */
int afb_apiset_start_service(struct afb_apiset *set, const char *name)
{
	struct api_desc *a;

	a = searchrec(set, name);
	if (!a) {
		RP_ERROR("can't find service %s", name);
		return X_ENOENT;
	}

	return start_api(a);
}

/**
 * Starts all possible services but stops at first error.
 * @param set the api set
 * @return 0 on success or a negative number when an error is found
 */
int afb_apiset_start_all_services(struct afb_apiset *set)
{
	struct afb_apiset *rootset;
	int rc, ret;
	int i;

	rootset = set;
	ret = 0;
	while (set) {
		i = 0;
		while (i < set->apis.count) {
			rc = set->apis.apis[i]->status;
			if (rc == NOT_STARTED) {
				rc = start_api(set->apis.apis[i]);
				if (rc < 0)
					ret = rc;
				set = rootset;
				i = 0;
			} else {
				if (rc)
					ret = -1;
				i++;
			}
		}
		set = set->subset;
	}
	return ret;
}

/**
 * Exits all started services
 * @param set the api set
 * @return 0 on success or a negative number when an error is found
 */
void afb_apiset_exit_all_services(struct afb_apiset *set, int code)
{
	struct api_desc *api;
	int i;

	while (set) {
		for (i = set->apis.count; i > 0 ; ) {
			api = set->apis.apis[--i];
			if (0 == api->status && api->api.itf->service_exit)
				api->api.itf->service_exit(api->api.closure, code);
			api->status = 0;
		}
		set = set->subset;
	}
}

#if WITH_AFB_HOOK
/**
 * Ask to update the hook flags of the 'api'
 * @param set the api set
 * @param name the api to update (NULL updates all)
 */
void afb_apiset_update_hooks(struct afb_apiset *set, const char *name)
{
	struct api_desc **i, **e, *d;

	if (!name) {
		i = set->apis.apis;
		e = &set->apis.apis[set->apis.count];
		while (i != e) {
			d = *i++;
			if (d->api.itf->update_hooks)
				d->api.itf->update_hooks(d->api.closure);
		}
	} else {
		d = searchrec(set, name);
		if (d && d->api.itf->update_hooks)
			d->api.itf->update_hooks(d->api.closure);
	}
}
#endif

/**
 * Set the logmask of the 'api' to 'mask'
 * @param set the api set
 * @param name the api to set (NULL set all)
 */
void afb_apiset_set_logmask(struct afb_apiset *set, const char *name, int mask)
{
	int i;
	struct api_desc *d;

	if (!name) {
		for (i = 0 ; i < set->apis.count ; i++) {
			d = set->apis.apis[i];;
			if (d->api.itf->set_logmask)
				d->api.itf->set_logmask(d->api.closure, mask);
		}
	} else {
		d = searchrec(set, name);
		if (d && d->api.itf->set_logmask)
			d->api.itf->set_logmask(d->api.closure, mask);
	}
}

/**
 * Get the logmask level of the 'api'
 * @param set the api set
 * @param name the api to get
 * @return the logmask level or -1 in case of error
 */
int afb_apiset_get_logmask(struct afb_apiset *set, const char *name)
{
	const struct api_desc *i;

	i = name ? searchrec(set, name) : NULL;
	if (!i)
		return X_ENOENT;

	if (!i->api.itf->get_logmask)
		return rp_logmask;

	return i->api.itf->get_logmask(i->api.closure);
}

void afb_apiset_describe(struct afb_apiset *set, const char *name, void (*describecb)(void *, struct json_object *), void *closure)
{
	const struct api_desc *i;
	struct json_object *r;

	r = NULL;
	if (name) {
		i = searchrec(set, name);
		if (i) {
			if (i->api.itf->describe) {
				i->api.itf->describe(i->api.closure, describecb, closure);
				return;
			}
		}
	}
	describecb(closure, r);
}


struct get_names {
	union  {
		struct {
			size_t count;
			size_t size;
		};
		struct {
			const char **ptr;
			char *data;
		};
	};
	int type;
};

static void get_names_count(void *closure, struct afb_apiset *set, const char *name, const char *aliasto)
{
	struct get_names *gc = closure;
	if ((2 - !aliasto) & gc->type) {
		gc->size += strlen(name);
		gc->count++;
	}
}

static void get_names_value(void *closure, struct afb_apiset *set, const char *name, const char *aliasto)
{
	struct get_names *gc = closure;
	if ((2 - !aliasto) & gc->type) {
		*gc->ptr++ = gc->data;
		gc->data = stpcpy(gc->data, name) + 1;
	}
}

#if !defined(APISET_NO_SORT)
static int get_names_sortcb(const void *a, const void *b)
{
	return namecmp(*(const char **)a, *(const char **)b);
}
#endif

/**
 * Get the list of api names
 * @param set the api set
 * @param rec recursive
 * @param type expected type: 1 names, 3 names+aliases, 2 aliases
 * @return a NULL terminated array of api names. Must be freed.
 */
const char **afb_apiset_get_names(struct afb_apiset *set, int rec, int type)
{
	struct get_names gc;
	size_t size;
	const char **names;

	gc.count = gc.size = 0;
	gc.type = type >= 1 && type <= 3 ? type : 1;
	afb_apiset_enum(set, rec, get_names_count, &gc);

	size = gc.size + gc.count * (1 + sizeof *names) + sizeof(*names);
	names = malloc(size);

	if (names) {
		gc.data = (char*)&names[gc.count + 1];
		gc.ptr = names;
		afb_apiset_enum(set, rec, get_names_value, &gc);
#if !defined(APISET_NO_SORT)
		qsort(names, (size_t)(gc.ptr - names), sizeof *names, get_names_sortcb);
#endif
		*gc.ptr = NULL;
	}
	return names;
}

/**
 * Enumerate the api names to a callback.
 * @param set the api set
 * @param rec should the enumeration be recursive
 * @param callback the function to call for each name
 * @param closure the closure for the callback
 */
void afb_apiset_enum(
	struct afb_apiset *set,
	int rec,
	void (*callback)(void *closure, struct afb_apiset *set, const char *name, const char *aliasto),
	void *closure)
{
	int i;
	struct afb_apiset *iset;
	struct api_desc *d;
	struct api_alias *a;

	iset = set;
	while (iset) {
		for (i = 0 ; i < iset->apis.count ; i++) {
			d = iset->apis.apis[i];
			if (searchrec(set, d->name) == d)
				callback(closure, iset, d->name, NULL);
		}
		a = iset->aliases;
		while (a) {
			if (searchrec(set, a->name) == a->api)
				callback(closure, iset, a->name, a->api->name);
			a = a->next;
		}
		iset = rec ? iset->subset : NULL;
	}
}

/**
 * Declare that the api of 'name' requires the api of name 'required'.
 * The api is searched in the apiset 'set' and if 'rec' isn't null also in its subset.
 * Returns 0 if the declaration successed or -1 in case of failure
 * (ENOMEM: allocation failure, ENOENT: api name not found)
 */
int afb_apiset_require(struct afb_apiset *declset, const char *apiname, struct afb_apiset *callset, const char *required)
{
	struct api_desc *a;
	struct api_depend *d;
	int rc;

	a = searchrec(declset, apiname);
	if (!a)
		rc = X_ENOENT;
	else {
		d = malloc(strlen(required) + 1 + sizeof *d);
		if (!d)
			rc = X_ENOMEM;
		else {
			d->callset = callset;
			strcpy(d->name, required);
			rc = api_array_add(&a->require.apis, d);
			if (rc < 0)
				free(d);
		}
	}
	return rc;
}

/**
 * Declare that the api of name 'apiname' requires the class of name 'classname'.
 * Returns 0 if the declaration successed or -1 in case of failure
 * (ENOMEM: allocation failure, ENOENT: api name not found)
 */
int afb_apiset_require_class(struct afb_apiset *declset, const char *apiname, const char *classname)
{
	struct api_desc *a = searchrec(declset, apiname);
	struct api_class *c = class_search(classname, 1);
	return a && c ? api_array_add(&a->require.classes, c) : X_ENOENT;
}

/**
 * Declare that the api of name 'apiname' provides the class of name 'classname'
 * Returns 0 if the declaration successed or -1 in case of failure
 * (ENOMEM: allocation failure, ENOENT: api name not found)
 */
int afb_apiset_provide_class(struct afb_apiset *declset, const char *apiname, const char *classname)
{
	struct api_desc *a = searchrec(declset, apiname);
	struct api_class *c = class_search(classname, 1);
	return a && c ? api_array_add(&c->providers, a) : X_ENOENT;
}

/**
 * Start any API that provides the class of name 'classname'
 */
int afb_apiset_class_start(const char *classname)
{
	struct api_class *cla = class_search(classname, 0);
	return cla ? start_class(cla) : X_ENOENT;
}

