#include <sys/types.h>
#include <sys/stat.h>

#include <assert.h>
#include <ctype.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "pkg.h"
#include "pkg_event.h"
#include "pkg_private.h"


struct hardlinks {
	ino_t *inodes;
	size_t len;
	size_t cap;
};

struct keyword {
	const char *keyword;
	STAILQ_HEAD(actions, action) actions;
	STAILQ_ENTRY(keyword) next;
};

struct plist {
	char *last_file;
	const char *prefix;
	struct sbuf *exec_scripts;
	struct sbuf *unexec_scripts;
	struct sbuf *pre_unexec_scripts;
	struct sbuf *post_unexec_scripts;
	struct pkg *pkg;
	const char *uname;
	const char *gname;
	const char *slash;
	bool ignore_next;
	mode_t perm;
	STAILQ_HEAD(keywords, keyword) keywords;
};

struct action {
	int (*perform)(struct plist *, char *);
	STAILQ_ENTRY(action) next;
};

static void
sbuf_append(struct sbuf *buf, __unused const char *comment, const char *str, ...)
{
	va_list ap;

	va_start(ap, str);

/*	if (sbuf_len(buf) == 0)
		sbuf_printf(buf, "#@%s\n", comment);*/

	sbuf_vprintf(buf, str, ap);
	va_end(ap);
}

#define post_unexec_append(buf, str, ...) sbuf_append(buf, "unexec", str, __VA_ARGS__)
#define pre_unexec_append(buf, str, ...) sbuf_append(buf, "unexec", str, __VA_ARGS__)
#define exec_append(buf, str, ...) sbuf_append(buf, "exec", str, __VA_ARGS__)

static int
setprefix(struct plist *p, char *line)
{
	/* if no arguments then set default prefix */
	if (line[0] == '\0')
		pkg_get(p->pkg, PKG_PREFIX, &p->prefix);
	else
		p->prefix = line;
	p->slash = p->prefix[strlen(p->prefix) -1] == '/' ? "" : "/";

	return (EPKG_OK);
}

static int
meta_dirrm(struct plist *p, char *line, bool try)
{
	size_t len;
	char path[MAXPATHLEN];

	len = strlen(line);

	while (isspace(line[len - 1]))
		line[len - 1] = '\0';

	if (line[0] == '/')
		snprintf(path, sizeof(path), "%s/", line);
	else
		snprintf(path, sizeof(path), "%s%s%s/", p->prefix, p->slash, line);

	return (pkg_adddir_attr(p->pkg, path, p->uname, p->gname, p->perm, try));
}

static int
dirrm(struct plist *p, char *line)
{
	return (meta_dirrm(p, line, false));
}

static int
dirrmtry(struct plist *p, char *line)
{
	return (meta_dirrm(p, line, true));
}

static int
setmod(struct plist *p, char *line)
{
	void *set;

	p->perm = 0;

	if (line[0] == '\0')
		return (EPKG_OK);

	if ((set = setmode(line)) == NULL)
		pkg_emit_error("%s wrong mode value", line);
	else
		p->perm = getmode(set, 0);
	return (EPKG_OK);
}

static int
setowner(struct plist *p, char *line)
{
	if (line[0] == '\0')
		p->uname = NULL;
	else
		p->uname = line;

	return (EPKG_OK);
}

static int
setgroup(struct plist *p, char *line)
{
	if (line[0] == '\0')
		p->gname = NULL;
	else
		p->gname = line;

	return (EPKG_OK);
}

static int
ignore(__unused struct plist *p, __unused char *line)
{
	return (EPKG_OK);
}

static int
ignore_next(struct plist *p, __unused char *line)
{
	p->ignore_next = true;

	return (EPKG_OK);
}

