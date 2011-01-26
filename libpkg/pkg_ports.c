#include <pkg.h>
#include <pkg_private.h>

#include <sha256.h>
#include <err.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>

int
ports_parse_plist(struct pkg *pkg, char *plist, const char *prefix)
{
	char *plist_p, *buf, *p, *plist_buf;
	int nbel, i;
	size_t next;
	char sha256[65];
	char path[MAXPATHLEN];
	char *last_plist_file = NULL;
	char *cmd = NULL;
	struct stat st;
	int ret = EPKG_OK;
	off_t sz = 0;

	buf = NULL;
	p = NULL;

	if (plist == NULL)
		return (EPKG_NULL_VALUE);

	if ((ret = file_to_buffer(plist, &plist_buf, &sz)) != EPKG_OK)
		return (ret);

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
			} else if (STARTS_WITH(plist_p, "@unexec ") || STARTS_WITH(plist_p, "@exec")) {
				buf = plist_p;

				while (!isspace(buf[0]))
					buf++;

				while (isspace(buf[0]))
					buf++;

				if (format_exec_cmd(&cmd, buf, prefix, last_plist_file) < 0)
					continue;

				if (plist_p[1] == 'u')
					pkg_addexec(pkg, cmd, PKG_UNEXEC);
				else
					pkg_addexec(pkg, cmd, PKG_EXEC);

				free(cmd);

			}else {
				warnx("%s is deprecated, ignoring", plist_p);
			}
		} else if (strlen(plist_p) > 0){
			buf = plist_p;
			last_plist_file = buf;

			if (prefix[strlen(prefix) - 1] == '/')
				snprintf(path, MAXPATHLEN, "%s%s", prefix, buf);
			else
				snprintf(path, MAXPATHLEN, "%s/%s", prefix, buf);

			if (lstat(path, &st) >= 0)
				p = S_ISLNK(st.st_mode) ? NULL : SHA256_File(path, sha256);
			else {
				warn("lstat(%s)", path);
				p = NULL;
			}

			ret += pkg_addfile(pkg, path, p, -1);
		}

		if (i != nbel) {
			plist_p += next + 1;
			next = strlen(plist_p);
		}
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

int
ports_parse_scripts(struct pkg *pkg, char *scripts)
{
	int nbel, i;
	char *script_p;
	size_t next;

	if (scripts == NULL)
		return (-1);

	nbel = split_chr(scripts, ' ');
	script_p = scripts;

	next = strlen(script_p);
	for (i = 0; i <= nbel; i++) {
		pkg_addscript(pkg, script_p);

		script_p += next + 1;
		next = strlen(script_p);
	}

	return (0);
}

int
ports_parse_options(struct pkg *pkg, char *options)
{
	int nbel, i;
	char *option_p;
	size_t next;
	char *value;

	if (options == NULL)
		return (-1);

	nbel = split_chr(options, ' ');
	option_p = options;

	next = strlen(option_p);
	for (i = 0; i <= nbel; i++) {
		value = strrchr(option_p, '=');
		value[0] = '\0';
		value++;
		pkg_addoption(pkg, option_p, value);

		option_p += next + 1;
		next = strlen(option_p);
	}

	return (0);
}
