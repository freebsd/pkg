#include <sys/param.h>

#include <archive.h>
#include <archive_entry.h>
#include <err.h>
#include <stdlib.h>
#include <glob.h>
#include <libgen.h>
#include <string.h>
#include <fcntl.h>

#include "pkg.h"
#include "pkg_private.h"

#define METADATA_GLOB "+{DEINSTALL,INSTALL,MTREE_DIRS}"

static const char * pkg_create_set_format(struct archive *, pkg_formats);
static int pkg_create_from_dir(struct pkg *, const char *, struct archive *);
static int pkg_create_append_buffer(struct archive *, struct archive_entry *, const char *, const char *);

/* TODO: error reporting */

static int
pkg_create_append_buffer(struct archive *a, struct archive_entry *e, const char *buf, const char *name)
{
	if (name == NULL || buf == NULL || buf[0] == '\0' || name[0] == '\0')
		return (-1);

	archive_entry_clear(e);
	archive_entry_set_filetype(e, AE_IFREG);
	archive_entry_set_pathname(e, name);
	archive_entry_set_size(e, strlen(buf));
	archive_write_header(a, e);
	archive_write_data(a, buf, strlen(buf));
	archive_entry_clear(e);

	return (0);

}

static int
pkg_create_from_dir(struct pkg *pkg, const char *root, struct archive *pkg_archive)
{
	struct archive_entry *entry;
	int fd;
	size_t len;
	char buf[BUFSIZ];
	char fpath[MAXPATHLEN];
	struct pkg_file **files;
	struct pkg_script **scripts;
	struct archive *ar;
	char *m;
	int i;
	const char *scriptname = NULL;

	ar = archive_read_disk_new();
	archive_read_disk_set_standard_lookup(ar);

	entry = archive_entry_new();

	pkg_emit_manifest(pkg, &m);

	pkg_create_append_buffer(pkg_archive, entry, m, "+MANIFEST");
	free(m);
	pkg_create_append_buffer(pkg_archive, entry, pkg_get(pkg, PKG_DESC), "+DESC");
	pkg_create_append_buffer(pkg_archive, entry, pkg_get(pkg, PKG_MTREE), "+MTREE_DIRS");

	if ((scripts = pkg_scripts(pkg)) != NULL) {
		for (i = 0; scripts[i] != NULL; i++) {
			switch (pkg_script_type(scripts[i])) {
				case PKG_SCRIPT_PRE_INSTALL:
					scriptname = "+PRE_INSTALL";
					break;
				case PKG_SCRIPT_POST_INSTALL:
					scriptname = "+POST_INSTALL";
					break;
				case PKG_SCRIPT_INSTALL:
					scriptname = "+INSTALL";
					break;
				case PKG_SCRIPT_PRE_DEINSTALL:
					scriptname = "+PRE_DEINSTALL";
					break;
				case PKG_SCRIPT_POST_DEINSTALL:
					scriptname = "+POST_DEINSTALL";
					break;
				case PKG_SCRIPT_DEINSTALL:
					scriptname = "+DEINSTALL";
					break;
				case PKG_SCRIPT_PRE_UPGRADE:
					scriptname = "+PRE_UPGRADE";
					break;
				case PKG_SCRIPT_POST_UPGRADE:
					scriptname = "+POST_UPGRADE";
					break;
				case PKG_SCRIPT_UPGRADE:
					scriptname = "+UPGRADE";
					break;
			}
			pkg_create_append_buffer(pkg_archive, entry, pkg_script_data(scripts[i]), scriptname);
		}
	}

	if ((files = pkg_files(pkg)) != NULL) {
		for (i = 0; files[i] != NULL; i++) {
			if (root != NULL)
				snprintf(fpath, sizeof(MAXPATHLEN), "%s%s", root, pkg_file_path(files[i]));
			else
				strlcpy(fpath, pkg_file_path(files[i]), MAXPATHLEN);

			archive_entry_copy_sourcepath(entry, fpath);

			if (archive_read_disk_entry_from_file(ar, entry, -1, 0) != ARCHIVE_OK)
				warn("archive_read_disk_entry_from_file(%s)", fpath);

			archive_entry_set_pathname(entry, pkg_file_path(files[i]));
			archive_write_header(pkg_archive, entry);
			fd = open(fpath, O_RDONLY);
			if (fd != -1) {
				while ( (len = read(fd, buf, sizeof(buf))) > 0 )
					archive_write_data(pkg_archive, buf, len);

				close(fd);
			}
			archive_entry_clear(entry);
		}
	}

	archive_entry_free(entry);
	archive_read_finish(ar);


	return (0);
}

int
pkg_create(const char *mpath, pkg_formats format, const char *outdir, const char *rootdir, struct pkg *pkg)
{
	char namever[FILENAME_MAX];
	struct archive *pkg_archive;
	char archive_path[MAXPATHLEN];
	const char *ext;

	(void)mpath;

	/* TODO: get stuff from db */

	pkg_archive = archive_write_new();

	if ((ext = pkg_create_set_format(pkg_archive, format)) == NULL) {
		archive_write_finish(pkg_archive);
		warnx("Unsupport format");
		return (-1);
	}

	snprintf(namever, sizeof(namever), "%s-%s", pkg_get(pkg, PKG_NAME), pkg_get(pkg, PKG_VERSION));
	snprintf(archive_path, sizeof(archive_path), "%s/%s.%s", outdir, namever, ext);

	archive_write_set_format_pax_restricted(pkg_archive);
	archive_write_open_filename(pkg_archive, archive_path);

	pkg_create_from_dir(pkg, rootdir, pkg_archive);

	archive_write_close(pkg_archive);
	archive_write_finish(pkg_archive);

	return (EPKG_OK);
}

static const char *
pkg_create_set_format(struct archive *pkg_archive, pkg_formats format)
{
	switch (format) {
		case TAR:
			archive_write_set_compression_none(pkg_archive);
			return ("tar");
		case TGZ:
			if (archive_write_set_compression_gzip(pkg_archive) != ARCHIVE_OK) {
				warnx("gzip compression is not supported, trying plain tar");
				return (pkg_create_set_format(pkg_archive, TAR));
			} else {
				return ("tgz");
			}
		case TBZ:
			if (archive_write_set_compression_bzip2(pkg_archive) != ARCHIVE_OK) {
				warnx("bzip2 compression is not supported, trying gzip");
				return (pkg_create_set_format(pkg_archive, TGZ));
			} else {
				return ("tbz");
			}
		case TXZ:
			if (archive_write_set_compression_xz(pkg_archive) != ARCHIVE_OK) {
				warnx("xz compression is not supported, trying bzip2");
				return (pkg_create_set_format(pkg_archive, TBZ));
			} else {
				return ("txz");
			}
	}
	return (NULL);
}