static void
populate_keywords(struct plist *p)
{
	struct keyword *k;
	struct action *a;

	/* @cwd */
	k = malloc(sizeof(struct keyword));
	a = malloc(sizeof(struct action));
	k->keyword = "cwd";
	STAILQ_INIT(&k->actions);
	a->perform = setprefix;
	STAILQ_INSERT_TAIL(&k->actions, a, next);
	STAILQ_INSERT_TAIL(&p->keywords, k, next);

	/* @ignore */
	k = malloc(sizeof(struct keyword));
	a = malloc(sizeof(struct action));
	k->keyword = "ignore";
	STAILQ_INIT(&k->actions);
	a->perform = ignore_next;
	STAILQ_INSERT_TAIL(&k->actions, a, next);
	STAILQ_INSERT_TAIL(&p->keywords, k, next);

	/* @comment */
	k = malloc(sizeof(struct keyword));
	a = malloc(sizeof(struct action));
	k->keyword = "comment";
	STAILQ_INIT(&k->actions);
	a->perform = ignore;
	STAILQ_INSERT_TAIL(&k->actions, a, next);
	STAILQ_INSERT_TAIL(&p->keywords, k, next);

	/* @dirrm */
	k = malloc(sizeof(struct keyword));
	a = malloc(sizeof(struct action));
	k->keyword = "dirrm";
	STAILQ_INIT(&k->actions);
	a->perform = dirrm;
	STAILQ_INSERT_TAIL(&k->actions, a, next);
	STAILQ_INSERT_TAIL(&p->keywords, k, next);

	/* @dirrmtry */
	k = malloc(sizeof(struct keyword));
	a = malloc(sizeof(struct action));
	k->keyword = "comment";
	STAILQ_INIT(&k->actions);
	a->perform = dirrmtry;
	STAILQ_INSERT_TAIL(&k->actions, a, next);
	STAILQ_INSERT_TAIL(&p->keywords, k, next);

	/* @mode */
	k = malloc(sizeof(struct keyword));
	a = malloc(sizeof(struct action));
	k->keyword = "mode";
	STAILQ_INIT(&k->actions);
	a->perform = setmod;
	STAILQ_INSERT_TAIL(&k->actions, a, next);
	STAILQ_INSERT_TAIL(&p->keywords, k, next);

	/* @owner */
	k = malloc(sizeof(struct keyword));
	a = malloc(sizeof(struct action));
	k->keyword = "owner";
	STAILQ_INIT(&k->actions);
	a->perform = setowner;
	STAILQ_INSERT_TAIL(&k->actions, a, next);
	STAILQ_INSERT_TAIL(&p->keywords, k, next);

	/* @group */
	k = malloc(sizeof(struct keyword));
	a = malloc(sizeof(struct action));
	k->keyword = "group";
	STAILQ_INIT(&k->actions);
	a->perform = setgroup;
	STAILQ_INSERT_TAIL(&k->actions, a, next);
	STAILQ_INSERT_TAIL(&p->keywords, k, next);


}

static int
parse_keywords(struct plist *plist, char *keyword, char *line)
{
	struct keyword *k;
	struct action *a;
	int ret = EPKG_FATAL;

	STAILQ_FOREACH(k, &plist->keywords, next) {
		if (!strcmp(k->keyword, keyword)) {
			STAILQ_FOREACH(a, &k->actions, next) {
				ret = a->perform(plist, line);
				if (ret != EPKG_OK)
					return (ret);
			}
			return (ret);
		}
	}

	return (ret);
}

