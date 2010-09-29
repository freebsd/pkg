#include <err.h>
#include <stdlib.h>
#include <sys/param.h>
#include <archive.h>
#include <archive_entry.h>
#include <pkgdb.h>
#include <glob.h>
#include <sys/stat.h>
#include <libgen.h>
#include <string.h>
#include <fcntl.h>
#include <cJSON.h>
#include "util.h"

#define METADATA_GLOB "+{DEINSTALL,INSTALL,MTREE_DIRS}"

static int pkg_create_from_dir(char *, const char *, struct archive *);

static int
pkg_create_from_dir(char *path, const char *root, struct archive *pkg_archive)
{
	struct archive_entry *entry;
	char glob_pattern[MAXPATHLEN + sizeof(METADATA_GLOB)];
	glob_t g;
	int fd, j;
	size_t len, i = 0;
	char buf[BUFSIZ];
	cJSON *manifest, *files, *file;
	char *buffer;
	char manifestpath[MAXPATHLEN], fpath[MAXPATHLEN];
	char *filepath;
	struct archive *ar;

	ar = archive_read_disk_new();
	archive_read_disk_set_standard_lookup(ar);

	snprintf(manifestpath, sizeof(manifestpath), "%s+MANIFEST", path);
	entry = archive_entry_new();

	if ((file_to_buffer(manifestpath, &buffer)) == -1) {
		/* TODO */
		warnx("fail");
		return (-1);
	}

	if ((manifest = cJSON_Parse(buffer)) == 0) {
		warnx("%s: Manifest corrupted, skipping", manifestpath);
		free(buffer);
		return (-1);
	}

	free(buffer);

	/* Add the metadatas */
	snprintf(glob_pattern, sizeof(glob_pattern), "%s"METADATA_GLOB, path);
	
	if (glob(glob_pattern, GLOB_NOSORT|GLOB_BRACE, NULL, &g) == 0) {
		for ( i = 0; i < g.gl_pathc; i++) {
			fd = open(g.gl_pathv[i], O_RDONLY);
			archive_entry_copy_sourcepath(entry, g.gl_pathv[i]);

			if (archive_read_disk_entry_from_file(ar, entry, -1, 0) != ARCHIVE_OK)
				warnx(archive_error_string(ar));

			archive_entry_set_pathname(entry, basename(g.gl_pathv[i]));
			archive_write_header(pkg_archive, entry);
			while ( (len = read(fd, buf, sizeof(buf))) > 0 )
				archive_write_data(pkg_archive, buf, len);

			close(fd);
			archive_entry_clear(entry);
		}
	}
	globfree(&g);
	cJSON_DeleteItemFromObject(manifest, "automatic");

	buffer = cJSON_Print(manifest);

	archive_entry_copy_sourcepath(entry, manifestpath);
	if (archive_read_disk_entry_from_file(ar, entry, -1, 0) != ARCHIVE_OK)
		warnx(archive_error_string(ar));

	archive_entry_set_pathname(entry, "+MANIFEST");
	archive_entry_set_size(entry, sizeof(buffer));
	archive_write_header(pkg_archive, entry);
	archive_write_data(pkg_archive, buffer, sizeof(buffer));
	archive_entry_clear(entry);

	free(buffer);

	files = cJSON_GetObjectItem(manifest, "files");

	for (j = 0; j < cJSON_GetArraySize(files); j++) {
		file = cJSON_GetArrayItem(files, j);
		filepath = cJSON_GetObjectItem(file, "path")->valuestring;
		snprintf(fpath, sizeof(MAXPATHLEN), "%s/%s", root, filepath);
		archive_entry_copy_sourcepath(entry, filepath);

		if (archive_read_disk_entry_from_file(ar, entry, -1, 0) != ARCHIVE_OK)
			warnx(archive_error_string(ar));

		archive_entry_set_pathname(entry, filepath);
		archive_write_header(pkg_archive, entry);
		fd = open(filepath, O_RDONLY);
		if (fd != -1) {
			while ( (len = read(fd, buf, sizeof(buf))) > 0 )
				archive_write_data(pkg_archive, buf, len);

			close(fd);
		}
		archive_entry_clear(entry);
	}

	cJSON_Delete(manifest);
	archive_entry_free(entry);
	archive_read_finish(ar);

	return (0);
}

int
pkg_create(char *pkgname, pkg_formats format, const char *outdir, const char *rootdir)
{
	struct pkgdb db;
	struct pkg *pkg;
	char *pkgdb_dir;
	char pkgpath[MAXPATHLEN];
	struct archive *pkg_archive;
	char archive_path[MAXPATHLEN];
	const char *ext;

	switch (format) {
		case TAR:
			ext = "tar";
			break;
		case TGZ:
			ext = "tgz";
			break;
		case TBZ:
			ext = "tbz";
			break;
		case TXZ:
			ext = "txz";
			break;
		default:
			warnx("Unsupport format");
			return (-1);
			break; /* NOT REACHED */
	}

	pkgdb_dir = getenv("PKG_DBDIR");

	pkgdb_init(&db, pkgname);

	if (pkgdb_count(&db) == 0) {
		warnx("%s: no such package", pkgname);
		return (-1);
	}

	PKGDB_FOREACH(pkg, &db) {
		printf("Creating package %s/%s.%s\n", outdir, pkg->name_version, ext);
		snprintf(pkgpath, sizeof(pkgpath), "%s/%s/", pkgdb_dir ? pkgdb_dir : PKG_DBDIR, pkg->name_version);
		snprintf(archive_path, sizeof(archive_path), "%s/%s.%s", outdir, pkg->name_version, ext);

		pkg_archive = archive_write_new();

		switch (format) {
			case TAR:
				archive_write_set_compression_none(pkg_archive);
				break;
			case TGZ:
				archive_write_set_compression_gzip(pkg_archive);
				break;
			case TBZ:
				archive_write_set_compression_bzip2(pkg_archive);
				break;
			case TXZ:
				if (archive_write_set_compression_lzma(pkg_archive) != ARCHIVE_OK) {
					warnx(archive_error_string(pkg_archive));
				}
				break;
		}

		archive_write_set_format_pax_restricted(pkg_archive);
		archive_write_open_filename(pkg_archive, archive_path);
		pkg_create_from_dir(pkgpath, rootdir, pkg_archive);
		archive_write_close(pkg_archive);
		archive_write_finish(pkg_archive);
	}
	return (0);
}
