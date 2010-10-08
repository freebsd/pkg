#include <sys/stat.h>
#include <sys/param.h>

#include <archive.h>
#include <archive_entry.h>
#include <err.h>
#include <stdlib.h>
#include <glob.h>
#include <libgen.h>
#include <string.h>
#include <fcntl.h>

#include "pkg_manifest.h"
#include "pkgdb.h"
#include "util.h"

#define METADATA_GLOB "+{DEINSTALL,INSTALL,MTREE_DIRS}"

static int pkg_create_from_dir(char *, const char *, struct archive *);

static int
pkg_create_from_dir(char *path, const char *root, struct archive *pkg_archive)
{
	struct archive_entry *entry;
	char glob_pattern[MAXPATHLEN + sizeof(METADATA_GLOB)];
	glob_t g;
	int fd;
	size_t len, i = 0;
	char buf[BUFSIZ];
	char *buffer;
	size_t buffer_len;
	char mpath[MAXPATHLEN];
	char fpath[MAXPATHLEN];
	const char *filepath;
	struct archive *ar;
	struct pkg_manifest *m;

	ar = archive_read_disk_new();
	archive_read_disk_set_standard_lookup(ar);

	snprintf(mpath, sizeof(mpath), "%s+MANIFEST", path);
	entry = archive_entry_new();

	if ((m = pkg_manifest_load_file(mpath)) == NULL)
		errx(EXIT_FAILURE, "Can not continue without manifest");

	/* Add the metadatas */
	snprintf(glob_pattern, sizeof(glob_pattern), "%s"METADATA_GLOB, path);
	
	if (glob(glob_pattern, GLOB_NOSORT|GLOB_BRACE, NULL, &g) == 0) {
		for ( i = 0; i < g.gl_pathc; i++) {
			fd = open(g.gl_pathv[i], O_RDONLY);
			archive_entry_copy_sourcepath(entry, g.gl_pathv[i]);

			if (archive_read_disk_entry_from_file(ar, entry, -1, 0) != ARCHIVE_OK)
				warnx("archive_read_disk_entry_from_file(): %s", archive_error_string(ar));

			archive_entry_set_pathname(entry, basename(g.gl_pathv[i]));
			archive_write_header(pkg_archive, entry);
			while ( (len = read(fd, buf, sizeof(buf))) > 0 )
				archive_write_data(pkg_archive, buf, len);

			close(fd);
			archive_entry_clear(entry);
		}
	}
	globfree(&g);

	/* TODO: remove automatic */
	buffer = pkg_manifest_dump_buffer(m);
	buffer_len = strlen(buffer);

	archive_entry_copy_sourcepath(entry, mpath);
	if (archive_read_disk_entry_from_file(ar, entry, -1, 0) != ARCHIVE_OK)
		warnx("archive_read_disk_entry_form_file(): %s", archive_error_string(ar));

	archive_entry_set_pathname(entry, "+MANIFEST");
	archive_entry_set_size(entry, buffer_len);
	archive_write_header(pkg_archive, entry);
	archive_write_data(pkg_archive, buffer, buffer_len);
	archive_entry_clear(entry);

	free(buffer);

	pkg_manifest_file_init(m);
	while (pkg_manifest_file_next(m) == 0) {
		filepath = pkg_manifest_file_path(m);
		snprintf(fpath, sizeof(MAXPATHLEN), "%s/%s", root, filepath);
		archive_entry_copy_sourcepath(entry, filepath);

		if (archive_read_disk_entry_from_file(ar, entry, -1, 0) != ARCHIVE_OK)
			warnx("archive_read_disk_entry_from_file(): %s", archive_error_string(ar));

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

	pkg_manifest_free(m);
	archive_entry_free(entry);
	archive_read_finish(ar);

	return (0);
}

int
pkg_create(char *pkgname, pkg_formats format, const char *outdir, const char *rootdir)
{
	struct pkgdb db;
	struct pkg pkg;
	const char *pkg_dbdir;
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

	pkg_dbdir = pkgdb_get_dir();

	pkgdb_init(&db, pkgname, MATCH_EXACT);

	if (pkgdb_query(&db, &pkg) != 0) {
		warnx("%s: no such package", pkgname);
		return (-1);
	}

	printf("Creating package %s/%s.%s\n", outdir, pkg_namever(&pkg), ext);
	snprintf(pkgpath, sizeof(pkgpath), "%s/%s/", pkg_dbdir, pkg_namever(&pkg));
	snprintf(archive_path, sizeof(archive_path), "%s/%s.%s", outdir, pkg_namever(&pkg), ext);

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
				warnx("%s", archive_error_string(pkg_archive));
			}
			break;
	}

	archive_write_set_format_pax_restricted(pkg_archive);
	archive_write_open_filename(pkg_archive, archive_path);
	pkg_create_from_dir(pkgpath, rootdir, pkg_archive);
	archive_write_close(pkg_archive);
	archive_write_finish(pkg_archive);

	pkgdb_free(&db);
	return (0);
}
