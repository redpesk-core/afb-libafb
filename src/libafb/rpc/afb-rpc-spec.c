/*
 * Copyright (C) 2015-2025 IoT.bzh Company
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

#include "afb-rpc-spec.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <rp-utils/rp-escape.h>

#include "sys/x-errno.h"
#include "core/afb-apiname.h"


#include <string.h>
#include <unistd.h>

/*
 * catchall (star) modes
 */
/** no star is given */
#define STAR_NO     0
/** generic star is given: local = remote */
#define STAR_YES    1
/** star given mapped to one api */
#define STAR_AS     2

/****************************************************************
 * afb_rpc_spec is a compact structure built as follow
 *
 * +----------+------------+----------+------------+
 * : count    : star_mode  :      star_arg         : imports
 * +----------+------------+----------+------------+
 * : count    : star_mode  :      star_arg         : exports
 * +----------+------------+----------+------------+
 *                  A L I G N
 * +----------+------------+----------+------------+
 * : offset from name      : offset to name        : offsets
 * :    ...   ...  ...  ...  ...   ...   ...       :
 * :    ...   ...  ...  ...  ...   ...   ...       :
 * :    ...   ...  ...  ...  ...   ...   ...       :
 * +----------+------------+----------+------------+
 * : characters of the strings                     : sstrings
 * :    ...   ...  ...  ...  ...   ...   ...       :
 * :    ...   ...  ...  ...  ...   ...   ...       :
 * :    ...   ...  ...  ...  ...   ...   ...       :
 * +----------+------------+----------+------------+
 */

/** Describes one of the entries imports or exports */
struct desc {
	/** upper of api exported */
	uint8_t  upper;
	/** catchall mode */
	uint8_t  star_mode;
	/** catchall associated API if needed */
	uint16_t star_arg;
};

/** Offsets of local and remote API strings */
struct off {
	/** Offset of local API string */
	uint16_t local;
	/** Offset of remote API string */
	uint16_t remote;
};

/** Structure describing a RPC specification */
struct afb_rpc_spec {
	/** reference count */
	uint16_t refcount;
	/** description of imports */
	struct desc imports;
	/** description of exports */
	struct desc exports;
	/** offsets of strings */
	struct off  offsets[];
};

/** structure for building strings of spec */
struct memstr {
	/** pointer to the previous value */
	const struct memstr *previous;
	/** pointer to the string (not zero terminated) */
	const char *string;
	/** the length of the string */
	size_t      length;
	/** the computed offset */
	size_t      offset;
};

/** structure for recording API spec */
struct recapi {
	/** star mode */
	uint8_t star;
	/** local string */
	struct memstr local;
	/** remote string */
	struct memstr remote;
};

/**********************************************************************/
/**********************************************************************/
/** BUILDING THE STRUCTURE ********************************************/
/**********************************************************************/
/**********************************************************************/

/** internal function for reporting an error and cleaning memory */
static
struct afb_rpc_spec *
build_error(
	struct afb_rpc_spec *spec,
	int code,
	int *error
) {
	free(spec);
	*error = code;
	return NULL;
}

/** Allocates the spec for the given strings and count of api spec */
static
struct afb_rpc_spec *
build_alloc(
	const struct memstr *memstr,
	unsigned             count,
	int                 *error
) {
	struct afb_rpc_spec *resu;
	size_t size;
	char *strs;

	/* check the count and string size */
	size = 1 + memstr->offset + memstr->length;
	if (count > 255 || size > 65535) {
		*error = -1;
		return NULL;
	}

	/* allocation */
	size += sizeof (struct afb_rpc_spec) + (unsigned)count * sizeof (struct off);
	resu = malloc(size);
	if (resu == NULL)
		*error = -1;
	else {
		/* initialisation */
		resu->refcount = 1;
		resu->imports.star_mode = resu->exports.star_mode = STAR_NO;
		resu->imports.star_arg = resu->exports.star_arg = 0;
		/* initialisation of strings */
		strs = (char*)&resu->offsets[count];
		while(memstr != NULL) {
			memcpy(&strs[memstr->offset], memstr->string, memstr->length);
			strs[memstr->offset + memstr->length] = 0;
			memstr = memstr->previous;
		}
	}
	return resu;
}

