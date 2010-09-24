#include <ctype.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#include "util.h"
#include "pkg_compat.h"

static struct {
	const char *key;
	enum plist_t val;
} str2plist[] = {
	{"unexec", PLIST_UNEXEC },
	{"srcdir", PLIST_SRC },
	{"pkgdep", PLIST_PKGDEP },
	{"owner", PLIST_CHOWN },
	{"option", PLIST_OPTION },
	{"noinst", PLIST_NOINST },
	{"name", PLIST_NAME },
	{"mtree", PLIST_MTREE },
	{"mode", PLIST_CHMOD },
	{"ignore_inst", PLIST_IGNORE_INST },
	{"ignore", PLIST_IGNORE },
	{"group", PLIST_CHGRP },
	{"exec", PLIST_CMD },
	{"display", PLIST_DISPLAY },
	{"dirrm", PLIST_DIR_RM },
	{"cwd", PLIST_CWD },
	{"conflicts", PLIST_CONFLICTS },
	{"comment", PLIST_COMMENT },
	{"cd", PLIST_CWD },
};

static void
str_lowercase(char *str)
{
	while (*str) {
		*str = tolower(*str);
		++str;
	}
}

static int
pkg_compat_plist_cmd(char *s, char **arg)
{
	char cmd[FILENAME_MAX + 20];    /* 20 == fudge for max cmd len */
	char *cp;
	char *sp;
	size_t i;

	strlcpy(cmd, s, sizeof(cmd));
	str_lowercase(cmd);
	cp = cmd;
	sp = s;
	while (*cp) {
		if (isspace(*cp)) {
			*cp = '\0';
			while (isspace(*sp)) /* Never sure if macro, increment later */
				++sp;
			break;
		}
		++cp, ++sp;
	}
	if (arg)
		*arg = sp;

	for (i = 0; i < sizeof(str2plist) / sizeof(str2plist[0]); i++) {
		if (strcmp(cmd, str2plist[i].key) == 0) {
			switch (str2plist[i].val) {
				case PLIST_COMMENT:
					if (!strncmp(*arg, "ORIGIN:", 7)) {
						*arg += 7;
						return PLIST_ORIGIN;
					} else if (!strncmp(*arg, "DEPORIGIN:", 10)) {
						*arg += 10;
						return PLIST_DEPORIGIN;
					} else if (!strncmp(*arg, "MD5:", 4)) {
						*arg += 4;
						return PLIST_MD5;
					}
					return PLIST_COMMENT;
					break;
				default:
					return str2plist[i].val;
					break;
			}
		}
	}
	return -1;
}

static void
pkg_compat_read_plist(cJSON *pkg, char *plist_str)
{
	int cmd;
	char *buf, *next, *cp = NULL;
	char *tmp;

	char *dep = NULL;
	char *prefix = NULL;
	char path_file[MAXPATHLEN];
	cJSON *object;

	buf = plist_str;
	while ((next = strchr(buf, '\n')) != NULL) {
		next[0] = '\0';

		while (strlen(buf) > 0 && isspace(buf[strlen(buf) - 1]))
			buf[strlen(buf) - 1] = '\0';

		if (buf[0] != '@') {
			cmd = PLIST_FILE;
		} else {
			cmd = pkg_compat_plist_cmd(buf + 1, &cp);
			if (cmd == -1) {
				warnx("%s: unknown command '%s'",
						__func__, buf);
			} else if (*cp == '\0') {
				cp = NULL;
				if (cmd == PLIST_PKGDEP) {
					warnx("corrupted record (pkgdep line without argument), ignoring");
					cmd = -1;
				}
			}
		}

		switch(cmd) {
			case PLIST_NAME:
				tmp = strrchr(cp, '-');
				tmp[0] = '\0';
				tmp++;
				cJSON_AddStringToObject(pkg, "name", cp);
				cJSON_AddStringToObject(pkg, "version", tmp);
				break;

			case PLIST_ORIGIN:
				cJSON_AddStringToObject(pkg, "origin", cp);
				break;

			case PLIST_CWD:
				prefix = cp;
				break;

			case PLIST_FILE:
				snprintf(path_file, MAXPATHLEN, "%s/%s", prefix, buf);
				break;

			case PLIST_MD5:
				object = cJSON_CreateObject();
				cJSON_AddStringToObject(object, "path", path_file);
				cJSON_AddStringToObject(object, "md5", cp);
				cJSON_AddItemToArray(cJSON_GetObjectItem(pkg, "files"), object);
				break;

			case PLIST_CMD:
				tmp = str_replace(cp, "\%D", prefix);
				cJSON_AddItemToArray(cJSON_GetObjectItem(pkg, "exec"), cJSON_CreateString(tmp));
				free(tmp);
				break;

			case PLIST_UNEXEC:
				tmp = str_replace(cp, "\%D", prefix);
				cJSON_AddItemToArray(cJSON_GetObjectItem(pkg, "unexec"), cJSON_CreateString(tmp));
				free(tmp);
				break;

			case PLIST_PKGDEP:
				dep = cp;
				break;
			case PLIST_DEPORIGIN:
				tmp = strrchr(dep, '-');
				tmp[0] = '\0';
				tmp++;
				object = cJSON_CreateObject();
				cJSON_AddStringToObject(object, "name", dep);
				cJSON_AddStringToObject(object, "origin", cp);
				cJSON_AddStringToObject(object, "version", tmp);
				cJSON_AddItemToArray(cJSON_GetObjectItem(pkg, "deps"), object);
				break;

			case PLIST_CONFLICTS:
				cJSON_AddItemToArray(cJSON_GetObjectItem(pkg, "conflicts"), cJSON_CreateString(cp));
				break;

			case PLIST_MTREE:
			case PLIST_DISPLAY:
			case PLIST_DIR_RM:
			case PLIST_COMMENT:
			case PLIST_IGNORE:
				/* IGNORING */
				break;

			default:
				warnx("====> unparsed line: '%s'", buf);
				break;
		}
		buf = next;
		buf++;
	}
}

