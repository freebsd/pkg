#include <sys/stat.h>

#include <err.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "pkg.h"
#include "pkg_error.h"
#include "pkg_private.h"

int
ports_parse_plist(struct pkg *pkg, char *plist)
{
	char *plist_p, *buf, *p, *plist_buf;
	int nbel, i;
	size_t next;
	size_t len;
	char sha256[65];
	char path[MAXPATHLEN];
	char *last_plist_file = NULL;
	char *cmd = NULL;
	const char *prefix = NULL;
	struct stat st;
	int ret = EPKG_OK;
	off_t sz = 0;
	const char *slash;
	int64_t flatsize = 0;
	struct sbuf *exec_scripts = sbuf_new_auto();
	struct sbuf *unexec_scripts = sbuf_new_auto();

	buf = NULL;
	p = NULL;

	if (pkg == NULL)
		return (ERROR_BAD_ARG("pkg"));

	if (plist == NULL)
		return (ERROR_BAD_ARG("plist"));

	if ((ret = file_to_buffer(plist, &plist_buf, &sz)) != EPKG_OK)
		return (ret);

	prefix = pkg_get(pkg, PKG_PREFIX);
	slash = prefix[strlen(prefix) - 1] == '/' ? "" : "/";

	nbel = split_chr(plist_buf, '\n');

	next = strlen(plist_buf);
	plist_p = plist_buf;

	for (i = 0; i <= nbel; i++) {
		if (plist_p[0] == '@') {
			if (STARTS_WITH(plist_p, "@cwd ")) {
				buf = plist_p;
				buf += 5;
				/* with no arguments default to the original
				 * prefix */
				if (buf[0] == '\0')
					prefix = pkg_get(pkg, PKG_PREFIX);
				else
					prefix = buf;
				slash = prefix[strlen(prefix) - 1] == '/' ? "" : "/";
			} else if (STARTS_WITH(plist_p, "@comment ")) {
				/* DO NOTHING: ignore the comments */
			} else if (STARTS_WITH(plist_p, "@unexec ") ||
					   STARTS_WITH(plist_p, "@exec ")) {
				buf = plist_p;

				while (!isspace(buf[0]))
					buf++;

				while (isspace(buf[0]))
					buf++;

				if (format_exec_cmd(&cmd, buf, prefix, last_plist_file) < 0)
					continue;

				if (plist_p[1] == 'u') {
					if (sbuf_len(unexec_scripts) == 0)
						sbuf_cat(unexec_scripts, "#@unexec\n"); /* to be able to regenerate the @unexec in pkg2legacy */
					sbuf_printf(unexec_scripts, "%s\n", cmd);
				} else {
					if (sbuf_len(exec_scripts) == 0)
						sbuf_cat(exec_scripts, "#@exec\n"); /* to be able to regenerate the @exec in pkg2legacy */
					sbuf_printf(exec_scripts, "%s\n", cmd);
				}

				free(cmd);

			} else if (STARTS_WITH(plist_p, "@dirrm ") || STARTS_WITH(plist_p, "@dirrmtry ")) {

				buf = plist_p;

				/* remove the @dirrm or @dirrmtry */
				while (!isspace(buf[0]))
					buf++;

				while (isspace(buf[0]))
					buf++;

				len = strlen(buf);

				while (isspace(buf[len - 1])) {
					buf[len - 1] = '\0';
					len--;
				}

				snprintf(path, MAXPATHLEN, "%s%s%s/", prefix, slash, buf);

				ret += pkg_addfile(pkg, path, NULL);

			} else {
				warnx("%s is deprecated, ignoring", plist_p);
			}
		} else if ((len = strlen(plist_p)) > 0){
			buf = plist_p;
			last_plist_file = buf;
			sha256[0] = '\0';

			/* remove spaces at the begining and at the end */
			while (isspace(buf[0]))
				buf++;

			while (isspace(buf[len -  1])) {
				buf[len - 1] = '\0';
				len--;
			}

			snprintf(path, MAXPATHLEN, "%s%s%s", prefix, slash, buf);

			if (lstat(path, &st) == 0) {
				if (S_ISLNK(st.st_mode)) {
					p = NULL;
				} else {
					flatsize += st.st_size;
					if (sha256_file(path, sha256) != 0)
						pkg_error_warn("can not compute sha256");
					p = sha256;
				}
				ret += pkg_addfile(pkg, path, p);
			} else {
				warn("lstat(%s)", path);
			}
		}

		if (i != nbel) {
			plist_p += next + 1;
			next = strlen(plist_p);
		}
	}

	pkg_setflatsize(pkg, flatsize);
	if (sbuf_len(exec_scripts) > 0) {
		sbuf_done(exec_scripts);
		pkg_appendscript(pkg, sbuf_data(exec_scripts), PKG_SCRIPT_POST_INSTALL);
	}
	if (sbuf_len(unexec_scripts) > 0) {
		sbuf_done(unexec_scripts);
		pkg_appendscript(pkg, sbuf_data(unexec_scripts), PKG_SCRIPT_POST_DEINSTALL);
	}

	sbuf_delete(exec_scripts);
	sbuf_delete(unexec_scripts);

	free(plist_buf);

	return (ret);
}
