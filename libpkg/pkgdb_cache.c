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

static cJSON *
pkgdb_cache_load_port(const char *pkg_dbdir, char *pkgname)
{
	cJSON *manifest;
	FILE *fs;
	char manifestpath[MAXPATHLEN];
	char *buffer;
	struct stat st;

	strlcpy(manifestpath, pkg_dbdir, MAXPATHLEN);
	strlcat(manifestpath, "/", MAXPATHLEN);
	strlcat(manifestpath, pkgname, MAXPATHLEN);
	strlcat(manifestpath, "/+MANIFEST", MAXPATHLEN);

	if (stat(manifestpath, &st) == -1) {
		warnx("No manifest for %s trying old format", pkgname);
		manifest = pkg_compat_convert_installed( pkg_dbdir, pkgname,
				manifestpath);

		return (manifest);
	}

	if ((fs = fopen(manifestpath, "r")) == NULL) {
		warn("Unable to read %s file, skipping", manifestpath);
		return (0);
	}

	buffer = malloc(st.st_size + 1);
	fread(buffer, st.st_size, 1, fs);
	fclose(fs);

	if ((manifest = cJSON_Parse(buffer)) == 0)
		warnx("%s: Manifest corrputed, skipping", pkgname);

	free(buffer);

	return (manifest);
}

static void
pkgdb_cache_rebuild(const char *pkg_dbdir, const char *cache_path)
{
	int fd;
	char key[BUFSIZ];
	char *value;
	char tmppath[MAXPATHLEN];
	struct cdb_make cdb_make;
	DIR *dir;
	struct dirent *portsdir;
	cJSON *manifest;
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

				if (portsdir->d_type != DT_DIR)
					continue;

				manifest = pkgdb_cache_load_port(pkg_dbdir,
						portsdir->d_name);

				if (manifest == 0)
					continue;

				nb_packages++;
				snprintf(key, BUFSIZ, "%d_name",nb_packages);
				value = cJSON_GetObjectItem(manifest, "name")->valuestring;
				cdb_make_add(&cdb_make, key, strlen(key), value, strlen(value));
				snprintf(key, BUFSIZ, "%d_version", nb_packages);
				value = cJSON_GetObjectItem(manifest, "version")->valuestring;
				cdb_make_add(&cdb_make, key, strlen(key), value, strlen(value));

				cJSON_Delete(manifest);
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