/** search for a string of given length in memstr */
static
const struct memstr *
build_memstr(
	const struct memstr *memstr,
	struct memstr       *dest,
	const char          *string,
	size_t               length
) {
	const struct memstr *it = memstr;
	while (it != NULL) {
		if (length == it->length
		 && (length == 0
		     || memcmp(string, it->string, it->length) == 0)) {
			/* found, just record offset */
			dest->offset = it->offset;
			/* return existing list */
			return memstr;
		}
		it = it->previous;
	}
	/* not foundi, record the string */
	dest->string = string;
	dest->length = length;
	dest->offset = memstr->offset + memstr->length + 1;
	/* return new list with dest on top */
	dest->previous = memstr;
	return dest;
}




/** */
static
int
build_get(
	const struct memstr **p_memstr,
	const char          **p_str,
	struct recapi        *recapi
) {
	size_t lloc, lrem, irem;
	const char *str = *p_str;
	const struct memstr *memstr = *p_memstr;

	/* get strings */
	for (lloc = 0; str[lloc] != 0 && str[lloc] != '@' && str[lloc] != ',' ; lloc++);
	if (str[lloc] != '@') {
		/* remote is same than local */
		irem = 0;
		lrem = lloc;
	}
	else {
		/* remote is not local */
		irem = lloc + 1;
		for (lrem = 0; str[irem + lrem] != 0 && str[irem + lrem] != ',' ; lrem++);
	}

	/* compute star kind and strings */
	if (lloc == 1 && str[0] == '*') {
		if (irem == 0)
			recapi->star = STAR_YES;
		else
			return -1;
	}
	else if (lrem == 1 && str[irem] == '*') {
		memstr = build_memstr(memstr, &recapi->local, str, lloc);
		recapi->star = STAR_AS;
	}
	else {
		memstr = build_memstr(memstr, &recapi->local, str, lloc);
		memstr = build_memstr(memstr, &recapi->remote, &str[irem], lrem);
		recapi->star = STAR_NO;
	}

	/* emit the result */
	*p_memstr = memstr;
	if (str[irem + lrem] != 0)
		*p_str = &str[irem + lrem + 1];
	else
		*p_str = NULL;
	return 0;
}

/** */
static
struct afb_rpc_spec *
build_set(
	struct afb_rpc_spec *spec,
	unsigned             count,
	int                 *error,
	const struct recapi *recapi,
	struct desc         *desc
) {
	if (recapi->star == STAR_NO) {
		spec->offsets[count].local = (uint16_t)recapi->local.offset;
		spec->offsets[count].remote = (uint16_t)recapi->remote.offset;
	}
	else if (desc->star_mode != STAR_NO)
		return build_error(spec, -1, error);
	else {
		desc->star_mode = recapi->star;
		if (recapi->star != STAR_YES)
			desc->star_arg = (uint16_t)recapi->local.offset;
	}
	return spec;
}

/** */
static
struct afb_rpc_spec *
build_exports(
	const struct memstr *memstr,
	unsigned             count,
	int                 *error,
	const char          *exports
) {
	struct afb_rpc_spec *resu;
	if (exports == NULL) {
		resu = build_alloc(memstr, count, error);
		if (resu != NULL)
			resu->exports.upper = (uint8_t)count;
	}
	else {
		struct recapi recapi;
		int rc = build_get(&memstr, &exports, &recapi);
		if (rc)
			return build_error(NULL, rc, error);

		resu = build_exports(memstr, count + (recapi.star == STAR_NO), error, exports);
		if (resu != NULL)
			resu = build_set(resu, count, error, &recapi, &resu->exports);
	}
	return resu;
}

/** */
static
struct afb_rpc_spec *
build_imports(
	const struct memstr *memstr,
	unsigned             count,
	int                 *error,
	const char          *exports,
	const char          *imports
) {
	struct afb_rpc_spec *resu;
	if (imports == NULL) {
		resu = build_exports(memstr, count, error, exports);
		if (resu != NULL)
			resu->imports.upper = (uint8_t)count;
	}
	else {
		struct recapi recapi;
		int rc = build_get(&memstr, &imports, &recapi);
		if (rc)
			return build_error(NULL, rc, error);

		resu = build_imports(memstr, count + (recapi.star == STAR_NO), error, exports, imports);
		if (resu != NULL)
			resu = build_set(resu, count, error, &recapi, &resu->imports);
	}
	return resu;
}

