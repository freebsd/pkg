#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cdb.h>
#include <cJSON.h>

#include "pkg_compat.h"
#include "pkgdb_cache.h"

static void pkgdb_cache_rebuild(const char *, const char *);

static void
pkgdb_cache_rebuild(const char *pkg_dbdir, const char *cache_path)
{
	int fd;
	char key[BUFSIZ];
	char *value;
	char tmppath[MAXPATHLEN];
	char contentpath[MAXPATHLEN];
	struct cdb_make cdb_make;
	DIR *dir;
	struct dirent *portsdir;
	struct stat st;
	FILE *content;
	char *content_buffer;
	cJSON *manifest_json;
	int nb_packages = 0;


	strlcpy(tmppath, pkg_dbdir, MAXPATHLEN);
	strlcat(tmppath, "/pkgdb.cache", MAXPATHLEN);

	printf("Rebuilding cache...\n");
	fd = mkstemp(tmppath);

	cdb_make_start(&cdb_make, fd);

	/* Now go through pkg_dbdir rebuild the cache */

	if ((dir = opendir(pkg_dbdir)) != NULL) {
		while ((portsdir = readdir(dir)) != NULL) {
			if (strcmp(portsdir->d_name, ".") != 0 &&
					strcmp(portsdir->d_name, "..") !=0) {
				strlcpy(contentpath, pkg_dbdir, MAXPATHLEN);
				strlcat(contentpath, "/", MAXPATHLEN);
				strlcat(contentpath, portsdir->d_name, MAXPATHLEN);
				strlcat(contentpath, "/+CONTENTS", MAXPATHLEN);

				if (stat(contentpath, &st) == -1) {
					warn("Unable to read %s informations, skipping:", portsdir->d_name);
					continue;
				}

				if ((content = fopen(contentpath, "r")) == NULL) {
					warn("Unable to read %s file, skipping", contentpath);
					continue;
				}

				content_buffer = malloc(st.st_size + 1);
				fread(content_buffer, st.st_size, 1, content);
				fclose(content);

				manifest_json = cJSON_Parse(content_buffer);
				if (manifest_json == 0)
					manifest_json = pkg_compat_converter(content_buffer);

				if (manifest_json == 0)
					continue; /* skipping */

				nb_packages++;
				snprintf(key, BUFSIZ, "%d_name",nb_packages);
				value = cJSON_GetObjectItem(manifest_json, "name")->valuestring;
				cdb_make_add(&cdb_make, key, strlen(key), value, strlen(value));
				snprintf(key, BUFSIZ, "%d_version", nb_packages);
				value = cJSON_GetObjectItem(manifest_json, "version")->valuestring;
				cdb_make_add(&cdb_make, key, strlen(key), value, strlen(value));
				cJSON_Delete(manifest_json);
			}
		}
	}

	cdb_make_finish(&cdb_make);

	close(fd);
	rename(tmppath, cache_path);
	chmod(cache_path, 0644);
}

void
pkgdb_cache_update()
{
	const char *pkg_dbdir;
	char cache_path[MAXPATHLEN];
	struct stat dir_st, cache_st;
	uid_t uid;

	if ((pkg_dbdir = getenv("PKG_DBDIR")) == NULL)
		pkg_dbdir = PKG_DBDIR;

	uid = getuid();

	if (stat(pkg_dbdir, &dir_st) == -1) {
		if (uid != 0)
			err(EXIT_FAILURE, "%s:", pkg_dbdir);

		if (errno == ENOENT)
			return;
		else
			err(EXIT_FAILURE, "%s:", pkg_dbdir);
	}

	strlcpy(cache_path, pkg_dbdir, MAXPATHLEN);
	strlcat(cache_path, "/pkgdb.cache", MAXPATHLEN);

	if (stat(cache_path, &cache_st) == -1) {
		if (errno == ENOENT) {
			if (uid == 0)
				pkgdb_cache_rebuild(pkg_dbdir, cache_path);
			return;
		} else {
			err(EXIT_FAILURE, "%s:", cache_path);
		}
	}

	if ( dir_st.st_mtime > cache_st.st_mtime )
		if (uid == 0)
			pkgdb_cache_rebuild(pkg_dbdir, cache_path);
}

struct pkg **
pkgdb_cache_list_packages()
{
	/* TODO */
	return NULL;
}