cJSON *
pkg_compat_converter(char *plist_str)
{
	struct utsname uts;
	char *osrelease;
	char *tmp;

	cJSON *rootpkg = cJSON_CreateObject();
	uname(&uts);
	
	cJSON_AddStringToObject(rootpkg, "arch", uts.machine);

	osrelease = strdup(uts.release);
	tmp = strrchr(osrelease, '-');
	tmp[0] = '\0';

	cJSON_AddStringToObject(rootpkg, "osrelease", osrelease);
	free(osrelease);

	cJSON_AddNumberToObject(rootpkg, "osversion", __FreeBSD_version);
	cJSON_AddFalseToObject(rootpkg, "automatic");
	cJSON_AddItemToObject(rootpkg, "files", cJSON_CreateArray());
	cJSON_AddItemToObject(rootpkg, "exec", cJSON_CreateArray());
	cJSON_AddItemToObject(rootpkg, "unexec", cJSON_CreateArray());
	cJSON_AddItemToObject(rootpkg, "options", cJSON_CreateArray());
	cJSON_AddItemToObject(rootpkg, "conflicts", cJSON_CreateArray());
	cJSON_AddItemToObject(rootpkg, "deps", cJSON_CreateArray());

	pkg_compat_read_plist(rootpkg, plist_str);

	return (rootpkg);
}

cJSON *
pkg_compat_convert_installed(const char *pkg_dbdir, char *pkgname, char *manifestpath)
{
	cJSON *rootpkg;
	char *cjson_output;
	FILE *fs;
	char *buffer;
	off_t buffer_len;
	char filepath[MAXPATHLEN];

	snprintf(filepath, sizeof(filepath), "%s/%s/+CONTENTS", pkg_dbdir, pkgname);

	rootpkg = cJSON_CreateObject();

	if ((file_to_buffer(filepath, &buffer)) == -1) {
		warn("Unable to read +CONTENTS for %s", pkgname);
		return (0);
	}

	rootpkg = pkg_compat_converter(buffer);
	free(buffer);

	if (rootpkg == 0) {
		warnx("%s: Manifest corrupted, skipping", pkgname);
		return (0);
	}

	/* adding comment */
	snprintf(filepath, sizeof(filepath), "%s/+COMMENT", dirname(filepath));

	if ((buffer_len = file_to_buffer(filepath, &buffer)) == -1) {
		warn("Unable to read +COMMENT for %s", pkgname);
	} else {
		if (buffer[buffer_len - 1 ] == '\n')
			buffer[buffer_len -1 ] = '\0';

		cJSON_AddStringToObject(rootpkg, "comment", buffer);
		free(buffer);
	}

	/* adding description */
	snprintf(filepath, sizeof(filepath), "%s/+DESC", dirname(filepath));

	if ((buffer_len = file_to_buffer(filepath, &buffer)) == -1) {
		warn("Unable to read +DESC for %s", pkgname);
	} else {
		cJSON_AddStringToObject(rootpkg, "desc", buffer);
		free(buffer);
	}


	/* adding display */
	snprintf(filepath, sizeof(filepath), "%s/+DISPLAY", dirname(filepath));
	/* ignore if no +DISPLAY */
	if ((buffer_len = file_to_buffer(filepath, &buffer)) != -1) {
		cJSON_AddStringToObject(rootpkg, "display", buffer);
		free(buffer);
	}

	/* write the new manifest */
	cjson_output = cJSON_Print(rootpkg);
	fs = fopen(manifestpath, "w+");
	fprintf(fs, "%s", cjson_output);
	free(cjson_output);
	fclose(fs);

	return (rootpkg);
}