/**********************************************************************/
/**********************************************************************/
/** PUBLIC INTERFACE **************************************************/
/**********************************************************************/
/**********************************************************************/

struct afb_rpc_spec *afb_rpc_spec_addref(struct afb_rpc_spec *spec)
{
	__atomic_add_fetch(&spec->refcount, 1, __ATOMIC_RELAXED);
	return spec;
}

void afb_rpc_spec_unref(struct afb_rpc_spec *spec)
{
	if (spec && !__atomic_sub_fetch(&spec->refcount, 1, __ATOMIC_RELAXED))
		free(spec);
}

int afb_rpc_spec_make(
	struct afb_rpc_spec **spec,
	const char *imports,
	const char *exports
) {
	int rc = 0;
	char c0 = 0;
	struct memstr memstr;

	/* init with empty initial string */
	memstr.previous = NULL;
	memstr.string = &c0;
	memstr.length = 0;
	memstr.offset = 0;

	/* build */
	*spec = build_imports(&memstr, 0, &rc, exports, imports);
	return rc;
}

int afb_rpc_spec_for_api(struct afb_rpc_spec **spec, const char *api, bool client)
{
	return afb_rpc_spec_make(spec,
	                         client ? api : NULL,
	                         client ? NULL : api);
}

int afb_rpc_spec_from_uri(struct afb_rpc_spec **spec, const char *uri, bool client)
{
	const char **args;
	const char *api, *as_api, *uri_args, *imports, *exports;
	char *apicpy;
	size_t len;
	int rc;

	/* look for "as-api" in URI query section */
	uri_args = strchr(uri, '?');
	if (uri_args != NULL) {
		args = rp_unescape_args(uri_args + 1);
		imports = rp_unescaped_args_get(args, "import");
		exports = rp_unescaped_args_get(args, "export");
		as_api = rp_unescaped_args_get(args, "as-api");
		if (imports != NULL || exports != NULL) {
			rc = afb_rpc_spec_make(spec, imports, exports);
			free(args);
			return rc;
		}
		if (as_api != NULL) {
			rc = afb_rpc_spec_for_api(spec, as_api, client);
			free(args);
			return rc;
		}
		free(args);
	}

	/* look for a '/' or a ':' */
	len = uri_args ? (size_t)(uri_args - uri) : strlen(uri); // stop before the '?' when there's one
	api = memrchr(uri, '/', len);
	if (api == NULL) {
		api = memrchr(uri, ':', len);
		if (api == NULL) {
			/* not found */
			*spec = NULL;
			return X_ENOENT;
		}
		if (api[1] == '@')
			api++;
	}

	/* at this point api is the char before an api name */
	api++;
	len -= (size_t)(api - uri);
	apicpy = malloc(len + 1);
	if (apicpy == NULL) {
		/* out of memory */
		*spec = NULL;
		return X_ENOMEM;
	}
	strncpy(apicpy, api, len);
	apicpy[len] = '\0';
	rc = afb_rpc_spec_for_api(spec, apicpy, client);
	free(apicpy);
	return rc;
}

int afb_rpc_spec_search(
	const struct afb_rpc_spec *spec,
	const char *api,
	bool client,
	const char **result
) {
	const char *strings = (const char*)&spec->offsets[spec->exports.upper];
	const struct desc *desc = client ? &spec->imports : &spec->exports;
	const struct off *iter = &spec->offsets[client ? 0 : spec->imports.upper];
	const struct off *end = &spec->offsets[desc->upper];

#define STR(idx) ((idx) ? &strings[idx] : NULL)
	/* it is asserted that client implies api != NULL */
	if (api == NULL || *api == 0) {
		/* search for the default api */
		for( ; iter != end ; iter++)
			if (iter->remote == 0) { /* empty string */
				*result = STR(iter->local); /* found */
				return 0;
			}

		switch (desc->star_mode) {
		case STAR_AS:
			*result = STR(desc->star_arg);
			return 0;
		case STAR_YES:
			*result = NULL;
			return X_EINVAL;
		default:
			*result = NULL;
			return X_ENOENT;
		}
	}
	else {
		/* search for the given api */
		for( ; iter != end ; iter++)
			if (strcmp(api, &strings[iter->local]) == 0) {
				*result = STR(iter->remote); /* found */
				return 0;
			}

		/* not found in the list */
		switch (desc->star_mode) {
		case STAR_YES:
			*result = api;
			return 0;
		case STAR_AS:
			*result = STR(desc->star_arg);
			return 0;
		default:
			*result = NULL;
			return X_ENOENT;
		}
	}
#undef STR
}

