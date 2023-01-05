/*
 * Copyright (C) 2015-2023 IoT.bzh Company
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


#include <stdio.h>
#include <string.h>

#include "utils/globset.h"

void process(FILE *in, FILE *out)
{
	int rc;
	char buffer[1024], *str;
	const struct globset_handler *gh;
	struct globset *set;

	set = globset_create();
	while (fgets(buffer, sizeof buffer, in)) {
		str = strchr(buffer,'\n');
		if (str) *str = 0;
		switch (buffer[0]) {
		case '+':
			rc = globset_add(set, &buffer[1], NULL, NULL);
			fprintf(out, "add [%s]: %d, %s\n", &buffer[1], rc, strerror(-rc));
			break;
		case '-':
			rc = globset_del(set, &buffer[1], NULL);
			fprintf(out, "del [%s]: %d, %s\n", &buffer[1], rc, strerror(-rc));
			break;
		case '?':
			gh = globset_search(set, &buffer[1]);
			fprintf(out, "search [%s]: %s%s\n", &buffer[1], gh ? "found " : "NOT FOUND", gh ? gh->pattern : "");
			break;
		default:
			gh = globset_match(set, buffer);
			fprintf(out, "match [%s]: %s%s\n", buffer, gh ? "found by " : "NOT FOUND", gh ? gh->pattern : "");
			break;
		}
	}
	globset_destroy(set);
}

int compare(FILE *f1, FILE *f2)
{
	int l = 0, n = 0;
	char b1[1024], b2[1024];
	char *s1, *s2;

	for(;;) {
		l++;
		s1 = fgets(b1, sizeof b1, f1);
		s2 = fgets(b2, sizeof b2, f2);
		if (s1 == NULL || s2 == NULL) {
			if (s1 != s2) {
				fprintf(stderr, "Length of outputs differ\n");
				n++;
			}
			return n;
		}
		if (strcmp(s1, s2)) {
			fprintf(stderr, "Line %d differ\n\t%s\t%s", l, s1, s2);
			n++;
		}
	}
}

int main(int ac, char **av)
{
	FILE *in = stdin;
	FILE *out = stdout;
	FILE *ref = NULL;

	if (ac >= 2) {
		in = fopen(av[1], "r");
		if (in == NULL) {
			fprintf(stderr, "Can't open file %s: %m\n", av[1]);
			return 1;
		}
	}

	if (ac < 3)
		setvbuf(stdout, NULL, _IOLBF, 0);
	else {
		ref = fopen(av[2], "r");
		if (ref == NULL) {
			fprintf(stderr, "Can't open file %s: %m\n", av[2]);
			return 1;
		}
		out = tmpfile();
		if (out == NULL) {
			fprintf(stderr, "Can't create temporary file: %m\n");
			fclose(ref);
			return 1;
		}
	}

	process(in, out);

	if (ref) {
		rewind(out);
		if (compare(out, ref))
			return 1;
		fclose(ref);
	}
	if (in != stdin)
		fclose(in);
	if (out != stdout)
		fclose(out);

	return 0;
}
