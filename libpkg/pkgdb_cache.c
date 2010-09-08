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
	char manifestpath[MAXPATHLEN];
	struct cdb_make cdb_make;
	DIR *dir;
	struct dirent *portsdir;
	struct stat st;
	FILE *content, *manifest;
	char *content_buffer = NULL;
	cJSON *manifest_json;
	int nb_packages = 0;
	char *manifest_out;


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

				strlcpy(manifestpath, pkg_dbdir, MAXPATHLEN);
				strlcat(manifestpath, "/", MAXPATHLEN);
				strlcat(manifestpath, portsdir->d_name, MAXPATHLEN);
				strlcat(manifestpath, "/+MANIFEST", MAXPATHLEN);

				if (stat(manifestpath, &st) == -1) {
					warnx("No manifest for %s trying old format", portsdir->d_name);
					strlcpy(contentpath, pkg_dbdir, MAXPATHLEN);
					strlcat(contentpath, "/", MAXPATHLEN);
					strlcat(contentpath, portsdir->d_name, MAXPATHLEN);
					strlcat(contentpath, "/+CONTENTS", MAXPATHLEN);
					if (stat(contentpath, &st) == -1 ) {
						warn("No content for %s, should be corrupted, skipping", portsdir->d_name);
						continue;
					}

					if ((content = fopen(contentpath, "r")) == NULL) {
						warn("Unable to read %s file, skipping", contentpath);
						continue;
					}
					
					content_buffer = malloc(st.st_size + 1);
					fread(content_buffer, st.st_size, 1, content);
					fclose(content);

					manifest_json = pkg_compat_converter(content_buffer);
					
					if (manifest_json == 0) {
						warnx("%s: Manifest corrupted, skipping", portsdir->d_name);
						continue;
					}

					/* writing the manifest to end
					 * conversion */
					manifest_out = cJSON_Print(manifest_json);
					manifest = fopen(manifestpath, "w+");
					fprintf(manifest, "%s", manifest_out);
					fclose(manifest);

				} else {

					if ((content = fopen(manifestpath, "r")) == NULL) {
						warn("Unable to read %s file, skipping", manifestpath);
						continue;
					}

					content_buffer = malloc(st.st_size + 1);
					fread(content_buffer, st.st_size, 1, content);
					fclose(content);

					manifest_json = cJSON_Parse(content_buffer);

					if (manifest_json == 0) {
						warnx("%s: Manifest corrputed, skipping", portsdir->d_name);
						continue;
					}
				}

				nb_packages++;
				snprintf(key, BUFSIZ, "%d_name",nb_packages);
				value = cJSON_GetObjectItem(manifest_json, "name")->valuestring;
				cdb_make_add(&cdb_make, key, strlen(key), value, strlen(value));
				snprintf(key, BUFSIZ, "%d_version", nb_packages);
				value = cJSON_GetObjectItem(manifest_json, "version")->valuestring;
				cdb_make_add(&cdb_make, key, strlen(key), value, strlen(value));

				cJSON_Delete(manifest_json);

				if (content_buffer != NULL)
					free(content_buffer);
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