int afb_rpc_spec_for_each(
	const struct afb_rpc_spec *spec,
	bool client,
	int (*callback)(void *closure, const char *locname, const char *remname),
	void *closure
) {
	int rc = 0;
	const struct desc *desc;
	uint8_t base;
	const char *loc, *rem, *str;

	desc = client ? &spec->imports : &spec->exports;
	base = client ? 0 : spec->imports.upper;
	str = (const char*)&spec->offsets[spec->exports.upper];

	switch (desc->star_mode) {
	default:
	case STAR_NO:
		break;
	case STAR_YES:
		rc = callback(closure, NULL, NULL);
		break;
	case STAR_AS:
		loc = &str[desc->star_arg];
		rc = callback(closure, loc, NULL);
		break;
	}
	while (rc == 0 && base < desc->upper) {
		loc = &str[spec->offsets[base].local];
		rem = &str[spec->offsets[base].remote];
		rc = callback(closure, loc, rem);
		base++;
	}
	return rc;
}

static int put(char **pbuf, size_t *size, const char *str)
{
	char *buf = *pbuf;
	size_t sz = *size;
	size_t len = strlen(str);
	size_t need = sz + len + 1;
	const size_t grain = 256;
	size_t allszbef = (sz + grain - 1) - ((sz + grain - 1) & (grain - 1));
	size_t allszaft = (need + grain - 1) - ((need + grain - 1) & (grain - 1));
	if (allszbef != allszaft) {
		char *nbuf = realloc(buf, allszaft);
		if (nbuf == NULL)
			return -1;
		buf = nbuf;
	}
	memcpy(&buf[sz], str, len + 1);
	*pbuf = buf;
	*size = sz + len;
	return 0;
}

static int put_str(
	char **buf,
	size_t *size,
	const struct afb_rpc_spec *spec,
	uint16_t offset
) {
	const char *str = (const char*)&spec->offsets[spec->exports.upper];
	return put(buf, size, &str[offset]);
}

static int put_desc(
	char **buf,
	size_t *size,
	const char *key,
	const struct afb_rpc_spec *spec,
	const struct desc *desc,
	uint8_t base,
	const char *prefix
) {
	int rc = 0;

	if (base != desc->upper || desc->star_mode != STAR_NO) {
		bool pc = true;
		if (*buf != NULL && prefix != NULL)
			rc = put(buf, size, prefix);
		if (rc >= 0)
			rc = put(buf, size, key);
		if (rc >= 0) {
			switch (desc->star_mode) {
			default:
			case STAR_NO:
				pc = false;
				break;
			case STAR_YES:
				rc = put(buf, size, "*");
				break;
			case STAR_AS:
				rc = put_str(buf, size, spec, desc->star_arg);
				if (rc >= 0)
					rc = put(buf, size, "@*");
				break;
			}
		}
		while (rc >= 0 && base < desc->upper) {
			if (pc)
				rc = put(buf, size, ",");
			else
				pc = true;
			if (rc >= 0)
				rc = put_str(buf, size, spec, spec->offsets[base].local);
			if (rc >= 0 && spec->offsets[base].local != spec->offsets[base].remote) {
				rc = put(buf, size, "@");
				if (rc >= 0)
					rc = put_str(buf, size, spec, spec->offsets[base].remote);
			}
			base++;
		}
	}
	return rc;
}

char *afb_rpc_spec_dump(
	const struct afb_rpc_spec *spec,
	size_t *length
) {
	char *resu = NULL;
	size_t size = 0;
	int rc = 0;

	if (spec != NULL) {
		rc = put_desc(&resu, &size, "import=", spec, &spec->imports, 0, NULL);
		if (rc == 0)
			rc = put_desc(&resu, &size, "export=", spec, &spec->exports, spec->imports.upper, "&");
	}
	if (rc >= 0 && resu == NULL)
		rc = put(&resu, &size, "NULL");
	if (rc < 0) {
		free(resu);
		resu = NULL;
		size = 0;
	}
	if (length != NULL)
		*length = size;
	return resu;
}

