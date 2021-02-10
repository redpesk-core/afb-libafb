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


#include <stdlib.h>

#include "sys/subpath.h"

/* a valid subpath is a relative path not looking deeper than root using .. */
int subpath_is_valid(const char *path)
{
	int l = 0, i = 0;

	/* absolute path is not valid */
	if (path[i] == '/')
		return 0;

	/* inspect the path */
	while(path[i]) {
		switch(path[i++]) {
		case '.':
			if (!path[i])
				break;
			if (path[i] == '/') {
				i++;
				break;
			}
			if (path[i++] == '.') {
				if (!path[i]) {
					l--;
					break;
				}
				if (path[i++] == '/') {
					l--;
					break;
				}
			}
		default:
			while(path[i] && path[i] != '/')
				i++;
			if (l >= 0)
				l++;
		case '/':
			break;
		}
	}
	return l >= 0;
}

/*
 * Return the path or NULL is not valid.
 * Ensure that the path doesn't start with '/' and that
 * it does not contains sequence of '..' going deeper than
 * root.
 * Returns the path or NULL in case of
 * invalid path.
 */
const char *subpath(const char *path)
{
	return path && subpath_is_valid(path) ? (path[0] ? path : ".") : NULL;
}

/*
 * Normalizes and checks the 'path'.
 * Removes any starting '/' and checks that 'path'
 * does not contains sequence of '..' going deeper than
 * root.
 * Returns the normalized path or NULL in case of
 * invalid path.
 */
const char *subpath_force(const char *path)
{
	while(path && *path == '/')
		path++;
	return subpath(path);
}

#if defined(TEST_subpath)
#include <stdio.h>
void t(const char *subpath, int validity) {
  printf("%s -> %d = %d, %s\n", subpath, validity, subpath_is_valid(subpath), subpath_is_valid(subpath)==validity ? "ok" : "NOT OK");
}
int main() {
  t("/",0);
  t("..",0);
  t(".",1);
  t("../a",0);
  t("a/..",1);
  t("a/../////..",0);
  t("a/../b/..",1);
  t("a/b/c/..",1);
  t("a/b/c/../..",1);
  t("a/b/c/../../..",1);
  t("a/b/c/../../../.",1);
  t("./..a/././..b/..c/./.././.././../.",1);
  t("./..a/././..b/..c/./.././.././.././..",0);
  t("./..a//.//./..b/..c/./.././/./././///.././.././a/a/a/a/a",1);
  return 0;
}
#endif

