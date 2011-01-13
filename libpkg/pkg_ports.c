#include <pkg.h>
#include <pkg_private.h>

#include <sha256.h>
#include <err.h>
#include <string.h>
#include <stdio.h>

int
ports_parse_plist(struct pkg *pkg, char *plist, const char *prefix)
{
	char *plist_p, *buf, *p;
	int nbel, i;
	size_t next;
	char sha256[65];
	char path[MAXPATHLEN];
	int ret = 0;

	buf = NULL;
	p = NULL;

	if (plist == NULL)
		return (-1);

	if (prefix == NULL)
		prefix = "/usr/local";

	nbel = split_chr(plist, '\n');

	next = strlen(plist);
	plist_p = plist;

	for (i = 0; i <= nbel; i++) {
		if (plist_p[0] == '@') {
			if (STARTS_WITH(plist_p, "@cwd ")) {
				buf = plist_p;
				buf += 5;
				if (buf != NULL)
					prefix = buf;
			} else if (STARTS_WITH(plist_p, "@comment ")){
				/* DO NOTHING: ignore the comments */
			}else {
				warnx("%s is deprecated, ignoring\n", buf);
			}
		} else {
			buf = plist_p;

			if (prefix[strlen(prefix) - 1] == '/')
				snprintf(path, MAXPATHLEN, "%s%s", prefix, buf);
			else
				snprintf(path, MAXPATHLEN, "%s/%s", prefix, buf);

			p = SHA256_File(path, sha256);

			if (p)
				pkg_addfile(pkg, path, p);
			else {
				warn("%s \n", path);
				ret--;
			}
		}

		plist_p += next + 1;
		next = strlen(plist_p);
	}
	return (ret);
}

int
ports_parse_depends(struct pkg *pkg, char *depends)
{
	struct pkg *dep;
	int nbel, i;
	char *dep_p, *buf, *v;
	size_t next;

	if (depends == NULL)
		return (-1);

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
		
		pkg_new(&dep);
		pkg_setname(dep, buf);
		buf += strlen(buf) + 1;

		pkg_setversion(dep, buf);
		buf += strlen(buf) + 1;

		pkg_setorigin(dep, buf);

		pkg_adddep(pkg, dep);

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
