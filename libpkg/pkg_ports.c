#include <pkg.h>
#include <pkg_private.h>

#include <sha256.h>
#include <err.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int
ports_parse_plist(struct pkg *pkg, char *plist, const char *prefix)
{
	char *plist_p, *buf, *p, *plist_buf;
	int nbel, i;
	size_t next;
	char sha256[65];
	char path[MAXPATHLEN];
	int ret = 0;

	buf = NULL;
	p = NULL;

	if (plist == NULL)
		return (-1);

	if (file_to_buffer(plist, &plist_buf) <= 0)
		return (-1);

	if (prefix == NULL)
		prefix = "/usr/local";

	nbel = split_chr(plist_buf, '\n');

	next = strlen(plist_buf);
	plist_p = plist_buf;

	for (i = 0; i <= nbel; i++) {
		if (plist_p[0] == '@') {
			if (STARTS_WITH(plist_p, "@cwd ")) {
				buf = plist_p;
				buf += 5;
				if (buf != NULL)
					prefix = buf;
			} else if (STARTS_WITH(plist_p, "@comment ")){
				/* DO NOTHING: ignore the comments */
			} else if (STARTS_WITH(plist_p, "@unexec ")) {
				/* TODO */
			} else if (STARTS_WITH(plist_p, "@exec ")) {
				/* TODO */
			}else {
				warnx("%s is deprecated, ignoring\n", plist_p);
			}
		} else if (strlen(plist_p) > 0){
			buf = plist_p;

			if (prefix[strlen(prefix) - 1] == '/')
				snprintf(path, MAXPATHLEN, "%s%s", prefix, buf);
			else
				snprintf(path, MAXPATHLEN, "%s/%s", prefix, buf);

			p = SHA256_File(path, sha256);

			if (p)
				pkg_addfile(pkg, path, p);
			else {
				ret--;
			}
		}

		plist_p += next + 1;
		next = strlen(plist_p);
	}

	free(plist_buf);

	return (ret);
}

int
ports_parse_depends(struct pkg *pkg, char *depends)
{
	int nbel, i;
	char *dep_p, *buf, *v, *name;;
	size_t next;

	if (depends == NULL)
		return (-1);

	if (depends[0] == '\0')
		return (0);

	nbel = split_chr(depends, '\n');

	buf = NULL;
	v = NULL;

	next = strlen(depends);
	dep_p = depends;

	for (i = 0; i <= nbel; i++) {

		buf = dep_p;
		split_chr(dep_p, ':');
		v = strrchr(dep_p, '-');
		v[0] = '\0';
		v++;
		
		name = buf;
		buf += strlen(buf) + 1;
		buf += strlen(buf) + 1;

		pkg_adddep(pkg, name, buf, v);

		dep_p += next + 1;
		next = strlen(dep_p);
	}

	return (0);
}

int
ports_parse_conflicts(struct pkg *pkg, char *conflicts)
{
	int nbel, i;
	char *conflict_p;
	size_t next;

	if (conflicts == NULL)
		return (-1);

	nbel = split_chr(conflicts, ' ');
	conflict_p = conflicts;

	next = strlen(conflict_p);
	for (i = 0; i <= nbel; i++) {
		pkg_addconflict(pkg, conflict_p);
		conflict_p += next + 1;
		next = strlen(conflict_p);
	}

	return (0);
}