int
ports_parse_plist(struct pkg *pkg, char *plist)
{
	char *plist_p, *buf, *p, *plist_buf;
	char comment[2];
	int nbel, i;
	size_t next;
	size_t len;
	size_t j;
	char sha256[SHA256_DIGEST_LENGTH * 2 + 1];
	char path[MAXPATHLEN + 1];
	char *cmd = NULL;
	struct stat st;
	int ret = EPKG_OK;
	off_t sz = 0;
	int64_t flatsize = 0;
	struct hardlinks hardlinks = {NULL, 0, 0};
	bool regular;
	regex_t preg1, preg2;
	regmatch_t pmatch[2];
	struct plist pplist;

	assert(pkg != NULL);
	assert(plist != NULL);

	pplist.last_file = NULL;
	pplist.prefix = NULL;
	pplist.exec_scripts = sbuf_new_auto();
	pplist.unexec_scripts = sbuf_new_auto();
	pplist.pre_unexec_scripts = sbuf_new_auto();
	pplist.post_unexec_scripts = sbuf_new_auto();
	pplist.uname = NULL;
	pplist.gname = NULL;
	pplist.perm = 0;
	pplist.pkg = pkg;
	pplist.slash = "";
	pplist.ignore_next = false;
	STAILQ_INIT(&pplist.keywords);

	populate_keywords(&pplist);

	regcomp(&preg1, "[[:space:]]\"(/[^\"]+)", REG_EXTENDED);
	regcomp(&preg2, "[[:space:]](/[[:graph:]/]+)", REG_EXTENDED);

	buf = NULL;
	p = NULL;

	if ((ret = file_to_buffer(plist, &plist_buf, &sz)) != EPKG_OK)
		return (ret);

	pkg_get(pkg, PKG_PREFIX, &pplist.prefix);
	pplist.slash = pplist.prefix[strlen(pplist.prefix) - 1] == '/' ? "" : "/";

	nbel = split_chr(plist_buf, '\n');

	next = strlen(plist_buf);
	plist_p = plist_buf;

	for (i = 0; i <= nbel; i++) {
		if (pplist.ignore_next) {
			pplist.ignore_next = false;
			continue;
		}

		if (plist_p[0] == '@') {
			if (STARTS_WITH(plist_p, "@unexec ") ||
			    STARTS_WITH(plist_p, "@exec ")) {
				buf = plist_p;

				while (!isspace(buf[0]))
					buf++;

				while (isspace(buf[0]))
					buf++;

				if (format_exec_cmd(&cmd, buf, pplist.prefix, pplist.last_file) != EPKG_OK)
					continue;

				if (plist_p[1] == 'u') {
					comment[0] = '\0';
					/* workaround to detect the @dirrmtry */
					if (STARTS_WITH(cmd, "rmdir ")) {
						comment[0] = '#';
						comment[1] = '\0';
					} else if (STARTS_WITH(cmd, "/bin/rmdir ")) {
						comment[0] = '#';
						comment[1] = '\0';
					}
					/* remove the glob if any */
					if (comment[0] == '#') {
						if (strchr(cmd, '*'))
						comment[0] = '\0';

						buf = cmd;

						/* start remove mkdir -? */
						/* remove the command */
						while (!isspace(buf[0]))
							buf++;

						while (isspace(buf[0]))
							buf++;

						if (buf[0] == '-')
							comment[0] = '\0';
						/* end remove mkdir -? */
					}

					/* more workarounds */
					if (strstr(cmd, "rmdir") || strstr(cmd, "kldxref") ||
					    strstr(cmd, "mkfontscale") || strstr(cmd, "mkfontdir") ||
					    strstr(cmd, "fc-cache") || strstr(cmd, "fonts.dir") ||
					    strstr(cmd, "fonts.scale") || strstr(cmd, "gtk-update-icon-cache") ||
					    strstr(cmd, "update-desktop-database") || strstr(cmd, "update-mime-database")) {
						if (comment[0] != '#')
							post_unexec_append(pplist.post_unexec_scripts, "%s%s\n", comment, cmd);
					} else
						sbuf_printf(pplist.unexec_scripts, "%s%s\n",comment, cmd);

					/* workaround to detect the @dirrmtry */
					if (comment[0] == '#') {
						buf = cmd;

						/* remove the @dirrm{,try}
						 * command */
						while (!isspace(buf[0]))
							buf++;

						split_chr(buf, '|');

						if (strstr(buf, "\"/")) {
							while (regexec(&preg1, buf, 2, pmatch, 0) == 0) {
								strlcpy(path, &buf[pmatch[1].rm_so], pmatch[1].rm_eo - pmatch[1].rm_so + 1);
								buf+=pmatch[1].rm_eo;
								if (!strcmp(path, "/dev/null"))
									continue;
								ret += pkg_adddir_attr(pkg, path, pplist.uname, pplist.gname, pplist.perm, 1);
							}
						} else {
							while (regexec(&preg2, buf, 2, pmatch, 0) == 0) {
								strlcpy(path, &buf[pmatch[1].rm_so], pmatch[1].rm_eo - pmatch[1].rm_so + 1);
								buf+=pmatch[1].rm_eo;
								if (!strcmp(path, "/dev/null"))
									continue;
								ret += pkg_adddir_attr(pkg, path, pplist.uname, pplist.gname, pplist.perm, 1);
							}
						}

					}
				} else {
					exec_append(pplist.exec_scripts, "%s\n", cmd);
				}

				free(cmd);
			} else {
				char *keyword = plist_p;
				keyword++; /* skip the @ */
				buf = keyword;
				while (!(isspace(buf[0]) || buf[0] == '\0'))
					buf++;
				if (buf[0] != '\0') {
					buf[0] = '\0';
					buf++;
				}
				/* trim write spaces */
				while (isspace(buf[0]))
					buf++;
				if (parse_keywords(&pplist, keyword, buf) != EPKG_OK)
					pkg_emit_error("unknown keyword %s, ignoring %s", keyword, plist_p);
			}
		} else if ((len = strlen(plist_p)) > 0){
			if (sbuf_len(pplist.unexec_scripts) > 0) {
				sbuf_finish(pplist.unexec_scripts);
				pre_unexec_append(pplist.pre_unexec_scripts, sbuf_data(pplist.unexec_scripts), "");
				sbuf_reset(pplist.unexec_scripts);
			}
			buf = plist_p;
			pplist.last_file = buf;
			sha256[0] = '\0';

			/* remove spaces at the begining and at the end */
			while (isspace(buf[0]))
				buf++;

			while (isspace(buf[len -  1])) {
				buf[len - 1] = '\0';
				len--;
			}

			snprintf(path, sizeof(path), "%s%s%s", pplist.prefix, pplist.slash, buf);

			if (lstat(path, &st) == 0) {
				p = NULL;
				regular = true;

				if (S_ISLNK(st.st_mode)) {
					regular = false;
				}
				/* Special case for hardlinks */
				if (st.st_nlink > 1) {
					for (j = 0; j < hardlinks.len; j++) {
						if (hardlinks.inodes[j] == st.st_ino) {
							regular = false;
							break;
						}
					}
					/* This is the first time we see this hardlink */
					if (regular == true) {
						if (hardlinks.cap <= hardlinks.len) {
							hardlinks.cap += BUFSIZ;
							hardlinks.inodes = reallocf(hardlinks.inodes,
								hardlinks.cap * sizeof(ino_t));
						}
						hardlinks.inodes[hardlinks.len++] = st.st_ino;
					}
				}

				if (regular) {
					flatsize += st.st_size;
					sha256_file(path, sha256);
					p = sha256;
				}
				ret += pkg_addfile_attr(pkg, path, p, pplist.uname, pplist.gname, pplist.perm);
			} else {
				pkg_emit_errno("lstat", path);
			}
		}

		if (i != nbel) {
			plist_p += next + 1;
			next = strlen(plist_p);
		}
	}

	pkg_set(pkg, PKG_FLATSIZE, flatsize);

	if (sbuf_len(pplist.pre_unexec_scripts) > 0) {
		sbuf_finish(pplist.pre_unexec_scripts);
		pkg_appendscript(pkg, sbuf_data(pplist.pre_unexec_scripts), PKG_SCRIPT_PRE_DEINSTALL);
	}
	if (sbuf_len(pplist.exec_scripts) > 0) {
		sbuf_finish(pplist.exec_scripts);
		pkg_appendscript(pkg, sbuf_data(pplist.exec_scripts), PKG_SCRIPT_POST_INSTALL);
	}
	if (sbuf_len(pplist.unexec_scripts) > 0) {
		sbuf_finish(pplist.unexec_scripts);
		post_unexec_append(pplist.post_unexec_scripts, sbuf_data(pplist.unexec_scripts), "");
		sbuf_finish(pplist.post_unexec_scripts);
	}
	if (sbuf_len(pplist.post_unexec_scripts) > 0)
		pkg_appendscript(pkg, sbuf_data(pplist.post_unexec_scripts), PKG_SCRIPT_POST_DEINSTALL);

	regfree(&preg1);
	regfree(&preg2);
	sbuf_delete(pplist.pre_unexec_scripts);
	sbuf_delete(pplist.exec_scripts);
	sbuf_delete(pplist.unexec_scripts);
	free(hardlinks.inodes);

	free(plist_buf);

	return (ret);
}
